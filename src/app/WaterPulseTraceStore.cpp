#include "app/WaterPulseTraceStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kSavedTraceMagic = 0x46575054UL;  // FWPT
constexpr std::uint16_t kSavedTraceVersion = 1;

struct SavedTraceHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t headerSize;
    std::uint16_t entrySize;
    std::uint16_t sampleSize;
    std::uint32_t sampleCount;
    std::uint32_t key;
};

struct SavedTraceEntry {
    std::uint32_t traceId;
    std::uint32_t startTime;
    WaterRecord record;
    std::uint32_t sampleCount;
    std::uint32_t totalPulses;
    std::uint32_t actualMl;
    std::uint8_t finished;
    std::uint8_t reserved[7];
};

static_assert(sizeof(SavedTraceHeader) == 20, "SavedTraceHeader must stay fixed-size");

bool sameRecordIdentity(const WaterRecord& a, const WaterRecord& b) {
    return a.startTime == b.startTime && a.volumeMl == b.volumeMl && a.targetValue == b.targetValue &&
           a.pulseCount == b.pulseCount && a.durationSec == b.durationSec && a.selectedPreset == b.selectedPreset &&
           a.result == b.result;
}

WaterPulseTraceState mergeState(WaterPulseTraceState current, WaterPulseTraceState next) {
    if (current == WaterPulseTraceState::Error || next == WaterPulseTraceState::Error) {
        return WaterPulseTraceState::Error;
    }
    if (current == WaterPulseTraceState::Stopped || next == WaterPulseTraceState::Stopped) {
        return WaterPulseTraceState::Stopped;
    }
    if (current == WaterPulseTraceState::Completed || next == WaterPulseTraceState::Completed) {
        return WaterPulseTraceState::Completed;
    }
    if (current == WaterPulseTraceState::Paused || next == WaterPulseTraceState::Paused) {
        return WaterPulseTraceState::Paused;
    }
    return WaterPulseTraceState::Running;
}

std::uint32_t roundU32(float value) {
    return value <= 0.0f ? 0 : static_cast<std::uint32_t>(std::lround(value));
}

void hashRecordField(std::uint32_t& hash, std::uint32_t value) {
    hash ^= value;
    hash *= 16777619UL;
}

SavedTraceHeader makeSavedTraceHeader(std::uint32_t key, std::size_t sampleCount) {
    return SavedTraceHeader{
        kSavedTraceMagic,
        kSavedTraceVersion,
        static_cast<std::uint16_t>(sizeof(SavedTraceHeader)),
        static_cast<std::uint16_t>(sizeof(SavedTraceEntry)),
        static_cast<std::uint16_t>(sizeof(WaterPulseTraceSample)),
        static_cast<std::uint32_t>(sampleCount),
        key,
    };
}

}  // namespace

WaterPulseTraceStore::WaterPulseTraceStore(WaterPulseTrace* traces,
                                           std::size_t traceCapacity,
                                           WaterPulseTraceSample* samples,
                                           std::size_t sampleCapacity,
                                           std::size_t budgetBytes)
    : traces_(traces),
      traceCapacity_(traceCapacity),
      traceCount_(0),
      samples_(samples),
      sampleCapacity_(sampleCapacity),
      sampleCount_(0),
      budgetBytes_(budgetBytes),
      nextTraceId_(1) {}

void WaterPulseTraceStore::setBudgetBytes(std::size_t budgetBytes) {
    budgetBytes_ = budgetBytes;
    enforceBudget();
}

std::uint32_t WaterPulseTraceStore::beginTrace(std::uint32_t startTime) {
    if (!traces_ || !samples_ || traceCapacity_ == 0 || sampleCapacity_ == 0) {
        return 0;
    }
    while (traceCount_ >= traceCapacity_) {
        dropOldest();
    }
    WaterPulseTrace& trace = traces_[traceCount_++];
    trace = WaterPulseTrace{};
    trace.traceId = nextTraceId_++;
    if (nextTraceId_ == 0) {
        nextTraceId_ = 1;
    }
    trace.startTime = startTime;
    trace.sampleStart = sampleCount_;
    enforceBudget();
    return trace.traceId;
}

