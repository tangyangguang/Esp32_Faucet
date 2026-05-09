#include <unity.h>

#include "app/WaterController.h"

using namespace faucet;

void startSelected(WaterController& controller, std::uint32_t nowMs = 1000) {
    TEST_ASSERT_TRUE(controller.requestStart(nowMs));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Confirm), static_cast<unsigned>(controller.snapshot().state));
    TEST_ASSERT_TRUE(controller.confirmStart(nowMs + 1000));
    TEST_ASSERT_TRUE(controller.snapshot().valveOpen);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Running), static_cast<unsigned>(controller.snapshot().state));
}

void test_volume_preset_completes_and_closes_valve() {
    SystemConfig config = makeDefaultConfig();
    WaterController controller(config);

    startSelected(controller);
    controller.addVolume(1500);
    controller.tick(3000);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::Completed), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterMode::Volume), static_cast<unsigned>(controller.result().mode));
    TEST_ASSERT_EQUAL_UINT32(1500, controller.result().volumeMl);
    TEST_ASSERT_FALSE(controller.snapshot().valveOpen);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Idle), static_cast<unsigned>(controller.snapshot().state));
}

void test_time_preset_completes_by_elapsed_time() {
    SystemConfig config = makeDefaultConfig();
    config.presets[2].enabled = true;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 10;
    WaterController controller(config);

    TEST_ASSERT_TRUE(controller.selectPreset(2));
    startSelected(controller, 2000);
    controller.addVolume(120);
    controller.tick(13000);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::Completed), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterMode::Time), static_cast<unsigned>(controller.result().mode));
    TEST_ASSERT_EQUAL_UINT32(120, controller.result().volumeMl);
}

void test_time_preset_completes_across_millis_wrap() {
    SystemConfig config = makeDefaultConfig();
    config.presets[2].enabled = true;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 10;
    WaterController controller(config);
    const std::uint32_t startMs = 0xFFFFF000UL;
    const std::uint32_t tickMs = startMs + static_cast<std::uint32_t>(11000);

    TEST_ASSERT_TRUE(controller.selectPreset(2));
    startSelected(controller, startMs);
    controller.addVolume(120);
    controller.tick(tickMs);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::Completed), static_cast<unsigned>(controller.result().result));
}

void test_confirm_timeout_cancels_without_result() {
    SystemConfig config = makeDefaultConfig();
    WaterController controller(config);

    TEST_ASSERT_TRUE(controller.requestStart(1000));
    controller.tick(1000 + (kDefaultConfirmTimeoutSec * 1000UL));

    TEST_ASSERT_FALSE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Idle), static_cast<unsigned>(controller.snapshot().state));
    TEST_ASSERT_FALSE(controller.snapshot().valveOpen);
}

void test_stop_finishes_running_task_as_user_stop() {
    SystemConfig config = makeDefaultConfig();
    WaterController controller(config);

    startSelected(controller, 1000);
    controller.addVolume(300);
    controller.stop(4500);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(WaterResult::StoppedByUser), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT32(300, controller.result().volumeMl);
    TEST_ASSERT_FALSE(controller.snapshot().valveOpen);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Idle), static_cast<unsigned>(controller.snapshot().state));
}

void test_pause_and_resume_excludes_paused_time() {
    SystemConfig config = makeDefaultConfig();
    config.presets[2].enabled = true;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 10;
    WaterController controller(config);

    TEST_ASSERT_TRUE(controller.selectPreset(2));
    startSelected(controller, 1000);
    controller.addVolume(1);
    TEST_ASSERT_TRUE(controller.togglePause(5000));
    TEST_ASSERT_FALSE(controller.snapshot().valveOpen);
    controller.tick(20000);
    TEST_ASSERT_FALSE(controller.hasResult());
    TEST_ASSERT_TRUE(controller.togglePause(20000));
    TEST_ASSERT_TRUE(controller.snapshot().valveOpen);
    controller.tick(27000);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::Completed), static_cast<unsigned>(controller.result().result));
}

