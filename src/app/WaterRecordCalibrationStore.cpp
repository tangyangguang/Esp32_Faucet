#include "app/WaterRecordCalibrationStore.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kCalibrationMagic = 0x4657434CUL;  // FWCL
constexpr std::uint16_t kCalibrationVersion = 2;

struct CalibrationHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    std::uint32_t capacity;
    std::uint32_t count;
    std::uint32_t oldestIndex;
    std::uint32_t reserved;
};

static_assert(sizeof(CalibrationHeader) == 24, "CalibrationHeader must stay fixed-size");

CalibrationHeader makeHeader(std::size_t capacity, std::size_t count, std::size_t oldestIndex) {
    return CalibrationHeader{
        kCalibrationMagic,
        kCalibrationVersion,
        static_cast<std::uint16_t>(sizeof(WaterRecordCalibration)),
        static_cast<std::uint32_t>(capacity),
        static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(oldestIndex),
        0,
    };
}

}  // namespace

WaterRecordCalibration makeWaterRecordCalibration(const WaterRecord& record) {
    WaterRecordCalibration calibration{};
    calibration.startTime = record.startTime;
    calibration.volumeMl = record.volumeMl;
    calibration.targetValue = record.targetValue;
    calibration.pulseCount = record.pulseCount;
    calibration.rejectedPulseCount = record.rejectedPulseCount;
    calibration.durationSec = record.durationSec;
    calibration.mode = record.mode;
    calibration.result = record.result;
    calibration.selectedPreset = record.selectedPreset;
    return calibration;
}

bool sameWaterRecordCalibrationIdentity(const WaterRecordCalibration& calibration, const WaterRecord& record) {
    return calibration.startTime == record.startTime && calibration.volumeMl == record.volumeMl &&
           calibration.targetValue == record.targetValue && calibration.pulseCount == record.pulseCount &&
           calibration.rejectedPulseCount == record.rejectedPulseCount && calibration.durationSec == record.durationSec &&
           calibration.mode == record.mode && calibration.result == record.result &&
           calibration.selectedPreset == record.selectedPreset;
}

std::size_t WaterRecordCalibrationReader::findAny(const WaterRecord* records,
                                                  std::size_t recordCount,
                                                  WaterRecordCalibration* output,
                                                  bool* found) const {
    if (found) {
        for (std::size_t i = 0; i < recordCount; ++i) {
            found[i] = false;
        }
    }
    if (!records || !output || !found || recordCount == 0 || !ready()) {
        return 0;
    }
    std::size_t matched = 0;
    for (std::size_t i = 0; i < recordCount; ++i) {
        if (find(records[i], output[i])) {
            found[i] = true;
            ++matched;
        }
    }
    return matched;
}

WaterRecordCalibrationStore::WaterRecordCalibrationStore(WaterRecordCalibration* entries, std::size_t capacity)
    : entries_(entries), capacity_(capacity), oldestIndex_(0), count_(0) {}

bool WaterRecordCalibrationStore::upsert(const WaterRecordCalibration& calibration) {
    if (!ready()) {
        return false;
    }
    WaterRecord record{
        calibration.startTime,
        calibration.volumeMl,
        calibration.targetValue,
        calibration.pulseCount,
        calibration.rejectedPulseCount,
        calibration.durationSec,
        calibration.mode,
        calibration.result,
        calibration.selectedPreset,
        0,
        0,
        {0, 0, 0, 0},
    };
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        if (sameWaterRecordCalibrationIdentity(entries_[index], record)) {
            WaterRecordCalibration next = calibration;
            next.calibrationCount = entries_[index].calibrationCount == UINT16_MAX
                                        ? UINT16_MAX
                                        : static_cast<std::uint16_t>(entries_[index].calibrationCount + 1U);
            entries_[index] = next;
            return true;
        }
    }
    WaterRecordCalibration next = calibration;
    if (next.calibrationCount == 0) {
        next.calibrationCount = 1;
    }
    entries_[appendIndex()] = next;
    return true;
}

bool WaterRecordCalibrationStore::find(const WaterRecord& record, WaterRecordCalibration& output) const {
    if (!ready()) {
        return false;
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        if (sameWaterRecordCalibrationIdentity(entries_[index], record)) {
            output = entries_[index];
            return true;
        }
    }
    return false;
}

std::size_t WaterRecordCalibrationStore::findAny(const WaterRecord* records,
                                                 std::size_t recordCount,
                                                 WaterRecordCalibration* output,
                                                 bool* found) const {
    if (found) {
        for (std::size_t i = 0; i < recordCount; ++i) {
            found[i] = false;
        }
    }
    if (!records || !output || !found || recordCount == 0 || !ready()) {
        return 0;
    }
    std::size_t matched = 0;
    for (std::size_t offset = 0; offset < count_ && matched < recordCount; ++offset) {
        const WaterRecordCalibration& candidate = entries_[physicalIndexFromNewestOffset(offset)];
        for (std::size_t i = 0; i < recordCount; ++i) {
            if (found[i] || !sameWaterRecordCalibrationIdentity(candidate, records[i])) {
                continue;
            }
            output[i] = candidate;
            found[i] = true;
            ++matched;
        }
    }
    return matched;
}

