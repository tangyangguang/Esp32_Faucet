#include "drivers/GpioButtonReader.h"

#include <Arduino.h>

namespace faucet {

GpioButtonReader* GpioButtonReader::instance_ = nullptr;

GpioButtonReader::GpioButtonReader(std::uint8_t cancelPin, std::uint8_t okPin, std::uint8_t plusPin, std::uint8_t minusPin)
    : cancelPin_(cancelPin), okPin_(okPin), plusPin_(plusPin), minusPin_(minusPin), cancelInterruptPending_(false) {}

void GpioButtonReader::begin() {
    instance_ = this;
    pinMode(cancelPin_, INPUT_PULLUP);
    pinMode(okPin_, INPUT_PULLUP);
    pinMode(plusPin_, INPUT_PULLUP);
    pinMode(minusPin_, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(cancelPin_), GpioButtonReader::isrCancel, FALLING);
}

ButtonLevels GpioButtonReader::read() const {
    return ButtonLevels{
        digitalRead(cancelPin_) == LOW,
        digitalRead(okPin_) == LOW,
        digitalRead(plusPin_) == LOW,
        digitalRead(minusPin_) == LOW,
    };
}

bool GpioButtonReader::consumeCancelInterrupt() {
    noInterrupts();
    const bool pending = cancelInterruptPending_;
    cancelInterruptPending_ = false;
    interrupts();
    return pending;
}

void IRAM_ATTR GpioButtonReader::isrCancel() {
    if (instance_) {
        instance_->cancelInterruptPending_ = true;
    }
}

}  // namespace faucet
