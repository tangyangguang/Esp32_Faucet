#pragma once

#include "app/FlowMeter.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class FlowPulseReader {
public:
    explicit FlowPulseReader(std::uint8_t pin);

    void begin();
    bool pop(std::uint32_t& nowUs);
    std::size_t drainTo(FlowMeter& meter);
    std::uint32_t droppedPulses() const;

private:
    static void isr();
    void pushPulseFromIsr(std::uint32_t nowUs);

    static FlowPulseReader* instance_;

    std::uint8_t pin_;
    volatile std::uint8_t head_;
    volatile std::uint8_t tail_;
    volatile std::uint32_t dropped_;
    volatile std::uint32_t pulseTimes_[32];
};

}  // namespace faucet
