#include "app/WaterRecordMeteringSnapshotStore.h"

#include <algorithm>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kSnapshotMagic = 0x46574D53UL;  // FWMS
constexpr std::uint16_t kSnapshotVersion = 1;

struct SnapshotHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t recordSize;
    std::uint32_t capacity;
    std::uint32_t count;
    std::uint32_t oldestIndex;
    std::uint32_t reserved;
};

static_assert(sizeof(SnapshotHeader) == 24, "SnapshotHeader must stay fixed-size");

SnapshotHeader makeHeader(std::size_t capacity, std::size_t count, std::size_t oldestIndex) {
    return SnapshotHeader{
        kSnapshotMagic,
        kSnapshotVersion,
        static_cast<std::uint16_t>(sizeof(WaterRecordMeteringSnapshot)),
        static_cast<std::uint32_t>(capacity),
        static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(oldestIndex),
        0,
    };
}

WaterRecord recordFromSnapshot(const WaterRecordMeteringSnapshot& snapshot) {
    return WaterRecord{
        snapshot.startTime,
        snapshot.volumeMl,
        snapshot.targetValue,
        snapshot.pulseCount,
        snapshot.rejectedPulseCount,
        snapshot.durationSec,
        snapshot.mode,
        snapshot.result,
        snapshot.selectedPreset,
        0,
        snapshot.pulsePerMlAtRun,
        {0, 0, 0, 0},
    };
}

}  // namespace

WaterRecordMeteringSnapshot makeWaterRecordMeteringSnapshot(const WaterRecord& record) {
    WaterRecordMeteringSnapshot snapshot{};
    snapshot.startTime = record.startTime;
    snapshot.volumeMl = record.volumeMl;
    snapshot.targetValue = record.targetValue;
    snapshot.pulseCount = record.pulseCount;
    snapshot.rejectedPulseCount = record.rejectedPulseCount;
    snapshot.durationSec = record.durationSec;
    snapshot.mode = record.mode;
    snapshot.result = record.result;
    snapshot.selectedPreset = record.selectedPreset;
    snapshot.pulsePerMlAtRun = record.pulsePerMlAtRun;
    return snapshot;
}

bool sameWaterRecordMeteringSnapshotIdentity(const WaterRecordMeteringSnapshot& snapshot,
                                             const WaterRecord& record) {
    return snapshot.startTime == record.startTime && snapshot.volumeMl == record.volumeMl &&
           snapshot.targetValue == record.targetValue && snapshot.pulseCount == record.pulseCount &&
           snapshot.rejectedPulseCount == record.rejectedPulseCount && snapshot.durationSec == record.durationSec &&
           snapshot.mode == record.mode && snapshot.result == record.result &&
           snapshot.selectedPreset == record.selectedPreset;
}

std::size_t WaterRecordMeteringSnapshotReader::findAny(const WaterRecord* records,
                                                       std::size_t recordCount,
                                                       WaterRecordMeteringSnapshot* output,
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

WaterRecordMeteringSnapshotStore::WaterRecordMeteringSnapshotStore(WaterRecordMeteringSnapshot* entries,
                                                                   std::size_t capacity)
    : entries_(entries), capacity_(capacity), oldestIndex_(0), count_(0) {}

bool WaterRecordMeteringSnapshotStore::upsert(const WaterRecordMeteringSnapshot& snapshot) {
    if (!ready()) {
        return false;
    }
    const WaterRecord record = recordFromSnapshot(snapshot);
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        if (sameWaterRecordMeteringSnapshotIdentity(entries_[index], record)) {
            entries_[index] = snapshot;
            return true;
        }
    }
    entries_[appendIndex()] = snapshot;
    return true;
}

bool WaterRecordMeteringSnapshotStore::find(const WaterRecord& record,
                                            WaterRecordMeteringSnapshot& output) const {
    if (!ready()) {
        return false;
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        if (sameWaterRecordMeteringSnapshotIdentity(entries_[index], record)) {
            output = entries_[index];
            return true;
        }
    }
    return false;
}

std::size_t WaterRecordMeteringSnapshotStore::findAny(const WaterRecord* records,
                                                      std::size_t recordCount,
                                                      WaterRecordMeteringSnapshot* output,
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
        const WaterRecordMeteringSnapshot& candidate = entries_[physicalIndexFromNewestOffset(offset)];
        for (std::size_t i = 0; i < recordCount; ++i) {
            if (found[i] || !sameWaterRecordMeteringSnapshotIdentity(candidate, records[i])) {
                continue;
            }
            output[i] = candidate;
            found[i] = true;
            ++matched;
        }
    }
    return matched;
}

std::size_t WaterRecordMeteringSnapshotStore::count() const {
    return ready() ? count_ : 0;
}

std::size_t WaterRecordMeteringSnapshotStore::capacity() const {
    return capacity_;
}

bool WaterRecordMeteringSnapshotStore::ready() const {
    return entries_ && capacity_ > 0;
}

const char* WaterRecordMeteringSnapshotStore::storageName() const {
    return ready() ? "ram" : "unavailable";
}

std::size_t WaterRecordMeteringSnapshotStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    if (count_ == 0) {
        return 0;
    }
    const std::size_t newest = (oldestIndex_ + count_ - 1U) % capacity_;
    return (newest + capacity_ - (offset % capacity_)) % capacity_;
}

std::size_t WaterRecordMeteringSnapshotStore::appendIndex() {
    if (count_ < capacity_) {
        const std::size_t index = (oldestIndex_ + count_) % capacity_;
        ++count_;
        return index;
    }
    const std::size_t index = oldestIndex_;
    oldestIndex_ = (oldestIndex_ + 1U) % capacity_;
    return index;
}

