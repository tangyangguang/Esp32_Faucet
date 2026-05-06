#pragma once

#include "app/DisplayPresenter.h"

#include <cstdint>

namespace faucet {

class OledDisplay {
public:
    OledDisplay(std::uint8_t sdaPin, std::uint8_t sclPin, std::uint8_t address = 0x3C);

    bool begin();
    bool present() const;
    void apply(const DisplayFrame& frame);

private:
    bool command(std::uint8_t value);
    bool data(const std::uint8_t* values, std::size_t len);
    void clear();
    void setOn(bool on);
    void drawLine(std::uint8_t page, const char* text);

    std::uint8_t sdaPin_;
    std::uint8_t sclPin_;
    std::uint8_t address_;
    bool present_;
    bool on_;
    DisplayFrame lastFrame_;
};

}  // namespace faucet
