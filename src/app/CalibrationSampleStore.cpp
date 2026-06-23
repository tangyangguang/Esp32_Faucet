#include "app/CalibrationSampleStore.h"

#include <algorithm>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kSampleMagic = 0x46435354UL;  // FCST
constexpr std::uint16_t kSampleVersion = 1;
constexpr std::uint8_t kStoreKindSession = 1;
constexpr std::uint32_t kMaxTraceSamples = kPulseTraceMaxRawEdgesPerTrace;

struct SampleHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint8_t kind;
    std::uint8_t reserved0;
    std::uint32_t slotCount;
    std::uint32_t maxSamples;
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
    std::uint32_t sampleCount;
    WaterPulseTrace trace;
    std::uint32_t checksum;
};

static_assert(sizeof(SampleHeader) == 24, "SampleHeader must stay fixed-size");

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
           slots * static_cast<std::size_t>(kMaxTraceSamples) * sizeof(WaterPulseTraceSample);
}

std::size_t indexOffset(std::uint8_t slot) {
    return sizeof(SampleHeader) + static_cast<std::size_t>(slot) * sizeof(SampleIndexEntry);
}

std::size_t sampleOffset(std::size_t slots, std::uint8_t slot) {
    return sizeof(SampleHeader) + slots * sizeof(SampleIndexEntry) +
           static_cast<std::size_t>(slot) * kMaxTraceSamples * sizeof(WaterPulseTraceSample);
}

SampleHeader makeHeader(std::uint8_t kind, std::size_t slots, std::uint32_t nextSampleId = 1) {
    return SampleHeader{
        kSampleMagic,
        kSampleVersion,
        kind,
        0,
        static_cast<std::uint32_t>(slots),
        kMaxTraceSamples,
        nextSampleId == 0 ? 1 : nextSampleId,
        0,
    };
}

bool validHeader(const SampleHeader& header, std::uint8_t kind, std::size_t slots) {
    return header.magic == kSampleMagic && header.version == kSampleVersion && header.kind == kind &&
           header.slotCount == slots && header.maxSamples == kMaxTraceSamples && header.nextSampleId != 0;
}

std::uint32_t entryChecksum(SampleIndexEntry entry) {
    entry.checksum = 0;
    return checksumBytes(reinterpret_cast<const std::uint8_t*>(&entry), sizeof(entry));
}

bool entryUsable(const SampleIndexEntry& entry) {
    return (entry.valid != 0 || entry.pendingActual != 0) && entry.sampleCount <= kMaxTraceSamples &&
           entry.checksum == entryChecksum(entry);
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
            header.slotCount != slots || header.maxSamples != kMaxTraceSamples)
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

SampleIndexEntry makeEntry(const CalibrationStoredTrace& trace, std::size_t sampleCount) {
    SampleIndexEntry entry{};
    entry.valid = trace.valid ? 1 : 0;
    entry.pendingActual = trace.pendingActual ? 1 : 0;
    entry.attemptIndex = trace.attemptIndex;
    entry.sampleId = trace.sampleId;
    entry.sessionId = trace.sessionId;
    entry.actualMl = trace.actualMl;
    entry.savedAt = trace.savedAt;
    entry.sampleCount = static_cast<std::uint32_t>(sampleCount);
    entry.trace = trace.trace;
    entry.trace.sampleCount = sampleCount;
    entry.trace.sampleStart = 0;
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
    trace.trace.sampleCount = entry.sampleCount;
    trace.trace.sampleStart = 0;
    trace.trace.actualMl = entry.actualMl;
    return trace;
}

bool writeSamples(WaterRecordFileBackend& backend,
                  const char* path,
                  std::size_t slots,
                  std::uint8_t slot,
                  const WaterPulseTraceSample* samples,
                  std::size_t sampleCount) {
    if (sampleCount > kMaxTraceSamples || (sampleCount > 0 && !samples)) {
        return false;
    }
    if (sampleCount == 0) {
        return true;
    }
    return backend.writeAt(path,
                           sampleOffset(slots, slot),
                           reinterpret_cast<const std::uint8_t*>(samples),
                           sampleCount * sizeof(WaterPulseTraceSample));
}

std::size_t readStoredSamples(WaterRecordFileBackend& backend,
                              const char* path,
                              std::size_t slots,
                              std::uint8_t slot,
                              WaterPulseTraceSample* output,
                              std::size_t outputCapacity) {
    SampleIndexEntry entry{};
    if (!output || outputCapacity == 0 || !readEntry(backend, path, slot, entry) || !entryUsable(entry)) {
        return 0;
    }
    const std::size_t count = std::min<std::size_t>(entry.sampleCount, outputCapacity);
    if (count == 0) {
        return 0;
    }
    return backend.readAt(path,
                          sampleOffset(slots, slot),
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
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount) {
    if (!ready() || slot >= kCalibrationSessionTraceSlots ||
        !ensureFileForWrite(backend_, path_, kStoreKindSession, kCalibrationSessionTraceSlots) ||
        !writeSamples(backend_, path_, kCalibrationSessionTraceSlots, slot, samples, sampleCount)) {
        status_ = ready_ ? AppStorageStatus::BackendFailure : status_;
        return false;
    }
    CalibrationStoredTrace pending = trace;
    pending.valid = false;
    pending.pendingActual = true;
    const SampleIndexEntry entry = makeEntry(pending, sampleCount);
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

std::size_t CalibrationSessionTraceStore::readSamples(std::uint8_t slot,
                                                      WaterPulseTraceSample* output,
                                                      std::size_t outputCapacity) const {
    if (!ready() || slot >= kCalibrationSessionTraceSlots || !backend_.exists(path_)) {
        return 0;
    }
    return readStoredSamples(backend_, path_, kCalibrationSessionTraceSlots, slot, output, outputCapacity);
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
