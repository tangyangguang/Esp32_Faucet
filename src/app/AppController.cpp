#include "app/AppController.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <new>

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

MeteringSchemeRecord defaultRuntimeMeteringScheme() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(scheme, 1, "默认计量方案", defaultMeteringParameters(), 0);
    return scheme;
}

bool appendSessionCalibrationSample(const CalibrationStoredTrace& stored,
                                    const CalibrationSessionTraceStore& traceStore,
                                    std::uint8_t slot,
                                    const SegmentedCalibrationOptions& options,
                                    SegmentedCalibrationSample* samples,
                                    std::size_t sampleCapacity,
                                    std::uint32_t* sourceIds,
                                    std::size_t& sampleCount) {
    if (!samples || !sourceIds || sampleCount >= sampleCapacity || !stored.valid || stored.pendingActual ||
        stored.actualMl == 0 || stored.trace.sampleCount < 6 || stored.trace.totalPulses == 0 ||
        stored.trace.truncated || stored.trace.resumedAfterPause) {
        return false;
    }
    WaterPulseTraceSample* traceSamples = new (std::nothrow) WaterPulseTraceSample[stored.trace.sampleCount]{};
    if (!traceSamples) {
        return false;
    }
    const std::size_t copied = traceStore.readSamples(slot, traceSamples, stored.trace.sampleCount);
    if (copied != stored.trace.sampleCount) {
        delete[] traceSamples;
        return false;
    }
    const WaterPulseTraceAnalysis analysis =
        analyzeWaterPulseTrace(stored.trace, traceSamples, stored.trace.sampleCount, options);
    delete[] traceSamples;
    if (!analysis.stable || analysis.stablePulseCount == 0) {
        return false;
    }
    samples[sampleCount] = SegmentedCalibrationSample{
        stored.actualMl,
        stored.trace.totalPulses,
        analysis.startupPulseCount,
        analysis.stablePulseCount,
        analysis.stableStartSec,
        analysis.stablePulsePerSec,
    };
    sourceIds[sampleCount] = stored.trace.traceId == 0 ? static_cast<std::uint32_t>(slot + 1) : stored.trace.traceId;
    ++sampleCount;
    return true;
}

bool calibrationStatusExpiresWhenIdle(CalibrationSessionStatus status) {
    return status == CalibrationSessionStatus::Preparing ||
           status == CalibrationSessionStatus::WaitingLocalRun ||
           status == CalibrationSessionStatus::AwaitingActual ||
           status == CalibrationSessionStatus::ReadyToGenerate ||
           status == CalibrationSessionStatus::Generated;
}

void fillCandidateFromSegmentedResult(MeteringSchemeCandidate& candidate,
                                      const SegmentedCalibrationResult& result,
                                      const SegmentedCalibrationOptions& options,
                                      const std::uint32_t* sourceIds,
                                      std::size_t sourceCount,
                                      std::uint32_t nowSeconds,
                                      MeteringSchemeSource sourceType) {
    (void)options;
    (void)sourceIds;
    (void)sourceCount;
    candidate = MeteringSchemeCandidate{};
    candidate.ready = true;
    candidate.sourceType = sourceType;
    candidate.params = MeteringParameters{
        result.startupPulseCount,
        result.startupVolumeMl,
        result.stablePulsePerLiter,
        result.startupDurationMs,
        result.stableFlowMlPerMin,
    };
    candidate.generatedAt = nowSeconds;
    candidate.sampleCount = result.sampleCount;
    candidate.minActualMl = result.minActualMl;
    candidate.maxActualMl = result.maxActualMl;
    candidate.maxErrorMl = result.maxErrorMl;
    candidate.maxErrorTenthPercent = result.maxRelativeErrorTenthPercent;
}

}  // namespace

AppController::AppController(const SystemConfig& config,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             WaterPulseTraceStore* pulseTraces,
                             WaterRecordCalibrationWriter* recordCalibrations,
                             CalibrationSessionFileStore* calibrationSessions,
                             CalibrationSessionTraceStore* calibrationSessionTraces,
                             CalibrationLongTermSampleStore* calibrationLongTermSamples,
                             WaterSensorManager* waterSensors)
    : config_(config),
      activeMeteringScheme_(defaultRuntimeMeteringScheme()),
      water_(config_),
      localMode_(LocalUiMode::Normal),
      buttons_(),
      flow_(activeMeteringScheme_.params, config_.pulseMinIntervalUs),
      valve_(config_.valveFullPowerSec, config_.valveHoldDutyPercent),
      statistics_(statistics),
      filters_(filters),
      records_(records),
      recordCalibrations_(recordCalibrations),
      meteringSchemes_(nullptr),
      pulseTraces_(pulseTraces),
      waterSensors_(waterSensors),
      calibrationSessions_(calibrationSessions),
      calibrationSessionTraces_(calibrationSessionTraces),
      calibrationLongTermSamples_(calibrationLongTermSamples),
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
      calibrationValveOpen_(false),
      lastRecordWriteOk_(true),
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
    restoreCalibrationSession();
}

