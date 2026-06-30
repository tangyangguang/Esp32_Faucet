#include "app/AppController.h"

#include "app/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <memory>
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
    initializeManualMeteringScheme(scheme, 1, "默认计量方案", defaultMeteringParameters(), 0);
    return scheme;
}

CalibrationSampleSummary makeCalibrationSummary(const WaterPulseTrace& trace,
                                                const WaterPulseTraceBucketSample* buckets,
                                                std::size_t bucketCount,
                                                std::uint32_t actualMl,
                                                const SegmentedCalibrationOptions& options) {
    CalibrationSampleSummary summary{};
    summary.actualMl = actualMl;
    summary.totalPulses = trace.totalPulses;
    summary.rejectedPulses = trace.minIntervalFilteredCount;
    summary.durationSec = trace.record.durationSec;
    summary.truncated = (trace.flags & (kPulseTraceFlagBucketOverflow | kPulseTraceFlagStartupOverflow)) != 0;
    if (!buckets || bucketCount == 0 || summary.truncated || actualMl < kCalibrationMinActualMl ||
        trace.totalPulses == 0) {
        return summary;
    }
    const std::uint32_t durationSec =
        trace.record.durationSec > 0
            ? trace.record.durationSec
            : static_cast<std::uint32_t>((bucketCount * kPulseTraceBucketMs + 999UL) / 1000UL);
    summary.durationSec = durationSec;
    if (durationSec == 0) {
        return summary;
    }
    std::uint32_t* perSecond = new (std::nothrow) std::uint32_t[durationSec]{};
    if (!perSecond) {
        return summary;
    }
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t sec = static_cast<std::uint32_t>((i * kPulseTraceBucketMs) / 1000UL);
        if (sec < durationSec) {
            perSecond[sec] += buckets[i].pulseCount;
        }
    }
    const std::uint32_t stableWindowSec =
        std::min<std::uint32_t>(std::max<std::uint32_t>(options.stableWindowSec, kMinCalibrationStableWindowSec),
                                kMaxCalibrationStableWindowSec);
    const std::uint32_t stableTolerancePercent = std::min<std::uint32_t>(
        std::max<std::uint32_t>(options.stableTolerancePercent, kMinCalibrationStableTolerancePercent),
        kMaxCalibrationStableTolerancePercent);
    if (durationSec >= 6 && stableWindowSec <= durationSec) {
        std::uint32_t runningCount = 0;
        std::uint32_t runningTotal = 0;
        for (std::uint32_t i = durationSec / 2; i < durationSec; ++i) {
            if (perSecond[i] > 0) {
                ++runningCount;
                runningTotal += perSecond[i];
            }
        }
        if (runningCount >= 3) {
            const float stableRate = static_cast<float>(runningTotal) / static_cast<float>(runningCount);
            const std::uint32_t stableFloor = static_cast<std::uint32_t>(stableRate + 0.5f);
            for (std::uint32_t i = 0; stableFloor > 0 && i + stableWindowSec <= durationSec; ++i) {
                if (perSecond[i] < stableFloor) {
                    continue;
                }
                std::uint32_t total = 0;
                std::uint32_t minValue = UINT32_MAX;
                std::uint32_t maxValue = 0;
                for (std::uint32_t j = 0; j < stableWindowSec; ++j) {
                    const std::uint32_t value = perSecond[i + j];
                    total += value;
                    minValue = std::min(minValue, value);
                    maxValue = std::max(maxValue, value);
                }
                if (minValue == 0) {
                    continue;
                }
                const float avg = static_cast<float>(total) / static_cast<float>(stableWindowSec);
                const float avgTolerance = stableRate * static_cast<float>(stableTolerancePercent) / 100.0f;
                const float spreadTolerance =
                    stableRate * static_cast<float>(stableTolerancePercent) * 1.6f / 100.0f;
                if (std::fabs(avg - stableRate) <= std::max(1.0f, avgTolerance) &&
                    static_cast<float>(maxValue - minValue) <= std::max(1.0f, spreadTolerance)) {
                    summary.stable = true;
                    summary.stableStartSec = i;
                    for (std::uint32_t k = 0; k < i; ++k) {
                        summary.startupPulseCount += perSecond[k];
                    }
                    std::uint32_t stableSeconds = 0;
                    for (std::uint32_t k = i; k < durationSec; ++k) {
                        summary.stablePulseCount += perSecond[k];
                        ++stableSeconds;
                    }
                    summary.stablePulsePerSec =
                        stableSeconds == 0
                            ? 0.0f
                            : static_cast<float>(summary.stablePulseCount) / static_cast<float>(stableSeconds);
                    break;
                }
            }
        }
    }
    if (!summary.stable && durationSec < kMinCalibrationStableWindowSec) {
        summary.stable = true;
        summary.stablePulseCount = trace.totalPulses;
        summary.stablePulsePerSec = static_cast<float>(trace.totalPulses) / static_cast<float>(durationSec);
    }
    summary.usableForGeneration = summary.stable && summary.stablePulseCount > 0;
    delete[] perSecond;
    return summary;
}

