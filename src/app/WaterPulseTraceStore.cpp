#include "app/WaterPulseTraceStore.h"

#include "app/MeteringScheme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace faucet {
namespace {

bool sameRecordIdentity(const WaterRecord& a, const WaterRecord& b) {
    return a.startTime == b.startTime && a.volumeMl == b.volumeMl && a.targetValue == b.targetValue &&
           a.pulseCount == b.pulseCount && a.durationSec == b.durationSec && a.selectedPreset == b.selectedPreset &&
           a.result == b.result;
}

std::uint32_t roundU32(float value) {
    return value <= 0.0f ? 0 : static_cast<std::uint32_t>(std::lround(value));
}

bool isEffectiveSample(const WaterPulseTraceSample* samples,
                       std::size_t index,
                       std::uint32_t pulseMinIntervalUs) {
    if (!samples) {
        return false;
    }
    if (index == 0) {
        return true;
    }
    const std::uint32_t minInterval = std::max<std::uint32_t>(1, pulseMinIntervalUs);
    std::uint32_t lastEffectiveElapsedUs = samples[0].elapsedUs;
    for (std::size_t i = 1; i < index; ++i) {
        if (samples[i].elapsedUs >= lastEffectiveElapsedUs &&
            samples[i].elapsedUs - lastEffectiveElapsedUs >= minInterval) {
            lastEffectiveElapsedUs = samples[i].elapsedUs;
        }
    }
    return samples[index].elapsedUs >= lastEffectiveElapsedUs &&
           samples[index].elapsedUs - lastEffectiveElapsedUs >= minInterval;
}

}  // namespace

WaterPulseTraceStore::WaterPulseTraceStore(WaterPulseTrace* traces,
                                           std::size_t traceCapacity,
                                           WaterPulseTraceSample* samples,
                                           std::size_t sampleCapacity,
                                           std::size_t recentTraceLimit)
    : traces_(traces),
      traceCapacity_(traceCapacity),
      traceCount_(0),
      samples_(samples),
      sampleCapacity_(sampleCapacity),
      sampleCount_(0),
      recentTraceLimit_(recentTraceLimit),
      nextTraceId_(1) {}

void WaterPulseTraceStore::setRecentTraceLimit(std::size_t recentTraceLimit) {
    recentTraceLimit_ = recentTraceLimit;
    enforceBudget();
}

std::uint32_t WaterPulseTraceStore::beginTrace(std::uint32_t startTime, std::uint32_t pulseMinIntervalUs) {
    if (!traces_ || !samples_ || traceCapacity_ == 0 || sampleCapacity_ == 0) {
        return 0;
    }
    enforceBudget();
    const std::size_t effectiveTraceLimit =
        recentTraceLimit_ == 0 ? traceCapacity_ : std::min(traceCapacity_, recentTraceLimit_);
    while (traceCount_ >= effectiveTraceLimit) {
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
    trace.pulseMinIntervalUs =
        std::min(std::max(pulseMinIntervalUs, kMinPulseMinIntervalUs), kMaxPulseMinIntervalUs);
    trace.finalState = WaterPulseTraceState::Running;
    enforceBudget();
    return trace.traceId;
}

bool WaterPulseTraceStore::appendRawEdge(std::uint32_t traceId, std::uint32_t elapsedUs) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || trace->finished) {
        return false;
    }
    if (trace->sampleCount > 0) {
        const WaterPulseTraceSample* previous = sampleAt(*trace, trace->sampleCount - 1);
        if (!previous || elapsedUs < previous->elapsedUs) {
            trace->truncated = true;
            return false;
        }
    }
    if (trace->sampleCount >= kPulseTraceMaxRawEdgesPerTrace) {
        trace->truncated = true;
        return true;
    }
    while (sampleCount_ >= sampleCapacity_) {
        dropOldest();
        trace = findById(traceId);
        if (!trace || trace->finished) {
            return false;
        }
    }
    samples_[sampleCount_++] = WaterPulseTraceSample{elapsedUs};
    if (isEffectiveSample(&samples_[trace->sampleStart], trace->sampleCount, trace->pulseMinIntervalUs)) {
        ++trace->totalPulses;
    }
    ++trace->sampleCount;
    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::finishTrace(std::uint32_t traceId,
                                       const WaterRecord& record,
                                       WaterPulseTraceState finalState,
                                       std::uint32_t endElapsedUs) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace) {
        return false;
    }
    if (endElapsedUs != kPulseTraceNoEndElapsedUs && trace->pauseWindowCount > 0) {
        WaterPulseTracePauseWindow& last = trace->pauseWindows[trace->pauseWindowCount - 1];
        if (last.endElapsedUs == 0) {
            last.endElapsedUs = std::max(last.startElapsedUs, endElapsedUs);
        }
    }
    trace->record = record;
    trace->totalPulses = effectivePulseCount(*trace, &samples_[trace->sampleStart], trace->sampleCount);
    if (record.pulseCount > 0) {
        trace->totalPulses = record.pulseCount;
    }
    trace->finalState = finalState;
    trace->finished = true;
    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::markPaused(std::uint32_t traceId, std::uint32_t elapsedUs) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || trace->finished) {
        return false;
    }
    if (trace->pauseWindowCount > 0) {
        WaterPulseTracePauseWindow& last = trace->pauseWindows[trace->pauseWindowCount - 1];
        if (last.endElapsedUs == 0) {
            return true;
        }
    }
    if (trace->pauseWindowCount >= kPulseTraceMaxPauseWindows) {
        trace->pauseWindowOverflow = true;
        return true;
    }
    trace->pauseWindows[trace->pauseWindowCount++] = WaterPulseTracePauseWindow{elapsedUs, 0};
    return true;
}

