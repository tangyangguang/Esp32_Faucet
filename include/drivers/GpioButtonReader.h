#pragma once

#include "app/ButtonInput.h"

#include <cstdint>

namespace faucet {

class GpioButtonReader {
public:
    GpioButtonReader(std::uint8_t stopPin, std::uint8_t okPin, std::uint8_t nextPin);

    void begin();
    ButtonLevels read() const;

private:
    std::uint8_t stopPin_;
    std::uint8_t okPin_;
    std::uint8_t nextPin_;
};

}  // namespace faucet
