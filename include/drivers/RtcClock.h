#pragma once

#include <cstdint>

namespace faucet {

struct RtcDateTime {
    bool valid;
    std::uint16_t year;
    std::uint8_t month;
    std::uint8_t day;
    std::uint8_t hour;
    std::uint8_t minute;
    std::uint8_t second;
};

class RtcClock {
public:
    RtcClock(std::uint8_t sdaPin, std::uint8_t sclPin);

    bool begin();
    bool present() const;
    RtcDateTime readNow() const;

private:
    std::uint8_t sdaPin_;
    std::uint8_t sclPin_;
    bool present_;
};

}  // namespace faucet
