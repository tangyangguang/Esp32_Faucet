#include "app/FlowMeter.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <iterator>
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
      filteredPulses_(0),
      lastPulseUs_(0),
      previousPulseUs_(0),
      pulseBuckets_{},
      bucketStartUs_(0),
      bucketIndex_(0),
      displayFlowMlPerMin_(0),
      displayFlowReady_(false),
      hasPulse_(false),
      hasPreviousPulse_(false) {}

void FlowMeter::reset() {
    pulseCount_ = 0;
    filteredPulses_ = 0;
    clearWindow();
}

void FlowMeter::clearWindow() {
    std::fill(std::begin(pulseBuckets_), std::end(pulseBuckets_), 0);
    bucketIndex_ = 0;
    displayFlowMlPerMin_ = 0;
    displayFlowReady_ = false;
    hasPulse_ = false;
    hasPreviousPulse_ = false;
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
        if (filteredPulses_ < UINT32_MAX) {
            ++filteredPulses_;
        }
        return false;
    }
    hasPreviousPulse_ = hasPulse_;
    previousPulseUs_ = lastPulseUs_;
    if (!hasPulse_) {
        bucketStartUs_ = nowUs;
    }
    lastPulseUs_ = nowUs;
    hasPulse_ = true;
    if (pulseCount_ < std::numeric_limits<std::uint32_t>::max()) {
        ++pulseCount_;
    }
    const std::size_t steps = elapsedSince(nowUs, bucketStartUs_) / kBucketUs;
    if (steps > 0) {
        const std::size_t binsToClear = std::min(steps, kBucketCount);
        for (std::size_t i = 0; i < binsToClear; ++i) {
            bucketIndex_ = (bucketIndex_ + 1) % kBucketCount;
            pulseBuckets_[bucketIndex_] = 0;
        }
        bucketStartUs_ += static_cast<std::uint32_t>(steps) * kBucketUs;
    }
    if (pulseBuckets_[bucketIndex_] < UINT32_MAX) {
        ++pulseBuckets_[bucketIndex_];
    }
    return true;
}

FlowSnapshot FlowMeter::snapshot(std::uint32_t nowUs) const {
    std::uint32_t instantFlow = 0;
    if (hasPreviousPulse_) {
        const std::uint32_t interval = elapsedSince(lastPulseUs_, previousPulseUs_);
        const std::uint32_t age = elapsedSince(nowUs, lastPulseUs_);
        if (interval > 0 && age <= 2000000UL) {
            instantFlow = flowFromInterval(interval);
        }
    }
    const std::uint32_t realtimeFlow = windowFlow(nowUs);
    const std::uint32_t smoothedFlow = displayFlow(nowUs, realtimeFlow);
    return FlowSnapshot{pulseCount_, volumeMl(), smoothedFlow, instantFlow, realtimeFlow, smoothedFlow, filteredPulses_};
}

std::uint32_t FlowMeter::volumeMl() const {
    return volumeMlForPulses(params_, pulseCount_);
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
    const std::uint32_t age = elapsedSince(nowUs, bucketStartUs_);
    if (age >= windowUs + kBucketUs) {
        return 0;
    }
    // Include the bucket intersecting the oldest edge of the window.
    const std::size_t count = (windowUs + kBucketUs - 1 - age) / kBucketUs + 1;
    std::uint64_t pulses = 0;
    std::size_t index = bucketIndex_;
    for (std::size_t i = 0; i < count; ++i) {
        pulses += pulseBuckets_[index];
        index = index == 0 ? kBucketCount - 1 : index - 1;
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(pulses, UINT32_MAX));
}

std::uint32_t FlowMeter::windowFlow(std::uint32_t nowUs) const {
    if (!hasPulse_ || elapsedSince(nowUs, lastPulseUs_) > 3000000UL) {
        return 0;
    }
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
