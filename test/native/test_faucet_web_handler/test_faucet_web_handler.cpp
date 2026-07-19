#include <unity.h>

#define ESP32BASE_WEB_NATIVE_TEST 1

#include "app/AppController.h"
#include "app/ConfigStore.h"
#include "web/FaucetWeb.h"
#include "../support/CalibrationTraceTestSupport.h"
#include "../support/FakeAdcReader.h"
#include "../support/FakeConfigBackend.h"
#include "../support/MemoryFileBackend.h"
#include "../support/MemoryRecordWriter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../../../Esp32Base/src/web/Esp32BaseWeb.cpp"
#include "../../../src/web/FaucetWebAssets.cpp"
#include "../../../src/web/FaucetWeb.cpp"

using namespace faucet;
using faucet_test::FakeAdcReader;
using faucet_test::FakeConfigBackend;
using faucet_test::okMv;
using faucet_test::MemoryFileBackend;
using faucet_test::MemoryRecordWriter;
using faucet_test::saveCompletedCalibrationTrace;

namespace {

class CountingWaterRecordReader : public WaterRecordReader {
public:
    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         WaterRecord* output,
                         std::size_t outputCapacity) const override {
        ++readPageCalls;
        if (!output || outputCapacity == 0 || pageSize == 0) {
            return 0;
        }
        const std::size_t offset = pageIndex * static_cast<std::size_t>(pageSize);
        if (offset >= records.size()) {
            return 0;
        }
        lastPageSize = pageSize;
        const std::size_t count = std::min<std::size_t>(pageSize, std::min(outputCapacity, records.size() - offset));
        for (std::size_t i = 0; i < count; ++i) {
            output[i] = records[offset + i];
        }
        return count;
    }

    std::size_t count() const override {
        ++countCalls;
        return records.size();
    }

    bool ready() const override {
        return readyFlag;
    }

    const char* storageName() const override {
        return "counting-records";
    }

    std::vector<WaterRecord> records;
    mutable std::uint32_t readPageCalls = 0;
    mutable std::uint32_t countCalls = 0;
    mutable std::uint16_t lastPageSize = 0;
    bool readyFlag = true;
};

std::uint32_t g_afterFormatFsNotifications = 0;

void countAfterFormatFsNotification() {
    ++g_afterFormatFsNotifications;
}

AppTickInput appInput(ButtonLevels levels, std::uint32_t nowMs, std::uint32_t nowUs) {
    return AppTickInput{
        levels,
        nowMs,
        nowUs,
        1714502400,
        {20260506, 202619, 202605},
        true,
        true,
        7,
    };
}

