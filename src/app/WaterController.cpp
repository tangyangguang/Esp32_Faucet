#include "app/WaterController.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <limits>

namespace faucet {
namespace {

std::uint32_t msFromSeconds(std::uint32_t seconds) {
    constexpr std::uint32_t maxMs = std::numeric_limits<std::uint32_t>::max();
    return seconds > maxMs / 1000UL ? maxMs : seconds * 1000UL;
}

WaterMode modeFromPreset(const PresetConfig& preset) {
    return preset.type == PresetType::Time ? WaterMode::Time : WaterMode::Volume;
}

}  // namespace

WaterController::WaterController(const SystemConfig& config)
    : config_(config),
      state_(WaterState::Idle),
      selectedPreset_(0),
      valveOpen_(false),
      confirmStartMs_(0),
      runStartMs_(0),
      pausedStartMs_(0),
      accumulatedPausedMs_(0),
      volumeMl_(0),
      highFlowStartMs_(0),
      lastElapsedSec_(0),
      lastError_(WaterResult::Completed),
      lastResult_{} {
    sanitizeConfig(config_);
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (enabledPreset(i)) {
            selectedPreset_ = i;
            break;
        }
    }
}

WaterSnapshot WaterController::snapshot() const {
    const PresetConfig* preset = selectedPresetConfig();
    return WaterSnapshot{
        state_,
        selectedPreset_,
        valveOpen_,
        volumeMl_,
        lastElapsedSec_,
        lastError_,
        preset ? modeFromPreset(*preset) : WaterMode::Volume,
        preset ? preset->value : 0,
    };
}

bool WaterController::hasResult() const {
    return lastResult_.valid;
}

WaterTaskResult WaterController::result() const {
    return lastResult_;
}

void WaterController::clearResult() {
    lastResult_ = {};
}

bool WaterController::canApplyConfig() const {
    return state_ != WaterState::Confirm && state_ != WaterState::Running && state_ != WaterState::Paused;
}

bool WaterController::applyConfig(const SystemConfig& config) {
    if (!canApplyConfig()) {
        return false;
    }
    config_ = config;
    sanitizeConfig(config_);
    if (!enabledPreset(selectedPreset_)) {
        selectedPreset_ = 0;
        for (std::size_t i = 0; i < kPresetCount; ++i) {
            if (enabledPreset(i)) {
                selectedPreset_ = i;
                break;
            }
        }
    }
    return true;
}

bool WaterController::selectNextPreset() {
    if (state_ == WaterState::Running || state_ == WaterState::Paused) {
        return false;
    }
    for (std::size_t step = 1; step <= kPresetCount; ++step) {
        const std::size_t index = (selectedPreset_ + step) % kPresetCount;
        if (enabledPreset(index)) {
            selectedPreset_ = index;
            return true;
        }
    }
    return false;
}

bool WaterController::selectPreset(std::size_t index) {
    if (state_ == WaterState::Running || state_ == WaterState::Paused || !enabledPreset(index)) {
        return false;
    }
    selectedPreset_ = index;
    return true;
}

bool WaterController::requestStart(std::uint32_t nowMs) {
    if (state_ != WaterState::Idle && state_ != WaterState::Error) {
        return false;
    }
    if (!selectedPresetConfig()) {
        return false;
    }
    state_ = WaterState::Confirm;
    confirmStartMs_ = nowMs;
    valveOpen_ = false;
    lastElapsedSec_ = 0;
    lastError_ = WaterResult::Completed;
    return true;
}

bool WaterController::confirmStart(std::uint32_t nowMs) {
    if (state_ != WaterState::Confirm || !selectedPresetConfig()) {
        return false;
    }
    state_ = WaterState::Running;
    valveOpen_ = true;
    runStartMs_ = nowMs;
    pausedStartMs_ = 0;
    accumulatedPausedMs_ = 0;
    volumeMl_ = 0;
    highFlowStartMs_ = 0;
    lastElapsedSec_ = 0;
    lastError_ = WaterResult::Completed;
    clearResult();
    return true;
}

void WaterController::cancel(std::uint32_t) {
    if (state_ == WaterState::Confirm || state_ == WaterState::Error) {
        state_ = WaterState::Idle;
        valveOpen_ = false;
    }
}

void WaterController::stop(std::uint32_t nowMs) {
    if (state_ == WaterState::Running || state_ == WaterState::Paused) {
        finish(nowMs, WaterResult::StoppedByUser, WaterState::Idle);
        return;
    }
    state_ = WaterState::Idle;
    valveOpen_ = false;
}

bool WaterController::togglePause(std::uint32_t nowMs) {
    if (state_ == WaterState::Running) {
        state_ = WaterState::Paused;
        valveOpen_ = false;
        pausedStartMs_ = nowMs;
        return true;
    }
    if (state_ == WaterState::Paused) {
        state_ = WaterState::Running;
        valveOpen_ = true;
        accumulatedPausedMs_ += elapsedSince(nowMs, pausedStartMs_);
        pausedStartMs_ = 0;
        return true;
    }
    return false;
}

