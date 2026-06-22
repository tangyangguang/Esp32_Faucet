#pragma once

#include <cstdint>

namespace faucet {

constexpr std::uint8_t kPinValve = 16;
constexpr std::uint8_t kPinFlow = 32;
constexpr std::uint8_t kPinButtonCancel = 33;
constexpr std::uint8_t kPinButtonOk = 25;
constexpr std::uint8_t kPinButtonPlus = 26;
constexpr std::uint8_t kPinButtonMinus = 27;
constexpr std::uint8_t kPinBeep = 17;
constexpr std::uint8_t kPinI2cSda = 21;
constexpr std::uint8_t kPinI2cScl = 22;
constexpr std::uint8_t kPinSt7789Sclk = 18;
constexpr std::uint8_t kPinSt7789Mosi = 23;
constexpr std::uint8_t kPinSt7789Dc = 19;
constexpr std::uint8_t kPinSt7789Rst = 14;
constexpr std::uint8_t kPinSt7789Backlight = 13;

constexpr std::uint8_t kLedcChannelValve = 0;
constexpr std::uint8_t kLedcChannelBeep = 1;
constexpr std::uint8_t kLedcResolutionBits = 8;

}  // namespace faucet
