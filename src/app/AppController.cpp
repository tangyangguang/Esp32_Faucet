#include "app/AppController.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <cstdio>

#ifndef NATIVE_BUILD
#include <Esp32Base.h>
#define APP_RESULT_LOG_I(tag, fmt, ...) ESP32BASE_LOG_I(tag, fmt, ##__VA_ARGS__)
#else
#define APP_RESULT_LOG_I(tag, fmt, ...) \
    do {                                \
    } while (0)
#endif

namespace faucet {
namespace {

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

bool isCancelButtonEvent(ButtonEventType type) {
    return type == ButtonEventType::CancelDown || type == ButtonEventType::CancelShort ||
           type == ButtonEventType::CancelLong;
}

MeteringSchemeRecord defaultRuntimeMeteringScheme() {
    MeteringSchemeRecord scheme{};
    scheme.id = 1;
    scheme.recordUsed = true;
    std::snprintf(scheme.name, sizeof(scheme.name), "默认计量方案");
    scheme.params = defaultMeteringParameters();
    scheme.sourceType = MeteringSchemeSource::Default;
    return scheme;
}


}  // namespace

AppController::AppController(const SystemConfig& config,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             WaterPulseTraceStore* pulseTraces,
                             CalibrationSessionFileStore* calibrationSessions,
                             CalibrationSessionTraceStore* calibrationSessionTraces,
                             WaterSensorManager* waterSensors)
    : AppController(config,
                    defaultRuntimeMeteringScheme(),
                    statistics,
                    filters,
                    records,
                    nullptr,
                    pulseTraces,
                    calibrationSessions,
                    calibrationSessionTraces,
                    waterSensors) {}

AppController::AppController(const SystemConfig& config,
                             const MeteringSchemeRecord& activeScheme,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             MeteringSchemeStore& meteringSchemes,
                             WaterPulseTraceStore* pulseTraces,
                             CalibrationSessionFileStore* calibrationSessions,
                             CalibrationSessionTraceStore* calibrationSessionTraces,
                             WaterSensorManager* waterSensors)
    : AppController(config,
                    activeScheme,
                    statistics,
                    filters,
                    records,
                    &meteringSchemes,
                    pulseTraces,
                    calibrationSessions,
                    calibrationSessionTraces,
                    waterSensors) {}

AppController::AppController(const SystemConfig& config,
                             const MeteringSchemeRecord& activeScheme,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             MeteringSchemeStore* meteringSchemes,
                             WaterPulseTraceStore* pulseTraces,
                             CalibrationSessionFileStore* calibrationSessions,
                             CalibrationSessionTraceStore* calibrationSessionTraces,
                             WaterSensorManager* waterSensors)
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
      meteringSchemes_(meteringSchemes),
      pulseTraces_(pulseTraces),
      waterSensors_(waterSensors),
      calibrationSessions_(calibrationSessions),
      calibrationSessionTraces_(calibrationSessionTraces),
      calibrationSession_{},
      activeTraceId_(0),
      activeTraceStartUs_(0),
      lastFlowVolumeMl_(0),
      currentFlowMlPerMin_(0),
      instantFlowMlPerMin_(0),
      windowFlowMlPerMin_(0),
      displayFlowMlPerMin_(0),
      runAverageFlowMlPerMin_(0),
      activeStartTimeSec_(0),
      activeStartTimeSynced_(false),
      activeStartBootId_(0),
      lastValveDesiredOpen_(false),
      valveOutputSink_(nullptr),
      persistenceDirty_(false),
      configDirty_(false),
      pendingBeep_(BeepPattern::None),
      flowDroppedPulses_(0),
      resultDisplayStartMs_(0),
      adjustmentStepMl_(config_.volumeAdjustStepMl),
      timeAdjustmentStepSec_(config_.timeAdjustStepSec),
      lastResultRecordValid_(false),
      lastResultRecord_{} {
    sanitizeConfig(config_);
    if (meteringSchemes_ &&
        (!activeMeteringScheme_.recordUsed || !validMeteringSchemeParameters(activeMeteringScheme_.params))) {
        activeMeteringScheme_ = defaultRuntimeMeteringScheme();
        flow_.setMeteringParameters(activeMeteringScheme_.params);
    }
    restoreCalibrationSession();
}

void AppController::resetInputs(ButtonLevels levels, std::uint32_t nowMs) {
    buttons_.reset(levels, nowMs);
}

void AppController::onFlowPulse(std::uint32_t nowUs) {
    if (pulseTraces_ && activeTraceId_ != 0) {
        pulseTraces_->appendPulseEdge(activeTraceId_, elapsedSince(nowUs, activeTraceStartUs_));
    }
    flow_.onPulse(nowUs);
}

void AppController::tick(const AppTickInput& input) {
    if (waterSensors_) {
        waterSensors_->tick(input.nowMs);
    }
    ButtonEvent event = buttons_.update(input.buttons, input.nowMs);
    if (input.buttons.cancelPressed && event.type != ButtonEventType::None && !isCancelButtonEvent(event.type)) {
        event = {ButtonEventType::CancelDown, ButtonId::Cancel};
    }
    if (input.timeSynced && waterSensors_) {
        waterSensors_->expireTdsCalibrationSession(input.nowSeconds);
    }
    handleButtonEvent(event, input.nowMs, input.nowUs, input.nowSeconds, input.timeSynced, input.bootId);
    if (localMode_ == LocalUiMode::Result && config_.resultDisplaySec > 0 &&
        elapsedAtLeast(input.nowMs, resultDisplayStartMs_, secondsToMillis(config_.resultDisplaySec))) {
        localMode_ = LocalUiMode::Normal;
    }

    syncFlow(input.nowUs);
    const FlowSnapshot flow = flow_.snapshot(input.nowUs);
    const bool wasRunningForSensorRun = water_.snapshot().state == WaterState::Running;
    currentFlowMlPerMin_ = flow.currentFlowMlPerMin;
    instantFlowMlPerMin_ = flow.instantFlowMlPerMin;
    windowFlowMlPerMin_ = flow.windowFlowMlPerMin;
    displayFlowMlPerMin_ = flow.displayFlowMlPerMin;
    water_.tick(input.nowMs, flow.windowFlowMlPerMin);
    if (waterSensors_ && wasRunningForSensorRun) {
        waterSensors_->sampleRun();
    }
    const WaterSnapshot waterAfterTick = water_.snapshot();
    runAverageFlowMlPerMin_ =
        waterAfterTick.elapsedSec == 0
            ? 0
            : static_cast<std::uint32_t>(
                  (static_cast<std::uint64_t>(waterAfterTick.volumeMl) * 60ULL + waterAfterTick.elapsedSec / 2ULL) /
                  waterAfterTick.elapsedSec);
    syncValve(input.nowMs);

    if (water_.hasResult()) {
        const bool calibrationRunResult =
            localMode_ == LocalUiMode::Calibration && calibrationSession_.status == CalibrationSessionStatus::Running;
        if (!calibrationRunResult && config_.resultDisplaySec > 0) {
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
    snapshot.lastResultRecordAvailable = lastResultRecordValid_;
    snapshot.lastResultRecord = lastResultRecord_;
    snapshot.calibrationStatus = calibrationSession_.status;
    snapshot.calibrationAttemptCount = calibrationSession_.attemptCount;
    snapshot.calibrationValidSampleCount = calibrationSession_.validSampleCount;
    snapshot.calibrationMaxRunSec = config_.maxOutTimeSec;
    snapshot.calibrationCanQuickGenerate = calibrationCanQuickGenerate(calibrationSession_);
    snapshot.calibrationRecommended = calibrationIsRecommended(calibrationSession_);
    snapshot.calibrationCandidate = calibrationCandidate_;
    bool foundCalibrationActual = false;
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        if (attempt.status != CalibrationAttemptStatus::Valid || attempt.actualMl == 0) {
            continue;
        }
        if (!foundCalibrationActual) {
            snapshot.calibrationMinActualMl = attempt.actualMl;
            snapshot.calibrationMaxActualMl = attempt.actualMl;
            foundCalibrationActual = true;
        } else {
            snapshot.calibrationMinActualMl = std::min(snapshot.calibrationMinActualMl, attempt.actualMl);
            snapshot.calibrationMaxActualMl = std::max(snapshot.calibrationMaxActualMl, attempt.actualMl);
        }
    }
    snapshot.pulsePerLiter = activeMeteringScheme_.params.stablePulsePerLiter;
    snapshot.meteringParams = activeMeteringScheme_.params;
    snapshot.currentFlowMlPerMin = currentFlowMlPerMin_;
    snapshot.instantFlowMlPerMin = instantFlowMlPerMin_;
    snapshot.windowFlowMlPerMin = windowFlowMlPerMin_;
    snapshot.displayFlowMlPerMin = displayFlowMlPerMin_;
    snapshot.runAverageFlowMlPerMin = runAverageFlowMlPerMin_;
    snapshot.flowDroppedPulses = flowDroppedPulses_;
    if (waterSensors_) {
        snapshot.sensors = waterSensors_->snapshot();
    }
    snapshot.temperatureSensorEnabled = config_.temperatureEnabled;
    snapshot.tdsSensorEnabled = config_.tdsEnabled;
    return snapshot;
}

bool AppController::consumePersistenceDirty() {
    const bool dirty = persistenceDirty_;
    persistenceDirty_ = false;
    return dirty;
}

void AppController::markPersistenceDirtyForRetry() {
    persistenceDirty_ = true;
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

BeepPattern AppController::consumeBeepPattern() {
    const BeepPattern pattern = pendingBeep_;
    pendingBeep_ = BeepPattern::None;
    return pattern;
}

bool AppController::emergencyStop(std::uint32_t nowMs) {
    const WaterState state = water_.snapshot().state;
    if (state == WaterState::Running || state == WaterState::Paused) {
        water_.stop(nowMs);
        syncValve(nowMs);
        pendingBeep_ = BeepPattern::Error;
        return true;
    }
    return false;
}

void AppController::setValveOutputSink(ValveOutputSink sink) {
    valveOutputSink_ = sink;
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
        pulseTraces_->setRecentTraceLimit(kRecentPulseTraceCount);
    }
    if (waterSensors_) {
        waterSensors_->configure(config_);
    }
    return true;
}

bool AppController::applyActiveMeteringScheme(const MeteringSchemeRecord& activeScheme) {
    if (!canApplyConfig() || !activeScheme.recordUsed ||
        !validMeteringSchemeParameters(activeScheme.params)) {
        return false;
    }
    activeMeteringScheme_ = activeScheme;
    flow_.setMeteringParameters(activeMeteringScheme_.params);
    return true;
}

void AppController::handleButtonEvent(ButtonEvent event,
                                      std::uint32_t nowMs,
                                      std::uint32_t nowUs,
                                      std::uint32_t nowSeconds,
                                      bool timeSynced,
                                      std::uint32_t bootId) {
    if (localMode_ == LocalUiMode::Calibration && event.type != ButtonEventType::None) {
        if (calibrationSession_.status == CalibrationSessionStatus::Preparing &&
            event.type == ButtonEventType::OkShort) {
            calibrationSession_.status = CalibrationSessionStatus::WaitingLocalRun;
            calibrationSession_.updatedAt = nowSeconds;
            saveCalibrationSession();
            pendingBeep_ = BeepPattern::Click;
            return;
        }
        if ((calibrationSession_.status == CalibrationSessionStatus::WaitingLocalRun ||
             calibrationSession_.status == CalibrationSessionStatus::ReadyToGenerate ||
             calibrationSession_.status == CalibrationSessionStatus::Generated) &&
            event.type == ButtonEventType::OkShort) {
            if (!beginCalibrationLocalRun(nowMs, nowUs, nowSeconds, timeSynced, bootId)) {
                pendingBeep_ = BeepPattern::Error;
            }
            return;
        }
        if (calibrationSession_.status == CalibrationSessionStatus::Running) {
            if (event.type == ButtonEventType::CancelDown || event.type == ButtonEventType::CancelShort ||
                event.type == ButtonEventType::CancelLong) {
                emergencyStop(nowMs);
                return;
            }
            if (event.type == ButtonEventType::OkShort) {
                pendingBeep_ = BeepPattern::Error;
                return;
            }
        }
        if (calibrationSession_.status == CalibrationSessionStatus::AwaitingActual &&
            (event.type == ButtonEventType::CancelShort || event.type == ButtonEventType::CancelLong)) {
            pendingBeep_ = BeepPattern::Error;
            return;
        }
        switch (event.type) {
            case ButtonEventType::CancelDown:
            case ButtonEventType::CancelShort:
            case ButtonEventType::CancelLong:
                discardCalibrationSessionForWeb(nowSeconds);
                pendingBeep_ = BeepPattern::Click;
                break;
            case ButtonEventType::OkShort:
                break;
            case ButtonEventType::OkLong:
                break;
            case ButtonEventType::PlusShort:
            case ButtonEventType::PlusLong:
                break;
            case ButtonEventType::MinusShort:
            case ButtonEventType::MinusLong:
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
            localMode_ = LocalUiMode::Normal;
            if (water_.selectNextPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            return;
        }
        if (event.type == ButtonEventType::MinusShort || event.type == ButtonEventType::MinusLong) {
            localMode_ = LocalUiMode::Normal;
            if (water_.selectPreviousPreset()) {
                pendingBeep_ = BeepPattern::Click;
            }
            return;
        }
        localMode_ = LocalUiMode::Normal;
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
        resetRunFlowState();
        pendingBeep_ = BeepPattern::Click;
    }
}

void AppController::resetRunFlowState() {
    flow_.reset();
    lastFlowVolumeMl_ = 0;
    currentFlowMlPerMin_ = 0;
    instantFlowMlPerMin_ = 0;
    windowFlowMlPerMin_ = 0;
    displayFlowMlPerMin_ = 0;
    runAverageFlowMlPerMin_ = 0;
    if (waterSensors_) {
        waterSensors_->beginRun();
    }
}

void AppController::syncFlow(std::uint32_t nowUs) {
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
                                     std::uint32_t nowUs) {
    if (!pulseTraces_ || activeTraceId_ == 0) {
        return;
    }
    pulseTraces_->finishTrace(activeTraceId_, record, finalState, elapsedSince(nowUs, activeTraceStartUs_));
    activeTraceId_ = 0;
    activeTraceStartUs_ = 0;
}

void AppController::syncValve(std::uint32_t nowMs) {
    const bool desiredOpen = water_.snapshot().valveOpen;
    if (desiredOpen && !lastValveDesiredOpen_) {
        valve_.open(nowMs);
    } else if (!desiredOpen && lastValveDesiredOpen_) {
        valve_.close();
    }
    valve_.tick(nowMs);
    lastValveDesiredOpen_ = desiredOpen;
    if (valveOutputSink_) {
        valveOutputSink_(valve_.output());
    }
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

    WaterRecord record{};
    record.startTime = startTime;
    record.volumeMl = result.volumeMl;
    record.targetValue = result.targetValue;
    record.pulseCount = flow.pulseCount;
    record.filteredPulseCount = flow.rejectedPulses;
    record.meteringSchemeId = activeMeteringScheme_.id;
    record.durationSec = result.durationSec;
    record.mode = result.mode;
    record.result = result.result;
    record.selectedPreset = result.selectedPreset;
    if (!startTimeSynced) {
        markWaterRecordBootId(record, bootId);
    }
    if (waterSensors_) {
        const WaterSensorRunSummary sensors = waterSensors_->finishRun();
        record.temperatureCentiC = sensors.temperatureCentiC;
        record.tdsPpm = sensors.tdsPpm;
        record.sensorSampleCount = sensors.sensorSampleCount;
        record.sensorFlags = sensors.sensorFlags;
    }

    APP_RESULT_LOG_I("app",
                     "water_result_ready result=%u volume_ml=%lu target=%lu pulses=%lu scheme_id=%lu",
                     static_cast<unsigned>(result.result),
                     static_cast<unsigned long>(record.volumeMl),
                     static_cast<unsigned long>(record.targetValue),
                     static_cast<unsigned long>(record.pulseCount),
                     static_cast<unsigned long>(record.meteringSchemeId));
    const WaterPulseTraceState traceState = traceStateForResult(result.result);
    finishPulseTrace(record, traceState, nowUs);

    lastResultRecord_ = WaterRecord{};
    lastResultRecordValid_ = false;
    APP_RESULT_LOG_I("app", "water_record_append_begin");
    const bool recordWriteOk = records_.append(record);
    APP_RESULT_LOG_I("app", "water_record_append_done ok=%s", recordWriteOk ? "yes" : "no");
    if (recordWriteOk && record.pulseCount > 0 && waterResultAllowsCalibration(record.result)) {
        lastResultRecord_ = record;
        lastResultRecordValid_ = true;
    }
    if (periodKeysValid) {
        statistics_.addWater(result.volumeMl, periodKeys);
    }
    filters_.addWater(result.volumeMl);
    APP_RESULT_LOG_I("app",
                     "water_runtime_totals_updated period_keys=%s today_ml=%lu total_ml=%lu",
                     periodKeysValid ? "valid" : "invalid",
                     static_cast<unsigned long>(statistics_.record().todayMl),
                     static_cast<unsigned long>(statistics_.record().totalMl));
    persistCalibrationPendingAttempt(record, nowSeconds);
    persistenceDirty_ = true;
    pendingBeep_ = result.result == WaterResult::Completed ? BeepPattern::Done : BeepPattern::Error;
}

}  // namespace faucet
