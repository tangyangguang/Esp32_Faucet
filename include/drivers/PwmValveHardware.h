#pragma once

#include "app/ValveDriver.h"

#include <cstdint>

namespace faucet {

class PwmValveHardware {
public:
    PwmValveHardware(std::uint8_t pin, std::uint8_t channel);

    void begin();
    void apply(ValveOutput output);

private:
    std::uint8_t pin_;
    std::uint8_t channel_;
};

}  // namespace faucet
