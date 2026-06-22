#include "drivers/Esp32AnalogAdcReader.h"

#ifndef NATIVE_BUILD
#include <Arduino.h>
#endif

namespace faucet {
namespace {

std::uint8_t attenuationForRange(AdcRange range) {
#ifndef NATIVE_BUILD
    switch (range) {
        case AdcRange::P256:
        case AdcRange::P512:
            return ADC_0db;
        case AdcRange::P2048:
            return ADC_6db;
        case AdcRange::P4096:
            return ADC_11db;
    }
#else
    (void)range;
#endif
    return 0;
}

}  // namespace

Esp32AnalogAdcReader::Esp32AnalogAdcReader()
    : pins_{defaultPinForChannel(AdcChannel::A0),
            defaultPinForChannel(AdcChannel::A1),
            defaultPinForChannel(AdcChannel::A2),
            defaultPinForChannel(AdcChannel::A3)},
      ranges_{AdcRange::P4096, AdcRange::P4096, AdcRange::P256, AdcRange::P4096} {}

bool Esp32AnalogAdcReader::begin() {
#ifndef NATIVE_BUILD
    analogReadResolution(12);
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        if (pins_[i] == kDisabledPin) {
            continue;
        }
        pinMode(pins_[i], INPUT);
        analogSetPinAttenuation(pins_[i], static_cast<adc_attenuation_t>(attenuationForRange(ranges_[i])));
    }
#endif
    return true;
}

bool Esp32AnalogAdcReader::setRange(AdcChannel channel, AdcRange range) {
    const std::size_t index = channelIndex(channel);
    if (index >= kChannelCount || pins_[index] == kDisabledPin) {
        return false;
    }
    ranges_[index] = range;
#ifndef NATIVE_BUILD
    analogSetPinAttenuation(pins_[index], static_cast<adc_attenuation_t>(attenuationForRange(range)));
#endif
    return true;
}

AdcReadResult Esp32AnalogAdcReader::readSingleEnded(AdcChannel channel) {
    const std::size_t index = channelIndex(channel);
    if (index >= kChannelCount || pins_[index] == kDisabledPin) {
        return {};
    }

#ifndef NATIVE_BUILD
    const std::uint32_t mv = analogReadMilliVolts(pins_[index]);
    AdcReadResult result{};
    result.ok = true;
    result.overflow = mv >= adcRangeFullScaleMv(ranges_[index]);
    result.millivolts = mv > 32767 ? 32767 : static_cast<std::int16_t>(mv);
    return result;
#else
    return {};
#endif
}

std::size_t Esp32AnalogAdcReader::channelIndex(AdcChannel channel) {
    return static_cast<std::size_t>(channel);
}

}  // namespace faucet