bool WaterPulseTraceStore::appendSecond(std::uint32_t traceId, std::uint32_t pulseDelta, WaterPulseTraceState state) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || trace->finished || sampleCount_ >= sampleCapacity_) {
        return false;
    }
    while (sampleCount_ >= sampleCapacity_) {
        dropOldest();
        trace = findById(traceId);
        if (!trace || trace->finished) {
            return false;
        }
    }
    const std::uint16_t clipped =
        pulseDelta > UINT16_MAX ? UINT16_MAX : static_cast<std::uint16_t>(pulseDelta);
    samples_[sampleCount_++] = WaterPulseTraceSample{clipped, state, 0};
    ++trace->sampleCount;
    trace->totalPulses += clipped;
    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::finishTrace(std::uint32_t traceId, const WaterRecord& record, WaterPulseTraceState finalState) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace) {
        return false;
    }
    trace->record = record;
    trace->finished = true;
    if (trace->sampleCount > 0) {
        samples_[trace->sampleStart + trace->sampleCount - 1].state =
            mergeState(samples_[trace->sampleStart + trace->sampleCount - 1].state, finalState);
    }
    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::setActualMl(std::uint32_t traceId, std::uint32_t actualMl) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || actualMl == 0) {
        return false;
    }
    trace->actualMl = actualMl;
    return true;
}

const WaterPulseTrace* WaterPulseTraceStore::findById(std::uint32_t traceId) const {
    const std::size_t index = indexOf(traceId);
    return index < traceCount_ ? &traces_[index] : nullptr;
}

WaterPulseTrace* WaterPulseTraceStore::findById(std::uint32_t traceId) {
    const std::size_t index = indexOf(traceId);
    return index < traceCount_ ? &traces_[index] : nullptr;
}

const WaterPulseTrace* WaterPulseTraceStore::findByRecord(const WaterRecord& record) const {
    for (std::size_t i = 0; i < traceCount_; ++i) {
        if (traces_[i].finished && sameRecordIdentity(traces_[i].record, record)) {
            return &traces_[i];
        }
    }
    return nullptr;
}

const WaterPulseTrace* WaterPulseTraceStore::traceAt(std::size_t index) const {
    return index < traceCount_ ? &traces_[index] : nullptr;
}

const WaterPulseTraceSample* WaterPulseTraceStore::sampleAt(const WaterPulseTrace& trace, std::size_t index) const {
    if (index >= trace.sampleCount || trace.sampleStart + index >= sampleCount_) {
        return nullptr;
    }
    return &samples_[trace.sampleStart + index];
}

WaterPulseTraceStats WaterPulseTraceStore::stats() const {
    WaterPulseTraceStats out{};
    out.traceCount = traceCount_;
    out.traceCapacity = traceCapacity_;
    out.sampleCount = sampleCount_;
    out.sampleCapacity = sampleCapacity_;
    out.usedBytes = usedBytes();
    out.budgetBytes = budgetBytes_;
    out.usagePercent = budgetBytes_ == 0
                           ? 0
                           : static_cast<std::uint8_t>(std::min<std::size_t>(100, (out.usedBytes * 100U) / budgetBytes_));
    if (traceCount_ > 0) {
        out.oldestStartTime = traces_[0].startTime;
        out.latestStartTime = traces_[traceCount_ - 1].startTime;
    }
    return out;
}

std::size_t WaterPulseTraceStore::count() const {
    return traceCount_;
}

std::size_t WaterPulseTraceStore::usedBytes() const {
    return traceCount_ * sizeof(WaterPulseTrace) + sampleCount_ * sizeof(WaterPulseTraceSample);
}

