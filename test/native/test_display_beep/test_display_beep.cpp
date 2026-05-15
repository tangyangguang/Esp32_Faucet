#include <unity.h>

#include "app/BeepDriver.h"
#include "app/DisplayPresenter.h"

#include <cstring>

using namespace faucet;

namespace {

AppSnapshot makeSnapshot(WaterState state, std::uint32_t targetMl, std::uint32_t volumeMl = 0) {
    return AppSnapshot{
        WaterSnapshot{
            state,
            0,
            state == WaterState::Running,
            volumeMl,
            0,
            WaterResult::Completed,
            WaterMode::Volume,
            targetMl,
        },
        ValveOutput{ValveState::Closed, false, 0},
        StatisticsRecord{1234, 0, 0, 9000, 20260506, 202619, 202605},
    };
}

}  // namespace

void test_display_idle_shows_preset_and_today_total() {
    DisplayPresenter presenter(30);
    presenter.wake(1000);

    DisplayFrame frame = presenter.render(makeSnapshot(WaterState::Idle, 1500), 2000);

    TEST_ASSERT_TRUE(frame.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Idle), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("P1 1.50L", frame.line1);
    TEST_ASSERT_EQUAL_STRING("+/- Sel OK", frame.line2);
}

void test_display_sleep_only_in_idle_after_timeout() {
    DisplayPresenter presenter(1);
    presenter.wake(1000);

    DisplayFrame idle = presenter.render(makeSnapshot(WaterState::Idle, 1500), 2100);
    DisplayFrame running = presenter.render(makeSnapshot(WaterState::Running, 1500), 2100);

    TEST_ASSERT_FALSE(idle.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Sleep), static_cast<std::uint8_t>(idle.page));
    TEST_ASSERT_TRUE(running.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Running), static_cast<std::uint8_t>(running.page));
}

void test_display_sleep_survives_millis_wrap() {
    DisplayPresenter presenter(1);
    presenter.wake(0xFFFFF000UL);

    DisplayFrame idle = presenter.render(makeSnapshot(WaterState::Idle, 1500), 0xFFFFF000UL + 1100UL);

    TEST_ASSERT_FALSE(idle.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Sleep), static_cast<std::uint8_t>(idle.page));
}

void test_display_time_preset_shows_remaining_seconds_and_error_reason() {
    DisplayPresenter presenter(30);
    presenter.wake(0);
    AppSnapshot running = makeSnapshot(WaterState::Running, 10, 0);
    running.water.mode = WaterMode::Time;
    running.water.elapsedSec = 4;
    DisplayFrame runningFrame = presenter.render(running, 500);

    AppSnapshot error = makeSnapshot(WaterState::Error, 1500, 0);
    error.water.lastResult = WaterResult::FlowError;
    DisplayFrame errorFrame = presenter.render(error, 500);

    TEST_ASSERT_EQUAL_STRING("Lft 6s 00:04", runningFrame.line1);
    TEST_ASSERT_EQUAL_STRING("Flow Error", errorFrame.line2);
}

void test_display_running_shows_remaining_volume() {
    DisplayPresenter presenter(30);
    presenter.wake(0);

    DisplayFrame frame = presenter.render(makeSnapshot(WaterState::Running, 1500, 400), 500);

    TEST_ASSERT_EQUAL_STRING("Lft 1.10L 00:00", frame.line1);
    TEST_ASSERT_EQUAL_STRING("Out 0.40L OK", frame.line2);
}

void test_display_confirm_and_pause_pages_are_short() {
    DisplayPresenter presenter(30);
    presenter.wake(0);

    DisplayFrame confirm = presenter.render(makeSnapshot(WaterState::Confirm, 7500), 500);
    DisplayFrame paused = presenter.render(makeSnapshot(WaterState::Paused, 7500, 300), 500);

    TEST_ASSERT_EQUAL_STRING("Set 7.50L S0.50", confirm.line1);
    TEST_ASSERT_EQUAL_STRING("+/- Adj OK Go", confirm.line2);
    TEST_ASSERT_EQUAL_STRING("Paus 0.30/7.50", paused.line1);
    TEST_ASSERT_EQUAL_STRING("Step0.50 +/-OK", paused.line2);
}

