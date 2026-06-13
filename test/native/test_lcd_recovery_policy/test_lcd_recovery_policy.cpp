#include <unity.h>

#include "app/LcdRecoveryPolicy.h"

using namespace faucet;

void test_lcd_recovery_policy_never_probes_while_sleeping() {
    LcdRecoveryPolicy policy;

    TEST_ASSERT_FALSE(policy.shouldProbe(false, true, true, 1000));
    TEST_ASSERT_FALSE(policy.shouldProbe(false, false, true, 2000));
    TEST_ASSERT_FALSE(policy.shouldProbe(false, true, false, 20000));
}

void test_lcd_recovery_policy_probes_on_user_activity_with_guard_interval() {
    LcdRecoveryPolicy policy;

    TEST_ASSERT_TRUE(policy.shouldProbe(true, true, true, 1000));
    TEST_ASSERT_FALSE(policy.shouldProbe(true, true, true, 1100));
    TEST_ASSERT_TRUE(policy.shouldProbe(true, true, true, 1200));
}

void test_lcd_recovery_policy_probes_healthy_lit_display_at_low_frequency() {
    LcdRecoveryPolicy policy;

    TEST_ASSERT_FALSE(policy.shouldProbe(true, true, false, 9000));
    TEST_ASSERT_TRUE(policy.shouldProbe(true, true, false, 10000));
    TEST_ASSERT_FALSE(policy.shouldProbe(true, true, false, 15000));
    TEST_ASSERT_TRUE(policy.shouldProbe(true, true, false, 20000));
}

void test_lcd_recovery_policy_retries_disconnected_lit_display_faster() {
    LcdRecoveryPolicy policy;

    TEST_ASSERT_FALSE(policy.shouldProbe(true, false, false, 900));
    TEST_ASSERT_TRUE(policy.shouldProbe(true, false, false, 1000));
    TEST_ASSERT_FALSE(policy.shouldProbe(true, false, false, 1500));
    TEST_ASSERT_TRUE(policy.shouldProbe(true, false, false, 2000));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_lcd_recovery_policy_never_probes_while_sleeping);
    RUN_TEST(test_lcd_recovery_policy_probes_on_user_activity_with_guard_interval);
    RUN_TEST(test_lcd_recovery_policy_probes_healthy_lit_display_at_low_frequency);
    RUN_TEST(test_lcd_recovery_policy_retries_disconnected_lit_display_faster);
    return UNITY_END();
}