void pressAndReleaseOk(AppController& app, std::uint32_t baseMs) {
    app.tick(appInput({false, true, false, false}, baseMs, baseMs * 1000UL));
    app.tick(appInput({false, true, false, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL));
    app.tick(appInput({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL));
    app.tick(appInput({false, false, false, false}, baseMs + 60 + kButtonDebounceMs,
                      (baseMs + 60 + kButtonDebounceMs) * 1000UL));
}

void setRunning(AppController& app) {
    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

void applyTestMeteringScheme(AppController& app) {
    MeteringSchemeRecord scheme{};
    scheme.id = 99;
    scheme.recordUsed = true;
    std::snprintf(scheme.name, sizeof(scheme.name), "native");
    scheme.params = MeteringParameters{0, 0, 1000, 0, 1950};
    scheme.sourceType = MeteringSchemeSource::CalibrationSession;
    scheme.sampleCount = 2;
    TEST_ASSERT_TRUE(app.applyActiveMeteringScheme(scheme));
}

std::uint32_t testNowSeconds() {
    return 1714502400UL;
}

std::uint32_t testBootId() {
    return 7UL;
}

WaterRecord makeWebRecord(std::uint32_t startTime, std::uint32_t volumeMl = 1000) {
    WaterRecord record{};
    record.startTime = startTime;
    record.volumeMl = volumeMl;
    record.targetValue = volumeMl;
    record.pulseCount = volumeMl / 10;
    record.durationSec = 30;
    record.mode = WaterMode::Volume;
    record.result = WaterResult::Completed;
    record.selectedPreset = 1;
    record.meteringSchemeId = 7;
    return record;
}

void saveWebSessionAttempt(CalibrationSessionTraceStore& traceStore,
                           CalibrationSessionRecord& session,
                           std::uint8_t slot,
                           CalibrationAttemptStatus status,
                           std::uint32_t actualMl,
                           std::uint32_t pulseCount = 0,
                           std::uint32_t stableSeconds = 6) {
    CalibrationAttempt attempt{};
    attempt.attemptIndex = slot;
    attempt.targetHintMl = actualMl > 0 ? actualMl : 1000;
    attempt.record = makeWebRecord(testNowSeconds() + slot * 10UL, attempt.targetHintMl);
    attempt.record.pulseCount = pulseCount > 0 ? pulseCount : 260 + static_cast<std::uint32_t>(slot) * 50UL;
    attempt.record.durationSec = 5 + stableSeconds;
    attempt.status = status;
    attempt.actualMl = status == CalibrationAttemptStatus::Valid ? actualMl : 0;
    const std::uint32_t startupPulses = std::min<std::uint32_t>(40, attempt.record.pulseCount);
    const std::uint32_t stablePulses = attempt.record.pulseCount - startupPulses;
    if (status == CalibrationAttemptStatus::Valid) {
        attempt.summary.actualMl = actualMl;
        attempt.summary.totalPulses = attempt.record.pulseCount;
        attempt.summary.durationSec = attempt.record.durationSec;
        attempt.summary.stable = true;
        attempt.summary.startupPulseCount = startupPulses;
        attempt.summary.stablePulseCount = stablePulses;
        attempt.summary.stableStartSec = 5;
        attempt.summary.stablePulsePerSec = static_cast<float>(stablePulses) / static_cast<float>(stableSeconds);
        attempt.summary.usableForGeneration = stablePulses > 0 && actualMl >= kCalibrationMinActualMl;
    }

    if (status == CalibrationAttemptStatus::Valid) {
        TEST_ASSERT_TRUE(saveCompletedCalibrationTrace(traceStore,
                                                       slot,
                                                       session.sessionId,
                                                       attempt.record,
                                                       actualMl,
                                                       startupPulses,
                                                       attempt.record.pulseCount - startupPulses,
                                                       stableSeconds));
        attempt.sessionTraceSlot = slot;
    } else {
        attempt.sessionTraceSlot = kCalibrationSessionTraceSlots;
    }

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    session.validSampleCount = countValidCalibrationSamples(session);
}

void saveWebCompactSessionAttempt(CalibrationSessionTraceStore& traceStore,
                                  CalibrationSessionRecord& session,
                                  std::uint8_t slot,
                                  std::uint32_t actualMl) {
    CalibrationAttempt attempt{};
    attempt.attemptIndex = slot;
    attempt.targetHintMl = actualMl;
    attempt.record = makeWebRecord(testNowSeconds() + slot * 10UL, actualMl);
    attempt.record.pulseCount = 10;
    attempt.record.durationSec = 2;
    attempt.status = CalibrationAttemptStatus::Valid;
    attempt.actualMl = actualMl;

    WaterPulseTraceBucketSample buckets[4]{{2}, {3}, {3}, {2}};
    WaterPulseTraceSample startup[3]{{0}, {120000}, {260000}};
    CalibrationStoredTrace stored{};
    stored.sessionId = session.sessionId;
    stored.attemptIndex = slot;
    stored.trace.traceId = slot + 100;
    stored.trace.startTime = attempt.record.startTime;
    stored.trace.record = attempt.record;
    stored.trace.bucketCount = 4;
    stored.trace.startupEdgeCount = 3;
    stored.trace.totalPulses = 10;
    stored.trace.actualMl = actualMl;
    stored.trace.pulseMinIntervalUs = kDefaultPulseMinIntervalUs;
    stored.trace.finalState = WaterPulseTraceState::Completed;
    stored.trace.finished = true;
    TEST_ASSERT_TRUE(traceStore.saveValid(slot, stored, buckets, 4, startup, 3, actualMl, attempt.record.startTime + 10));
    attempt.sessionTraceSlot = slot;

    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    session.validSampleCount = countValidCalibrationSamples(session);
}

void fillCountingRecords(CountingWaterRecordReader& reader) {
    const std::uint32_t today = testNowSeconds();
    for (std::uint32_t i = 0; i < 40; ++i) {
        reader.records.push_back(makeWebRecord(today - i * 3600UL, 500 + i));
    }
}

struct WebFixture {
    SystemConfig config = makeDefaultConfig();
    FakeConfigBackend backend;
    ConfigStore configStore{backend};
    StatisticsStore statistics;
    FilterStore filters{config.filters};
    CountingWaterRecordReader records;
    MemoryRecordWriter recordWriter;
    MemoryFileBackend calibrationFiles;
    MeteringSchemeStore meteringSchemes{calibrationFiles, "/metering-schemes.bin"};
    CalibrationSessionFileStore sessionStore{calibrationFiles, "/cal-session.bin"};
    CalibrationSessionTraceStore traceStore{calibrationFiles, "/cal-traces.bin"};
    FakeAdcReader adc;
    WaterSensorManager waterSensors{adc};
    AppController app{config,
                      statistics,
                      filters,
                      recordWriter,
                      nullptr,
                      &sessionStore,
                      &traceStore,
                      &waterSensors};

    WebFixture() {
        adc.values[0] = okMv(1100);
        adc.values[1] = okMv(1634);
        adc.values[2] = okMv(24);
        waterSensors.configure(config);
        waterSensors.begin();
        TEST_ASSERT_TRUE(sessionStore.begin());
        TEST_ASSERT_TRUE(traceStore.begin());
        applyTestMeteringScheme(app);
        installContext(records);
    }

    void installContext(AppController& contextApp, const WaterRecordReader& recordReader) {
        FaucetWebContext context{};
        context.config = &config;
        context.configStore = &configStore;
        context.statistics = &statistics;
        context.app = &contextApp;
        context.filters = &filters;
        context.records = &recordReader;
        context.meteringSchemes = &meteringSchemes;
        context.calibrationSessions = &sessionStore;
        context.calibrationSessionTraces = &traceStore;
        context.nowSeconds = testNowSeconds;
        context.bootId = testBootId;
        context.afterFormatFs = countAfterFormatFsNotification;
        setFaucetWebContext(context);
    }

    void installContext(const WaterRecordReader& recordReader) {
        installContext(app, recordReader);
    }
};

void registerRoutes() {
    Esp32BaseWeb::nativeTestReset();
    TEST_ASSERT_TRUE(registerFaucetWeb());
}

void beginWebRequest(Esp32BaseWeb::Method method,
                     const char* path,
                     bool authenticated = true,
                     bool sameOrigin = true) {
    Esp32BaseWeb::nativeTestBeginRequest(method, path);
    Esp32BaseWeb::nativeTestSetAuthenticated(authenticated);
    Esp32BaseWeb::nativeTestSetSameOrigin(sameOrigin);
}

void beginWebGet(const char* path) {
    beginWebRequest(Esp32BaseWeb::METHOD_GET, path);
}

void beginWebPost(const char* path, bool authenticated = true, bool sameOrigin = true) {
    beginWebRequest(Esp32BaseWeb::METHOD_POST, path, authenticated, sameOrigin);
}

void setManualMeteringParams(const char* name = "手工参数") {
    Esp32BaseWeb::nativeTestSetParam("name", name);
    Esp32BaseWeb::nativeTestSetParam("startupPulseCount", "10");
    Esp32BaseWeb::nativeTestSetParam("startupVolumeMl", "210");
    Esp32BaseWeb::nativeTestSetParam("stablePulsePerLiter", "420");
    Esp32BaseWeb::nativeTestSetParam("startupDurationSec", "3.5");
    Esp32BaseWeb::nativeTestSetParam("stableFlowMlPerMin", "1600");
}

void beginPresetPost(const char* action) {
    beginWebPost("/api/faucet/presets");
    if (action) {
        Esp32BaseWeb::nativeTestSetParam("action", action);
    }
}

void dispatchPresetPost() {
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/presets", Esp32BaseWeb::METHOD_POST));
}

void dispatchFilterPost() {
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters", Esp32BaseWeb::METHOD_POST));
}

void enableTdsForFixture(WebFixture& fixture) {
    fixture.config.tdsKind = TdsKind::AnalogTdsAo;
    fixture.config.temperatureKind = TemperatureKind::NtcBeta;
    TEST_ASSERT_TRUE(fixture.app.applyConfig(fixture.config));
}

}  // namespace

void test_home_page_initial_render_does_not_read_record_pages() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    fillCountingRecords(reader);
    fixture.installContext(reader);
    registerRoutes();
    beginWebGet("/index");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/index", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_UINT32(0, reader.readPageCalls);
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "id='inputVoltageStatus'"));
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "id='inputVoltageNote'"));
}

