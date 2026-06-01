#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstdio>
#include <cstring>

using namespace faucet;

void test_routes_fit_esp32base_default_route_capacity() {
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base());
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base(21));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(21, faucetWebRouteCount());
    TEST_ASSERT_EQUAL_size_t(21, faucetWebRouteCount());
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

void test_navigation_pages_use_requested_order_and_labels() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    const char* expectedPaths[] = {
        "/index", "/faucet/records", "/faucet/calibration", "/faucet/stats", "/faucet/presets", "/faucet/filters"};
    const char* expectedTitles[] = {"首页", "记录", "校准", "统计", "预设", "滤芯"};

    for (std::size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING(expectedPaths[i], routes[i].path);
        TEST_ASSERT_EQUAL(FaucetWebMethod::Get, routes[i].method);
        TEST_ASSERT_EQUAL(FaucetWebRouteKind::Page, routes[i].kind);
        TEST_ASSERT_EQUAL_STRING(expectedTitles[i], routes[i].title);
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
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/index"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/api/faucet/today"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/unknown"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("api/faucet/status"));
}

void test_business_api_routes_use_explicit_methods() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool foundPresetsGet = false;
    bool foundPresetsPost = false;
    bool foundFiltersGet = false;
    bool foundFiltersPost = false;
    bool foundRecordsGet = false;
    bool foundRecordsPost = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/api/faucet/presets") == 0) {
            foundPresetsGet = foundPresetsGet || routes[i].method == FaucetWebMethod::Get;
            foundPresetsPost = foundPresetsPost || routes[i].method == FaucetWebMethod::Post;
            TEST_ASSERT_NOT_EQUAL(FaucetWebMethod::Any, routes[i].method);
        }
        if (std::strcmp(routes[i].path, "/api/faucet/filters") == 0) {
            foundFiltersGet = foundFiltersGet || routes[i].method == FaucetWebMethod::Get;
            foundFiltersPost = foundFiltersPost || routes[i].method == FaucetWebMethod::Post;
            TEST_ASSERT_NOT_EQUAL(FaucetWebMethod::Any, routes[i].method);
        }
        if (std::strcmp(routes[i].path, "/api/faucet/records") == 0) {
            foundRecordsGet = foundRecordsGet || routes[i].method == FaucetWebMethod::Get;
            foundRecordsPost = foundRecordsPost || routes[i].method == FaucetWebMethod::Post;
            TEST_ASSERT_NOT_EQUAL(FaucetWebMethod::Any, routes[i].method);
        }
    }

    TEST_ASSERT_TRUE(foundPresetsGet);
    TEST_ASSERT_TRUE(foundPresetsPost);
    TEST_ASSERT_TRUE(foundFiltersGet);
    TEST_ASSERT_TRUE(foundFiltersPost);
    TEST_ASSERT_TRUE(foundRecordsGet);
    TEST_ASSERT_TRUE(foundRecordsPost);
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/config"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/records/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/trace-calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/trace-save"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/trace-delete"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/records/detail"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/calibration/detail"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/latest/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/config"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/records"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/logs"));
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

void test_app_css_route_is_hidden_from_navigation() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool found = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/faucet/app.css") == 0) {
            found = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Api &&
                    routes[i].title == nullptr;
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/app.css"));
}

void test_filter_forms_use_registered_api_endpoints() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool foundFilters = false;
    bool foundReset = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/api/faucet/filters") == 0) {
            foundFilters = foundFilters || (routes[i].method == FaucetWebMethod::Post &&
                                            routes[i].kind == FaucetWebRouteKind::Api && routes[i].title == nullptr);
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
    bool foundCalibrationPost = false;
    bool foundCalibrationDetail = false;
    bool foundApi = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/faucet/records") == 0) {
            found = routes[i].method == FaucetWebMethod::Get && routes[i].kind == FaucetWebRouteKind::Page &&
                    std::strcmp(routes[i].title, "记录") == 0;
        }
        if (std::strcmp(routes[i].path, "/faucet/calibration") == 0) {
            foundCalibrationPage = foundCalibrationPage ||
                                   (routes[i].method == FaucetWebMethod::Get &&
                                   routes[i].kind == FaucetWebRouteKind::Page &&
                                   std::strcmp(routes[i].title, "校准") == 0);
            foundCalibrationPost = foundCalibrationPost ||
                                   (routes[i].method == FaucetWebMethod::Post &&
                                    routes[i].kind == FaucetWebRouteKind::Api &&
                                    routes[i].title == nullptr);
        }
        if (std::strcmp(routes[i].path, "/faucet/calibration/detail") == 0) {
            foundCalibrationDetail = routes[i].method == FaucetWebMethod::Get &&
                                     routes[i].kind == FaucetWebRouteKind::Api &&
                                     routes[i].title == nullptr;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/records") == 0) {
            foundApi = foundApi || (routes[i].method == FaucetWebMethod::Post &&
                                    routes[i].kind == FaucetWebRouteKind::Api && routes[i].title == nullptr);
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(foundCalibrationPage);
    TEST_ASSERT_TRUE(foundCalibrationPost);
    TEST_ASSERT_TRUE(foundCalibrationDetail);
    TEST_ASSERT_TRUE(foundApi);
}

void test_web_page_source_has_no_remote_water_control_forms() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
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

void test_web_page_source_links_cacheable_app_css() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "FAUCET_WEB_CSS_VERSION"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "href='/faucet/app.css?v="));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "beginResponse(200, \"text/css; charset=utf-8\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "handleAppCss"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "setHeadExtraCallback(sendAppStylesheetLink)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendResponseHeader(\"Cache-Control\", \"public, max-age=86400\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendResponseHeader(\"X-Content-Type-Options\", \"nosniff\")"));
    TEST_ASSERT_NULL(std::strstr(buffer, "setHeadExtraCallback(sendAppStyles)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendChunk(\"<style>\")"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendChunk(\"</style>\")"));

    const char* handler = std::strstr(buffer, "void handleAppCss()");
    TEST_ASSERT_NOT_NULL(handler);
    const char* cacheHeader = std::strstr(handler, "sendResponseHeader(\"Cache-Control\", \"public, max-age=86400\")");
    const char* nosniffHeader = std::strstr(handler, "sendResponseHeader(\"X-Content-Type-Options\", \"nosniff\")");
    const char* beginResponse = std::strstr(handler, "beginResponse(200, \"text/css; charset=utf-8\"");
    TEST_ASSERT_NOT_NULL(cacheHeader);
    TEST_ASSERT_NOT_NULL(nosniffHeader);
    TEST_ASSERT_NOT_NULL(beginResponse);
    TEST_ASSERT_TRUE(cacheHeader < beginResponse);
    TEST_ASSERT_TRUE(nosniffHeader < beginResponse);
}

