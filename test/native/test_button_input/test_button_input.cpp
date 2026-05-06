#include <unity.h>

#include "app/ButtonInput.h"

using namespace faucet;

ButtonEvent updateAt(ButtonInput& input, bool stop, bool ok, bool next, std::uint32_t nowMs) {
    return input.update({stop, ok, next}, nowMs);
}

void test_stop_down_emits_after_debounce_without_waiting_for_release() {
    ButtonInput input;
    updateAt(input, true, false, false, 10);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::None),
        static_cast<unsigned>(updateAt(input, true, false, false, 20).type));

    const ButtonEvent event = updateAt(input, true, false, false, 40);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::StopDown), static_cast<unsigned>(event.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonId::Stop), static_cast<unsigned>(event.button));
}

void test_stop_short_emits_on_release() {
    ButtonInput input;
    updateAt(input, true, false, false, 0);
    updateAt(input, true, false, false, 30);
    updateAt(input, false, false, false, 200);

    const ButtonEvent event = updateAt(input, false, false, false, 230);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::StopShort), static_cast<unsigned>(event.type));
}

void test_ok_short_and_next_long_events() {
    ButtonInput input;

    updateAt(input, false, true, false, 0);
    updateAt(input, false, true, false, 30);
    updateAt(input, false, false, false, 200);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::OkShort),
        static_cast<unsigned>(updateAt(input, false, false, false, 230).type));

    updateAt(input, false, false, true, 1000);
    updateAt(input, false, false, true, 1030);
    updateAt(input, false, false, false, 2500);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::NextLong),
        static_cast<unsigned>(updateAt(input, false, false, false, 2530).type));
}

void test_bounce_shorter_than_debounce_is_ignored() {
    ButtonInput input;

    updateAt(input, false, true, false, 10);
    updateAt(input, false, false, false, 20);
    updateAt(input, false, false, false, 60);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::None),
        static_cast<unsigned>(updateAt(input, false, false, false, 100).type));
}

void test_factory_reset_combo_emits_once_after_five_seconds() {
    ButtonInput input;
    updateAt(input, true, true, false, 0);
    updateAt(input, true, true, false, 30);  // StopDown is emitted first.
    updateAt(input, true, true, false, 60);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::None),
        static_cast<unsigned>(updateAt(input, true, true, false, 5029).type));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::FactoryResetCombo),
        static_cast<unsigned>(updateAt(input, true, true, false, 5030).type));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::None),
        static_cast<unsigned>(updateAt(input, true, true, false, 7000).type));
}

void test_combo_cancelled_if_released_before_timeout() {
    ButtonInput input;
    updateAt(input, true, true, false, 0);
    updateAt(input, true, true, false, 30);
    updateAt(input, true, false, false, 3000);
    updateAt(input, true, false, false, 3030);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<unsigned>(ButtonEventType::None),
        static_cast<unsigned>(updateAt(input, true, false, false, 7000).type));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_stop_down_emits_after_debounce_without_waiting_for_release);
    RUN_TEST(test_stop_short_emits_on_release);
    RUN_TEST(test_ok_short_and_next_long_events);
    RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
    RUN_TEST(test_factory_reset_combo_emits_once_after_five_seconds);
    RUN_TEST(test_combo_cancelled_if_released_before_timeout);
    return UNITY_END();
}