bool appendSummaryCalibrationSample(const CalibrationAttempt& attempt,
                                    SegmentedCalibrationSample* samples,
                                    std::size_t sampleCapacity,
                                    std::size_t& sampleCount) {
    if (!samples || sampleCount >= sampleCapacity ||
        attempt.status != CalibrationAttemptStatus::Valid || !attempt.summary.usableForGeneration ||
        attempt.summary.actualMl == 0 || attempt.summary.totalPulses == 0 ||
        attempt.summary.truncated || !attempt.summary.stable || attempt.summary.stablePulseCount == 0) {
        return false;
    }
    samples[sampleCount] = SegmentedCalibrationSample{
        attempt.summary.actualMl,
        attempt.summary.totalPulses,
        attempt.summary.startupPulseCount,
        attempt.summary.stablePulseCount,
        attempt.summary.stableStartSec,
        attempt.summary.stablePulsePerSec,
    };
    ++sampleCount;
    return true;
}

void rejectCalibrationAttempt(CalibrationSessionRecord& session,
                              CalibrationAttempt& attempt,
                              CalibrationInvalidReason reason,
                              std::uint32_t nowSeconds) {
    attempt.status = CalibrationAttemptStatus::Invalid;
    attempt.invalidReason = reason;
    session.validSampleCount = countValidCalibrationSamples(session);
    session.status = calibrationCanStartAttempt(session) ? CalibrationSessionStatus::WaitingLocalRun
                                                        : CalibrationSessionStatus::Failed;
    session.updatedAt = nowSeconds;
}

