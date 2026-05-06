#include "drivers/RtcClock.h"

#include <Arduino.h>
#include <Wire.h>

namespace faucet {
namespace {

constexpr std::uint8_t kDs3231Address = 0x68;

std::uint8_t bcdToDec(std::uint8_t value) {
    return static_cast<std::uint8_t>(((value >> 4U) * 10U) + (value & 0x0FU));
}

}  // namespace

RtcClock::RtcClock(std::uint8_t sdaPin, std::uint8_t sclPin)
    : sdaPin_(sdaPin), sclPin_(sclPin), present_(false) {}

bool RtcClock::begin() {
    Wire.begin(sdaPin_, sclPin_);
    Wire.beginTransmission(kDs3231Address);
    present_ = Wire.endTransmission() == 0;
    return present_;
}

bool RtcClock::present() const {
    return present_;
}

RtcDateTime RtcClock::readNow() const {
    if (!present_) {
        return RtcDateTime{false, 0, 0, 0, 0, 0, 0};
    }

    Wire.beginTransmission(kDs3231Address);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        return RtcDateTime{false, 0, 0, 0, 0, 0, 0};
    }
    if (Wire.requestFrom(kDs3231Address, static_cast<std::uint8_t>(7)) != 7) {
        return RtcDateTime{false, 0, 0, 0, 0, 0, 0};
    }

    const std::uint8_t second = bcdToDec(Wire.read() & 0x7F);
    const std::uint8_t minute = bcdToDec(Wire.read() & 0x7F);
    const std::uint8_t hour = bcdToDec(Wire.read() & 0x3F);
    Wire.read();  // day of week
    const std::uint8_t day = bcdToDec(Wire.read() & 0x3F);
    const std::uint8_t monthRaw = Wire.read();
    const std::uint8_t month = bcdToDec(monthRaw & 0x1F);
    const std::uint16_t year = static_cast<std::uint16_t>(2000U + bcdToDec(Wire.read()));

    return RtcDateTime{true, year, month, day, hour, minute, second};
}

}  // namespace faucet
