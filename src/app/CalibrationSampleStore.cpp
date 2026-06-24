#include "app/CalibrationSampleStore.h"

#include <algorithm>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kSampleMagic = 0x46435354UL;  // FCST
constexpr std::uint16_t kSampleVersion = 2;
constexpr std::uint8_t kStoreKindSession = 1;
constexpr std::uint32_t kMaxTraceBuckets = kPulseTraceMaxBucketsPerTrace;
constexpr std::uint32_t kMaxTraceStartupEdges = kPulseTraceMaxStartupEdgesPerTrace;

struct SampleHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint8_t kind;
    std::uint8_t reserved0;
    std::uint32_t slotCount;
    std::uint32_t maxBuckets;
    std::uint32_t maxStartupEdges;
    std::uint32_t nextSampleId;
    std::uint32_t reserved1;
};

struct SampleIndexEntry {
    std::uint8_t valid;
    std::uint8_t pendingActual;
    std::uint8_t attemptIndex;
    std::uint8_t reserved0;
    std::uint32_t sampleId;
    std::uint32_t sessionId;
    std::uint32_t actualMl;
    std::uint32_t savedAt;
    std::uint32_t bucketCount;
    std::uint32_t startupEdgeCount;
    WaterPulseTrace trace;
    std::uint32_t checksum;
};

std::uint32_t checksumBytes(const std::uint8_t* data, std::size_t len) {
    std::uint32_t hash = 2166136261UL;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619UL;
    }
    return hash == 0 ? 1 : hash;
}

bool validPath(const char* path) {
    return path && path[0] == '/';
}

std::size_t fileSizeFor(std::size_t slots) {
    return sizeof(SampleHeader) + slots * sizeof(SampleIndexEntry) +
           slots * static_cast<std::size_t>(kMaxTraceBuckets) * sizeof(WaterPulseTraceBucketSample) +
           slots * static_cast<std::size_t>(kMaxTraceStartupEdges) * sizeof(WaterPulseTraceSample);
}

std::size_t indexOffset(std::uint8_t slot) {
    return sizeof(SampleHeader) + static_cast<std::size_t>(slot) * sizeof(SampleIndexEntry);
}

std::size_t bucketOffset(std::size_t slots, std::uint8_t slot) {
    return sizeof(SampleHeader) + slots * sizeof(SampleIndexEntry) +
           static_cast<std::size_t>(slot) * kMaxTraceBuckets * sizeof(WaterPulseTraceBucketSample);
}

std::size_t startupOffset(std::size_t slots, std::uint8_t slot) {
    return sizeof(SampleHeader) + slots * sizeof(SampleIndexEntry) +
           slots * static_cast<std::size_t>(kMaxTraceBuckets) * sizeof(WaterPulseTraceBucketSample) +
           static_cast<std::size_t>(slot) * kMaxTraceStartupEdges * sizeof(WaterPulseTraceSample);
}

SampleHeader makeHeader(std::uint8_t kind, std::size_t slots, std::uint32_t nextSampleId = 1) {
    return SampleHeader{
        kSampleMagic,
        kSampleVersion,
        kind,
        0,
        static_cast<std::uint32_t>(slots),
        kMaxTraceBuckets,
        kMaxTraceStartupEdges,
        nextSampleId == 0 ? 1 : nextSampleId,
        0,
    };
}

bool validHeader(const SampleHeader& header, std::uint8_t kind, std::size_t slots) {
    return header.magic == kSampleMagic && header.version == kSampleVersion && header.kind == kind &&
           header.slotCount == slots && header.maxBuckets == kMaxTraceBuckets &&
           header.maxStartupEdges == kMaxTraceStartupEdges && header.nextSampleId != 0;
}

std::uint32_t entryChecksum(SampleIndexEntry entry) {
    entry.checksum = 0;
    return checksumBytes(reinterpret_cast<const std::uint8_t*>(&entry), sizeof(entry));
}

bool entryUsable(const SampleIndexEntry& entry) {
    return (entry.valid != 0 || entry.pendingActual != 0) && entry.bucketCount <= kMaxTraceBuckets &&
           entry.startupEdgeCount <= kMaxTraceStartupEdges && entry.checksum == entryChecksum(entry);
}

bool readHeader(WaterRecordFileBackend& backend, const char* path, SampleHeader& header) {
    return backend.readAt(path, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header));
}

bool writeHeader(WaterRecordFileBackend& backend, const char* path, const SampleHeader& header) {
    return backend.writeAt(path, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header));
}

bool readEntry(WaterRecordFileBackend& backend, const char* path, std::uint8_t slot, SampleIndexEntry& entry) {
    return backend.readAt(path, indexOffset(slot), reinterpret_cast<std::uint8_t*>(&entry), sizeof(entry));
}

