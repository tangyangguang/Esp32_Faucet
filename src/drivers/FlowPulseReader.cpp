#include "drivers/FlowPulseReader.h"

#include <Arduino.h>

namespace faucet {

FlowPulseReader* FlowPulseReader::instance_ = nullptr;

FlowPulseReader::FlowPulseReader(std::uint8_t pin)
    : pin_(pin), head_(0), tail_(0), dropped_(0), pulseTimes_{} {}

void FlowPulseReader::begin() {
    instance_ = this;
    pinMode(pin_, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin_), FlowPulseReader::isr, RISING);
}

bool FlowPulseReader::pop(std::uint32_t& nowUs) {
    noInterrupts();
    if (tail_ == head_) {
        interrupts();
        return false;
    }
    nowUs = pulseTimes_[tail_];
    tail_ = static_cast<std::uint8_t>((tail_ + 1U) % 32U);
    interrupts();
    return true;
}

std::size_t FlowPulseReader::drainTo(FlowMeter& meter) {
    std::size_t drained = 0;
    while (true) {
        std::uint32_t nowUs = 0;
        if (!pop(nowUs)) {
            break;
        }
        meter.onPulse(nowUs);
        ++drained;
    }
    return drained;
}

std::uint32_t FlowPulseReader::droppedPulses() const {
    noInterrupts();
    const std::uint32_t dropped = dropped_;
    interrupts();
    return dropped;
}

void IRAM_ATTR FlowPulseReader::isr() {
    if (instance_) {
        instance_->pushPulseFromIsr(micros());
    }
}

void IRAM_ATTR FlowPulseReader::pushPulseFromIsr(std::uint32_t nowUs) {
    const std::uint8_t next = static_cast<std::uint8_t>((head_ + 1U) % 32U);
    if (next == tail_) {
        ++dropped_;
        return;
    }
    pulseTimes_[head_] = nowUs;
    head_ = next;
}

}  // namespace faucet
