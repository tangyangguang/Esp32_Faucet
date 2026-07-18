#include "drivers/Ads1115AdcReader.h"

#include <Arduino.h>
#include <Wire.h>

namespace faucet {
namespace {

constexpr std::uint8_t kRegisterConversion = 0x00;
constexpr std::uint8_t kRegisterConfig = 0x01;
constexpr std::uint16_t kConfigStartSingle = 1U << 15U;
constexpr std::uint16_t kConfigSingleShot = 1U << 8U;
constexpr std::uint16_t kConfigDataRate860Sps = 0x07U << 5U;
constexpr std::uint16_t kConfigComparatorDisabled = 0x03U;
constexpr std::uint32_t kConversionTimeoutUs = 4000UL;

}  // namespace

Ads1115AdcReader::Ads1115AdcReader(std::uint8_t address)
    : address_(address),
      ready_(false),
      ranges_{AdcRange::P4096, AdcRange::P4096, AdcRange::P256, AdcRange::P4096} {}

bool Ads1115AdcReader::begin() {
    return probe();
}

bool Ads1115AdcReader::probe() {
    Wire.beginTransmission(address_);
    ready_ = Wire.endTransmission() == 0;
    return ready_;
}

bool Ads1115AdcReader::setRange(AdcChannel channel, AdcRange range) {
    const std::size_t index = channelIndex(channel);
    if (index >= kChannelCount) {
        return false;
    }
    ranges_[index] = range;
    return true;
}

AdcReadResult Ads1115AdcReader::readSingleEnded(AdcChannel channel) {
    const std::size_t index = channelIndex(channel);
    if (index >= kChannelCount || (!ready_ && !probe())) {
        return {};
    }

    const std::uint16_t config =
        kConfigStartSingle |
        (static_cast<std::uint16_t>(muxBits(channel)) << 12U) |
        (static_cast<std::uint16_t>(pgaBits(ranges_[index])) << 9U) |
        kConfigSingleShot |
        kConfigDataRate860Sps |
        kConfigComparatorDisabled;
    if (!writeRegister(kRegisterConfig, config)) {
        ready_ = false;
        return {};
    }

    const std::uint32_t startedUs = micros();
    std::uint16_t currentConfig = 0;
    do {
        if (!readRegister(kRegisterConfig, currentConfig)) {
            ready_ = false;
            return {};
        }
        if ((currentConfig & kConfigStartSingle) != 0) {
            break;
        }
        delayMicroseconds(100);
    } while (micros() - startedUs < kConversionTimeoutUs);
    if ((currentConfig & kConfigStartSingle) == 0) {
        return {};
    }

    std::uint16_t raw = 0;
    if (!readRegister(kRegisterConversion, raw)) {
        ready_ = false;
        return {};
    }
    return adcRawToMillivolts(static_cast<std::int16_t>(raw), ranges_[index]);
}

std::size_t Ads1115AdcReader::channelIndex(AdcChannel channel) {
    return static_cast<std::size_t>(channel);
}

bool Ads1115AdcReader::writeRegister(std::uint8_t reg, std::uint16_t value) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(static_cast<std::uint8_t>(value >> 8U));
    Wire.write(static_cast<std::uint8_t>(value & 0xFFU));
    return Wire.endTransmission() == 0;
}

bool Ads1115AdcReader::readRegister(std::uint8_t reg, std::uint16_t& value) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(static_cast<int>(address_), 2) != 2) {
        return false;
    }
    value = static_cast<std::uint16_t>(Wire.read()) << 8U;
    value |= static_cast<std::uint8_t>(Wire.read());
    return true;
}

}  // namespace faucet
