#include <unity.h>

#include "app/BeepDriver.h"

using namespace faucet;

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
    RUN_TEST(test_beep_click_turns_off_after_duration);
    RUN_TEST(test_beep_duration_survives_millis_wrap);
    RUN_TEST(test_beep_disabled_ignores_patterns_and_stops_active_output);
    RUN_TEST(test_beep_patterns_have_distinct_feedback);
    return UNITY_END();
}