WaterRecordMeteringSnapshotFileStore::WaterRecordMeteringSnapshotFileStore(WaterRecordFileBackend& backend,
                                                                           const char* path,
                                                                           std::size_t capacity)
    : backend_(backend), path_(path), capacity_(capacity), oldestIndex_(0), count_(0), ready_(false) {}

bool WaterRecordMeteringSnapshotFileStore::begin() {
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
        return initializeNewFile();
    }
    ready_ = true;
    return true;
}

bool WaterRecordMeteringSnapshotFileStore::upsert(const WaterRecordMeteringSnapshot& snapshot) {
    if (!ready() && !begin()) {
        return false;
    }
    const WaterRecord record = recordFromSnapshot(snapshot);
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        WaterRecordMeteringSnapshot existing{};
        if (!readEntry(index, existing)) {
            ready_ = false;
            return false;
        }
        if (sameWaterRecordMeteringSnapshotIdentity(existing, record)) {
            return appendEntry(index, snapshot);
        }
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
    if (!appendEntry(index, snapshot)) {
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

bool WaterRecordMeteringSnapshotFileStore::find(const WaterRecord& record,
                                                WaterRecordMeteringSnapshot& output) const {
    if (!ready()) {
        return false;
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const std::size_t index = physicalIndexFromNewestOffset(i);
        WaterRecordMeteringSnapshot candidate{};
        if (!readEntry(index, candidate)) {
            return false;
        }
        if (sameWaterRecordMeteringSnapshotIdentity(candidate, record)) {
            output = candidate;
            return true;
        }
    }
    return false;
}

std::size_t WaterRecordMeteringSnapshotFileStore::findAny(const WaterRecord* records,
                                                          std::size_t recordCount,
                                                          WaterRecordMeteringSnapshot* output,
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
        const std::size_t index = physicalIndexFromNewestOffset(offset);
        WaterRecordMeteringSnapshot candidate{};
        if (!readEntry(index, candidate)) {
            return matched;
        }
        for (std::size_t i = 0; i < recordCount; ++i) {
            if (found[i] || !sameWaterRecordMeteringSnapshotIdentity(candidate, records[i])) {
                continue;
            }
            output[i] = candidate;
            found[i] = true;
            ++matched;
        }
    }
    return matched;
}

std::size_t WaterRecordMeteringSnapshotFileStore::count() const {
    return ready() ? count_ : 0;
}

std::size_t WaterRecordMeteringSnapshotFileStore::capacity() const {
    return capacity_;
}

bool WaterRecordMeteringSnapshotFileStore::ready() const {
    return ready_ && path_ && backend_.exists(path_);
}

const char* WaterRecordMeteringSnapshotFileStore::storageName() const {
    return ready() ? "file" : "unavailable";
}

bool WaterRecordMeteringSnapshotFileStore::initializeNewFile() {
    oldestIndex_ = 0;
    count_ = 0;
    ready_ = backend_.createSized(path_, sizeof(SnapshotHeader)) && saveHeader();
    return ready_;
}

bool WaterRecordMeteringSnapshotFileStore::loadHeader() {
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(sizeof(SnapshotHeader))) {
        return false;
    }
    SnapshotHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        return false;
    }
    if (header.magic != kSnapshotMagic || header.version != kSnapshotVersion ||
        header.recordSize != sizeof(WaterRecordMeteringSnapshot) || header.capacity == 0 ||
        header.capacity != capacity_ || header.count > header.capacity ||
        header.oldestIndex >= header.capacity) {
        return false;
    }
    const std::size_t requiredSize =
        sizeof(SnapshotHeader) + static_cast<std::size_t>(header.count) * sizeof(WaterRecordMeteringSnapshot);
    if (fileSize < static_cast<std::int64_t>(requiredSize)) {
        return false;
    }
    capacity_ = header.capacity;
    count_ = header.count;
    oldestIndex_ = header.oldestIndex;
    return true;
}

bool WaterRecordMeteringSnapshotFileStore::saveHeader() const {
    const SnapshotHeader header = makeHeader(capacity_, count_, oldestIndex_);
    return backend_.writeAt(path_, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header));
}

bool WaterRecordMeteringSnapshotFileStore::readEntry(std::size_t index,
                                                     WaterRecordMeteringSnapshot& output) const {
    if (!ready() || index >= capacity_) {
        return false;
    }
    return backend_.readAt(path_,
                           entryOffset(index),
                           reinterpret_cast<std::uint8_t*>(&output),
                           sizeof(output));
}

bool WaterRecordMeteringSnapshotFileStore::appendEntry(std::size_t index,
                                                       const WaterRecordMeteringSnapshot& snapshot) {
    if (!ready_ || index >= capacity_) {
        return false;
    }
    const std::size_t offset = entryOffset(index);
    const std::int64_t fileSize = backend_.fileSize(path_);
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(&snapshot);
    if (fileSize == static_cast<std::int64_t>(offset)) {
        return backend_.appendBytes(path_, data, sizeof(snapshot));
    }
    if (fileSize > static_cast<std::int64_t>(offset)) {
        return backend_.writeAt(path_, offset, data, sizeof(snapshot));
    }
    return false;
}

std::size_t WaterRecordMeteringSnapshotFileStore::physicalIndexFromNewestOffset(std::size_t offset) const {
    if (count_ == 0) {
        return 0;
    }
    const std::size_t newest = (oldestIndex_ + count_ - 1U) % capacity_;
    return (newest + capacity_ - (offset % capacity_)) % capacity_;
}

std::size_t WaterRecordMeteringSnapshotFileStore::entryOffset(std::size_t index) const {
    return sizeof(SnapshotHeader) + index * sizeof(WaterRecordMeteringSnapshot);
}

}  // namespace faucet
