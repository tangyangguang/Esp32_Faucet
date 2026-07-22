#pragma once

#include "app/AdcReader.h"

#include <cstddef>
#include <cstdint>

namespace faucet_test {

class FakeAdcReader : public faucet::AdcReader {
public:
    bool beginOk = true;
    bool failAll = false;
    faucet::AdcReadResult values[4]{};
    faucet::AdcRange ranges[4]{};
    std::size_t readCount[4]{};
    std::size_t setRangeCount[4]{};
    bool analogMillivoltsEnabled[4]{};
    std::int16_t analogMillivolts[4]{};

    FakeAdcReader() {
        for (auto& range : ranges) {
            range = faucet::AdcRange::P4096;
        }
    }

    bool begin() override {
        return beginOk;
    }

    bool setRange(faucet::AdcChannel channel, faucet::AdcRange range) override {
        ranges[index(channel)] = range;
        ++setRangeCount[index(channel)];
        return true;
    }

    faucet::AdcReadResult readSingleEnded(faucet::AdcChannel channel) override {
        const std::size_t channelIndex = index(channel);
        ++readCount[channelIndex];
        if (failAll) {
            return {};
        }
        if (!analogMillivoltsEnabled[channelIndex]) {
            return values[channelIndex];
        }
        faucet::AdcReadResult result{};
        result.ok = analogMillivolts[channelIndex] >= 0;
        result.millivolts = analogMillivolts[channelIndex];
        if (!result.ok) {
            return result;
        }
        const std::uint16_t fullScale = faucet::adcRangeFullScaleMv(ranges[channelIndex]);
        if (static_cast<std::uint16_t>(result.millivolts) >= fullScale) {
            result.raw = 32760;
            result.overflow = true;
            return result;
        }
        result.raw = static_cast<std::int16_t>(
            (static_cast<std::int32_t>(result.millivolts) * 32768 + fullScale / 2U) / fullScale);
        return result;
    }

    void setAnalogMillivolts(faucet::AdcChannel channel, std::int16_t millivolts) {
        const std::size_t channelIndex = index(channel);
        analogMillivoltsEnabled[channelIndex] = true;
        analogMillivolts[channelIndex] = millivolts;
    }

    void clearAnalogMillivolts(faucet::AdcChannel channel) {
        analogMillivoltsEnabled[index(channel)] = false;
    }

    static std::size_t index(faucet::AdcChannel channel) {
        return static_cast<std::size_t>(channel);
    }
};

inline faucet::AdcReadResult okMv(std::int16_t mv) {
    faucet::AdcReadResult result{};
    result.ok = true;
    result.raw = static_cast<std::int16_t>(static_cast<std::int32_t>(mv) * 32768 / 4096);
    result.millivolts = mv;
    return result;
}

}  // namespace faucet_test
