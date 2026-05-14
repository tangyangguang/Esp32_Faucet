#pragma once

#include <cstdint>

namespace faucet {

constexpr std::uint8_t kPinValve = 16;
constexpr std::uint8_t kPinFlow = 32;
constexpr std::uint8_t kPinButtonCancel = 25;
constexpr std::uint8_t kPinButtonOk = 33;
constexpr std::uint8_t kPinButtonPlus = 26;
constexpr std::uint8_t kPinButtonMinus = 27;
constexpr std::uint8_t kPinBeep = 17;
constexpr std::uint8_t kPinI2cSda = 21;
constexpr std::uint8_t kPinI2cScl = 22;

constexpr std::uint8_t kLedcChannelValve = 0;
constexpr std::uint8_t kLedcChannelBeep = 1;
constexpr std::uint8_t kLedcResolutionBits = 8;

}  // namespace faucet
