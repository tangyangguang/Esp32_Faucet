#include "drivers/PwmValveHardware.h"

#include "drivers/BoardPins.h"

#include <Arduino.h>

namespace faucet {
namespace {

constexpr std::uint32_t kValvePwmFrequencyHz = 20000;

}  // namespace

PwmValveHardware::PwmValveHardware(std::uint8_t pwmPin, std::uint8_t shutdownPin, std::uint8_t channel)
    : pwmPin_(pwmPin), shutdownPin_(shutdownPin), channel_(channel) {}

void PwmValveHardware::forceSafeState() {
    pinMode(shutdownPin_, OUTPUT);
    digitalWrite(shutdownPin_, HIGH);
    pinMode(pwmPin_, OUTPUT);
    digitalWrite(pwmPin_, LOW);
}

void PwmValveHardware::begin() {
    forceSafeState();
    ledcSetup(channel_, kValvePwmFrequencyHz, kLedcResolutionBits);
    ledcAttachPin(pwmPin_, channel_);
    ledcWrite(channel_, 0);
}

void PwmValveHardware::apply(ValveOutput output) {
    if (!output.enabled || output.dutyPercent == 0) {
        digitalWrite(shutdownPin_, HIGH);
        ledcWrite(channel_, 0);
        return;
    }
    ledcWrite(channel_, ledcDutyFromPercent(output.dutyPercent));
    digitalWrite(shutdownPin_, LOW);
}

}  // namespace faucet
