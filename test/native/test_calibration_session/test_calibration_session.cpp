#include <unity.h>

#include "app/CalibrationSession.h"

using namespace faucet;

namespace {

CalibrationAttempt validAttempt(std::uint8_t index, std::uint32_t actualMl) {
    CalibrationAttempt attempt{};
    attempt.attemptIndex = index;
    attempt.actualMl = actualMl;
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

void test_five_valid_samples_stop_new_runs() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    for (std::uint8_t i = 0; i < kCalibrationMaxValidSamples; ++i) {
        TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(i, 500 + static_cast<std::uint32_t>(i) * 500)));
    }

    TEST_ASSERT_EQUAL_UINT8(5, kCalibrationMaxValidSamples);
    TEST_ASSERT_EQUAL_UINT8(5, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanStartAttempt(session));
    TEST_ASSERT_FALSE(appendCalibrationAttempt(session, validAttempt(5, 3500)));
}

void test_ten_attempts_stop_session_when_not_ready() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);

    for (std::uint8_t i = 0; i < kCalibrationMaxAttempts; ++i) {
        CalibrationAttempt attempt{};
        attempt.attemptIndex = i;
        attempt.status = CalibrationAttemptStatus::Skipped;
        attempt.skipReason = CalibrationSkipReason::Mistake;
        TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    }

    TEST_ASSERT_EQUAL_UINT8(10, kCalibrationMaxAttempts);
    TEST_ASSERT_EQUAL_UINT8(10, countCalibrationAttempts(session));
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
    RUN_TEST(test_two_valid_samples_allow_quick_generation);
    RUN_TEST(test_three_valid_samples_are_recommended);
    RUN_TEST(test_five_valid_samples_stop_new_runs);
    RUN_TEST(test_ten_attempts_stop_session_when_not_ready);
    RUN_TEST(test_skipped_attempt_does_not_count_as_valid);
    RUN_TEST(test_paused_resume_attempt_is_invalid_for_generation);
    RUN_TEST(test_attempt_keeps_full_water_record_identity);
    return UNITY_END();
}
