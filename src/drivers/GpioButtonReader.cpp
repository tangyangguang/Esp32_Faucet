#include "drivers/GpioButtonReader.h"

#include <Arduino.h>

namespace faucet {

GpioButtonReader::GpioButtonReader(std::uint8_t cancelPin, std::uint8_t okPin, std::uint8_t plusPin, std::uint8_t minusPin)
    : cancelPin_(cancelPin), okPin_(okPin), plusPin_(plusPin), minusPin_(minusPin) {}

void GpioButtonReader::begin() {
    pinMode(cancelPin_, INPUT);
    pinMode(okPin_, INPUT);
    pinMode(plusPin_, INPUT);
    pinMode(minusPin_, INPUT);
}

ButtonLevels GpioButtonReader::read() const {
    return ButtonLevels{
        digitalRead(cancelPin_) == LOW,
        digitalRead(okPin_) == LOW,
        digitalRead(plusPin_) == LOW,
        digitalRead(minusPin_) == LOW,
    };
}

}  // namespace faucet
