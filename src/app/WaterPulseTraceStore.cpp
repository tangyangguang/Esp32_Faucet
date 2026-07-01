#include "app/WaterPulseTraceStore.h"

#include <algorithm>
#include <cstring>

namespace faucet {
namespace {

bool sameRecordIdentity(const WaterRecord& a, const WaterRecord& b) {
    return a.startTime == b.startTime && a.volumeMl == b.volumeMl && a.targetValue == b.targetValue &&
           a.pulseCount == b.pulseCount && a.durationSec == b.durationSec && a.selectedPreset == b.selectedPreset &&
           a.result == b.result;
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

}  // namespace faucet
