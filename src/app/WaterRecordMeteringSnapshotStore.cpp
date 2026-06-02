#include "app/WaterRecordMeteringSnapshotStore.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kSnapshotMagic = 0x46574D53UL;  // FWMS
constexpr std::uint16_t kSnapshotVersion = 2;
constexpr std::uint16_t kLegacySnapshotVersion = 1;
constexpr std::size_t kMigrationCopyChunkSize = 256;

struct LegacyMeteringParametersV1 {
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t stablePulsePerLiter;
};

struct WaterRecordMeteringSnapshotV1 {
    std::uint32_t startTime;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint32_t pulseCount;
    std::uint32_t rejectedPulseCount;
    std::uint16_t durationSec;
    WaterMode mode;
    WaterResult result;
    std::uint8_t selectedPreset;
    std::uint8_t reserved0;
    float pulsePerMlAtRun;
    std::uint32_t meteringSchemeId;
    std::uint32_t meteringSchemeRevision;
    LegacyMeteringParametersV1 params;
    std::uint8_t reserved[4];
};

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

bool tempPathFor(const char* path, char* out, std::size_t len) {
    if (!path || !out || len == 0) {
        return false;
    }
    const int written = std::snprintf(out, len, "%s.tmp", path);
    return written > 0 && static_cast<std::size_t>(written) < len;
}

std::size_t requiredV2Size(const SnapshotHeader& header) {
    return sizeof(SnapshotHeader) + static_cast<std::size_t>(header.count) * sizeof(WaterRecordMeteringSnapshot);
}

bool validV2HeaderForFile(const SnapshotHeader& header, std::size_t expectedCapacity, std::int64_t fileSize) {
    return header.magic == kSnapshotMagic &&
           header.version == kSnapshotVersion &&
           header.recordSize == sizeof(WaterRecordMeteringSnapshot) &&
           header.capacity == expectedCapacity &&
           header.count <= header.capacity &&
           header.oldestIndex < header.capacity &&
           fileSize >= static_cast<std::int64_t>(requiredV2Size(header));
}

bool copyFileBytes(WaterRecordFileBackend& backend, const char* from, const char* to, std::size_t size) {
    if (!from || !to || !backend.createSized(to, size)) {
        return false;
    }
    std::uint8_t buffer[kMigrationCopyChunkSize]{};
    for (std::size_t offset = 0; offset < size; offset += sizeof(buffer)) {
        const std::size_t chunk = std::min<std::size_t>(sizeof(buffer), size - offset);
        if (!backend.readAt(from, offset, buffer, chunk) || !backend.writeAt(to, offset, buffer, chunk)) {
            return false;
        }
    }
    return true;
}

bool writeV2SnapshotFile(WaterRecordFileBackend& backend,
                         const char* path,
                         const SnapshotHeader& header,
                         const WaterRecordMeteringSnapshot* migrated) {
    if (!path || (header.count > 0 && !migrated) || !backend.createSized(path, sizeof(SnapshotHeader))) {
        return false;
    }
    if (!backend.writeAt(path, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header))) {
        return false;
    }
    for (std::size_t i = 0; i < header.count; ++i) {
        if (!backend.appendBytes(path,
                                 reinterpret_cast<const std::uint8_t*>(&migrated[i]),
                                 sizeof(WaterRecordMeteringSnapshot))) {
            return false;
        }
    }
    return true;
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

MeteringParameters expandLegacyParams(LegacyMeteringParametersV1 params) {
    return MeteringParameters{
        params.startupPulseCount,
        params.startupVolumeMl,
        params.stablePulsePerLiter,
    };
}

