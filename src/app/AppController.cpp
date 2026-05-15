#include "app/AppController.h"

#include "app/TimeUtils.h"

#include <cmath>
#include <limits>

namespace faucet {
namespace {

constexpr std::uint32_t kResultCalibrationHoldMs = 5000;
constexpr std::uint32_t kCalibrationMinActualMl = 100;
constexpr std::uint32_t kCalibrationMaxDriftPercent = 30;

std::uint32_t msFromSeconds(std::uint32_t seconds) {
    constexpr std::uint32_t maxMs = std::numeric_limits<std::uint32_t>::max();
    return seconds > maxMs / 1000UL ? maxMs : seconds * 1000UL;
}

bool resultAllowsCalibration(WaterResult result) {
    return result == WaterResult::Completed || result == WaterResult::StoppedByUser;
}

bool finitePulsePerMl(float value) {
    return std::isfinite(value) && value >= kMinPulsePerMl && value <= kMaxPulsePerMl;
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
      activeStartTimeSynced_(false),
      activeStartBootId_(0),
      lastValveDesiredOpen_(false),
      calibrationValveOpen_(false),
      lastLogWriteOk_(true),
      persistenceDirty_(false),
      configDirty_(false),
      factoryResetRequested_(false),
      pendingBeep_(BeepPattern::None),
      flowDroppedPulses_(0),
      resultDisplayStartMs_(0),
      adjustmentStepMl_(100),
      lastResultRecordValid_(false),
      lastResultRecord_{},
      resultOkHoldTracking_(false),
      resultOkHoldTriggered_(false),
      resultOkHoldStartMs_(0),
      calibrationActualMl_(0),
      calibrationStepMl_(100) {
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
    updateResultCalibrationHold(input.buttons.okPressed, input.nowMs);
    handleButtonEvent(event, input.nowMs, input.nowSeconds, input.timeSynced, input.bootId);
    if (localMode_ == LocalUiMode::Result && config_.resultDisplaySec > 0 &&
        elapsedAtLeast(input.nowMs, resultDisplayStartMs_, msFromSeconds(config_.resultDisplaySec))) {
        localMode_ = LocalUiMode::Normal;
    }

    syncFlow(input.nowUs);
    const FlowSnapshot flow = flow_.snapshot(input.nowUs);
    water_.tick(input.nowMs, flow.currentFlowMlPerMin);
    syncValve(input.nowMs);

    if (water_.hasResult()) {
        if (config_.resultDisplaySec > 0) {
            localMode_ = LocalUiMode::Result;
            resultDisplayStartMs_ = input.nowMs;
        }
        std::uint32_t resultStartTime = activeStartTimeSec_;
        bool resultStartSynced = activeStartTimeSynced_;
        std::uint32_t resultBootId = activeStartBootId_;
        const std::uint32_t uptimeSec = input.nowMs / 1000UL;
        if (!resultStartSynced && input.timeSynced && input.nowSeconds >= uptimeSec) {
            resultStartTime = input.nowSeconds - uptimeSec + activeStartTimeSec_;
            resultStartSynced = true;
            resultBootId = 0;
        }
        processResult(resultStartTime, input.periodKeys, input.periodKeysValid, resultStartSynced, resultBootId, flow);
        water_.clearResult();
    }

    if (input.periodKeysValid) {
        statistics_.rollPeriods(input.periodKeys);
    }
}

AppSnapshot AppController::snapshot() const {
    AppSnapshot snapshot{};
    snapshot.water = water_.snapshot();
    snapshot.valve = valve_.output();
    snapshot.statistics = statistics_.record();
    snapshot.calibration = calibration_.snapshot();
    snapshot.localMode = localMode_;
    snapshot.adjustmentStepMl = adjustmentStepMl_;
    snapshot.calibrationActualMl = calibrationActualMl_;
    snapshot.calibrationStepMl = calibrationStepMl_;
    snapshot.calibrationReady = lastResultRecordValid_;
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
    return localMode_ != LocalUiMode::Result && water_.canApplyConfig();
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

CalibrationApplyResult AppController::applyCalibrationFromRecord(const WaterLogRecord& record, std::uint32_t actualMl) {
    if (water_.snapshot().state != WaterState::Idle || localMode_ == LocalUiMode::Calibration) {
        return CalibrationApplyResult::NotAvailable;
    }
    if (actualMl < kCalibrationMinActualMl || actualMl > kMaxCalibrationTargetMl) {
        return CalibrationApplyResult::InvalidActual;
    }
    if (record.pulseCount == 0 || !resultAllowsCalibration(record.result)) {
        return CalibrationApplyResult::InvalidRecord;
    }
    const float nextPulsePerMl = static_cast<float>(record.pulseCount) / static_cast<float>(actualMl);
    if (!finitePulsePerMl(nextPulsePerMl)) {
        return CalibrationApplyResult::InvalidFactor;
    }
    const float oldPulsePerMl = finitePulsePerMl(config_.pulsePerMl) ? config_.pulsePerMl : kDefaultPulsePerMl;
    const float ratio = nextPulsePerMl > oldPulsePerMl ? nextPulsePerMl / oldPulsePerMl : oldPulsePerMl / nextPulsePerMl;
    if (ratio > 1.0f + static_cast<float>(kCalibrationMaxDriftPercent) / 100.0f) {
        return CalibrationApplyResult::TooMuchDrift;
    }
    config_.pulsePerMl = nextPulsePerMl;
    flow_.setPulsePerMl(config_.pulsePerMl);
    calibration_.reset(config_.pulsePerMl, config_.calibrationTargetsMl);
    configDirty_ = true;
    pendingBeep_ = BeepPattern::Done;
    localMode_ = LocalUiMode::Result;
    return CalibrationApplyResult::Saved;
}

void AppController::handleButtonEvent(ButtonEvent event,
                                      std::uint32_t nowMs,
                                      std::uint32_t nowSeconds,
                                      bool timeSynced,
                                      std::uint32_t bootId) {
    if (localMode_ == LocalUiMode::Calibration && event.type != ButtonEventType::None) {
        switch (event.type) {
            case ButtonEventType::CancelDown:
            case ButtonEventType::CancelShort:
            case ButtonEventType::CancelLong:
                localMode_ = LocalUiMode::Result;
                pendingBeep_ = BeepPattern::Click;
                break;
            case ButtonEventType::OkShort:
                saveLocalCalibration();
                break;
            case ButtonEventType::OkLong:
                toggleCalibrationStep();
                pendingBeep_ = BeepPattern::Click;
                break;
            case ButtonEventType::PlusShort:
            case ButtonEventType::PlusLong:
                if (adjustCalibrationActual(static_cast<std::int32_t>(calibrationStepMl_))) {
                    pendingBeep_ = BeepPattern::Click;
                }
                break;
            case ButtonEventType::MinusShort:
            case ButtonEventType::MinusLong:
                if (adjustCalibrationActual(-static_cast<std::int32_t>(calibrationStepMl_))) {
                    pendingBeep_ = BeepPattern::Click;
                }
                break;
            default:
                break;
        }
        return;
    }

    if (localMode_ == LocalUiMode::Result && event.type != ButtonEventType::None) {
        if (event.type == ButtonEventType::OkLong) {
            return;
        }
        if ((event.type == ButtonEventType::CancelDown || event.type == ButtonEventType::CancelShort ||
             event.type == ButtonEventType::CancelLong) &&
            !elapsedAtLeast(nowMs, resultDisplayStartMs_, 500UL)) {
            return;
        }
        if (event.type == ButtonEventType::PlusShort || event.type == ButtonEventType::PlusLong) {
            exitResultDisplay(nowMs);
            if (water_.selectNextPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            return;
        }
        if (event.type == ButtonEventType::MinusShort || event.type == ButtonEventType::MinusLong) {
            exitResultDisplay(nowMs);
            if (water_.selectPreviousPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            return;
        }
        exitResultDisplay(nowMs);
        pendingBeep_ = BeepPattern::Click;
        return;
    }

    const WaterSnapshot water = water_.snapshot();
    switch (event.type) {
        case ButtonEventType::CancelDown:
        case ButtonEventType::CancelShort:
        case ButtonEventType::CancelLong:
            emergencyStop(nowMs);
            if (water.state == WaterState::Confirm || water.state == WaterState::Error) {
                water_.cancel(nowMs);
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::OkShort:
            if (water.state == WaterState::Idle || water.state == WaterState::Error) {
                if (water_.requestStart(nowMs)) {
                    adjustmentStepMl_ = 100;
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if (water.state == WaterState::Confirm) {
                startSelectedPreset(nowMs, nowSeconds, timeSynced, bootId);
            } else if (water.state == WaterState::Running || water.state == WaterState::Paused) {
                if (water_.togglePause(nowMs)) {
                    pendingBeep_ = BeepPattern::Click;
                }
            }
            break;
        case ButtonEventType::OkLong:
            if (water.state == WaterState::Confirm || water.state == WaterState::Paused) {
                toggleAdjustmentStep();
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::PlusShort:
        case ButtonEventType::PlusLong:
            if (water.state == WaterState::Confirm || water.state == WaterState::Paused) {
                if (water_.adjustTarget(static_cast<std::int32_t>(adjustmentStepMl_))) {
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if (water_.selectNextPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::MinusShort:
        case ButtonEventType::MinusLong:
            if (water.state == WaterState::Confirm || water.state == WaterState::Paused) {
                if (water_.adjustTarget(-static_cast<std::int32_t>(adjustmentStepMl_))) {
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if (water_.selectPreviousPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        default:
            break;
    }
}

void AppController::startSelectedPreset(std::uint32_t nowMs,
                                        std::uint32_t nowSeconds,
                                        bool timeSynced,
                                        std::uint32_t bootId) {
    if (water_.confirmStart(nowMs)) {
        activeStartTimeSec_ = nowSeconds;
        activeStartTimeSynced_ = timeSynced;
        activeStartBootId_ = timeSynced ? 0 : bootId;
        flow_.reset();
        lastFlowVolumeMl_ = 0;
        pendingBeep_ = BeepPattern::Click;
    }
}

void AppController::exitResultDisplay(std::uint32_t) {
    localMode_ = LocalUiMode::Normal;
    resultOkHoldTracking_ = false;
    resultOkHoldTriggered_ = false;
}

void AppController::toggleAdjustmentStep() {
    adjustmentStepMl_ = adjustmentStepMl_ == 100 ? 500 : 100;
}

void AppController::toggleCalibrationStep() {
    calibrationStepMl_ = calibrationStepMl_ == 100 ? 10 : 100;
}

void AppController::enterCalibrationFromResult(std::uint32_t) {
    if (!lastResultRecordValid_ || !resultAllowsCalibration(lastResultRecord_.result) || lastResultRecord_.pulseCount == 0) {
        pendingBeep_ = BeepPattern::Error;
        return;
    }
    calibrationActualMl_ = lastResultRecord_.volumeMl;
    if (calibrationActualMl_ < kCalibrationMinActualMl) {
        calibrationActualMl_ = 1000;
    }
    if (calibrationActualMl_ > kMaxCalibrationTargetMl) {
        calibrationActualMl_ = kMaxCalibrationTargetMl;
    }
    calibrationStepMl_ = 100;
    localMode_ = LocalUiMode::Calibration;
    resultOkHoldTracking_ = false;
    resultOkHoldTriggered_ = true;
    pendingBeep_ = BeepPattern::Click;
}

void AppController::updateResultCalibrationHold(bool okPressed, std::uint32_t nowMs) {
    if (localMode_ != LocalUiMode::Result) {
        resultOkHoldTracking_ = false;
        resultOkHoldTriggered_ = false;
        return;
    }
    if (!okPressed) {
        resultOkHoldTracking_ = false;
        resultOkHoldTriggered_ = false;
        return;
    }
    if (!resultOkHoldTracking_) {
        resultOkHoldTracking_ = true;
        resultOkHoldTriggered_ = false;
        resultOkHoldStartMs_ = nowMs;
        return;
    }
    if (!resultOkHoldTriggered_ && elapsedAtLeast(nowMs, resultOkHoldStartMs_, kResultCalibrationHoldMs)) {
        enterCalibrationFromResult(nowMs);
    }
}

bool AppController::adjustCalibrationActual(std::int32_t deltaMl) {
    const std::int64_t next = static_cast<std::int64_t>(calibrationActualMl_) + deltaMl;
    const std::uint32_t clamped =
        next < static_cast<std::int64_t>(kCalibrationMinActualMl)
            ? kCalibrationMinActualMl
            : next > static_cast<std::int64_t>(kMaxCalibrationTargetMl) ? kMaxCalibrationTargetMl : static_cast<std::uint32_t>(next);
    if (clamped == calibrationActualMl_) {
        return false;
    }
    calibrationActualMl_ = clamped;
    return true;
}

CalibrationApplyResult AppController::saveLocalCalibration() {
    if (!lastResultRecordValid_) {
        pendingBeep_ = BeepPattern::Error;
        return CalibrationApplyResult::NotAvailable;
    }
    const CalibrationApplyResult result = applyCalibrationFromRecord(lastResultRecord_, calibrationActualMl_);
    if (result != CalibrationApplyResult::Saved) {
        pendingBeep_ = BeepPattern::Error;
    }
    return result;
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

void AppController::processResult(std::uint32_t startTime,
                                  const PeriodKeys& periodKeys,
                                  bool periodKeysValid,
                                  bool startTimeSynced,
                                  std::uint32_t bootId,
                                  const FlowSnapshot& flow) {
    const WaterTaskResult result = water_.result();
    if (!result.valid) {
        return;
    }

    WaterLogRecord record{
        startTime,
        result.volumeMl,
        result.targetValue,
        flow.pulseCount,
        flow.rejectedPulses,
        result.durationSec,
        result.mode,
        result.result,
        result.selectedPreset,
        0,
        config_.pulsePerMl,
        {0, 0, 0, 0},
    };
    if (!startTimeSynced) {
        markWaterLogBootId(record, bootId);
    }

    lastResultRecord_ = record;
    lastResultRecordValid_ = record.pulseCount > 0 && resultAllowsCalibration(record.result);
    lastLogWriteOk_ = logs_.append(record);
    if (periodKeysValid) {
        statistics_.addWater(result.volumeMl, periodKeys);
    }
    filters_.addWater(result.volumeMl);
    persistenceDirty_ = true;
    pendingBeep_ = result.result == WaterResult::Completed ? BeepPattern::Done : BeepPattern::Error;
}

}  // namespace faucet