void fillCandidateFromSegmentedResult(MeteringSchemeCandidate& candidate,
                                      const SegmentedCalibrationResult& result,
                                      std::uint32_t nowSeconds,
                                      MeteringSchemeSource sourceType) {
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
                             WaterSensorManager* waterSensors)
    : AppController(config,
                    defaultRuntimeMeteringScheme(),
                    statistics,
                    filters,
                    records,
                    nullptr,
                    pulseTraces,
                    recordCalibrations,
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
                             WaterRecordCalibrationWriter* recordCalibrations,
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
                    recordCalibrations,
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
                             WaterRecordCalibrationWriter* recordCalibrations,
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
      recordCalibrations_(recordCalibrations),
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
    if (!calibrationSessionTraces_->clearForNewSession()) {
        return false;
    }
    initializeCalibrationSessionRecord(calibrationSession_, sessionId, nowSeconds);
    calibrationCandidate_ = MeteringSchemeCandidate{};
    calibrationSession_.status = CalibrationSessionStatus::WaitingLocalRun;
    calibrationSession_.updatedAt = nowSeconds;
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
        calibrationSession_.attemptCount == 0 || actualMl < kCalibrationMinActualMl || !calibrationSessionTraces_) {
        return false;
    }
    CalibrationAttempt& attempt = calibrationSession_.attempts[calibrationSession_.attemptCount - 1];
    const WaterPulseTrace* trace = pulseTraces_ ? pulseTraces_->findByRecord(attempt.record) : nullptr;
    if (attempt.status != CalibrationAttemptStatus::PendingActual || !trace || trace->totalPulses == 0 ||
        trace->bucketCount == 0 ||
        (trace->flags & (kPulseTraceFlagBucketOverflow | kPulseTraceFlagStartupOverflow)) != 0) {
        rejectCalibrationAttempt(calibrationSession_, attempt, CalibrationInvalidReason::AnalysisFailed, nowSeconds);
        saveCalibrationSession();
        return false;
    }
    std::unique_ptr<WaterPulseTraceBucketSample[]> buckets;
    std::unique_ptr<WaterPulseTraceSample[]> startupEdges;
    buckets.reset(new (std::nothrow) WaterPulseTraceBucketSample[trace->bucketCount]{});
    if (!buckets) {
        rejectCalibrationAttempt(calibrationSession_, attempt, CalibrationInvalidReason::StorageFailed, nowSeconds);
        saveCalibrationSession();
        return false;
    }
    for (std::size_t i = 0; i < trace->bucketCount; ++i) {
        const WaterPulseTraceBucketSample* bucket = pulseTraces_->bucketAt(*trace, i);
        if (bucket) {
            buckets[i] = *bucket;
        }
    }
    if (trace->startupEdgeCount > 0) {
        startupEdges.reset(new (std::nothrow) WaterPulseTraceSample[trace->startupEdgeCount]{});
        if (!startupEdges) {
            rejectCalibrationAttempt(calibrationSession_, attempt, CalibrationInvalidReason::StorageFailed, nowSeconds);
            saveCalibrationSession();
            return false;
        }
        for (std::size_t i = 0; i < trace->startupEdgeCount; ++i) {
            const WaterPulseTraceSample* edge = pulseTraces_->startupEdgeAt(*trace, i);
            if (edge) {
                startupEdges[i] = *edge;
            }
        }
    }
    const CalibrationSampleSummary summary =
        makeCalibrationSummary(*trace,
                               buckets.get(),
                               trace->bucketCount,
                               actualMl,
                               segmentedCalibrationOptionsFromConfig(config_));
    CalibrationStoredTrace stored{};
    stored.sessionId = calibrationSession_.sessionId;
    stored.attemptIndex = attempt.attemptIndex;
    stored.trace = *trace;

    const bool savedTrace = calibrationSessionTraces_->saveValid(attempt.sessionTraceSlot,
                                                                 stored,
                                                                 buckets.get(),
                                                                 trace->bucketCount,
                                                                 startupEdges.get(),
                                                                 trace->startupEdgeCount,
                                                                 actualMl,
                                                                 nowSeconds);
    if (!savedTrace) {
        rejectCalibrationAttempt(calibrationSession_, attempt, CalibrationInvalidReason::StorageFailed, nowSeconds);
        saveCalibrationSession();
        return false;
    }
    if (recordCalibrations_) {
        WaterRecordCalibration calibration = makeWaterRecordCalibration(attempt.record);
        calibration.actualMl = actualMl;
        recordCalibrations_->upsert(calibration);
    }

    attempt.actualMl = actualMl;
    attempt.summary = summary;
    attempt.status = CalibrationAttemptStatus::Valid;
    attempt.invalidReason = CalibrationInvalidReason::None;
    calibrationSession_.validSampleCount = countValidCalibrationSamples(calibrationSession_);
    calibrationSession_.updatedAt = nowSeconds;
    if (pulseTraces_) {
        pulseTraces_->setActualMlByRecord(attempt.record, actualMl);
    }
    pendingBeep_ = BeepPattern::Done;
    if (calibrationCanQuickGenerate(calibrationSession_)) {
        return refreshCalibrationCandidate(nowSeconds);
    }
    clearCalibrationCandidate();
    calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                : CalibrationSessionStatus::Failed;
    return saveCalibrationSession();
}

