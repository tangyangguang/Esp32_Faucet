#pragma once

#include "app/DisplayPresenter.h"
#include "app/LcdRecoveryPolicy.h"

#include <cstdint>

namespace faucet {

class Lcd1602Display {
public:
    Lcd1602Display(std::uint8_t sdaPin, std::uint8_t sclPin);

    bool begin(std::uint8_t address);
    bool present() const;
    void apply(const DisplayFrame& frame, bool userActivity = false);

private:
    void write4(std::uint8_t value, bool rs);
    void write8(std::uint8_t value, bool rs);
    void command(std::uint8_t value);
    void data(std::uint8_t value);
    void pulse(std::uint8_t value);
    bool writeExpander(std::uint8_t value);
    bool probeBus();
    bool initialize();
    void markBusFailure();
    void setBacklight(bool on);
    void clear();
    void drawLine(std::uint8_t row, const char* text);

    std::uint8_t sdaPin_;
    std::uint8_t sclPin_;
    std::uint8_t address_;
    bool present_;
    bool backlight_;
    bool busFailed_;
    DisplayFrame lastFrame_;
    LcdRecoveryPolicy recovery_;
};

}  // namespace faucet