void test_app_css_covers_current_page_layout_classes() {
    WebFixture fixture;
    registerRoutes();
    beginWebGet("/faucet/app.css");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/app.css", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "--bg:#fbfcfb"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "body{max-width:1120px"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".machine-status-strip{display:flex;align-items:center;gap:7px;flex-wrap:wrap}"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".next-preset-control"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".machine-status-strip"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".today-total-meta"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".filter-head"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".calibration-session-badges"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".machine-task-card{display:flex"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".tds-calibration-summary>div"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".temperature-calibration-summary>div"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".active-metering-metrics"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".metering-metric{min-width:0"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".distribution-head"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".count-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".chart-unit"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".calibration-session-panel .status-pill"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".calibration-kpi-main"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".calibration-slot-table th,.metering-scheme-table th{font-weight:500}"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), ".filter-life-details"));
}

void test_filter_page_renders_replacement_date_and_life_details() {
    WebFixture fixture;
    FilterRecord filter = fixture.filters.record(0);
    filter.startTime = secondsSince2000(2026, 5, 1, 0, 0, 0);
    filter.startBootId = 0;
    filter.recommendDays = 180;
    filter.maxDays = 360;
    filter.lifeMl = 1200000;
    TEST_ASSERT_TRUE(fixture.filters.updateFilter(0, filter));
    registerRoutes();
    beginWebGet("/faucet/filters");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/filters", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "<th>上次更换</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "<th>寿命详情</th>"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "2026-05-01"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "建议/最短"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "6 个月</strong><small>180 天</small>"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "最长"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "12 个月</strong><small>360 天</small>"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "流量寿命"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "1200.00 L"));
}

void test_stats_page_renders_daily_charts_and_usage_panels() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    fillCountingRecords(reader);
    fixture.installContext(reader);
    registerRoutes();
    beginWebGet("/faucet/stats");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/stats", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "class='daily-chart'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "<svg viewBox='0 0 1080 292'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "chart-unit"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "count-line"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "class='usage-grid'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "最近 30 天分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "按容量段分布"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "按完成结果分布"));
}

void test_after_format_fs_notification_resets_runtime_statistics() {
    WebFixture fixture;
    StatisticsRecord record{};
    record.todayMl = 210;
    record.weekMl = 900;
    record.monthMl = 1234;
    record.totalMl = 4567;
    record.lastDayKey = 20260506;
    record.lastWeekKey = 202619;
    record.lastMonthKey = 202605;
    fixture.statistics = StatisticsStore(record);
    TEST_ASSERT_TRUE(fixture.configStore.saveStatistics(record));
    registerRoutes();

    Esp32BaseWeb::nativeTestNotifyToolsFormatFsSuccess(true, true);

    const StatisticsRecord live = fixture.app.snapshot().statistics;
    TEST_ASSERT_EQUAL_UINT32(0, live.todayMl);
    TEST_ASSERT_EQUAL_UINT32(0, live.weekMl);
    TEST_ASSERT_EQUAL_UINT32(0, live.monthMl);
    TEST_ASSERT_EQUAL_UINT32(0, live.totalMl);
    TEST_ASSERT_EQUAL_UINT32(20260506, live.lastDayKey);
    const StatisticsRecord persisted = fixture.configStore.loadStatistics({20260506, 202619, 202605});
    TEST_ASSERT_EQUAL_UINT32(0, persisted.todayMl);
    TEST_ASSERT_EQUAL_UINT32(0, persisted.totalMl);
}

void test_after_format_fs_notification_notifies_app_storage_rebuild() {
    WebFixture fixture;
    g_afterFormatFsNotifications = 0;
    registerRoutes();

    Esp32BaseWeb::nativeTestNotifyToolsFormatFsSuccess(true, true);

    TEST_ASSERT_EQUAL_UINT32(1, g_afterFormatFsNotifications);
}

void test_calibration_home_redirects_active_flow_session_to_workflow() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.app.startCalibrationSessionForWeb(testNowSeconds()));
    registerRoutes();

    beginWebGet("/faucet/calibration");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_page_preserves_active_session_state() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.app.startCalibrationSessionForWeb(testNowSeconds()));
    CalibrationSessionRecord before{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(before));
    registerRoutes();

    beginWebGet("/faucet/calibration/flow");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    CalibrationSessionRecord loaded{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(loaded));
    TEST_ASSERT_EQUAL_UINT32(before.sessionId, loaded.sessionId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(before.status),
                            static_cast<unsigned>(loaded.status));
}