bool AppController::removeCalibrationSessionSampleForWeb(std::uint8_t attemptIndex, std::uint32_t nowSeconds) {
    if (water_.snapshot().state != WaterState::Idle) {
        return false;
    }

    std::uint8_t selectedIndex = kCalibrationMaxAttempts;
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        if (attempt.attemptIndex == attemptIndex) {
            selectedIndex = i;
            break;
        }
    }
    if (selectedIndex >= kCalibrationMaxAttempts) {
        return false;
    }
    CalibrationSessionRecord nextSession = calibrationSession_;
    CalibrationAttempt& nextAttempt = nextSession.attempts[selectedIndex];
    if (nextAttempt.status != CalibrationAttemptStatus::Valid &&
        nextAttempt.status != CalibrationAttemptStatus::PendingActual) {
        return false;
    }
    nextAttempt.status = CalibrationAttemptStatus::Removed;
    nextAttempt.invalidReason = CalibrationInvalidReason::None;
    nextSession.validSampleCount = countValidCalibrationSamples(nextSession);
    nextSession.updatedAt = nowSeconds;
    const bool canQuickGenerateAfterRemove = calibrationCanQuickGenerate(nextSession);
    nextSession.status = canQuickGenerateAfterRemove ? CalibrationSessionStatus::ReadyToGenerate
                                                     : (calibrationCanStartAttempt(nextSession)
                                                            ? CalibrationSessionStatus::WaitingLocalRun
                                                            : CalibrationSessionStatus::Failed);

    if (!calibrationSessions_ || !calibrationSessions_->ready() || !calibrationSessions_->save(nextSession)) {
        return false;
    }

    calibrationSession_ = nextSession;
    clearCalibrationCandidate();
    if (canQuickGenerateAfterRemove) {
        return refreshCalibrationCandidate(nowSeconds);
    }
    return true;
}

bool AppController::skipCalibrationAttemptForWeb(std::uint32_t nowSeconds) {
    if (calibrationSession_.status != CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.attemptCount == 0) {
        return false;
    }
    CalibrationAttempt& attempt = calibrationSession_.attempts[calibrationSession_.attemptCount - 1];
    if (calibrationSessionTraces_ && attempt.sessionTraceSlot < kCalibrationSessionTraceSlots) {
        calibrationSessionTraces_->invalidate(attempt.sessionTraceSlot);
    }
    attempt.status = CalibrationAttemptStatus::Skipped;
    calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                : CalibrationSessionStatus::Failed;
    calibrationSession_.updatedAt = nowSeconds;
    return saveCalibrationSession();
}

bool AppController::generateCalibrationForWeb(std::uint32_t nowSeconds) {
    return refreshCalibrationCandidate(nowSeconds) &&
           calibrationSession_.status == CalibrationSessionStatus::Generated &&
           calibrationCandidate_.ready;
}