bool WaterPulseTraceStore::markResumedAfterPause(std::uint32_t traceId, std::uint32_t elapsedUs) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || trace->finished) {
        return false;
    }
    trace->resumedAfterPause = true;
    if (elapsedUs != kPulseTraceNoEndElapsedUs && trace->pauseWindowCount > 0) {
        WaterPulseTracePauseWindow& last = trace->pauseWindows[trace->pauseWindowCount - 1];
        if (last.endElapsedUs == 0) {
            last.endElapsedUs = std::max(last.startElapsedUs, elapsedUs);
        }
    }
    return true;
}

bool WaterPulseTraceStore::setActualMl(std::uint32_t traceId, std::uint32_t actualMl) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || actualMl == 0) {
        return false;
    }
    trace->actualMl = actualMl;
    return true;
}

bool WaterPulseTraceStore::setActualMlByRecord(const WaterRecord& record, std::uint32_t actualMl) {
    if (actualMl == 0) {
        return false;
    }
    for (std::size_t i = 0; i < traceCount_; ++i) {
        if (traces_[i].finished && sameRecordIdentity(traces_[i].record, record)) {
            traces_[i].actualMl = actualMl;
            return true;
        }
    }
    return false;
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
    out.sampleCapacityPerTrace = kPulseTraceMaxRawEdgesPerTrace;
    out.usedBytes = usedBytes();
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
    const std::size_t effectiveTraceLimit =
        recentTraceLimit_ == 0 ? traceCapacity_ : std::min(traceCapacity_, recentTraceLimit_);
    while (traceCount_ > effectiveTraceLimit) {
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

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceSample* samples,
                                     std::size_t sampleCount,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    if (!samples || !buckets || bucketCapacity == 0 || sampleCount == 0) {
        return 0;
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }
    std::size_t bucketCount = 0;
    std::uint32_t cumulative = 0;
    std::uint32_t cumulativeRaw = 0;
    std::size_t i = 0;
    while (i < sampleCount && bucketCount < bucketCapacity) {
        WaterPulseTraceBucket bucket{};
        bucket.startSec = (samples[i].elapsedUs / 1000000UL / bucketSeconds) * bucketSeconds;
        bucket.durationSec = bucketSeconds;
        bucket.state = trace.finalState == WaterPulseTraceState::Running ? WaterPulseTraceState::Running
                                                                         : trace.finalState;
        const std::uint32_t bucketEndSec = bucket.startSec + bucketSeconds;
        while (i < sampleCount && samples[i].elapsedUs / 1000000UL < bucketEndSec) {
            ++bucket.rawEdgeDelta;
            if (isEffectiveSample(samples, i, trace.pulseMinIntervalUs)) {
                ++bucket.pulseDelta;
            }
            ++i;
        }
        cumulative += bucket.pulseDelta;
        cumulativeRaw += bucket.rawEdgeDelta;
        bucket.cumulativePulses = cumulative;
        bucket.cumulativeRawEdges = cumulativeRaw;
        buckets[bucketCount++] = bucket;
    }
    return bucketCount;
}

std::uint32_t effectivePulseCount(const WaterPulseTrace& trace,
                                  const WaterPulseTraceSample* samples,
                                  std::size_t sampleCount) {
    if (!samples || sampleCount == 0) {
        return 0;
    }
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if (isEffectiveSample(samples, i, trace.pulseMinIntervalUs)) {
            ++count;
        }
    }
    return count;
}