void test_flow_calibration_page_renders_manual_metering_entry() {
    WebFixture fixture;
    registerRoutes();

    beginWebGet("/faucet/calibration/flow");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "历史参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "手工设置参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "修改当前参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='开始校准'"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "退出校准流程"));
}

void test_generated_metering_candidate_uses_compact_table_and_quality_warnings() {
    WebFixture fixture;
    registerRoutes();
    beginWebGet("/widget");
    TEST_ASSERT_TRUE(Esp32BaseWeb::beginResponse(200, "text/html; charset=utf-8", nullptr));
    MeteringSchemeCandidate candidate{};
    candidate.ready = true;
    candidate.sourceType = MeteringSchemeSource::CalibrationSession;
    candidate.params = MeteringParameters{190, 111, 2171, 7000, 1891};
    candidate.generatedAt = testNowSeconds();
    candidate.sampleCount = 2;
    candidate.minActualMl = 1000;
    candidate.maxActualMl = 1800;
    candidate.maxErrorMl = 40;
    candidate.maxErrorTenthPercent = 24;
    sendGeneratedMeteringCandidatePanel(candidate);
    Esp32BaseWeb::endResponse();

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "metering-candidate-table"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "启动脉冲"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "稳态流速"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "容量范围"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "最大误差"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "当前只有 2 条有效样本"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "样本容量跨度只有"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "active-metering-metrics"));
}

void test_applied_calibration_keeps_parameter_visible_and_hides_apply_button() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    MeteringSchemeCandidate candidate{};
    candidate.ready = true;
    candidate.sourceType = MeteringSchemeSource::CalibrationSession;
    candidate.params = MeteringParameters{190, 111, 2171, 7000, 1891};
    candidate.generatedAt = testNowSeconds();
    candidate.sampleCount = 2;
    candidate.minActualMl = 1000;
    candidate.maxActualMl = 1800;
    candidate.maxErrorMl = 20;
    candidate.maxErrorTenthPercent = 12;
    std::uint32_t schemeId = 0;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.saveCandidateAsCurrent(candidate,
                                                                    "校准生成计量方案",
                                                                    testNowSeconds() + 30,
                                                                    schemeId));
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::Applied;
    session.appliedSchemeId = schemeId;
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));

    registerRoutes();
    beginWebGet("/widget");
    TEST_ASSERT_TRUE(Esp32BaseWeb::beginResponse(200, "text/html; charset=utf-8", nullptr));
    AppSnapshot snapshot{};
    snapshot.calibrationStatus = CalibrationSessionStatus::Applied;
    snapshot.calibrationValidSampleCount = 2;
    snapshot.calibrationCanQuickGenerate = true;
    sendFlowCalibrationSessionPanel(snapshot, false);
    Esp32BaseWeb::endResponse();

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "已应用"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='开始新校准'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "根据现有样本重新生成参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "name='action' value='regenerate_session'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "metering-candidate-table"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "2171P/L"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "value='使用这组参数'"));
}

void test_applied_calibration_renders_regenerated_candidate_actions() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    MeteringSchemeCandidate appliedCandidate{};
    appliedCandidate.ready = true;
    appliedCandidate.sourceType = MeteringSchemeSource::CalibrationSession;
    appliedCandidate.params = MeteringParameters{190, 111, 2171, 7000, 1891};
    appliedCandidate.generatedAt = testNowSeconds();
    appliedCandidate.sampleCount = 2;
    appliedCandidate.minActualMl = 1000;
    appliedCandidate.maxActualMl = 1800;
    std::uint32_t schemeId = 0;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.saveCandidateAsCurrent(appliedCandidate,
                                                                    "校准生成计量方案",
                                                                    testNowSeconds() + 30,
                                                                    schemeId));
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::Applied;
    session.appliedSchemeId = schemeId;
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));

    registerRoutes();
    beginWebGet("/widget");
    TEST_ASSERT_TRUE(Esp32BaseWeb::beginResponse(200, "text/html; charset=utf-8", nullptr));
    AppSnapshot snapshot{};
    snapshot.calibrationStatus = CalibrationSessionStatus::Applied;
    snapshot.calibrationValidSampleCount = 2;
    snapshot.calibrationCanQuickGenerate = true;
    snapshot.calibrationCandidate = appliedCandidate;
    snapshot.calibrationCandidate.params = MeteringParameters{200, 120, 2200, 8000, 1800};
    snapshot.calibrationCandidate.generatedAt = testNowSeconds() + 60;
    sendFlowCalibrationSessionPanel(snapshot, false);
    Esp32BaseWeb::endResponse();

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "已应用"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "待应用"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "2200P/L"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='使用这组参数'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='放弃这组参数'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='再次重新生成'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "name='action' value='discard_candidate'"));
}

void test_metering_history_orders_newest_created_at_first() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    MeteringSchemeCandidate older{};
    older.ready = true;
    older.sourceType = MeteringSchemeSource::CalibrationSession;
    older.params = MeteringParameters{10, 20, 300, 1000, 1500};
    older.generatedAt = testNowSeconds() + 10;
    older.sampleCount = 2;
    older.minActualMl = 1000;
    older.maxActualMl = 2000;
    std::uint32_t olderId = 0;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.saveCandidateAsCurrent(older, "older history", older.generatedAt, olderId));
    MeteringSchemeCandidate newer = older;
    newer.params = MeteringParameters{11, 21, 301, 1000, 1500};
    newer.generatedAt = testNowSeconds() + 20;
    std::uint32_t newerId = 0;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.saveCandidateAsCurrent(newer, "newer history", newer.generatedAt, newerId));
    registerRoutes();

    beginWebGet("/faucet/calibration/flow");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    const std::size_t newerPos = body.find("newer history");
    const std::size_t olderPos = body.find("older history");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, newerPos);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, olderPos);
    TEST_ASSERT_LESS_THAN_size_t(olderPos, newerPos);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "创建："));
}

