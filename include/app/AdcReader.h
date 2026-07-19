#pragma once

#include <cstdint>

namespace faucet {

enum class AdcChannel : std::uint8_t {
    A0 = 0,
    A1 = 1,
    A2 = 2,
    A3 = 3,
};

enum class AdcRange : std::uint8_t {
    P256 = 0,
    P512 = 1,
    P2048 = 2,
    P4096 = 3,
};

struct AdcReadResult {
    bool ok = false;
    bool overflow = false;
    std::int16_t raw = 0;
    std::int16_t millivolts = 0;
};

class AdcReader {
public:
    virtual ~AdcReader() = default;

    virtual bool begin() = 0;
    virtual bool setRange(AdcChannel channel, AdcRange range) = 0;
    virtual AdcReadResult readSingleEnded(AdcChannel channel) = 0;
};

std::uint16_t adcRangeFullScaleMv(AdcRange range);
AdcRange nextLargerRange(AdcRange range);
AdcRange nextSmallerRange(AdcRange range);
AdcReadResult adcRawToMillivolts(std::int16_t raw, AdcRange range);

}  // namespace faucet
