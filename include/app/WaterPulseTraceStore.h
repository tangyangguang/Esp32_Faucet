#pragma once

#include "app/AppConfig.h"
#include "app/WaterRecordFileStore.h"
#include "app/WaterRecordStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

enum class WaterPulseTraceState : std::uint8_t {
    Running = 0,
    Paused = 1,
    Completed = 2,
    Stopped = 3,
    Error = 4,
    PauseTimeout = 5,
    SafetyStopped = 6,
};

constexpr std::size_t kPulseTraceMaxPauseWindows = 8;
constexpr std::uint32_t kPulseTraceNoEndElapsedUs = UINT32_MAX;

struct WaterPulseTraceSample {
    std::uint32_t elapsedUs;
};

struct WaterPulseTracePauseWindow {
    std::uint32_t startElapsedUs;
    std::uint32_t endElapsedUs;
};

enum PulseTraceFlags : std::uint8_t {
    kPulseTraceFlagBucketOverflow = 1U << 0U,
    kPulseTraceFlagStartupOverflow = 1U << 1U,
    kPulseTraceFlagDroppedPulseOverflow = 1U << 2U,
};

struct WaterPulseTraceBucketSample {
    std::uint16_t pulseCount;
};

struct WaterPulseTrace {
    std::uint32_t traceId;
    std::uint32_t startTime;
    WaterRecord record;
    std::size_t sampleStart;
    std::size_t sampleCount;
    std::size_t bucketStart;
    std::size_t bucketCount;
    std::size_t startupEdgeStart;
    std::size_t startupEdgeCount;
    std::uint32_t totalPulses;
    std::uint32_t actualMl;
    std::uint32_t pulseMinIntervalUs;
    std::uint32_t minIntervalFilteredCount;
    std::uint32_t droppedPulseCount;
    std::uint32_t lastEffectiveElapsedUs;
    WaterPulseTraceState finalState;
    std::uint8_t flags;
    bool finished;
    bool truncated;
    bool resumedAfterPause;
    bool pauseWindowOverflow;
    bool hasEffectivePulse;
    std::uint8_t pauseWindowCount;
    WaterPulseTracePauseWindow pauseWindows[kPulseTraceMaxPauseWindows];
};

struct WaterPulseTraceStats {
    std::size_t traceCount;
    std::size_t traceCapacity;
    std::size_t sampleCount;
    std::size_t sampleCapacity;
    std::size_t sampleCapacityPerTrace;
    std::size_t usedBytes;
    std::uint32_t oldestStartTime;
    std::uint32_t latestStartTime;
};

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
    std::uint32_t rawEdgeDelta;
    std::uint32_t cumulativePulses;
    std::uint32_t cumulativeRawEdges;
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

class WaterPulseTraceStore {
public:
    WaterPulseTraceStore(WaterPulseTrace* traces,
                         std::size_t traceCapacity,
                         WaterPulseTraceSample* samples,
                         std::size_t sampleCapacity,
                         std::size_t recentTraceLimit);
    WaterPulseTraceStore(WaterPulseTrace* traces,
                         std::size_t traceCapacity,
                         WaterPulseTraceBucketSample* buckets,
                         std::size_t bucketCapacity,
                         WaterPulseTraceSample* startupEdges,
                         std::size_t startupEdgeCapacity,
                         std::size_t recentTraceLimit);

    void setRecentTraceLimit(std::size_t recentTraceLimit);
    std::uint32_t beginTrace(std::uint32_t startTime, std::uint32_t pulseMinIntervalUs);
    bool appendPulseEdge(std::uint32_t traceId, std::uint32_t elapsedUs);
    bool appendRawEdge(std::uint32_t traceId, std::uint32_t elapsedUs);
    bool finishTrace(std::uint32_t traceId,
                     const WaterRecord& record,
                     WaterPulseTraceState finalState,
                     std::uint32_t endElapsedUs = kPulseTraceNoEndElapsedUs);
    bool markPaused(std::uint32_t traceId, std::uint32_t elapsedUs);
    bool markResumedAfterPause(std::uint32_t traceId, std::uint32_t elapsedUs = kPulseTraceNoEndElapsedUs);
    bool setActualMl(std::uint32_t traceId, std::uint32_t actualMl);
    bool setActualMlByRecord(const WaterRecord& record, std::uint32_t actualMl);

    const WaterPulseTrace* findById(std::uint32_t traceId) const;
    WaterPulseTrace* findById(std::uint32_t traceId);
    const WaterPulseTrace* findByRecord(const WaterRecord& record) const;
    const WaterPulseTrace* traceAt(std::size_t index) const;
    const WaterPulseTraceSample* sampleAt(const WaterPulseTrace& trace, std::size_t index) const;
    const WaterPulseTraceBucketSample* bucketAt(const WaterPulseTrace& trace, std::size_t index) const;
    const WaterPulseTraceSample* startupEdgeAt(const WaterPulseTrace& trace, std::size_t index) const;
    WaterPulseTraceStats stats() const;
    std::size_t count() const;

private:
    std::size_t usedBytes() const;
    void enforceBudget();
    void dropOldest();
    std::size_t indexOf(std::uint32_t traceId) const;

    WaterPulseTrace* traces_;
    std::size_t traceCapacity_;
    std::size_t traceCount_;
    WaterPulseTraceSample* samples_;
    std::size_t sampleCapacity_;
    std::size_t sampleCount_;
    WaterPulseTraceBucketSample* buckets_;
    std::size_t bucketCapacity_;
    std::size_t bucketCount_;
    WaterPulseTraceSample* startupEdges_;
    std::size_t startupEdgeCapacity_;
    std::size_t startupEdgeCount_;
    std::size_t recentTraceLimit_;
    std::uint32_t nextTraceId_;
};

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceSample* samples,
                                     std::size_t sampleCount,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity);

WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount);
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceSample* samples,
                                               std::size_t sampleCount,
                                               const SegmentedCalibrationOptions& options);
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace, const WaterPulseTraceStore& store);
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceStore& store,
                                               const SegmentedCalibrationOptions& options);
std::uint32_t effectivePulseCount(const WaterPulseTrace& trace,
                                  const WaterPulseTraceSample* samples,
                                  std::size_t sampleCount);
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
SegmentedCalibrationOptions segmentedCalibrationOptionsFromConfig(const SystemConfig& config);

}  // namespace faucet
