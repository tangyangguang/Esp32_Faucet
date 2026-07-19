#pragma once

#include "app/ValveDriver.h"

#include <cstdint>

namespace faucet {

class PwmValveHardware {
public:
    PwmValveHardware(std::uint8_t pwmPin, std::uint8_t shutdownPin, std::uint8_t channel);

    void forceSafeState();
    bool begin(std::uint32_t frequencyHz);
    bool configureFrequency(std::uint32_t frequencyHz);
    void apply(ValveOutput output);
    std::uint32_t frequencyHz() const;

private:
    std::uint8_t pwmPin_;
    std::uint8_t shutdownPin_;
    std::uint8_t channel_;
    std::uint32_t frequencyHz_;
    bool begun_;
    bool outputEnabled_;
};

}  // namespace faucet