WaterRecordMeteringSnapshot expandLegacySnapshot(const WaterRecordMeteringSnapshotV1& legacy) {
    WaterRecordMeteringSnapshot snapshot{};
    snapshot.startTime = legacy.startTime;
    snapshot.volumeMl = legacy.volumeMl;
    snapshot.targetValue = legacy.targetValue;
    snapshot.pulseCount = legacy.pulseCount;
    snapshot.rejectedPulseCount = legacy.rejectedPulseCount;
    snapshot.durationSec = legacy.durationSec;
    snapshot.mode = legacy.mode;
    snapshot.result = legacy.result;
    snapshot.selectedPreset = legacy.selectedPreset;
    snapshot.pulsePerMlAtRun = legacy.pulsePerMlAtRun;
    snapshot.meteringSchemeId = legacy.meteringSchemeId;
    snapshot.meteringSchemeRevision = legacy.meteringSchemeRevision;
    snapshot.params = expandLegacyParams(legacy.params);
    std::memcpy(snapshot.reserved, legacy.reserved, sizeof(snapshot.reserved));
    return snapshot;
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
        return false;
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

bool WaterRecordMeteringSnapshotFileStore::migrateV1File() {
    const std::int64_t fileSize = backend_.fileSize(path_);
    SnapshotHeader header{};
    if (fileSize < static_cast<std::int64_t>(sizeof(header)) ||
        !backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header)) ||
        header.magic != kSnapshotMagic ||
        header.version != kLegacySnapshotVersion ||
        header.recordSize != sizeof(WaterRecordMeteringSnapshotV1) ||
        header.capacity == 0 ||
        header.capacity != capacity_ ||
        header.count > header.capacity ||
        header.oldestIndex >= header.capacity) {
        return false;
    }
    const std::size_t requiredSize =
        sizeof(SnapshotHeader) + static_cast<std::size_t>(header.count) * sizeof(WaterRecordMeteringSnapshotV1);
    if (fileSize < static_cast<std::int64_t>(requiredSize)) {
        return false;
    }
    WaterRecordMeteringSnapshot* migrated =
        new (std::nothrow) WaterRecordMeteringSnapshot[header.count]{};
    if (header.count > 0 && !migrated) {
        return false;
    }
    for (std::size_t i = 0; i < header.count; ++i) {
        WaterRecordMeteringSnapshotV1 legacy{};
        if (!backend_.readAt(path_,
                             sizeof(SnapshotHeader) + i * sizeof(WaterRecordMeteringSnapshotV1),
                             reinterpret_cast<std::uint8_t*>(&legacy),
                             sizeof(legacy))) {
            delete[] migrated;
            return false;
        }
        migrated[i] = expandLegacySnapshot(legacy);
    }
    capacity_ = header.capacity;
    count_ = header.count;
    oldestIndex_ = header.oldestIndex;
    char tempPath[96]{};
    if (!tempPathFor(path_, tempPath, sizeof(tempPath))) {
        delete[] migrated;
        return false;
    }
    const SnapshotHeader migratedHeader = makeHeader(capacity_, count_, oldestIndex_);
    if (!writeV2SnapshotFile(backend_, tempPath, migratedHeader, migrated)) {
        delete[] migrated;
        return false;
    }
    if (!copyFileBytes(backend_, tempPath, path_, requiredV2Size(migratedHeader))) {
        delete[] migrated;
        return false;
    }
    backend_.removeFile(tempPath);
    ready_ = true;
    delete[] migrated;
    return true;
}

bool WaterRecordMeteringSnapshotFileStore::loadHeader() {
    char tempPath[96]{};
    const bool hasTempPath = tempPathFor(path_, tempPath, sizeof(tempPath));
    auto recoverFromTemp = [&]() -> bool {
        if (!hasTempPath || !backend_.exists(tempPath)) {
            return false;
        }
        SnapshotHeader tempHeader{};
        const std::int64_t tempSize = backend_.fileSize(tempPath);
        if (tempSize >= static_cast<std::int64_t>(sizeof(tempHeader)) &&
            backend_.readAt(tempPath, 0, reinterpret_cast<std::uint8_t*>(&tempHeader), sizeof(tempHeader)) &&
            validV2HeaderForFile(tempHeader, capacity_, tempSize) &&
            copyFileBytes(backend_, tempPath, path_, requiredV2Size(tempHeader))) {
            backend_.removeFile(tempPath);
            capacity_ = tempHeader.capacity;
            count_ = tempHeader.count;
            oldestIndex_ = tempHeader.oldestIndex;
            return true;
        }
        return false;
    };
    const std::int64_t fileSize = backend_.fileSize(path_);
    if (fileSize < static_cast<std::int64_t>(sizeof(SnapshotHeader))) {
        return recoverFromTemp();
    }
    SnapshotHeader header{};
    if (!backend_.readAt(path_, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
        return recoverFromTemp();
    }
    if (header.magic != kSnapshotMagic || header.capacity == 0 ||
        header.capacity != capacity_ || header.count > header.capacity ||
        header.oldestIndex >= header.capacity) {
        return recoverFromTemp();
    }
    if (validV2HeaderForFile(header, capacity_, fileSize)) {
        if (hasTempPath && backend_.exists(tempPath)) {
            backend_.removeFile(tempPath);
        }
        capacity_ = header.capacity;
        count_ = header.count;
        oldestIndex_ = header.oldestIndex;
        return true;
    }
    if (header.version == kLegacySnapshotVersion && header.recordSize == sizeof(WaterRecordMeteringSnapshotV1)) {
        if (recoverFromTemp()) {
            return true;
        }
        return migrateV1File();
    }
    return recoverFromTemp();
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
