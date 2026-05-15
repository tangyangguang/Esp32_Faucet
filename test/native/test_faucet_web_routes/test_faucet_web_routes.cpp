#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstdio>
#include <cstring>

using namespace faucet;

void test_routes_fit_esp32base_default_route_capacity() {
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base());
    TEST_ASSERT_LESS_OR_EQUAL_size_t(kFaucetWebMaxRoutes, faucetWebRouteCount());
    TEST_ASSERT_EQUAL_size_t(13, faucetWebRouteCount());
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

void test_route_whitelist_rejects_unknown_and_dangerous_control_aliases() {
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/start"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/stop"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/pause"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/resume"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/water/start"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/water/stop"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/export"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/unknown"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("api/faucet/status"));
}

void test_dual_method_routes_are_merged_to_any() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool foundPresets = false;
    bool foundFilters = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/api/faucet/presets") == 0) {
            foundPresets = routes[i].method == FaucetWebMethod::Any;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/filters") == 0) {
            foundFilters = routes[i].method == FaucetWebMethod::Any;
        }
    }

    TEST_ASSERT_TRUE(foundPresets);
    TEST_ASSERT_TRUE(foundFilters);
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/config"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/api/faucet/records/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/latest/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/config"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/records"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/logs"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/calibration"));
}

void test_filter_edit_route_is_hidden_from_navigation() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool found = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/faucet/filters/edit") == 0) {
            found = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Api &&
                    routes[i].title == nullptr;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_filter_forms_use_registered_api_endpoints() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool foundFilters = false;
    bool foundReset = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/api/faucet/filters") == 0) {
            foundFilters = routes[i].method == FaucetWebMethod::Any && routes[i].kind == FaucetWebRouteKind::Api &&
                           routes[i].title == nullptr;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/filters/reset") == 0) {
            foundReset = routes[i].method == FaucetWebMethod::Post && routes[i].kind == FaucetWebRouteKind::Api &&
                         routes[i].title == nullptr;
        }
    }
    TEST_ASSERT_TRUE(foundFilters);
    TEST_ASSERT_TRUE(foundReset);
}

void test_presets_page_is_available_in_navigation() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool found = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/faucet/presets") == 0) {
            found = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Page &&
                    std::strcmp(routes[i].title, "预设") == 0;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void test_records_page_and_calibration_api_are_available() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool found = false;
    bool foundApi = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/faucet/records") == 0) {
            found = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Page &&
                    std::strcmp(routes[i].title, "记录") == 0;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/records/calibration") == 0) {
            foundApi = routes[i].method == FaucetWebMethod::Post && routes[i].kind == FaucetWebRouteKind::Api &&
                       routes[i].title == nullptr;
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(foundApi);
}

void test_web_page_source_has_no_remote_water_control_forms() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[70000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NULL(std::strstr(buffer, "action='/api/faucet/start'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "action=\"/api/faucet/start\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "action='/api/faucet/stop'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "action=\"/api/faucet/stop\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "href='/api/faucet/start'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "href=\"/api/faucet/start\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "href='/api/faucet/stop'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "href=\"/api/faucet/stop\""));
}

void test_web_page_source_contains_expected_ui_improvements() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[80000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "presetTypeChanged"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "type='hidden' name='return' value='/faucet/presets'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "page-current"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "末页"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filters-table"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action='/api/faucet/filters'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action='/api/faucet/filters/reset'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action='/api/faucet/records/calibration'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "/api/faucet/calibration"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "量杯实际水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已用天数 (天)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "data-filter-start"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "开始时间"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "出水量 (L)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "daily-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最近 14 天出水趋势"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "未知时间出水量"));
}

void test_app_config_source_uses_clear_business_labels_and_help() {
    FILE* file = std::fopen("src/app/FaucetAppConfig.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[24000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "出水系统参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "确认页超时"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最大出水时长"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "无流量判定超时"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存后需重启，重启后重新探测 LCD。"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_routes_fit_esp32base_default_route_capacity);
    RUN_TEST(test_routes_do_not_register_remote_water_control_paths);
    RUN_TEST(test_route_whitelist_rejects_unknown_and_dangerous_control_aliases);
    RUN_TEST(test_dual_method_routes_are_merged_to_any);
    RUN_TEST(test_filter_edit_route_is_hidden_from_navigation);
    RUN_TEST(test_filter_forms_use_registered_api_endpoints);
    RUN_TEST(test_presets_page_is_available_in_navigation);
    RUN_TEST(test_records_page_and_calibration_api_are_available);
    RUN_TEST(test_web_page_source_has_no_remote_water_control_forms);
    RUN_TEST(test_web_page_source_contains_expected_ui_improvements);
    RUN_TEST(test_app_config_source_uses_clear_business_labels_and_help);
    return UNITY_END();
}
