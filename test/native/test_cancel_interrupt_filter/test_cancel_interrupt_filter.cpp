#include <unity.h>

#include "drivers/CancelInterruptFilter.h"

using namespace faucet;

void test_cancel_filter_rejects_short_low_pulse() {
    CancelInterruptFilter filter;

    CancelInterruptFilterResult result = filter.update(true, true, 1000);
    TEST_ASSERT_FALSE(result.pressed);
    TEST_ASSERT_FALSE(result.emergencyStop);

    result = filter.update(false, false, 1500);
    TEST_ASSERT_FALSE(result.pressed);
    TEST_ASSERT_FALSE(result.emergencyStop);
}

void test_cancel_filter_confirms_held_interrupt_after_one_millisecond() {
    CancelInterruptFilter filter;

    TEST_ASSERT_FALSE(filter.update(true, true, 1000).pressed);
    TEST_ASSERT_FALSE(filter.update(true, false, 1999).pressed);

    const CancelInterruptFilterResult confirmed = filter.update(true, false, 2000);
    TEST_ASSERT_TRUE(confirmed.pressed);
    TEST_ASSERT_TRUE(confirmed.emergencyStop);

    const CancelInterruptFilterResult held = filter.update(true, false, 3000);
    TEST_ASSERT_TRUE(held.pressed);
    TEST_ASSERT_FALSE(held.emergencyStop);
}

void test_cancel_filter_keeps_debounced_path_without_interrupt() {
    CancelInterruptFilter filter;

    TEST_ASSERT_FALSE(filter.update(true, false, 5000).pressed);
    const CancelInterruptFilterResult confirmed = filter.update(true, false, 6000);
    TEST_ASSERT_TRUE(confirmed.pressed);
    TEST_ASSERT_FALSE(confirmed.emergencyStop);
}

void test_cancel_filter_release_allows_next_interrupt() {
    CancelInterruptFilter filter;

    filter.update(true, true, 1000);
    TEST_ASSERT_TRUE(filter.update(true, false, 2000).emergencyStop);
    TEST_ASSERT_FALSE(filter.update(false, false, 2100).pressed);
    TEST_ASSERT_FALSE(filter.update(true, true, 3000).pressed);
    TEST_ASSERT_TRUE(filter.update(true, false, 4000).emergencyStop);
}

void test_cancel_filter_confirmation_survives_micros_wrap() {
    CancelInterruptFilter filter;
    constexpr std::uint32_t startUs = 0xFFFFFF00UL;

    TEST_ASSERT_FALSE(filter.update(true, true, startUs).pressed);
    const CancelInterruptFilterResult result =
        filter.update(true, false, startUs + kCancelInterruptConfirmUs);
    TEST_ASSERT_TRUE(result.pressed);
    TEST_ASSERT_TRUE(result.emergencyStop);
}

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_cancel_filter_rejects_short_low_pulse);
    RUN_TEST(test_cancel_filter_confirms_held_interrupt_after_one_millisecond);
    RUN_TEST(test_cancel_filter_keeps_debounced_path_without_interrupt);
    RUN_TEST(test_cancel_filter_release_allows_next_interrupt);
    RUN_TEST(test_cancel_filter_confirmation_survives_micros_wrap);
    return UNITY_END();
}
