#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstring>

using namespace faucet;

namespace {

bool routeExists(const char* path, FaucetWebMethod method) {
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, path) == 0 && routes[i].method == method) {
            return true;
        }
    }
    return false;
}

const FaucetWebRoute* findRoute(const char* path, FaucetWebMethod method) {
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, path) == 0 && routes[i].method == method) {
            return &routes[i];
        }
    }
    return nullptr;
}

bool hasAnyRoute(const char* path) {
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

void test_routes_fit_esp32base_default_route_capacity() {
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base());
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base(24));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(24, faucetWebRouteCount());
}

void test_routes_do_not_register_remote_water_control_paths() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(faucetWebRouteAllowed(routes[i].path), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/api/faucet/water/"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/water/"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/start"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/stop"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/pause"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/resume"), routes[i].path);
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp(routes[i].path, "/api/faucet/start"));
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp(routes[i].path, "/api/faucet/stop"));
    }
}

void test_route_whitelist_rejects_unknown_and_dangerous_control_aliases() {
    TEST_ASSERT_FALSE(faucetWebRouteAllowed(nullptr));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed(""));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("api/faucet/status"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/config"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/logs"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/metering"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/unknown"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/config"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/start"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/stop"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/pause"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/resume"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/water/start"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/water/stop"));
}

void test_navigation_pages_use_requested_order_and_labels() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    const char* expectedPaths[] = {
        "/index", "/faucet/records", "/faucet/stats", "/faucet/presets", "/faucet/filters", "/faucet/calibration"};
    const char* expectedTitles[] = {"首页", "记录", "统计", "预设", "滤芯", "校准"};

    for (std::size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING(expectedPaths[i], routes[i].path);
        TEST_ASSERT_EQUAL(FaucetWebMethod::Get, routes[i].method);
        TEST_ASSERT_NOT_NULL(routes[i].title);
        TEST_ASSERT_EQUAL_STRING(expectedTitles[i], routes[i].title);
    }
}

void test_hidden_pages_and_assets_are_registered_without_navigation_titles() {
    const char* hiddenGetRoutes[] = {
        "/faucet/app.css",
        "/faucet/calibration/flow",
        "/faucet/calibration/samples",
        "/faucet/filters/edit",
        "/faucet/records/detail",
        "/faucet/calibration/detail",
    };

    for (const char* path : hiddenGetRoutes) {
        const FaucetWebRoute* route = findRoute(path, FaucetWebMethod::Get);
        TEST_ASSERT_NOT_NULL_MESSAGE(route, path);
        TEST_ASSERT_NULL_MESSAGE(route->title, path);
        TEST_ASSERT_TRUE_MESSAGE(faucetWebRouteAllowed(path), path);
    }
}

void test_business_api_routes_use_explicit_methods() {
    TEST_ASSERT_TRUE(routeExists("/api/faucet/status", FaucetWebMethod::Get));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/status", FaucetWebMethod::Post));

    TEST_ASSERT_TRUE(routeExists("/api/faucet/today", FaucetWebMethod::Get));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/today", FaucetWebMethod::Post));

    TEST_ASSERT_TRUE(routeExists("/api/faucet/stats", FaucetWebMethod::Get));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/stats", FaucetWebMethod::Post));

    TEST_ASSERT_TRUE(routeExists("/api/faucet/presets", FaucetWebMethod::Get));
    TEST_ASSERT_TRUE(routeExists("/api/faucet/presets", FaucetWebMethod::Post));

    TEST_ASSERT_TRUE(routeExists("/api/faucet/records", FaucetWebMethod::Get));
    TEST_ASSERT_TRUE(routeExists("/api/faucet/records", FaucetWebMethod::Post));

    TEST_ASSERT_TRUE(routeExists("/api/faucet/filters", FaucetWebMethod::Get));
    TEST_ASSERT_TRUE(routeExists("/api/faucet/filters", FaucetWebMethod::Post));

    TEST_ASSERT_FALSE(routeExists("/api/faucet/filters/reset", FaucetWebMethod::Get));
    TEST_ASSERT_TRUE(routeExists("/api/faucet/filters/reset", FaucetWebMethod::Post));
}

void test_business_write_routes_are_post_only() {
    const char* writePaths[] = {
        "/faucet/calibration",
        "/faucet/calibration/flow",
        "/api/faucet/presets",
        "/api/faucet/records",
        "/api/faucet/filters",
        "/api/faucet/filters/reset",
    };

    for (const char* path : writePaths) {
        TEST_ASSERT_TRUE_MESSAGE(routeExists(path, FaucetWebMethod::Post), path);
    }

    TEST_ASSERT_TRUE(routeExists("/faucet/calibration", FaucetWebMethod::Get));
    TEST_ASSERT_TRUE(routeExists("/faucet/calibration/flow", FaucetWebMethod::Get));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/filters/reset", FaucetWebMethod::Get));
}

void test_removed_or_legacy_business_aliases_are_not_registered() {
    const char* removedPaths[] = {
        "/api/faucet/records/calibration",
        "/faucet/records/calibration",
        "/api/faucet/records/trace-calibration",
        "/api/faucet/records/trace-save",
        "/api/faucet/records/trace-delete",
        "/api/faucet/calibration",
        "/api/faucet/records/latest/calibration",
    };

    for (const char* path : removedPaths) {
        TEST_ASSERT_FALSE_MESSAGE(hasAnyRoute(path), path);
        TEST_ASSERT_FALSE_MESSAGE(faucetWebRouteAllowed(path), path);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_routes_fit_esp32base_default_route_capacity);
    RUN_TEST(test_routes_do_not_register_remote_water_control_paths);
    RUN_TEST(test_route_whitelist_rejects_unknown_and_dangerous_control_aliases);
    RUN_TEST(test_navigation_pages_use_requested_order_and_labels);
    RUN_TEST(test_hidden_pages_and_assets_are_registered_without_navigation_titles);
    RUN_TEST(test_business_api_routes_use_explicit_methods);
    RUN_TEST(test_business_write_routes_are_post_only);
    RUN_TEST(test_removed_or_legacy_business_aliases_are_not_registered);
    return UNITY_END();
}
