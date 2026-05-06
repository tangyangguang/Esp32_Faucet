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
    TEST_ASSERT_EQUAL_STRING("1500 ml", frame.line1);
    TEST_ASSERT_EQUAL_STRING("Today 1234", frame.line2);
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

void test_display_running_shows_remaining_volume() {
    DisplayPresenter presenter(30);
    presenter.wake(0);

    DisplayFrame frame = presenter.render(makeSnapshot(WaterState::Running, 1500, 400), 500);

    TEST_ASSERT_EQUAL_STRING("Remain 1100", frame.line1);
    TEST_ASSERT_EQUAL_STRING("Out 400", frame.line2);
}

void test_display_confirm_and_pause_pages_are_short() {
    DisplayPresenter presenter(30);
    presenter.wake(0);

    DisplayFrame confirm = presenter.render(makeSnapshot(WaterState::Confirm, 7500), 500);
    DisplayFrame paused = presenter.render(makeSnapshot(WaterState::Paused, 7500, 300), 500);

    TEST_ASSERT_EQUAL_STRING("OK Start?", confirm.line1);
    TEST_ASSERT_EQUAL_STRING("7500 ml", confirm.line2);
    TEST_ASSERT_EQUAL_STRING("Paused", paused.line1);
    TEST_ASSERT_EQUAL_STRING("Out 300", paused.line2);
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
    RUN_TEST(test_display_running_shows_remaining_volume);
    RUN_TEST(test_display_confirm_and_pause_pages_are_short);
    RUN_TEST(test_beep_click_turns_off_after_duration);
    RUN_TEST(test_beep_disabled_ignores_patterns_and_stops_active_output);
    RUN_TEST(test_beep_patterns_have_distinct_feedback);
    return UNITY_END();
}