std::size_t WaterRecordCalibrationStore::count() const {
    return ready() ? count_ : 0;
}

std::size_t WaterRecordCalibrationStore::capacity() const {
    return capacity_;
}

bool WaterRecordCalibrationStore::ready() const {
    return entries_ && capacity_ > 0;
}

const char* WaterRecordCalibrationStore::storageName() const {
    return ready() ? "ram" : "unavailable";
}

std::size_t WaterRecordCalibrationStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    if (count_ == 0) {
        return 0;
    }
    const std::size_t newest = (oldestIndex_ + count_ - 1U) % capacity_;
    return (newest + capacity_ - (offset % capacity_)) % capacity_;
}

std::size_t WaterRecordCalibrationStore::appendIndex() {
    if (count_ < capacity_) {
        const std::size_t index = (oldestIndex_ + count_) % capacity_;
        ++count_;
        return index;
    }
    const std::size_t index = oldestIndex_;
    oldestIndex_ = (oldestIndex_ + 1U) % capacity_;
    return index;
}

WaterRecordCalibrationFileStore::WaterRecordCalibrationFileStore(WaterRecordFileBackend& backend,
                                                                 const char* path,
                                                                 std::size_t capacity)
    : backend_(backend), path_(path), capacity_(capacity), oldestIndex_(0), count_(0), ready_(false) {}

bool WaterRecordCalibrationFileStore::begin() {
    ready_ = false;
    oldestIndex_ = 0;
    count_ = 0;
    if (!path_ || capacity_ == 0) {
        return false;
    }
    if (!backend_.exists(path_)) {
        return initializeNewFile();
    }
    if (!loadHeader()) {
        return false;
    }
    ready_ = true;
    return true;
}

bool WaterRecordCalibrationFileStore::upsert(const WaterRecordCalibration& calibration) {
    if (!ready()) {
        if (!begin()) {
            return false;
        }
    }
    WaterRecord record{
        calibration.startTime,
        calibration.volumeMl,
        calibration.targetValue,
        calibration.pulseCount,
        calibration.rejectedPulseCount,
        calibration.durationSec,
        calibration.mode,
        calibration.result,
        calibration.selectedPreset,
        0,
        0,
        {0, 0, 0, 0},
    };
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        WaterRecordCalibration existing{};
        if (!readEntry(index, existing)) {
            ready_ = false;
            return false;
        }
        if (!sameWaterRecordCalibrationIdentity(existing, record)) {
            continue;
        }
        WaterRecordCalibration next = calibration;
        next.calibrationCount = existing.calibrationCount == UINT16_MAX
                                    ? UINT16_MAX
                                    : static_cast<std::uint16_t>(existing.calibrationCount + 1U);
        return appendEntry(index, next);
    }

    std::size_t index = 0;
    std::size_t nextOldest = oldestIndex_;
    std::size_t nextCount = count_;
    if (nextCount < capacity_) {
        index = (nextOldest + nextCount) % capacity_;
        ++nextCount;
    } else {
        index = nextOldest;
        nextOldest = (nextOldest + 1U) % capacity_;
    }
    WaterRecordCalibration next = calibration;
    if (next.calibrationCount == 0) {
        next.calibrationCount = 1;
    }
    if (!appendEntry(index, next)) {
        return false;
    }
    oldestIndex_ = nextOldest;
    count_ = nextCount;
    if (!saveHeader()) {
        ready_ = false;
        return false;
    }
    return true;
}

bool WaterRecordCalibrationFileStore::find(const WaterRecord& record, WaterRecordCalibration& output) const {
    if (!ready()) {
        return false;
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        WaterRecordCalibration candidate{};
        if (!readEntry(index, candidate)) {
            return false;
        }
        if (sameWaterRecordCalibrationIdentity(candidate, record)) {
            output = candidate;
            return true;
        }
    }
    return false;
}