void WaterPulseTraceStore::enforceBudget() {
    while (traceCount_ > 1 && budgetBytes_ > 0 && usedBytes() > budgetBytes_) {
        dropOldest();
    }
}

void WaterPulseTraceStore::dropOldest() {
    if (traceCount_ == 0) {
        sampleCount_ = 0;
        return;
    }
    const std::size_t removedSamples = traces_[0].sampleCount;
    if (removedSamples > 0 && removedSamples <= sampleCount_) {
        std::memmove(samples_, samples_ + removedSamples, (sampleCount_ - removedSamples) * sizeof(WaterPulseTraceSample));
        sampleCount_ -= removedSamples;
    }
    for (std::size_t i = 1; i < traceCount_; ++i) {
        traces_[i].sampleStart -= removedSamples;
        traces_[i - 1] = traces_[i];
    }
    --traceCount_;
}

std::size_t WaterPulseTraceStore::indexOf(std::uint32_t traceId) const {
    for (std::size_t i = 0; i < traceCount_; ++i) {
        if (traces_[i].traceId == traceId) {
            return i;
        }
    }
    return traceCount_;
}

WaterPulseTraceFileStore::WaterPulseTraceFileStore(WaterRecordFileBackend& backend,
                                                   const char* pathPrefix,
                                                   std::size_t sampleCapacityPerTrace)
    : backend_(backend),
      pathPrefix_(pathPrefix),
      sampleCapacityPerTrace_(sampleCapacityPerTrace),
      ready_(false) {}

bool WaterPulseTraceFileStore::begin() {
    ready_ = false;
    if (!pathPrefix_ || pathPrefix_[0] != '/' || std::strlen(pathPrefix_) > 16 ||
        sampleCapacityPerTrace_ == 0 || sampleCapacityPerTrace_ > UINT32_MAX) {
        return false;
    }
    ready_ = true;
    return true;
}

bool WaterPulseTraceFileStore::save(const WaterPulseTrace& trace,
                                    const WaterPulseTraceSample* samples,
                                    std::size_t sampleCount,
                                    std::uint32_t* savedTraceId) {
    if (!ready() || !samples || sampleCount == 0 || sampleCount > sampleCapacityPerTrace_ ||
        sampleCount != trace.sampleCount) {
        return false;
    }
    WaterPulseTrace next = trace;
    const std::uint32_t key = keyForRecord(next.record);
    if (key == 0) {
        return false;
    }
    next.traceId = key;
    char path[32]{};
    if (!pathForKey(key, path, sizeof(path))) {
        return false;
    }
    const std::size_t fileSize = sampleOffset() + sampleCount * sizeof(WaterPulseTraceSample);
    if (!backend_.createSized(path, fileSize)) {
        return false;
    }
    const SavedTraceHeader header = makeSavedTraceHeader(key, sampleCount);
    SavedTraceEntry entry{};
    entry.traceId = key;
    entry.startTime = next.startTime;
    entry.record = next.record;
    entry.sampleCount = static_cast<std::uint32_t>(sampleCount);
    entry.totalPulses = next.totalPulses;
    entry.actualMl = next.actualMl;
    entry.finished = next.finished ? 1 : 0;
    const bool wrote = backend_.writeAt(path, 0, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)) &&
                       backend_.writeAt(path,
                                        sizeof(SavedTraceHeader),
                                        reinterpret_cast<const std::uint8_t*>(&entry),
                                        sizeof(entry)) &&
                       backend_.writeAt(path,
                                        sampleOffset(),
                                        reinterpret_cast<const std::uint8_t*>(samples),
                                        sampleCount * sizeof(WaterPulseTraceSample));
    if (!wrote) {
        backend_.removeFile(path);
        return false;
    }
    if (savedTraceId) {
        *savedTraceId = key;
    }
    return true;
}