AppController::AppController(const SystemConfig& config,
                             const MeteringSchemeRecord& activeScheme,
                             StatisticsStore& statistics,
                             FilterStore& filters,
                             WaterRecordWriter& records,
                             MeteringSchemeStore& meteringSchemes,
                             WaterPulseTraceStore* pulseTraces,
                             WaterRecordCalibrationWriter* recordCalibrations,
                             CalibrationSessionFileStore* calibrationSessions,
                             CalibrationSessionTraceStore* calibrationSessionTraces,
                             CalibrationLongTermSampleStore* calibrationLongTermSamples,
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
      recordCalibrations_(recordCalibrations),
      meteringSchemes_(&meteringSchemes),
      pulseTraces_(pulseTraces),
      waterSensors_(waterSensors),
      calibrationSessions_(calibrationSessions),
      calibrationSessionTraces_(calibrationSessionTraces),
      calibrationLongTermSamples_(calibrationLongTermSamples),
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
      calibrationValveOpen_(false),
      lastRecordWriteOk_(true),
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
    if (!activeMeteringScheme_.recordUsed || !validMeteringSchemeParameters(activeMeteringScheme_.params)) {
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
        pulseTraces_->appendRawEdge(activeTraceId_, elapsedSince(nowUs, activeTraceStartUs_));
    }
    flow_.onPulse(nowUs);
}

