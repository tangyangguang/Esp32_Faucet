#pragma once

#include "app/AppConfig.h"

#include <cstdint>

namespace faucet {

enum class ValveState : std::uint8_t {
    Closed = 0,
    OpeningFullPower = 1,
    Holding = 2,
};

struct ValveOutput {
    ValveState state;
    bool enabled;
    std::uint8_t dutyPercent;
};

class ValveDriver {
public:
    ValveDriver(std::uint32_t fullPowerSec = kDefaultValveFullPowerSec,
                std::uint8_t holdDutyPercent = kDefaultValveHoldDutyPercent);

    bool configure(std::uint32_t fullPowerSec, std::uint8_t holdDutyPercent);
    void open(std::uint32_t nowMs);
    void close();
    void tick(std::uint32_t nowMs);
    ValveOutput output() const;

private:
    std::uint32_t fullPowerMs_;
    std::uint8_t holdDutyPercent_;
    std::uint32_t openedAtMs_;
    ValveState state_;
};

}  // namespace faucet
