#pragma once

#include <cstdint>

namespace faucet {

// Board1 / Schematic1, 2026-07-11.
constexpr std::uint8_t kPinValvePwm = 26;
constexpr std::uint8_t kPinValveShutdown = 32;
constexpr std::uint8_t kPinFlowPrimary = 33;
constexpr std::uint8_t kPinFlowSecondary = 25;
constexpr std::uint8_t kPinButtonCancel = 39;
constexpr std::uint8_t kPinButtonOk = 36;
constexpr std::uint8_t kPinButtonPlus = 34;
constexpr std::uint8_t kPinButtonMinus = 35;
constexpr std::uint8_t kPinBeep = 13;
constexpr std::uint8_t kPinI2cSda = 21;
constexpr std::uint8_t kPinI2cScl = 22;
constexpr std::uint8_t kPinAds1115Alert = 27;
constexpr std::uint8_t kPinSt7789Sclk = 18;
constexpr std::uint8_t kPinSt7789Mosi = 23;
constexpr std::uint8_t kPinSt7789Cs = 14;
constexpr std::uint8_t kPinSt7789Dc = 17;
constexpr std::uint8_t kPinSt7789Rst = 16;
constexpr std::uint8_t kPinSt7789Backlight = 19;

constexpr std::uint8_t kAds1115Address = 0x48;

constexpr std::uint8_t kLedcChannelValve = 0;
constexpr std::uint8_t kLedcChannelBeep = 1;
constexpr std::uint8_t kLedcResolutionBits = 8;

static_assert(kPinValvePwm != kPinValveShutdown, "valve PWM and shutdown pins must differ");
static_assert(kPinBeep != kPinSt7789Backlight, "beep and TFT backlight pins must differ");
static_assert(kPinFlowPrimary != kPinFlowSecondary, "flow inputs must differ");

inline std::uint32_t ledcDutyFromPercent(std::uint8_t percent) {
    constexpr std::uint32_t maxDuty = (1UL << kLedcResolutionBits) - 1UL;
    return static_cast<std::uint32_t>(percent) * maxDuty / 100UL;
}

}  // namespace faucet