void AppController::tick(const AppTickInput& input) {
    if (waterSensors_) {
        waterSensors_->tick(input.nowMs);
    }
    const ButtonEvent event = buttons_.update(input.buttons, input.nowMs);
    expireIdleCalibrationSession(input.timeSynced ? input.nowSeconds : 0);
    handleButtonEvent(event, input.nowMs, input.nowUs, input.nowSeconds, input.timeSynced, input.bootId);
    if (localMode_ == LocalUiMode::Result && config_.resultDisplaySec > 0 &&
        elapsedAtLeast(input.nowMs, resultDisplayStartMs_, msFromSeconds(config_.resultDisplaySec))) {
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
    snapshot.calibrationReady = lastResultRecordValid_;
    snapshot.calibrationStatus = calibrationSession_.status;
    snapshot.calibrationIdleExpiresAt =
        calibrationStatusExpiresWhenIdle(calibrationSession_.status) && calibrationSession_.updatedAt > 0
            ? calibrationSession_.updatedAt + kCalibrationIdleTimeoutSec
            : 0;
    snapshot.calibrationAttemptCount = calibrationSession_.attemptCount;
    snapshot.calibrationValidSampleCount = calibrationSession_.validSampleCount;
    snapshot.calibrationCanQuickGenerate = calibrationCanQuickGenerate(calibrationSession_);
    snapshot.calibrationRecommended = calibrationIsRecommended(calibrationSession_);
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
    snapshot.lcdSensorPageEnabled = config_.lcdSensorPageEnabled;
    snapshot.temperatureSensorEnabled = config_.temperatureEnabled;
    snapshot.tdsSensorEnabled = config_.tdsEnabled;
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
    water_.stop(nowMs);
    syncValve(nowMs);
    if (state == WaterState::Running || state == WaterState::Paused) {
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
        activeScheme.deleted ||
        !validMeteringSchemeParameters(activeScheme.params)) {
        return false;
    }
    activeMeteringScheme_ = activeScheme;
    flow_.setMeteringParameters(activeMeteringScheme_.params);
    return true;
}

bool AppController::startCalibrationSessionForWeb(std::uint32_t nowSeconds) {
    if (water_.snapshot().state != WaterState::Idle || !calibrationSessions_ || !calibrationSessions_->ready() ||
        !calibrationSessionTraces_ || !calibrationSessionTraces_->ready() ||
        (calibrationSession_.status != CalibrationSessionStatus::Idle &&
         calibrationSession_.status != CalibrationSessionStatus::Applied &&
         calibrationSession_.status != CalibrationSessionStatus::Discarded &&
         calibrationSession_.status != CalibrationSessionStatus::Failed)) {
        return false;
    }
    const std::uint32_t sessionId = nowSeconds == 0 ? 1 : nowSeconds;
    if (!calibrationSessionTraces_->clearForNewSession(sessionId)) {
        return false;
    }
    calibrationSession_ = makeCalibrationSession(sessionId, nowSeconds);
    calibrationCandidate_ = MeteringSchemeCandidate{};
    if (!saveCalibrationSession()) {
        calibrationSession_ = CalibrationSessionRecord{};
        return false;
    }
    localMode_ = LocalUiMode::Calibration;
    pendingBeep_ = BeepPattern::Click;
    return true;
}

bool AppController::discardCalibrationSessionForWeb(std::uint32_t nowSeconds) {
    if (localMode_ != LocalUiMode::Calibration || calibrationSession_.status == CalibrationSessionStatus::Running ||
        water_.snapshot().state == WaterState::Running || water_.snapshot().state == WaterState::Paused) {
        return false;
    }
    calibrationSession_.status = CalibrationSessionStatus::Discarded;
    calibrationCandidate_ = MeteringSchemeCandidate{};
    calibrationSession_.updatedAt = nowSeconds;
    const bool ok = saveCalibrationSession();
    localMode_ = LocalUiMode::Normal;
    return ok;
}

bool AppController::submitCalibrationActualForWeb(std::uint32_t actualMl, std::uint32_t nowSeconds) {
    if (calibrationSession_.status != CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.attemptCount == 0 || actualMl < kCalibrationMinActualMl || !recordCalibrations_ ||
        !calibrationSessionTraces_) {
        return false;
    }
    CalibrationAttempt& attempt = calibrationSession_.attempts[calibrationSession_.attemptCount - 1];
    const WaterPulseTrace* trace = pulseTraces_ ? pulseTraces_->findByRecord(attempt.record) : nullptr;
    if (attempt.status != CalibrationAttemptStatus::PendingActual || !trace || trace->truncated ||
        trace->resumedAfterPause || trace->sampleCount == 0 ||
        trace->sampleCount > kPulseTraceMaxRawEdgesPerTrace || trace->totalPulses == 0) {
        attempt.status = CalibrationAttemptStatus::Invalid;
        attempt.invalidReason = CalibrationInvalidReason::AnalysisFailed;
        calibrationSession_.updatedAt = nowSeconds;
        saveCalibrationSession();
        return false;
    }
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{};
    if (!samples) {
        attempt.status = CalibrationAttemptStatus::Invalid;
        attempt.invalidReason = CalibrationInvalidReason::StorageFailed;
        calibrationSession_.updatedAt = nowSeconds;
        saveCalibrationSession();
        return false;
    }
    for (std::size_t i = 0; i < trace->sampleCount; ++i) {
        const WaterPulseTraceSample* sample = pulseTraces_->sampleAt(*trace, i);
        if (sample) {
            samples[i] = *sample;
        }
    }
    CalibrationStoredTrace stored{};
    stored.pendingActual = true;
    stored.sessionId = calibrationSession_.sessionId;
    stored.attemptIndex = attempt.attemptIndex;
    stored.trace = *trace;

    WaterRecordCalibration calibration = makeWaterRecordCalibration(attempt.record);
    calibration.actualMl = actualMl;
    calibration.calibratedAt = nowSeconds;
    calibration.oldPulsePerMl = static_cast<float>(activeMeteringScheme_.params.stablePulsePerLiter) / 1000.0f;
    calibration.newPulsePerMl = calibration.oldPulsePerMl;
    calibration.oldStartupCompensationMl = activeMeteringScheme_.params.startupVolumeMl;
    calibration.newStartupCompensationMl = activeMeteringScheme_.params.startupVolumeMl;
    calibration.kind = WaterRecordCalibrationKind::PulsePerMl;
    if (!recordCalibrations_->upsert(calibration) ||
        !calibrationSessionTraces_->savePending(attempt.sessionTraceSlot, stored, samples, trace->sampleCount) ||
        !calibrationSessionTraces_->commitValid(attempt.sessionTraceSlot, actualMl, nowSeconds)) {
        delete[] samples;
        attempt.status = CalibrationAttemptStatus::Invalid;
        attempt.invalidReason = CalibrationInvalidReason::StorageFailed;
        calibrationSession_.updatedAt = nowSeconds;
        saveCalibrationSession();
        return false;
    }
    delete[] samples;

    attempt.actualMl = actualMl;
    attempt.status = CalibrationAttemptStatus::Valid;
    attempt.invalidReason = CalibrationInvalidReason::None;
    calibrationSession_.validSampleCount = countValidCalibrationSamples(calibrationSession_);
    calibrationSession_.status = calibrationCanQuickGenerate(calibrationSession_) ? CalibrationSessionStatus::ReadyToGenerate
                                                                                  : CalibrationSessionStatus::WaitingLocalRun;
    calibrationSession_.updatedAt = nowSeconds;
    if (pulseTraces_) {
        pulseTraces_->setActualMlByRecord(attempt.record, actualMl);
    }
    pendingBeep_ = BeepPattern::Done;
    return saveCalibrationSession();
}

bool AppController::saveCalibrationSessionSampleToLongTermForWeb(std::uint8_t attemptIndex,
                                                                 std::uint32_t nowSeconds,
                                                                 std::uint32_t& sampleId) {
    sampleId = 0;
    if (!calibrationLongTermSamples_ || !calibrationLongTermSamples_->ready() || !calibrationSessionTraces_ ||
        !calibrationSessionTraces_->ready() || water_.snapshot().state != WaterState::Idle) {
        return false;
    }

    const CalibrationAttempt* selected = nullptr;
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        if (attempt.attemptIndex == attemptIndex) {
            selected = &attempt;
            break;
        }
    }
    if (!selected || selected->status != CalibrationAttemptStatus::Valid ||
        selected->sessionTraceSlot >= kCalibrationSessionTraceSlots) {
        return false;
    }

    CalibrationStoredTrace existing[kCalibrationLongTermSampleSlots]{};
    const std::size_t existingCount =
        calibrationLongTermSamples_->list(existing, kCalibrationLongTermSampleSlots);
    for (std::size_t i = 0; i < existingCount; ++i) {
        if (existing[i].sessionId == calibrationSession_.sessionId &&
            existing[i].attemptIndex == selected->attemptIndex) {
            sampleId = existing[i].sampleId;
            calibrationSession_.updatedAt = nowSeconds;
            return saveCalibrationSession();
        }
    }

    CalibrationStoredTrace stored{};
    if (!calibrationSessionTraces_->load(selected->sessionTraceSlot, stored) || !stored.valid ||
        stored.pendingActual || stored.actualMl == 0 || stored.trace.sampleCount == 0 ||
        stored.trace.sampleCount > kPulseTraceMaxRawEdgesPerTrace) {
        return false;
    }

    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[stored.trace.sampleCount]{};
    if (!samples) {
        return false;
    }
    const std::size_t copied =
        calibrationSessionTraces_->readSamples(selected->sessionTraceSlot, samples, stored.trace.sampleCount);
    if (copied != stored.trace.sampleCount) {
        delete[] samples;
        return false;
    }
    stored.sessionId = calibrationSession_.sessionId;
    stored.attemptIndex = selected->attemptIndex;
    stored.savedAt = nowSeconds;
    const bool saved = calibrationLongTermSamples_->save(stored, samples, copied, sampleId);
    delete[] samples;
    if (!saved) {
        return false;
    }
    calibrationSession_.updatedAt = nowSeconds;
    pendingBeep_ = BeepPattern::Done;
    return saveCalibrationSession();
}

