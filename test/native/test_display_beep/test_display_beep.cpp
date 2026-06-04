#include <unity.h>

#include "app/BeepDriver.h"
#include "app/DisplayPresenter.h"

#include <cstdio>
#include <cstring>

using namespace faucet;

namespace {

AppSnapshot makeSnapshot(WaterState state, std::uint32_t targetMl, std::uint32_t volumeMl = 0) {
    AppSnapshot snapshot{
        WaterSnapshot{
            state,
            0,
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
    snapshot.pulsePerLiter = 450;
    return snapshot;
}

void assertDisplayLinesFit(const DisplayFrame& frame) {
    TEST_ASSERT_LESS_OR_EQUAL_size_t(16, std::strlen(frame.line1));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(16, std::strlen(frame.line2));
}

void assertPulseLabelFixedRight(const DisplayFrame& frame) {
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(4, std::strlen(frame.line1));
    TEST_ASSERT_EQUAL_STRING("450P", frame.line1 + std::strlen(frame.line1) - 4);
}

}  // namespace

void test_display_idle_shows_preset_and_today_total() {
    DisplayPresenter presenter(30);
    presenter.wake(1000);

    DisplayFrame frame = presenter.render(makeSnapshot(WaterState::Idle, 1500), 2000);

    TEST_ASSERT_TRUE(frame.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Idle), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("SEL P1 1.5L 450P", frame.line1);
    TEST_ASSERT_EQUAL_STRING("TODAY 1.23L", frame.line2);
    assertPulseLabelFixedRight(frame);
    assertDisplayLinesFit(frame);
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

    TEST_ASSERT_EQUAL_STRING("RUN 00:04   450P", runningFrame.line1);
    TEST_ASSERT_EQUAL_STRING("0.00L LFT 6s", runningFrame.line2);
    TEST_ASSERT_EQUAL_STRING("Flow Error", errorFrame.line2);
    TEST_ASSERT_EQUAL_STRING("Error       450P", errorFrame.line1);
    assertPulseLabelFixedRight(errorFrame);
    assertPulseLabelFixedRight(runningFrame);
    assertDisplayLinesFit(runningFrame);
    assertDisplayLinesFit(errorFrame);
}

void test_display_running_shows_remaining_volume() {
    DisplayPresenter presenter(30);
    presenter.wake(0);

    DisplayFrame frame = presenter.render(makeSnapshot(WaterState::Running, 1500, 400), 500);

    TEST_ASSERT_EQUAL_STRING("RUN 00:00   450P", frame.line1);
    TEST_ASSERT_EQUAL_STRING("0.40L LFT 1.10L", frame.line2);
    assertPulseLabelFixedRight(frame);
    assertDisplayLinesFit(frame);
}

void test_display_confirm_and_pause_pages_are_short() {
    DisplayPresenter presenter(30);
    presenter.wake(0);

    DisplayFrame confirm = presenter.render(makeSnapshot(WaterState::Confirm, 7500), 500);
    DisplayFrame paused = presenter.render(makeSnapshot(WaterState::Paused, 7500, 300), 500);
    AppSnapshot timeConfirm = makeSnapshot(WaterState::Confirm, 60);
    timeConfirm.water.mode = WaterMode::Time;
    timeConfirm.water.targetValue = 60;
    DisplayFrame timeConfirmFrame = presenter.render(timeConfirm, 500);

    TEST_ASSERT_EQUAL_STRING("GO P1 7.5L  450P", confirm.line1);
    TEST_ASSERT_EQUAL_STRING("STEP 0.10L", confirm.line2);
    TEST_ASSERT_EQUAL_STRING("PAU 0.30L   450P", paused.line1);
    TEST_ASSERT_EQUAL_STRING("CAN=STOP OK=RUN", paused.line2);
    TEST_ASSERT_EQUAL_STRING("GO P1 60s   450P", timeConfirmFrame.line1);
    TEST_ASSERT_EQUAL_STRING("STEP 10s", timeConfirmFrame.line2);
    assertPulseLabelFixedRight(confirm);
    assertPulseLabelFixedRight(paused);
    assertPulseLabelFixedRight(timeConfirmFrame);
    assertDisplayLinesFit(confirm);
    assertDisplayLinesFit(paused);
    assertDisplayLinesFit(timeConfirmFrame);
}

void test_display_result_page_shows_summary() {
    DisplayPresenter presenter(30);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Idle, 7500, 7500);
    snapshot.water.elapsedSec = 163;
    snapshot.water.lastResult = WaterResult::Completed;
    snapshot.localMode = LocalUiMode::Result;
    snapshot.calibrationReady = true;

    DisplayFrame frame = presenter.render(snapshot, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Result), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("Done 7.50L  450P", frame.line1);
    TEST_ASSERT_EQUAL_STRING("OK Back", frame.line2);
    assertPulseLabelFixedRight(frame);
    assertDisplayLinesFit(frame);
}

void test_display_local_calibration_page_shows_session_state() {
    DisplayPresenter presenter(30);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Idle, 1500, 1500);
    snapshot.localMode = LocalUiMode::Calibration;
    snapshot.calibrationStatus = CalibrationSessionStatus::Preparing;
    snapshot.pulsePerLiter = 450;

