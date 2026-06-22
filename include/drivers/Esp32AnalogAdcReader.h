#pragma once

#include "app/AdcReader.h"
#include "drivers/BoardPins.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class Esp32AnalogAdcReader : public AdcReader {
public:
    static constexpr std::uint8_t kDisabledPin = 255;
    static constexpr std::size_t kChannelCount = 4;

    Esp32AnalogAdcReader();

    bool begin() override;
    bool setRange(AdcChannel channel, AdcRange range) override;
    AdcReadResult readSingleEnded(AdcChannel channel) override;

    static constexpr std::uint8_t defaultPinForChannel(AdcChannel channel) {
        return channel == AdcChannel::A1 ? kPinTemperatureAdc
             : channel == AdcChannel::A2 ? kPinTdsAdc
                                          : kDisabledPin;
    }

private:
    std::uint8_t pins_[kChannelCount];
    AdcRange ranges_[kChannelCount];

    static std::size_t channelIndex(AdcChannel channel);
};

}  // namespace faucet
