#include <unity.h>

#include "web/FaucetWebPolicy.h"

#include <cstring>

using namespace faucet;

void test_web_write_busy_policy_allows_writes_when_water_task_is_inactive() {
    char location[64] = "unchanged";

    TEST_ASSERT_FALSE(faucetWebWriteBusyRedirect(false, FaucetWebWriteTarget::Records, location, sizeof(location)));
    TEST_ASSERT_EQUAL_STRING("unchanged", location);
}

void test_web_write_busy_policy_redirects_record_and_calibration_writes() {
    char location[64]{};

    TEST_ASSERT_TRUE(faucetWebWriteBusyRedirect(true, FaucetWebWriteTarget::Records, location, sizeof(location)));
    TEST_ASSERT_EQUAL_STRING("/faucet/records?error=busy", location);

    location[0] = '\0';
    TEST_ASSERT_TRUE(faucetWebWriteBusyRedirect(true, FaucetWebWriteTarget::Calibration, location, sizeof(location)));
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=busy", location);
}

void test_web_write_busy_policy_redirects_filter_reset_before_runtime_write() {
    char location[64]{};

    TEST_ASSERT_TRUE(faucetWebWriteBusyRedirect(true, FaucetWebWriteTarget::Filters, location, sizeof(location)));
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?error=busy", location);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_web_write_busy_policy_allows_writes_when_water_task_is_inactive);
    RUN_TEST(test_web_write_busy_policy_redirects_record_and_calibration_writes);
    RUN_TEST(test_web_write_busy_policy_redirects_filter_reset_before_runtime_write);
    return UNITY_END();
}
