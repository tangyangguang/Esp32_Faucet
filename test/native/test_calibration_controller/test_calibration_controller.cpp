#include <unity.h>

#include "app/CalibrationController.h"

using namespace faucet;

void test_controller_starts_ready_with_default_target() {
    CalibrationController controller(0.42f);
    const CalibrationSnapshot snapshot = controller.snapshot();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationState::Ready), static_cast<unsigned>(snapshot.state));
    TEST_ASSERT_EQUAL_UINT32(1500, snapshot.targetMl);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.sampleCount);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.42f, snapshot.oldPulsePerMl);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.42f, snapshot.proposedPulsePerMl);
    TEST_ASSERT_FALSE(controller.canSave());
}

void test_target_selection_accepts_only_supported_targets() {
    CalibrationController controller;

    TEST_ASSERT_TRUE(controller.setTargetMl(1500));
    TEST_ASSERT_EQUAL_UINT32(1500, controller.snapshot().targetMl);
    TEST_ASSERT_TRUE(controller.setTargetMl(7500));
    TEST_ASSERT_FALSE(controller.setTargetMl(750));
    TEST_ASSERT_EQUAL_UINT32(7500, controller.snapshot().targetMl);
}

void test_two_consistent_samples_produce_average_and_can_save() {
    CalibrationController controller(0.45f);

    TEST_ASSERT_TRUE(controller.setTargetMl(1500));
    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Accepted),
        static_cast<unsigned>(controller.finishSample(675)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationState::NeedsMoreSamples),
        static_cast<unsigned>(controller.snapshot().state));
    TEST_ASSERT_FALSE(controller.canSave());

    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Accepted),
        static_cast<unsigned>(controller.finishSample(690)));

    const CalibrationSnapshot snapshot = controller.snapshot();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationState::ReadyToSave), static_cast<unsigned>(snapshot.state));
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.sampleCount);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.455f, controller.proposedPulsePerMl());
    TEST_ASSERT_TRUE(controller.canSave());
}

void test_inconsistent_second_sample_is_rejected() {
    CalibrationController controller(0.45f);

    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Accepted),
        static_cast<unsigned>(controller.finishSample(450)));

    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Inconsistent),
        static_cast<unsigned>(controller.finishSample(600)));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationState::Rejected),
        static_cast<unsigned>(controller.snapshot().state));
    TEST_ASSERT_FALSE(controller.canSave());
}

void test_invalid_pulse_count_is_rejected() {
    CalibrationController controller;

    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::InvalidPulseCount),
        static_cast<unsigned>(controller.finishSample(0)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationState::Rejected),
        static_cast<unsigned>(controller.snapshot().state));
}

void test_cannot_change_target_while_sampling() {
    CalibrationController controller;

    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_FALSE(controller.setTargetMl(7500));
    TEST_ASSERT_EQUAL_UINT32(1500, controller.snapshot().targetMl);
}

void test_reset_clears_sampling_state_and_uses_new_targets() {
    CalibrationController controller(0.45f);
    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Accepted),
        static_cast<unsigned>(controller.finishSample(675)));

    const std::uint32_t targets[kCalibrationTargetCount] = {0, 2500, 0, 0};
    controller.reset(0.62f, targets);
    const CalibrationSnapshot snapshot = controller.snapshot();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationState::Ready), static_cast<unsigned>(snapshot.state));
    TEST_ASSERT_EQUAL_UINT32(2500, snapshot.targetMl);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.sampleCount);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.62f, snapshot.oldPulsePerMl);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.62f, snapshot.proposedPulsePerMl);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, snapshot.lastSamplePulsePerMl);
    TEST_ASSERT_FALSE(controller.setTargetMl(1500));
}

void test_switching_targets_clears_previous_samples() {
    CalibrationController controller(0.45f);

    TEST_ASSERT_TRUE(controller.setTargetMl(1500));
    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Accepted),
        static_cast<unsigned>(controller.finishSample(675)));
    TEST_ASSERT_EQUAL_UINT8(1, controller.snapshot().sampleCount);

    TEST_ASSERT_TRUE(controller.setTargetMl(7500));
    TEST_ASSERT_EQUAL_UINT32(7500, controller.snapshot().targetMl);
    TEST_ASSERT_EQUAL_UINT8(0, controller.snapshot().sampleCount);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.45f, controller.proposedPulsePerMl());

    TEST_ASSERT_TRUE(controller.beginSampling());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(CalibrationSampleResult::Accepted),
        static_cast<unsigned>(controller.finishSample(3375)));
    TEST_ASSERT_EQUAL_UINT8(1, controller.snapshot().sampleCount);
}

void test_controller_uses_configured_enabled_targets() {
    CalibrationController controller;
    const std::uint32_t targets[kCalibrationTargetCount] = {0, 7500, 0, 0};

    controller.reset(0.42f, targets);

    TEST_ASSERT_EQUAL_UINT32(7500, controller.snapshot().targetMl);
    TEST_ASSERT_FALSE(controller.setTargetMl(1500));
    TEST_ASSERT_TRUE(controller.setTargetMl(7500));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_controller_starts_ready_with_default_target);
    RUN_TEST(test_target_selection_accepts_only_supported_targets);
    RUN_TEST(test_two_consistent_samples_produce_average_and_can_save);
    RUN_TEST(test_inconsistent_second_sample_is_rejected);
    RUN_TEST(test_invalid_pulse_count_is_rejected);
    RUN_TEST(test_cannot_change_target_while_sampling);
    RUN_TEST(test_reset_clears_sampling_state_and_uses_new_targets);
    RUN_TEST(test_switching_targets_clears_previous_samples);
    RUN_TEST(test_controller_uses_configured_enabled_targets);
    return UNITY_END();
}
