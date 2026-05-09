#include "drivers/GpioButtonReader.h"

#include <Arduino.h>

namespace faucet {

GpioButtonReader* GpioButtonReader::instance_ = nullptr;

GpioButtonReader::GpioButtonReader(std::uint8_t stopPin, std::uint8_t okPin, std::uint8_t nextPin)
    : stopPin_(stopPin), okPin_(okPin), nextPin_(nextPin), stopInterruptPending_(false) {}

void GpioButtonReader::begin() {
    instance_ = this;
    pinMode(stopPin_, INPUT);
    pinMode(okPin_, INPUT);
    pinMode(nextPin_, INPUT);
    attachInterrupt(digitalPinToInterrupt(stopPin_), GpioButtonReader::isrStop, FALLING);
}

ButtonLevels GpioButtonReader::read() const {
    return ButtonLevels{
        digitalRead(stopPin_) == LOW,
        digitalRead(okPin_) == LOW,
        digitalRead(nextPin_) == LOW,
    };
}

bool GpioButtonReader::consumeStopInterrupt() {
    noInterrupts();
    const bool pending = stopInterruptPending_;
    stopInterruptPending_ = false;
    interrupts();
    return pending;
}

void IRAM_ATTR GpioButtonReader::isrStop() {
    if (instance_) {
        instance_->stopInterruptPending_ = true;
    }
}

}  // namespace faucet
