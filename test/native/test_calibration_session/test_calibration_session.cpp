#include <unity.h>

#include "app/CalibrationSession.h"

using namespace faucet;

namespace {

CalibrationAttempt validAttempt(std::uint8_t index, std::uint32_t actualMl) {
    CalibrationAttempt attempt{};
    attempt.attemptIndex = index;
    attempt.actualMl = actualMl;
    attempt.record.pulseCount = 1;
    attempt.summary.actualMl = actualMl;
    attempt.summary.totalPulses = 1;
    attempt.summary.stable = true;
    attempt.summary.stablePulseCount = 1;
    attempt.summary.usableForGeneration = true;
    attempt.status = CalibrationAttemptStatus::Valid;
    return attempt;
}

}  // namespace

void test_new_session_starts_preparing() {
    const CalibrationSessionRecord session = makeCalibrationSession(42, 1770000000);

    TEST_ASSERT_EQUAL_UINT32(42, session.sessionId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Preparing),
                            static_cast<unsigned>(session.status));
    TEST_ASSERT_EQUAL_UINT32(1770000000, session.startedAt);
    TEST_ASSERT_EQUAL_UINT32(1770000000, session.updatedAt);
    TEST_ASSERT_EQUAL_UINT8(0, session.attemptCount);
    TEST_ASSERT_EQUAL_UINT8(0, session.validSampleCount);
}

void test_attempt_status_ordinals_keep_existing_values() {
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<unsigned>(CalibrationAttemptStatus::Empty));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<unsigned>(CalibrationAttemptStatus::PendingActual));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<unsigned>(CalibrationAttemptStatus::Valid));
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<unsigned>(CalibrationAttemptStatus::Skipped));
    TEST_ASSERT_EQUAL_UINT8(4, static_cast<unsigned>(CalibrationAttemptStatus::Invalid));
    TEST_ASSERT_EQUAL_UINT8(5, static_cast<unsigned>(CalibrationAttemptStatus::Removed));
}

void test_one_valid_sample_is_insufficient_for_quick_generation() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(0, 500)));

    TEST_ASSERT_EQUAL_UINT8(1, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationCoverageQuality::Insufficient),
                            static_cast<unsigned>(calibrationCoverageQuality(session)));
}

void test_valid_status_below_min_actual_ml_does_not_count_for_quick_generation() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt belowMin = validAttempt(0, kCalibrationMinActualMl - 1);

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, belowMin));
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(1, kCalibrationMinActualMl)));

    TEST_ASSERT_EQUAL_UINT8(2, countCalibrationAttempts(session));
    TEST_ASSERT_EQUAL_UINT8(1, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationCoverageQuality::Insufficient),
                            static_cast<unsigned>(calibrationCoverageQuality(session)));
}

void test_valid_status_zero_pulses_does_not_count_for_quick_generation() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt zeroPulses = validAttempt(0, 500);
    zeroPulses.record.pulseCount = 0;
    zeroPulses.summary.totalPulses = 0;
    zeroPulses.summary.usableForGeneration = false;

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, zeroPulses));
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(1, 1000)));

    TEST_ASSERT_EQUAL_UINT8(2, countCalibrationAttempts(session));
    TEST_ASSERT_EQUAL_UINT8(1, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationCoverageQuality::Insufficient),
                            static_cast<unsigned>(calibrationCoverageQuality(session)));
}

void test_two_valid_samples_allow_quick_generation() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(0, 500)));
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(1, 1000)));

    TEST_ASSERT_EQUAL_UINT8(2, kCalibrationMinQuickSamples);
    TEST_ASSERT_EQUAL_UINT8(2, countValidCalibrationSamples(session));
    TEST_ASSERT_TRUE(calibrationCanQuickGenerate(session));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationCoverageQuality::NarrowQuick),
                            static_cast<unsigned>(calibrationCoverageQuality(session)));
}

void test_two_narrow_valid_samples_allow_quick_generation() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(0, 500)));
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(1, 600)));

    TEST_ASSERT_EQUAL_UINT8(2, countValidCalibrationSamples(session));
    TEST_ASSERT_TRUE(calibrationCanQuickGenerate(session));
    TEST_ASSERT_NOT_EQUAL(static_cast<unsigned>(CalibrationCoverageQuality::Insufficient),
                          static_cast<unsigned>(calibrationCoverageQuality(session)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationCoverageQuality::NarrowQuick),
                            static_cast<unsigned>(calibrationCoverageQuality(session)));
}

void test_three_valid_samples_are_recommended() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(0, 500)));
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(1, 1500)));
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(2, 2500)));

    TEST_ASSERT_EQUAL_UINT8(3, kCalibrationRecommendedSamples);
    TEST_ASSERT_TRUE(calibrationIsRecommended(session));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationCoverageQuality::Recommended),
                            static_cast<unsigned>(calibrationCoverageQuality(session)));
}

void test_valid_summary_counts_as_valid_sample() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt attempt{};
    attempt.attemptIndex = 0;
    attempt.status = CalibrationAttemptStatus::Valid;
    attempt.actualMl = 1500;
    attempt.record.pulseCount = 0;
    attempt.summary.actualMl = 1500;
    attempt.summary.totalPulses = 360;
    attempt.summary.stable = true;
    attempt.summary.stablePulseCount = 320;
    attempt.summary.usableForGeneration = true;

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));

    TEST_ASSERT_EQUAL_UINT8(1, countValidCalibrationSamples(session));
}

