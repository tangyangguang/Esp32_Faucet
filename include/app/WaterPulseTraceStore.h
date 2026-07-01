#pragma once

#include "app/WaterPulseTraceTypes.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class WaterPulseTraceStore {
public:
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
    bool finishTrace(std::uint32_t traceId,
                     const WaterRecord& record,
                     WaterPulseTraceState finalState);
    bool setActualMlByRecord(const WaterRecord& record, std::uint32_t actualMl);

    const WaterPulseTrace* findById(std::uint32_t traceId) const;
    WaterPulseTrace* findById(std::uint32_t traceId);
    const WaterPulseTrace* findByRecord(const WaterRecord& record) const;
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
    WaterPulseTraceBucketSample* buckets_;
    std::size_t bucketCapacity_;
    std::size_t bucketCount_;
    WaterPulseTraceSample* startupEdges_;
    std::size_t startupEdgeCapacity_;
    std::size_t startupEdgeCount_;
    std::size_t recentTraceLimit_;
    std::uint32_t nextTraceId_;
};

}  // namespace faucet