void test_flow_calibration_manual_page_prefills_current_parameters() {
    WebFixture fixture;
    registerRoutes();

    beginWebGet("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("manual", "1");
    Esp32BaseWeb::nativeTestSetParam("name", "复制参数");
    Esp32BaseWeb::nativeTestSetParam("startupPulseCount", "10");
    Esp32BaseWeb::nativeTestSetParam("startupVolumeMl", "210");
    Esp32BaseWeb::nativeTestSetParam("stablePulsePerLiter", "420");
    Esp32BaseWeb::nativeTestSetParam("startupDurationMs", "3500");
    Esp32BaseWeb::nativeTestSetParam("stableFlowMlPerMin", "1600");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "手工设置计量参数"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "name='action' value='create_metering_scheme'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='复制参数'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "name='startupDurationSec'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "value='3.500'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "data-base-stable-ppl='420'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "data-base-stable-flow='1600'"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "baseFlow*basePpl/next"));
}

void test_temperature_calibration_post_accepts_celsius_decimal_input() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    fixture.app.tick(appInput({false, false, false, false}, 1000, 1000000));
    registerRoutes();
    beginWebPost("/faucet/calibration");
    Esp32BaseWeb::nativeTestSetParam("action", "temperature_save");
    Esp32BaseWeb::nativeTestSetParam("referenceC", "25.5");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?view=temperature&saved=temperature",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.app.config().temperatureCalibrated);
    TEST_ASSERT_TRUE(fixture.config.temperatureCalibrated);
}

void test_input_voltage_calibration_page_shows_raw_and_correspondence_columns() {
    WebFixture fixture;
    fixture.app.tick(appInput({false, false, false, false}, 1000, 1000000));
    registerRoutes();
    beginWebGet("/faucet/calibration");
    Esp32BaseWeb::nativeTestSetParam("view", "voltage");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "输入电压校准"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "ADC 原始值"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "理论输入"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "万用表实际电压"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "name='actualVoltage'"));
}

void test_flow_calibration_manual_post_creates_current_parameter() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    const std::uint32_t oldActiveId = fixture.meteringSchemes.activeSchemeId();
    TEST_ASSERT_EQUAL_UINT32(99, fixture.app.activeMeteringScheme().id);
    registerRoutes();

    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "create_metering_scheme");
    setManualMeteringParams("手工低压");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(oldActiveId, fixture.meteringSchemes.activeSchemeId());
    TEST_ASSERT_NOT_EQUAL(99, fixture.app.activeMeteringScheme().id);
    TEST_ASSERT_EQUAL_UINT32(fixture.meteringSchemes.activeSchemeId(), fixture.app.activeMeteringScheme().id);
    TEST_ASSERT_EQUAL_STRING("手工低压", fixture.app.activeMeteringScheme().name);
    TEST_ASSERT_EQUAL_UINT32(420, fixture.app.activeMeteringScheme().params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Manual),
                            static_cast<unsigned>(fixture.app.activeMeteringScheme().sourceType));
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponseHeader("Location"),
                                     "/faucet/calibration/flow?saved=metering_manual&createdScheme="));
}

void test_flow_calibration_manual_post_rejects_busy_without_changing_current() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    const std::uint32_t oldActiveId = fixture.meteringSchemes.activeSchemeId();
    setRunning(fixture.app);
    registerRoutes();

    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "create_metering_scheme");
    setManualMeteringParams("busy manual");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_UINT32(oldActiveId, fixture.meteringSchemes.activeSchemeId());
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=busy",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_rejects_unknown_write_action() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    const std::uint32_t oldActiveId = fixture.meteringSchemes.activeSchemeId();
    TEST_ASSERT_EQUAL_UINT32(99, fixture.app.activeMeteringScheme().id);
    registerRoutes();
    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "unknown_write");
    Esp32BaseWeb::nativeTestSetParam("name", "unknown current");
    Esp32BaseWeb::nativeTestSetParam("startupPulseCount", "12");
    Esp32BaseWeb::nativeTestSetParam("startupVolumeMl", "345");
    Esp32BaseWeb::nativeTestSetParam("stablePulsePerLiter", "1234");
    Esp32BaseWeb::nativeTestSetParam("startupDurationSec", "3.450");
    Esp32BaseWeb::nativeTestSetParam("stableFlowMlPerMin", "2100");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_UINT32(oldActiveId, fixture.meteringSchemes.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(99, fixture.app.activeMeteringScheme().id);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_value",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_running_water_allows_read_only_business_pages() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    fillCountingRecords(reader);
    fixture.installContext(reader);
    setRunning(fixture.app);

    struct PageCase {
        const char* path;
    };
    const PageCase pages[] = {
        {"/faucet/records"},
        {"/faucet/stats"},
        {"/faucet/calibration"},
        {"/faucet/calibration/flow"},
    };
    for (const PageCase& page : pages) {
        registerRoutes();
        beginWebGet(page.path);

        TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch(page.path, Esp32BaseWeb::METHOD_GET));

        TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
        TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("\"error\":\"busy\""));
    }
}

void test_records_api_filters_by_documented_date_params() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    reader.records.push_back(makeWebRecord(secondsSince2000(2026, 5, 4, 8, 0, 0), 4404));
    reader.records.push_back(makeWebRecord(secondsSince2000(2026, 5, 3, 8, 0, 0), 3303));
    reader.records.push_back(makeWebRecord(secondsSince2000(2026, 5, 2, 8, 0, 0), 2202));
    reader.records.push_back(makeWebRecord(secondsSince2000(2026, 5, 1, 8, 0, 0), 1101));
    fixture.installContext(reader);
    registerRoutes();
    beginWebGet("/api/faucet/records");
    Esp32BaseWeb::nativeTestSetParam("startDate", "2026-05-02");
    Esp32BaseWeb::nativeTestSetParam("endDate", "2026-05-03");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/records", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "\"total\":2"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "\"volumeMl\":3303"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "\"volumeMl\":2202"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "\"volumeMl\":4404"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "\"volumeMl\":1101"));
}

