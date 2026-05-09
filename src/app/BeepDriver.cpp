#include "app/BeepDriver.h"

#include "app/TimeUtils.h"

namespace faucet {

BeepDriver::BeepDriver(bool enabled)
    : configuredEnabled_(enabled),
      pattern_(BeepPattern::None),
      startedMs_(0),
      outputEnabled_(false) {}

void BeepDriver::setEnabled(bool enabled) {
    configuredEnabled_ = enabled;
    if (!configuredEnabled_) {
        pattern_ = BeepPattern::None;
        outputEnabled_ = false;
    }
}

void BeepDriver::play(BeepPattern pattern, std::uint32_t nowMs) {
    if (!configuredEnabled_ || pattern == BeepPattern::None) {
        return;
    }
    pattern_ = pattern;
    startedMs_ = nowMs;
    outputEnabled_ = true;
}

void BeepDriver::tick(std::uint32_t nowMs) {
    if (pattern_ == BeepPattern::None || !outputEnabled_) {
        return;
    }
    if (elapsedAtLeast(nowMs, startedMs_, durationMs(pattern_))) {
        pattern_ = BeepPattern::None;
        outputEnabled_ = false;
    }
}

BeepOutput BeepDriver::output() const {
    if (!outputEnabled_ || pattern_ == BeepPattern::None) {
        return {false, 0, 0};
    }
    return {true, frequencyHz(pattern_), 50};
}

BeepPattern BeepDriver::activePattern() const {
    return pattern_;
}

std::uint32_t BeepDriver::durationMs(BeepPattern pattern) {
    switch (pattern) {
        case BeepPattern::Click:
            return 40;
        case BeepPattern::Done:
            return 180;
        case BeepPattern::Error:
            return 600;
        case BeepPattern::None:
        default:
            return 0;
    }
}

std::uint16_t BeepDriver::frequencyHz(BeepPattern pattern) {
    switch (pattern) {
        case BeepPattern::Click:
            return 2400;
        case BeepPattern::Done:
            return 1800;
        case BeepPattern::Error:
            return 900;
        case BeepPattern::None:
        default:
            return 0;
    }
}

}  // namespace faucet
