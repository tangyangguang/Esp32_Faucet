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

std::size_t bucketIndexForElapsedUs(std::uint32_t elapsedUs) {
    return elapsedUs / (kPulseTraceBucketMs * 1000UL);
}

}  // namespace

WaterPulseTraceStore::WaterPulseTraceStore(WaterPulseTrace* traces,
                                           std::size_t traceCapacity,
                                           WaterPulseTraceBucketSample* buckets,
                                           std::size_t bucketCapacity,
                                           WaterPulseTraceSample* startupEdges,
                                           std::size_t startupEdgeCapacity,
                                           std::size_t recentTraceLimit)
    : traces_(traces),
      traceCapacity_(traceCapacity),
      traceCount_(0),
      buckets_(buckets),
      bucketCapacity_(bucketCapacity),
      bucketCount_(0),
      startupEdges_(startupEdges),
      startupEdgeCapacity_(startupEdgeCapacity),
      startupEdgeCount_(0),
      recentTraceLimit_(recentTraceLimit),
      nextTraceId_(1) {}

void WaterPulseTraceStore::setRecentTraceLimit(std::size_t recentTraceLimit) {
    recentTraceLimit_ = recentTraceLimit;
    enforceBudget();
}

std::uint32_t WaterPulseTraceStore::beginTrace(std::uint32_t startTime, std::uint32_t pulseMinIntervalUs) {
    if (!traces_ || traceCapacity_ == 0 || !buckets_ || bucketCapacity_ == 0 || !startupEdges_ ||
        startupEdgeCapacity_ == 0) {
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
    trace.bucketStart = bucketCount_;
    trace.startupEdgeStart = startupEdgeCount_;
    trace.pulseMinIntervalUs =
        std::min(std::max(pulseMinIntervalUs, kMinPulseMinIntervalUs), kMaxPulseMinIntervalUs);
    trace.finalState = WaterPulseTraceState::Running;
    enforceBudget();
    return trace.traceId;
}

bool WaterPulseTraceStore::appendPulseEdge(std::uint32_t traceId, std::uint32_t elapsedUs) {
    WaterPulseTrace* trace = findById(traceId);
    if (!trace || trace->finished) {
        return false;
    }
    const std::uint32_t minInterval = std::max<std::uint32_t>(1, trace->pulseMinIntervalUs);
    if (trace->hasEffectivePulse && elapsedUs < trace->lastEffectiveElapsedUs) {
        return false;
    }
    if (trace->hasEffectivePulse && elapsedUs >= trace->lastEffectiveElapsedUs &&
        elapsedUs - trace->lastEffectiveElapsedUs < minInterval) {
        ++trace->minIntervalFilteredCount;
        enforceBudget();
        return findById(traceId) != nullptr;
    }

    ++trace->totalPulses;
    trace->lastEffectiveElapsedUs = elapsedUs;
    trace->hasEffectivePulse = true;

    if (startupEdges_ && elapsedUs < kPulseTraceStartupDetailMs * 1000UL) {
        if (trace->startupEdgeCount >= kPulseTraceMaxStartupEdgesPerTrace ||
            trace->startupEdgeStart + trace->startupEdgeCount >= startupEdgeCapacity_) {
            trace->flags |= kPulseTraceFlagStartupOverflow;
        } else {
            startupEdges_[trace->startupEdgeStart + trace->startupEdgeCount] = WaterPulseTraceSample{elapsedUs};
            ++trace->startupEdgeCount;
            startupEdgeCount_ = std::max(startupEdgeCount_, trace->startupEdgeStart + trace->startupEdgeCount);
        }
    }

    if (buckets_) {
        const std::size_t bucketIndex = bucketIndexForElapsedUs(elapsedUs);
        if (bucketIndex >= kPulseTraceMaxBucketsPerTrace) {
            trace->flags |= kPulseTraceFlagBucketOverflow;
        } else if (trace->bucketStart + bucketIndex >= bucketCapacity_) {
            trace->flags |= kPulseTraceFlagBucketOverflow;
        } else {
            while (trace->bucketCount <= bucketIndex) {
                buckets_[trace->bucketStart + trace->bucketCount] = WaterPulseTraceBucketSample{};
                ++trace->bucketCount;
                bucketCount_ = std::max(bucketCount_, trace->bucketStart + trace->bucketCount);
            }
            WaterPulseTraceBucketSample& bucket = buckets_[trace->bucketStart + bucketIndex];
            if (bucket.pulseCount < UINT16_MAX) {
                ++bucket.pulseCount;
            } else {
                ++trace->droppedPulseCount;
                trace->flags |= kPulseTraceFlagDroppedPulseOverflow;
            }
        }
    }

    enforceBudget();
    return findById(traceId) != nullptr;
}

bool WaterPulseTraceStore::finishTrace(std::uint32_t traceId,
                                       const WaterRecord& record,
                                       WaterPulseTraceState finalState,
                                       std::uint32_t endElapsedUs) {
    (void)endElapsedUs;
    WaterPulseTrace* trace = findById(traceId);
    if (!trace) {
        return false;
    }
    trace->record = record;
    if (record.pulseCount > 0) {
        trace->totalPulses = record.pulseCount;
    }
    trace->finalState = finalState;
    trace->finished = true;
    enforceBudget();
    return findById(traceId) != nullptr;
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

const WaterPulseTraceBucketSample* WaterPulseTraceStore::bucketAt(const WaterPulseTrace& trace,
                                                                  std::size_t index) const {
    if (!buckets_ || index >= trace.bucketCount || trace.bucketStart + index >= bucketCount_) {
        return nullptr;
    }
    return &buckets_[trace.bucketStart + index];
}

const WaterPulseTraceSample* WaterPulseTraceStore::startupEdgeAt(const WaterPulseTrace& trace,
                                                                 std::size_t index) const {
    if (!startupEdges_ || index >= trace.startupEdgeCount || trace.startupEdgeStart + index >= startupEdgeCount_) {
        return nullptr;
    }
    return &startupEdges_[trace.startupEdgeStart + index];
}

WaterPulseTraceStats WaterPulseTraceStore::stats() const {
    WaterPulseTraceStats out{};
    out.traceCount = traceCount_;
    out.traceCapacity = traceCapacity_;
    out.bucketCount = bucketCount_;
    out.bucketCapacity = bucketCapacity_;
    out.startupEdgeCount = startupEdgeCount_;
    out.startupEdgeCapacity = startupEdgeCapacity_;
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
    return traceCount_ * sizeof(WaterPulseTrace) + bucketCount_ * sizeof(WaterPulseTraceBucketSample) +
           startupEdgeCount_ * sizeof(WaterPulseTraceSample);
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
        bucketCount_ = 0;
        startupEdgeCount_ = 0;
        return;
    }
    const std::size_t removedBuckets = traces_[0].bucketCount;
    if (removedBuckets > 0 && removedBuckets <= bucketCount_) {
        std::memmove(buckets_,
                     buckets_ + removedBuckets,
                     (bucketCount_ - removedBuckets) * sizeof(WaterPulseTraceBucketSample));
        bucketCount_ -= removedBuckets;
    }
    const std::size_t removedStartupEdges = traces_[0].startupEdgeCount;
    if (removedStartupEdges > 0 && removedStartupEdges <= startupEdgeCount_) {
        std::memmove(startupEdges_,
                     startupEdges_ + removedStartupEdges,
                     (startupEdgeCount_ - removedStartupEdges) * sizeof(WaterPulseTraceSample));
        startupEdgeCount_ -= removedStartupEdges;
    }
    for (std::size_t i = 1; i < traceCount_; ++i) {
        traces_[i].bucketStart -= removedBuckets;
        traces_[i].startupEdgeStart -= removedStartupEdges;
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
                                     const WaterPulseTraceBucketSample* compactBuckets,
                                     std::size_t compactBucketCount,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    if (!compactBuckets || !buckets || bucketCapacity == 0 || compactBucketCount == 0) {
        return 0;
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }
    const std::size_t compactPerOutput =
        std::max<std::size_t>(1, static_cast<std::size_t>(bucketSeconds * 1000UL / kPulseTraceBucketMs));
    std::size_t bucketCount = 0;
    std::uint32_t cumulative = 0;
    for (std::size_t i = 0; i < compactBucketCount && bucketCount < bucketCapacity; i += compactPerOutput) {
        WaterPulseTraceBucket bucket{};
        bucket.startSec = static_cast<std::uint32_t>((i * kPulseTraceBucketMs) / 1000UL);
        bucket.durationSec = bucketSeconds;
        bucket.state = trace.finalState == WaterPulseTraceState::Running ? WaterPulseTraceState::Running
                                                                         : trace.finalState;
        const std::size_t end = std::min(compactBucketCount, i + compactPerOutput);
        for (std::size_t j = i; j < end; ++j) {
            bucket.pulseDelta += compactBuckets[j].pulseCount;
        }
        cumulative += bucket.pulseDelta;
        bucket.cumulativePulses = cumulative;
        buckets[bucketCount++] = bucket;
    }
    return bucketCount;
}

bool waterPulseTraceAnalysisEligible(const WaterPulseTrace& trace) {
    return (trace.flags & kPulseTraceFlagBucketOverflow) == 0 && trace.bucketCount > 0;
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

namespace {

WaterPulseTraceAnalysis analyzeCompactBuckets(const WaterPulseTrace& trace,
                                              const WaterPulseTraceBucketSample* compactBuckets,
                                              std::size_t compactBucketCount,
                                              const SegmentedCalibrationOptions& options) {
    WaterPulseTraceAnalysis out{};
    if (!compactBuckets || compactBucketCount == 0 || !waterPulseTraceAnalysisEligible(trace)) {
        return out;
    }
    const std::uint32_t stableWindowSec =
        std::min<std::uint32_t>(std::max<std::uint32_t>(options.stableWindowSec, kMinCalibrationStableWindowSec),
                                kMaxCalibrationStableWindowSec);
    const std::uint32_t stableTolerancePercent = std::min<std::uint32_t>(
        std::max<std::uint32_t>(options.stableTolerancePercent, kMinCalibrationStableTolerancePercent),
        kMaxCalibrationStableTolerancePercent);
    const std::uint32_t durationSec =
        trace.record.durationSec > 0
            ? trace.record.durationSec
            : static_cast<std::uint32_t>((compactBucketCount * kPulseTraceBucketMs + 999UL) / 1000UL);
    if (durationSec < 6 || stableWindowSec > durationSec) {
        return out;
    }
    std::uint16_t* perSecond = new (std::nothrow) std::uint16_t[durationSec]{};
    if (!perSecond) {
        return out;
    }
    for (std::size_t i = 0; i < compactBucketCount; ++i) {
        const std::uint32_t sec = static_cast<std::uint32_t>((i * kPulseTraceBucketMs) / 1000UL);
        if (sec < durationSec) {
            const std::uint32_t total = perSecond[sec] + compactBuckets[i].pulseCount;
            perSecond[sec] = total > UINT16_MAX ? UINT16_MAX : static_cast<std::uint16_t>(total);
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
        delete[] perSecond;
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

}  // namespace

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceBucketSample* compactBuckets,
                                               std::size_t compactBucketCount) {
    return analyzeWaterPulseTrace(trace, compactBuckets, compactBucketCount, defaultSegmentedCalibrationOptions());
}

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceBucketSample* compactBuckets,
                                               std::size_t compactBucketCount,
                                               const SegmentedCalibrationOptions& options) {
    return analyzeCompactBuckets(trace, compactBuckets, compactBucketCount, options);
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

FitResult fitNoStartupSamples(const SegmentedCalibrationSample* samples,
                              std::size_t sampleCount,
                              const bool* excluded) {
    FitResult fit{};
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if ((excluded && excluded[i]) || !sampleUsableForFit(samples[i])) {
            continue;
        }
        const double x = static_cast<double>(samples[i].totalPulses);
        const double y = static_cast<double>(samples[i].actualMl);
        sumXX += x * x;
        sumXY += x * y;
        if (samples[i].stablePulsePerSec > 0.0f) {
            fit.totalStablePulsePerSec += samples[i].stablePulsePerSec;
            ++fit.stablePulseRateSampleCount;
        }
        fit.minActualMl = std::min(fit.minActualMl, samples[i].actualMl);
        fit.maxActualMl = std::max(fit.maxActualMl, samples[i].actualMl);
        ++fit.sampleCount;
    }
    if (fit.sampleCount < 2 || !(sumXX > 0.0)) {
        return fit;
    }
    fit.mlPerStablePulse = sumXY / sumXX;
    fit.startupVolumeMl = 0.0;
    if (!(fit.mlPerStablePulse > 0.0)) {
        return fit;
    }
    for (std::size_t i = 0; i < sampleCount; ++i) {
        if ((excluded && excluded[i]) || !sampleUsableForFit(samples[i])) {
            continue;
        }
        const double estimated = fit.mlPerStablePulse * samples[i].totalPulses;
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
        return fit.totalStartupPulseCount == 0 ? fitNoStartupSamples(samples, sampleCount, excluded) : fit;
    }
    fit.mlPerStablePulse = (n * sumXY - sumX * sumY) / denominator;
    fit.startupVolumeMl = (sumY - fit.mlPerStablePulse * sumX) / n;
    if (!(fit.mlPerStablePulse > 0.0)) {
        return fit;
    }
    if (fit.totalStartupPulseCount == 0) {
        return fitNoStartupSamples(samples, sampleCount, excluded);
    }
    if (!(fit.startupVolumeMl > 0.0)) {
        fit.startupVolumeMl = 0.0;
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
    WaterPulseTraceBucketSample* compactBuckets =
        new (std::nothrow) WaterPulseTraceBucketSample[trace.bucketCount]{};
    if (!compactBuckets) {
        return WaterPulseTraceAnalysis{};
    }
    for (std::size_t i = 0; i < trace.bucketCount; ++i) {
        const WaterPulseTraceBucketSample* bucket = store.bucketAt(trace, i);
        compactBuckets[i] = bucket ? *bucket : WaterPulseTraceBucketSample{};
    }
    const WaterPulseTraceAnalysis result = analyzeCompactBuckets(trace, compactBuckets, trace.bucketCount, options);
    delete[] compactBuckets;
    return result;
}

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceStore& store,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity) {
    WaterPulseTraceBucketSample* compactBuckets =
        new (std::nothrow) WaterPulseTraceBucketSample[trace.bucketCount]{};
    if (!compactBuckets) {
        return 0;
    }
    for (std::size_t i = 0; i < trace.bucketCount; ++i) {
        const WaterPulseTraceBucketSample* bucket = store.bucketAt(trace, i);
        compactBuckets[i] = bucket ? *bucket : WaterPulseTraceBucketSample{};
    }
    const std::size_t result =
        aggregateWaterPulseTrace(trace, compactBuckets, trace.bucketCount, bucketSeconds, buckets, bucketCapacity);
    delete[] compactBuckets;
    return result;
}

}  // namespace faucet