void test_records_api_caps_page_size_to_json_buffer_safe_limit() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    for (std::uint32_t i = 0; i < 100; ++i) {
        reader.records.push_back(makeWebRecord(secondsSince2000(2026, 5, 4, 8, 0, 0) - i * 60UL, 1000 + i));
    }
    fixture.installContext(reader);
    registerRoutes();
    beginWebGet("/api/faucet/records");
    Esp32BaseWeb::nativeTestSetParam("pageSize", "200");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/records", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "\"pageSize\":80"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "\"count\":80"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "buffer_too_small"));
}

void test_records_page_caps_manual_page_size_to_lightweight_limit() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    for (std::uint32_t i = 0; i < 100; ++i) {
        reader.records.push_back(makeWebRecord(secondsSince2000(2026, 5, 4, 8, 0, 0) - i * 60UL, 1000 + i));
    }
    fixture.installContext(reader);
    registerRoutes();
    beginWebGet("/faucet/records");
    Esp32BaseWeb::nativeTestSetParam("pageSize", "200");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/records", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_UINT16(50, reader.lastPageSize);
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "pageSize=50"));
}

void test_filter_save_rejects_missing_enabled_without_changing_live_state() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/api/faucet/filters");
    Esp32BaseWeb::nativeTestSetParam("index", "0");

    dispatchFilterPost();

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?error=invalid_enabled",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.filters.record(0).enabled);
    TEST_ASSERT_EQUAL_INT32(0, fixture.backend.getInt("faucet_cfg", "ver", 0));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.backend.filterRuntimeWrites);
}

void test_filter_save_allows_explicit_disabled_form_submission() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/api/faucet/filters");
    Esp32BaseWeb::nativeTestSetParam("index", "0");
    Esp32BaseWeb::nativeTestSetParam("enabledSet", "1");

    dispatchFilterPost();

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?saved=1", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_FALSE(fixture.filters.record(0).enabled);
    const SystemConfig persisted = fixture.configStore.loadSystemConfig();
    TEST_ASSERT_FALSE(persisted.filters[0].enabled);
}

void test_filter_save_accepts_explicit_enabled_zero_value() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/api/faucet/filters");
    Esp32BaseWeb::nativeTestSetParam("index", "0");
    Esp32BaseWeb::nativeTestSetParam("enabled", "0");

    dispatchFilterPost();

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?saved=1", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_FALSE(fixture.filters.record(0).enabled);
    const SystemConfig persisted = fixture.configStore.loadSystemConfig();
    TEST_ASSERT_FALSE(persisted.filters[0].enabled);
}

void test_filter_reset_handler_rejects_missing_auth_before_context_work() {
    registerRoutes();
    beginWebPost("/api/faucet/filters/reset", false);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);
}

void test_filter_reset_handler_rejects_cross_origin_post() {
    registerRoutes();
    beginWebPost("/api/faucet/filters/reset", true, false);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(403, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("Forbidden", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_filter_reset_handler_returns_invalid_index_without_runtime_write() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetParam("index", "999");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(400, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_index\"}", Esp32BaseWeb::nativeTestResponse().body.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, fixture.backend.filterRuntimeWrites);
}

void test_filter_reset_handler_redirects_busy_before_runtime_write() {
    WebFixture fixture;
    const std::uint32_t originalStartTime = fixture.filters.record(0).startTime;
    setRunning(fixture.app);
    registerRoutes();
    beginWebPost("/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetParam("index", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT32(originalStartTime, fixture.filters.record(0).startTime);
    TEST_ASSERT_EQUAL_UINT32(0, fixture.backend.filterRuntimeWrites);
}

void test_filter_reset_handler_persists_runtime_start_time() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetParam("index", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?reset=1", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT32(testNowSeconds(), fixture.filters.record(0).startTime);
    TEST_ASSERT_EQUAL_UINT32(0, fixture.filters.record(0).usedMl);
    char expected[16]{};
    std::snprintf(expected, sizeof(expected), "%lu", static_cast<unsigned long>(testNowSeconds()));
    char text[16]{};
    TEST_ASSERT_TRUE(fixture.backend.getStr("faucet_run", "f0_start", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING(expected, text);
    TEST_ASSERT_TRUE(fixture.backend.getStr("faucet_run", "f0_used", text, sizeof(text), ""));
    TEST_ASSERT_EQUAL_STRING("0", text);
}

void test_flow_calibration_session_start_redirects_busy_to_flow_center() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_session_start_redirects_success_from_idle() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?saved=session_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(fixture.app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app.snapshot().calibrationStatus));
}

void test_calibration_session_start_recovers_missing_session_file_after_format() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.calibrationFiles.removeFile("/cal-session.bin"));
    registerRoutes();
    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?saved=session_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.calibrationFiles.exists("/cal-session.bin"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app.snapshot().calibrationStatus));
}

void test_calibration_detail_reads_persisted_session_trace_without_ram_cache() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebSessionAttempt(fixture.traceStore, session, 0, CalibrationAttemptStatus::Valid, 1500);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    registerRoutes();

    beginWebGet("/faucet/calibration/detail");
    Esp32BaseWeb::nativeTestSetParam("slot", "0");
    Esp32BaseWeb::nativeTestSetParam("bucket", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/detail", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_GREATER_THAN_size_t(0, body.size());
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "pulse-detail-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "<svg"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "目标预计总脉冲"));
}

