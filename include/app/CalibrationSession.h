#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

constexpr std::uint8_t kCalibrationMinQuickSamples = 2;
constexpr std::uint8_t kCalibrationRecommendedSamples = 3;
constexpr std::uint8_t kCalibrationMaxValidSamples = 6;
constexpr std::uint8_t kCalibrationMaxAttempts = 6;
constexpr std::uint32_t kCalibrationMinActualMl = 100;
constexpr std::uint32_t kCalibrationMinVolumeSpanMl = 500;
constexpr std::uint32_t kCalibrationRecommendedVolumeSpanMl = 1000;

enum class CalibrationSessionStatus : std::uint8_t {
    Idle,
    Preparing,
    WaitingLocalRun,
    Running,
    AwaitingActual,
    ReadyToGenerate,
    Generated,
    Applied,
    Discarded,
    Failed,
};

enum class CalibrationAttemptStatus : std::uint8_t {
    Empty,
    PendingActual,
    Valid,
    Skipped,
    Invalid,
    Removed,
};

enum class CalibrationSkipReason : std::uint8_t {
    None,
    OverflowOrUnclearReading,
    ContainerMissed,
    WaterPathClosed,
    Mistake,
    Other,
};

enum class CalibrationInvalidReason : std::uint8_t {
    None,
    TruncatedTrace,
    MissingActualMl,
    NoEffectivePulse,
    AnalysisFailed,
    ErrorResult,
    StorageFailed,
};

enum class CalibrationCoverageQuality : std::uint8_t {
    Insufficient,
    NarrowQuick,
    Recommended,
};

struct CalibrationSampleSummary {
    std::uint32_t actualMl = 0;
    std::uint32_t totalPulses = 0;
    std::uint32_t rejectedPulses = 0;
    std::uint32_t durationSec = 0;
    bool truncated = false;
    bool stable = false;
    std::uint32_t startupPulseCount = 0;
    std::uint32_t stablePulseCount = 0;
    std::uint32_t stableStartSec = 0;
    float stablePulsePerSec = 0.0f;
    bool usableForGeneration = false;
};

struct CalibrationAttempt {
    std::uint8_t attemptIndex = 0;
    std::uint8_t sessionTraceSlot = 255;
    WaterRecord record{};
    std::uint32_t targetHintMl = 0;
    std::uint32_t actualMl = 0;
    CalibrationAttemptStatus status = CalibrationAttemptStatus::Empty;
    CalibrationSkipReason skipReason = CalibrationSkipReason::None;
    CalibrationInvalidReason invalidReason = CalibrationInvalidReason::None;
    bool truncated = false;
    CalibrationSampleSummary summary{};
};

struct CalibrationSessionRecord {
    std::uint32_t sessionId = 0;
    CalibrationSessionStatus status = CalibrationSessionStatus::Idle;
    std::uint32_t startedAt = 0;
    std::uint32_t updatedAt = 0;
    std::uint32_t appliedSchemeId = 0;
    std::uint8_t attemptCount = 0;
    std::uint8_t validSampleCount = 0;
    CalibrationAttempt attempts[kCalibrationMaxAttempts]{};
};

void initializeCalibrationSessionRecord(CalibrationSessionRecord& session,
                                        std::uint32_t sessionId,
                                        std::uint32_t nowSeconds);
CalibrationSessionRecord makeCalibrationSession(std::uint32_t sessionId, std::uint32_t nowSeconds);
std::uint8_t countValidCalibrationSamples(const CalibrationSessionRecord& session);
std::uint8_t countCalibrationAttempts(const CalibrationSessionRecord& session);
bool calibrationCanStartAttempt(const CalibrationSessionRecord& session);
bool calibrationCanQuickGenerate(const CalibrationSessionRecord& session);
bool calibrationIsRecommended(const CalibrationSessionRecord& session);
CalibrationCoverageQuality calibrationCoverageQuality(const CalibrationSessionRecord& session);
bool appendCalibrationAttempt(CalibrationSessionRecord& session, const CalibrationAttempt& attempt);

}  // namespace faucet
