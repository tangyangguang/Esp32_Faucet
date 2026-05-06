#include "app/AppController.h"

namespace faucet {

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
      factoryConfirmArmedMs_(0) {
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
        input.nowMs >= calibrationStartMs_ &&
        input.nowMs - calibrationStartMs_ >= config_.maxOutTimeSec * 1000UL) {
        calibrationValveOpen_ = false;
        localMode_ = LocalUiMode::CalibrationRejected;
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

void AppController::handleButtonEvent(ButtonEvent event, std::uint32_t nowMs, std::uint32_t nowSeconds) {
    switch (event.type) {
        case ButtonEventType::StopDown:
        case ButtonEventType::StopShort:
        case ButtonEventType::StopLong:
            water_.stop(nowMs);
            break;
        case ButtonEventType::OkShort:
            if (water_.snapshot().state == WaterState::Idle) {
                water_.requestStart(nowMs);
            } else if (water_.snapshot().state == WaterState::Confirm) {
                if (water_.confirmStart(nowMs)) {
                    activeStartTimeSec_ = nowSeconds;
                    flow_.reset();
                    lastFlowVolumeMl_ = 0;
                }
            } else {
                water_.togglePause(nowMs);
            }
            break;
        case ButtonEventType::OkLong:
            if (water_.snapshot().state == WaterState::Idle) {
                enterCalibration();
            }
            break;
        case ButtonEventType::NextShort:
            water_.selectNextPreset();
            break;
        case ButtonEventType::FactoryResetCombo:
            if (water_.snapshot().state == WaterState::Idle) {
                localMode_ = LocalUiMode::FactoryResetConfirm;
                factoryConfirmArmedMs_ = nowMs + 300UL;
            }
            break;
        default:
            break;
    }
}

void AppController::handleCalibrationEvent(ButtonEvent event, std::uint32_t nowMs, std::uint32_t nowUs) {
    if (localMode_ == LocalUiMode::FactoryResetConfirm && nowMs < factoryConfirmArmedMs_ &&
        event.type != ButtonEventType::None) {
        return;
    }
    switch (event.type) {
        case ButtonEventType::StopDown:
        case ButtonEventType::StopShort:
        case ButtonEventType::StopLong:
            calibrationValveOpen_ = false;
            localMode_ = LocalUiMode::Normal;
            calibration_.reset(config_.pulsePerMl);
            flow_.reset();
            lastFlowVolumeMl_ = 0;
            break;
        case ButtonEventType::NextShort:
        case ButtonEventType::NextLong:
            if (localMode_ == LocalUiMode::CalibrationSelect || localMode_ == LocalUiMode::CalibrationRejected) {
                cycleCalibrationTarget();
            }
            break;
        case ButtonEventType::OkShort:
            if (localMode_ == LocalUiMode::CalibrationSelect || localMode_ == LocalUiMode::CalibrationRejected) {
                localMode_ = LocalUiMode::CalibrationConfirm;
            } else if (localMode_ == LocalUiMode::CalibrationConfirm) {
                if (calibration_.beginSampling()) {
                    calibrationValveOpen_ = true;
                    calibrationStartMs_ = nowMs;
                    flow_.reset();
                    lastFlowVolumeMl_ = 0;
                    localMode_ = LocalUiMode::CalibrationSampling;
                }
            } else if (localMode_ == LocalUiMode::CalibrationSampling) {
                finishCalibrationSample(nowUs);
            } else if (localMode_ == LocalUiMode::CalibrationReview) {
                saveCalibration();
            } else if (localMode_ == LocalUiMode::FactoryResetConfirm) {
                factoryResetRequested_ = true;
                localMode_ = LocalUiMode::Normal;
            }
            break;
        default:
            break;
    }
}

void AppController::enterCalibration() {
    calibration_.reset(config_.pulsePerMl);
    localMode_ = LocalUiMode::CalibrationSelect;
}

void AppController::cycleCalibrationTarget() {
    const std::uint32_t current = calibration_.snapshot().targetMl;
    const std::uint32_t next = current == 500 ? 1000 : current == 1000 ? 1500 : current == 1500 ? 2000 : 500;
    calibration_.setTargetMl(next);
    localMode_ = LocalUiMode::CalibrationSelect;
}

void AppController::finishCalibrationSample(std::uint32_t nowUs) {
    calibrationValveOpen_ = false;
    const FlowSnapshot snapshot = flow_.snapshot(nowUs);
    const CalibrationSampleResult result = calibration_.finishSample(snapshot.pulseCount);
    if (result != CalibrationSampleResult::Accepted) {
        localMode_ = LocalUiMode::CalibrationRejected;
        return;
    }
    localMode_ = calibration_.canSave() ? LocalUiMode::CalibrationReview : LocalUiMode::CalibrationSelect;
}

void AppController::saveCalibration() {
    if (!calibration_.canSave()) {
        return;
    }
    config_.pulsePerMl = calibration_.proposedPulsePerMl();
    sanitizeConfig(config_);
    flow_.setPulsePerMl(config_.pulsePerMl);
    calibration_.reset(config_.pulsePerMl);
    localMode_ = LocalUiMode::Normal;
    configDirty_ = true;
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
}

}  // namespace faucet
