#include "app/CalibrationSession.h"

#include <algorithm>

namespace faucet {

namespace {

bool isValidSample(const CalibrationAttempt& attempt) {
    return attempt.status == CalibrationAttemptStatus::Valid &&
           attempt.summary.usableForGeneration &&
           attempt.summary.actualMl >= kCalibrationMinActualMl &&
           attempt.summary.totalPulses > 0;
}

std::uint32_t validSampleSpanMl(const CalibrationSessionRecord& session) {
    bool found = false;
    std::uint32_t minMl = 0;
    std::uint32_t maxMl = 0;
    for (std::uint8_t i = 0; i < session.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = session.attempts[i];
        if (!isValidSample(attempt)) {
            continue;
        }
        if (!found) {
            minMl = attempt.summary.actualMl;
            maxMl = attempt.summary.actualMl;
            found = true;
        } else {
            minMl = std::min(minMl, attempt.summary.actualMl);
            maxMl = std::max(maxMl, attempt.summary.actualMl);
        }
    }
    return found ? maxMl - minMl : 0;
}

}  // namespace

void initializeCalibrationSessionRecord(CalibrationSessionRecord& session,
                                        std::uint32_t sessionId,
                                        std::uint32_t nowSeconds) {
    session = CalibrationSessionRecord{};
    session.sessionId = sessionId;
    session.status = CalibrationSessionStatus::Preparing;
    session.startedAt = nowSeconds;
    session.updatedAt = nowSeconds;
}

CalibrationSessionRecord makeCalibrationSession(std::uint32_t sessionId, std::uint32_t nowSeconds) {
    CalibrationSessionRecord session{};
    initializeCalibrationSessionRecord(session, sessionId, nowSeconds);
    return session;
}

std::uint8_t countValidCalibrationSamples(const CalibrationSessionRecord& session) {
    std::uint8_t count = 0;
    for (std::uint8_t i = 0; i < session.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        if (isValidSample(session.attempts[i])) {
            ++count;
        }
    }
    return count;
}

std::uint8_t countCalibrationAttempts(const CalibrationSessionRecord& session) {
    return std::min<std::uint8_t>(session.attemptCount, kCalibrationMaxAttempts);
}

bool calibrationCanStartAttempt(const CalibrationSessionRecord& session) {
    return countCalibrationAttempts(session) < kCalibrationMaxAttempts &&
           countValidCalibrationSamples(session) < kCalibrationMaxValidSamples &&
           session.status != CalibrationSessionStatus::Failed && session.status != CalibrationSessionStatus::Applied &&
           session.status != CalibrationSessionStatus::Discarded;
}

CalibrationCoverageQuality calibrationCoverageQuality(const CalibrationSessionRecord& session) {
    if (countValidCalibrationSamples(session) < kCalibrationMinQuickSamples) {
        return CalibrationCoverageQuality::Insufficient;
    }
    const std::uint32_t spanMl = validSampleSpanMl(session);
    if (spanMl < kCalibrationRecommendedVolumeSpanMl) {
        return CalibrationCoverageQuality::NarrowQuick;
    }
    return CalibrationCoverageQuality::Recommended;
}

bool calibrationCanQuickGenerate(const CalibrationSessionRecord& session) {
    return calibrationCoverageQuality(session) != CalibrationCoverageQuality::Insufficient;
}

bool calibrationIsRecommended(const CalibrationSessionRecord& session) {
    return countValidCalibrationSamples(session) >= kCalibrationRecommendedSamples &&
           calibrationCoverageQuality(session) == CalibrationCoverageQuality::Recommended;
}

bool appendCalibrationAttempt(CalibrationSessionRecord& session, const CalibrationAttempt& attempt) {
    if (session.attemptCount >= kCalibrationMaxAttempts) {
        return false;
    }
    if (isValidSample(attempt) && countValidCalibrationSamples(session) >= kCalibrationMaxValidSamples) {
        return false;
    }

    CalibrationAttempt stored = attempt;
    if (stored.attemptIndex == 0 && session.attemptCount != 0) {
        stored.attemptIndex = session.attemptCount;
    }
    session.attempts[session.attemptCount] = stored;
    ++session.attemptCount;
    session.validSampleCount = countValidCalibrationSamples(session);
    if (session.validSampleCount >= kCalibrationMinQuickSamples &&
        session.status != CalibrationSessionStatus::Generated && session.status != CalibrationSessionStatus::Applied) {
        session.status = CalibrationSessionStatus::ReadyToGenerate;
    }
    if (session.attemptCount >= kCalibrationMaxAttempts && !calibrationCanQuickGenerate(session)) {
        session.status = CalibrationSessionStatus::Failed;
    }
    return true;
}

}  // namespace faucet
