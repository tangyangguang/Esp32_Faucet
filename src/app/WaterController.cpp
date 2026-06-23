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
      activePreset_(0),
      valveOpen_(false),
      confirmStartMs_(0),
      runStartMs_(0),
      pausedStartMs_(0),
      accumulatedPausedMs_(0),
      volumeMl_(0),
      noFlowLastVolumeMl_(0),
      noFlowLastActivityMs_(0),
      activeMode_(WaterMode::Volume),
      targetValue_(0),
      highFlowStartMs_(0),
      lastElapsedSec_(0),
      lastError_(WaterResult::Completed),
      lastResult_{} {
    sanitizeConfig(config_);
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (enabledPreset(i)) {
            selectedPreset_ = i;
            activePreset_ = i;
            break;
        }
    }
}

WaterSnapshot WaterController::snapshot() const {
    const PresetConfig* preset = selectedPresetConfig();
    return WaterSnapshot{
        state_,
        selectedPreset_,
        state_ == WaterState::Idle ? selectedPreset_ : activePreset_,
        valveOpen_,
        volumeMl_,
        lastElapsedSec_,
        lastError_,
        state_ == WaterState::Idle ? (preset ? modeFromPreset(*preset) : WaterMode::Volume) : activeMode_,
        state_ == WaterState::Idle ? (preset ? preset->value : 0) : targetValue_,
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
    activePreset_ = selectedPreset_;
    return true;
}

bool WaterController::selectNextPreset() {
    for (std::size_t step = 1; step <= kPresetCount; ++step) {
        const std::size_t index = (selectedPreset_ + step) % kPresetCount;
        if (enabledPreset(index)) {
            selectedPreset_ = index;
            return true;
        }
    }
    return false;
}

bool WaterController::selectPreviousPreset() {
    for (std::size_t step = 1; step <= kPresetCount; ++step) {
        const std::size_t index = (selectedPreset_ + kPresetCount - step) % kPresetCount;
        if (enabledPreset(index)) {
            selectedPreset_ = index;
            return true;
        }
    }
    return false;
}

bool WaterController::selectPreset(std::size_t index) {
    if (!enabledPreset(index)) {
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
    activePreset_ = selectedPreset_;
    state_ = WaterState::Confirm;
    confirmStartMs_ = nowMs;
    valveOpen_ = false;
    activeMode_ = modeFromPreset(*activePresetConfig());
    targetValue_ = activePresetConfig()->value;
    volumeMl_ = 0;
    noFlowLastVolumeMl_ = 0;
    noFlowLastActivityMs_ = nowMs;
    runStartMs_ = nowMs;
    pausedStartMs_ = 0;
    accumulatedPausedMs_ = 0;
    highFlowStartMs_ = 0;
    lastElapsedSec_ = 0;
    lastError_ = WaterResult::Completed;
    return true;
}

bool WaterController::confirmStart(std::uint32_t nowMs) {
    if (state_ != WaterState::Confirm || !activePresetConfig()) {
        return false;
    }
    state_ = WaterState::Running;
    valveOpen_ = true;
    runStartMs_ = nowMs;
    pausedStartMs_ = 0;
    accumulatedPausedMs_ = 0;
    volumeMl_ = 0;
    noFlowLastVolumeMl_ = 0;
    noFlowLastActivityMs_ = nowMs;
    highFlowStartMs_ = 0;
    lastElapsedSec_ = 0;
    lastError_ = WaterResult::Completed;
    clearResult();
    return true;
}

bool WaterController::startUntargeted(std::uint32_t nowMs) {
    if (state_ != WaterState::Idle && state_ != WaterState::Error) {
        return false;
    }
    if (!selectedPresetConfig()) {
        return false;
    }
    activePreset_ = selectedPreset_;
    state_ = WaterState::Running;
    valveOpen_ = true;
    activeMode_ = WaterMode::Volume;
    targetValue_ = 0;
    confirmStartMs_ = 0;
    runStartMs_ = nowMs;
    pausedStartMs_ = 0;
    accumulatedPausedMs_ = 0;
    volumeMl_ = 0;
    noFlowLastVolumeMl_ = 0;
    noFlowLastActivityMs_ = nowMs;
    highFlowStartMs_ = 0;
    lastElapsedSec_ = 0;
    lastError_ = WaterResult::Completed;
    clearResult();
    return true;
}

bool WaterController::adjustTarget(std::int32_t delta) {
    if (state_ != WaterState::Confirm || delta == 0) {
        return false;
    }

    const std::uint32_t minTarget = activeMode_ == WaterMode::Time ? kMinTimePresetSec : kMinVolumePresetMl;
    const std::uint32_t maxTarget =
        activeMode_ == WaterMode::Time ? kMaxTimePresetSec : std::min<std::uint32_t>(kMaxVolumePresetMl, config_.maxOutVolumeMl);
    std::int64_t next = static_cast<std::int64_t>(targetValue_) + delta;
    if (next < static_cast<std::int64_t>(minTarget)) {
        next = minTarget;
    }
    if (next > static_cast<std::int64_t>(maxTarget)) {
        next = maxTarget;
    }
    const std::uint32_t changed = static_cast<std::uint32_t>(next);
    if (changed == targetValue_) {
        return false;
    }
    targetValue_ = changed;
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
        noFlowLastActivityMs_ = nowMs;
        noFlowLastVolumeMl_ = volumeMl_;
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

const PresetConfig* WaterController::activePresetConfig() const {
    return enabledPreset(activePreset_) ? &config_.presets[activePreset_] : nullptr;
}

void WaterController::finish(std::uint32_t nowMs, WaterResult result, WaterState nextState) {
    const PresetConfig* preset = activePresetConfig();
    lastResult_.valid = true;
    lastResult_.mode = state_ == WaterState::Idle ? (preset ? modeFromPreset(*preset) : WaterMode::Volume) : activeMode_;
    lastResult_.result = result;
    lastResult_.volumeMl = volumeMl_;
    lastResult_.targetValue = targetValue_;
    lastResult_.durationSec = static_cast<std::uint16_t>(std::min<std::uint32_t>(activeElapsedMs(nowMs) / 1000UL, 65535));
    lastResult_.selectedPreset = activePreset_ < 255 ? static_cast<std::uint8_t>(activePreset_) : 255;
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

    if (activeMode_ == WaterMode::Volume && targetValue_ > 0) {
        const std::uint64_t overflowLimit =
            (static_cast<std::uint64_t>(targetValue_) * (100UL + config_.overflowPercent)) / 100UL;
        if (volumeMl_ > overflowLimit) {
            finish(nowMs, WaterResult::SafetyStopped, WaterState::Error);
            return true;
        }
    }

    if (volumeMl_ > noFlowLastVolumeMl_) {
        noFlowLastVolumeMl_ = volumeMl_;
        noFlowLastActivityMs_ = nowMs;
    }
    if (elapsedSince(nowMs, noFlowLastActivityMs_) >= msFromSeconds(config_.noFlowTimeoutSec)) {
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
    if (targetValue_ == 0) {
        return false;
    }
    if (activeMode_ == WaterMode::Volume && volumeMl_ >= targetValue_) {
        finish(nowMs, WaterResult::Completed, WaterState::Idle);
        return true;
    }
    if (activeMode_ == WaterMode::Time && activeElapsedMs(nowMs) >= msFromSeconds(targetValue_)) {
        finish(nowMs, WaterResult::Completed, WaterState::Idle);
        return true;
    }
    return false;
}

bool WaterController::enabledPreset(std::size_t index) const {
    return index < kPresetCount && config_.presets[index].enabled;
}

}  // namespace faucet
