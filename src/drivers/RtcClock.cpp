#include "drivers/RtcClock.h"

#include <Arduino.h>
#include <Wire.h>

namespace faucet {
namespace {

constexpr std::uint8_t kDs3231Address = 0x68;

std::uint8_t bcdToDec(std::uint8_t value) {
    return static_cast<std::uint8_t>(((value >> 4U) * 10U) + (value & 0x0FU));
}

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month) {
    static constexpr std::uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return days[month - 1];
}

bool validDateTime(std::uint16_t year,
                   std::uint8_t month,
                   std::uint8_t day,
                   std::uint8_t hour,
                   std::uint8_t minute,
                   std::uint8_t second) {
    return year >= 2020 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 &&
           day <= daysInMonth(year, month) && hour <= 23 && minute <= 59 && second <= 59;
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
    if (!validDateTime(year, month, day, hour, minute, second)) {
        return RtcDateTime{false, 0, 0, 0, 0, 0, 0};
    }

    return RtcDateTime{true, year, month, day, hour, minute, second};
}

}  // namespace faucet