void WaterController::addVolume(std::uint32_t volumeMl) {
    if (state_ != WaterState::Running) {
        return;
    }
    const std::uint32_t max = std::numeric_limits<std::uint32_t>::max();
    volumeMl_ = max - volumeMl_ < volumeMl ? max : volumeMl_ + volumeMl;
}

void WaterController::tick(std::uint32_t nowMs, std::uint32_t currentFlowMlPerMin) {
    if (state_ == WaterState::Running || state_ == WaterState::Paused) {
        lastElapsedSec_ = activeElapsedMs(nowMs) / 1000UL;
    }

    if (state_ == WaterState::Confirm &&
        elapsedSince(nowMs, confirmStartMs_) >= msFromSeconds(config_.confirmTimeoutSec)) {
        state_ = WaterState::Idle;
        valveOpen_ = false;
        return;
    }

    if (state_ == WaterState::Paused &&
        elapsedSince(nowMs, pausedStartMs_) >= msFromSeconds(config_.pauseTimeoutSec)) {
        finish(nowMs, WaterResult::PauseTimeout, WaterState::Idle);
        return;
    }

    if (state_ != WaterState::Running) {
        return;
    }
    if (checkSafety(nowMs, currentFlowMlPerMin)) {
        return;
    }
    checkTarget(nowMs);
}

const PresetConfig* WaterController::selectedPresetConfig() const {
    return enabledPreset(selectedPreset_) ? &config_.presets[selectedPreset_] : nullptr;
}

void WaterController::finish(std::uint32_t nowMs, WaterResult result, WaterState nextState) {
    const PresetConfig* preset = selectedPresetConfig();
    lastResult_.valid = true;
    lastResult_.mode = preset ? modeFromPreset(*preset) : WaterMode::Volume;
    lastResult_.result = result;
    lastResult_.volumeMl = volumeMl_;
    lastResult_.durationSec = static_cast<std::uint16_t>(std::min<std::uint32_t>(activeElapsedMs(nowMs) / 1000UL, 65535));
    lastError_ = result;
    lastElapsedSec_ = activeElapsedMs(nowMs) / 1000UL;

    state_ = nextState;
    valveOpen_ = false;
    highFlowStartMs_ = 0;
}

std::uint32_t WaterController::activeElapsedMs(std::uint32_t nowMs) const {
    std::uint32_t paused = accumulatedPausedMs_;
    if (state_ == WaterState::Paused) {
        paused += elapsedSince(nowMs, pausedStartMs_);
    }
    const std::uint32_t total = elapsedSince(nowMs, runStartMs_);
    return total > paused ? total - paused : 0;
}

bool WaterController::checkSafety(std::uint32_t nowMs, std::uint32_t currentFlowMlPerMin) {
    if (activeElapsedMs(nowMs) >= msFromSeconds(config_.maxOutTimeSec) || volumeMl_ >= config_.maxOutVolumeMl) {
        finish(nowMs, WaterResult::SafetyStopped, WaterState::Error);
        return true;
    }

    const PresetConfig* preset = selectedPresetConfig();
    if (preset && preset->type == PresetType::Volume) {
        const std::uint64_t overflowLimit =
            (static_cast<std::uint64_t>(preset->value) * (100UL + config_.overflowPercent)) / 100UL;
        if (volumeMl_ > overflowLimit) {
            finish(nowMs, WaterResult::SafetyStopped, WaterState::Error);
            return true;
        }
    }

    if (volumeMl_ == 0 && activeElapsedMs(nowMs) >= msFromSeconds(config_.noFlowTimeoutSec)) {
        finish(nowMs, WaterResult::FlowError, WaterState::Error);
        return true;
    }

    if (currentFlowMlPerMin >= config_.highFlowMlPerMin) {
        if (highFlowStartMs_ == 0) {
            highFlowStartMs_ = nowMs;
        } else if (elapsedSince(nowMs, highFlowStartMs_) >= msFromSeconds(config_.highFlowDurationSec)) {
            finish(nowMs, WaterResult::FlowError, WaterState::Error);
            return true;
        }
    } else {
        highFlowStartMs_ = 0;
    }
    return false;
}

bool WaterController::checkTarget(std::uint32_t nowMs) {
    const PresetConfig* preset = selectedPresetConfig();
    if (!preset) {
        return false;
    }
    if (preset->type == PresetType::Volume && volumeMl_ >= preset->value) {
        finish(nowMs, WaterResult::Completed, WaterState::Idle);
        return true;
    }
    if (preset->type == PresetType::Time && activeElapsedMs(nowMs) >= msFromSeconds(preset->value)) {
        finish(nowMs, WaterResult::Completed, WaterState::Idle);
        return true;
    }
    return false;
}

bool WaterController::enabledPreset(std::size_t index) const {
    return index < kPresetCount && config_.presets[index].enabled;
}

}  // namespace faucet
