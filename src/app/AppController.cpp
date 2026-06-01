#include "app/AppController.h"

#include "app/TimeUtils.h"

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

WaterPulseTraceState traceStateForResult(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return WaterPulseTraceState::Completed;
        case WaterResult::StoppedByUser:
            return WaterPulseTraceState::Stopped;
        case WaterResult::PauseTimeout:
            return WaterPulseTraceState::PauseTimeout;
        case WaterResult::SafetyStopped:
            return WaterPulseTraceState::SafetyStopped;
        case WaterResult::FlowError:
        default:
            return WaterPulseTraceState::Error;
    }
}

MeteringSchemeRecord schemeFromLegacyConfig(const SystemConfig& config) {
    (void)config;
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(scheme, 1, "运行计量方案", defaultMeteringParameters(), 0);
    return scheme;
}

}  // namespace

AppController::AppController(const SystemConfig& config,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             WaterPulseTraceStore* pulseTraces)
    : config_(config),
      activeMeteringScheme_(schemeFromLegacyConfig(config)),
      water_(config_),
      localMode_(LocalUiMode::Normal),
      buttons_(),
      flow_(activeMeteringScheme_.params, config_.pulseMinIntervalUs),
      valve_(config_.valveFullPowerSec, config_.valveHoldDutyPercent),
      statistics_(statistics),
      filters_(filters),
      records_(records),
      meteringSnapshots_(nullptr),
      meteringSchemes_(nullptr),
      pulseTraces_(pulseTraces),
      activeTraceId_(0),
      activeTraceStartUs_(0),
      lastFlowVolumeMl_(0),
      activeStartTimeSec_(0),
      activeStartTimeSynced_(false),
      activeStartBootId_(0),
      lastValveDesiredOpen_(false),
      calibrationValveOpen_(false),
      lastRecordWriteOk_(true),
      persistenceDirty_(false),
      configDirty_(false),
      factoryResetRequested_(false),
      pendingBeep_(BeepPattern::None),
      flowDroppedPulses_(0),
      resultDisplayStartMs_(0),
      adjustmentStepMl_(config_.volumeAdjustStepMl),
      timeAdjustmentStepSec_(config_.timeAdjustStepSec),
      lastResultRecordValid_(false),
      lastResultRecord_{},
      resultOkHoldTracking_(false),
      resultOkHoldTriggered_(false),
      resultOkHoldStartMs_(0),
      calibrationActualMl_(0),
      calibrationStepMl_(100),
      calibrationIgnoreOkUntilReleased_(false) {
    sanitizeConfig(config_);
}

AppController::AppController(const SystemConfig& config,
                             const MeteringSchemeRecord& activeScheme,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             WaterRecordMeteringSnapshotWriter& meteringSnapshots,
                             MeteringSchemeStore& meteringSchemes,
                             WaterPulseTraceStore* pulseTraces)
    : config_(config),
      activeMeteringScheme_(activeScheme),
      water_(config_),
      localMode_(LocalUiMode::Normal),
      buttons_(),
      flow_(activeMeteringScheme_.params, config_.pulseMinIntervalUs),
      valve_(config_.valveFullPowerSec, config_.valveHoldDutyPercent),
      statistics_(statistics),
      filters_(filters),
      records_(records),
      meteringSnapshots_(&meteringSnapshots),
      meteringSchemes_(&meteringSchemes),
      pulseTraces_(pulseTraces),
      activeTraceId_(0),
      activeTraceStartUs_(0),
      lastFlowVolumeMl_(0),
      activeStartTimeSec_(0),
      activeStartTimeSynced_(false),
      activeStartBootId_(0),
      lastValveDesiredOpen_(false),
      calibrationValveOpen_(false),
      lastRecordWriteOk_(true),
      persistenceDirty_(false),
      configDirty_(false),
      factoryResetRequested_(false),
      pendingBeep_(BeepPattern::None),
      flowDroppedPulses_(0),
      resultDisplayStartMs_(0),
      adjustmentStepMl_(config_.volumeAdjustStepMl),
      timeAdjustmentStepSec_(config_.timeAdjustStepSec),
      lastResultRecordValid_(false),
      lastResultRecord_{},
      resultOkHoldTracking_(false),
      resultOkHoldTriggered_(false),
      resultOkHoldStartMs_(0),
      calibrationActualMl_(0),
      calibrationStepMl_(100),
      calibrationIgnoreOkUntilReleased_(false) {
    sanitizeConfig(config_);
    if (!activeMeteringScheme_.valid || !validMeteringSchemeParameters(activeMeteringScheme_.params)) {
        activeMeteringScheme_ = schemeFromLegacyConfig(config_);
        flow_.setMeteringParameters(activeMeteringScheme_.params);
    }
}

