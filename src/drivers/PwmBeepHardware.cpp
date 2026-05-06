#include "drivers/PwmBeepHardware.h"

#include "drivers/BoardPins.h"

#include <Arduino.h>

namespace faucet {
namespace {

std::uint32_t dutyFromPercent(std::uint8_t percent) {
    return static_cast<std::uint32_t>(percent) * 255UL / 100UL;
}

}  // namespace

PwmBeepHardware::PwmBeepHardware(std::uint8_t pin, std::uint8_t channel)
    : pin_(pin), channel_(channel), currentFrequencyHz_(0) {}

void PwmBeepHardware::begin() {
    ledcSetup(channel_, 2000, kLedcResolutionBits);
    ledcAttachPin(pin_, channel_);
    ledcWrite(channel_, 0);
}

void PwmBeepHardware::apply(BeepOutput output) {
    if (!output.enabled || output.frequencyHz == 0) {
        ledcWrite(channel_, 0);
        return;
    }
    if (currentFrequencyHz_ != output.frequencyHz) {
        ledcSetup(channel_, output.frequencyHz, kLedcResolutionBits);
        currentFrequencyHz_ = output.frequencyHz;
    }
    ledcWrite(channel_, dutyFromPercent(output.dutyPercent));
}

}  // namespace faucet
