#pragma once

#include "app/BeepDriver.h"

#include <cstdint>

namespace faucet {

class PwmBeepHardware {
public:
    PwmBeepHardware(std::uint8_t pin, std::uint8_t channel);

    void begin();
    void apply(BeepOutput output);

private:
    std::uint8_t pin_;
    std::uint8_t channel_;
    std::uint16_t currentFrequencyHz_;
};

}  // namespace faucet
