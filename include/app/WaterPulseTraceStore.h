#pragma once

#include "app/AppTypes.h"
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
};

struct WaterPulseTraceSample {
    std::uint16_t pulseDelta;
    WaterPulseTraceState state;
    std::uint8_t reserved;
};

struct WaterPulseTrace {
    std::uint32_t traceId;
    std::uint32_t startTime;
    WaterRecord record;
    std::size_t sampleStart;
    std::size_t sampleCount;
    std::uint32_t totalPulses;
    std::uint32_t actualMl;
    bool finished;
};

struct WaterPulseTraceStats {
    std::size_t traceCount;
    std::size_t traceCapacity;
    std::size_t sampleCount;
    std::size_t sampleCapacity;
    std::size_t usedBytes;
    std::size_t budgetBytes;
    std::uint8_t usagePercent;
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

struct WaterPulseTraceBucket {
    std::uint32_t startSec;
    std::uint32_t durationSec;
    std::uint32_t pulseDelta;
    std::uint32_t cumulativePulses;
    WaterPulseTraceState state;
};

struct SegmentedCalibrationSample {
    std::uint32_t actualMl;
    std::uint32_t totalPulses;
    std::uint32_t startupPulseCount;
    std::uint32_t stablePulseCount;
    std::uint32_t startupDurationSec;
};

struct SegmentedCalibrationResult {
    bool valid;
    std::uint32_t startupDurationSec;
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t startupPulsePerLiter;
    std::uint32_t stablePulsePerLiter;
    std::uint32_t overallPulsePerLiter;
};

class WaterPulseTraceStore {
public:
    WaterPulseTraceStore(WaterPulseTrace* traces,
                         std::size_t traceCapacity,
                         WaterPulseTraceSample* samples,
                         std::size_t sampleCapacity,
                         std::size_t budgetBytes);

    void setBudgetBytes(std::size_t budgetBytes);
    std::uint32_t beginTrace(std::uint32_t startTime);
    bool appendSecond(std::uint32_t traceId, std::uint32_t pulseDelta, WaterPulseTraceState state);
    bool finishTrace(std::uint32_t traceId, const WaterRecord& record, WaterPulseTraceState finalState);
    bool setActualMl(std::uint32_t traceId, std::uint32_t actualMl);

    const WaterPulseTrace* findById(std::uint32_t traceId) const;
    WaterPulseTrace* findById(std::uint32_t traceId);
    const WaterPulseTrace* findByRecord(const WaterRecord& record) const;
    const WaterPulseTrace* traceAt(std::size_t index) const;
    const WaterPulseTraceSample* sampleAt(const WaterPulseTrace& trace, std::size_t index) const;
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
    std::size_t budgetBytes_;
    std::uint32_t nextTraceId_;
};

class WaterPulseTraceFileStore {
public:
    WaterPulseTraceFileStore(WaterRecordFileBackend& backend, const char* pathPrefix, std::size_t sampleCapacityPerTrace);

    bool begin();
    bool save(const WaterPulseTrace& trace,
              const WaterPulseTraceSample* samples,
              std::size_t sampleCount,
              std::uint32_t* savedTraceId = nullptr);
    bool remove(std::uint32_t traceId);
    bool findById(std::uint32_t traceId, WaterPulseTrace& output) const;
    bool findByRecord(const WaterRecord& record, WaterPulseTrace& output) const;
    std::size_t readSamples(std::uint32_t traceId, WaterPulseTraceSample* output, std::size_t outputCapacity) const;
    bool containsRecord(const WaterRecord& record) const;
    std::size_t sampleCapacityPerTrace() const;
    bool ready() const;

private:
    std::uint32_t keyForRecord(const WaterRecord& record) const;
    bool pathForKey(std::uint32_t key, char* out, std::size_t len) const;
    bool readTraceFile(std::uint32_t key, WaterPulseTrace& output) const;
    std::size_t sampleOffset() const;

    WaterRecordFileBackend& backend_;
    const char* pathPrefix_;
    std::size_t sampleCapacityPerTrace_;
    bool ready_;
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
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace, const WaterPulseTraceStore& store);

std::size_t aggregateWaterPulseTrace(const WaterPulseTrace& trace,
                                     const WaterPulseTraceStore& store,
                                     std::uint32_t bucketSeconds,
                                     WaterPulseTraceBucket* buckets,
                                     std::size_t bucketCapacity);

bool computeSegmentedCalibration(const SegmentedCalibrationSample* samples,
                                 std::size_t sampleCount,
                                 SegmentedCalibrationResult& result);

}  // namespace faucet