bool AppController::skipCalibrationAttemptForWeb(CalibrationSkipReason reason, std::uint32_t nowSeconds) {
    if (calibrationSession_.status != CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.attemptCount == 0) {
        return false;
    }
    CalibrationAttempt& attempt = calibrationSession_.attempts[calibrationSession_.attemptCount - 1];
    if (calibrationSessionTraces_ && attempt.sessionTraceSlot < kCalibrationSessionTraceSlots) {
        calibrationSessionTraces_->invalidate(attempt.sessionTraceSlot);
    }
    attempt.status = CalibrationAttemptStatus::Skipped;
    attempt.skipReason = reason;
    calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                : CalibrationSessionStatus::Failed;
    calibrationSession_.updatedAt = nowSeconds;
    return saveCalibrationSession();
}

bool AppController::generateCalibrationForWeb(std::uint32_t nowSeconds) {
    if (!meteringSchemes_ || !meteringSchemes_->ready() || !calibrationSessionTraces_ ||
        !calibrationSessionTraces_->ready() || !calibrationCanQuickGenerate(calibrationSession_) ||
        water_.snapshot().state != WaterState::Idle) {
        return false;
    }

    SegmentedCalibrationSample samples[kCalibrationMaxValidSamples]{};
    std::uint32_t sourceIds[kCalibrationMaxValidSamples]{};
    std::size_t sampleCount = 0;
    const SegmentedCalibrationOptions options = segmentedCalibrationOptionsFromConfig(config_);
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        if (attempt.status != CalibrationAttemptStatus::Valid ||
            attempt.sessionTraceSlot >= kCalibrationSessionTraceSlots) {
            continue;
        }
        CalibrationStoredTrace stored{};
        if (!calibrationSessionTraces_->load(attempt.sessionTraceSlot, stored)) {
            continue;
        }
        appendSessionCalibrationSample(stored,
                                       *calibrationSessionTraces_,
                                       attempt.sessionTraceSlot,
                                       options,
                                       samples,
                                       kCalibrationMaxValidSamples,
                                       sourceIds,
                                       sampleCount);
    }

    SegmentedCalibrationResult result{};
    if (!computeSegmentedCalibration(samples, sampleCount, options, result) || !result.valid) {
        return false;
    }
    MeteringSchemeCandidate candidate{};
    fillCandidateFromSegmentedResult(candidate,
                                     result,
                                     options,
                                     sourceIds,
                                     sampleCount,
                                     nowSeconds,
                                     MeteringSchemeSource::CalibrationSession);
    calibrationCandidate_ = candidate;
    calibrationSession_.status = CalibrationSessionStatus::Generated;
    calibrationSession_.updatedAt = nowSeconds;
    pendingBeep_ = BeepPattern::Done;
    return saveCalibrationSession();
}

