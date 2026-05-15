#include "drivers/Lcd1602Display.h"

#include "app/TimeUtils.h"

#include <Arduino.h>
#include <Wire.h>

#include <cstring>

namespace faucet {
namespace {

constexpr std::uint8_t kRs = 0x01;
constexpr std::uint8_t kEnable = 0x04;
constexpr std::uint8_t kBacklight = 0x08;
constexpr std::uint8_t kDataShift = 4;
constexpr std::uint8_t kCols = 16;
constexpr std::uint8_t kRows = 2;
constexpr std::uint8_t kLineAddress[kRows] = {0x00, 0x40};
constexpr std::uint32_t kReconnectRetryMs = 3000;
constexpr std::uint32_t kHealthyReinitMs = 5000;

bool sameFrame(const DisplayFrame& a, const DisplayFrame& b) {
    return a.page == b.page && a.on == b.on && std::strncmp(a.line1, b.line1, kDisplayLineLength) == 0 &&
           std::strncmp(a.line2, b.line2, kDisplayLineLength) == 0;
}

}  // namespace

Lcd1602Display::Lcd1602Display(std::uint8_t sdaPin, std::uint8_t sclPin)
    : sdaPin_(sdaPin),
      sclPin_(sclPin),
      address_(0x27),
      present_(false),
      backlight_(true),
      busFailed_(false),
      lastInitMs_(0),
      lastRetryMs_(0),
      lastFrame_{} {}

bool Lcd1602Display::begin(std::uint8_t address) {
    address_ = address;
    return initialize();
}

bool Lcd1602Display::initialize() {
    busFailed_ = false;
    backlight_ = true;
    delay(50);
    if (!writeExpander(kBacklight)) {
        markBusFailure();
        return false;
    }
    write4(0x03, false);
    delay(5);
    write4(0x03, false);
    delayMicroseconds(150);
    write4(0x03, false);
    write4(0x02, false);

    command(0x28);
    command(0x08);
    clear();
    command(0x06);
    command(0x0C);
    if (busFailed_) {
        markBusFailure();
        return false;
    }
    present_ = true;
    lastInitMs_ = millis();
    lastFrame_ = {};
    return true;
}

bool Lcd1602Display::present() const {
    return present_;
}

void Lcd1602Display::apply(const DisplayFrame& frame) {
    const std::uint32_t nowMs = millis();
    if (!present_) {
        if (elapsedAtLeast(nowMs, lastRetryMs_, kReconnectRetryMs)) {
            lastRetryMs_ = nowMs;
            initialize();
        }
        return;
    }

    const bool reinitialized = shouldReinitialize(nowMs, frame) && initialize();
    if (!present_) {
        return;
    }
    if (!reinitialized && sameFrame(frame, lastFrame_)) {
        return;
    }
    setBacklight(frame.on);
    if (busFailed_) {
        markBusFailure();
        return;
    }
    if (!frame.on) {
        lastFrame_ = frame;
        return;
    }
    drawLine(0, frame.line1);
    drawLine(1, frame.line2);
    if (busFailed_) {
        markBusFailure();
    } else {
        lastFrame_ = frame;
    }
}

void Lcd1602Display::write4(std::uint8_t value, bool rs) {
    const std::uint8_t data = static_cast<std::uint8_t>((value << kDataShift) | (rs ? kRs : 0) |
                                                        (backlight_ ? kBacklight : 0));
    pulse(data);
}

void Lcd1602Display::write8(std::uint8_t value, bool rs) {
    write4(static_cast<std::uint8_t>(value >> 4), rs);
    write4(static_cast<std::uint8_t>(value & 0x0F), rs);
}

void Lcd1602Display::command(std::uint8_t value) {
    write8(value, false);
    delayMicroseconds(50);
}

void Lcd1602Display::data(std::uint8_t value) {
    write8(value, true);
    delayMicroseconds(50);
}

void Lcd1602Display::pulse(std::uint8_t value) {
    writeExpander(static_cast<std::uint8_t>(value | kEnable));
    delayMicroseconds(1);
    writeExpander(static_cast<std::uint8_t>(value & ~kEnable));
    delayMicroseconds(50);
}

bool Lcd1602Display::writeExpander(std::uint8_t value) {
    Wire.beginTransmission(address_);
    Wire.write(value);
    const bool ok = Wire.endTransmission() == 0;
    if (!ok) {
        busFailed_ = true;
    }
    return ok;
}

void Lcd1602Display::markBusFailure() {
    present_ = false;
    busFailed_ = true;
    lastRetryMs_ = millis();
}

bool Lcd1602Display::shouldReinitialize(std::uint32_t nowMs, const DisplayFrame& frame) const {
    return frame.on && elapsedAtLeast(nowMs, lastInitMs_, kHealthyReinitMs);
}

void Lcd1602Display::setBacklight(bool on) {
    if (backlight_ == on) {
        return;
    }
    backlight_ = on;
    writeExpander(backlight_ ? kBacklight : 0);
}

void Lcd1602Display::clear() {
    command(0x01);
    delay(2);
}

void Lcd1602Display::drawLine(std::uint8_t row, const char* text) {
    if (row >= kRows) {
        return;
    }
    command(static_cast<std::uint8_t>(0x80 | kLineAddress[row]));
    for (std::uint8_t i = 0; i < kCols; ++i) {
        const char ch = text && text[i] != '\0' ? text[i] : ' ';
        data(static_cast<std::uint8_t>(ch));
    }
}

}  // namespace faucet