void test_calibration_detail_reads_persisted_bucket_trace_without_ram_cache() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebCompactSessionAttempt(fixture.traceStore, session, 0, 1500);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    registerRoutes();

    beginWebGet("/faucet/calibration/detail");
    Esp32BaseWeb::nativeTestSetParam("slot", "0");
    Esp32BaseWeb::nativeTestSetParam("bucket", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/detail", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_GREATER_THAN_size_t(0, body.size());
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "pulse-detail-chart"));
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "2s"));
}

void test_record_detail_does_not_render_pulse_chart_for_normal_records() {
    WebFixture fixture;
    WaterRecord record = makeWebRecord(testNowSeconds(), 1000);
    record.meteringSchemeId = 0;
    record.pulseCount = 40;
    record.durationSec = 8;
    fixture.records.records.push_back(record);
    registerRoutes();

    beginWebGet("/faucet/records/detail");
    Esp32BaseWeb::nativeTestSetParam("start", "1714502400");
    Esp32BaseWeb::nativeTestSetParam("boot", "0");
    Esp32BaseWeb::nativeTestSetParam("volume", "1000");
    Esp32BaseWeb::nativeTestSetParam("target", "1000");
    Esp32BaseWeb::nativeTestSetParam("pulses", "40");
    Esp32BaseWeb::nativeTestSetParam("filtered", "0");
    Esp32BaseWeb::nativeTestSetParam("duration", "8");
    Esp32BaseWeb::nativeTestSetParam("mode", "0");
    Esp32BaseWeb::nativeTestSetParam("result", "0");
    Esp32BaseWeb::nativeTestSetParam("preset", "1");
    Esp32BaseWeb::nativeTestSetParam("temp", "0");
    Esp32BaseWeb::nativeTestSetParam("tds", "0");
    Esp32BaseWeb::nativeTestSetParam("sensorSamples", "0");
    Esp32BaseWeb::nativeTestSetParam("sensorFlags", "0");
    Esp32BaseWeb::nativeTestSetParam("scheme", "0");
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/records/detail", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(body.c_str(), "接水详情"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "pulse-detail-chart"));
    TEST_ASSERT_NULL(std::strstr(body.c_str(), "脉冲趋势"));
}

void test_tds_calibration_start_redirects_busy_to_calibration_page() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    setRunning(fixture.app);
    registerRoutes();
    beginWebPost("/faucet/calibration");
    Esp32BaseWeb::nativeTestSetParam("action", "tds_start_session");
    Esp32BaseWeb::nativeTestSetParam("referencePpm", "10");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?view=tds&error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_tds_calibration_start_redirects_success_from_idle() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    registerRoutes();
    beginWebPost("/faucet/calibration");
    Esp32BaseWeb::nativeTestSetParam("action", "tds_start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?view=tds&saved=tds_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.app.tdsCalibrationSnapshot().sessionActive);
}

void test_tds_calibration_save_persists_config_after_stable_samples() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    TEST_ASSERT_TRUE(fixture.app.startTdsCalibrationSessionForWeb(testNowSeconds()));
    TEST_ASSERT_TRUE(fixture.app.startTdsCalibrationPointForWeb(10, testNowSeconds()));
    for (std::uint32_t i = 1; i <= 12; ++i) {
        fixture.waterSensors.tick(i * 1000UL);
    }
    TEST_ASSERT_TRUE(fixture.app.tdsCalibrationSnapshot().readyToSave);
    TEST_ASSERT_TRUE(fixture.app.saveTdsCalibrationPointForWeb(testNowSeconds()));
    TEST_ASSERT_TRUE(fixture.app.tdsCalibrationSnapshot().candidateReady);
    registerRoutes();
    beginWebPost("/faucet/calibration");
    Esp32BaseWeb::nativeTestSetParam("action", "tds_apply_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?view=tds&saved=tds_saved",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    const SystemConfig persisted = fixture.configStore.loadSystemConfig();
    TEST_ASSERT_TRUE(persisted.tdsCalibrated);
    TEST_ASSERT_TRUE(persisted.tdsScale > 0.0f);
}

void test_calibration_post_rejects_missing_action_as_invalid_action() {
    WebFixture fixture;
    registerRoutes();
    beginWebPost("/faucet/calibration");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=invalid_action",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_unknown_flow_calibration_write_is_rejected_while_running() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    beginWebPost("/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetParam("action", "unknown_write");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_value",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_presets_handler_rejects_missing_auth_before_context_work() {
    registerRoutes();
    beginWebPost("/api/faucet/presets", false);
    Esp32BaseWeb::nativeTestSetParam("action", "select_next");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);
}

void test_presets_handler_rejects_cross_origin_post() {
    registerRoutes();
    beginWebPost("/api/faucet/presets", true, false);
    Esp32BaseWeb::nativeTestSetParam("action", "select_next");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(403, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("Forbidden", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_presets_handler_rejects_invalid_action() {
    WebFixture fixture;
    registerRoutes();
    beginPresetPost("not_a_preset_action");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(400, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_action\"}", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_presets_handler_select_next_and_previous_return_status_json() {
    WebFixture fixture;
    registerRoutes();
    beginPresetPost("select_next");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "\"selectedPreset\":1"));
    TEST_ASSERT_EQUAL_size_t(1, fixture.app.snapshot().water.selectedPreset);

    beginPresetPost("select_previous");
    dispatchPresetPost();

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "\"selectedPreset\":0"));
    TEST_ASSERT_EQUAL_size_t(0, fixture.app.snapshot().water.selectedPreset);
}

void test_presets_handler_selects_requested_enabled_preset() {
    WebFixture fixture;
    registerRoutes();
    beginPresetPost("select");
    Esp32BaseWeb::nativeTestSetParam("index", "1");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "\"selectedPreset\":1"));
    TEST_ASSERT_EQUAL_size_t(1, fixture.app.snapshot().water.selectedPreset);
}

