#pragma once

#include "app/MeteringScheme.h"

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kDefaultPulseFilterUs = 1000;

struct FlowSnapshot {
    std::uint32_t pulseCount;
    std::uint32_t volumeMl;
    std::uint32_t currentFlowMlPerMin;
    std::uint32_t rejectedPulses;
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
    std::uint32_t volumeFromPulses() const;
    std::uint32_t flowFromInterval(std::uint32_t intervalUs) const;

    MeteringParameters params_;
    std::uint32_t pulseFilterUs_;
    std::uint32_t pulseCount_;
    std::uint32_t rejectedPulses_;
    std::uint32_t lastPulseUs_;
    std::uint32_t previousPulseUs_;
    bool hasPulse_;
};

}  // namespace faucet
