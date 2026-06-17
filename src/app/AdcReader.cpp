#include "app/AdcReader.h"

#include <cstdlib>

namespace faucet {

std::uint16_t adcRangeFullScaleMv(AdcRange range) {
    switch (range) {
        case AdcRange::P256:
            return 256;
        case AdcRange::P512:
            return 512;
        case AdcRange::P2048:
            return 2048;
        case AdcRange::P4096:
            return 4096;
    }
    return 2048;
}

AdcRange nextLargerRange(AdcRange range) {
    switch (range) {
        case AdcRange::P256:
            return AdcRange::P512;
        case AdcRange::P512:
            return AdcRange::P2048;
        case AdcRange::P2048:
            return AdcRange::P4096;
        case AdcRange::P4096:
            return AdcRange::P4096;
    }
    return AdcRange::P4096;
}

AdcRange nextSmallerRange(AdcRange range) {
    switch (range) {
        case AdcRange::P4096:
            return AdcRange::P2048;
        case AdcRange::P2048:
            return AdcRange::P512;
        case AdcRange::P512:
            return AdcRange::P256;
        case AdcRange::P256:
            return AdcRange::P256;
    }
    return AdcRange::P256;
}

AdcReadResult adcRawToMillivolts(std::int16_t raw, AdcRange range) {
    AdcReadResult result{};
    if (raw < 0) {
        return result;
    }
    const std::uint16_t fullScaleMv = adcRangeFullScaleMv(range);
    const std::int32_t mv = static_cast<std::int32_t>(raw) * fullScaleMv / 32768;
    result.ok = true;
    result.overflow = raw >= 32760;
    result.millivolts = static_cast<std::int16_t>(mv);
    return result;
}

}  // namespace faucet
