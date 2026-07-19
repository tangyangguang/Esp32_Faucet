#include "drivers/PwmValveHardware.h"

#include "drivers/BoardPins.h"

#include <Arduino.h>

namespace faucet {
namespace {

constexpr std::uint8_t kStartupKickDutyPercent = 50;
constexpr std::uint32_t kStartupKickCycles = 2;

std::uint32_t startupKickDurationUs(std::uint32_t frequencyHz) {
    return static_cast<std::uint32_t>(
        (1000000ULL * kStartupKickCycles + frequencyHz - 1ULL) / frequencyHz);
}

}  // namespace

PwmValveHardware::PwmValveHardware(std::uint8_t pwmPin, std::uint8_t shutdownPin, std::uint8_t channel)
    : pwmPin_(pwmPin),
      shutdownPin_(shutdownPin),
      channel_(channel),
      frequencyHz_(kDefaultValvePwmFrequencyHz),
      begun_(false),
      outputEnabled_(false) {}

void PwmValveHardware::forceSafeState() {
    pinMode(shutdownPin_, OUTPUT);
    digitalWrite(shutdownPin_, HIGH);
    pinMode(pwmPin_, OUTPUT);
    digitalWrite(pwmPin_, LOW);
    outputEnabled_ = false;
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
    if (!begun_) {
        return begin(frequencyHz);
    }
    if (frequencyHz < kMinValvePwmFrequencyHz || frequencyHz > kMaxValvePwmFrequencyHz ||
        ledcSetup(channel_, frequencyHz, kLedcResolutionBits) == 0) {
        return false;
    }
    frequencyHz_ = frequencyHz;
    return true;
}

void PwmValveHardware::apply(ValveOutput output) {
    if (!begun_ || !output.enabled || output.dutyPercent == 0) {
        digitalWrite(shutdownPin_, HIGH);
        if (begun_) {
            ledcWrite(channel_, 0);
        } else {
            digitalWrite(pwmPin_, LOW);
        }
        outputEnabled_ = false;
        return;
    }

    if (!outputEnabled_) {
        // EG27324 uses cycle-by-cycle shutdown recovery. INA must produce a
        // fresh edge after SD is released; Arduino maps the maximum 8-bit
        // duty to constant high, so applying 100% before SD would not do so.
        ledcWrite(channel_, 0);
        digitalWrite(shutdownPin_, LOW);
        ledcWrite(channel_, ledcDutyFromPercent(kStartupKickDutyPercent));
        delayMicroseconds(startupKickDurationUs(frequencyHz_));
        outputEnabled_ = true;
    }

    ledcWrite(channel_, ledcDutyFromPercent(output.dutyPercent));
}

std::uint32_t PwmValveHardware::frequencyHz() const {
    return frequencyHz_;
}

}  // namespace faucet