bool writeEntry(WaterRecordFileBackend& backend, const char* path, std::uint8_t slot, SampleIndexEntry entry) {
    entry.checksum = entryChecksum(entry);
    return backend.writeAt(path, indexOffset(slot), reinterpret_cast<const std::uint8_t*>(&entry), sizeof(entry));
}

bool writeBlankEntry(WaterRecordFileBackend& backend, const char* path, std::uint8_t slot) {
    const SampleIndexEntry entry{};
    return backend.writeAt(path, indexOffset(slot), reinterpret_cast<const std::uint8_t*>(&entry), sizeof(entry));
}

bool initializeFile(WaterRecordFileBackend& backend, const char* path, std::uint8_t kind, std::size_t slots) {
    if (!backend.createSized(path, fileSizeFor(slots))) {
        return false;
    }
    return writeHeader(backend, path, makeHeader(kind, slots));
}

bool beginFixedStore(WaterRecordFileBackend& backend, const char* path, std::uint8_t kind, std::size_t slots) {
    if (!validPath(path)) {
        return false;
    }
    if (!backend.exists(path)) {
        return initializeFile(backend, path, kind, slots);
    }
    if (backend.fileSize(path) != static_cast<std::int64_t>(fileSizeFor(slots))) {
        return false;
    }
    SampleHeader header{};
    return readHeader(backend, path, header) && validHeader(header, kind, slots);
}

AppStorageStatus statusForFixedStore(WaterRecordFileBackend& backend,
                                     const char* path,
                                     std::uint8_t kind,
                                     std::size_t slots) {
    if (!validPath(path)) {
        return AppStorageStatus::InvalidPath;
    }
    if (!backend.exists(path)) {
        return AppStorageStatus::Ready;
    }
    if (backend.fileSize(path) != static_cast<std::int64_t>(fileSizeFor(slots))) {
        return AppStorageStatus::Corrupt;
    }
    SampleHeader header{};
    if (!readHeader(backend, path, header)) {
        return AppStorageStatus::BackendFailure;
    }
    if (validHeader(header, kind, slots)) {
        return AppStorageStatus::Ready;
    }
    return (header.magic != kSampleMagic || header.version != kSampleVersion || header.kind != kind ||
            header.slotCount != slots || header.maxBuckets != kMaxTraceBuckets ||
            header.maxStartupEdges != kMaxTraceStartupEdges)
               ? AppStorageStatus::IncompatibleFormat
               : AppStorageStatus::Corrupt;
}

bool ensureFileForWrite(WaterRecordFileBackend& backend, const char* path, std::uint8_t kind, std::size_t slots) {
    if (!validPath(path)) {
        return false;
    }
    if (!backend.exists(path)) {
        return initializeFile(backend, path, kind, slots);
    }
    if (backend.fileSize(path) != static_cast<std::int64_t>(fileSizeFor(slots))) {
        return false;
    }
    SampleHeader header{};
    return readHeader(backend, path, header) && validHeader(header, kind, slots);
}

SampleIndexEntry makeEntry(const CalibrationStoredTrace& trace,
                           std::size_t bucketCount,
                           std::size_t startupEdgeCount) {
    SampleIndexEntry entry{};
    entry.valid = trace.valid ? 1 : 0;
    entry.pendingActual = trace.pendingActual ? 1 : 0;
    entry.attemptIndex = trace.attemptIndex;
    entry.sampleId = trace.sampleId;
    entry.sessionId = trace.sessionId;
    entry.actualMl = trace.actualMl;
    entry.savedAt = trace.savedAt;
    entry.bucketCount = static_cast<std::uint32_t>(bucketCount);
    entry.startupEdgeCount = static_cast<std::uint32_t>(startupEdgeCount);
    entry.trace = trace.trace;
    entry.trace.bucketCount = bucketCount;
    entry.trace.startupEdgeCount = startupEdgeCount;
    entry.trace.bucketStart = 0;
    entry.trace.startupEdgeStart = 0;
    entry.trace.actualMl = trace.actualMl;
    return entry;
}

CalibrationStoredTrace storedFromEntry(const SampleIndexEntry& entry) {
    CalibrationStoredTrace trace{};
    trace.valid = entry.valid != 0;
    trace.pendingActual = entry.pendingActual != 0;
    trace.sampleId = entry.sampleId;
    trace.sessionId = entry.sessionId;
    trace.attemptIndex = entry.attemptIndex;
    trace.actualMl = entry.actualMl;
    trace.savedAt = entry.savedAt;
    trace.trace = entry.trace;
    trace.trace.bucketCount = entry.bucketCount;
    trace.trace.startupEdgeCount = entry.startupEdgeCount;
    trace.trace.bucketStart = 0;
    trace.trace.startupEdgeStart = 0;
    trace.trace.actualMl = entry.actualMl;
    return trace;
}

