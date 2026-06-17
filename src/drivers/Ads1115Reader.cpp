#include "drivers/Ads1115Reader.h"

#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <Wire.h>
#endif

namespace faucet {
namespace {

constexpr std::uint8_t kConversionRegister = 0x00;
constexpr std::uint8_t kConfigRegister = 0x01;
constexpr std::uint16_t kConfigOsStart = 0x8000;
constexpr std::uint16_t kConfigModeSingleShot = 0x0100;
constexpr std::uint16_t kConfigDataRate128Sps = 0x0080;
constexpr std::uint16_t kConfigComparatorDisabled = 0x0003;
constexpr std::uint32_t kConversionTimeoutMs = 25;

std::size_t channelIndex(AdcChannel channel) {
    return static_cast<std::size_t>(channel);
}

std::uint16_t adsMuxBits(AdcChannel channel) {
    switch (channel) {
        case AdcChannel::A0:
            return 0x4000;
        case AdcChannel::A1:
            return 0x5000;
        case AdcChannel::A2:
            return 0x6000;
        case AdcChannel::A3:
            return 0x7000;
    }
    return 0x4000;
}

std::uint16_t adsPgaBits(AdcRange range) {
    switch (range) {
        case AdcRange::P256:
            return 0x0A00;
        case AdcRange::P512:
            return 0x0800;
        case AdcRange::P2048:
            return 0x0400;
        case AdcRange::P4096:
            return 0x0200;
    }
    return 0x0400;
}

}  // namespace

Ads1115Reader::Ads1115Reader(std::uint8_t address)
    : address_(address), ranges_{AdcRange::P4096, AdcRange::P4096, AdcRange::P256, AdcRange::P4096} {}

bool Ads1115Reader::begin() {
    std::uint16_t ignored = 0;
    return readRegister(kConfigRegister, ignored);
}

bool Ads1115Reader::setRange(AdcChannel channel, AdcRange range) {
    const std::size_t index = channelIndex(channel);
    if (index >= kChannelCount) {
        return false;
    }
    ranges_[index] = range;
    return true;
}

AdcReadResult Ads1115Reader::readSingleEnded(AdcChannel channel) {
    const std::size_t index = channelIndex(channel);
    if (index >= kChannelCount) {
        return {};
    }
    const AdcRange range = ranges_[index];
    const std::uint16_t config = kConfigOsStart | adsMuxBits(channel) | adsPgaBits(range) | kConfigModeSingleShot |
                                 kConfigDataRate128Sps | kConfigComparatorDisabled;
    if (!writeRegister(kConfigRegister, config)) {
        return {};
    }

#ifndef NATIVE_BUILD
    const std::uint32_t start = millis();
    while (millis() - start < kConversionTimeoutMs) {
        std::uint16_t current = 0;
        if (!readRegister(kConfigRegister, current)) {
            return {};
        }
        if ((current & kConfigOsStart) != 0) {
            std::uint16_t raw = 0;
            if (!readRegister(kConversionRegister, raw)) {
                return {};
            }
            return adcRawToMillivolts(static_cast<std::int16_t>(raw), range);
        }
        delay(1);
    }
#endif

    return {};
}

bool Ads1115Reader::writeRegister(std::uint8_t reg, std::uint16_t value) {
#ifndef NATIVE_BUILD
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    Wire.write(static_cast<std::uint8_t>(value & 0xFFU));
    return Wire.endTransmission() == 0;
#else
    (void)reg;
    (void)value;
    return false;
#endif
}

bool Ads1115Reader::readRegister(std::uint8_t reg, std::uint16_t& value) {
#ifndef NATIVE_BUILD
    Wire.beginTransmission(address_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    const std::uint8_t read = Wire.requestFrom(address_, static_cast<std::uint8_t>(2));
    if (read != 2 || Wire.available() < 2) {
        return false;
    }
    const std::uint8_t msb = Wire.read();
    const std::uint8_t lsb = Wire.read();
    value = (static_cast<std::uint16_t>(msb) << 8U) | lsb;
    return true;
#else
    (void)reg;
    value = 0;
    return false;
#endif
}

}  // namespace faucet
