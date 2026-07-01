#pragma once

#include "app/AppConfig.h"
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

struct WaterPulseTraceSample {
    std::uint32_t elapsedUs;
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
    bool hasEffectivePulse;
};

struct WaterPulseTraceStats {
    std::size_t traceCount;
    std::size_t traceCapacity;
    std::size_t bucketCount;
    std::size_t bucketCapacity;
    std::size_t startupEdgeCount;
    std::size_t startupEdgeCapacity;
    std::size_t usedBytes;
    std::uint32_t oldestStartTime;
    std::uint32_t latestStartTime;
};

}  // namespace faucet
