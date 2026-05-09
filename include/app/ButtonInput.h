#pragma once

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kButtonDebounceMs = 30;
constexpr std::uint32_t kButtonLongPressMs = 1000;
constexpr std::uint32_t kFactoryResetComboMs = 5000;
constexpr std::uint32_t kFactoryResetComboCooldownMs = 1000;

enum class ButtonId : std::uint8_t {
    Stop = 0,
    Ok = 1,
    Next = 2,
};

enum class ButtonEventType : std::uint8_t {
    None = 0,
    StopDown = 1,
    StopShort = 2,
    StopLong = 3,
    OkShort = 4,
    OkLong = 5,
    NextShort = 6,
    NextLong = 7,
    FactoryResetCombo = 8,
};

struct ButtonLevels {
    bool stopPressed;
    bool okPressed;
    bool nextPressed;
};

struct ButtonEvent {
    ButtonEventType type;
    ButtonId button;
};

class ButtonInput {
public:
    ButtonInput();

    void reset(ButtonLevels levels, std::uint32_t nowMs);
    ButtonEvent update(ButtonLevels levels, std::uint32_t nowMs);

private:
    struct ButtonState {
        bool stablePressed;
        bool rawPressed;
        std::uint32_t rawChangedMs;
        std::uint32_t pressedMs;
        bool stopDownEmitted;
    };

    ButtonEvent updateButton(ButtonId id, ButtonState& state, bool rawPressed, std::uint32_t nowMs);
    ButtonEvent checkCombo(std::uint32_t nowMs);
    static ButtonEvent shortOrLongEvent(ButtonId id, std::uint32_t heldMs);
    static ButtonEvent none();

    ButtonState stop_;
    ButtonState ok_;
    ButtonState next_;
    std::uint32_t comboStartMs_;
    std::uint32_t comboReleasedMs_;
    bool comboArmed_;
    bool comboEmitted_;
    bool comboLocked_;
    bool comboReleaseSeen_;
};

}  // namespace faucet