void AppController::resetInputs(ButtonLevels levels, std::uint32_t nowMs) {
    buttons_.reset(levels, nowMs);
}

void AppController::onFlowPulse(std::uint32_t nowUs) {
    if (pulseTraces_ && activeTraceId_ != 0) {
        pulseTraces_->appendRawEdge(activeTraceId_, elapsedSince(nowUs, activeTraceStartUs_));
    }
    flow_.onPulse(nowUs);
}

void AppController::tick(const AppTickInput& input) {
    const ButtonEvent event = buttons_.update(input.buttons, input.nowMs);
    updateResultCalibrationHold(input.buttons.okPressed, input.nowMs);
    handleButtonEvent(event, input.nowMs, input.nowUs, input.nowSeconds, input.timeSynced, input.bootId);
    if (localMode_ == LocalUiMode::Calibration && !input.buttons.okPressed) {
        calibrationIgnoreOkUntilReleased_ = false;
    }
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
        processResult(resultStartTime,
                      input.periodKeys,
                      input.periodKeysValid,
                      resultStartSynced,
                      resultBootId,
                      input.timeSynced ? input.nowSeconds : 0,
                      flow,
                      input.nowUs);
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
    snapshot.localMode = localMode_;
    snapshot.adjustmentStepMl = adjustmentStepMl_;
    snapshot.timeAdjustmentStepSec = timeAdjustmentStepSec_;
    snapshot.calibrationActualMl = calibrationActualMl_;
    snapshot.calibrationStepMl = calibrationStepMl_;
    snapshot.calibrationReady = lastResultRecordValid_;
    snapshot.pulsePerLiter = activeMeteringScheme_.params.stablePulsePerLiter;
    snapshot.meteringParams = activeMeteringScheme_.params;
    snapshot.flowDroppedPulses = flowDroppedPulses_;
    return snapshot;
}