bool writeBuckets(WaterRecordFileBackend& backend,
                  const char* path,
                  std::size_t slots,
                  std::uint8_t slot,
                  const WaterPulseTraceBucketSample* buckets,
                  std::size_t bucketCount) {
    if (bucketCount > kMaxTraceBuckets || (bucketCount > 0 && !buckets)) {
        return false;
    }
    if (bucketCount == 0) {
        return true;
    }
    return backend.writeAt(path,
                           bucketOffset(slots, slot),
                           reinterpret_cast<const std::uint8_t*>(buckets),
                           bucketCount * sizeof(WaterPulseTraceBucketSample));
}

bool writeStartupEdges(WaterRecordFileBackend& backend,
                       const char* path,
                       std::size_t slots,
                       std::uint8_t slot,
                       const WaterPulseTraceSample* startupEdges,
                       std::size_t startupEdgeCount) {
    if (startupEdgeCount > kMaxTraceStartupEdges || (startupEdgeCount > 0 && !startupEdges)) {
        return false;
    }
    if (startupEdgeCount == 0) {
        return true;
    }
    return backend.writeAt(path,
                           startupOffset(slots, slot),
                           reinterpret_cast<const std::uint8_t*>(startupEdges),
                           startupEdgeCount * sizeof(WaterPulseTraceSample));
}

std::size_t readStoredBuckets(WaterRecordFileBackend& backend,
                              const char* path,
                              std::size_t slots,
                              std::uint8_t slot,
                              WaterPulseTraceBucketSample* output,
                              std::size_t outputCapacity) {
    SampleIndexEntry entry{};
    if (!output || outputCapacity == 0 || !readEntry(backend, path, slot, entry) || !entryUsable(entry)) {
        return 0;
    }
    const std::size_t count = std::min<std::size_t>(entry.bucketCount, outputCapacity);
    if (count == 0) {
        return 0;
    }
    return backend.readAt(path,
                          bucketOffset(slots, slot),
                          reinterpret_cast<std::uint8_t*>(output),
                          count * sizeof(WaterPulseTraceBucketSample))
               ? count
               : 0;
}

std::size_t readStoredStartupEdges(WaterRecordFileBackend& backend,
                                   const char* path,
                                   std::size_t slots,
                                   std::uint8_t slot,
                                   WaterPulseTraceSample* output,
                                   std::size_t outputCapacity) {
    SampleIndexEntry entry{};
    if (!output || outputCapacity == 0 || !readEntry(backend, path, slot, entry) || !entryUsable(entry)) {
        return 0;
    }
    const std::size_t count = std::min<std::size_t>(entry.startupEdgeCount, outputCapacity);
    if (count == 0) {
        return 0;
    }
    return backend.readAt(path,
                          startupOffset(slots, slot),
                          reinterpret_cast<std::uint8_t*>(output),
                          count * sizeof(WaterPulseTraceSample))
               ? count
               : 0;
}

}  // namespace

CalibrationSessionTraceStore::CalibrationSessionTraceStore(WaterRecordFileBackend& backend, const char* path)
    : backend_(backend), path_(path), ready_(false), status_(AppStorageStatus::Unavailable) {}

bool CalibrationSessionTraceStore::begin() {
    ready_ = beginFixedStore(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots);
    if (!ready_ && validPath(path_)) {
        if (backend_.exists(path_) && !backend_.removeFile(path_)) {
            status_ = AppStorageStatus::BackendFailure;
            return false;
        }
        ready_ = initializeFile(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots);
    }
    status_ = statusForFixedStore(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots);
    return ready_;
}

bool CalibrationSessionTraceStore::clear() {
    if (!ready()) {
        return false;
    }
    if (!backend_.exists(path_)) {
        const bool initialized = initializeFile(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots);
        status_ = initialized ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return initialized;
    }
    for (std::uint8_t i = 0; i < kCalibrationSessionTraceSlots; ++i) {
        if (!writeBlankEntry(backend_, path_, i)) {
            status_ = AppStorageStatus::BackendFailure;
            return false;
        }
    }
    status_ = AppStorageStatus::Ready;
    return true;
}

bool CalibrationSessionTraceStore::clearForNewSession(std::uint32_t sessionId) {
    (void)sessionId;
    if (!ready()) {
        return false;
    }
    if (!backend_.exists(path_)) {
        const bool initialized = initializeFile(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots);
        status_ = initialized ? AppStorageStatus::Ready : AppStorageStatus::BackendFailure;
        return initialized;
    }
    status_ = AppStorageStatus::Ready;
    return true;
}