    DisplayFrame frame = presenter.render(snapshot, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Calibration), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("Cal Ready   450P", frame.line1);
    TEST_ASSERT_EQUAL_STRING("Cup Ready OK", frame.line2);
    assertPulseLabelFixedRight(frame);
    assertDisplayLinesFit(frame);

    snapshot.calibrationStatus = CalibrationSessionStatus::Running;
    snapshot.water.volumeMl = 520;
    frame = presenter.render(snapshot, 800);
    TEST_ASSERT_EQUAL_STRING("Cal 0.52L   450P", frame.line1);
    TEST_ASSERT_EQUAL_STRING("Cancel Stop", frame.line2);
    assertPulseLabelFixedRight(frame);
    assertDisplayLinesFit(frame);

    snapshot.calibrationStatus = CalibrationSessionStatus::AwaitingActual;
    frame = presenter.render(snapshot, 900);
    TEST_ASSERT_EQUAL_STRING("Actual ml   450P", frame.line1);
    TEST_ASSERT_EQUAL_STRING("Input/Cancel", frame.line2);
    assertPulseLabelFixedRight(frame);
    assertDisplayLinesFit(frame);
}

void test_display_omits_pulse_label_when_unavailable() {
    DisplayPresenter presenter(30);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Idle, 1500);
    snapshot.pulsePerLiter = 0;

    DisplayFrame frame = presenter.render(snapshot, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(DisplayPage::Idle), static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_STRING("SEL P1 1.5L", frame.line1);
    TEST_ASSERT_EQUAL_STRING("TODAY 1.23L", frame.line2);
    assertDisplayLinesFit(frame);
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

void test_lcd_driver_does_not_initialize_while_display_frame_is_sleeping() {
    FILE* file = std::fopen("src/drivers/Lcd1602Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char buffer[12000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "if (!frame.on)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "return;"));
    TEST_ASSERT_NULL(std::strstr(buffer, "shouldReinitialize(nowMs, frame) && initialize()"));
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
    RUN_TEST(test_display_local_calibration_page_shows_session_state);
    RUN_TEST(test_display_omits_pulse_label_when_unavailable);
    RUN_TEST(test_beep_click_turns_off_after_duration);
    RUN_TEST(test_beep_duration_survives_millis_wrap);
    RUN_TEST(test_beep_disabled_ignores_patterns_and_stops_active_output);
    RUN_TEST(test_beep_patterns_have_distinct_feedback);
    RUN_TEST(test_lcd_driver_does_not_initialize_while_display_frame_is_sleeping);
    return UNITY_END();
}
