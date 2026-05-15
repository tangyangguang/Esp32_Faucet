#pragma once

#include "app/AppConfig.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

enum class WaterState : std::uint8_t {
    Idle = 0,
    Confirm = 1,
    Running = 2,
    Paused = 3,
    Error = 4,
};

struct WaterTaskResult {
    bool valid;
    WaterMode mode;
    WaterResult result;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint16_t durationSec;
    std::uint8_t selectedPreset;
};

struct WaterSnapshot {
    WaterState state;
    std::size_t selectedPreset;
    bool valveOpen;
    std::uint32_t volumeMl;
    std::uint32_t elapsedSec;
    WaterResult lastResult;
    WaterMode mode;
    std::uint32_t targetValue;
};

class WaterController {
public:
    explicit WaterController(const SystemConfig& config);

    WaterSnapshot snapshot() const;
    bool hasResult() const;
    WaterTaskResult result() const;
    void clearResult();
    bool canApplyConfig() const;
    bool applyConfig(const SystemConfig& config);

    bool selectNextPreset();
    bool selectPreviousPreset();
    bool selectPreset(std::size_t index);
    bool requestStart(std::uint32_t nowMs);
    bool confirmStart(std::uint32_t nowMs);
    bool adjustTarget(std::int32_t delta);
    void cancel(std::uint32_t nowMs);
    void stop(std::uint32_t nowMs);
    bool togglePause(std::uint32_t nowMs);
    void addVolume(std::uint32_t volumeMl);
    void tick(std::uint32_t nowMs, std::uint32_t currentFlowMlPerMin = 0);

private:
    const PresetConfig* selectedPresetConfig() const;
    void finish(std::uint32_t nowMs, WaterResult result, WaterState nextState);
    std::uint32_t activeElapsedMs(std::uint32_t nowMs) const;
    bool checkSafety(std::uint32_t nowMs, std::uint32_t currentFlowMlPerMin);
    bool checkTarget(std::uint32_t nowMs);
    bool enabledPreset(std::size_t index) const;

    SystemConfig config_;
    WaterState state_;
    std::size_t selectedPreset_;
    bool valveOpen_;
    std::uint32_t confirmStartMs_;
    std::uint32_t runStartMs_;
    std::uint32_t pausedStartMs_;
    std::uint32_t accumulatedPausedMs_;
    std::uint32_t volumeMl_;
    WaterMode activeMode_;
    std::uint32_t targetValue_;
    std::uint32_t highFlowStartMs_;
    std::uint32_t lastElapsedSec_;
    WaterResult lastError_;
    WaterTaskResult lastResult_;
};

}  // namespace faucet