bool WaterPulseTraceFileStore::remove(std::uint32_t traceId) {
    if (!ready() || traceId == 0) {
        return false;
    }
    char path[32]{};
    if (!pathForKey(traceId, path, sizeof(path)) || !backend_.exists(path)) {
        return false;
    }
    return backend_.removeFile(path);
}

bool WaterPulseTraceFileStore::findById(std::uint32_t traceId, WaterPulseTrace& output) const {
    return traceId != 0 && readTraceFile(traceId, output);
}

bool WaterPulseTraceFileStore::findByRecord(const WaterRecord& record, WaterPulseTrace& output) const {
    const std::uint32_t key = keyForRecord(record);
    if (key == 0 || !readTraceFile(key, output)) {
        return false;
    }
    return sameRecordIdentity(output.record, record);
}

std::size_t WaterPulseTraceFileStore::readSamples(std::uint32_t traceId,
                                                  WaterPulseTraceSample* output,
                                                  std::size_t outputCapacity) const {
    if (!ready() || !output || outputCapacity == 0 || traceId == 0) {
        return 0;
    }
    WaterPulseTrace trace{};
    if (!findById(traceId, trace) || trace.sampleCount == 0 || trace.sampleCount > outputCapacity ||
        trace.sampleCount > sampleCapacityPerTrace_) {
        return 0;
    }
    char path[32]{};
    if (!pathForKey(traceId, path, sizeof(path))) {
        return 0;
    }
    return backend_.readAt(path,
                           sampleOffset(),
                           reinterpret_cast<std::uint8_t*>(output),
                           trace.sampleCount * sizeof(WaterPulseTraceSample))
               ? trace.sampleCount
               : 0;
}

bool WaterPulseTraceFileStore::containsRecord(const WaterRecord& record) const {
    WaterPulseTrace trace{};
    return findByRecord(record, trace);
}

std::size_t WaterPulseTraceFileStore::sampleCapacityPerTrace() const {
    return sampleCapacityPerTrace_;
}

bool WaterPulseTraceFileStore::ready() const {
    return ready_ && pathPrefix_ && pathPrefix_[0] == '/';
}

std::uint32_t WaterPulseTraceFileStore::keyForRecord(const WaterRecord& record) const {
    std::uint32_t hash = 2166136261UL;
    hashRecordField(hash, record.startTime);
    hashRecordField(hash, record.volumeMl);
    hashRecordField(hash, record.targetValue);
    hashRecordField(hash, record.pulseCount);
    hashRecordField(hash, record.rejectedPulseCount);
    hashRecordField(hash, record.durationSec);
    hashRecordField(hash, static_cast<std::uint32_t>(record.mode));
    hashRecordField(hash, static_cast<std::uint32_t>(record.result));
    hashRecordField(hash, record.selectedPreset);
    if (hash == 0) {
        hash = 1;
    }
    return hash;
}

bool WaterPulseTraceFileStore::pathForKey(std::uint32_t key, char* out, std::size_t len) const {
    if (!ready() || key == 0 || !out || len == 0) {
        return false;
    }
    const int written = std::snprintf(out, len, "%s%08lx.bin", pathPrefix_, static_cast<unsigned long>(key));
    return written > 0 && static_cast<std::size_t>(written) < len;
}