bool AppController::applyGeneratedCalibrationForWeb(std::uint32_t nowSeconds) {
    if (!meteringSchemes_ || !meteringSchemes_->ready() ||
        calibrationSession_.status != CalibrationSessionStatus::Generated ||
        water_.snapshot().state != WaterState::Idle) {
        return false;
    }
    if (!calibrationCandidate_.ready ||
        calibrationCandidate_.sourceType != MeteringSchemeSource::CalibrationSession) {
        return false;
    }
    std::uint32_t newId = 0;
    if (!meteringSchemes_->saveCandidateAsNew(calibrationCandidate_, "校准生成计量方案", nowSeconds, newId) ||
        !meteringSchemes_->setActiveScheme(newId, nowSeconds)) {
        return false;
    }
    calibrationCandidate_ = MeteringSchemeCandidate{};
    MeteringSchemeRecord active{};
    if (!meteringSchemes_->activeScheme(active) || !applyActiveMeteringScheme(active)) {
        return false;
    }
    calibrationSession_.status = CalibrationSessionStatus::Applied;
    calibrationSession_.appliedSchemeId = newId;
    calibrationSession_.updatedAt = nowSeconds;
    pendingBeep_ = BeepPattern::Done;
    return saveCalibrationSession();
}

bool AppController::canUseTdsCalibration() const {
    return waterSensors_ && localMode_ == LocalUiMode::Normal && water_.snapshot().state == WaterState::Idle;
}

bool AppController::startTdsSinglePointCalibrationForWeb(std::uint16_t referencePpm,
                                                         std::uint32_t nowSeconds) {
    return canUseTdsCalibration() &&
           waterSensors_->startTdsSinglePointCalibration(referencePpm, nowSeconds);
}

bool AppController::startTdsTwoPointLowCalibrationForWeb(std::uint16_t referencePpm,
                                                         std::uint32_t nowSeconds) {
    return canUseTdsCalibration() &&
           waterSensors_->startTdsTwoPointLow(referencePpm, nowSeconds);
}

bool AppController::startTdsTwoPointHighCalibrationForWeb(std::uint16_t referencePpm,
                                                          std::uint32_t nowSeconds) {
    return canUseTdsCalibration() &&
           waterSensors_->startTdsTwoPointHigh(referencePpm, nowSeconds);
}

bool AppController::cancelTdsCalibrationForWeb() {
    return canUseTdsCalibration() && waterSensors_->cancelTdsCalibration();
}

