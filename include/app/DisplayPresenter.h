#pragma once

#include "app/AppController.h"

#include <cstdint>

namespace faucet {

constexpr std::size_t kDisplayLineLength = 17;

enum class DisplayPage : std::uint8_t {
    Idle = 0,
    Confirm = 1,
    Running = 2,
    Paused = 3,
    Result = 4,
    Error = 5,
    Sleep = 6,
    Calibration = 7,
};

struct DisplayFrame {
    DisplayPage page;
    bool on;
    char line1[kDisplayLineLength];
    char line2[kDisplayLineLength];
};

class DisplayPresenter {
public:
    explicit DisplayPresenter(std::uint32_t sleepTimeoutSec = kDefaultDisplaySleepSec);

    void configure(std::uint32_t sleepTimeoutSec);
    void wake(std::uint32_t nowMs);
    DisplayFrame render(const AppSnapshot& snapshot, std::uint32_t nowMs);

private:
    std::uint32_t sleepTimeoutMs_;
    std::uint32_t lastWakeMs_;

    static DisplayFrame makeFrame(DisplayPage page, bool on, const char* line1, const char* line2);
};

}  // namespace faucet
