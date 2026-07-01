#pragma once

#include "app/WaterPulseTraceTypes.h"

namespace faucet {

class WaterPulseTraceStore;

struct WaterPulseTraceAnalysis {
    bool stable;
    std::uint32_t stableStartSec;
    std::uint32_t startupPulseCount;
    std::uint32_t stablePulseCount;
    float stablePulsePerSec;
    std::uint8_t confidence;
};

struct SegmentedCalibrationOptions {
    std::uint32_t pulseMinIntervalUsOverride;
    std::uint32_t stableWindowSec;
    std::uint8_t stableTolerancePercent;
    std::uint32_t minVolumeSpanMl;
    std::uint32_t maxErrorMl;
    std::uint16_t maxRelativeErrorTenthPercent;
};

enum class SegmentedCalibrationRejectReason : std::uint8_t {
    None = 0,
    NotEnoughSamples = 1,
    VolumeSpanTooSmall = 2,
    DegenerateFit = 3,
    InvalidFit = 4,
    ErrorTooHigh = 5,
};

constexpr std::uint8_t kSegmentedCalibrationQualityNone = 0;
constexpr std::uint8_t kSegmentedCalibrationQualityVolumeSpanSmall = 1U << 0U;
constexpr std::uint8_t kSegmentedCalibrationQualityErrorHigh = 1U << 1U;

struct WaterPulseTraceBucket {
    std::uint32_t startSec;
    std::uint32_t durationSec;
    std::uint32_t pulseDelta;
    std::uint32_t cumulativePulses;
    WaterPulseTraceState state;
};

struct SegmentedCalibrationSample {
    constexpr SegmentedCalibrationSample()
        : actualMl(0),
          totalPulses(0),
          startupPulseCount(0),
          stablePulseCount(0),
          startupDurationSec(0),
          stablePulsePerSec(0.0f) {}

    constexpr SegmentedCalibrationSample(std::uint32_t actualMlValue,
                                         std::uint32_t totalPulsesValue,
                                         std::uint32_t startupPulseCountValue,
                                         std::uint32_t stablePulseCountValue,
                                         std::uint32_t startupDurationSecValue,
                                         float stablePulsePerSecValue = 0.0f)
        : actualMl(actualMlValue),
          totalPulses(totalPulsesValue),
          startupPulseCount(startupPulseCountValue),
          stablePulseCount(stablePulseCountValue),
          startupDurationSec(startupDurationSecValue),
          stablePulsePerSec(stablePulsePerSecValue) {}

    std::uint32_t actualMl;
    std::uint32_t totalPulses;
    std::uint32_t startupPulseCount;
    std::uint32_t stablePulseCount;
    std::uint32_t startupDurationSec;
    float stablePulsePerSec;
};

struct SegmentedCalibrationResult {
    bool valid;
    SegmentedCalibrationRejectReason rejectReason;
    std::uint8_t qualityWarnings;
    std::uint16_t sampleCount;
    std::uint16_t excludedSampleCount;
    std::uint32_t startupDurationSec;
    std::uint32_t startupDurationMs;
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t stablePulsePerLiter;
    std::uint32_t stableFlowMlPerMin;
    std::uint32_t minActualMl;
    std::uint32_t maxActualMl;
    std::uint32_t maxErrorMl;
    std::uint16_t maxRelativeErrorTenthPercent;
};

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceBucketSample* compactBuckets,
                                     std::size_t compactBucketCount,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity);

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceBucketSample* compactBuckets,
                                               std::size_t compactBucketCount);
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceBucketSample* compactBuckets,
                                               std::size_t compactBucketCount,
                                               const SegmentedCalibrationOptions& options);
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace, const WaterPulseTraceStore& store);
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceStore& store,
                                               const SegmentedCalibrationOptions& options);
bool waterPulseTraceAnalysisEligible(const WaterPulseTrace& trace);

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceStore& store,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity);

bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 SegmentedCalibrationResult& result);
bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 const SegmentedCalibrationOptions& options,
                                 SegmentedCalibrationResult& result);

SegmentedCalibrationOptions defaultSegmentedCalibrationOptions();

}  // namespace faucet