bool AppController::refreshCalibrationCandidate(std::uint32_t nowSeconds) {
    clearCalibrationCandidate();
    calibrationSession_.validSampleCount = countValidCalibrationSamples(calibrationSession_);
    const bool canQuickGenerate = calibrationCanQuickGenerate(calibrationSession_);
    const bool statusAllowsGenerate =
        calibrationSession_.status == CalibrationSessionStatus::WaitingLocalRun ||
        calibrationSession_.status == CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.status == CalibrationSessionStatus::ReadyToGenerate ||
        calibrationSession_.status == CalibrationSessionStatus::Generated;
    if (calibrationSession_.sessionId == 0 || !statusAllowsGenerate) {
        return false;
    }
    if (!meteringSchemes_ || !meteringSchemes_->ready() || !canQuickGenerate ||
        water_.snapshot().state != WaterState::Idle) {
        calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                    : CalibrationSessionStatus::Failed;
        calibrationSession_.updatedAt = nowSeconds;
        return saveCalibrationSession();
    }

    std::unique_ptr<SegmentedCalibrationSample[]> samples(
        new (std::nothrow) SegmentedCalibrationSample[kCalibrationMaxValidSamples]{});
    if (!samples) {
        return false;
    }
    std::size_t sampleCount = 0;
    const SegmentedCalibrationOptions options = segmentedCalibrationOptionsFromConfig(config_);
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        appendSummaryCalibrationSample(attempt, samples.get(), kCalibrationMaxValidSamples, sampleCount);
    }

    SegmentedCalibrationResult result{};
    if (!computeSegmentedCalibration(samples.get(), sampleCount, options, result) || !result.valid) {
        calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                    : CalibrationSessionStatus::Failed;
        calibrationSession_.updatedAt = nowSeconds;
        return saveCalibrationSession();
    }
    MeteringSchemeCandidate candidate{};
    fillCandidateFromSegmentedResult(candidate,
                                     result,
                                     nowSeconds,
                                     MeteringSchemeSource::CalibrationSession);
    calibrationCandidate_ = candidate;
    calibrationSession_.status = CalibrationSessionStatus::Generated;
    calibrationSession_.updatedAt = nowSeconds;
    pendingBeep_ = BeepPattern::Done;
    return saveCalibrationSession();
}

