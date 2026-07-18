#pragma once

#include "app/ValveDriver.h"

#include <cstdint>

namespace faucet {

class PwmValveHardware {
public:
    PwmValveHardware(std::uint8_t pwmPin, std::uint8_t shutdownPin, std::uint8_t channel);

    void forceSafeState();
    void begin();
    void apply(ValveOutput output);

private:
    std::uint8_t pwmPin_;
    std::uint8_t shutdownPin_;
    std::uint8_t channel_;
};

}  // namespace faucet
