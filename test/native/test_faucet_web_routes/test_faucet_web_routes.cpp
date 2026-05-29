#include <unity.h>

#include "web/FaucetWebRoutes.h"

#include <cstdio>
#include <cstring>

using namespace faucet;

void test_routes_fit_esp32base_default_route_capacity() {
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base());
    TEST_ASSERT_TRUE(faucetWebRoutesFitEsp32Base(16));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(16, faucetWebRouteCount());
    TEST_ASSERT_EQUAL_size_t(16, faucetWebRouteCount());
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
        "/faucet", "/faucet/records", "/faucet/calibration", "/faucet/stats", "/faucet/presets", "/faucet/filters"};
    const char* expectedTitles[] = {"首页", "记录", "校准", "统计", "预设", "滤芯"};

    for (std::size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_STRING(expectedPaths[i], routes[i].path);
        TEST_ASSERT_EQUAL(i == 2 ? FaucetWebMethod::Any : FaucetWebMethod::Get, routes[i].method);
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
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/api/faucet/today"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/unknown"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("api/faucet/status"));
}

void test_dual_method_routes_are_merged_to_any() {
    const FaucetWebRoute* routes = faucetWebRoutes();
    bool foundPresets = false;
    bool foundFilters = false;
    bool foundRecords = false;
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (std::strcmp(routes[i].path, "/api/faucet/presets") == 0) {
            foundPresets = routes[i].method == FaucetWebMethod::Any;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/filters") == 0) {
            foundFilters = routes[i].method == FaucetWebMethod::Any;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/records") == 0) {
            foundRecords = routes[i].method == FaucetWebMethod::Any;
        }
    }

    TEST_ASSERT_TRUE(foundPresets);
    TEST_ASSERT_TRUE(foundFilters);
    TEST_ASSERT_TRUE(foundRecords);
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/config"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/faucet/records/calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/trace-calibration"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/trace-save"));
    TEST_ASSERT_FALSE(faucetWebRouteAllowed("/api/faucet/records/trace-delete"));
    TEST_ASSERT_TRUE(faucetWebRouteAllowed("/faucet/records/detail"));
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
        if (std::strcmp(routes[i].path, "/faucet/calibration") == 0) {
            foundCalibrationPage = routes[i].method == FaucetWebMethod::Any &&
                                   routes[i].kind == FaucetWebRouteKind::Page &&
                                   std::strcmp(routes[i].title, "校准") == 0;
        }
        if (std::strcmp(routes[i].path, "/api/faucet/records") == 0) {
            foundApi = routes[i].method == FaucetWebMethod::Any && routes[i].kind == FaucetWebRouteKind::Api &&
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
    static char buffer[300000]{};
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
    static char buffer[300000]{};
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
    static char buffer[300000]{};
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "body{max-width:1120px"));
    TEST_ASSERT_NULL(std::strstr(buffer, "body{max-width:1280px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>时间</th><th>模式</th><th>目标</th><th>出水</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>用时</th><th>脉冲</th><th>结果</th><th>操作</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>脉冲/升</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "formatWaterRecordListTime(records[i], startTime"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "%luP (%luP/L)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "</td><td>%luP/L"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "metering-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-cache-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "saved-trace-diagnostic"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "计量诊断"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "临时缓存"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已保存明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "控制P/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "稳态P/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "启动等效"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "有效样本"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".diagnostic-metric strong{display:block;color:var(--text);font-size:14px;line-height:1.2;font-weight:650;font-variant-numeric:tabular-nums;white-space:nowrap;overflow-wrap:normal}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已保存明细 <b>%u条</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已测容量 <b>%u条</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "还需 <b>%s</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "建议补偿"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "拟合误差"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "候选已生成"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本不足"));
    TEST_ASSERT_NULL(std::strstr(buffer, "最近校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "占用 <b>%s / %s</b>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<span><b>%u%%</b></span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "单条最多 <b>%lu 点</b>"));
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
    TEST_ASSERT_NULL(std::strstr(buffer, "当前计量参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "records-top-grid"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".records-top-grid .records-diagnostic-panel"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".diagnostic-metric-grid.three"));
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
    TEST_ASSERT_NULL(std::strstr(calibrationHandler, "<a class='btn-link' href='/faucet/records'>历史记录</a>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-badge"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已存明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "trace-head-meter"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-detail-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "pulse-line-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "legend-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "cum-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "cum-line-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "legend-cum-paused"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "脉冲趋势"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-frequency"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "trace-frequency-label'>聚合频率"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".trace-frequency a.page-current"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-y-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-cum-y-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "chart-x-label"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "运行累计"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketRunningPulseDelta"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketOnlyHasRunningSamples"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketRunning ? bucketRunningPulseDelta(samples, trace->sampleCount, buckets[i]) : 0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "runningCumulative"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "prevPulseValid = false"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketRunning ? \"pulse-line\" : \"pulse-line pulse-line-paused\""));
    TEST_ASSERT_NULL(std::strstr(buffer, "prevX = x;"));
    TEST_ASSERT_NULL(std::strstr(buffer, "prevCumX = x;"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucketsToShow[] = {1, 2, 3, 4, 5}"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bucket == bucketSeconds ? \"btn-link page-current\" : \"btn-link\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "aria-current='%s'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "detail-data"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<section class='panel detail-data'><div class='panel-head'><h3>原始明细</h3>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "显示原始明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "显示所有明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "target='_blank'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "raw=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "all=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kRawTracePreviewLastSecond = 30"));
    TEST_ASSERT_NULL(std::strstr(buffer, "kRawTracePreviewLastSecond = 60"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "rawTracePreviewSampleCount"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "rawTraceShowAll"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "beginResponse(200, \"text/plain; charset=utf-8\""));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "时间\\t脉冲数\\t累计脉冲数\\t状态\\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "%lu秒\\t%u\\t%lu\\t%s\\n"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "WaterPulseTraceState::PauseTimeout"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "waterResultAllowsCalibration(record.result)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "默认展示 0秒 到 %lu秒"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "原始秒级数据共 %lu 行，当前展示 %lu 行。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "加载原始明细"));
    TEST_ASSERT_NULL(std::strstr(buffer, "下载文本"));
    TEST_ASSERT_NULL(std::strstr(buffer, "fetch(rawUrl,{cache:'no-store'})"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<pre id='rawTraceText' class='raw-trace-text'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "需要排查时再拉取纯文本"));
    TEST_ASSERT_NULL(std::strstr(buffer, "避免页面一次性生成大量表格"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<details open class='panel detail-data'>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<summary>查看明细数据</summary>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<table class='raw-trace-table'><tr><th>时间</th><th>脉冲数</th><th>累计脉冲数</th><th>状态</th></tr>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><td>%lu秒</td><td>%u</td><td>%lu</td><td>%s</td></tr>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<tr><td>%lu</td><td>%lu</td><td>%lu</td><td>%s</td></tr>"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存到设备"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "已保存到设备"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "确认将这条脉冲明细保存到设备存储？"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, " / 滤%luP"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>诊断</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "脉冲 %lu / 过滤 %lu / 系数 %.3f"));
    TEST_ASSERT_NULL(std::strstr(buffer, "量杯实际水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "容量校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准工作台"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<details class='panel calibration-volume-panel'>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<summary>容量校准</summary>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".calibration-volume-panel summary{padding-bottom:6px"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "出水信息"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "上次校准记录"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<tr><th>出水信息</th><td>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "估算出水"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实际出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实测脉冲/升"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "估算差"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "控制参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "未修改"));
    TEST_ASSERT_NULL(std::strstr(buffer, "记录摘要"));
    TEST_ASSERT_NULL(std::strstr(buffer, "上次实测记录"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "实际出水量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存容量"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存重校"));
    TEST_ASSERT_NULL(std::strstr(buffer, "保存实测量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "saveRecordActualMeasurement"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "calibration?saved=actual"));
    TEST_ASSERT_NULL(std::strstr(buffer, "applyCalibrationFromRecord(record, actualMl)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准已保存。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "未修改"));
    TEST_ASSERT_NULL(std::strstr(buffer, "step='10' value='%lu'></label>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "step='1' value='%lu'></label>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "保存这条记录的实际容量；不会修改原始脉冲、当前关阀控制 P/L 或分段拟合参数。"));
    TEST_ASSERT_NULL(std::strstr(buffer, "name='saveTrace'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='save_latest_trace'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='delete_latest_trace'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "明细文件"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='generate_segmented'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='apply_segmented'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='restore_segmented'"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "action='/faucet/calibration'"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<span class='status-pill status-muted'>手动执行</span>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "手动执行：扫描已保存且带实测容量的脉冲明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本状态"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "样本已入库"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "记录已校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "来自记录容量校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "未输入实测容量"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "请在校准页输入最新记录的实际出水量。"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-context{display:flex;flex-direction:column;gap:3px;min-width:0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-alert{margin:0;color:#8a6f3d;font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-preset-line{margin:0;color:var(--muted);font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-progress-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:var(--muted);font-size:13px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-task-card span{display:block;color:var(--muted);font-size:12px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-task-card small{display:block;margin-top:4px;color:var(--muted);font-size:12px;line-height:1.2;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-status-item{display:inline-flex;align-items:center;gap:5px;min-height:28px;padding:0 9px;border:1px solid #dce4ea;border-radius:999px;background:#f7f9fb;color:#66737c;font-size:12px;font-weight:400"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, ".machine-status-note{color:#7a858e;font-size:11px;font-weight:400"));
    TEST_ASSERT_NULL(std::strstr(buffer, "align-items:baseline;gap:5px;min-height:28px"));
    TEST_ASSERT_NULL(std::strstr(buffer, "machine-kpis"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-hero"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-hero-head"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-context"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-alert"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "machine-preset-line"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "faucetSet('targetMeta'"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"pulsePerLiter\", \"流量计\", pulsePerLiter"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "阀门"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "设备不在待机状态，请回到待机后再保存配置。"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>目标值</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "<th>脉冲</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>脉冲</th><th>轨迹</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>校准</th>"));
    TEST_ASSERT_NULL(std::strstr(buffer, "<th>诊断</th>"));

    const char* detailHandler = std::strstr(buffer, "void handleRecordDetailPage() {");
    TEST_ASSERT_NOT_NULL(detailHandler);
    const char* sampleStatus = std::strstr(detailHandler, "<section class='panel sample-status-panel'><h3>样本状态</h3>");
    const char* detailOverview = std::strstr(detailHandler, "<section class='panel'><h3>明细概况</h3>");
    const char* pulseTrend = std::strstr(detailHandler, "<section class='panel'><div class='panel-head'><h3>脉冲趋势</h3>");
    TEST_ASSERT_NOT_NULL(sampleStatus);
    TEST_ASSERT_NOT_NULL(detailOverview);
    TEST_ASSERT_NOT_NULL(pulseTrend);
    TEST_ASSERT_TRUE(sampleStatus < detailOverview);
    TEST_ASSERT_TRUE(sampleStatus < pulseTrend);
}

void test_record_calibration_api_saves_actual_without_segmented_generation() {
    FILE* file = std::fopen("src/web/FaucetWeb.cpp", "rb");
    TEST_ASSERT_NOT_NULL(file);
    static char buffer[300000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    const char* handler = std::strstr(buffer, "void handleRecordCalibrationApi() {");
    TEST_ASSERT_NOT_NULL(handler);
    const char* nextHandler = std::strstr(handler, "void handleTraceCalibrationApi() {");
    TEST_ASSERT_NOT_NULL(nextHandler);

    const char* findExisting = std::strstr(handler, "const bool calibrated = findRecordCalibration(record, calibration);");
    const char* defaultActual =
        std::strstr(handler, "const std::uint32_t defaultActualMl = calibrated ? calibration.actualMl : record.volumeMl;");
    const char* unchangedGuard = std::strstr(handler, "if (calibrated && actualMl == defaultActualMl)");
    const char* unchangedRedirect =
        unchangedGuard ? std::strstr(unchangedGuard, "Esp32BaseWeb::redirectSeeOther(\"/faucet/calibration?saved=actual\")") : nullptr;
    const char* saveMeasurement = std::strstr(handler, "saveRecordActualMeasurement(record, actualMl)");
    const char* syncAfterSave =
        saveMeasurement ? std::strstr(saveMeasurement, "syncSegmentedCalibrationFromActual(record, actualMl)") : nullptr;
    const char* applyAfterSave =
        saveMeasurement ? std::strstr(saveMeasurement, "applySegmentedCalibrationFromAvailableSamples()") : nullptr;
    const char* traceSaveAfterMeasurement =
        saveMeasurement ? std::strstr(saveMeasurement, "autoSaveTraceAsSegmentedSample(record, actualMl)") : nullptr;
    const char* traceActualSync =
        saveMeasurement ? std::strstr(saveMeasurement, "syncTraceActualMeasurement(record, actualMl)") : nullptr;

    TEST_ASSERT_TRUE(findExisting != nullptr && findExisting < nextHandler);
    TEST_ASSERT_TRUE(defaultActual != nullptr && defaultActual < nextHandler);
    TEST_ASSERT_TRUE(unchangedGuard != nullptr && unchangedGuard < nextHandler);
    TEST_ASSERT_TRUE(unchangedRedirect != nullptr && unchangedRedirect < nextHandler);
    TEST_ASSERT_TRUE(unchangedGuard < saveMeasurement);
    TEST_ASSERT_TRUE(syncAfterSave == nullptr || syncAfterSave > nextHandler);
    TEST_ASSERT_TRUE(applyAfterSave == nullptr || applyAfterSave > nextHandler);
    TEST_ASSERT_TRUE(traceSaveAfterMeasurement == nullptr || traceSaveAfterMeasurement > nextHandler);
    TEST_ASSERT_TRUE(traceActualSync != nullptr && traceActualSync < nextHandler);
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "autoSaveTraceAsSegmentedSample"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "generateSegmentedCalibrationCandidateFromSavedSamples"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "校准已保存。"));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kSavedPulseTraceMaxCount = 32"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"/faucet_pulse_traces_v2.bin\""));
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
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "当前控制用 P/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "脉冲/L"));
    TEST_ASSERT_NULL(std::strstr(buffer, "当前实际参与关阀控制的单系数"));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地确认页容量调整步进"));
    TEST_ASSERT_NULL(std::strstr(buffer, "本地确认页时间调整步进"));
    TEST_ASSERT_NULL(std::strstr(buffer, "按秒保存最近出水脉冲明细"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "容量步进"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "时间步进"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "脉冲明细缓存"));
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
    TEST_ASSERT_NOT_NULL(markVersion);
    TEST_ASSERT_TRUE_MESSAGE(loadConfig < markVersion, "AppConfig save must let ConfigStore migrate legacy fields first");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_routes_fit_esp32base_default_route_capacity);
    RUN_TEST(test_routes_do_not_register_remote_water_control_paths);
    RUN_TEST(test_navigation_pages_use_requested_order_and_labels);
    RUN_TEST(test_route_whitelist_rejects_unknown_and_dangerous_control_aliases);
    RUN_TEST(test_dual_method_routes_are_merged_to_any);
    RUN_TEST(test_filter_edit_route_is_hidden_from_navigation);
    RUN_TEST(test_app_css_route_is_hidden_from_navigation);
    RUN_TEST(test_filter_forms_use_registered_api_endpoints);
    RUN_TEST(test_presets_page_is_available_in_navigation);
    RUN_TEST(test_records_page_and_calibration_api_are_available);
    RUN_TEST(test_web_page_source_has_no_remote_water_control_forms);
    RUN_TEST(test_web_page_source_links_cacheable_app_css);
    RUN_TEST(test_web_page_source_contains_expected_ui_improvements);
    RUN_TEST(test_record_calibration_api_saves_actual_without_segmented_generation);
    RUN_TEST(test_main_source_renders_live_display_frame_for_web);
    RUN_TEST(test_app_config_source_uses_clear_business_labels_and_help);
    RUN_TEST(test_app_config_save_migrates_before_marking_current_version);
    return UNITY_END();
}
