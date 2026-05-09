#include <unity.h>

#include "app/ValveDriver.h"

using namespace faucet;

void test_starts_closed() {
    ValveDriver valve;
    ValveOutput output = valve.output();

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::Closed), static_cast<unsigned>(output.state));
    TEST_ASSERT_FALSE(output.enabled);
    TEST_ASSERT_EQUAL_UINT8(0, output.dutyPercent);
}

void test_open_uses_full_power_then_hold_duty() {
    ValveDriver valve(3, 30);

    valve.open(1000);
    ValveOutput output = valve.output();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::OpeningFullPower), static_cast<unsigned>(output.state));
    TEST_ASSERT_TRUE(output.enabled);
    TEST_ASSERT_EQUAL_UINT8(100, output.dutyPercent);

    valve.tick(3999);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::OpeningFullPower), static_cast<unsigned>(valve.output().state));

    valve.tick(4000);
    output = valve.output();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::Holding), static_cast<unsigned>(output.state));
    TEST_ASSERT_TRUE(output.enabled);
    TEST_ASSERT_EQUAL_UINT8(30, output.dutyPercent);
}

void test_close_is_idempotent_and_disables_output() {
    ValveDriver valve;

    valve.close();
    valve.close();
    TEST_ASSERT_FALSE(valve.output().enabled);

    valve.open(0);
    valve.close();
    ValveOutput output = valve.output();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::Closed), static_cast<unsigned>(output.state));
    TEST_ASSERT_FALSE(output.enabled);
    TEST_ASSERT_EQUAL_UINT8(0, output.dutyPercent);
}

void test_full_power_mode_when_hold_duty_is_100() {
    ValveDriver valve(1, 100);

    valve.open(0);
    valve.tick(1000);

    ValveOutput output = valve.output();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::Holding), static_cast<unsigned>(output.state));
    TEST_ASSERT_TRUE(output.enabled);
    TEST_ASSERT_EQUAL_UINT8(100, output.dutyPercent);
}

void test_full_power_timeout_survives_millis_wrap() {
    ValveDriver valve(1, 30);

    valve.open(0xFFFFF000UL);
    valve.tick(0xFFFFF000UL + 1000UL);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(ValveState::Holding), static_cast<unsigned>(valve.output().state));
}

void test_rejects_invalid_configuration() {
    ValveDriver valve(3, 30);

    TEST_ASSERT_FALSE(valve.configure(0, 30));
    TEST_ASSERT_FALSE(valve.configure(3, 19));
    TEST_ASSERT_FALSE(valve.configure(11, 30));
    TEST_ASSERT_TRUE(valve.configure(2, 40));

    valve.open(0);
    valve.tick(2000);
    TEST_ASSERT_EQUAL_UINT8(40, valve.output().dutyPercent);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_starts_closed);
    RUN_TEST(test_open_uses_full_power_then_hold_duty);
    RUN_TEST(test_close_is_idempotent_and_disables_output);
    RUN_TEST(test_full_power_mode_when_hold_duty_is_100);
    RUN_TEST(test_full_power_timeout_survives_millis_wrap);
    RUN_TEST(test_rejects_invalid_configuration);
    return UNITY_END();
}
