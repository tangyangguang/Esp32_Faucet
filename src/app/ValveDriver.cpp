#include "app/ValveDriver.h"

#include "app/TimeUtils.h"

namespace faucet {
namespace {

std::uint32_t msFromSeconds(std::uint32_t seconds) {
    return seconds * 1000UL;
}

bool validConfig(std::uint32_t fullPowerSec, std::uint8_t holdDutyPercent) {
    return fullPowerSec >= 1 && fullPowerSec <= 10 && holdDutyPercent >= kMinValveHoldDutyPercent &&
           holdDutyPercent <= kMaxValveHoldDutyPercent;
}

}  // namespace

ValveDriver::ValveDriver(std::uint32_t fullPowerSec, std::uint8_t holdDutyPercent)
    : fullPowerMs_(msFromSeconds(kDefaultValveFullPowerSec)),
      holdDutyPercent_(kDefaultValveHoldDutyPercent),
      openedAtMs_(0),
      state_(ValveState::Closed) {
    configure(fullPowerSec, holdDutyPercent);
}

bool ValveDriver::configure(std::uint32_t fullPowerSec, std::uint8_t holdDutyPercent) {
    if (!validConfig(fullPowerSec, holdDutyPercent)) {
        return false;
    }
    fullPowerMs_ = msFromSeconds(fullPowerSec);
    holdDutyPercent_ = holdDutyPercent;
    return true;
}

void ValveDriver::open(std::uint32_t nowMs) {
    openedAtMs_ = nowMs;
    state_ = ValveState::OpeningFullPower;
}

void ValveDriver::close() {
    state_ = ValveState::Closed;
    openedAtMs_ = 0;
}

void ValveDriver::tick(std::uint32_t nowMs) {
    if (state_ == ValveState::OpeningFullPower && elapsedAtLeast(nowMs, openedAtMs_, fullPowerMs_)) {
        state_ = ValveState::Holding;
    }
}

ValveOutput ValveDriver::output() const {
    switch (state_) {
        case ValveState::OpeningFullPower:
            return {state_, true, 100};
        case ValveState::Holding:
            return {state_, true, holdDutyPercent_};
        case ValveState::Closed:
        default:
            return {ValveState::Closed, false, 0};
    }
}

}  // namespace faucet
