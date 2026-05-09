#include "app/AppController.h"

#include "app/TimeUtils.h"

#include <limits>

namespace faucet {
namespace {

std::uint32_t msFromSeconds(std::uint32_t seconds) {
    constexpr std::uint32_t maxMs = std::numeric_limits<std::uint32_t>::max();
    return seconds > maxMs / 1000UL ? maxMs : seconds * 1000UL;
}

}  // namespace

AppController::AppController(const SystemConfig& config,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             AppLogWriter& logs)
    : config_(config),
      water_(config_),
      calibration_(config_.pulsePerMl),
      localMode_(LocalUiMode::Normal),
      buttons_(),
      flow_(config_.pulsePerMl),
      valve_(config_.valveFullPowerSec, config_.valveHoldDutyPercent),
      statistics_(statistics),
      filters_(filters),
      logs_(logs),
      lastFlowVolumeMl_(0),
      activeStartTimeSec_(0),
      lastValveDesiredOpen_(false),
      calibrationValveOpen_(false),
      calibrationStartMs_(0),
      lastLogWriteOk_(true),
      persistenceDirty_(false),
      configDirty_(false),
      factoryResetRequested_(false),
      factoryConfirmArmedMs_(0),
      pendingBeep_(BeepPattern::None),
      flowDroppedPulses_(0) {
    sanitizeConfig(config_);
}

void AppController::resetInputs(ButtonLevels levels, std::uint32_t nowMs) {
    buttons_.reset(levels, nowMs);
}

void AppController::onFlowPulse(std::uint32_t nowUs) {
    flow_.onPulse(nowUs);
}

void AppController::tick(const AppTickInput& input) {
    const ButtonEvent event = buttons_.update(input.buttons, input.nowMs);
    if (localMode_ == LocalUiMode::Normal) {
        handleButtonEvent(event, input.nowMs, input.nowSeconds);
    } else {
        handleCalibrationEvent(event, input.nowMs, input.nowUs);
    }

    if (localMode_ == LocalUiMode::CalibrationSampling &&
        elapsedAtLeast(input.nowMs, calibrationStartMs_, msFromSeconds(config_.maxOutTimeSec))) {
        calibrationValveOpen_ = false;
        localMode_ = LocalUiMode::CalibrationRejected;
        pendingBeep_ = BeepPattern::Error;
    }

    syncFlow(input.nowUs);
    const FlowSnapshot flow = flow_.snapshot(input.nowUs);
    water_.tick(input.nowMs, flow.currentFlowMlPerMin);
    syncValve(input.nowMs);

    if (water_.hasResult()) {
        processResult(activeStartTimeSec_, input.periodKeys);
        water_.clearResult();
    }

    statistics_.rollPeriods(input.periodKeys);
}

AppSnapshot AppController::snapshot() const {
    AppSnapshot snapshot{};
    snapshot.water = water_.snapshot();
    snapshot.valve = valve_.output();
    snapshot.statistics = statistics_.record();
    snapshot.calibration = calibration_.snapshot();
    snapshot.localMode = localMode_;
    snapshot.flowDroppedPulses = flowDroppedPulses_;
    return snapshot;
}

bool AppController::lastLogWriteOk() const {
    return lastLogWriteOk_;
}

bool AppController::consumePersistenceDirty() {
    const bool dirty = persistenceDirty_;
    persistenceDirty_ = false;
    return dirty;
}

bool AppController::consumeConfigDirty() {
    const bool dirty = configDirty_;
    configDirty_ = false;
    return dirty;
}

const SystemConfig& AppController::config() const {
    return config_;
}

bool AppController::consumeFactoryResetRequest() {
    const bool requested = factoryResetRequested_;
    factoryResetRequested_ = false;
    return requested;
}

BeepPattern AppController::consumeBeepPattern() {
    const BeepPattern pattern = pendingBeep_;
    pendingBeep_ = BeepPattern::None;
    return pattern;
}

bool AppController::emergencyStop(std::uint32_t nowMs) {
    const WaterState state = water_.snapshot().state;
    water_.stop(nowMs);
    syncValve(nowMs);
    if (state == WaterState::Running || state == WaterState::Paused) {
        pendingBeep_ = BeepPattern::Error;
        return true;
    }
    return false;
}

void AppController::setFlowDroppedPulses(std::uint32_t droppedPulses) {
    flowDroppedPulses_ = droppedPulses;
}

bool AppController::canApplyConfig() const {
    return localMode_ == LocalUiMode::Normal && water_.canApplyConfig();
}

bool AppController::applyConfig(const SystemConfig& config) {
    if (!canApplyConfig() || !water_.applyConfig(config)) {
        return false;
    }
    SystemConfig safe = config;
    sanitizeConfig(safe);
    config_ = safe;
    flow_.setPulsePerMl(config_.pulsePerMl);
    valve_.configure(config_.valveFullPowerSec, config_.valveHoldDutyPercent);
    calibration_.reset(config_.pulsePerMl, config_.calibrationTargetsMl);
    return true;
}

void AppController::handleButtonEvent(ButtonEvent event, std::uint32_t nowMs, std::uint32_t nowSeconds) {
    switch (event.type) {
        case ButtonEventType::StopDown:
        case ButtonEventType::StopShort:
        case ButtonEventType::StopLong:
            emergencyStop(nowMs);
            break;
        case ButtonEventType::OkShort:
            if (water_.snapshot().state == WaterState::Idle) {
                if (water_.requestStart(nowMs)) {
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if (water_.snapshot().state == WaterState::Confirm) {
                if (water_.confirmStart(nowMs)) {
                    activeStartTimeSec_ = nowSeconds;
                    flow_.reset();
                    lastFlowVolumeMl_ = 0;
                    pendingBeep_ = BeepPattern::Click;
                }
            } else {
                if (water_.togglePause(nowMs)) {
                    pendingBeep_ = BeepPattern::Click;
                }
            }
            break;
        case ButtonEventType::OkLong:
            if (water_.snapshot().state == WaterState::Idle) {
                enterCalibration();
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::NextShort:
            if (water_.selectNextPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::FactoryResetCombo:
            if (water_.snapshot().state == WaterState::Idle) {
                localMode_ = LocalUiMode::FactoryResetConfirm;
                factoryConfirmArmedMs_ = nowMs;
                pendingBeep_ = BeepPattern::Error;
            }
            break;
        default:
            break;
    }
}

void AppController::handleCalibrationEvent(ButtonEvent event, std::uint32_t nowMs, std::uint32_t nowUs) {
    if (localMode_ == LocalUiMode::FactoryResetConfirm && !elapsedAtLeast(nowMs, factoryConfirmArmedMs_, 300UL) &&
        event.type != ButtonEventType::None) {
        return;
    }
    switch (event.type) {
        case ButtonEventType::StopDown:
        case ButtonEventType::StopShort:
        case ButtonEventType::StopLong:
            calibrationValveOpen_ = false;
            localMode_ = LocalUiMode::Normal;
            calibration_.reset(config_.pulsePerMl, config_.calibrationTargetsMl);
            flow_.reset();
            lastFlowVolumeMl_ = 0;
            pendingBeep_ = BeepPattern::Click;
            break;
        case ButtonEventType::NextShort:
        case ButtonEventType::NextLong:
            if (localMode_ == LocalUiMode::CalibrationSelect || localMode_ == LocalUiMode::CalibrationRejected) {
                cycleCalibrationTarget();
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::OkShort:
            if (localMode_ == LocalUiMode::CalibrationSelect || localMode_ == LocalUiMode::CalibrationRejected) {
                localMode_ = LocalUiMode::CalibrationConfirm;
                pendingBeep_ = BeepPattern::Click;
            } else if (localMode_ == LocalUiMode::CalibrationConfirm) {
                if (calibration_.beginSampling()) {
                    calibrationValveOpen_ = true;
                    calibrationStartMs_ = nowMs;
                    flow_.reset();
                    lastFlowVolumeMl_ = 0;
                    localMode_ = LocalUiMode::CalibrationSampling;
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if (localMode_ == LocalUiMode::CalibrationSampling) {
                finishCalibrationSample(nowUs);
            } else if (localMode_ == LocalUiMode::CalibrationReview) {
                saveCalibration();
            } else if (localMode_ == LocalUiMode::FactoryResetConfirm) {
                factoryResetRequested_ = true;
                localMode_ = LocalUiMode::Normal;
                pendingBeep_ = BeepPattern::Error;
            }
            break;
        default:
            break;
    }
}

void AppController::enterCalibration() {
    calibration_.reset(config_.pulsePerMl, config_.calibrationTargetsMl);
    localMode_ = LocalUiMode::CalibrationSelect;
}

void AppController::cycleCalibrationTarget() {
    const std::uint32_t current = calibration_.snapshot().targetMl;
    const std::uint32_t next = nextEnabledCalibrationTarget(config_.calibrationTargetsMl, current);
    calibration_.setTargetMl(next);
    localMode_ = LocalUiMode::CalibrationSelect;
}

void AppController::finishCalibrationSample(std::uint32_t nowUs) {
    calibrationValveOpen_ = false;
    const FlowSnapshot snapshot = flow_.snapshot(nowUs);
    const CalibrationSampleResult result = calibration_.finishSample(snapshot.pulseCount);
    if (result != CalibrationSampleResult::Accepted) {
        localMode_ = LocalUiMode::CalibrationRejected;
        pendingBeep_ = BeepPattern::Error;
        return;
    }
    localMode_ = calibration_.canSave() ? LocalUiMode::CalibrationReview : LocalUiMode::CalibrationSelect;
    pendingBeep_ = calibration_.canSave() ? BeepPattern::Done : BeepPattern::Click;
}

void AppController::saveCalibration() {
    if (!calibration_.canSave()) {
        return;
    }
    config_.pulsePerMl = calibration_.proposedPulsePerMl();
    sanitizeConfig(config_);
    flow_.setPulsePerMl(config_.pulsePerMl);
    calibration_.reset(config_.pulsePerMl, config_.calibrationTargetsMl);
    localMode_ = LocalUiMode::Normal;
    configDirty_ = true;
    pendingBeep_ = BeepPattern::Done;
}

void AppController::syncFlow(std::uint32_t nowUs) {
    if (calibrationValveOpen_) {
        return;
    }
    if (water_.snapshot().state != WaterState::Running) {
        return;
    }

    const FlowSnapshot flow = flow_.snapshot(nowUs);
    if (flow.volumeMl > lastFlowVolumeMl_) {
        water_.addVolume(flow.volumeMl - lastFlowVolumeMl_);
        lastFlowVolumeMl_ = flow.volumeMl;
    }
}

void AppController::syncValve(std::uint32_t nowMs) {
    const bool desiredOpen = water_.snapshot().valveOpen || calibrationValveOpen_;
    if (desiredOpen && !lastValveDesiredOpen_) {
        valve_.open(nowMs);
    } else if (!desiredOpen && lastValveDesiredOpen_) {
        valve_.close();
    }
    valve_.tick(nowMs);
    lastValveDesiredOpen_ = desiredOpen;
}

void AppController::processResult(std::uint32_t startTime, const PeriodKeys& periodKeys) {
    const WaterTaskResult result = water_.result();
    if (!result.valid) {
        return;
    }

    const WaterLogRecord record{
        startTime,
        result.volumeMl,
        result.durationSec,
        result.mode,
        result.result,
        {0, 0},
    };

    lastLogWriteOk_ = logs_.append(record);
    statistics_.addWater(result.volumeMl, periodKeys);
    filters_.addWater(result.volumeMl);
    persistenceDirty_ = true;
    pendingBeep_ = result.result == WaterResult::Completed ? BeepPattern::Done : BeepPattern::Error;
}

}  // namespace faucet
