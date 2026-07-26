#pragma once

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kCancelInterruptConfirmUs = 1000;

struct CancelInterruptFilterResult {
    bool pressed;
    bool emergencyStop;
};

class CancelInterruptFilter {
public:
    void reset(bool pressed, std::uint32_t nowUs = 0) {
        confirming_ = false;
        confirmed_ = pressed;
        interruptSeen_ = false;
        emergencyEmitted_ = false;
        confirmStartedUs_ = nowUs;
    }

    CancelInterruptFilterResult update(bool rawPressed,
                                       bool interruptPending,
                                       std::uint32_t nowUs) {
        if (!rawPressed) {
            reset(false, nowUs);
            return {false, false};
        }

        interruptSeen_ = interruptSeen_ || interruptPending;
        if (!confirmed_ && !confirming_) {
            confirming_ = true;
            confirmStartedUs_ = nowUs;
        }
        if (confirming_ &&
            static_cast<std::uint32_t>(nowUs - confirmStartedUs_) >= kCancelInterruptConfirmUs) {
            confirming_ = false;
            confirmed_ = true;
        }

        const bool emergencyStop = confirmed_ && interruptSeen_ && !emergencyEmitted_;
        if (emergencyStop) {
            emergencyEmitted_ = true;
        }
        return {confirmed_, emergencyStop};
    }

private:
    bool confirming_ = false;
    bool confirmed_ = false;
    bool interruptSeen_ = false;
    bool emergencyEmitted_ = false;
    std::uint32_t confirmStartedUs_ = 0;
};

}  // namespace faucet