void test_presets_handler_saves_requested_preset() {
    WebFixture fixture;
    registerRoutes();
    beginPresetPost(nullptr);
    Esp32BaseWeb::nativeTestSetParam("index", "2");
    Esp32BaseWeb::nativeTestSetParam("enabled", "on");
    Esp32BaseWeb::nativeTestSetParam("type", "time");
    Esp32BaseWeb::nativeTestSetParam("value", "90");
    Esp32BaseWeb::nativeTestSetParam("name", "Tea");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"restartRecommended\":true}",
                             Esp32BaseWeb::nativeTestResponse().body.c_str());
    const SystemConfig persisted = fixture.configStore.loadSystemConfig();
    TEST_ASSERT_TRUE(persisted.presets[2].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(PresetType::Time),
                            static_cast<std::uint8_t>(persisted.presets[2].type));
    TEST_ASSERT_EQUAL_UINT32(90, persisted.presets[2].value);
    TEST_ASSERT_EQUAL_STRING("Tea", persisted.presets[2].name);
    TEST_ASSERT_EQUAL_UINT32(90, fixture.app.config().presets[2].value);
}

void test_presets_handler_running_select_next_only_changes_next_preset() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    beginPresetPost("select_next");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "\"state\":\"running\""));
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "\"selectedPreset\":1"));
    TEST_ASSERT_NOT_NULL(std::strstr(Esp32BaseWeb::nativeTestResponse().body.c_str(), "\"activePreset\":0"));
    TEST_ASSERT_EQUAL_size_t(1, fixture.app.snapshot().water.selectedPreset);
    TEST_ASSERT_EQUAL_size_t(0, fixture.app.snapshot().water.activePreset);
    TEST_ASSERT_EQUAL_UINT32(1500, fixture.app.snapshot().water.targetValue);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_home_page_initial_render_does_not_read_record_pages);
    RUN_TEST(test_app_css_covers_current_page_layout_classes);
    RUN_TEST(test_filter_page_renders_replacement_date_and_life_details);
    RUN_TEST(test_stats_page_renders_daily_charts_and_usage_panels);
    RUN_TEST(test_after_format_fs_notification_resets_runtime_statistics);
    RUN_TEST(test_after_format_fs_notification_notifies_app_storage_rebuild);
    RUN_TEST(test_calibration_home_redirects_active_flow_session_to_workflow);
    RUN_TEST(test_flow_calibration_page_preserves_active_session_state);
    RUN_TEST(test_flow_calibration_page_renders_manual_metering_entry);
    RUN_TEST(test_generated_metering_candidate_uses_compact_table_and_quality_warnings);
    RUN_TEST(test_applied_calibration_keeps_parameter_visible_and_hides_apply_button);
    RUN_TEST(test_applied_calibration_renders_regenerated_candidate_actions);
    RUN_TEST(test_metering_history_orders_newest_created_at_first);
    RUN_TEST(test_flow_calibration_manual_page_prefills_current_parameters);
    RUN_TEST(test_temperature_calibration_post_accepts_celsius_decimal_input);
    RUN_TEST(test_input_voltage_calibration_page_shows_raw_and_correspondence_columns);
    RUN_TEST(test_flow_calibration_manual_post_creates_current_parameter);
    RUN_TEST(test_flow_calibration_manual_post_rejects_busy_without_changing_current);
    RUN_TEST(test_flow_calibration_rejects_unknown_write_action);
    RUN_TEST(test_running_water_allows_read_only_business_pages);
    RUN_TEST(test_records_api_filters_by_documented_date_params);
    RUN_TEST(test_records_api_caps_page_size_to_json_buffer_safe_limit);
    RUN_TEST(test_records_page_caps_manual_page_size_to_lightweight_limit);
    RUN_TEST(test_filter_save_rejects_missing_enabled_without_changing_live_state);
    RUN_TEST(test_filter_save_allows_explicit_disabled_form_submission);
    RUN_TEST(test_filter_save_accepts_explicit_enabled_zero_value);
    RUN_TEST(test_filter_reset_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_filter_reset_handler_rejects_cross_origin_post);
    RUN_TEST(test_filter_reset_handler_returns_invalid_index_without_runtime_write);
    RUN_TEST(test_filter_reset_handler_redirects_busy_before_runtime_write);
    RUN_TEST(test_filter_reset_handler_persists_runtime_start_time);
    RUN_TEST(test_flow_calibration_session_start_redirects_busy_to_flow_center);
    RUN_TEST(test_flow_calibration_session_start_redirects_success_from_idle);
    RUN_TEST(test_calibration_session_start_recovers_missing_session_file_after_format);
    RUN_TEST(test_calibration_detail_reads_persisted_session_trace_without_ram_cache);
    RUN_TEST(test_calibration_detail_reads_persisted_bucket_trace_without_ram_cache);
    RUN_TEST(test_record_detail_does_not_render_pulse_chart_for_normal_records);
    RUN_TEST(test_tds_calibration_start_redirects_busy_to_calibration_page);
    RUN_TEST(test_tds_calibration_start_redirects_success_from_idle);
    RUN_TEST(test_tds_calibration_save_persists_config_after_stable_samples);
    RUN_TEST(test_calibration_post_rejects_missing_action_as_invalid_action);
    RUN_TEST(test_unknown_flow_calibration_write_is_rejected_while_running);
    RUN_TEST(test_presets_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_presets_handler_rejects_cross_origin_post);
    RUN_TEST(test_presets_handler_rejects_invalid_action);
    RUN_TEST(test_presets_handler_select_next_and_previous_return_status_json);
    RUN_TEST(test_presets_handler_selects_requested_enabled_preset);
    RUN_TEST(test_presets_handler_saves_requested_preset);
    RUN_TEST(test_presets_handler_running_select_next_only_changes_next_preset);
    return UNITY_END();
}
