#include "app/WaterLogFileStore.h"

#include <algorithm>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kLogMagic = 0x464C5746UL;  // FWLF
constexpr std::uint16_t kLogVersion = 1;

struct LogHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    std::uint32_t capacity;
    std::uint32_t count;
    std::uint32_t oldestIndex;
    std::uint32_t reserved;
};

static_assert(sizeof(LogHeader) == 24, "LogHeader must stay fixed-size");

bool validPath(const char* path) {
    return path && path[0] == '/';
}

LogHeader makeHeader(std::size_t capacity, std::size_t count, std::size_t oldestIndex) {
    return LogHeader{
        kLogMagic,
        kLogVersion,
        static_cast<std::uint16_t>(sizeof(WaterLogRecord)),
        static_cast<std::uint32_t>(capacity),
        static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(oldestIndex),
        0,
    };
}

}  // namespace

WaterLogFileStore::WaterLogFileStore(WaterLogFileBackend& backend, const char* path, std::size_t capacity)
    : backend_(backend),
      path_(path),
      capacity_(capacity),
      oldestIndex_(0),
      count_(0),
      ready_(false) {}

bool WaterLogFileStore::begin() {
    ready_ = false;
    if (!validPath(path_) || capacity_ == 0 || capacity_ > UINT32_MAX) {
        return false;
    }

    if (!backend_.exists(path_)) {
        return initializeNewFile();
    }

    if (!loadHeader()) {
        backend_.removeFile(path_);
        return initializeNewFile();
    }

    ready_ = true;
    return true;
}

bool WaterLogFileStore::append(const WaterLogRecord& record) {
    if (!ready_) {
        return false;
    }

    std::size_t writeIndex = 0;
    std::size_t nextCount = count_;
    std::size_t nextOldestIndex = oldestIndex_;
    if (count_ < capacity_) {
        writeIndex = (oldestIndex_ + count_) % capacity_;
        ++nextCount;
    } else {
        writeIndex = oldestIndex_;
        nextOldestIndex = (oldestIndex_ + 1) % capacity_;
    }

    if (!appendRecord(writeIndex, record)) {
        return false;
    }
    const std::size_t oldCount = count_;
    const std::size_t oldOldestIndex = oldestIndex_;
    count_ = nextCount;
    oldestIndex_ = nextOldestIndex;
    if (saveHeader()) {
        return true;
    }
    count_ = oldCount;
    oldestIndex_ = oldOldestIndex;
    return false;
}

std::size_t WaterLogFileStore::rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec) {
    if (!ready_ || bootId == 0) {
        return 0;
    }
    std::size_t changed = 0;
    for (std::size_t offset = 0; offset < count_; ++offset) {
        const std::size_t index = physicalIndexFromNewestOffset(offset);
        WaterLogRecord record{};
        if (!backend_.readAt(path_, recordOffset(index), reinterpret_cast<std::uint8_t*>(&record), sizeof(record))) {
            return changed;
        }
        if (!waterLogHasBootRelativeTime(record) || waterLogBootId(record) != bootId) {
            continue;
        }
        record.startTime = bootStartRealSec + record.startTime;
        clearWaterLogBootId(record);
        if (!backend_.writeAt(path_, recordOffset(index), reinterpret_cast<const std::uint8_t*>(&record), sizeof(record))) {
            return changed;
        }
        ++changed;
    }
    return changed;
}

std::size_t WaterLogFileStore::readPage(std::size_t pageIndex,
                                        std::uint16_t pageSize,
                                        WaterLogRecord* output,
                                        std::size_t outputCapacity) const {
    if (!ready_ || !output || outputCapacity == 0 || count_ == 0) {
        return 0;
    }

    const std::uint16_t sanitizedPageSize = sanitizeLogPageSize(pageSize);
    const std::size_t startOffset = pageIndex * static_cast<std::size_t>(sanitizedPageSize);
    if (startOffset >= count_) {
        return 0;
    }

    const std::size_t available = count_ - startOffset;
    const std::size_t limit = std::min<std::size_t>({available, sanitizedPageSize, outputCapacity});
    for (std::size_t i = 0; i < limit; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(startOffset + i);
        if (!backend_.readAt(path_,
                             recordOffset(index),
                             reinterpret_cast<std::uint8_t*>(&output[i]),
                             sizeof(output[i]))) {
            return i;
        }
    }
    return limit;
}

bool WaterLogFileStore::clear() {
    if (!ready_) {
        return false;
    }
    oldestIndex_ = 0;
    count_ = 0;
    return saveHeader();
}

std::size_t WaterLogFileStore::count() const {
    return count_;
}

std::size_t WaterLogFileStore::capacity() const {
    return capacity_;
}

bool WaterLogFileStore::ready() const {
    return ready_;
}

const char* WaterLogFileStore::storageName() const {
    return ready_ ? "file" : "unavailable";
}

bool WaterLogFileStore::initializeNewFile() {
    oldestIndex_ = 0;
    count_ = 0;
    ready_ = backend_.createSized(path_, sizeof(LogHeader)) && saveHeader();
    return ready_;
}

bool WaterLogFileStore::loadHeader() {
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(sizeof(LogHeader)) ||
        fileSize > static_cast<std::int64_t>(fileSizeBytes())) {
        return false;
    }

    LogHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        return false;
    }

    if (header.magic != kLogMagic || header.version != kLogVersion ||
        header.recordSize != sizeof(WaterLogRecord) || header.capacity == 0 ||
        header.count > header.capacity || header.oldestIndex >= header.capacity) {
        return false;
    }

    const std::size_t requiredSize = sizeof(LogHeader) + static_cast<std::size_t>(header.count) * sizeof(WaterLogRecord);
    if (fileSize < static_cast<std::int64_t>(requiredSize)) {
        return false;
    }

    capacity_ = header.capacity;
    count_ = header.count;
    oldestIndex_ = header.oldestIndex;
    return true;
}

bool WaterLogFileStore::saveHeader() const {
    const LogHeader header = makeHeader(capacity_, count_, oldestIndex_);
    return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header));
}

bool WaterLogFileStore::appendRecord(std::size_t index, const WaterLogRecord& record) {
    const std::size_t offset = recordOffset(index);
    const std::int64_t fileSize = backend_.fileSize(path_);
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(&record);

    if (fileSize == static_cast<std::int64_t>(offset)) {
        return backend_.appendBytes(path_, data, sizeof(record));
    }
    if (fileSize > static_cast<std::int64_t>(offset)) {
        return backend_.writeAt(path_, offset, data, sizeof(record));
    }
    return false;
}

std::size_t WaterLogFileStore::fileSizeBytes() const {
    return sizeof(LogHeader) + capacity_ * sizeof(WaterLogRecord);
}

std::size_t WaterLogFileStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    return (oldestIndex_ + count_ - 1 - offset) % capacity_;
}

std::size_t WaterLogFileStore::recordOffset(std::size_t index) const {
    return sizeof(LogHeader) + index * sizeof(WaterLogRecord);
}

}  // namespace faucet
