#include "app/ButtonInput.h"

#include "app/TimeUtils.h"

namespace faucet {

ButtonInput::ButtonInput() {
    reset({false, false, false}, 0);
}

void ButtonInput::reset(ButtonLevels levels, std::uint32_t nowMs) {
    stop_ = {levels.stopPressed, levels.stopPressed, nowMs, levels.stopPressed ? nowMs : 0, false};
    ok_ = {levels.okPressed, levels.okPressed, nowMs, levels.okPressed ? nowMs : 0, false};
    next_ = {levels.nextPressed, levels.nextPressed, nowMs, levels.nextPressed ? nowMs : 0, false};
    comboStartMs_ = 0;
    comboReleasedMs_ = 0;
    comboArmed_ = false;
    comboEmitted_ = false;
    comboLocked_ = false;
    comboReleaseSeen_ = false;
}

ButtonEvent ButtonInput::update(ButtonLevels levels, std::uint32_t nowMs) {
    const ButtonEvent stopEvent = updateButton(ButtonId::Stop, stop_, levels.stopPressed, nowMs);
    const ButtonEvent okEvent = updateButton(ButtonId::Ok, ok_, levels.okPressed, nowMs);
    const ButtonEvent nextEvent = updateButton(ButtonId::Next, next_, levels.nextPressed, nowMs);
    const ButtonEvent comboEvent = checkCombo(nowMs);

    if (stopEvent.type != ButtonEventType::None) {
        return stopEvent;
    }
    if (comboEvent.type != ButtonEventType::None) {
        return comboEvent;
    }
    if (okEvent.type != ButtonEventType::None) {
        return okEvent;
    }
    if (nextEvent.type != ButtonEventType::None) {
        return nextEvent;
    }
    return none();
}

ButtonEvent ButtonInput::updateButton(ButtonId id, ButtonState& state, bool rawPressed, std::uint32_t nowMs) {
    if (rawPressed != state.rawPressed) {
        state.rawPressed = rawPressed;
        state.rawChangedMs = nowMs;
        return none();
    }

    if (rawPressed == state.stablePressed || !elapsedAtLeast(nowMs, state.rawChangedMs, kButtonDebounceMs)) {
        return none();
    }

    state.stablePressed = rawPressed;
    if (state.stablePressed) {
        state.pressedMs = nowMs;
        if (id == ButtonId::Stop && !state.stopDownEmitted) {
            state.stopDownEmitted = true;
            return {ButtonEventType::StopDown, id};
        }
        return none();
    }

    const std::uint32_t heldMs = elapsedSince(nowMs, state.pressedMs);
    state.stopDownEmitted = false;
    return shortOrLongEvent(id, heldMs);
}

ButtonEvent ButtonInput::checkCombo(std::uint32_t nowMs) {
    if (comboLocked_) {
        if (!stop_.stablePressed && !ok_.stablePressed) {
            if (!comboReleaseSeen_) {
                comboReleaseSeen_ = true;
                comboReleasedMs_ = nowMs;
            } else if (elapsedAtLeast(nowMs, comboReleasedMs_, kFactoryResetComboCooldownMs)) {
                comboLocked_ = false;
                comboReleaseSeen_ = false;
            }
        } else {
            comboReleaseSeen_ = false;
        }
        return none();
    }

    if (stop_.stablePressed && ok_.stablePressed) {
        if (!comboArmed_) {
            comboArmed_ = true;
            comboEmitted_ = false;
            comboStartMs_ = nowMs;
        }
        if (!comboEmitted_ && elapsedAtLeast(nowMs, comboStartMs_, kFactoryResetComboMs)) {
            comboEmitted_ = true;
            comboLocked_ = true;
            comboReleaseSeen_ = false;
            return {ButtonEventType::FactoryResetCombo, ButtonId::Stop};
        }
    } else {
        comboArmed_ = false;
        comboEmitted_ = false;
        comboStartMs_ = 0;
    }
    return none();
}

ButtonEvent ButtonInput::shortOrLongEvent(ButtonId id, std::uint32_t heldMs) {
    const bool longPress = heldMs >= kButtonLongPressMs;
    switch (id) {
        case ButtonId::Stop:
            return {longPress ? ButtonEventType::StopLong : ButtonEventType::StopShort, id};
        case ButtonId::Ok:
            return {longPress ? ButtonEventType::OkLong : ButtonEventType::OkShort, id};
        case ButtonId::Next:
            return {longPress ? ButtonEventType::NextLong : ButtonEventType::NextShort, id};
    }
    return none();
}

ButtonEvent ButtonInput::none() {
    return {ButtonEventType::None, ButtonId::Stop};
}

}  // namespace faucet