void test_pause_timeout_stops_task() {
    SystemConfig config = makeDefaultConfig();
    config.pauseTimeoutSec = 10;
    WaterController controller(config);

    startSelected(controller, 1000);
    TEST_ASSERT_TRUE(controller.togglePause(3000));
    controller.tick(13000);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::PauseTimeout), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Idle), static_cast<unsigned>(controller.snapshot().state));
}

void test_no_flow_timeout_enters_error() {
    SystemConfig config = makeDefaultConfig();
    config.noFlowTimeoutSec = 3;
    WaterController controller(config);

    startSelected(controller, 1000);
    controller.tick(5000);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::FlowError), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Error), static_cast<unsigned>(controller.snapshot().state));
    TEST_ASSERT_FALSE(controller.snapshot().valveOpen);
}

void test_max_volume_safety_has_priority_over_normal_completion() {
    SystemConfig config = makeDefaultConfig();
    config.maxOutVolumeMl = 1200;
    WaterController controller(config);

    startSelected(controller, 1000);
    controller.addVolume(1500);
    controller.tick(2500);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(WaterResult::SafetyStopped), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Error), static_cast<unsigned>(controller.snapshot().state));
}

void test_max_out_time_forces_safety_stop() {
    SystemConfig config = makeDefaultConfig();
    config.maxOutTimeSec = 30;
    config.presets[2].enabled = true;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 60;
    WaterController controller(config);

    TEST_ASSERT_TRUE(controller.selectPreset(2));
    startSelected(controller, 1000);
    controller.addVolume(1);
    controller.tick(32000);

    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(WaterResult::SafetyStopped), static_cast<unsigned>(controller.result().result));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Error), static_cast<unsigned>(controller.snapshot().state));
    TEST_ASSERT_FALSE(controller.snapshot().valveOpen);
}

void test_high_flow_must_persist_before_error() {
    SystemConfig config = makeDefaultConfig();
    config.highFlowMlPerMin = 1000;
    config.highFlowDurationSec = 5;
    WaterController controller(config);

    startSelected(controller, 1000);
    controller.addVolume(1);
    controller.tick(3000, 1200);
    controller.tick(7000, 1200);
    TEST_ASSERT_FALSE(controller.hasResult());

    controller.tick(8000, 1200);
    TEST_ASSERT_TRUE(controller.hasResult());
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterResult::FlowError), static_cast<unsigned>(controller.result().result));
}

void test_next_preset_skips_disabled_presets() {
    SystemConfig config = makeDefaultConfig();
    WaterController controller(config);

    TEST_ASSERT_EQUAL_UINT(0, controller.snapshot().selectedPreset);
    TEST_ASSERT_TRUE(controller.selectNextPreset());
    TEST_ASSERT_EQUAL_UINT(1, controller.snapshot().selectedPreset);
    TEST_ASSERT_TRUE(controller.selectNextPreset());
    TEST_ASSERT_EQUAL_UINT(0, controller.snapshot().selectedPreset);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_volume_preset_completes_and_closes_valve);
    RUN_TEST(test_time_preset_completes_by_elapsed_time);
    RUN_TEST(test_time_preset_completes_across_millis_wrap);
    RUN_TEST(test_confirm_timeout_cancels_without_result);
    RUN_TEST(test_stop_finishes_running_task_as_user_stop);
    RUN_TEST(test_pause_and_resume_excludes_paused_time);
    RUN_TEST(test_pause_timeout_stops_task);
    RUN_TEST(test_no_flow_timeout_enters_error);
    RUN_TEST(test_max_volume_safety_has_priority_over_normal_completion);
    RUN_TEST(test_max_out_time_forces_safety_stop);
    RUN_TEST(test_high_flow_must_persist_before_error);
    RUN_TEST(test_next_preset_skips_disabled_presets);
    return UNITY_END();
}