bool waterPulseTraceAnalysisEligible(const WaterPulseTrace& trace) {
    return !trace.resumedAfterPause && !trace.truncated && trace.sampleCount > 0;
}

SegmentedCalibrationOptions defaultSegmentedCalibrationOptions() {
    return SegmentedCalibrationOptions{
        kDefaultCalibrationAnalysisPulseMinIntervalUs,
        kDefaultCalibrationStableWindowSec,
        kDefaultCalibrationStableTolerancePercent,
        kDefaultCalibrationMinVolumeSpanMl,
        kDefaultCalibrationMaxErrorMl,
        kDefaultCalibrationMaxRelativeErrorTenthPercent,
    };
}

SegmentedCalibrationOptions segmentedCalibrationOptionsFromConfig(const SystemConfig& config) {
    SystemConfig safe = config;
    sanitizeConfig(safe);
    return SegmentedCalibrationOptions{
        safe.calibrationAnalysisPulseMinIntervalUs,
        safe.calibrationStableWindowSec,
        safe.calibrationStableTolerancePercent,
        safe.calibrationMinVolumeSpanMl,
        safe.calibrationMaxErrorMl,
        safe.calibrationMaxRelativeErrorTenthPercent,
    };
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount) {
    return analyzeWaterPulseTrace(trace, samples, sampleCount, defaultSegmentedCalibrationOptions());
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount,
                                               const SegmentedCalibrationOptions& options) {
    WaterPulseTraceAnalysis out{};
    if (!samples || sampleCount < 6 || !waterPulseTraceAnalysisEligible(trace)) {
        return out;
    }
    const std::uint32_t pulseMinIntervalUs =
        options.pulseMinIntervalUsOverride == 0 ? trace.pulseMinIntervalUs : options.pulseMinIntervalUsOverride;
    const std::uint32_t stableWindowSec =
        std::min<std::uint32_t>(std::max<std::uint32_t>(options.stableWindowSec, kMinCalibrationStableWindowSec),
                                kMaxCalibrationStableWindowSec);
    const std::uint32_t stableTolerancePercent = std::min<std::uint32_t>(
        std::max<std::uint32_t>(options.stableTolerancePercent, kMinCalibrationStableTolerancePercent),
        kMaxCalibrationStableTolerancePercent);
    const std::uint32_t durationSec =
        trace.record.durationSec > 0 ? trace.record.durationSec : (samples[sampleCount - 1].elapsedUs / 1000000UL + 1);
    if (durationSec < 6 || stableWindowSec > durationSec) {
        return out;
    }
    std::uint16_t* perSecond = new (std::nothrow) std::uint16_t[durationSec]{};
    if (!perSecond) {
        return out;
    }
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const std::uint32_t sec = samples[i].elapsedUs / 1000000UL;
        if (sec < durationSec && isEffectiveSample(samples, i, pulseMinIntervalUs) && perSecond[sec] < UINT16_MAX) {
            ++perSecond[sec];
        }
    }
    std::uint32_t runningCount = 0;
    std::uint32_t runningTotal = 0;
    for (std::uint32_t i = durationSec / 2; i < durationSec; ++i) {
        if (perSecond[i] > 0) {
            ++runningCount;
            runningTotal += perSecond[i];
        }
    }
    if (runningCount < 3) {
        delete[] perSecond;
        return out;
    }
    const float stableRate = static_cast<float>(runningTotal) / static_cast<float>(runningCount);
    const std::uint32_t stableFloor = roundU32(stableRate);
    if (stableFloor == 0) {
        return out;
    }
    for (std::uint32_t i = 0; i + stableWindowSec <= durationSec; ++i) {
        if (perSecond[i] < stableFloor) {
            continue;
        }
        std::uint32_t total = 0;
        std::uint32_t minValue = UINT32_MAX;
        std::uint32_t maxValue = 0;
        for (std::size_t j = 0; j < stableWindowSec; ++j) {
            const std::uint32_t value = perSecond[i + j];
            total += value;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (minValue == 0) {
            continue;
        }
        const float avg = static_cast<float>(total) / static_cast<float>(stableWindowSec);
        const float avgTolerance = stableRate * static_cast<float>(stableTolerancePercent) / 100.0f;
        const float spreadTolerance =
            stableRate * static_cast<float>(stableTolerancePercent) * 1.6f / 100.0f;
        if (std::fabs(avg - stableRate) <= std::max(1.0f, avgTolerance) &&
            static_cast<float>(maxValue - minValue) <= std::max(1.0f, spreadTolerance)) {
            out.stable = true;
            out.stableStartSec = static_cast<std::uint32_t>(i);
            for (std::uint32_t k = 0; k < i; ++k) {
                out.startupPulseCount += perSecond[k];
            }
            std::uint32_t stableSeconds = 0;
            for (std::uint32_t k = i; k < durationSec; ++k) {
                out.stablePulseCount += perSecond[k];
                ++stableSeconds;
            }
            out.stablePulsePerSec =
                stableSeconds == 0 ? 0.0f : static_cast<float>(out.stablePulseCount) / static_cast<float>(stableSeconds);
            out.confidence = static_cast<std::uint8_t>(std::min<std::size_t>(100, 40 + stableSeconds * 5));
            delete[] perSecond;
            return out;
        }
    }
    delete[] perSecond;
    return out;
}

bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 SegmentedCalibrationResult& result) {
    return computeSegmentedCalibration(samples, sampleCount, defaultSegmentedCalibrationOptions(), result);
}

namespace {

struct FitResult {
    bool valid = false;
    std::uint16_t sampleCount = 0;
    std::uint64_t totalStartupDurationSec = 0;
    std::uint64_t totalStartupPulseCount = 0;
    double totalStablePulsePerSec = 0.0;
    std::uint16_t stablePulseRateSampleCount = 0;
    std::uint32_t minActualMl = UINT32_MAX;
    std::uint32_t maxActualMl = 0;
    double mlPerStablePulse = 0.0;
    double startupVolumeMl = 0.0;
    std::uint32_t maxErrorMl = 0;
    std::uint16_t maxRelativeErrorTenthPercent = 0;
    std::size_t worstSample = 0;
};

bool sampleUsableForFit(const SegmentedCalibrationSample& sample) {
    return sample.actualMl > 0 && sample.stablePulseCount > 0 && sample.totalPulses > 0;
}

FitResult fitSegmentedSamples(const SegmentedCalibrationSample* samples,
                              std::size_t sampleCount,
                              const bool* excluded) {
    FitResult fit{};
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if ((excluded && excluded[i]) || !sampleUsableForFit(samples[i])) {
            continue;
        }
        const double x = static_cast<double>(samples[i].stablePulseCount);
        const double y = static_cast<double>(samples[i].actualMl);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        fit.totalStartupDurationSec += samples[i].startupDurationSec;
        fit.totalStartupPulseCount += samples[i].startupPulseCount;
        if (samples[i].stablePulsePerSec > 0.0f) {
            fit.totalStablePulsePerSec += samples[i].stablePulsePerSec;
            ++fit.stablePulseRateSampleCount;
        }
        fit.minActualMl = std::min(fit.minActualMl, samples[i].actualMl);
        fit.maxActualMl = std::max(fit.maxActualMl, samples[i].actualMl);
        ++fit.sampleCount;
    }
    if (fit.sampleCount < 2) {
        return fit;
    }
    const double n = static_cast<double>(fit.sampleCount);
    const double denominator = n * sumXX - sumX * sumX;
    if (!(denominator > 0.0)) {
        return fit;
    }
    fit.mlPerStablePulse = (n * sumXY - sumX * sumY) / denominator;
    fit.startupVolumeMl = (sumY - fit.mlPerStablePulse * sumX) / n;
    if (!(fit.mlPerStablePulse > 0.0) || !(fit.startupVolumeMl > 0.0)) {
        return fit;
    }
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if ((excluded && excluded[i]) || !sampleUsableForFit(samples[i])) {
            continue;
        }
        const double estimated = fit.startupVolumeMl + fit.mlPerStablePulse * samples[i].stablePulseCount;
        const std::uint32_t errorMl = roundU32(static_cast<float>(std::fabs(estimated - samples[i].actualMl)));
        const std::uint32_t relTenths =
            samples[i].actualMl == 0
                ? 0
                : static_cast<std::uint32_t>((static_cast<std::uint64_t>(errorMl) * 1000ULL +
                                              samples[i].actualMl / 2ULL) /
                                             samples[i].actualMl);
        if (errorMl > fit.maxErrorMl || (errorMl == fit.maxErrorMl && relTenths > fit.maxRelativeErrorTenthPercent)) {
            fit.maxErrorMl = errorMl;
            fit.maxRelativeErrorTenthPercent = relTenths > UINT16_MAX ? UINT16_MAX : static_cast<std::uint16_t>(relTenths);
            fit.worstSample = i;
        }
    }
    fit.valid = true;
    return fit;
}