bool CalibrationSessionTraceStore::savePending(std::uint8_t slot,
                                               const CalibrationStoredTrace& trace,
                                               const WaterPulseTraceBucketSample* buckets,
                                               std::size_t bucketCount,
                                               const WaterPulseTraceSample* startupEdges,
                                               std::size_t startupEdgeCount) {
    if (!ready() || slot >= kCalibrationSessionTraceSlots ||
        !ensureFileForWrite(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots) ||
        !writeBuckets(backend_, path_, kCalibrationSessionTraceSlots, slot, buckets, bucketCount) ||
        !writeStartupEdges(backend_, path_, kCalibrationSessionTraceSlots, slot, startupEdges, startupEdgeCount)) {
        status_ = ready_ ? AppStorageStatus::BackendFailure : status_;
        return false;
    }
    CalibrationStoredTrace pending = trace;
    pending.valid = false;
    pending.pendingActual = true;
    const SampleIndexEntry entry = makeEntry(pending, bucketCount, startupEdgeCount);
    return writeEntry(backend_, path_, slot, entry);
}

bool CalibrationSessionTraceStore::saveValid(std::uint8_t slot,
                                             const CalibrationStoredTrace& trace,
                                             const WaterPulseTraceBucketSample* buckets,
                                             std::size_t bucketCount,
                                             const WaterPulseTraceSample* startupEdges,
                                             std::size_t startupEdgeCount,
                                             std::uint32_t actualMl,
                                             std::uint32_t savedAt) {
    if (!ready() || slot >= kCalibrationSessionTraceSlots ||
        !ensureFileForWrite(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots) ||
        !writeBuckets(backend_, path_, kCalibrationSessionTraceSlots, slot, buckets, bucketCount) ||
        !writeStartupEdges(backend_, path_, kCalibrationSessionTraceSlots, slot, startupEdges, startupEdgeCount)) {
        status_ = ready_ ? AppStorageStatus::BackendFailure : status_;
        return false;
    }
    CalibrationStoredTrace valid = trace;
    valid.valid = true;
    valid.pendingActual = false;
    valid.actualMl = actualMl;
    valid.savedAt = savedAt;
    valid.trace.actualMl = actualMl;
    const SampleIndexEntry entry = makeEntry(valid, bucketCount, startupEdgeCount);
    return writeEntry(backend_, path_, slot, entry);
}

bool CalibrationSessionTraceStore::commitValid(std::uint8_t slot, std::uint32_t actualMl, std::uint32_t savedAt) {
    if (!ready() || slot >= kCalibrationSessionTraceSlots || !backend_.exists(path_)) {
        return false;
    }
    SampleIndexEntry entry{};
    if (!readEntry(backend_, path_, slot, entry) || !entryUsable(entry) || entry.pendingActual == 0) {
        return false;
    }
    entry.valid = 1;
    entry.pendingActual = 0;
    entry.actualMl = actualMl;
    entry.savedAt = savedAt;
    entry.trace.actualMl = actualMl;
    return writeEntry(backend_, path_, slot, entry);
}

bool CalibrationSessionTraceStore::invalidate(std::uint8_t slot) {
    return ready() && slot < kCalibrationSessionTraceSlots &&
           (!backend_.exists(path_) || writeBlankEntry(backend_, path_, slot));
}

bool CalibrationSessionTraceStore::load(std::uint8_t slot, CalibrationStoredTrace& trace) const {
    if (!ready() || slot >= kCalibrationSessionTraceSlots || !backend_.exists(path_)) {
        return false;
    }
    SampleIndexEntry entry{};
    if (!readEntry(backend_, path_, slot, entry) || !entryUsable(entry)) {
        return false;
    }
    trace = storedFromEntry(entry);
    return true;
}

std::size_t CalibrationSessionTraceStore::readBuckets(std::uint8_t slot,
                                                      WaterPulseTraceBucketSample* output,
                                                      std::size_t outputCapacity) const {
    if (!ready() || slot >= kCalibrationSessionTraceSlots || !backend_.exists(path_)) {
        return 0;
    }
    return readStoredBuckets(backend_, path_, kCalibrationSessionTraceSlots, slot, output, outputCapacity);
}

std::size_t CalibrationSessionTraceStore::readStartupEdges(std::uint8_t slot,
                                                           WaterPulseTraceSample* output,
                                                           std::size_t outputCapacity) const {
    if (!ready() || slot >= kCalibrationSessionTraceSlots || !backend_.exists(path_)) {
        return 0;
    }
    return readStoredStartupEdges(backend_, path_, kCalibrationSessionTraceSlots, slot, output, outputCapacity);
}

std::size_t CalibrationSessionTraceStore::capacity() const {
    return kCalibrationSessionTraceSlots;
}

bool CalibrationSessionTraceStore::ready() const {
    return ready_;
}

AppStorageStatus CalibrationSessionTraceStore::status() const {
    return ready_ ? AppStorageStatus::Ready : status_;
}

const char* CalibrationSessionTraceStore::storageName() const {
    return "calibration-session-trace-file";
}

}  // namespace faucet
