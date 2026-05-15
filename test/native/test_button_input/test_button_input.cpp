#include <unity.h>

#include "app/ButtonInput.h"

using namespace faucet;

ButtonEvent updateAt(ButtonInput& input,
                     bool cancel,
                     bool ok,
                     bool plus,
                     bool minus,
                     std::uint32_t nowMs) {
    return input.update({cancel, ok, plus, minus}, nowMs);
}

void test_cancel_down_emits_after_debounce_without_waiting_for_release() {
    ButtonInput input;
    updateAt(input, true, false, false, false, 10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::None),
                            static_cast<unsigned>(updateAt(input, true, false, false, false, 20).type));

    const ButtonEvent event = updateAt(input, true, false, false, false, 40);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::CancelDown), static_cast<unsigned>(event.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonId::Cancel), static_cast<unsigned>(event.button));
}

void test_cancel_short_emits_on_release() {
    ButtonInput input;
    updateAt(input, true, false, false, false, 0);
    updateAt(input, true, false, false, false, 30);
    updateAt(input, false, false, false, false, 200);

    const ButtonEvent event = updateAt(input, false, false, false, false, 230);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::CancelShort), static_cast<unsigned>(event.type));
}

void test_ok_plus_and_minus_events() {
    ButtonInput input;

    updateAt(input, false, true, false, false, 0);
    updateAt(input, false, true, false, false, 30);
    updateAt(input, false, false, false, false, 200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::OkShort),
                            static_cast<unsigned>(updateAt(input, false, false, false, false, 230).type));

    updateAt(input, false, false, true, false, 1000);
    updateAt(input, false, false, true, false, 1030);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::PlusLong),
                            static_cast<unsigned>(updateAt(input, false, false, true, false, 2030).type));
    updateAt(input, false, false, false, false, 2500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::None),
                            static_cast<unsigned>(updateAt(input, false, false, false, false, 2530).type));

    updateAt(input, false, false, false, true, 3000);
    updateAt(input, false, false, false, true, 3030);
    updateAt(input, false, false, false, false, 3200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::MinusShort),
                            static_cast<unsigned>(updateAt(input, false, false, false, false, 3230).type));
}

void test_ok_long_emits_when_threshold_is_reached_before_release() {
    ButtonInput input;
    updateAt(input, false, true, false, false, 100);
    updateAt(input, false, true, false, false, 130);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::None),
                            static_cast<unsigned>(updateAt(input, false, true, false, false, 1129).type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::OkLong),
                            static_cast<unsigned>(updateAt(input, false, true, false, false, 1130).type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::None),
                            static_cast<unsigned>(updateAt(input, false, true, false, false, 1500).type));

    updateAt(input, false, false, false, false, 1600);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::None),
                            static_cast<unsigned>(updateAt(input, false, false, false, false, 1630).type));
}

void test_bounce_shorter_than_debounce_is_ignored() {
    ButtonInput input;

    updateAt(input, false, true, false, false, 10);
    updateAt(input, false, false, false, false, 20);
    updateAt(input, false, false, false, false, 60);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::None),
                            static_cast<unsigned>(updateAt(input, false, false, false, false, 100).type));
}

void test_debounce_survives_millis_wrap() {
    ButtonInput input;
    const std::uint32_t startMs = 0xFFFFFFF0UL;

    updateAt(input, true, false, false, false, startMs);

    const ButtonEvent event = updateAt(input, true, false, false, false, startMs + kButtonDebounceMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ButtonEventType::CancelDown), static_cast<unsigned>(event.type));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_cancel_down_emits_after_debounce_without_waiting_for_release);
    RUN_TEST(test_cancel_short_emits_on_release);
    RUN_TEST(test_ok_plus_and_minus_events);
    RUN_TEST(test_ok_long_emits_when_threshold_is_reached_before_release);
    RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
    RUN_TEST(test_debounce_survives_millis_wrap);
    return UNITY_END();
}
