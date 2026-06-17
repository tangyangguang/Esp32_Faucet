#pragma once

#include "app/AdcReader.h"

#include <cstdint>

namespace faucet {

class Ads1115Reader : public AdcReader {
public:
    explicit Ads1115Reader(std::uint8_t address = 0x48);

    bool begin() override;
    bool setRange(AdcChannel channel, AdcRange range) override;
    AdcReadResult readSingleEnded(AdcChannel channel) override;

private:
    static constexpr std::size_t kChannelCount = 4;

    std::uint8_t address_;
    AdcRange ranges_[kChannelCount];

    bool writeRegister(std::uint8_t reg, std::uint16_t value);
    bool readRegister(std::uint8_t reg, std::uint16_t& value);
};

}  // namespace faucet