bool AppController::lastRecordWriteOk() const {
    return lastRecordWriteOk_;
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

const MeteringSchemeRecord& AppController::activeMeteringScheme() const {
    return activeMeteringScheme_;
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

bool AppController::selectNextPresetForWeb() {
    return water_.selectNextPreset();
}

bool AppController::selectPreviousPresetForWeb() {
    return water_.selectPreviousPreset();
}

bool AppController::selectPresetForWeb(std::size_t index) {
    return water_.selectPreset(index);
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
    flow_.setPulseFilterUs(config_.pulseMinIntervalUs);
    valve_.configure(config_.valveFullPowerSec, config_.valveHoldDutyPercent);
    adjustmentStepMl_ = config_.volumeAdjustStepMl;
    timeAdjustmentStepSec_ = config_.timeAdjustStepSec;
    if (pulseTraces_) {
        pulseTraces_->setRecentTraceLimit(config_.recentPulseTraceCount);
    }
    return true;
}

bool AppController::applyActiveMeteringScheme(const MeteringSchemeRecord& activeScheme) {
    if (!canApplyConfig() || !activeScheme.valid || !activeScheme.enabled ||
        !validMeteringSchemeParameters(activeScheme.params)) {
        return false;
    }
    activeMeteringScheme_ = activeScheme;
    flow_.setMeteringParameters(activeMeteringScheme_.params);
    return true;
}

CalibrationApplyResult AppController::applyCalibrationFromRecord(const WaterRecord& record, std::uint32_t actualMl) {
    return applyCalibrationFromRecordInternal(record, actualMl, false);
}

CalibrationApplyResult AppController::applyCalibrationFromRecordInternal(const WaterRecord& record,
                                                                         std::uint32_t actualMl,
                                                                         bool allowLocalCalibration) {
    if (water_.snapshot().state != WaterState::Idle ||
        (localMode_ == LocalUiMode::Calibration && !allowLocalCalibration)) {
        return CalibrationApplyResult::NotAvailable;
    }
    if (actualMl < kCalibrationMinActualMl || actualMl > kMaxVolumePresetMl) {
        return CalibrationApplyResult::InvalidActual;
    }
    if (record.pulseCount == 0 || !waterResultAllowsCalibration(record.result)) {
        return CalibrationApplyResult::InvalidRecord;
    }
    pendingBeep_ = BeepPattern::Done;
    localMode_ = LocalUiMode::Result;
    return CalibrationApplyResult::Saved;
}

void AppController::handleButtonEvent(ButtonEvent event,
                                      std::uint32_t nowMs,
                                      std::uint32_t nowUs,
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
                if (calibrationIgnoreOkUntilReleased_) {
                    break;
                }
                saveLocalCalibration();
                break;
            case ButtonEventType::OkLong:
                if (calibrationIgnoreOkUntilReleased_) {
                    break;
                }
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
                    adjustmentStepMl_ = config_.volumeAdjustStepMl;
                    timeAdjustmentStepSec_ = config_.timeAdjustStepSec;
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if (water.state == WaterState::Confirm) {
                startSelectedPreset(nowMs, nowUs, nowSeconds, timeSynced, bootId);
            } else if (water.state == WaterState::Running || water.state == WaterState::Paused) {
                if (water_.togglePause(nowMs)) {
                    if (pulseTraces_ && activeTraceId_ != 0) {
                        const std::uint32_t elapsedUs = elapsedSince(nowUs, activeTraceStartUs_);
                        if (water.state == WaterState::Running) {
                            pulseTraces_->markPaused(activeTraceId_, elapsedUs);
                        } else if (water.state == WaterState::Paused) {
                            pulseTraces_->markResumedAfterPause(activeTraceId_, elapsedUs);
                        }
                    }
                    pendingBeep_ = BeepPattern::Click;
                }
            }
            break;
        case ButtonEventType::OkLong:
            break;
        case ButtonEventType::PlusShort:
        case ButtonEventType::PlusLong:
            if (water.state == WaterState::Confirm) {
                const std::uint32_t step =
                    water.mode == WaterMode::Time ? timeAdjustmentStepSec_ : adjustmentStepMl_;
                if (water_.adjustTarget(static_cast<std::int32_t>(step))) {
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if ((water.state == WaterState::Idle || water.state == WaterState::Error) &&
                       water_.selectNextPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        case ButtonEventType::MinusShort:
        case ButtonEventType::MinusLong:
            if (water.state == WaterState::Confirm) {
                const std::uint32_t step =
                    water.mode == WaterMode::Time ? timeAdjustmentStepSec_ : adjustmentStepMl_;
                if (water_.adjustTarget(-static_cast<std::int32_t>(step))) {
                    pendingBeep_ = BeepPattern::Click;
                }
            } else if ((water.state == WaterState::Idle || water.state == WaterState::Error) &&
                       water_.selectPreviousPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            break;
        default:
            break;
    }
}

void AppController::startSelectedPreset(std::uint32_t nowMs,
                                        std::uint32_t nowUs,
                                        std::uint32_t nowSeconds,
                                        bool timeSynced,
                                        std::uint32_t bootId) {
    if (water_.confirmStart(nowMs)) {
        activeStartTimeSec_ = nowSeconds;
        activeStartTimeSynced_ = timeSynced;
        activeStartBootId_ = timeSynced ? 0 : bootId;
        flow_.reset();
        lastFlowVolumeMl_ = 0;
        if (pulseTraces_) {
            pulseTraces_->setRecentTraceLimit(config_.recentPulseTraceCount);
            activeTraceId_ = pulseTraces_->beginTrace(nowSeconds, config_.pulseMinIntervalUs);
            activeTraceStartUs_ = nowUs;
        }
        pendingBeep_ = BeepPattern::Click;
    }
}

void AppController::exitResultDisplay(std::uint32_t) {
    localMode_ = LocalUiMode::Normal;
    resultOkHoldTracking_ = false;
    resultOkHoldTriggered_ = false;
}

void AppController::toggleCalibrationStep() {
    calibrationStepMl_ = calibrationStepMl_ == 100 ? 10 : 100;
}

void AppController::enterCalibrationFromResult(std::uint32_t) {
    if (!lastResultRecordValid_ || !waterResultAllowsCalibration(lastResultRecord_.result) ||
        lastResultRecord_.pulseCount == 0) {
        pendingBeep_ = BeepPattern::Error;
        return;
    }
    calibrationActualMl_ = lastResultRecord_.volumeMl;
    if (calibrationActualMl_ < kCalibrationMinActualMl) {
        calibrationActualMl_ = 1000;
    }
    if (calibrationActualMl_ > kMaxVolumePresetMl) {
        calibrationActualMl_ = kMaxVolumePresetMl;
    }
    calibrationStepMl_ = 100;
    localMode_ = LocalUiMode::Calibration;
    calibrationIgnoreOkUntilReleased_ = true;
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
            : next > static_cast<std::int64_t>(kMaxVolumePresetMl) ? kMaxVolumePresetMl : static_cast<std::uint32_t>(next);
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
    const CalibrationApplyResult result = applyCalibrationFromRecordInternal(lastResultRecord_, calibrationActualMl_, true);
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

void AppController::finishPulseTrace(const WaterRecord& record,
                                     WaterPulseTraceState finalState,
                                     const FlowSnapshot& flow,
                                     std::uint32_t nowUs) {
    if (!pulseTraces_ || activeTraceId_ == 0) {
        return;
    }
    (void)flow;
    pulseTraces_->finishTrace(activeTraceId_, record, finalState, elapsedSince(nowUs, activeTraceStartUs_));
    activeTraceId_ = 0;
    activeTraceStartUs_ = 0;
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
                                  std::uint32_t nowSeconds,
                                  const FlowSnapshot& flow,
                                  std::uint32_t nowUs) {
    const WaterTaskResult result = water_.result();
    if (!result.valid) {
        return;
    }

    WaterRecord record{
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
        static_cast<float>(activeMeteringScheme_.params.stablePulsePerLiter) / 1000.0f,
        {0, 0, 0, 0},
    };
    if (!startTimeSynced) {
        markWaterRecordBootId(record, bootId);
    }

    const WaterPulseTraceState traceState = traceStateForResult(result.result);
    finishPulseTrace(record, traceState, flow, nowUs);

    lastResultRecord_ = record;
    lastResultRecordValid_ = record.pulseCount > 0 && waterResultAllowsCalibration(record.result);
    lastRecordWriteOk_ = records_.append(record);
    if (lastRecordWriteOk_ && meteringSnapshots_) {
        WaterRecordMeteringSnapshot snapshot = makeWaterRecordMeteringSnapshot(record);
        snapshot.meteringSchemeId = activeMeteringScheme_.id;
        snapshot.meteringSchemeRevision = activeMeteringScheme_.revision;
        snapshot.params = activeMeteringScheme_.params;
        if (!meteringSnapshots_->upsert(snapshot)) {
            lastRecordWriteOk_ = false;
            if (meteringSchemes_) {
                meteringSchemes_->markUsageStatsDirty(activeMeteringScheme_.id);
            }
        } else if (meteringSchemes_ &&
                   !meteringSchemes_->incrementUsageAfterRecordWrite(activeMeteringScheme_.id, nowSeconds)) {
            lastRecordWriteOk_ = false;
        }
    }
    if (periodKeysValid) {
        statistics_.addWater(result.volumeMl, periodKeys);
    }
    filters_.addWater(result.volumeMl);
    persistenceDirty_ = true;
    pendingBeep_ = result.result == WaterResult::Completed ? BeepPattern::Done : BeepPattern::Error;
}

}  // namespace faucet
