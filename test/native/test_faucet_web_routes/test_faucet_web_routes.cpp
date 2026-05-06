#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstring>

using namespace faucet;

void test_routes_fit_esp32base_default_route_capacity() {
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base());
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kFaucetWebMaxRoutes, faucetWebRouteCount());
    TEST_ASSERT_EQUAL_size_t(14, faucetWebRouteCount());
}

void test_routes_do_not_register_remote_water_control_paths() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(faucetWebRouteAllowed(routes[i].path), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/api/faucet/water/"), routes[i].path);
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp(routes[i].path, "/api/faucet/start"));
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp(routes[i].path, "/api/faucet/stop"));
    }
}

void test_route_blacklist_rejects_dangerous_control_aliases() {
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/start"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/stop"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/pause"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/resume"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/water/start"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/water/stop"));
}

void test_dual_method_routes_are_merged_to_any() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool foundConfig = false;
    bool foundPresets = false;
    bool foundCalibration = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/api/faucet/config") == 0) {
            foundConfig = routes[i].method == FaucetWebMethod::Any;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/presets") == 0) {
            foundPresets = routes[i].method == FaucetWebMethod::Any;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/calibration") == 0) {
            foundCalibration = routes[i].method == FaucetWebMethod::Any;
        }
    }

    TEST_ASSERT_TRUE(foundConfig);
    TEST_ASSERT_TRUE(foundPresets);
    TEST_ASSERT_TRUE(foundCalibration);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_routes_fit_esp32base_default_route_capacity);
    RUN_TEST(test_routes_do_not_register_remote_water_control_paths);
    RUN_TEST(test_route_blacklist_rejects_dangerous_control_aliases);
    RUN_TEST(test_dual_method_routes_are_merged_to_any);
    return UNITY_END();
}
