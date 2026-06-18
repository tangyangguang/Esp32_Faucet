#pragma once

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kButtonDebounceMs = 10;
constexpr std::uint32_t kButtonLongPressMs = 1000;
enum class ButtonId : std::uint8_t {
    Cancel = 0,
    Ok = 1,
    Plus = 2,
    Minus = 3,
};

enum class ButtonEventType : std::uint8_t {
    None = 0,
    CancelDown = 1,
    CancelShort = 2,
    CancelLong = 3,
    OkShort = 4,
    OkLong = 5,
    PlusShort = 6,
    PlusLong = 7,
    MinusShort = 8,
    MinusLong = 9,
};

struct ButtonLevels {
    bool cancelPressed;
    bool okPressed;
    bool plusPressed;
    bool minusPressed;
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
        bool cancelDownEmitted;
        bool longEmitted;
    };

    ButtonEvent updateButton(ButtonId id, ButtonState& state, bool rawPressed, std::uint32_t nowMs);
    static ButtonEvent buttonEvent(ButtonId id, bool longPress);
    static ButtonEvent none();

    ButtonState cancel_;
    ButtonState ok_;
    ButtonState plus_;
    ButtonState minus_;
};

}  // namespace faucet
