#pragma once

#include "app/AppStorageStatus.h"
#include "app/WaterPulseTraceStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kCalibrationSessionTraceSlots = 6;

struct CalibrationStoredTrace {
    bool valid = false;
    bool pendingActual = false;
    std::uint32_t sampleId = 0;
    std::uint32_t sessionId = 0;
    std::uint8_t attemptIndex = 0;
    std::uint32_t actualMl = 0;
    std::uint32_t savedAt = 0;
    WaterPulseTrace trace{};
};

class CalibrationSessionTraceStore {
public:
    CalibrationSessionTraceStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool clear();
    bool clearForNewSession(std::uint32_t sessionId);
    bool savePending(std::uint8_t slot,
                     const CalibrationStoredTrace& trace,
                     const WaterPulseTraceBucketSample* buckets,
                     std::size_t bucketCount,
                     const WaterPulseTraceSample* startupEdges,
                     std::size_t startupEdgeCount);
    bool commitValid(std::uint8_t slot, std::uint32_t actualMl, std::uint32_t savedAt);
    bool invalidate(std::uint8_t slot);
    bool load(std::uint8_t slot, CalibrationStoredTrace& trace) const;
    std::size_t readBuckets(std::uint8_t slot,
                            WaterPulseTraceBucketSample* output,
                            std::size_t outputCapacity) const;
    std::size_t readStartupEdges(std::uint8_t slot,
                                 WaterPulseTraceSample* output,
                                 std::size_t outputCapacity) const;
    std::size_t capacity() const;
    bool ready() const;
    AppStorageStatus status() const;
    const char* storageName() const;

private:
    WaterRecordFileBackend& backend_;
    const char* path_;
    bool ready_;
    AppStorageStatus status_;
};

}  // namespace faucet