void fillSegmentedResult(const FitResult& fit,
                         std::uint16_t excludedCount,
                         SegmentedCalibrationResult& result) {
    result.valid = true;
    result.rejectReason = SegmentedCalibrationRejectReason::None;
    result.sampleCount = fit.sampleCount;
    result.excludedSampleCount = excludedCount;
    result.startupDurationSec =
        static_cast<std::uint32_t>((fit.totalStartupDurationSec + fit.sampleCount / 2U) / fit.sampleCount);
    result.startupDurationMs =
        result.startupDurationSec > UINT32_MAX / 1000UL ? UINT32_MAX : result.startupDurationSec * 1000UL;
    result.startupPulseCount =
        static_cast<std::uint32_t>((fit.totalStartupPulseCount + fit.sampleCount / 2U) / fit.sampleCount);
    result.startupVolumeMl = roundU32(static_cast<float>(fit.startupVolumeMl));
    result.stablePulsePerLiter = roundU32(static_cast<float>(1000.0 / fit.mlPerStablePulse));
    if (fit.stablePulseRateSampleCount > 0 && result.stablePulsePerLiter > 0) {
        const double avgStablePulsePerSec =
            fit.totalStablePulsePerSec / static_cast<double>(fit.stablePulseRateSampleCount);
        result.stableFlowMlPerMin =
            roundU32(static_cast<float>(avgStablePulsePerSec * 60000.0 /
                                        static_cast<double>(result.stablePulsePerLiter)));
    } else {
        result.stableFlowMlPerMin = kDefaultStableFlowMlPerMin;
    }
    result.minActualMl = fit.minActualMl;
    result.maxActualMl = fit.maxActualMl;
    result.maxErrorMl = fit.maxErrorMl;
    result.maxRelativeErrorTenthPercent = fit.maxRelativeErrorTenthPercent;
    if (!validMeteringSchemeParameters(MeteringParameters{result.startupPulseCount,
                                                          result.startupVolumeMl,
                                                          result.stablePulsePerLiter,
                                                          result.startupDurationMs,
                                                          result.stableFlowMlPerMin})) {
        result.valid = false;
        result.rejectReason = SegmentedCalibrationRejectReason::InvalidFit;
    }
}

}  // namespace

bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 const SegmentedCalibrationOptions& options,
                                 SegmentedCalibrationResult& result) {
    result = SegmentedCalibrationResult{};
    if (!samples || sampleCount < 2) {
        result.rejectReason = SegmentedCalibrationRejectReason::NotEnoughSamples;
        return false;
    }
    SegmentedCalibrationOptions safe = options;
    safe.minVolumeSpanMl =
        std::min<std::uint32_t>(std::max<std::uint32_t>(safe.minVolumeSpanMl, kMinCalibrationMinVolumeSpanMl),
                                kMaxCalibrationMinVolumeSpanMl);
    safe.maxErrorMl = std::min<std::uint32_t>(std::max<std::uint32_t>(safe.maxErrorMl, kMinCalibrationMaxErrorMl),
                                              kMaxCalibrationMaxErrorMl);
    safe.maxRelativeErrorTenthPercent = std::min<std::uint16_t>(
        std::max<std::uint16_t>(safe.maxRelativeErrorTenthPercent, kMinCalibrationMaxRelativeErrorTenthPercent),
        kMaxCalibrationMaxRelativeErrorTenthPercent);

    bool* excluded = new (std::nothrow) bool[sampleCount]{};
    if (!excluded) {
        result.rejectReason = SegmentedCalibrationRejectReason::InvalidFit;
        return false;
    }
    FitResult fit = fitSegmentedSamples(samples, sampleCount, excluded);
    if (fit.sampleCount < 2) {
        result.rejectReason = SegmentedCalibrationRejectReason::NotEnoughSamples;
        delete[] excluded;
        return false;
    }
    if (!fit.valid) {
        result.rejectReason = SegmentedCalibrationRejectReason::DegenerateFit;
        result.sampleCount = fit.sampleCount;
        result.minActualMl = fit.minActualMl == UINT32_MAX ? 0 : fit.minActualMl;
        result.maxActualMl = fit.maxActualMl;
        delete[] excluded;
        return false;
    }
    std::uint16_t excludedCount = 0;
    if (fit.sampleCount >= 3 &&
        (fit.maxErrorMl > safe.maxErrorMl ||
         fit.maxRelativeErrorTenthPercent > safe.maxRelativeErrorTenthPercent)) {
        excluded[fit.worstSample] = true;
        excludedCount = 1;
        const FitResult retry = fitSegmentedSamples(samples, sampleCount, excluded);
        if (retry.sampleCount >= 2 && retry.valid) {
            fit = retry;
        } else {
            excluded[fit.worstSample] = false;
            excludedCount = 0;
        }
    }
    std::uint8_t qualityWarnings = kSegmentedCalibrationQualityNone;
    if (fit.maxActualMl <= fit.minActualMl + safe.minVolumeSpanMl) {
        qualityWarnings |= kSegmentedCalibrationQualityVolumeSpanSmall;
    }
    if (fit.maxErrorMl > safe.maxErrorMl ||
        fit.maxRelativeErrorTenthPercent > safe.maxRelativeErrorTenthPercent) {
        qualityWarnings |= kSegmentedCalibrationQualityErrorHigh;
    }
    fillSegmentedResult(fit, excludedCount, result);
    result.qualityWarnings = qualityWarnings;
    delete[] excluded;
    return result.valid;
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace, const WaterPulseTraceStore& store) {
    return analyzeWaterPulseTrace(trace, store, defaultSegmentedCalibrationOptions());
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceStore& store,
                                               const SegmentedCalibrationOptions& options) {
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return WaterPulseTraceAnalysis{};
    }
    for (std::size_t i = 0; i < trace.sampleCount; ++i) {
        const WaterPulseTraceSample* sample = store.sampleAt(trace, i);
        samples[i] = sample ? *sample : WaterPulseTraceSample{};
    }
    const WaterPulseTraceAnalysis result = analyzeWaterPulseTrace(trace, samples, trace.sampleCount, options);
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
