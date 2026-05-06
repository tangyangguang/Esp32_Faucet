#include "drivers/PwmValveHardware.h"

#include "drivers/BoardPins.h"

#include <Arduino.h>

namespace faucet {
namespace {

constexpr std::uint32_t kValvePwmFrequencyHz = 20000;

std::uint32_t dutyFromPercent(std::uint8_t percent) {
    return static_cast<std::uint32_t>(percent) * 255UL / 100UL;
}

}  // namespace

PwmValveHardware::PwmValveHardware(std::uint8_t pin, std::uint8_t channel) : pin_(pin), channel_(channel) {}

void PwmValveHardware::begin() {
    ledcSetup(channel_, kValvePwmFrequencyHz, kLedcResolutionBits);
    ledcAttachPin(pin_, channel_);
    ledcWrite(channel_, 0);
}

void PwmValveHardware::apply(ValveOutput output) {
    ledcWrite(channel_, output.enabled ? dutyFromPercent(output.dutyPercent) : 0);
}

}  // namespace faucet
