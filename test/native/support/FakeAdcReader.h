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
        ++readCount[index(channel)];
        if (failAll) {
            return {};
        }
        return values[index(channel)];
    }

    static std::size_t index(faucet::AdcChannel channel) {
        return static_cast<std::size_t>(channel);
    }
};

inline faucet::AdcReadResult okMv(std::int16_t mv) {
    faucet::AdcReadResult result{};
    result.ok = true;
    result.millivolts = mv;
    return result;
}

}  // namespace faucet_test
