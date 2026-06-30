#pragma once

#include <cstdint>

namespace faucet {

enum class BeepPattern : std::uint8_t {
    None = 0,
    Click = 1,
    Done = 2,
    Error = 3,
};

struct BeepOutput {
    bool enabled;
    std::uint16_t frequencyHz;
    std::uint8_t dutyPercent;
};

class BeepDriver {
public:
    explicit BeepDriver(bool enabled = true);

    void setEnabled(bool enabled);
    void play(BeepPattern pattern, std::uint32_t nowMs);
    void tick(std::uint32_t nowMs);
    BeepOutput output() const;

private:
    bool configuredEnabled_;
    BeepPattern pattern_;
    std::uint32_t startedMs_;
    bool outputEnabled_;

    static std::uint32_t durationMs(BeepPattern pattern);
    static std::uint16_t frequencyHz(BeepPattern pattern);
};

}  // namespace faucet