void test_web_page_source_contains_expected_ui_improvements() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "input.secondary:hover,input.secondary:focus-visible"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "input.secondary:hover,input.secondary:focus-visible{background:#10574e;border-color:#10574e;color:#fff}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".btn-link:hover,.btn-link:focus-visible"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".row-actions a:hover,.row-actions a:focus-visible"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "color:#fff"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action='/api/faucet/records'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='calibrate'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"/faucet/calibration\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "href='/faucet/records/calibration'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "/api/faucet/calibration"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实际出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "下次预设"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预计 %luP/L · %luP"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预计 %luP/L · %luP · 约 %s"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "稳态估算 %s · %luP · %luP/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "预计 %s · %luP · 稳态 %.2fP/s"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预计约 %s"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "缺少近期平均流速"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "缺少近期流速"));
    TEST_ASSERT_NULL(std::strstr(buffer, "缺少稳态脉冲"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<span>全程平均 %luP/L</span><span>预计 %luP</span>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "时间预设 · 不估算脉冲"));
    TEST_ASSERT_NULL(std::strstr(buffer, "暂无时长估算"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action=select_previous"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action=select_next"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSelectPreset("));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "nextPresetControl"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendMachineStatusNoteOnlyItem(\"meteringParams\", \"计量参数\", meteringParams)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动 %luP · %luml / 稳态 %luP/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "currentFlowValue"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "currentFlowMlPerMin"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "recentAverageFlowMlPerMin"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "L/min · 近期平均"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetFlowMeta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetFlowLitersPerMin"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "return n>0?"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "L/min"));
    TEST_ASSERT_NULL(std::strstr(buffer, "faucetToggle('currentFlowCard',shown)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "body{max-width:1120px"));
    TEST_ASSERT_NULL(std::strstr(buffer, "body{max-width:1280px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>时间</th><th>模式</th><th>目标</th><th>出水</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>用时</th><th>流速</th><th>全程平均</th><th>总脉冲</th><th>结果</th><th>操作</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>全程平均 P/L</th><th>计量方案</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>脉冲/升</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "formatWaterRecordListTime(records[i], startTime"));
    TEST_ASSERT_NULL(std::strstr(buffer, "pulse-total\">总%luP"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-total-cell"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "%luP"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<strong>%luP/L</strong>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "status-pill status-ok'>已校准</span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "recordFlowMlPerMin"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "平均流速"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最高流速"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "稳态流速"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "stablePulsePerLiter"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "std::uint32_t requestedPageSize = kDefaultRecordPageSize"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "std::uint32_t requestedPageNo = 0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "page = requestedPageNo - 1"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sizes[] = {10, 15, 20, 30, 50}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='pageNo'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "value='跳转'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".page-size select,.page-size input{height:32px;min-height:32px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".page-size input[name=pageNo]{width:58px;text-align:center}"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<span>开始</span><input type='date' name='startDate'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<span>结束</span><input type='date' name='endDate'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "&startDate=%s&endDate=%s"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h3>脉冲明细缓存</h3>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "明细条数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "records-diagnostic-strip"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "metering-status-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sample-coverage-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-cache-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "saved-trace-diagnostic"));
    TEST_ASSERT_NULL(std::strstr(buffer, "trace-storage-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "计量状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "计量方案"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动脉冲数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "findRecordMeteringSnapshot"));
    TEST_ASSERT_NULL(std::strstr(buffer, "g_context.recordMeteringSnapshots->findAny"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本覆盖"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "生成计量参数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h3>计量方案生成</h3>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "生成参数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h3>参数生成</h3>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "生成候选参数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "明细存储"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "RAM 最近明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "临时缓存"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已保存明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "控制P/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "当前方案ID"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "当前方案</span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "ID <b>#%lu</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendMeteringSnapshotLabel"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "稳态P/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动脉冲数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动水量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动等效"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "可生成样本"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".diagnostic-metric strong{display:block;color:var(--text);font-size:14px;line-height:1.2;font-weight:650;font-variant-numeric:tabular-nums;white-space:nowrap;overflow-wrap:normal}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已保存明细 <b>%u条</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已测容量 <b>%u条</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "还需 <b>%s</b>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "建议补偿"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "拟合误差"));
    TEST_ASSERT_NULL(std::strstr(buffer, "候选已生成"));
    const char* statusPanel = std::strstr(buffer, "void sendSegmentedMeteringPanel()");
    TEST_ASSERT_NOT_NULL(statusPanel);
    const char* tracePanel = std::strstr(statusPanel, "void sendPulseTraceCachePanel()");
    TEST_ASSERT_NOT_NULL(tracePanel);
    char meteringStatusPanel[12000]{};
    const std::size_t statusPanelLen = static_cast<std::size_t>(tracePanel - statusPanel);
    TEST_ASSERT_LESS_THAN(sizeof(meteringStatusPanel), statusPanelLen + 1);
    std::memcpy(meteringStatusPanel, statusPanel, statusPanelLen);
    TEST_ASSERT_NULL(std::strstr(meteringStatusPanel, "样本不足"));
    TEST_ASSERT_NULL(std::strstr(meteringStatusPanel, "可生成"));
    TEST_ASSERT_NULL(std::strstr(meteringStatusPanel, "loadCandidate"));
    TEST_ASSERT_NOT_NULL(std::strstr(meteringStatusPanel, "当前启用"));
    TEST_ASSERT_NOT_NULL(std::strstr(meteringStatusPanel, "出水记录 <b>%lu</b> 条"));
    TEST_ASSERT_NULL(std::strstr(buffer, "最近校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最近明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "RAM 数据点"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "RAM 占用 <b>%s</b>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<span><b>%u%%</b></span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "单条上限 <b>%lu 点</b>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "上限能力"));
    TEST_ASSERT_NULL(std::strstr(buffer, "设备存储上限"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已保存脉冲明细已达上限"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "设备存储明细文件异常"));
    TEST_ASSERT_NULL(std::strstr(buffer, "永久保存"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='action' value='delete_legacy'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "清理旧版明细文件"));
    TEST_ASSERT_NULL(std::strstr(buffer, "delete_legacy"));
    TEST_ASSERT_NULL(std::strstr(buffer, "%s / %s · %u%%"));
    TEST_ASSERT_NULL(std::strstr(buffer, "约 %lu 点 / 最多 %lu 条"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCard(\"内存占用\", used)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "样本总数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "原始轨迹缓存"));
    TEST_ASSERT_NULL(std::strstr(buffer, "脉冲轨迹"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h3>当前计量参数</h3>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "records-top-grid"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".records-top-grid .records-diagnostic-panel"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".diagnostic-metric-grid.three"));
    TEST_ASSERT_NULL(std::strstr(buffer, ".trace-storage-groups"));
    TEST_ASSERT_NULL(std::strstr(buffer, "metric-grid calibration-param-grid"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "calibration-param-layout"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "calibration-slot-table"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "scheme-edit-form"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "scheme-edit-section"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "核心计量参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "当前启用方案：保存后会立即影响后续出水估算。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".scheme-edit-grid{display:grid;grid-template-columns:repeat(12,1fr)"));
    TEST_ASSERT_NULL(std::strstr(buffer, ".calibration-slot-form{display:grid;grid-template-columns"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h3>候选方案</h3>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "暂无候选方案"));
    TEST_ASSERT_NULL(std::strstr(buffer, "手工新建方案"));
    const char* recordsHandler = std::strstr(buffer, "void handleRecordsPage() {");
    const char* calibrationHandler = std::strstr(buffer, "void handleCalibrationPage() {");
    TEST_ASSERT_NOT_NULL(recordsHandler);
    TEST_ASSERT_NOT_NULL(calibrationHandler);
    const char* recordsDiagnostic = std::strstr(recordsHandler, "records-diagnostic-strip");
    const char* recordsCalibrateLink = std::strstr(recordsHandler, "href='/faucet/calibration'");
    TEST_ASSERT_TRUE(recordsDiagnostic == nullptr || recordsDiagnostic > calibrationHandler);
    TEST_ASSERT_TRUE(recordsCalibrateLink == nullptr || recordsCalibrateLink > calibrationHandler);
    TEST_ASSERT_NOT_NULL(std::strstr(calibrationHandler, "records-diagnostic-strip"));
    TEST_ASSERT_NOT_NULL(std::strstr(calibrationHandler, "sendSegmentedMeteringPanel"));
    TEST_ASSERT_NOT_NULL(std::strstr(calibrationHandler, "sendPulseTraceCachePanel"));
    const char* pulseTracePanel = std::strstr(calibrationHandler, "sendPulseTraceCachePanel");
    const char* generationPanel = pulseTracePanel ? std::strstr(pulseTracePanel, "sendCalibrationGenerationPanel") : nullptr;
    TEST_ASSERT_NOT_NULL(generationPanel);
    TEST_ASSERT_TRUE(pulseTracePanel < generationPanel);
    TEST_ASSERT_NULL(std::strstr(calibrationHandler, "<a class='btn-link' href='/faucet/records'>历史记录</a>"));
    const char* registerHandler = std::strstr(buffer, "bool registerFaucetWeb() {");
    TEST_ASSERT_NOT_NULL(registerHandler);
    TEST_ASSERT_NOT_NULL(std::strstr(registerHandler, "routes[i].kind == FaucetWebRouteKind::Page && routes[i].method == FaucetWebMethod::Get"));
    TEST_ASSERT_NOT_NULL(std::strstr(registerHandler, "Esp32BaseWeb::addRoute(routes[i].path, toBaseMethod(routes[i].method), handlerFor(routes[i]))"));
    const char* handlerForSource = std::strstr(buffer, "Esp32BaseWeb::Handler handlerFor(const FaucetWebRoute& route)");
    TEST_ASSERT_NOT_NULL(handlerForSource);
    TEST_ASSERT_NOT_NULL(std::strstr(handlerForSource, "return handleCalibrationPost;"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-badge"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已存明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "trace-head-meter"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-detail-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-line"));
    TEST_ASSERT_NULL(std::strstr(buffer, "pulse-line-paused"));
    TEST_ASSERT_NULL(std::strstr(buffer, "raw-line-paused"));
    TEST_ASSERT_NULL(std::strstr(buffer, "pulse-dot-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "raw-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pause-window"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pause-boundary"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "legend-raw"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "legend-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "暂停区间"));
    TEST_ASSERT_NULL(std::strstr(buffer, "非运行状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "volume-line"));
    TEST_ASSERT_NULL(std::strstr(buffer, "volume-dot"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "legend-volume"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "累计估算出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "volume-line-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "const char* volumeLineClass"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "WaterPulseTraceState::Paused ? \"volume-line volume-line-paused\" : \"volume-line\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "legend-cum-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "脉冲趋势"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "id='pulse-trend'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetLoadTraceChart"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "old.replaceWith(next);history.replaceState(null,'',a.href);"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-frequency"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-frequency-label'>聚合频率"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".trace-frequency a.page-current"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-y-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-raw-y-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-x-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "有效最高 %lu 脉冲 / 原始最高 %lu 边沿"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>有效脉冲</th><td>%lu</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>原始边沿</th><td>%lu</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>被过滤边沿</th><td>%lu</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>有效率</th><td>%lu.%lu%%</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>最高频率</th><td>有效 %lu 脉冲/%lus，原始 %lu 边沿/%lus</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>暂停次数</th><td>%u</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>暂停总时长</th><td>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendDurationSeconds(pauseTotalUs)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>暂停后恢复</th><td>%s</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "暂停后恢复出水，属于多段出水，不参与启动段和分段校准拟合。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "effectivePulseCount(*trace, samples, trace->sampleCount)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketRunningPulseDelta"));
    TEST_ASSERT_NULL(std::strstr(buffer, "bucketOnlyHasRunningSamples"));
    TEST_ASSERT_NULL(std::strstr(buffer, "bucketRunning ? bucketRunningPulseDelta(samples, trace->sampleCount, buckets[i]) : 0"));
    TEST_ASSERT_NULL(std::strstr(buffer, "bucketRunning ? buckets[i].rawEdgeDelta : 0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "const std::uint32_t chartDelta = buckets[i].rawEdgeDelta;"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "const std::uint32_t rawDelta = buckets[i].rawEdgeDelta;"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "rawCumulative"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "prevPulseValid = false"));
    TEST_ASSERT_NULL(std::strstr(buffer, "bucketRunning ? \"pulse-line\" : \"pulse-line pulse-line-paused\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "prevX = x;"));
    TEST_ASSERT_NULL(std::strstr(buffer, "prevCumX = x;"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketsToShow[] = {1, 2, 3, 4, 5}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucket == bucketSeconds ? \"btn-link page-current\" : \"btn-link\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "aria-current='%s'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "onclick='return faucetLoadTraceChart(this)'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "xLabelCount"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "maxEndSec <= 12 ? std::min<std::uint32_t>(maxEndSec + 1U, 13U) : 11U"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "detail-data"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<section class='panel detail-data'><div class='panel-head'><h3>原始明细</h3>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "显示原始明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "显示所有明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "导出所有明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "target='_blank'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "raw=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "all=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kRawTracePreviewEdgeCount = 30"));
    TEST_ASSERT_NULL(std::strstr(buffer, "kRawTracePreviewLastSecond"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "rawTracePreviewSampleCount"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "rawTraceShowAll"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "beginResponse(200, \"text/plain; charset=utf-8\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "序号\\t距任务开始(us)\\t与上一边沿间隔(us)\\t是否有效\\t有效累计\\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "%lu\\t%lu\\t%lu\\t%s\\t%lu\\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".raw-edge-invalid"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "raw-edge-invalid"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "首个边沿"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "仅显示前 %lu 个原始边沿，共 %lu 行；完整明细请使用 all=1。\\n"));
    TEST_ASSERT_NULL(std::strstr(buffer, "仅显示 0秒 到 %lu秒"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "WaterPulseTraceState::PauseTimeout"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "waterResultAllowsCalibration(record.result)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "原始边沿 %lu 个，有效 %lu 个，过滤 %lu 个；当前预览前 %lu 个（有效 %lu 个）。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "默认展示前 %lu 个原始边沿"));
    TEST_ASSERT_NULL(std::strstr(buffer, "原始边沿共 %lu 个，当前展示 %lu 个。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "加载原始明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "下载文本"));
    TEST_ASSERT_NULL(std::strstr(buffer, "fetch(rawUrl,{cache:'no-store'})"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<pre id='rawTraceText' class='raw-trace-text'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "需要排查时再拉取纯文本"));
    TEST_ASSERT_NULL(std::strstr(buffer, "避免页面一次性生成大量表格"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<details open class='panel detail-data'>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<summary>查看明细数据</summary>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<table class='raw-trace-table'><tr><th>序号</th><th>距任务开始</th><th>与上一边沿间隔</th><th>是否有效</th><th>有效累计</th></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendDurationUs(samples[i].elapsedUs)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendDurationUs(intervalUs)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<tr><td>%lu秒</td><td>%u</td><td>%lu</td><td>%s</td></tr>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<tr><td>%lu-%lus</td><td>%luP</td><td>%luP</td><td>%s</td></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "inline-note"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "measured-note"));
    TEST_ASSERT_NULL(std::strstr(buffer, "record-more"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-cell"));
    TEST_ASSERT_NULL(std::strstr(buffer, "action='/api/faucet/records/trace-calibration'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "action='/api/faucet/records/trace-save'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "action='/api/faucet/records/trace-delete'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='save'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='delete'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "value='保存明细'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "删除已保存明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "确认删除这条已保存的脉冲明细？"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "saved=1&trace"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendTargetDeltaHint(records[i])"));
    TEST_ASSERT_NULL(std::strstr(buffer, "calibrated ? \"重校\" : \"校准\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "修改实测"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存实测"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测 "));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测 %luP/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "measuredPulsePerLiter"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "滤%luP"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>诊断</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "脉冲 %lu / 过滤 %lu / 系数 %.3f"));
    TEST_ASSERT_NULL(std::strstr(buffer, "量杯实际水量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "确认/校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准工作台"));
    TEST_ASSERT_NULL(std::strstr(buffer, "latest-record-table"));
    TEST_ASSERT_NULL(std::strstr(buffer, "latest-calibration-edit-row"));
    TEST_ASSERT_NULL(std::strstr(buffer, "最后一条出水记录"));
    TEST_ASSERT_NULL(std::strstr(buffer, "href='#confirm-volume'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<details id='confirm-volume' class='panel calibration-volume-panel'>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<summary>确认/校准容量</summary>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".sample-calibration-edit-row"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".sample-calibration-info"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".sample-calibration-inputs"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sample-volume-control"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".sample-volume-field .sample-volume-control{display:flex"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".sample-volume-control .unit-label{display:inline-flex"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "估算出水"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准记录"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<tr><th>出水信息</th><td>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实际出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测脉冲/升"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "估算差"));
    TEST_ASSERT_NULL(std::strstr(buffer, "控制参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "未修改"));
    TEST_ASSERT_NULL(std::strstr(buffer, "记录摘要"));
    TEST_ASSERT_NULL(std::strstr(buffer, "上次实测记录"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "量杯实测容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存校准容量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存容量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存重校"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存实测量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "saveRecordActualMeasurement"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "saveRamTraceToDevice(traceId"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "calibration?saved=actual"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetShowSampleCalibration"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSubmitSampleCalibration"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetReplaceCalibrationSection"));
    TEST_ASSERT_NULL(std::strstr(buffer, "fetch('/faucet/calibration?partial=latest'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='ajax' value='1'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "href='#confirm-latest-volume'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "applyCalibrationFromRecord(record, actualMl)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准已保存。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "不会修改当前计量方案"));
    TEST_ASSERT_NULL(std::strstr(buffer, "step='10' value='%lu'></label>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sample-volume-input-row"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "' step='1' value='"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<span class='unit-label'>ml</span></span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存后写入设备样本库"));
    TEST_ASSERT_NULL(std::strstr(buffer, "确认最后一条记录容量后会自动入库"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='saveTrace'"));
    const char* latestPanelSource = std::strstr(buffer, "void sendLatestCalibrationRecordPanel");
    TEST_ASSERT_NULL(latestPanelSource);
    const char* samplesPanelSource = std::strstr(buffer, "void sendCalibrationSamplesPanel");
    TEST_ASSERT_NOT_NULL(samplesPanelSource);
    const char* sampleRowSource = std::strstr(buffer, "void sendCalibrationSampleRow");
    TEST_ASSERT_NOT_NULL(sampleRowSource);
    const char* generationPanelSource = std::strstr(buffer, "void sendCalibrationGenerationPanel");
    TEST_ASSERT_NOT_NULL(generationPanelSource);
    TEST_ASSERT_TRUE(samplesPanelSource < generationPanelSource);
    char samplesPanel[20000]{};
    const std::size_t samplesLen = static_cast<std::size_t>(generationPanelSource - samplesPanelSource);
    TEST_ASSERT_LESS_THAN(sizeof(samplesPanel), samplesLen + 1);
    std::memcpy(samplesPanel, samplesPanelSource, samplesLen);
    TEST_ASSERT_NOT_NULL(std::strstr(samplesPanel,
                                     "<table class='calibration-sample-table'><tr><th>时间</th><th>目标</th><th>估算出水</th><th>实测容量</th><th>脉冲</th><th>前 %u 秒脉冲</th><th>稳态</th><th>来源</th><th>状态</th><th>操作</th></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(samplesPanel, "sampleSeconds"));
    TEST_ASSERT_NOT_NULL(std::strstr(samplesPanel, "min='1' max='60' step='1' value='%u'"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "name='traceSource' value='"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "name='trace' value='"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "sample-calibration-edit-row"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "faucetShowSampleCalibration"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "校准容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "重新校准"));
    TEST_ASSERT_NULL(std::strstr(sampleRowSource, "修改容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".volume-line{fill:none;stroke:#9aa7a9;stroke-width:1.5"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".volume-line-paused{stroke-dasharray:5 5;opacity:.55}"));
    TEST_ASSERT_NULL(std::strstr(buffer, ".volume-dot"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".legend-volume{background:#9aa7a9;opacity:.65}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kDefaultSamplePulseWindowSec = 10"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kMaxSamplePulseWindowSec = 60"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "firstSecondsPulseTotal"));
    TEST_ASSERT_NULL(std::strstr(samplesPanel, "\">查看</a>"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "sample-status-pills"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='action' value='save_latest_trace'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='action' value='delete_latest_trace'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "handleTraceSaveApi"));
    TEST_ASSERT_NULL(std::strstr(buffer, "handleSaveLatestTraceApi"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "明细文件"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='generate_segmented'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='save_generated_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='discard_generated_scheme'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='action' value='save_candidate_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='create_metering_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='edit_metering_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='enable_metering_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='disable_metering_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='delete_metering_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "value='保存修改'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "value='保存槽位'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='action' value='apply_segmented'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='action' value='restore_segmented'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存为新方案"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "放弃生成结果"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "生成结果"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "class='form-actions generated-result-actions'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".generated-result-actions{align-items:flex-end}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sample-coverage-compact"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "generated-scheme-layout"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "generated-estimator"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "试算目标水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sample-trial-modal"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetOpenSampleTrial"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "data-sample-trial='1'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预计出水时长"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预计脉冲总数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "data-startup-pulses"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "calibration-generation-settings"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "generated-residual-table"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "误差偏大"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "重新生成"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启用此方案"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "createdScheme"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetEstimateGeneratedScheme"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSubmitGenerationAction"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='ajax' value='1'"));
    const char* calibrationPost = std::strstr(buffer, "void handleCalibrationPost()");
    TEST_ASSERT_NOT_NULL(calibrationPost);
    const char* calibrationPostEnd = std::strstr(calibrationPost, "bool persistFilterConfig");
    TEST_ASSERT_NOT_NULL(calibrationPostEnd);
    const char* calibrationPostActionBuffer = std::strstr(calibrationPost, "char text[32]{};");
    TEST_ASSERT_NOT_NULL(calibrationPostActionBuffer);
    TEST_ASSERT_TRUE(calibrationPostActionBuffer < calibrationPostEnd);
    const char* recordsApi = std::strstr(buffer, "void handleRecordsApi()");
    TEST_ASSERT_NOT_NULL(recordsApi);
    const char* recordsApiEnd = std::strstr(recordsApi, "void handleStatsApi()");
    TEST_ASSERT_NOT_NULL(recordsApiEnd);
    const char* recordsApiActionBuffer = std::strstr(recordsApi, "char text[32]{};");
    TEST_ASSERT_NOT_NULL(recordsApiActionBuffer);
    TEST_ASSERT_TRUE(recordsApiActionBuffer < recordsApiEnd);
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "href='/faucet/calibration?scheme=new'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "href='/faucet/calibration?scheme=%lu'"));
    const char* schemeEditSource = std::strstr(buffer, "void sendMeteringSchemeEditPage");
    TEST_ASSERT_NOT_NULL(schemeEditSource);
    TEST_ASSERT_NOT_NULL(std::strstr(schemeEditSource, "scheme ? scheme->params : defaultMeteringParameters()"));
    TEST_ASSERT_NOT_NULL(std::strstr(schemeEditSource, "方案信息"));
    TEST_ASSERT_NOT_NULL(std::strstr(schemeEditSource, "核心计量参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(schemeEditSource, "适用条件"));
    TEST_ASSERT_NOT_NULL(std::strstr(schemeEditSource, "scheme-edit-actions"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启用只切换当前计量参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action='/faucet/calibration'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<span class='status-pill status-muted'>手动执行</span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "手动执行：只扫描满足有效样本条件的数据"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "生成参数：样本与拟合"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "出水估算：计量方案如何使用"));
    TEST_ASSERT_NULL(std::strstr(buffer, "参数说明与计算公式"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "有效样本条件"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "原始边沿未因超过单条上限而被截断"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "未发生暂停后恢复出水"));
    TEST_ASSERT_NULL(std::strstr(buffer, "样本来源"));
    TEST_ASSERT_NULL(std::strstr(buffer, "样本先记录原始脉冲明细，再用量杯实测容量校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测容量 = Vs + 稳态脉冲数 × 每脉冲毫升数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "估算出水量 = Vs + round((P - Ns) × 1000 / Ps)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "估算出水量 = round(脉冲数 / 控制P/ml) + 启动补偿"));
    TEST_ASSERT_NULL(std::strstr(buffer, "实测容量 ≈ 启动等效水量 + 稳态脉冲数 × 1000 / 稳态P/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "全程平均只做长期诊断"));
    TEST_ASSERT_NULL(std::strstr(buffer, "有临时样本未保存"));
    TEST_ASSERT_NULL(std::strstr(buffer, "请进入明细页保存后再生成"));
    TEST_ASSERT_NULL(std::strstr(buffer, "仍然生成候选"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h3>样本</h3>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h3>有效样本</h3>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "这里展示有脉冲明细的数据。RAM 临时样本重启会丢失；校准容量会先写入设备样本库"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "RAM 临时"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "待校准容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "容量已校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "state.resumedAfterPause = trace.resumedAfterPause"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<span class='status-pill status-warn'>暂停后恢复</span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "稳态失败"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "可参与生成"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "可入库"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "明细已截断"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "不入库"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "不可入库"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本已入库"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "容量已校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "来自校准容量记录"));
    const char* schemeListSource = std::strstr(buffer, "void sendCalibrationParameterPanels");
    const char* formulaPanelSource = std::strstr(buffer, "void sendMeteringSchemeEditPage");
    TEST_ASSERT_NOT_NULL(schemeListSource);
    TEST_ASSERT_NOT_NULL(formulaPanelSource);
    char schemeList[32000]{};
    const std::size_t schemeListLen = static_cast<std::size_t>(formulaPanelSource - schemeListSource);
    TEST_ASSERT_LESS_THAN(sizeof(schemeList), schemeListLen + 1);
    std::memcpy(schemeList, schemeListSource, schemeListLen);
    TEST_ASSERT_NULL(std::strstr(schemeList, "calibration-slot-form"));
    TEST_ASSERT_NULL(std::strstr(schemeList, "手工新建方案"));
    TEST_ASSERT_NULL(std::strstr(schemeList, "<h3>候选方案</h3>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "待校准容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "请在校准页的样本列表中校准这条记录的量杯实测容量。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "/faucet/calibration/detail"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "返回校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "returnTo' value='calibration'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "const char* detailPath = fromCalibration ? \"/faucet/calibration/detail\" : \"/faucet/records/detail\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "actualMlForSegmentedSample(*trace)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "actualMlForSegmentedSample(trace)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存为分段样本"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存并自动校准"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动阶段的等效脉冲/升"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动段 P/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已用天数 (天)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "data-filter-start"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "开始时间"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实际出水"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "daily-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最近 30 天出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "每日出水量 (L)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "stats-card-meta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "今日 %lu 次"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "日均 %.1f 次 · 总共 %lu 次"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "累计 %lu 次"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-y-tick"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "count-y-tick"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "count-line-halo"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "count-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "count-dot"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "count-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "每日次数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "countLabelY"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "std::snprintf(countText"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "y='250' transform='rotate(-45"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".count-line{fill:none;stroke:#acbbc1;stroke-width:1.15"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".count-dot{fill:#acbbc1;stroke:#fff;stroke-width:.6"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "r='1.7'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "transform='rotate(-45"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bar-label"));
    TEST_ASSERT_NULL(std::strstr(buffer, "count-row-label"));
    TEST_ASSERT_NULL(std::strstr(buffer, "count-row-zero"));
    TEST_ASSERT_NULL(std::strstr(buffer, "countLabelX"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最近 30 天分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "以下占比均按最近 30 天记录次数统计"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendCountVolumeDistributionRow"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "roundedPercent(count, totalCount)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<strong>%lu 次</strong><small>占 %lu%% · 合计 %s</small>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "P%u · %s · %s"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "preset.type == PresetType::Time ? \"时间\" : \"容量\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendVolumeDistributionRow"));
    TEST_ASSERT_NULL(std::strstr(buffer, "roundedPercent(volumeMl, totalMl)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "按容量段分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "按完成结果分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "0.5 L 以下"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "10 L 以上"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "正常完成"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "暂停超时"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "distribution-scope"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "border-radius:6px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".metric-card span{display:block;color:var(--muted);font-size:13px;font-weight:500"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".metric-card strong{display:block;color:var(--text);font-size:18px;line-height:1.2;font-weight:500}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "rx='1'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-cards"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "disabled-row"));
    TEST_ASSERT_NULL(std::strstr(buffer, "duration-key"));
    TEST_ASSERT_NULL(std::strstr(buffer, "duration-bar"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "过去 30 天日均"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "按预设分布"));
    TEST_ASSERT_NULL(std::strstr(buffer, "按时段分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "0.5 L 以下"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "0.5 - 2 L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "2 - 5 L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "5 - 10 L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "10 L 以上"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "--bg:#fbfcfb"));
    TEST_ASSERT_NULL(std::strstr(buffer, "--bg:#fff"));
    TEST_ASSERT_NULL(std::strstr(buffer, "--bg:#f7f9f8"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendFmt(\"<span>流量：已用 %s</span>\", usedFlow);"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "建议 %lu 天"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最长 %lu 天"));
    TEST_ASSERT_NULL(std::strstr(buffer, "filter.lifeMl == 0 ? \"未设置\" : lifeFlow"));
    TEST_ASSERT_NULL(std::strstr(buffer, "未设置流量寿命"));
    TEST_ASSERT_NULL(std::strstr(buffer, "未知时间出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "sendPageStart(\"首页\")"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendPageStart(\"智能出水\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h2>机器状态</h2>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-status"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-main"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-overview"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-task-grid"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-task-card"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-status-strip"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-status-item"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-status{padding:14px 16px;margin:0 0 14px;border-color:#d8e1e6;background:#fbfcfd}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-task-card{display:flex;flex-direction:column;justify-content:center;min-height:68px;padding:11px 12px;border:1px solid #dde6eb;border-radius:7px;background:#fff;box-shadow:0 1px 2px rgba(16,24,40,.025)}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-status-item{display:inline-flex;align-items:center"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "background:#f7f9fb;color:#66737c"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-hero-head{display:grid;grid-template-columns:max-content minmax(0,1fr);align-items:center;gap:14px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-context{display:flex;flex-direction:column;gap:6px;min-width:0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-alert{margin:0;color:#8a6f3d;font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".next-preset-control{display:grid;grid-template-columns:30px minmax(0,1fr) 30px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".preset-step{"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-progress-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:var(--muted);font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-task-card span{display:block;color:var(--muted);font-size:12px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-task-card small{display:block;margin-top:4px;color:var(--muted);font-size:11px;line-height:1.2;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-status-item{display:inline-flex;align-items:center;gap:5px;min-height:28px;padding:0 9px;border:1px solid #dce4ea;border-radius:999px;background:#f7f9fb;color:#66737c;font-size:12px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-status-note{color:#7a858e;font-size:11px;font-weight:400"));
    TEST_ASSERT_NULL(std::strstr(buffer, "align-items:baseline;gap:5px;min-height:28px"));
    TEST_ASSERT_NULL(std::strstr(buffer, "machine-kpis"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-hero"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-hero-head"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-context"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-alert"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "next-preset-control"));
    TEST_ASSERT_NULL(std::strstr(buffer, "machine-indicator"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "设备可用，等待按键启动"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "id='machineStatusNote'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "kpiRemaining"));
    TEST_ASSERT_NULL(std::strstr(buffer, "kpiElapsed"));
    TEST_ASSERT_NULL(std::strstr(buffer, "kpiResult"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "remainingValue"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "targetMeta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "outputMeta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "remainingMeta"));
    TEST_ASSERT_NULL(std::strstr(buffer, "elapsedValue"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "resultStatus"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"PWM\", valvePwmDuty"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "valvePwmDuty"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "valvePwmNote"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "s.valveDutyPercent"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "s.valveFullPowerSec+'s全功率→'+s.valveHoldDutyPercent+'%保持'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSetMaybe('targetMeta'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSet('outputMeta'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSet('remainingMeta'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "function faucetStatusNote"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "function faucetToggle"));
    TEST_ASSERT_NULL(std::strstr(buffer, "id='valvePill'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "var pill=document.getElementById('valvePill')"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-summary"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-records"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "id='todayOverview'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "fetch('/api/faucet/today'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "function updateFaucetTodayOverview"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "outerHTML"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "beginResponse(200, \"text/html; charset=utf-8\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "今日接水记录"));
    TEST_ASSERT_NULL(std::strstr(buffer, "今日总结"));
    TEST_ASSERT_NULL(std::strstr(buffer, "全部记录"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCard(\"今日次数\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCard(\"今日总量\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCard(\"总用时\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "overview.durationSec"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-summary-card"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-total-main"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-total-meta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-meta-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-meta-item"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "justify-content:flex-start"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".today-summary-label{display:block;color:var(--muted);font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".today-total-meta{display:flex;align-items:center;flex-wrap:wrap;gap:3px 8px;color:var(--muted);font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".today-meta-value{color:#52616b;font-weight:500"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "接水 "));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "用时 "));
    TEST_ASSERT_NULL(std::strstr(buffer, "共 %s 用时 %s"));
    TEST_ASSERT_NULL(std::strstr(buffer, "总用时 %s"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "today-record-table"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>开始</th><th>停止</th><th>用时</th><th>实际出水</th><th>预设目标</th><th>结果</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "%02lu:%02lu:%02lu"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "record-duration"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实际出水"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预设目标"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<td><strong>%s</strong></td>"));
    TEST_ASSERT_NULL(std::strstr(buffer, ".today-record-table strong"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "%lu 分 %lu 秒"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "预设 %u · %s"));
    TEST_ASSERT_NULL(std::strstr(buffer, "record-cell"));
    TEST_ASSERT_NULL(std::strstr(buffer, "record-label"));
    TEST_ASSERT_NULL(std::strstr(buffer, "record-value"));
    TEST_ASSERT_NULL(std::strstr(buffer, "today-record-list"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<div class='today-record-item'>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "record-time-range"));
    TEST_ASSERT_NULL(std::strstr(buffer, "开始 %s"));
    TEST_ASSERT_NULL(std::strstr(buffer, "停止 %s"));
    TEST_ASSERT_NULL(std::strstr(buffer, "用时 %s</span>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<strong>%s</strong><span class='record-preset'>%s</span>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "location.reload()"));
    TEST_ASSERT_NULL(std::strstr(buffer, "setInterval(updateFaucetHomeStatus"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "fetch('/api/faucet/status'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "var faucetIdlePollMs=10000"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "var faucetActivePollMs=1000"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "function scheduleFaucetHomeStatus"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "setTimeout(updateFaucetHomeStatus"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "shouldShowProgress"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "shouldShowProgress ? \"\" : \" style='display:none'\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-main compact"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "justify-content:center"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "id='machineState'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "screenStatus"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCard(\"最近一次\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCard(\"平均单次\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地显示"));
    TEST_ASSERT_NULL(std::strstr(buffer, "local-display-card"));
    TEST_ASSERT_NULL(std::strstr(buffer, "当前状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetToggle('machineStatusNote',s.state==='running')"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"screenStatus\", \"屏幕\", screenOn"));
    TEST_ASSERT_NULL(std::strstr(buffer, "local-display-meta"));
    TEST_ASSERT_NULL(std::strstr(buffer, ".local-display-card{grid-column:1/-1;max-width:320px}"));
    TEST_ASSERT_NULL(std::strstr(buffer, "仅设备按键操作"));
    TEST_ASSERT_NULL(std::strstr(buffer, "sendMetricCardClass(\"运行状态\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h2>本次任务</h2><div class='metric-grid'>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<h2>安全状态</h2><div class='metric-grid'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<h2>今日概览</h2>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-progress-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-progress-row"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-track"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "filter-progress-fill"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "if (filter.lifeMl > 0)"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"meteringParams\", \"计量参数\", meteringParams"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "'预计 '+e.fullRunPulsePerLiter+'P/L · '+e.pulseCount+'P"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "'稳态估算 '+faucetLiters(e.targetMl)+' · '+e.pulseCount+'P · '"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetTargetMeta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "阀门"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "设备不在待机状态，请回到待机后再保存配置。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>目标值</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>流速</th><th>全程平均</th><th>总脉冲</th><th>结果</th><th>操作</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>脉冲</th><th>轨迹</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>校准</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>诊断</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "StablePulseEstimateCache"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "latestRamTraceId"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "savedTraceStatsKey"));

    const char* detailHandler = std::strstr(buffer, "void handleRecordDetailPage() {");
    TEST_ASSERT_NOT_NULL(detailHandler);
    const char* sampleStatus = std::strstr(detailHandler, "<section class='panel sample-status-panel'><h3>样本状态</h3>");
    const char* detailOverview = std::strstr(detailHandler, "<section class='panel'><h3>明细概况</h3>");
    const char* pulseTrend = std::strstr(detailHandler, "<section id='pulse-trend' class='panel'><div class='panel-head'><h3>脉冲趋势</h3>");
    TEST_ASSERT_NOT_NULL(sampleStatus);
    TEST_ASSERT_NOT_NULL(detailOverview);
    TEST_ASSERT_NOT_NULL(pulseTrend);
    TEST_ASSERT_TRUE(sampleStatus < detailOverview);
    TEST_ASSERT_TRUE(sampleStatus < pulseTrend);
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动脉冲数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "目标全程平均 P/L"));
}

void test_record_calibration_api_saves_actual_without_segmented_generation() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* handler = std::strstr(buffer, "void handleRecordCalibrationApi() {");
    TEST_ASSERT_NOT_NULL(handler);
    const char* nextHandler = std::strstr(handler, "void handleTraceCalibrationApi() {");
    TEST_ASSERT_NOT_NULL(nextHandler);

    const char* findExisting = std::strstr(handler, "const bool calibrated = findRecordCalibration(record, calibration);");
    const char* readTrace = std::strstr(handler, "readCalibrationTraceFromRequest(trace, savedSource, traceId)");
    const char* defaultActual =
        std::strstr(handler, "const std::uint32_t defaultActualMl = calibrated ? calibration.actualMl : trace.record.volumeMl;");
    const char* saveToDevice = std::strstr(handler, "ensureCalibratedTraceSaved(savedSource, traceId, trace.record, actualMl)");
    const char* saveMeasurement = std::strstr(handler, "saveRecordActualMeasurement(record, actualMl)");
    const char* syncAfterSave =
        saveMeasurement ? std::strstr(saveMeasurement, "syncSegmentedCalibrationFromActual(record, actualMl)") : nullptr;
    const char* applyAfterSave =
        saveMeasurement ? std::strstr(saveMeasurement, "applySegmentedCalibrationFromAvailableSamples()") : nullptr;
    const char* traceSaveAfterMeasurement =
        saveMeasurement ? std::strstr(saveMeasurement, "autoSaveTraceAsSegmentedSample(record, actualMl)") : nullptr;
    const char* traceActualSync =
        saveMeasurement ? std::strstr(saveMeasurement, "syncTraceActualMeasurement(record, actualMl)") : nullptr;

    TEST_ASSERT_TRUE(readTrace != nullptr && readTrace < nextHandler);
    TEST_ASSERT_TRUE(findExisting != nullptr && findExisting < nextHandler);
    TEST_ASSERT_TRUE(defaultActual != nullptr && defaultActual < nextHandler);
    TEST_ASSERT_TRUE(saveToDevice != nullptr && saveToDevice < saveMeasurement);
    TEST_ASSERT_NULL(std::strstr(handler, "readPage(0, 1, &record, 1)"));
    TEST_ASSERT_TRUE(syncAfterSave == nullptr || syncAfterSave > nextHandler);
    TEST_ASSERT_TRUE(applyAfterSave == nullptr || applyAfterSave > nextHandler);
    TEST_ASSERT_TRUE(traceSaveAfterMeasurement == nullptr || traceSaveAfterMeasurement > nextHandler);
    TEST_ASSERT_TRUE(traceActualSync != nullptr && traceActualSync < nextHandler);
    TEST_ASSERT_NULL(std::strstr(buffer, "autoSaveTraceAsSegmentedSample"));
    TEST_ASSERT_NULL(std::strstr(buffer, "syncSegmentedCalibrationFromActual"));
    TEST_ASSERT_NULL(std::strstr(buffer, "applySegmentedCalibrationFromAvailableSamples"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "generateSegmentedCalibrationResultFromSavedSamples"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准已保存。"));
}

void test_calibration_page_avoids_large_metering_scheme_stack_arrays() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* panel = std::strstr(buffer, "void sendCalibrationParameterPanels()");
    TEST_ASSERT_NOT_NULL(panel);
    const char* nextFunction = std::strstr(panel, "void sendCalibrationFormulaPanel()");
    TEST_ASSERT_NOT_NULL(nextFunction);

    const char* schemePointer = std::strstr(panel, "MeteringSchemeRecord* schemes");
    const char* schemeAllocation = std::strstr(panel, "new (std::nothrow) MeteringSchemeRecord[10]");
    const char* schemeRelease = std::strstr(panel, "delete[] schemes");
    const char* stackArray = std::strstr(panel, "MeteringSchemeRecord schemes[10]");
    TEST_ASSERT_NOT_NULL(schemePointer);
    TEST_ASSERT_NOT_NULL(schemeAllocation);
    TEST_ASSERT_NOT_NULL(schemeRelease);
    TEST_ASSERT_TRUE(schemePointer < nextFunction);
    TEST_ASSERT_TRUE(schemeAllocation < nextFunction);
    TEST_ASSERT_TRUE(schemeRelease < nextFunction);
    TEST_ASSERT_TRUE(stackArray == nullptr || stackArray > nextFunction);

    const char* diagnostics = std::strstr(buffer, "SegmentedSampleDiagnostics collectSegmentedSampleDiagnostics");
    TEST_ASSERT_NOT_NULL(diagnostics);
    const char* diagnosticsEnd = std::strstr(diagnostics, "bool saveRamTraceToDevice");
    TEST_ASSERT_NOT_NULL(diagnosticsEnd);
    TEST_ASSERT_NOT_NULL(std::strstr(diagnostics, "SegmentedCalibrationSample* samples"));
    TEST_ASSERT_NOT_NULL(std::strstr(diagnostics, "WaterRecord* seenRecords"));
    TEST_ASSERT_NOT_NULL(std::strstr(diagnostics, "new (std::nothrow) SegmentedCalibrationSample[kSegmentedCalibrationMaxSamples]"));
    TEST_ASSERT_NOT_NULL(std::strstr(diagnostics, "new (std::nothrow) WaterRecord[kSegmentedCalibrationMaxSamples]"));
    TEST_ASSERT_NOT_NULL(std::strstr(diagnostics, "delete[] samples"));
    TEST_ASSERT_NOT_NULL(std::strstr(diagnostics, "delete[] seenRecords"));
    TEST_ASSERT_TRUE(std::strstr(diagnostics, "SegmentedCalibrationSample samples[kSegmentedCalibrationMaxSamples]") == nullptr ||
                     std::strstr(diagnostics, "SegmentedCalibrationSample samples[kSegmentedCalibrationMaxSamples]") > diagnosticsEnd);
    TEST_ASSERT_TRUE(std::strstr(diagnostics, "WaterRecord seenRecords[kSegmentedCalibrationMaxSamples]") == nullptr ||
                     std::strstr(diagnostics, "WaterRecord seenRecords[kSegmentedCalibrationMaxSamples]") > diagnosticsEnd);

    const char* samplesPanel = std::strstr(buffer, "void sendCalibrationSamplesPanel");
    TEST_ASSERT_NOT_NULL(samplesPanel);
    const char* samplesPanelEnd = std::strstr(samplesPanel, "void sendCalibrationGenerationPanel");
    TEST_ASSERT_NOT_NULL(samplesPanelEnd);
    TEST_ASSERT_NOT_NULL(std::strstr(samplesPanel, "WaterRecord* listed"));
    TEST_ASSERT_NOT_NULL(std::strstr(samplesPanel, "new (std::nothrow) WaterRecord[kSavedPulseTraceMaxCountLimit]"));
    TEST_ASSERT_NOT_NULL(std::strstr(samplesPanel, "delete[] listed"));
    TEST_ASSERT_TRUE(std::strstr(samplesPanel, "WaterRecord listed[kSavedPulseTraceMaxCountLimit]") == nullptr ||
                     std::strstr(samplesPanel, "WaterRecord listed[kSavedPulseTraceMaxCountLimit]") > samplesPanelEnd);
}

void test_calibration_page_reports_specific_errors_and_hides_stale_generated_result() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetCalibrationErrorMessage"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "no_calibration_record"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "这条样本不可校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存失败："));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "'HTTP 401':'认证已失效，请刷新页面重新登录。'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "'HTTP 404':'保存接口路径不存在，请刷新页面后重试。'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "credentials:'same-origin'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "fetch('/faucet/calibration',{method:'POST'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "fetch(f.action,{method:'POST'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "fetch('/faucet/calibration',{method:'POST',body:new FormData(f),cache:'no-store',credentials:'same-origin'}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSubmitGenerationAction"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准已保存，但页面刷新失败，请手动刷新查看最新状态。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "生成操作已完成，但页面刷新失败，请手动刷新查看最新状态。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "alert('保存失败，请稍后重试。')"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本质量提醒"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "容量跨度不足"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "拟合误差偏大"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本偏少，建议补充不同容量样本"));

    const char* panel = std::strstr(buffer, "void sendCalibrationGenerationPanel");
    TEST_ASSERT_NOT_NULL(panel);
    const char* nextFunction = std::strstr(panel, "void sendCalibrationPageScript");
    TEST_ASSERT_NOT_NULL(nextFunction);
    const char* requested = std::strstr(panel, "generationResultRequested()");
    const char* candidateReady = std::strstr(panel, "const bool candidateReady");
    TEST_ASSERT_NOT_NULL(requested);
    TEST_ASSERT_NOT_NULL(candidateReady);
    TEST_ASSERT_TRUE(requested < candidateReady);
    TEST_ASSERT_TRUE(candidateReady < nextFunction);
    TEST_ASSERT_NOT_NULL(std::strstr(panel, "class='generated-scheme-table'"));
    TEST_ASSERT_NOT_NULL(std::strstr(panel, "生成参数后显示待保存结果"));
}

void test_pulse_trace_and_calibration_pages_keep_saved_and_ram_sources_consistent() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* recordsPage = std::strstr(buffer, "void handleRecordsPage() {");
    const char* recordDetailPage = std::strstr(buffer, "void handleRecordDetailPage() {");
    TEST_ASSERT_NOT_NULL(recordsPage);
    TEST_ASSERT_NOT_NULL(recordDetailPage);
    const char* recordsEnd = std::strstr(recordsPage, "void handleCalibrationPage() {");
    TEST_ASSERT_NOT_NULL(recordsEnd);
    const char* savedTraceBranch = std::strstr(recordsPage, "if (hasSavedTrace) {");
    const char* ramTraceBranch = std::strstr(recordsPage, "} else if (trace) {");
    TEST_ASSERT_NOT_NULL(savedTraceBranch);
    TEST_ASSERT_NOT_NULL(ramTraceBranch);
    TEST_ASSERT_TRUE(savedTraceBranch < recordsEnd);
    TEST_ASSERT_TRUE(ramTraceBranch < recordsEnd);
    TEST_ASSERT_TRUE_MESSAGE(savedTraceBranch < ramTraceBranch,
                             "Records page must show saved trace state before RAM trace state when both exist");
    TEST_ASSERT_NOT_NULL(std::strstr(recordsPage, "saved=1&trace=%lu&bucket=1'>已存明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(recordsPage, "trace=%lu&bucket=1'>明细"));

    const char* detailEnd = std::strstr(recordDetailPage, "void handleFiltersPage() {");
    TEST_ASSERT_NOT_NULL(detailEnd);
    const char* alreadySaved = std::strstr(recordDetailPage, "const bool alreadySaved");
    const char* deleteSaved = std::strstr(recordDetailPage, "删除已保存明细");
    const char* saveTrace = std::strstr(recordDetailPage, "保存明细");
    TEST_ASSERT_NOT_NULL(alreadySaved);
    TEST_ASSERT_NOT_NULL(deleteSaved);
    TEST_ASSERT_NOT_NULL(saveTrace);
    TEST_ASSERT_TRUE(alreadySaved < detailEnd);
    TEST_ASSERT_TRUE(deleteSaved < detailEnd);
    TEST_ASSERT_TRUE(saveTrace < detailEnd);
    TEST_ASSERT_NOT_NULL(std::strstr(recordDetailPage, "savedSource || alreadySaved"));
    TEST_ASSERT_NOT_NULL(std::strstr(recordDetailPage, "RAM 样本校准成功后会写入设备样本库"));
    TEST_ASSERT_NOT_NULL(std::strstr(recordDetailPage, "未发生暂停后恢复出水且稳态识别成功"));

    const char* samplesPanel = std::strstr(buffer, "void sendCalibrationSamplesPanel");
    const char* generationPanel = std::strstr(buffer, "void sendCalibrationGenerationPanel");
    TEST_ASSERT_NOT_NULL(samplesPanel);
    TEST_ASSERT_NOT_NULL(generationPanel);
    const char* savedRow = std::strstr(samplesPanel, "sendCalibrationSampleRow(savedTraces[i], true");
    const char* rememberSavedRecord = std::strstr(samplesPanel, "listed[listedCount++] = savedTraces[i].record");
    const char* ramLoop = std::strstr(samplesPanel, "if (g_context.pulseTraces)");
    const char* duplicateGuard = std::strstr(samplesPanel, "traceAlreadyListed(listed, listedCount, trace->record)");
    const char* ramRow = std::strstr(samplesPanel, "sendCalibrationSampleRow(*trace, false");
    TEST_ASSERT_NOT_NULL(savedRow);
    TEST_ASSERT_NOT_NULL(rememberSavedRecord);
    TEST_ASSERT_NOT_NULL(ramLoop);
    TEST_ASSERT_NOT_NULL(duplicateGuard);
    TEST_ASSERT_NOT_NULL(ramRow);
    TEST_ASSERT_TRUE(savedRow < generationPanel);
    TEST_ASSERT_TRUE(rememberSavedRecord < generationPanel);
    TEST_ASSERT_TRUE(ramLoop < generationPanel);
    TEST_ASSERT_TRUE(duplicateGuard < generationPanel);
    TEST_ASSERT_TRUE(ramRow < generationPanel);
    TEST_ASSERT_TRUE(savedRow < ramLoop);
    TEST_ASSERT_TRUE(rememberSavedRecord < duplicateGuard);
    TEST_ASSERT_TRUE(duplicateGuard < ramRow);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "generateSegmentedCalibrationResultFromSavedSamples"));
    const char* generateFunction = std::strstr(buffer, "bool generateSegmentedCalibrationResultFromSavedSamples()");
    TEST_ASSERT_NOT_NULL(generateFunction);
    TEST_ASSERT_NOT_NULL(std::strstr(generateFunction, "collectSegmentedSampleDiagnostics(false)"));
    TEST_ASSERT_NULL(std::strstr(generateFunction, "collectSegmentedSampleDiagnostics(true)"));
}

void test_calibration_sample_row_does_not_send_long_form_markup_through_sendfmt() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* sampleRow = std::strstr(buffer, "void sendCalibrationSampleRow");
    const char* samplesPanel = std::strstr(buffer, "void sendCalibrationSamplesPanel");
    TEST_ASSERT_NOT_NULL(sampleRow);
    TEST_ASSERT_NOT_NULL(samplesPanel);
    TEST_ASSERT_TRUE(sampleRow < samplesPanel);

    char sampleRowSource[14000]{};
    const std::size_t sampleRowLen = static_cast<std::size_t>(samplesPanel - sampleRow);
    TEST_ASSERT_LESS_THAN(sizeof(sampleRowSource), sampleRowLen + 1);
    std::memcpy(sampleRowSource, sampleRow, sampleRowLen);

    TEST_ASSERT_NULL(std::strstr(sampleRowSource,
                                 "sendFmt(\"</div></td></tr><tr id='%s' class='sample-calibration-edit-row'"));
    TEST_ASSERT_NULL(std::strstr(sampleRowSource,
                                 "sendFmt(\"</div><div class='sample-calibration-inputs'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource, "Esp32BaseWeb::sendChunk(\"</div></td></tr><tr id='\""));
    TEST_ASSERT_NOT_NULL(std::strstr(sampleRowSource,
                                     "Esp32BaseWeb::sendChunk(\"</div><div class='sample-calibration-inputs'>\""));
}

void test_sendfmt_uses_dynamic_fallback_for_long_markup() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* sendFmt = std::strstr(buffer, "void sendFmt(const char* fmt, ...) {");
    const char* nextFunction = std::strstr(sendFmt, "void sendHtmlAttrEscaped");
    TEST_ASSERT_NOT_NULL(sendFmt);
    TEST_ASSERT_NOT_NULL(nextFunction);
    TEST_ASSERT_TRUE(sendFmt < nextFunction);

    char sendFmtSource[2600]{};
    const std::size_t sendFmtLen = static_cast<std::size_t>(nextFunction - sendFmt);
    TEST_ASSERT_LESS_THAN(sizeof(sendFmtSource), sendFmtLen + 1);
    std::memcpy(sendFmtSource, sendFmt, sendFmtLen);

    TEST_ASSERT_NOT_NULL(std::strstr(sendFmtSource, "va_copy"));
    TEST_ASSERT_NOT_NULL(std::strstr(sendFmtSource, "new (std::nothrow) char"));
    TEST_ASSERT_NOT_NULL(std::strstr(sendFmtSource, "needed + 1"));
    TEST_ASSERT_NOT_NULL(std::strstr(sendFmtSource, "delete[]"));
}

void test_metering_scheme_table_uses_compact_usage_count_layout() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* panel = std::strstr(buffer, "void sendCalibrationParameterPanels()");
    TEST_ASSERT_NOT_NULL(panel);
    const char* nextFunction = std::strstr(panel, "void sendMeteringSchemeEditPage");
    TEST_ASSERT_NOT_NULL(nextFunction);

    TEST_ASSERT_NOT_NULL(std::strstr(panel, "<th>使用次数</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(panel, "<b>%lu</b><span>条</span>"));
    const char* oldRecordHeader = std::strstr(panel, "<th>记录</th>");
    const char* oldRecordCell = std::strstr(panel, "出水记录 <b>%lu</b> 条");
    TEST_ASSERT_TRUE(oldRecordHeader == nullptr || oldRecordHeader > nextFunction);
    TEST_ASSERT_TRUE(oldRecordCell == nullptr || oldRecordCell > nextFunction);
    TEST_ASSERT_NULL(std::strstr(buffer, "min-width:1040px"));
    TEST_ASSERT_NULL(std::strstr(buffer, "min-width:520px"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::setDeviceName(\"首页\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::setHomePath(\"/index\")"));
    TEST_ASSERT_NULL(std::strstr(buffer, "Esp32BaseWeb::setHomePath(\"/faucet\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kSavedPulseTraceMaxCount = faucet::kSavedPulseTraceMaxCount"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"/faucet_pulse_traces_v4.bin\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "\"/faucet_saved_traces_v1.bin\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "\"/fpt_\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "feedStartupWatchdog()"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "startup_phase=%s"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "logStartupPhase(\"hardware_ready\")"));
    TEST_ASSERT_NULL(std::strstr(buffer, "Esp32BaseWeb::setDeviceName(\"智能出水\")"));
    TEST_ASSERT_NULL(std::strstr(buffer, "return g_lastDisplayFrame;"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "return fileStore_->upsert(calibration);"));
    TEST_ASSERT_NULL(std::strstr(buffer, "fileStore_ && fileStore_->ready() && fileStore_->upsert(calibration)"));
}

void test_main_source_wires_metering_scheme_and_snapshot_stores() {
    FILE* file = std::fopen("src/main.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[64000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "#include \"app/MeteringSchemeStore.h\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "#include \"app/WaterRecordMeteringSnapshotStore.h\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"/faucet_metering_schemes_v1.bin\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"/faucet_record_metering_v1.bin\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "PersistentRecordMeteringSnapshotStore"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "g_meteringSchemes.migrateLegacyFromConfig(g_configBackend, nowSeconds)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "g_meteringSchemes.activeScheme(activeScheme)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "g_config, activeScheme"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "g_recordMeteringSnapshots"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "g_meteringSchemes"));
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
    TEST_ASSERT_NULL(std::strstr(buffer, "当前控制用 P/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "脉冲/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "当前实际参与关阀控制的单系数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地确认页容量调整步进"));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地确认页时间调整步进"));
    TEST_ASSERT_NULL(std::strstr(buffer, "按秒保存最近出水脉冲明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "容量步进"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "时间步进"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "RAM 最近脉冲明细条数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "loadSystemConfig()"));
    TEST_ASSERT_NULL(std::strstr(buffer, "SystemConfig defaults = makeDefaultConfig()"));
    TEST_ASSERT_NULL(std::strstr(buffer, "addCoreFields(const SystemConfig& defaults)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动补偿水量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "全程平均脉冲数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动段时长"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动段脉冲"));
    TEST_ASSERT_NULL(std::strstr(buffer, "启动段水量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "稳态段 P/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "分段计量已校准"));
    TEST_ASSERT_NULL(std::strstr(buffer, "流量计脉冲系数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "高级救援参数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "pulse/ml"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存后需重启，重启后重新探测 LCD。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "仅保存在 RAM 中，重启会丢失；用于查看最近出水明细。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "有效脉冲间隔阈值；最大频率 = 1000000 / 当前值 Hz。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "实时计量和明细分析的有效脉冲判定阈值；最大有效频率 = 1000000 / 当前值 Hz。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "用于查看最近出水明细和校准后自动入库。"));
}

void test_app_config_save_migrates_before_marking_current_version() {
    FILE* file = std::fopen("src/app/FaucetAppConfig.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[24000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* saveHandler = std::strstr(buffer, "void onAppConfigSave");
    TEST_ASSERT_NOT_NULL(saveHandler);
    const char* loadConfig = std::strstr(saveHandler, "g_context.configStore->loadSystemConfig()");
    const char* markVersion = std::strstr(saveHandler, "Esp32BaseConfig::setInt(kConfigNs, kVersionKey, kConfigVersion)");
    TEST_ASSERT_NOT_NULL(loadConfig);
    TEST_ASSERT_NULL_MESSAGE(markVersion, "AppConfig must not mark business config version directly");
}

void test_app_config_submit_rejects_read_only_business_config_before_field_writes() {
    FILE* file = std::fopen("src/app/FaucetAppConfig.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[24000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* validateHandler = std::strstr(buffer, "bool validateAppConfigPage");
    TEST_ASSERT_NOT_NULL(validateHandler);
    const char* changeHandler = std::strstr(validateHandler, "void onAppConfigChange");
    TEST_ASSERT_NOT_NULL(changeHandler);
    const char* readOnlyCheck = std::strstr(validateHandler, "g_context.configStore->systemConfigReadOnly()");
    TEST_ASSERT_NOT_NULL_MESSAGE(readOnlyCheck, "AppConfig submit validation must reject read-only business config");
    TEST_ASSERT_TRUE_MESSAGE(readOnlyCheck < changeHandler, "Read-only gate must run in pre-save page validation");
}

void test_web_config_writes_reload_current_config_before_persisting() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* persistHandler = std::strstr(buffer, "bool persistConfig(const SystemConfig& config) {");
    TEST_ASSERT_NOT_NULL(persistHandler);
    const char* loadConfig = std::strstr(persistHandler, "g_context.configStore->loadSystemConfig()");
    const char* saveConfig = std::strstr(persistHandler, "g_context.configStore->saveSystemConfig");
    TEST_ASSERT_NOT_NULL(loadConfig);
    TEST_ASSERT_NOT_NULL(saveConfig);
    TEST_ASSERT_TRUE_MESSAGE(loadConfig < saveConfig, "Web saves must merge changes into loaded migrated config");
}

void test_presets_api_allows_next_preset_switch_actions() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* presetsApi = std::strstr(buffer, "void handlePresetsApi()");
    TEST_ASSERT_NOT_NULL(presetsApi);
    const char* browserForm = std::strstr(presetsApi, "const bool browserForm = Esp32BaseWeb::hasParam(\"return\")");
    TEST_ASSERT_NOT_NULL(browserForm);
    const char* previous = std::strstr(presetsApi, "selectPreviousPresetForWeb()");
    const char* next = std::strstr(presetsApi, "selectNextPresetForWeb()");
    const char* select = std::strstr(presetsApi, "selectPresetForWeb(index)");
    const char* status = std::strstr(presetsApi, "sendCurrentStatusJson()");
    TEST_ASSERT_NOT_NULL(previous);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_NOT_NULL(select);
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_TRUE_MESSAGE(previous < browserForm, "preset switch actions must be handled before config form save");
    TEST_ASSERT_TRUE_MESSAGE(next < browserForm, "preset switch actions must be handled before config form save");
    TEST_ASSERT_TRUE_MESSAGE(select < browserForm, "preset switch actions must be handled before config form save");
    TEST_ASSERT_TRUE_MESSAGE(status < browserForm, "preset switch actions must return updated status JSON");
}

void test_business_post_handlers_use_post_allowed_guard() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::checkPostAllowed(\"faucet_presets\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::checkPostAllowed(\"faucet_records\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::checkPostAllowed(\"faucet_calibration\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::checkPostAllowed(\"faucet_filters\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Esp32BaseWeb::checkPostAllowed(\"faucet_filter_reset\")"));
}

void test_heavy_web_handlers_return_busy_while_water_task_active() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* recordsPage = std::strstr(buffer, "void handleRecordsPage()");
    const char* calibrationPage = std::strstr(buffer, "void handleCalibrationPage()");
    const char* detailPage = std::strstr(buffer, "void handleRecordDetailPage()");
    const char* recordsApi = std::strstr(buffer, "void handleRecordsApi()");
    TEST_ASSERT_NOT_NULL(recordsPage);
    TEST_ASSERT_NOT_NULL(calibrationPage);
    TEST_ASSERT_NOT_NULL(detailPage);
    TEST_ASSERT_NOT_NULL(recordsApi);

    TEST_ASSERT_NOT_NULL(std::strstr(recordsPage, "sendBusyJson(\"records_page\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(calibrationPage, "sendBusyJson(\"calibration_page\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(detailPage, "sendBusyJson(\"record_detail\")"));
    TEST_ASSERT_NOT_NULL(std::strstr(recordsApi, "sendBusyJson(\"records_api\")"));
}

const char* findWithin(const char* begin, const char* end, const char* needle) {
    const char* found = std::strstr(begin, needle);
    return found && found < end ? found : nullptr;
}

void test_web_write_handlers_return_busy_before_trace_or_filter_runtime_writes() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[340000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* traceDelete = std::strstr(buffer, "void handleTraceDeleteApi() {");
    const char* traceSave = std::strstr(buffer, "void handleTraceSaveApi() {");
    const char* filtersReset = std::strstr(buffer, "void handleFiltersResetApi() {");
    TEST_ASSERT_NOT_NULL(traceDelete);
    TEST_ASSERT_NOT_NULL(traceSave);
    TEST_ASSERT_NOT_NULL(filtersReset);

    const char* traceDeleteBusy = findWithin(traceDelete, traceSave, "faucetWebWriteBusyRedirect(waterTaskActive()");
    const char* traceDeleteWrite = findWithin(traceDelete, traceSave, "g_context.savedPulseTraces->remove(traceId)");
    TEST_ASSERT_NOT_NULL_MESSAGE(traceDeleteBusy, "trace delete must return busy while water task is active");
    TEST_ASSERT_NOT_NULL(traceDeleteWrite);
    TEST_ASSERT_TRUE_MESSAGE(traceDeleteBusy < traceDeleteWrite, "trace delete busy guard must run before device trace write");
    TEST_ASSERT_NOT_NULL(findWithin(traceDelete, traceSave, "FaucetWebWriteTarget::Calibration : FaucetWebWriteTarget::Records"));

    const char* traceSaveBusy = findWithin(traceSave, filtersReset, "faucetWebWriteBusyRedirect(waterTaskActive()");
    const char* traceSaveWrite = findWithin(traceSave, filtersReset, "saveRamTraceToDevice(traceId");
    TEST_ASSERT_NOT_NULL_MESSAGE(traceSaveBusy, "trace save must return busy while water task is active");
    TEST_ASSERT_NOT_NULL(traceSaveWrite);
    TEST_ASSERT_TRUE_MESSAGE(traceSaveBusy < traceSaveWrite, "trace save busy guard must run before device trace write");
    TEST_ASSERT_NOT_NULL(findWithin(traceSave, filtersReset, "FaucetWebWriteTarget::Calibration : FaucetWebWriteTarget::Records"));

    const char* filtersResetEnd = std::strstr(filtersReset, "Esp32BaseWeb::Handler handlerFor");
    TEST_ASSERT_NOT_NULL(filtersResetEnd);
    const char* filtersResetBusy = findWithin(filtersReset, filtersResetEnd, "faucetWebWriteBusyRedirect(waterTaskActive()");
    const char* filtersResetWrite = findWithin(filtersReset, filtersResetEnd, "g_context.filters->updateFilter(index, record)");
    TEST_ASSERT_NOT_NULL_MESSAGE(filtersResetBusy, "filter reset must return busy while water task is active");
    TEST_ASSERT_NOT_NULL(filtersResetWrite);
    TEST_ASSERT_TRUE_MESSAGE(filtersResetBusy < filtersResetWrite, "filter reset busy guard must run before runtime write");
    TEST_ASSERT_NOT_NULL(findWithin(filtersReset, filtersResetEnd, "FaucetWebWriteTarget::Filters"));
}

void test_incomplete_factory_reset_path_is_not_kept_as_dead_code() {
    FILE* mainFile = std::fopen("src/main.cpp", "rb");
    TEST_ASSERT_NOT_NULL(mainFile);
    static char mainBuffer[90000]{};
    const std::size_t mainRead = std::fread(mainBuffer, 1, sizeof(mainBuffer) - 1, mainFile);
    std::fclose(mainFile);
    TEST_ASSERT_GREATER_THAN_size_t(0, mainRead);

    FILE* appHeader = std::fopen("include/app/AppController.h", "rb");
    TEST_ASSERT_NOT_NULL(appHeader);
    static char headerBuffer[24000]{};
    const std::size_t headerRead = std::fread(headerBuffer, 1, sizeof(headerBuffer) - 1, appHeader);
    std::fclose(appHeader);
    TEST_ASSERT_GREATER_THAN_size_t(0, headerRead);

    TEST_ASSERT_NULL(std::strstr(mainBuffer, "consumeFactoryResetRequest"));
    TEST_ASSERT_NULL(std::strstr(headerBuffer, "factoryResetRequested_"));
    TEST_ASSERT_NULL(std::strstr(headerBuffer, "consumeFactoryResetRequest"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_routes_fit_esp32base_default_route_capacity);
    RUN_TEST(test_routes_do_not_register_remote_water_control_paths);
    RUN_TEST(test_navigation_pages_use_requested_order_and_labels);
    RUN_TEST(test_route_whitelist_rejects_unknown_and_dangerous_control_aliases);
    RUN_TEST(test_business_api_routes_use_explicit_methods);
    RUN_TEST(test_filter_edit_route_is_hidden_from_navigation);
    RUN_TEST(test_app_css_route_is_hidden_from_navigation);
    RUN_TEST(test_filter_forms_use_registered_api_endpoints);
    RUN_TEST(test_presets_page_is_available_in_navigation);
    RUN_TEST(test_records_page_and_calibration_api_are_available);
    RUN_TEST(test_web_page_source_has_no_remote_water_control_forms);
    RUN_TEST(test_web_page_source_links_cacheable_app_css);
    RUN_TEST(test_web_page_source_contains_expected_ui_improvements);
    RUN_TEST(test_record_calibration_api_saves_actual_without_segmented_generation);
    RUN_TEST(test_calibration_page_avoids_large_metering_scheme_stack_arrays);
    RUN_TEST(test_calibration_page_reports_specific_errors_and_hides_stale_generated_result);
    RUN_TEST(test_pulse_trace_and_calibration_pages_keep_saved_and_ram_sources_consistent);
    RUN_TEST(test_calibration_sample_row_does_not_send_long_form_markup_through_sendfmt);
    RUN_TEST(test_sendfmt_uses_dynamic_fallback_for_long_markup);
    RUN_TEST(test_metering_scheme_table_uses_compact_usage_count_layout);
    RUN_TEST(test_main_source_renders_live_display_frame_for_web);
    RUN_TEST(test_main_source_wires_metering_scheme_and_snapshot_stores);
    RUN_TEST(test_app_config_source_uses_clear_business_labels_and_help);
    RUN_TEST(test_app_config_save_migrates_before_marking_current_version);
    RUN_TEST(test_app_config_submit_rejects_read_only_business_config_before_field_writes);
    RUN_TEST(test_web_config_writes_reload_current_config_before_persisting);
    RUN_TEST(test_presets_api_allows_next_preset_switch_actions);
    RUN_TEST(test_business_post_handlers_use_post_allowed_guard);
    RUN_TEST(test_heavy_web_handlers_return_busy_while_water_task_active);
    RUN_TEST(test_web_write_handlers_return_busy_before_trace_or_filter_runtime_writes);
    RUN_TEST(test_incomplete_factory_reset_path_is_not_kept_as_dead_code);
    return UNITY_END();
}