bool WaterPulseTraceFileStore::readTraceFile(std::uint32_t key, WaterPulseTrace& output) const {
    if (!ready() || key == 0) {
        return false;
    }
    char path[32]{};
    if (!pathForKey(key, path, sizeof(path)) || !backend_.exists(path)) {
        return false;
    }
    SavedTraceHeader header{};
    SavedTraceEntry entry{};
    if (!backend_.readAt(path, 0, reinterpret_cast<std::uint8_t*>(&header), sizeof(header)) ||
        !backend_.readAt(path, sizeof(SavedTraceHeader), reinterpret_cast<std::uint8_t*>(&entry), sizeof(entry))) {
        return false;
    }
    if (header.magic != kSavedTraceMagic || header.version != kSavedTraceVersion ||
        header.headerSize != sizeof(SavedTraceHeader) || header.entrySize != sizeof(SavedTraceEntry) ||
        header.sampleSize != sizeof(WaterPulseTraceSample) || header.key != key || header.sampleCount == 0 ||
        header.sampleCount > sampleCapacityPerTrace_ || entry.sampleCount != header.sampleCount ||
        entry.traceId != key) {
        return false;
    }
    const std::int64_t expectedSize =
        static_cast<std::int64_t>(sampleOffset() + header.sampleCount * sizeof(WaterPulseTraceSample));
    if (backend_.fileSize(path) != expectedSize) {
        return false;
    }
    output = WaterPulseTrace{};
    output.traceId = entry.traceId;
    output.startTime = entry.startTime;
    output.record = entry.record;
    output.sampleStart = 0;
    output.sampleCount = entry.sampleCount;
    output.totalPulses = entry.totalPulses;
    output.actualMl = entry.actualMl;
    output.finished = entry.finished != 0;
    return true;
}

