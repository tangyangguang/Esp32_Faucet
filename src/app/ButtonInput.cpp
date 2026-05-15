#include "app/ButtonInput.h"

#include "app/TimeUtils.h"

namespace faucet {

ButtonInput::ButtonInput() {
    reset({false, false, false, false}, 0);
}

void ButtonInput::reset(ButtonLevels levels, std::uint32_t nowMs) {
    cancel_ = {levels.cancelPressed, levels.cancelPressed, nowMs, levels.cancelPressed ? nowMs : 0, false, false};
    ok_ = {levels.okPressed, levels.okPressed, nowMs, levels.okPressed ? nowMs : 0, false, false};
    plus_ = {levels.plusPressed, levels.plusPressed, nowMs, levels.plusPressed ? nowMs : 0, false, false};
    minus_ = {levels.minusPressed, levels.minusPressed, nowMs, levels.minusPressed ? nowMs : 0, false, false};
}

ButtonEvent ButtonInput::update(ButtonLevels levels, std::uint32_t nowMs) {
    const ButtonEvent cancelEvent = updateButton(ButtonId::Cancel, cancel_, levels.cancelPressed, nowMs);
    const ButtonEvent okEvent = updateButton(ButtonId::Ok, ok_, levels.okPressed, nowMs);
    const ButtonEvent plusEvent = updateButton(ButtonId::Plus, plus_, levels.plusPressed, nowMs);
    const ButtonEvent minusEvent = updateButton(ButtonId::Minus, minus_, levels.minusPressed, nowMs);

    if (cancelEvent.type != ButtonEventType::None) {
        return cancelEvent;
    }
    if (okEvent.type != ButtonEventType::None) {
        return okEvent;
    }
    if (plusEvent.type != ButtonEventType::None) {
        return plusEvent;
    }
    if (minusEvent.type != ButtonEventType::None) {
        return minusEvent;
    }
    return none();
}

ButtonEvent ButtonInput::updateButton(ButtonId id, ButtonState& state, bool rawPressed, std::uint32_t nowMs) {
    if (rawPressed != state.rawPressed) {
        state.rawPressed = rawPressed;
        state.rawChangedMs = nowMs;
        return none();
    }

    if (rawPressed == state.stablePressed) {
        if (state.stablePressed && !state.longEmitted &&
            elapsedAtLeast(nowMs, state.pressedMs, kButtonLongPressMs)) {
            state.longEmitted = true;
            return buttonEvent(id, true);
        }
        return none();
    }

    if (!elapsedAtLeast(nowMs, state.rawChangedMs, kButtonDebounceMs)) {
        return none();
    }

    state.stablePressed = rawPressed;
    if (state.stablePressed) {
        state.pressedMs = nowMs;
        state.longEmitted = false;
        if (id == ButtonId::Cancel && !state.cancelDownEmitted) {
            state.cancelDownEmitted = true;
            return {ButtonEventType::CancelDown, id};
        }
        return none();
    }

    state.cancelDownEmitted = false;
    if (state.longEmitted) {
        state.longEmitted = false;
        return none();
    }
    return buttonEvent(id, false);
}

ButtonEvent ButtonInput::buttonEvent(ButtonId id, bool longPress) {
    switch (id) {
        case ButtonId::Cancel:
            return {longPress ? ButtonEventType::CancelLong : ButtonEventType::CancelShort, id};
        case ButtonId::Ok:
            return {longPress ? ButtonEventType::OkLong : ButtonEventType::OkShort, id};
        case ButtonId::Plus:
            return {longPress ? ButtonEventType::PlusLong : ButtonEventType::PlusShort, id};
        case ButtonId::Minus:
            return {longPress ? ButtonEventType::MinusLong : ButtonEventType::MinusShort, id};
    }
    return none();
}

ButtonEvent ButtonInput::none() {
    return {ButtonEventType::None, ButtonId::Cancel};
}

}  // namespace faucet
