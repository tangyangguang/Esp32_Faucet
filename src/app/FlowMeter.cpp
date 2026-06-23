#include "app/FlowMeter.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace faucet {
namespace {

bool validParams(const MeteringParameters& params) {
    if (params.stablePulsePerLiter < kMinSegmentedPulsePerLiter ||
        params.stablePulsePerLiter > kMaxSegmentedPulsePerLiter ||
        params.startupPulseCount > kMaxSegmentedStartupPulseCount ||
        params.startupVolumeMl > kMaxSegmentedStartupVolumeMl) {
        return false;
    }
    return (params.startupPulseCount == 0 && params.startupVolumeMl == 0) ||
           params.startupPulseCount > 0;
}

}  // namespace

FlowMeter::FlowMeter(MeteringParameters params, std::uint32_t pulseFilterUs)
    : params_(validParams(params) ? params : defaultMeteringParameters()),
      pulseFilterUs_(pulseFilterUs),
      pulseCount_(0),
      rejectedPulses_(0),
      lastPulseUs_(0),
      previousPulseUs_(0),
      recentPulseUs_{},
      recentPulseStart_(0),
      recentPulseCount_(0),
      displayFlowMlPerMin_(0),
      displayFlowReady_(false),
      hasPulse_(false) {}

void FlowMeter::reset() {
    pulseCount_ = 0;
    rejectedPulses_ = 0;
    lastPulseUs_ = 0;
    previousPulseUs_ = 0;
    recentPulseStart_ = 0;
    recentPulseCount_ = 0;
    displayFlowMlPerMin_ = 0;
    displayFlowReady_ = false;
    hasPulse_ = false;
}

bool FlowMeter::setMeteringParameters(MeteringParameters params) {
    if (!validParams(params)) {
        return false;
    }
    params_ = params;
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
    const std::size_t index = (recentPulseStart_ + recentPulseCount_) % kRecentPulseCapacity;
    recentPulseUs_[index] = nowUs;
    if (recentPulseCount_ < kRecentPulseCapacity) {
        ++recentPulseCount_;
    } else {
        recentPulseStart_ = (recentPulseStart_ + 1U) % kRecentPulseCapacity;
    }
    return true;
}

FlowSnapshot FlowMeter::snapshot(std::uint32_t nowUs) const {
    std::uint32_t instantFlow = 0;
    if (hasPulse_ && previousPulseUs_ != 0) {
        const std::uint32_t interval = elapsedSince(lastPulseUs_, previousPulseUs_);
        const std::uint32_t age = elapsedSince(nowUs, lastPulseUs_);
        if (interval > 0 && age <= 2000000UL) {
            instantFlow = flowFromInterval(interval);
        }
    }
    const std::uint32_t realtimeFlow = windowFlow(nowUs);
    const std::uint32_t smoothedFlow = displayFlow(nowUs, realtimeFlow);
    return FlowSnapshot{pulseCount_, volumeFromPulses(), smoothedFlow, instantFlow, realtimeFlow, smoothedFlow, rejectedPulses_};
}

std::uint32_t FlowMeter::volumeFromPulses() const {
    if (pulseCount_ == 0) {
        return 0;
    }
    if (params_.startupPulseCount > 0 && pulseCount_ <= params_.startupPulseCount) {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(pulseCount_) * params_.startupVolumeMl + params_.startupPulseCount / 2ULL) /
            params_.startupPulseCount);
    }
    const std::uint32_t stablePulses =
        pulseCount_ > params_.startupPulseCount ? pulseCount_ - params_.startupPulseCount : pulseCount_;
    const std::uint64_t stableMl =
        (static_cast<std::uint64_t>(stablePulses) * 1000ULL + params_.stablePulsePerLiter / 2ULL) /
        params_.stablePulsePerLiter;
    const std::uint64_t total = static_cast<std::uint64_t>(params_.startupVolumeMl) + stableMl;
    return total > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max()
                                                            : static_cast<std::uint32_t>(total);
}

std::uint32_t FlowMeter::flowFromInterval(std::uint32_t intervalUs) const {
    if (intervalUs == 0 || params_.stablePulsePerLiter == 0) {
        return 0;
    }
    const double pulsesPerMinute = 60000000.0 / static_cast<double>(intervalUs);
    const double mlPerMinute = pulsesPerMinute * 1000.0 / static_cast<double>(params_.stablePulsePerLiter);
    if (mlPerMinute <= 0.0) {
        return 0;
    }
    if (mlPerMinute >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(mlPerMinute + 0.5);
}

std::uint32_t FlowMeter::flowFromPulseWindow(std::uint32_t pulseCount, std::uint32_t windowUs) const {
    if (pulseCount == 0 || windowUs == 0 || params_.stablePulsePerLiter == 0) {
        return 0;
    }
    const double pulsesPerMinute = static_cast<double>(pulseCount) * 60000000.0 / static_cast<double>(windowUs);
    const double mlPerMinute = pulsesPerMinute * 1000.0 / static_cast<double>(params_.stablePulsePerLiter);
    if (mlPerMinute <= 0.0) {
        return 0;
    }
    if (mlPerMinute >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(mlPerMinute + 0.5);
}

std::uint32_t FlowMeter::recentPulseCount(std::uint32_t nowUs, std::uint32_t windowUs) const {
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < recentPulseCount_; ++i) {
        const std::size_t index = (recentPulseStart_ + i) % kRecentPulseCapacity;
        if (elapsedSince(nowUs, recentPulseUs_[index]) <= windowUs) {
            ++count;
        }
    }
    return count;
}

std::uint32_t FlowMeter::windowFlow(std::uint32_t nowUs) const {
    if (!hasPulse_ || elapsedSince(nowUs, lastPulseUs_) > 3000000UL) {
        return 0;
    }
    constexpr std::uint32_t kDefaultWindowUs = 2000000UL;
    constexpr std::uint32_t kLowFlowWindowUs = 3000000UL;
    std::uint32_t pulses = recentPulseCount(nowUs, kDefaultWindowUs);
    std::uint32_t windowUs = kDefaultWindowUs;
    if (pulses < 2U) {
        pulses = recentPulseCount(nowUs, kLowFlowWindowUs);
        windowUs = kLowFlowWindowUs;
    }
    return flowFromPulseWindow(pulses, windowUs);
}

std::uint32_t FlowMeter::displayFlow(std::uint32_t nowUs, std::uint32_t windowFlow) const {
    if (!hasPulse_ || elapsedSince(nowUs, lastPulseUs_) > 3000000UL || windowFlow == 0) {
        displayFlowMlPerMin_ = 0;
        displayFlowReady_ = false;
        return 0;
    }
    if (!displayFlowReady_ || displayFlowMlPerMin_ == 0) {
        displayFlowMlPerMin_ = windowFlow;
        displayFlowReady_ = true;
        return displayFlowMlPerMin_;
    }
    const std::uint64_t smoothed =
        static_cast<std::uint64_t>(displayFlowMlPerMin_) * 7ULL + static_cast<std::uint64_t>(windowFlow) * 3ULL;
    displayFlowMlPerMin_ = static_cast<std::uint32_t>((smoothed + 5ULL) / 10ULL);
    return displayFlowMlPerMin_;
}

}  // namespace faucet