std::size_t WaterRecordCalibrationFileStore::findAny(const WaterRecord* records,
                                                     std::size_t recordCount,
                                                     WaterRecordCalibration* output,
                                                     bool* found) const {
    if (found) {
        for (std::size_t i = 0; i < recordCount; ++i) {
            found[i] = false;
        }
    }
    if (!records || !output || !found || recordCount == 0 || !ready()) {
        return 0;
    }
    constexpr std::size_t kBatchSize = 32;
    WaterRecordCalibration* batch = new (std::nothrow) WaterRecordCalibration[kBatchSize]{};
    if (!batch) {
        return WaterRecordCalibrationReader::findAny(records, recordCount, output, found);
    }
    std::size_t matched = 0;
    for (std::size_t offset = 0; offset < count_ && matched < recordCount;) {
        const std::size_t newestIndex = physicalIndexFromNewestOffset(offset);
        const std::size_t batchCount = std::min<std::size_t>({count_ - offset, kBatchSize, newestIndex + 1U});
        const std::size_t firstIndex = newestIndex + 1U - batchCount;
        if (!readEntries(firstIndex, batch, batchCount)) {
            delete[] batch;
            return matched;
        }
        for (std::size_t batchOffset = 0; batchOffset < batchCount && matched < recordCount; ++batchOffset) {
            const WaterRecordCalibration& candidate = batch[batchCount - 1U - batchOffset];
            for (std::size_t i = 0; i < recordCount; ++i) {
                if (found[i] || !sameWaterRecordCalibrationIdentity(candidate, records[i])) {
                    continue;
                }
                output[i] = candidate;
                found[i] = true;
                ++matched;
            }
        }
        offset += batchCount;
    }
    delete[] batch;
    return matched;
}

std::size_t WaterRecordCalibrationFileStore::count() const {
    return ready() ? count_ : 0;
}

std::size_t WaterRecordCalibrationFileStore::capacity() const {
    return capacity_;
}

bool WaterRecordCalibrationFileStore::ready() const {
    if (!ready_ || !path_) {
        return false;
    }
    return backend_.exists(path_);
}

const char* WaterRecordCalibrationFileStore::storageName() const {
    return ready() ? "file" : "unavailable";
}

bool WaterRecordCalibrationFileStore::clear() {
    return initializeNewFile();
}

bool WaterRecordCalibrationFileStore::initializeNewFile() {
    oldestIndex_ = 0;
    count_ = 0;
    ready_ = backend_.createSized(path_, sizeof(CalibrationHeader)) && saveHeader();
    return ready_;
}

bool WaterRecordCalibrationFileStore::loadHeader() {
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(sizeof(CalibrationHeader))) {
        return false;
    }
    CalibrationHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        return false;
    }
    if (header.magic != kCalibrationMagic || header.version != kCalibrationVersion ||
        header.recordSize != sizeof(WaterRecordCalibration) || header.capacity == 0 ||
        header.capacity != capacity_ || header.count > header.capacity || header.oldestIndex >= header.capacity) {
        return false;
    }
    const std::size_t requiredSize =
        sizeof(CalibrationHeader) + static_cast<std::size_t>(header.count) * sizeof(WaterRecordCalibration);
    if (fileSize < static_cast<std::int64_t>(requiredSize)) {
        return false;
    }
    capacity_ = header.capacity;
    count_ = header.count;
    oldestIndex_ = header.oldestIndex;
    return true;
}

bool WaterRecordCalibrationFileStore::saveHeader() const {
    const CalibrationHeader header = makeHeader(capacity_, count_, oldestIndex_);
    return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header));
}

bool WaterRecordCalibrationFileStore::readEntry(std::size_t index, WaterRecordCalibration& output) const {
    if (index >= capacity_) {
        return false;
    }
    return backend_.readAt(path_, entryOffset(index), reinterpret_cast<std::uint8_t*>(&output), sizeof(output));
}

bool WaterRecordCalibrationFileStore::readEntries(std::size_t firstIndex,
                                                  WaterRecordCalibration* output,
                                                  std::size_t count) const {
    if (!output || count == 0 || firstIndex >= capacity_ || count > capacity_ - firstIndex) {
        return false;
    }
    return backend_.readAt(path_,
                           entryOffset(firstIndex),
                           reinterpret_cast<std::uint8_t*>(output),
                           count * sizeof(WaterRecordCalibration));
}

bool WaterRecordCalibrationFileStore::appendEntry(std::size_t index, const WaterRecordCalibration& calibration) {
    if (index >= capacity_) {
        return false;
    }
    const std::size_t offset = entryOffset(index);
    const std::int64_t fileSize = backend_.fileSize(path_);
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(&calibration);
    if (fileSize == static_cast<std::int64_t>(offset)) {
        return backend_.appendBytes(path_, data, sizeof(calibration));
    }
    if (fileSize > static_cast<std::int64_t>(offset)) {
        return backend_.writeAt(path_, offset, data, sizeof(calibration));
    }
    return false;
}

std::size_t WaterRecordCalibrationFileStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    if (count_ == 0) {
        return 0;
    }
    const std::size_t newest = (oldestIndex_ + count_ - 1U) % capacity_;
    return (newest + capacity_ - (offset % capacity_)) % capacity_;
}

std::size_t WaterRecordCalibrationFileStore::entryOffset(std::size_t index) const {
    return sizeof(CalibrationHeader) + index * sizeof(WaterRecordCalibration);
}

}  // namespace faucet
