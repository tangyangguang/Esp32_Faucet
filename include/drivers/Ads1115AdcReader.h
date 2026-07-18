#pragma once

#include "app/AdcReader.h"
#include "drivers/BoardPins.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class Ads1115AdcReader : public AdcReader {
public:
    static constexpr std::size_t kChannelCount = 4;

    explicit Ads1115AdcReader(std::uint8_t address = kAds1115Address);

    bool begin() override;
    bool setRange(AdcChannel channel, AdcRange range) override;
    AdcReadResult readSingleEnded(AdcChannel channel) override;

    static constexpr std::uint8_t muxBits(AdcChannel channel) {
        return static_cast<std::uint8_t>(0x04U + static_cast<std::uint8_t>(channel));
    }

    static constexpr std::uint8_t pgaBits(AdcRange range) {
        return range == AdcRange::P4096 ? 0x01U
             : range == AdcRange::P2048 ? 0x02U
             : range == AdcRange::P512  ? 0x04U
                                        : 0x05U;
    }

private:
    std::uint8_t address_;
    bool ready_;
    AdcRange ranges_[kChannelCount];

    static std::size_t channelIndex(AdcChannel channel);
    bool probe();
    bool writeRegister(std::uint8_t reg, std::uint16_t value);
    bool readRegister(std::uint8_t reg, std::uint16_t& value);
};

}  // namespace faucet
