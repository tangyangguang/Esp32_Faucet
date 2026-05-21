#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstdio>
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
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/records/calibration"));
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
    bool foundCalibrationPage = false;
    bool foundApi = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/faucet/records") == 0) {
            found = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Page &&
                    std::strcmp(routes[i].title, "记录") == 0;
        }
        if (std::strcmp(routes[i].path, "/faucet/records/calibration") == 0) {
            foundCalibrationPage = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Api &&
                                   routes[i].title == nullptr;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/records/calibration") == 0) {
            foundApi = routes[i].method == FaucetWebMethod::Post && routes[i].kind == FaucetWebRouteKind::Api &&
                       routes[i].title == nullptr;
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(foundCalibrationPage);
    TEST_ASSERT_TRUE(foundApi);
}

void test_web_page_source_has_no_remote_water_control_forms() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[100000]{};
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
    static char buffer[100000]{};
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "href='/faucet/records/calibration'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "/api/faucet/calibration"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "量杯实际水量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendFmt(\"<section class='panel'><h3>量杯实际水量</h3>\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendChunk(\"<section class='panel'><h3>量杯实际水量</h3>\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已用天数 (天)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "data-filter-start"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "开始时间"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "出水量 (L)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "daily-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最近 30 天出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-cards"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "disabled-row"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "duration-key"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "duration-bar"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "过去 30 天日均"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "按预设分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "--bg:#fff"));
    TEST_ASSERT_NULL(std::strstr(buffer, "--bg:#f7f9f8"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendFmt(\"<span>流量：已用 %s</span>\", usedFlow);"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "建议 %lu 天"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最长 %lu 天"));
    TEST_ASSERT_NULL(std::strstr(buffer, "filter.lifeMl == 0 ? \"未设置\" : lifeFlow"));
    TEST_ASSERT_NULL(std::strstr(buffer, "未设置流量寿命"));
    TEST_ASSERT_NULL(std::strstr(buffer, "未知时间出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendPageStart(\"智能出水\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h2>当前状态</h2><div class='status-grid'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "本地显示"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "local-display-card"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "white-space:pre"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "运行状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "屏幕状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "local-display-meta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "char buffer[512]"));
    TEST_ASSERT_NULL(std::strstr(buffer, ".local-display-card{grid-column:1/-1;max-width:320px}"));
    TEST_ASSERT_NULL(std::strstr(buffer, "仅设备按键操作"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCardClass(\"运行状态\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h2>本次任务</h2><div class='metric-grid'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h2>安全状态</h2><div class='metric-grid'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h2>今日概览</h2><div class='metric-grid'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-progress-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "status-ok"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "status-warn"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "status-error"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "目标值"));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地页面"));
    TEST_ASSERT_NULL(std::strstr(buffer, "主显示"));
    TEST_ASSERT_NULL(std::strstr(buffer, "辅助提示"));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地面板提示"));
    TEST_ASSERT_NULL(std::strstr(buffer, "formatHomeMainDisplay"));
    TEST_ASSERT_NULL(std::strstr(buffer, "formatHomeAuxiliaryDisplay"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h2>状态</h2><div class='metric-grid'>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h2>出水详情</h2><div class='metric-grid'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "流量计校准系数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "阀门状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "设备不在待机状态，请回到待机后再保存配置。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>目标值</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>诊断</th>"));
}

void test_main_source_renders_live_display_frame_for_web() {
    FILE* file = std::fopen("src/main.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[64000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucet::FaucetDisplayStatus currentDisplayStatus()"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucet::DisplayPresenter awakePresenter(0)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "return g_lastDisplayFrame;"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "流量计校准系数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "脉冲/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "每升水对应的脉冲数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "流量计脉冲系数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "高级救援参数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "pulse/ml"));
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
    RUN_TEST(test_main_source_renders_live_display_frame_for_web);
    RUN_TEST(test_app_config_source_uses_clear_business_labels_and_help);
    return UNITY_END();
}