bool AppController::saveTdsCalibrationForWeb(std::uint32_t nowSeconds) {
    if (!canUseTdsCalibration()) {
        return false;
    }
    SystemConfig updated = config_;
    if (!waterSensors_->saveReadyTdsCalibration(updated, nowSeconds)) {
        return false;
    }
    sanitizeConfig(updated);
    config_ = updated;
    water_.applyConfig(config_);
    pendingBeep_ = BeepPattern::Done;
    return true;
}

bool AppController::saveTemperatureCalibrationForWeb(std::int16_t referenceCentiC) {
    if (!canApplyConfig() || !waterSensors_ || !config_.temperatureEnabled ||
        config_.temperatureKind != TemperatureKind::Ntc50kB3950) {
        return false;
    }
    const WaterSensorSnapshot sensors = waterSensors_->snapshot();
    if (!sensors.temperatureRawCentiC.valid) {
        return false;
    }
    config_.temperatureOffsetCentiC =
        static_cast<std::int16_t>(referenceCentiC - sensors.temperatureRawCentiC.value);
    config_.temperatureCalibrated = true;
    sanitizeConfig(config_);
    water_.applyConfig(config_);
    pendingBeep_ = BeepPattern::Done;
    return true;
}

TdsCalibrationSessionSnapshot AppController::tdsCalibrationSnapshot() const {
    return waterSensors_ ? waterSensors_->calibrationSnapshot() : TdsCalibrationSessionSnapshot{};
}

CalibrationApplyResult AppController::applyCalibrationFromRecord(const WaterRecord& record, std::uint32_t actualMl) {
    return applyCalibrationFromRecordInternal(record, actualMl, false, 0);
}

CalibrationApplyResult AppController::applyCalibrationFromRecordInternal(const WaterRecord& record,
                                                                         std::uint32_t actualMl,
                                                                         bool allowLocalCalibration,
                                                                         std::uint32_t calibratedAt) {
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
    if (!recordCalibrations_) {
        return CalibrationApplyResult::NotAvailable;
    }
    WaterRecordCalibration calibration = makeWaterRecordCalibration(record);
    calibration.actualMl = actualMl;
    calibration.calibratedAt = calibratedAt;
    const float stablePulsePerMl = static_cast<float>(activeMeteringScheme_.params.stablePulsePerLiter) / 1000.0f;
    calibration.oldPulsePerMl = stablePulsePerMl;
    calibration.newPulsePerMl = stablePulsePerMl;
    calibration.oldStartupCompensationMl = activeMeteringScheme_.params.startupVolumeMl;
    calibration.newStartupCompensationMl = activeMeteringScheme_.params.startupVolumeMl;
    calibration.kind = WaterRecordCalibrationKind::PulsePerMl;
    if (!recordCalibrations_->upsert(calibration)) {
        return CalibrationApplyResult::NotAvailable;
    }
    if (pulseTraces_) {
        pulseTraces_->setActualMlByRecord(record, actualMl);
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
        if (calibrationSession_.status == CalibrationSessionStatus::Preparing &&
            event.type == ButtonEventType::OkShort) {
            calibrationSession_.status = CalibrationSessionStatus::WaitingLocalRun;
            calibrationSession_.updatedAt = nowSeconds;
            saveCalibrationSession();
            pendingBeep_ = BeepPattern::Click;
            return;
        }
        if (calibrationSession_.status == CalibrationSessionStatus::WaitingLocalRun &&
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
                const WaterSnapshot water = water_.snapshot();
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
                return;
            }
        }
        if (calibrationSession_.status == CalibrationSessionStatus::AwaitingActual &&
            (event.type == ButtonEventType::CancelShort || event.type == ButtonEventType::CancelLong)) {
            skipCalibrationAttemptForWeb(CalibrationSkipReason::Mistake, nowSeconds);
            pendingBeep_ = BeepPattern::Click;
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
        currentFlowMlPerMin_ = 0;
        instantFlowMlPerMin_ = 0;
        windowFlowMlPerMin_ = 0;
        displayFlowMlPerMin_ = 0;
        runAverageFlowMlPerMin_ = 0;
        if (waterSensors_) {
            waterSensors_->beginRun();
        }
        pendingBeep_ = BeepPattern::Click;
    }
}

void AppController::exitResultDisplay(std::uint32_t) {
    localMode_ = LocalUiMode::Normal;
}

