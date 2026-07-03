#include "app/AppController.h"

#include "app/WaterPulseTraceAnalysis.h"

#include <memory>
#include <new>

namespace faucet {
namespace {

bool pulseBucketsCoverDuration(std::size_t bucketCount, std::uint32_t durationSec) {
    if (durationSec == 0) {
        return false;
    }
    const std::uint64_t coveredMs = static_cast<std::uint64_t>(bucketCount) * kPulseTraceBucketMs;
    const std::uint64_t durationMs = static_cast<std::uint64_t>(durationSec) * 1000ULL;
    return coveredMs >= durationMs;
}

CalibrationSampleSummary makeCalibrationSummary(const WaterPulseTrace& trace,
                                                const WaterPulseTraceBucketSample* buckets,
                                                std::size_t bucketCount,
                                                std::uint32_t actualMl,
                                                const SegmentedCalibrationOptions& options) {
    CalibrationSampleSummary summary{};
    summary.actualMl = actualMl;
    summary.totalPulses = trace.totalPulses;
    summary.filteredPulses = trace.minIntervalFilteredCount;
    summary.durationSec = trace.record.durationSec;
    summary.truncated = (trace.flags & kPulseTraceFlagStartupOverflow) != 0;
    if (!buckets || bucketCount == 0 || summary.truncated || actualMl < kCalibrationMinActualMl ||
        trace.totalPulses == 0) {
        return summary;
    }
    summary.durationSec =
        trace.record.durationSec > 0
            ? trace.record.durationSec
            : static_cast<std::uint32_t>((bucketCount * kPulseTraceBucketMs + 999UL) / 1000UL);
    const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(trace, buckets, bucketCount, options);
    summary.stable = analysis.stable;
    summary.startupPulseCount = analysis.startupPulseCount;
    summary.stablePulseCount = analysis.stablePulseCount;
    summary.stableStartSec = analysis.stableStartSec;
    summary.stablePulsePerSec = analysis.stablePulsePerSec;
    if (!summary.stable && summary.durationSec > 0 && summary.durationSec < kMinCalibrationStableWindowSec) {
        summary.stable = true;
        summary.stablePulseCount = trace.totalPulses;
        summary.stablePulsePerSec =
            static_cast<float>(trace.totalPulses) / static_cast<float>(summary.durationSec);
    }
    const bool bucketDataComplete =
        (trace.flags & kPulseTraceFlagBucketOverflow) == 0 ||
        pulseBucketsCoverDuration(bucketCount, summary.durationSec);
    summary.usableForGeneration = summary.stable && summary.stablePulseCount > 0 && bucketDataComplete;
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
        trace->bucketCount == 0 || (trace->flags & kPulseTraceFlagStartupOverflow) != 0) {
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
                               defaultSegmentedCalibrationOptions());
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
    const SegmentedCalibrationOptions options = defaultSegmentedCalibrationOptions();
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
    if (!meteringSchemes_->saveCandidateAsCurrent(calibrationCandidate_, "校准生成计量方案", nowSeconds, newId)) {
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
    localMode_ = LocalUiMode::Normal;
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

bool AppController::applyTdsCalibrationForWeb() {
    if (!canUseTdsCalibration()) {
        return false;
    }
    SystemConfig updated = config_;
    if (!waterSensors_->applyReadyTdsCalibration(updated)) {
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
    if (!canApplyConfig() || !waterSensors_ || config_.temperatureKind != TemperatureKind::Ntc50kB3950) {
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
    if ((calibrationSession_.status == CalibrationSessionStatus::ReadyToGenerate ||
         calibrationSession_.status == CalibrationSessionStatus::Generated) &&
        calibrationCanQuickGenerate(calibrationSession_)) {
        const BeepPattern pendingBeepSnapshot = pendingBeep_;
        refreshCalibrationCandidate(calibrationSession_.updatedAt);
        pendingBeep_ = pendingBeepSnapshot;
    }
    if (calibrationSession_.status == CalibrationSessionStatus::WaitingLocalRun ||
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
    if (!trace || trace->bucketCount == 0 || (trace->flags & kPulseTraceFlagStartupOverflow) != 0) {
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


}  // namespace faucet
