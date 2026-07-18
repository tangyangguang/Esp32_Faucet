#include "drivers/PwmValveHardware.h"

#include "drivers/BoardPins.h"

#include <Arduino.h>

namespace faucet {
PwmValveHardware::PwmValveHardware(std::uint8_t pwmPin, std::uint8_t shutdownPin, std::uint8_t channel)
    : pwmPin_(pwmPin),
      shutdownPin_(shutdownPin),
      channel_(channel),
      frequencyHz_(kDefaultValvePwmFrequencyHz),
      begun_(false) {}

void PwmValveHardware::forceSafeState() {
    pinMode(shutdownPin_, OUTPUT);
    digitalWrite(shutdownPin_, HIGH);
    pinMode(pwmPin_, OUTPUT);
    digitalWrite(pwmPin_, LOW);
}

bool PwmValveHardware::begin(std::uint32_t frequencyHz) {
    forceSafeState();
    if (frequencyHz < kMinValvePwmFrequencyHz || frequencyHz > kMaxValvePwmFrequencyHz ||
        ledcSetup(channel_, frequencyHz, kLedcResolutionBits) == 0) {
        begun_ = false;
        return false;
    }
    ledcAttachPin(pwmPin_, channel_);
    ledcWrite(channel_, 0);
    frequencyHz_ = frequencyHz;
    begun_ = true;
    return true;
}

bool PwmValveHardware::configureFrequency(std::uint32_t frequencyHz) {
    if (begun_ && frequencyHz == frequencyHz_) {
        return true;
    }
    return begin(frequencyHz);
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

std::uint32_t PwmValveHardware::frequencyHz() const {
    return frequencyHz_;
}

}  // namespace faucet
