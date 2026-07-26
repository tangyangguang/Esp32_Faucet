#pragma once

#include "app/ButtonInput.h"

#include <cstdint>

namespace faucet {

class GpioButtonReader {
public:
    GpioButtonReader(std::uint8_t cancelPin, std::uint8_t okPin, std::uint8_t plusPin, std::uint8_t minusPin);

    void begin();
    ButtonLevels read() const;

private:
    std::uint8_t cancelPin_;
    std::uint8_t okPin_;
    std::uint8_t plusPin_;
    std::uint8_t minusPin_;
};

}  // namespace faucet
