#pragma once

#include "app/CalibrationSampleStore.h"
#include "app/CalibrationSession.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace faucet_test {

inline std::size_t fillCalibrationSamples(faucet::WaterPulseTraceSample* samples,
                                          std::size_t capacity,
                                          std::uint32_t startupPulses,
                                          std::uint32_t stablePulses,
                                          std::uint32_t stableSeconds) {
    if (!samples || capacity < startupPulses + stablePulses || stableSeconds == 0) {
        return 0;
    }
    std::size_t count = 0;
    for (std::uint32_t sec = 0; sec < 5; ++sec) {
        const std::uint32_t pulsesThisSec = startupPulses / 5 + (sec < startupPulses % 5 ? 1 : 0);
        for (std::uint32_t i = 0; i < pulsesThisSec; ++i) {
            samples[count++] =
                faucet::WaterPulseTraceSample{static_cast<std::uint32_t>(sec * 1000000UL + i * 5000UL)};
        }
    }
    for (std::uint32_t sec = 0; sec < stableSeconds; ++sec) {
        const std::uint32_t pulsesThisSec = stablePulses / stableSeconds + (sec < stablePulses % stableSeconds ? 1 : 0);
        for (std::uint32_t i = 0; i < pulsesThisSec; ++i) {
            samples[count++] =
                faucet::WaterPulseTraceSample{static_cast<std::uint32_t>((5 + sec) * 1000000UL + i * 5000UL)};
        }
    }
    return count;
}

inline bool saveCompletedCalibrationTrace(faucet::CalibrationSessionTraceStore& traceStore,
                                          std::uint8_t slot,
                                          std::uint32_t sessionId,
                                          const faucet::WaterRecord& record,
                                          std::uint32_t actualMl,
                                          std::uint32_t startupPulses,
                                          std::uint32_t stablePulses,
                                          std::uint32_t stableSeconds) {
    faucet::WaterPulseTraceSample samples[4096]{};
    faucet::WaterPulseTraceBucketSample buckets[faucet::kPulseTraceMaxBucketsPerTrace]{};
    const std::size_t sampleCount = fillCalibrationSamples(samples, 4096, startupPulses, stablePulses, stableSeconds);
    if (sampleCount == 0) {
        return false;
    }

    std::size_t bucketCount = 0;
    std::size_t startupEdgeCount = 0;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const std::size_t bucketIndex = samples[i].elapsedUs / (faucet::kPulseTraceBucketMs * 1000UL);
        if (bucketIndex >= faucet::kPulseTraceMaxBucketsPerTrace) {
            return false;
        }
        ++buckets[bucketIndex].pulseCount;
        bucketCount = std::max(bucketCount, bucketIndex + 1);
        if (samples[i].elapsedUs < faucet::kPulseTraceStartupDetailMs * 1000UL) {
            ++startupEdgeCount;
        }
    }

    faucet::CalibrationStoredTrace stored{};
    stored.sessionId = sessionId;
    stored.attemptIndex = slot;
    stored.trace.traceId = slot + 1;
    stored.trace.startTime = record.startTime;
    stored.trace.record = record;
    stored.trace.bucketCount = bucketCount;
    stored.trace.startupEdgeCount = startupEdgeCount;
    stored.trace.totalPulses = startupPulses + stablePulses;
    stored.trace.actualMl = actualMl;
    stored.trace.pulseMinIntervalUs = faucet::kDefaultPulseMinIntervalUs;
    stored.trace.finalState = faucet::WaterPulseTraceState::Completed;
    stored.trace.finished = true;
    return traceStore.saveValid(
        slot, stored, buckets, bucketCount, samples, startupEdgeCount, actualMl, record.startTime + 10);
}

}  // namespace faucet_test