std::size_t WaterPulseTraceFileStore::sampleOffset() const {
    return sizeof(SavedTraceHeader) + sizeof(SavedTraceEntry);
}

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace&,
                                     const WaterPulseTraceSample* samples,
                                     std::size_t sampleCount,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    if (!samples || !buckets || bucketCapacity == 0 || sampleCount == 0) {
        return 0;
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }
    std::size_t bucketCount = 0;
    std::uint32_t cumulative = 0;
    for (std::size_t i = 0; i < sampleCount && bucketCount < bucketCapacity;) {
        WaterPulseTraceBucket bucket{};
        bucket.startSec = static_cast<std::uint32_t>(i);
        bucket.durationSec = bucketSeconds;
        bucket.state = samples[i].state;
        for (std::uint32_t j = 0; j < bucketSeconds && i < sampleCount; ++j, ++i) {
            bucket.pulseDelta += samples[i].pulseDelta;
            bucket.state = mergeState(bucket.state, samples[i].state);
        }
        cumulative += bucket.pulseDelta;
        bucket.cumulativePulses = cumulative;
        buckets[bucketCount++] = bucket;
    }
    return bucketCount;
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace&,
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount) {
    WaterPulseTraceAnalysis out{};
    if (!samples || sampleCount < 6) {
        return out;
    }
    std::uint32_t runningCount = 0;
    std::uint32_t runningTotal = 0;
    for (std::size_t i = sampleCount / 2; i < sampleCount; ++i) {
        if (samples[i].state == WaterPulseTraceState::Running && samples[i].pulseDelta > 0) {
            ++runningCount;
            runningTotal += samples[i].pulseDelta;
        }
    }
    if (runningCount < 3) {
        return out;
    }
    const float stableRate = static_cast<float>(runningTotal) / static_cast<float>(runningCount);
    const std::uint32_t stableFloor = roundU32(stableRate);
    if (stableFloor == 0) {
        return out;
    }
    constexpr std::size_t kWindow = 4;
    for (std::size_t i = 0; i + kWindow <= sampleCount; ++i) {
        if (samples[i].state != WaterPulseTraceState::Running || samples[i].pulseDelta < stableFloor) {
            continue;
        }
        std::uint32_t total = 0;
        std::uint32_t minValue = UINT32_MAX;
        std::uint32_t maxValue = 0;
        bool allRunning = true;
        for (std::size_t j = 0; j < kWindow; ++j) {
            if (samples[i + j].state != WaterPulseTraceState::Running) {
                allRunning = false;
                break;
            }
            const std::uint32_t value = samples[i + j].pulseDelta;
            total += value;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (!allRunning || minValue == 0) {
            continue;
        }
        const float avg = static_cast<float>(total) / static_cast<float>(kWindow);
        if (std::fabs(avg - stableRate) <= std::max(1.0f, stableRate * 0.25f) &&
            static_cast<float>(maxValue - minValue) <= std::max(1.0f, stableRate * 0.4f)) {
            out.stable = true;
            out.stableStartSec = static_cast<std::uint32_t>(i);
            for (std::size_t k = 0; k < i; ++k) {
                if (samples[k].state == WaterPulseTraceState::Running) {
                    out.startupPulseCount += samples[k].pulseDelta;
                }
            }
            std::uint32_t stableSeconds = 0;
            for (std::size_t k = i; k < sampleCount; ++k) {
                if (samples[k].state == WaterPulseTraceState::Running) {
                    out.stablePulseCount += samples[k].pulseDelta;
                    ++stableSeconds;
                }
            }
            out.stablePulsePerSec =
                stableSeconds == 0 ? 0.0f : static_cast<float>(out.stablePulseCount) / static_cast<float>(stableSeconds);
            out.confidence = static_cast<std::uint8_t>(std::min<std::size_t>(100, 40 + stableSeconds * 5));
            return out;
        }
    }
    return out;
}

bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 SegmentedCalibrationResult& result) {
    result = SegmentedCalibrationResult{};
    if (!samples || sampleCount < 2) {
        return false;
    }
    const SegmentedCalibrationSample* low = nullptr;
    const SegmentedCalibrationSample* high = nullptr;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if (samples[i].actualMl == 0 || samples[i].stablePulseCount == 0 || samples[i].totalPulses == 0) {
            continue;
        }
        if (!low || samples[i].actualMl < low->actualMl) {
            low = &samples[i];
        }
        if (!high || samples[i].actualMl > high->actualMl) {
            high = &samples[i];
        }
    }
    if (!low || !high || low == high || high->actualMl <= low->actualMl + 500UL ||
        high->stablePulseCount <= low->stablePulseCount) {
        return false;
    }
    const float stablePulsePerMl =
        static_cast<float>(high->stablePulseCount - low->stablePulseCount) /
        static_cast<float>(high->actualMl - low->actualMl);
    if (!(stablePulsePerMl > 0.0f)) {
        return false;
    }
    const float startupVolumeMl =
        static_cast<float>(low->actualMl) - static_cast<float>(low->stablePulseCount) / stablePulsePerMl;
    if (!(startupVolumeMl > 0.0f)) {
        return false;
    }
    result.valid = true;
    result.startupDurationSec = low->startupDurationSec;
    result.startupPulseCount = low->startupPulseCount;
    result.startupVolumeMl = roundU32(startupVolumeMl);
    result.startupPulsePerLiter =
        result.startupVolumeMl == 0
            ? 0
            : roundU32(static_cast<float>(result.startupPulseCount) * 1000.0f /
                       static_cast<float>(result.startupVolumeMl));
    result.stablePulsePerLiter = roundU32(stablePulsePerMl * 1000.0f);
    result.overallPulsePerLiter =
        roundU32(static_cast<float>(high->totalPulses) * 1000.0f / static_cast<float>(high->actualMl));
    return true;
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace, const WaterPulseTraceStore& store) {
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return WaterPulseTraceAnalysis{};
    }
    for (std::size_t i = 0; i < trace.sampleCount; ++i) {
        const WaterPulseTraceSample* sample = store.sampleAt(trace, i);
        samples[i] = sample ? *sample : WaterPulseTraceSample{};
    }
    const WaterPulseTraceAnalysis result = analyzeWaterPulseTrace(trace, samples, trace.sampleCount);
    delete[] samples;
    return result;
}

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceStore& store,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return 0;
    }
    for (std::size_t i = 0; i < trace.sampleCount; ++i) {
        const WaterPulseTraceSample* sample = store.sampleAt(trace, i);
        samples[i] = sample ? *sample : WaterPulseTraceSample{};
    }
    const std::size_t result =
        aggregateWaterPulseTrace(trace, samples, trace.sampleCount, bucketSeconds, buckets, bucketCapacity);
    delete[] samples;
    return result;
}

}  // namespace faucet
