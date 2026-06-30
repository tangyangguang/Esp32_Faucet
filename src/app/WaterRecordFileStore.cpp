#include "app/WaterRecordFileStore.h"

#include <algorithm>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kRecordMagic = 0x46575244UL;  // FWRD
constexpr std::uint16_t kRecordVersion = 3;
struct RecordHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    std::uint32_t capacity;
    std::uint32_t count;
    std::uint32_t oldestIndex;
    std::uint32_t commitSeq;
    std::uint32_t checksum;
    std::uint32_t reserved;
};

static_assert(sizeof(RecordHeader) == 32, "RecordHeader must stay fixed-size");

bool validPath(const char* path) {
    return path && path[0] == '/';
}

std::uint32_t headerChecksum(RecordHeader header) {
    header.checksum = 0;
    header.reserved = 0;
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&header);
    std::uint32_t hash = 2166136261UL;
    for (std::size_t i = 0; i < sizeof(RecordHeader); ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash == 0 ? 1 : hash;
}

RecordHeader makeHeader(std::size_t capacity, std::size_t count, std::size_t oldestIndex, std::uint32_t commitSeq) {
    RecordHeader header{
        kRecordMagic,
        kRecordVersion,
        static_cast<std::uint16_t>(sizeof(WaterRecord)),
        static_cast<std::uint32_t>(capacity),
        static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(oldestIndex),
        commitSeq == 0 ? 1U : commitSeq,
        0,
        0,
    };
    header.checksum = headerChecksum(header);
    return header;
}

bool validHeaderFields(const RecordHeader& header, std::size_t expectedCapacity) {
    return header.magic == kRecordMagic && header.version == kRecordVersion &&
           header.recordSize == sizeof(WaterRecord) && header.capacity != 0 &&
           header.capacity == expectedCapacity && header.count <= header.capacity &&
           header.oldestIndex < header.capacity && header.commitSeq != 0;
}

bool validHeader(const RecordHeader& header, std::size_t expectedCapacity) {
    if (!validHeaderFields(header, expectedCapacity)) {
        return false;
    }
    return header.checksum == headerChecksum(header);
}

}  // namespace

WaterRecordFileStore::WaterRecordFileStore(WaterRecordFileBackend& backend, const char* path, std::size_t capacity)
    : backend_(backend),
      path_(path),
      capacity_(capacity),
      oldestIndex_(0),
      count_(0),
      commitSeq_(0),
      ready_(false),
      status_(WaterRecordFileStatus::Unavailable) {}

bool WaterRecordFileStore::begin() {
    ready_ = false;
    status_ = WaterRecordFileStatus::Unavailable;
    if (!validPath(path_) || capacity_ == 0 || capacity_ > UINT32_MAX) {
        status_ = !validPath(path_) ? WaterRecordFileStatus::InvalidPath : WaterRecordFileStatus::InvalidCapacity;
        return false;
    }

    if (!backend_.exists(path_)) {
        return initializeNewFile();
    }

    if (!loadHeader()) {
        return false;
    }

    ready_ = true;
    status_ = WaterRecordFileStatus::Ready;
    return true;
}

bool WaterRecordFileStore::append(const WaterRecord& record) {
    if (!ready_) {
        if (validPath(path_) && !backend_.exists(path_)) {
            if (!initializeNewFile()) {
                return false;
            }
        } else {
            status_ = WaterRecordFileStatus::Unavailable;
            return false;
        }
    }
    if (!backend_.exists(path_) && !initializeNewFile()) {
        status_ = WaterRecordFileStatus::BackendFailure;
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
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }
    const std::size_t oldCount = count_;
    const std::size_t oldOldestIndex = oldestIndex_;
    count_ = nextCount;
    oldestIndex_ = nextOldestIndex;
    if (saveHeader()) {
        status_ = WaterRecordFileStatus::Ready;
        return true;
    }
    count_ = oldCount;
    oldestIndex_ = oldOldestIndex;
    status_ = WaterRecordFileStatus::BackendFailure;
    return false;
}

std::size_t WaterRecordFileStore::rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec) {
    if (!ready() || bootId == 0) {
        return 0;
    }
    std::size_t changed = 0;
    for (std::size_t offset = 0; offset < count_; ++offset) {
        const std::size_t index = physicalIndexFromNewestOffset(offset);
        WaterRecord record{};
        if (!backend_.readAt(path_, recordOffset(index), reinterpret_cast<std::uint8_t*>(&record), sizeof(record))) {
            return changed;
        }
        if (!waterRecordHasBootRelativeTime(record) || waterRecordBootId(record) != bootId) {
            continue;
        }
        record.startTime = bootStartRealSec + record.startTime;
        clearWaterRecordBootId(record);
        if (!backend_.writeAt(path_, recordOffset(index), reinterpret_cast<const std::uint8_t*>(&record), sizeof(record))) {
            return changed;
        }
        ++changed;
    }
    return changed;
}

std::size_t WaterRecordFileStore::readPage(std::size_t pageIndex,
                                        std::uint16_t pageSize,
                                        WaterRecord* output,
                                        std::size_t outputCapacity) const {
    if (!ready_ || !output || outputCapacity == 0 || count_ == 0) {
        return 0;
    }
    if (!ready()) {
        return 0;
    }

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(pageSize);
    const std::size_t startOffset = pageIndex * static_cast<std::size_t>(sanitizedPageSize);
    if (startOffset >= count_) {
        return 0;
    }

    const std::size_t available = count_ - startOffset;
    const std::size_t limit = std::min<std::size_t>({available, sanitizedPageSize, outputCapacity});
    std::size_t copied = 0;
    while (copied < limit) {
        const std::size_t newestOffset = startOffset + copied;
        const std::size_t newestIndex = physicalIndexFromNewestOffset(newestOffset);
        const std::size_t batch = std::min<std::size_t>(limit - copied, newestIndex + 1);
        const std::size_t firstIndex = newestIndex + 1 - batch;
        if (!readRecordSpan(firstIndex, output + copied, batch)) {
            return copied;
        }
        std::reverse(output + copied, output + copied + batch);
        copied += batch;
    }
    return limit;
}

