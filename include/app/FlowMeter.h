#pragma once

#include "app/MeteringScheme.h"

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kDefaultPulseFilterUs = 1000;

struct FlowSnapshot {
    std::uint32_t pulseCount;
    std::uint32_t volumeMl;
    std::uint32_t currentFlowMlPerMin;
    std::uint32_t instantFlowMlPerMin;
    std::uint32_t windowFlowMlPerMin;
    std::uint32_t displayFlowMlPerMin;
    std::uint32_t filteredPulses;
};

class FlowMeter {
public:
    explicit FlowMeter(MeteringParameters params = defaultMeteringParameters(),
                       std::uint32_t pulseFilterUs = kDefaultPulseFilterUs);

    void reset();
    void clearWindow();
    bool setMeteringParameters(MeteringParameters params);
    void setPulseFilterUs(std::uint32_t pulseFilterUs);
    bool onPulse(std::uint32_t nowUs);
    std::uint32_t volumeMl() const;
    FlowSnapshot snapshot(std::uint32_t nowUs) const;

private:
    static constexpr std::uint32_t kBucketUs = 10000;
    static constexpr std::uint32_t kDefaultWindowUs = 2000000;
    static constexpr std::uint32_t kLowFlowWindowUs = 3000000;
    static constexpr std::size_t kBucketCount = kLowFlowWindowUs / kBucketUs + 1;

    std::uint32_t flowFromInterval(std::uint32_t intervalUs) const;
    std::uint32_t flowFromPulseWindow(std::uint32_t pulseCount, std::uint32_t windowUs) const;
    std::uint32_t recentPulseCount(std::uint32_t nowUs, std::uint32_t windowUs) const;
    std::uint32_t windowFlow(std::uint32_t nowUs) const;
    std::uint32_t displayFlow(std::uint32_t nowUs, std::uint32_t windowFlow) const;

    MeteringParameters params_;
    std::uint32_t pulseFilterUs_;
    std::uint32_t pulseCount_;
    std::uint32_t filteredPulses_;
    std::uint32_t lastPulseUs_;
    std::uint32_t previousPulseUs_;
    std::uint32_t pulseBuckets_[kBucketCount];
    std::uint32_t bucketStartUs_;
    std::size_t bucketIndex_;
    mutable std::uint32_t displayFlowMlPerMin_;
    mutable bool displayFlowReady_;
    bool hasPulse_;
    bool hasPreviousPulse_;
};

}  // namespace faucet