void test_max_valid_samples_stop_new_runs() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    for (std::uint8_t i = 0; i < kCalibrationMaxValidSamples; ++i) {
        TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(i, 500 + static_cast<std::uint32_t>(i) * 500)));
    }

    TEST_ASSERT_EQUAL_UINT8(6, kCalibrationMaxValidSamples);
    TEST_ASSERT_EQUAL_UINT8(6, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanStartAttempt(session));
    TEST_ASSERT_FALSE(appendCalibrationAttempt(session, validAttempt(6, 3500)));
}

void test_session_allows_six_valid_samples() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    for (std::uint8_t i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(i, 500 + static_cast<std::uint32_t>(i) * 250)));
    }
    TEST_ASSERT_EQUAL_UINT8(6, kCalibrationMaxValidSamples);
    TEST_ASSERT_EQUAL_UINT8(6, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanStartAttempt(session));
}

void test_max_attempts_stop_session_when_not_ready() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    for (std::uint8_t i = 0; i < kCalibrationMaxAttempts; ++i) {
        CalibrationAttempt attempt{};
        attempt.attemptIndex = i;
        attempt.status = CalibrationAttemptStatus::Skipped;
        attempt.skipReason = CalibrationSkipReason::Mistake;
        TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    }

    TEST_ASSERT_EQUAL_UINT8(6, kCalibrationMaxAttempts);
    TEST_ASSERT_EQUAL_UINT8(6, countCalibrationAttempts(session));
    TEST_ASSERT_FALSE(calibrationCanStartAttempt(session));
    CalibrationAttempt extra{};
    extra.status = CalibrationAttemptStatus::Skipped;
    TEST_ASSERT_FALSE(appendCalibrationAttempt(session, extra));
}

void test_skipped_attempt_does_not_count_as_valid() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt skipped{};
    skipped.attemptIndex = 0;
    skipped.status = CalibrationAttemptStatus::Skipped;
    skipped.skipReason = CalibrationSkipReason::OverflowOrUnclearReading;

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, skipped));

    TEST_ASSERT_EQUAL_UINT8(1, countCalibrationAttempts(session));
    TEST_ASSERT_EQUAL_UINT8(0, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
}

void test_removed_sample_does_not_count_as_valid() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt attempt = validAttempt(0, 800);
    attempt.status = CalibrationAttemptStatus::Removed;
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    TEST_ASSERT_EQUAL_UINT8(0, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
}

void test_paused_resume_attempt_is_invalid_for_generation() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt attempt{};
    attempt.attemptIndex = 0;
    attempt.status = CalibrationAttemptStatus::Invalid;
    attempt.invalidReason = CalibrationInvalidReason::ResumedAfterPause;
    attempt.resumedAfterPause = true;
    attempt.actualMl = 1000;

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));

    TEST_ASSERT_EQUAL_UINT8(1, countCalibrationAttempts(session));
    TEST_ASSERT_EQUAL_UINT8(0, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
}

void test_attempt_keeps_full_water_record_identity() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt attempt = validAttempt(0, 1000);
    attempt.record.startTime = 1770000100;
    attempt.record.volumeMl = 980;
    attempt.record.targetValue = 1000;
    attempt.record.pulseCount = 221;
    attempt.record.rejectedPulseCount = 3;
    attempt.record.durationSec = 7;
    attempt.record.mode = WaterMode::Volume;
    attempt.record.result = WaterResult::Completed;
    attempt.record.selectedPreset = 2;

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));

    const CalibrationAttempt& stored = session.attempts[0];
    TEST_ASSERT_EQUAL_UINT32(1770000100, stored.record.startTime);
    TEST_ASSERT_EQUAL_UINT32(980, stored.record.volumeMl);
    TEST_ASSERT_EQUAL_UINT32(1000, stored.record.targetValue);
    TEST_ASSERT_EQUAL_UINT32(221, stored.record.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(3, stored.record.rejectedPulseCount);
    TEST_ASSERT_EQUAL_UINT16(7, stored.record.durationSec);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterMode::Volume), static_cast<unsigned>(stored.record.mode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::Completed), static_cast<unsigned>(stored.record.result));
    TEST_ASSERT_EQUAL_UINT8(2, stored.record.selectedPreset);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_new_session_starts_preparing);
    RUN_TEST(test_attempt_status_ordinals_keep_existing_values);
    RUN_TEST(test_one_valid_sample_is_insufficient_for_quick_generation);
    RUN_TEST(test_valid_status_below_min_actual_ml_does_not_count_for_quick_generation);
    RUN_TEST(test_valid_status_zero_pulses_does_not_count_for_quick_generation);
    RUN_TEST(test_two_valid_samples_allow_quick_generation);
    RUN_TEST(test_two_narrow_valid_samples_allow_quick_generation);
    RUN_TEST(test_three_valid_samples_are_recommended);
    RUN_TEST(test_valid_summary_counts_as_valid_sample);
    RUN_TEST(test_max_valid_samples_stop_new_runs);
    RUN_TEST(test_session_allows_six_valid_samples);
    RUN_TEST(test_max_attempts_stop_session_when_not_ready);
    RUN_TEST(test_skipped_attempt_does_not_count_as_valid);
    RUN_TEST(test_removed_sample_does_not_count_as_valid);
    RUN_TEST(test_paused_resume_attempt_is_invalid_for_generation);
    RUN_TEST(test_attempt_keeps_full_water_record_identity);
    return UNITY_END();
}