void test_display_result_page_shows_summary() {
    DisplayPresenter presenter(30);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Idle, 7500, 7500);
    snapshot.water.elapsedSec = 163;
    snapshot.water.lastResult = WaterResult::Completed;
    snapshot.localMode = LocalUiMode::Result;

    DisplayFrame frame = presenter.render(snapshot, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Result), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("Done 7.50L", frame.line1);
    TEST_ASSERT_EQUAL_STRING("OK Back", frame.line2);
}

void test_display_local_calibration_page_shows_actual_and_step() {
    DisplayPresenter presenter(30);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Idle, 1500, 1500);
    snapshot.localMode = LocalUiMode::Calibration;
    snapshot.calibrationActualMl = 1000;
    snapshot.calibrationStepMl = 100;

    DisplayFrame frame = presenter.render(snapshot, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Calibration), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("Actual 1.00L", frame.line1);
    TEST_ASSERT_EQUAL_STRING("S0.10 +/- OK", frame.line2);
}

void test_beep_click_turns_off_after_duration() {
    BeepDriver beep(true);

    beep.play(BeepPattern::Click, 1000);
    TEST_ASSERT_TRUE(beep.output().enabled);
    TEST_ASSERT_EQUAL_UINT16(2400, beep.output().frequencyHz);

    beep.tick(1039);
    TEST_ASSERT_TRUE(beep.output().enabled);
    beep.tick(1040);
    TEST_ASSERT_FALSE(beep.output().enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::None), static_cast<std::uint8_t>(beep.activePattern()));
}

void test_beep_duration_survives_millis_wrap() {
    BeepDriver beep(true);

    beep.play(BeepPattern::Click, 0xFFFFF000UL);
    beep.tick(0xFFFFF000UL + 40UL);

    TEST_ASSERT_FALSE(beep.output().enabled);
}

void test_beep_disabled_ignores_patterns_and_stops_active_output() {
    BeepDriver beep(true);
    beep.play(BeepPattern::Error, 1000);
    TEST_ASSERT_TRUE(beep.output().enabled);

    beep.setEnabled(false);
    TEST_ASSERT_FALSE(beep.output().enabled);

    beep.play(BeepPattern::Click, 1100);
    TEST_ASSERT_FALSE(beep.output().enabled);
}

void test_beep_patterns_have_distinct_feedback() {
    BeepDriver beep(true);

    beep.play(BeepPattern::Done, 100);
    TEST_ASSERT_TRUE(beep.output().enabled);
    TEST_ASSERT_EQUAL_UINT16(1800, beep.output().frequencyHz);
    beep.tick(279);
    TEST_ASSERT_TRUE(beep.output().enabled);
    beep.tick(280);
    TEST_ASSERT_FALSE(beep.output().enabled);

    beep.play(BeepPattern::Error, 300);
    TEST_ASSERT_EQUAL_UINT16(900, beep.output().frequencyHz);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_display_idle_shows_preset_and_today_total);
    RUN_TEST(test_display_sleep_only_in_idle_after_timeout);
    RUN_TEST(test_display_sleep_survives_millis_wrap);
    RUN_TEST(test_display_running_shows_remaining_volume);
    RUN_TEST(test_display_time_preset_shows_remaining_seconds_and_error_reason);
    RUN_TEST(test_display_confirm_and_pause_pages_are_short);
    RUN_TEST(test_display_result_page_shows_summary);
    RUN_TEST(test_display_local_calibration_page_shows_actual_and_step);
    RUN_TEST(test_beep_click_turns_off_after_duration);
    RUN_TEST(test_beep_duration_survives_millis_wrap);
    RUN_TEST(test_beep_disabled_ignores_patterns_and_stops_active_output);
    RUN_TEST(test_beep_patterns_have_distinct_feedback);
    return UNITY_END();
}
