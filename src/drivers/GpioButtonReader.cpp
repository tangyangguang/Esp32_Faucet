#include "drivers/GpioButtonReader.h"

#include <Arduino.h>

namespace faucet {

GpioButtonReader::GpioButtonReader(std::uint8_t stopPin, std::uint8_t okPin, std::uint8_t nextPin)
    : stopPin_(stopPin), okPin_(okPin), nextPin_(nextPin) {}

void GpioButtonReader::begin() {
    pinMode(stopPin_, INPUT);
    pinMode(okPin_, INPUT);
    pinMode(nextPin_, INPUT);
}

ButtonLevels GpioButtonReader::read() const {
    return ButtonLevels{
        digitalRead(stopPin_) == LOW,
        digitalRead(okPin_) == LOW,
        digitalRead(nextPin_) == LOW,
    };
}

}  // namespace faucet
