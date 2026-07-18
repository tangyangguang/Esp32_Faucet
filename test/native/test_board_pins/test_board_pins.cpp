#include <unity.h>

#include "drivers/BoardPins.h"

using namespace faucet;

void test_board1_control_pin_mapping_matches_2026_07_11_netlist() {
    TEST_ASSERT_EQUAL_UINT8(26, kPinValvePwm);
    TEST_ASSERT_EQUAL_UINT8(32, kPinValveShutdown);
    TEST_ASSERT_EQUAL_UINT8(33, kPinFlowPrimary);
    TEST_ASSERT_EQUAL_UINT8(25, kPinFlowSecondary);
    TEST_ASSERT_EQUAL_UINT8(39, kPinButtonCancel);
    TEST_ASSERT_EQUAL_UINT8(36, kPinButtonOk);
    TEST_ASSERT_EQUAL_UINT8(34, kPinButtonPlus);
    TEST_ASSERT_EQUAL_UINT8(35, kPinButtonMinus);
    TEST_ASSERT_EQUAL_UINT8(13, kPinBeep);
}

void test_board1_bus_and_display_pin_mapping_matches_2026_07_11_netlist() {
    TEST_ASSERT_EQUAL_UINT8(21, kPinI2cSda);
    TEST_ASSERT_EQUAL_UINT8(22, kPinI2cScl);
    TEST_ASSERT_EQUAL_UINT8(27, kPinAds1115Alert);
    TEST_ASSERT_EQUAL_UINT8(0x48, kAds1115Address);
    TEST_ASSERT_EQUAL_UINT8(18, kPinSt7789Sclk);
    TEST_ASSERT_EQUAL_UINT8(23, kPinSt7789Mosi);
    TEST_ASSERT_EQUAL_UINT8(14, kPinSt7789Cs);
    TEST_ASSERT_EQUAL_UINT8(17, kPinSt7789Dc);
    TEST_ASSERT_EQUAL_UINT8(16, kPinSt7789Rst);
    TEST_ASSERT_EQUAL_UINT8(19, kPinSt7789Backlight);
}

void test_valve_and_beep_use_independent_ledc_timers() {
    TEST_ASSERT_EQUAL_UINT8(0, kLedcChannelValve);
    TEST_ASSERT_EQUAL_UINT8(2, kLedcChannelBeep);
    TEST_ASSERT_NOT_EQUAL(kLedcChannelValve / 2U, kLedcChannelBeep / 2U);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_board1_control_pin_mapping_matches_2026_07_11_netlist);
    RUN_TEST(test_board1_bus_and_display_pin_mapping_matches_2026_07_11_netlist);
    RUN_TEST(test_valve_and_beep_use_independent_ledc_timers);
    return UNITY_END();
}