bool WaterRecordFileStore::clear() {
    if (!ready()) {
        return false;
    }
    oldestIndex_ = 0;
    count_ = 0;
    return saveHeader();
}

std::size_t WaterRecordFileStore::count() const {
    return ready() ? count_ : 0;
}

bool WaterRecordFileStore::ready() const {
    return ready_ && status_ == WaterRecordFileStatus::Ready && backend_.exists(path_);
}

const char* WaterRecordFileStore::storageName() const {
    return ready() ? "file" : "unavailable";
}

WaterRecordFileStatus WaterRecordFileStore::status() const {
    if (validPath(path_) && !backend_.exists(path_)) {
        return WaterRecordFileStatus::Missing;
    }
    return status_;
}

bool WaterRecordFileStore::initializeNewFile() {
    oldestIndex_ = 0;
    count_ = 0;
    commitSeq_ = 2;
    const RecordHeader primary = makeHeader(capacity_, count_, oldestIndex_, 1);
    const RecordHeader secondary = makeHeader(capacity_, count_, oldestIndex_, commitSeq_);
    ready_ = backend_.appendBytes(path_, reinterpret_cast<const std::uint8_t*>(&primary), sizeof(primary)) &&
             backend_.appendBytes(path_, reinterpret_cast<const std::uint8_t*>(&secondary), sizeof(secondary));
    status_ = ready_ ? WaterRecordFileStatus::Ready : WaterRecordFileStatus::BackendFailure;
    return ready_;
}

bool WaterRecordFileStore::loadHeader() {
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(2 * sizeof(RecordHeader))) {
        status_ = WaterRecordFileStatus::Corrupt;
        return false;
    }
    if (fileSize > static_cast<std::int64_t>(fileSizeBytes())) {
        status_ = WaterRecordFileStatus::IncompatibleFormat;
        return false;
    }

    RecordHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }

    const bool primaryValid = validHeader(header, capacity_);
    RecordHeader backup{};
    if (!backend_.readAt(path_, backupHeaderOffset(), reinterpret_cast<std::uint8_t*>(&backup), sizeof(backup))) {
        status_ = WaterRecordFileStatus::BackendFailure;
        return false;
    }
    const bool backupValid = validHeader(backup, capacity_);

    if (!primaryValid && !backupValid) {
        status_ = (header.magic != kRecordMagic || header.version != kRecordVersion ||
                   header.recordSize != sizeof(WaterRecord) || header.capacity != capacity_)
                      ? WaterRecordFileStatus::IncompatibleFormat
                      : WaterRecordFileStatus::Corrupt;
        return false;
    }

    const RecordHeader& selected =
        primaryValid && (!backupValid || header.commitSeq >= backup.commitSeq) ? header : backup;
    const std::size_t requiredSize =
        2 * sizeof(RecordHeader) + static_cast<std::size_t>(selected.count) * sizeof(WaterRecord);
    if (fileSize < static_cast<std::int64_t>(requiredSize)) {
        status_ = WaterRecordFileStatus::Corrupt;
        return false;
    }

    capacity_ = selected.capacity;
    count_ = selected.count;
    oldestIndex_ = selected.oldestIndex;
    commitSeq_ = selected.commitSeq;
    status_ = WaterRecordFileStatus::Ready;
    return true;
}

bool WaterRecordFileStore::saveHeader() {
    const std::uint32_t nextSeq = commitSeq_ + 1U == 0 ? 1U : commitSeq_ + 1U;
    const RecordHeader header = makeHeader(capacity_, count_, oldestIndex_, nextSeq);
    const std::size_t offset = (nextSeq % 2U) == 0 ? backupHeaderOffset() : 0;
    if (!backend_.writeAt(path_, offset, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header))) {
        return false;
    }
    commitSeq_ = nextSeq;
    return true;
}

bool WaterRecordFileStore::appendRecord(std::size_t index, const WaterRecord& record) {
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

bool WaterRecordFileStore::readRecordSpan(std::size_t firstIndex, WaterRecord* output, std::size_t count) const {
    if (!output || count == 0 || firstIndex >= capacity_ || count > capacity_ - firstIndex) {
        return false;
    }
    return backend_.readAt(path_,
                           recordOffset(firstIndex),
                           reinterpret_cast<std::uint8_t*>(output),
                           count * sizeof(WaterRecord));
}

std::size_t WaterRecordFileStore::fileSizeBytes() const {
    return 2 * sizeof(RecordHeader) + capacity_ * sizeof(WaterRecord);
}

std::size_t WaterRecordFileStore::backupHeaderOffset() const {
    return sizeof(RecordHeader);
}

std::size_t WaterRecordFileStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    return (oldestIndex_ + count_ - 1 - offset) % capacity_;
}

std::size_t WaterRecordFileStore::recordOffset(std::size_t index) const {
    return 2 * sizeof(RecordHeader) + index * sizeof(WaterRecord);
}

}  // namespace faucet