void AppController::clearCalibrationCandidate() {
    calibrationCandidate_ = MeteringSchemeCandidate{};
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
        !meteringSchemes_->setActiveScheme(newId)) {
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

bool AppController::startTdsCalibrationSessionForWeb(std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->startTdsCalibrationSession(nowSeconds);
}

bool AppController::startTdsCalibrationPointForWeb(std::uint16_t referencePpm, std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->startTdsCalibrationPoint(referencePpm, nowSeconds);
}

bool AppController::saveTdsCalibrationPointForWeb(std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->saveStableTdsCalibrationPoint(nowSeconds);
}

bool AppController::removeTdsCalibrationPointForWeb(std::uint8_t index, std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->removeTdsCalibrationPoint(index, nowSeconds);
}

bool AppController::discardTdsCalibrationForWeb() {
    return canUseTdsCalibration() && waterSensors_->discardTdsCalibrationSession();
}

bool AppController::applyTdsCalibrationForWeb(std::uint32_t nowSeconds) {
    if (!canUseTdsCalibration()) {
        return false;
    }
    SystemConfig updated = config_;
    if (!waterSensors_->applyReadyTdsCalibration(updated, nowSeconds)) {
        return false;
    }
    sanitizeConfig(updated);
    config_ = updated;
    water_.applyConfig(config_);
    waterSensors_->configure(config_);
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

void AppController::restoreCalibrationSession() {
    calibrationSession_ = CalibrationSessionRecord{};
    if (!calibrationSessions_ || !calibrationSessions_->ready()) {
        return;
    }
    if (!calibrationSessions_->load(calibrationSession_)) {
        return;
    }
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
    if (calibrationSession_.status == CalibrationSessionStatus::Generated &&
        calibrationCanQuickGenerate(calibrationSession_)) {
        const BeepPattern restoredBeep = pendingBeep_;
        refreshCalibrationCandidate(calibrationSession_.updatedAt);
        pendingBeep_ = restoredBeep;
    }
    if (calibrationSession_.status == CalibrationSessionStatus::Preparing ||
        calibrationSession_.status == CalibrationSessionStatus::WaitingLocalRun ||
        calibrationSession_.status == CalibrationSessionStatus::AwaitingActual ||
        calibrationSession_.status == CalibrationSessionStatus::ReadyToGenerate ||
        calibrationSession_.status == CalibrationSessionStatus::Generated) {
        localMode_ = LocalUiMode::Calibration;
    }
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
    if (trace && trace->bucketCount > 0) {
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
    if ((calibrationSession_.status != CalibrationSessionStatus::WaitingLocalRun &&
         calibrationSession_.status != CalibrationSessionStatus::ReadyToGenerate &&
         calibrationSession_.status != CalibrationSessionStatus::Generated) ||
        water_.snapshot().state != WaterState::Idle || !calibrationCanStartAttempt(calibrationSession_)) {
        return false;
    }
    if (!water_.startUntargeted(nowMs)) {
        return false;
    }
    clearCalibrationCandidate();
    calibrationSession_.status = CalibrationSessionStatus::Running;
    calibrationSession_.updatedAt = nowSeconds;
    saveCalibrationSession();
    activeStartTimeSec_ = nowSeconds;
    activeStartTimeSynced_ = timeSynced;
    activeStartBootId_ = timeSynced ? 0 : bootId;
    resetRunFlowState();
    if (pulseTraces_) {
        pulseTraces_->setRecentTraceLimit(kRecentPulseTraceCount);
        activeTraceId_ = pulseTraces_->beginTrace(nowSeconds, config_.pulseMinIntervalUs);
        activeTraceStartUs_ = nowUs;
    }
    pendingBeep_ = BeepPattern::Click;
    return true;
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

std::uint8_t AppController::selectCalibrationSessionTraceSlot() const {
    bool occupied[kCalibrationSessionTraceSlots]{};
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        if ((attempt.status == CalibrationAttemptStatus::Valid ||
             attempt.status == CalibrationAttemptStatus::PendingActual) &&
            attempt.sessionTraceSlot < kCalibrationSessionTraceSlots) {
            occupied[attempt.sessionTraceSlot] = true;
        }
    }
    for (std::uint8_t slot = 0; slot < kCalibrationSessionTraceSlots; ++slot) {
        if (!occupied[slot]) {
            return slot;
        }
    }
    return 255;
}

void AppController::persistCalibrationPendingAttempt(const WaterRecord& record, std::uint32_t nowSeconds) {
    if (localMode_ != LocalUiMode::Calibration ||
        calibrationSession_.status != CalibrationSessionStatus::Running ||
        !calibrationSessionTraces_ || !calibrationSessionTraces_->ready() ||
        calibrationSession_.attemptCount >= kCalibrationMaxAttempts) {
        return;
    }

    const std::uint8_t slot = selectCalibrationSessionTraceSlot();
    CalibrationAttempt attempt{};
    attempt.attemptIndex = calibrationSession_.attemptCount;
    attempt.sessionTraceSlot = slot;
    attempt.record = record;
    attempt.targetHintMl = record.targetValue;
    attempt.status = CalibrationAttemptStatus::PendingActual;

    if (slot >= kCalibrationSessionTraceSlots) {
        attempt.status = CalibrationAttemptStatus::Invalid;
        attempt.invalidReason = CalibrationInvalidReason::StorageFailed;
        appendCalibrationAttempt(calibrationSession_, attempt);
        calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_) ? CalibrationSessionStatus::WaitingLocalRun
                                                                                    : CalibrationSessionStatus::Failed;
        calibrationSession_.updatedAt = nowSeconds;
        saveCalibrationSession();
        return;
    }

    const WaterPulseTrace* trace = pulseTraces_ ? pulseTraces_->findByRecord(record) : nullptr;
    if (!trace || trace->bucketCount == 0 ||
        (trace->flags & (kPulseTraceFlagBucketOverflow | kPulseTraceFlagStartupOverflow)) != 0) {
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
    finishPulseTrace(record, traceState, nowUs);

    lastResultRecord_ = WaterRecord{};
    lastResultRecordValid_ = false;
    APP_RESULT_LOG_I("app", "water_record_append_begin");
    const bool recordWriteOk = records_.append(record);
    APP_RESULT_LOG_I("app", "water_record_append_done ok=%s", recordWriteOk ? "yes" : "no");
    if (recordWriteOk && recordCalibrations_ && record.pulseCount > 0 && waterResultAllowsCalibration(record.result)) {
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
