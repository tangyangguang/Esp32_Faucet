#include "app/FlowMeter.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace faucet {
namespace {

bool validPulsePerMl(float value) {
    return std::isfinite(value) && value >= kMinPulsePerMl && value <= kMaxPulsePerMl;
}

}  // namespace

FlowMeter::FlowMeter(float pulsePerMl, std::uint32_t pulseFilterUs)
    : pulsePerMl_(validPulsePerMl(pulsePerMl) ? pulsePerMl : kDefaultPulsePerMl),
      pulseFilterUs_(pulseFilterUs),
      pulseCount_(0),
      rejectedPulses_(0),
      lastPulseUs_(0),
      previousPulseUs_(0),
      hasPulse_(false) {}

void FlowMeter::reset() {
    pulseCount_ = 0;
    rejectedPulses_ = 0;
    lastPulseUs_ = 0;
    previousPulseUs_ = 0;
    hasPulse_ = false;
}

bool FlowMeter::setPulsePerMl(float pulsePerMl) {
    if (!validPulsePerMl(pulsePerMl)) {
        return false;
    }
    pulsePerMl_ = pulsePerMl;
    return true;
}

void FlowMeter::setPulseFilterUs(std::uint32_t pulseFilterUs) {
    pulseFilterUs_ = pulseFilterUs;
}

bool FlowMeter::onPulse(std::uint32_t nowUs) {
    if (hasPulse_ && elapsedSince(nowUs, lastPulseUs_) < pulseFilterUs_) {
        ++rejectedPulses_;
        return false;
    }
    previousPulseUs_ = hasPulse_ ? lastPulseUs_ : 0;
    lastPulseUs_ = nowUs;
    hasPulse_ = true;
    if (pulseCount_ < std::numeric_limits<std::uint32_t>::max()) {
        ++pulseCount_;
    }
    return true;
}

FlowSnapshot FlowMeter::snapshot(std::uint32_t nowUs) const {
    std::uint32_t flow = 0;
    if (hasPulse_ && previousPulseUs_ != 0) {
        const std::uint32_t interval = elapsedSince(lastPulseUs_, previousPulseUs_);
        const std::uint32_t age = elapsedSince(nowUs, lastPulseUs_);
        if (interval > 0 && age <= 2000000UL) {
            flow = flowFromInterval(interval);
        }
    }
    return FlowSnapshot{pulseCount_, volumeFromPulses(), flow, rejectedPulses_};
}

std::uint32_t FlowMeter::volumeFromPulses() const {
    const float volume = static_cast<float>(pulseCount_) / pulsePerMl_;
    if (volume <= 0.0f) {
        return 0;
    }
    if (volume >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(volume + 0.5f);
}

std::uint32_t FlowMeter::flowFromInterval(std::uint32_t intervalUs) const {
    const double pulsesPerMinute = 60000000.0 / static_cast<double>(intervalUs);
    const double mlPerMinute = pulsesPerMinute / static_cast<double>(pulsePerMl_);
    if (mlPerMinute <= 0.0) {
        return 0;
    }
    if (mlPerMinute >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(mlPerMinute + 0.5);
}

}  // namespace faucet