void AppController::restoreCalibrationSession() {
    calibrationSession_ = CalibrationSessionRecord{};
    if (!calibrationSessions_ || !calibrationSessions_->ready()) {
        return;
    }
    CalibrationSessionRecord loaded{};
    if (!calibrationSessions_->load(loaded)) {
        return;
    }
    calibrationSession_ = loaded;
    if (calibrationSession_.status == CalibrationSessionStatus::Running) {
        if (calibrationSession_.attemptCount > 0) {
            CalibrationAttempt& attempt = calibrationSession_.attempts[calibrationSession_.attemptCount - 1];
            attempt.status = CalibrationAttemptStatus::Invalid;
            attempt.invalidReason = CalibrationInvalidReason::ErrorResult;
            if (calibrationSessionTraces_ && attempt.sessionTraceSlot < kCalibrationSessionTraceSlots) {
                calibrationSessionTraces_->invalidate(attempt.sessionTraceSlot);
            }
        }
        calibrationSession_.status = CalibrationSessionStatus::WaitingLocalRun;
        saveCalibrationSession();
    }
    invalidateAwaitingActualIfRamTraceMissing(0);
    if (calibrationSession_.status == CalibrationSessionStatus::Preparing ||
        calibrationSession_.status == CalibrationSessionStatus::WaitingLocalRun ||
        calibrationSession_.status == CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.status == CalibrationSessionStatus::ReadyToGenerate ||
        calibrationSession_.status == CalibrationSessionStatus::Generated) {
        localMode_ = LocalUiMode::Calibration;
    }
}

void AppController::expireIdleCalibrationSession(std::uint32_t nowSeconds) {
    if (nowSeconds == 0 || localMode_ != LocalUiMode::Calibration ||
        !calibrationStatusExpiresWhenIdle(calibrationSession_.status) ||
        calibrationSession_.updatedAt == 0 || nowSeconds < calibrationSession_.updatedAt ||
        nowSeconds - calibrationSession_.updatedAt < kCalibrationIdleTimeoutSec) {
        return;
    }
    calibrationSession_.status = CalibrationSessionStatus::Discarded;
    calibrationCandidate_ = MeteringSchemeCandidate{};
    calibrationSession_.updatedAt = nowSeconds;
    saveCalibrationSession();
    localMode_ = LocalUiMode::Normal;
}

bool AppController::invalidateAwaitingActualIfRamTraceMissing(std::uint32_t nowSeconds) {
    if (calibrationSession_.status != CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.attemptCount == 0) {
        return false;
    }
    CalibrationAttempt& attempt = calibrationSession_.attempts[calibrationSession_.attemptCount - 1];
    if (attempt.status != CalibrationAttemptStatus::PendingActual) {
        return false;
    }
    const WaterPulseTrace* trace = pulseTraces_ ? pulseTraces_->findByRecord(attempt.record) : nullptr;
    if (trace && trace->sampleCount > 0) {
        return false;
    }
    attempt.status = CalibrationAttemptStatus::Invalid;
    attempt.invalidReason = CalibrationInvalidReason::MissingActualMl;
    calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                : CalibrationSessionStatus::Failed;
    if (nowSeconds != 0) {
        calibrationSession_.updatedAt = nowSeconds;
    }
    saveCalibrationSession();
    return true;
}

bool AppController::saveCalibrationSession() {
    return calibrationSessions_ && calibrationSessions_->ready() && calibrationSessions_->save(calibrationSession_);
}

bool AppController::beginCalibrationLocalRun(std::uint32_t nowMs,
                                             std::uint32_t nowUs,
                                             std::uint32_t nowSeconds,
                                             bool timeSynced,
                                             std::uint32_t bootId) {
    if (calibrationSession_.status != CalibrationSessionStatus::WaitingLocalRun ||
        water_.snapshot().state != WaterState::Idle || !calibrationCanStartAttempt(calibrationSession_)) {
        return false;
    }
    if (!water_.requestStart(nowMs) || !water_.confirmStart(nowMs)) {
        return false;
    }
    calibrationSession_.status = CalibrationSessionStatus::Running;
    calibrationSession_.updatedAt = nowSeconds;
    saveCalibrationSession();
    activeStartTimeSec_ = nowSeconds;
    activeStartTimeSynced_ = timeSynced;
    activeStartBootId_ = timeSynced ? 0 : bootId;
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
    if (pulseTraces_) {
        pulseTraces_->setRecentTraceLimit(kRecentPulseTraceCount);
        activeTraceId_ = pulseTraces_->beginTrace(nowSeconds, config_.pulseMinIntervalUs);
        activeTraceStartUs_ = nowUs;
    }
    pendingBeep_ = BeepPattern::Click;
    return true;
}

