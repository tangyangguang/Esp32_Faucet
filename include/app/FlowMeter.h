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
    bool setMeteringParameters(MeteringParameters params);
    void setPulseFilterUs(std::uint32_t pulseFilterUs);
    bool onPulse(std::uint32_t nowUs);
    FlowSnapshot snapshot(std::uint32_t nowUs) const;

private:
    static constexpr std::size_t kRecentPulseCapacity = 512;

    std::uint32_t volumeFromPulses() const;
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
    std::uint32_t recentPulseUs_[kRecentPulseCapacity];
    std::size_t recentPulseStart_;
    std::size_t recentPulseCount_;
    mutable std::uint32_t displayFlowMlPerMin_;
    mutable bool displayFlowReady_;
    bool hasPulse_;
};

}  // namespace faucet
