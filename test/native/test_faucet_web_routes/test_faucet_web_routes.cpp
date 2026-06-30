#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstring>

using namespace faucet;

namespace {

const FaucetWebRoute* routeFor(const char* path, FaucetWebMethod method) {
    if (!path) {
        return nullptr;
    }
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, path) == 0 && routes[i].method == method) {
            return &routes[i];
        }
    }
    return nullptr;
}

bool routeExists(const char* path, FaucetWebMethod method) {
    return routeFor(path, method) != nullptr;
}

bool pathRegistered(const char* path) {
    return routeFor(path, FaucetWebMethod::Get) || routeFor(path, FaucetWebMethod::Post);
}

}  // namespace

void test_routes_fit_esp32base_default_route_capacity() {
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base());
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kFaucetWebMaxRoutes, faucetWebRouteCount());
}

void test_routes_do_not_register_remote_water_control_paths() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/api/faucet/water/"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/water/"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/start"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/stop"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/pause"), routes[i].path);
        TEST_ASSERT_NULL_MESSAGE(std::strstr(routes[i].path, "/resume"), routes[i].path);
    }

    TEST_ASSERT_FALSE(pathRegistered(nullptr));
    TEST_ASSERT_FALSE(pathRegistered(""));
    TEST_ASSERT_FALSE(pathRegistered("api/faucet/status"));
    TEST_ASSERT_FALSE(pathRegistered("/api/faucet/start"));
    TEST_ASSERT_FALSE(pathRegistered("/api/faucet/stop"));
    TEST_ASSERT_FALSE(pathRegistered("/api/faucet/water/start"));
}

void test_navigation_routes_keep_titles_and_hidden_routes_do_not() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    const char* navPaths[] = {
        "/index", "/faucet/records", "/faucet/stats", "/faucet/presets", "/faucet/filters", "/faucet/calibration"};
    const char* navTitles[] = {"首页", "记录", "统计", "预设", "滤芯", "校准"};

    for (std::size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING(navPaths[i], routes[i].path);
        TEST_ASSERT_EQUAL(FaucetWebMethod::Get, routes[i].method);
        TEST_ASSERT_EQUAL_STRING(navTitles[i], routes[i].title);
    }

    const char* hiddenGetRoutes[] = {
        "/faucet/app.css",
        "/faucet/calibration/flow",
        "/faucet/filters/edit",
        "/faucet/records/detail",
        "/faucet/calibration/detail",
    };

    for (const char* path : hiddenGetRoutes) {
        const FaucetWebRoute* route = routeFor(path, FaucetWebMethod::Get);
        TEST_ASSERT_NOT_NULL_MESSAGE(route, path);
        TEST_ASSERT_NULL_MESSAGE(route->title, path);
    }
}

void test_business_write_routes_are_post_only_where_required() {
    const char* writePaths[] = {
        "/faucet/calibration",
        "/faucet/calibration/flow",
        "/api/faucet/presets",
        "/api/faucet/filters",
        "/api/faucet/filters/reset",
    };

    for (const char* path : writePaths) {
        TEST_ASSERT_TRUE_MESSAGE(routeExists(path, FaucetWebMethod::Post), path);
    }

    TEST_ASSERT_TRUE(routeExists("/faucet/calibration", FaucetWebMethod::Get));
    TEST_ASSERT_TRUE(routeExists("/faucet/calibration/flow", FaucetWebMethod::Get));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/filters/reset", FaucetWebMethod::Get));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/status", FaucetWebMethod::Post));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/today", FaucetWebMethod::Post));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/records", FaucetWebMethod::Post));
    TEST_ASSERT_FALSE(routeExists("/api/faucet/stats", FaucetWebMethod::Post));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_routes_fit_esp32base_default_route_capacity);
    RUN_TEST(test_routes_do_not_register_remote_water_control_paths);
    RUN_TEST(test_navigation_routes_keep_titles_and_hidden_routes_do_not);
    RUN_TEST(test_business_write_routes_are_post_only_where_required);
    return UNITY_END();
}