void AppController::persistCalibrationPendingAttempt(const WaterRecord& record, std::uint32_t nowSeconds) {
    if (localMode_ != LocalUiMode::Calibration ||
        calibrationSession_.status != CalibrationSessionStatus::Running ||
        !calibrationSessionTraces_ || !calibrationSessionTraces_->ready() ||
        calibrationSession_.attemptCount >= kCalibrationMaxAttempts) {
        return;
    }

    const std::uint8_t slot = calibrationSession_.validSampleCount;
    CalibrationAttempt attempt{};
    attempt.attemptIndex = calibrationSession_.attemptCount;
    attempt.sessionTraceSlot = slot;
    attempt.record = record;
    attempt.targetHintMl = record.targetValue;
    attempt.status = CalibrationAttemptStatus::PendingActual;

    const WaterPulseTrace* trace = pulseTraces_ ? pulseTraces_->findByRecord(record) : nullptr;
    if (!trace || trace->sampleCount == 0 || trace->sampleCount > kPulseTraceMaxRawEdgesPerTrace) {
        attempt.status = CalibrationAttemptStatus::Invalid;
        attempt.invalidReason = CalibrationInvalidReason::TruncatedTrace;
        appendCalibrationAttempt(calibrationSession_, attempt);
        calibrationSession_.status = CalibrationSessionStatus::WaitingLocalRun;
        calibrationSession_.updatedAt = nowSeconds;
        saveCalibrationSession();
        return;
    }

    appendCalibrationAttempt(calibrationSession_, attempt);
    calibrationSession_.status = CalibrationSessionStatus::AwaitingActual;
    calibrationSession_.updatedAt = nowSeconds;
    saveCalibrationSession();
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
        activeMeteringScheme_.id,
        {0, 0, 0, 0},
    };
    if (!startTimeSynced) {
        markWaterRecordBootId(record, bootId);
    }
    if (waterSensors_) {
        const WaterSensorRunSummary sensors = waterSensors_->finishRun();
        record.temperatureAvgCentiC = sensors.temperatureAvgCentiC;
        record.temperatureMinCentiC = sensors.temperatureMinCentiC;
        record.temperatureMaxCentiC = sensors.temperatureMaxCentiC;
        record.tdsAvgPpm = sensors.tdsAvgPpm;
        record.tdsMinPpm = sensors.tdsMinPpm;
        record.tdsMaxPpm = sensors.tdsMaxPpm;
        record.tdsVoltageAvgMv = sensors.tdsVoltageAvgMv;
        record.sensorSampleCount = sensors.sensorSampleCount;
        record.sensorFlags = sensors.sensorFlags;
        record.tdsCalibrationRevisionAtRun = sensors.tdsCalibrationRevisionAtRun;
        record.tdsCalibrationModeAtRun = sensors.tdsCalibrationModeAtRun;
        record.tdsCalibratedAtRun = sensors.tdsCalibratedAtRun;
        record.tdsTemperatureCompensatedAtRun = sensors.tdsTemperatureCompensatedAtRun;
        record.tdsTempFallback25CAtRun = sensors.tdsTempFallback25CAtRun;
    }

    APP_RESULT_LOG_I("app",
                     "water_result_ready result=%u volume_ml=%lu target=%lu pulses=%lu scheme_id=%lu",
                     static_cast<unsigned>(result.result),
                     static_cast<unsigned long>(record.volumeMl),
                     static_cast<unsigned long>(record.targetValue),
                     static_cast<unsigned long>(record.pulseCount),
                     static_cast<unsigned long>(record.meteringSchemeId));
    const WaterPulseTraceState traceState = traceStateForResult(result.result);
    finishPulseTrace(record, traceState, flow, nowUs);

    lastResultRecord_ = WaterRecord{};
    lastResultRecordValid_ = false;
    APP_RESULT_LOG_I("app", "water_record_append_begin");
    lastRecordWriteOk_ = records_.append(record);
    APP_RESULT_LOG_I("app", "water_record_append_done ok=%s", lastRecordWriteOk_ ? "yes" : "no");
    if (lastRecordWriteOk_ && meteringSchemes_ && !activeMeteringScheme_.usedEver) {
        activeMeteringScheme_.usedEver = true;
        meteringSchemes_->markUsedAfterRecordWrite(activeMeteringScheme_.id);
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
