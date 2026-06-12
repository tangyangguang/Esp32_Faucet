#include <unity.h>

#define ESP32BASE_WEB_NATIVE_TEST 1

#include "app/AppController.h"
#include "app/ConfigStore.h"
#include "app/WaterRecordCalibrationStore.h"
#include "web/FaucetWeb.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../../../Esp32Base/src/web/Esp32BaseWeb.cpp"
#include "../../../src/web/FaucetWeb.cpp"

using namespace faucet;

namespace {

class FakeConfigBackend : public ConfigBackend {
public:
    std::uint32_t filterRuntimeWrites = 0;

    bool setInt(const char* ns, const char* key, std::int32_t value) override {
        ints[makeKey(ns, key)] = value;
        countFilterRuntimeWrite(ns);
        return true;
    }

    std::int32_t getInt(const char* ns, const char* key, std::int32_t def) override {
        const auto it = ints.find(makeKey(ns, key));
        return it == ints.end() ? def : it->second;
    }

    bool setBool(const char* ns, const char* key, bool value) override {
        bools[makeKey(ns, key)] = value;
        return true;
    }

    bool getBool(const char* ns, const char* key, bool def) override {
        const auto it = bools.find(makeKey(ns, key));
        return it == bools.end() ? def : it->second;
    }

    bool setStr(const char* ns, const char* key, const char* value) override {
        strings[makeKey(ns, key)] = value ? value : "";
        countFilterRuntimeWrite(ns);
        return true;
    }

    bool getStr(const char* ns, const char* key, char* out, std::size_t len, const char* def) override {
        if (!out || len == 0) {
            return false;
        }
        const auto it = strings.find(makeKey(ns, key));
        const std::string value = it == strings.end() ? std::string(def ? def : "") : it->second;
        std::strncpy(out, value.c_str(), len - 1);
        out[len - 1] = '\0';
        return it != strings.end();
    }

    bool clearNamespace(const char* ns) override {
        const std::string prefix = std::string(ns ? ns : "") + "/";
        erasePrefix(ints, prefix);
        erasePrefix(bools, prefix);
        erasePrefix(strings, prefix);
        return true;
    }

private:
    static std::string makeKey(const char* ns, const char* key) {
        return std::string(ns ? ns : "") + "/" + (key ? key : "");
    }

    void countFilterRuntimeWrite(const char* ns) {
        if (std::strcmp(ns ? ns : "", "faucet_run") == 0) {
            ++filterRuntimeWrites;
        }
    }

    template <typename T>
    void erasePrefix(std::map<std::string, T>& values, const std::string& prefix) {
        for (auto it = values.begin(); it != values.end();) {
            if (it->first.rfind(prefix, 0) == 0) {
                it = values.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::map<std::string, std::int32_t> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strings;
};

class MemoryRecordWriter : public WaterRecordWriter {
public:
    bool append(const WaterRecord& record) override {
        records.push_back(record);
        return true;
    }

    std::vector<WaterRecord> records;
};

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
    bool readyFlag = true;
};

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    std::uint32_t longTermSampleBulkReads = 0;
    std::uint32_t meteringSchemeRecordReads = 0;
    std::uint32_t calibrationSessionReads = 0;
    std::uint32_t calibrationSessionTraceReads = 0;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        if (!path) {
            return false;
        }
        files[path] = std::vector<std::uint8_t>(size, 0);
        return true;
    }

    bool appendBytes(const char* path, const std::uint8_t* data, std::size_t len) override {
        if (!path || (!data && len > 0)) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        const std::size_t oldSize = file.size();
        file.resize(oldSize + len, 0);
        if (len > 0) {
            std::memcpy(file.data() + oldSize, data, len);
        }
        return true;
    }

    bool readAt(const char* path, std::size_t offset, std::uint8_t* out, std::size_t len) override {
        if (!path || !out) {
            return false;
        }
        if (std::strcmp(path, "/cal-samples.bin") == 0 && len > 1024) {
            ++longTermSampleBulkReads;
        }
        if (std::strcmp(path, "/metering-schemes.bin") == 0 && offset >= 32 && len > 32) {
            ++meteringSchemeRecordReads;
        }
        if (std::strcmp(path, "/cal-session.bin") == 0) {
            ++calibrationSessionReads;
        }
        if (std::strcmp(path, "/cal-traces.bin") == 0) {
            ++calibrationSessionTraceReads;
        }
        const auto it = files.find(path);
        if (it == files.end() || offset + len > it->second.size()) {
            return false;
        }
        std::memcpy(out, it->second.data() + offset, len);
        return true;
    }

    bool writeAt(const char* path, std::size_t offset, const std::uint8_t* data, std::size_t len) override {
        if (!path || (!data && len > 0)) {
            return false;
        }
        std::vector<std::uint8_t>& file = files[path];
        if (offset + len > file.size()) {
            file.resize(offset + len, 0);
        }
        if (len > 0) {
            std::memcpy(file.data() + offset, data, len);
        }
        return true;
    }

    bool removeFile(const char* path) override {
        files.erase(path ? path : "");
        return true;
    }

private:
    std::map<std::string, std::vector<std::uint8_t>> files;
};

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
    initializeManualMeteringScheme(scheme, 99, "native", MeteringParameters{0, 0, 1000}, 1714502300);
    TEST_ASSERT_TRUE(app.applyActiveMeteringScheme(scheme));
}

std::size_t fillWebCalibrationSamples(WaterPulseTraceSample* samples,
                                       std::size_t capacity,
                                       std::uint32_t startupPulses,
                                       std::uint32_t stablePulses,
                                       std::uint32_t stableSeconds) {
    if (!samples || capacity < startupPulses + stablePulses || stableSeconds == 0) {
        return 0;
    }
    std::size_t count = 0;
    for (std::uint32_t sec = 0; sec < 5; ++sec) {
        const std::uint32_t pulsesThisSec = startupPulses / 5 + (sec < startupPulses % 5 ? 1 : 0);
        for (std::uint32_t i = 0; i < pulsesThisSec; ++i) {
            samples[count++] = WaterPulseTraceSample{static_cast<std::uint32_t>(sec * 1000000UL + i * 5000UL)};
        }
    }
    for (std::uint32_t sec = 0; sec < stableSeconds; ++sec) {
        const std::uint32_t pulsesThisSec = stablePulses / stableSeconds + (sec < stablePulses % stableSeconds ? 1 : 0);
        for (std::uint32_t i = 0; i < pulsesThisSec; ++i) {
            samples[count++] =
                WaterPulseTraceSample{static_cast<std::uint32_t>((5 + sec) * 1000000UL + i * 5000UL)};
        }
    }
    return count;
}

void saveLongTermWebSample(CalibrationLongTermSampleStore& sampleStore,
                           std::uint32_t actualMl,
                           std::uint32_t startupPulses,
                           std::uint32_t stablePulses,
                           std::uint32_t stableSeconds) {
    WaterPulseTraceSample samples[2048]{};
    const std::size_t sampleCount =
        fillWebCalibrationSamples(samples, 2048, startupPulses, stablePulses, stableSeconds);
    TEST_ASSERT_GREATER_THAN_size_t(0, sampleCount);
    CalibrationStoredTrace stored{};
    stored.valid = true;
    stored.pendingActual = false;
    stored.sessionId = 42;
    stored.attemptIndex = 1;
    stored.actualMl = actualMl;
    stored.savedAt = 1714502400UL;
    stored.trace.traceId = 700;
    stored.trace.startTime = 1714502400UL;
    stored.trace.record = WaterRecord{
        1714502400UL,
        actualMl,
        actualMl,
        startupPulses + stablePulses,
        0,
        static_cast<std::uint16_t>(5 + stableSeconds),
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        99,
        7,
        {0, 0, 0, 0},
    };
    stored.trace.sampleCount = sampleCount;
    stored.trace.totalPulses = startupPulses + stablePulses;
    stored.trace.actualMl = actualMl;
    stored.trace.pulseMinIntervalUs = kDefaultPulseMinIntervalUs;
    stored.trace.finalState = WaterPulseTraceState::Completed;
    stored.trace.finished = true;
    std::uint32_t sampleId = 0;
    TEST_ASSERT_TRUE(sampleStore.save(stored, samples, sampleCount, sampleId));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, sampleId);
}

std::uint32_t testNowSeconds() {
    return 1714502400UL;
}

std::uint32_t testBootId() {
    return 7UL;
}

WaterRecord makeWebRecord(std::uint32_t startTime, std::uint32_t volumeMl = 1000) {
    return WaterRecord{
        startTime,
        volumeMl,
        volumeMl,
        volumeMl / 10,
        0,
        30,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        1,
        7,
        {0, 0, 0, 0},
    };
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
    WaterRecord recordsStorage[4]{};
    WaterRecordStore records{recordsStorage, 4};
    WaterRecordCalibration calibrationsStorage[4]{};
    WaterRecordCalibrationStore calibrations{calibrationsStorage, 4};
    MemoryRecordWriter recordWriter;
    MemoryFileBackend calibrationFiles;
    MeteringSchemeStore meteringSchemes{calibrationFiles, "/metering-schemes.bin"};
    CalibrationSessionFileStore sessionStore{calibrationFiles, "/cal-session.bin"};
    CalibrationSessionTraceStore traceStore{calibrationFiles, "/cal-traces.bin"};
    CalibrationLongTermSampleStore sampleStore{calibrationFiles, "/cal-samples.bin"};
    AppController app{config,
                      statistics,
                      filters,
                      recordWriter,
                      nullptr,
                      &calibrations,
                      &sessionStore,
                      &traceStore,
                      &sampleStore};

    WebFixture() {
        TEST_ASSERT_TRUE(sessionStore.begin());
        TEST_ASSERT_TRUE(traceStore.begin());
        TEST_ASSERT_TRUE(sampleStore.begin());
        applyTestMeteringScheme(app);
        installContext(records);
    }

    void installContext(const WaterRecordReader& recordReader) {
        FaucetWebContext context{};
        context.config = &config;
        context.configStore = &configStore;
        context.app = &app;
        context.filters = &filters;
        context.records = &recordReader;
        context.recordCalibrations = &calibrations;
        context.recordCalibrationWriter = &calibrations;
        context.meteringSchemes = &meteringSchemes;
        context.calibrationSessions = &sessionStore;
        context.calibrationSessionTraces = &traceStore;
        context.calibrationLongTermSamples = &sampleStore;
        context.nowSeconds = testNowSeconds;
        context.bootId = testBootId;
        setFaucetWebContext(context);
    }
};

void registerRoutes() {
    Esp32BaseWeb::nativeTestReset();
    TEST_ASSERT_TRUE(registerFaucetWeb());
}

void beginPresetPost(const char* action) {
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/presets");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    if (action) {
        Esp32BaseWeb::nativeTestSetParam("action", action);
    }
}

void dispatchPresetPost() {
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/presets", Esp32BaseWeb::METHOD_POST));
}

}  // namespace

void test_home_page_places_screen_status_in_machine_hero_footer() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/index");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/index", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    const std::size_t machineHero = body.find("<div class='machine-hero'>");
    const std::size_t screenStatus = body.find("id='screenStatus'");
    const std::size_t screenFooter = body.find("machine-screen-footer");
    const std::size_t machineOverview = body.find("<div class='machine-overview'>");
    const std::size_t statusStrip = body.find("machine-status-strip");
    const std::size_t screenStatusItem = body.find("screenStatusItem");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, machineHero);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, screenStatus);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, screenFooter);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, machineOverview);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, statusStrip);
    TEST_ASSERT_EQUAL(std::string::npos, screenStatusItem);
    TEST_ASSERT_TRUE(machineHero < screenFooter);
    TEST_ASSERT_TRUE(screenFooter < screenStatus);
    TEST_ASSERT_TRUE(screenStatus < machineOverview);
    TEST_ASSERT_TRUE(screenStatus < statusStrip);
}

void test_home_page_initial_render_does_not_read_record_pages() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    fillCountingRecords(reader);
    fixture.installContext(reader);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/index");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/index", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("今日概览"));
    TEST_ASSERT_EQUAL_UINT32(0, reader.readPageCalls);
}

void test_stats_page_shows_zero_preset_distribution_when_no_recent_records() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/stats?partial=report");
    Esp32BaseWeb::nativeTestSetParam("partial", "report");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/stats", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("按预设分布"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("P1 ·"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<strong>0 次</strong><small>占 0%"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("最近 30 天没有可聚合的真实时间记录。"));
}

void test_stats_page_initial_render_does_not_read_record_pages() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    fillCountingRecords(reader);
    fixture.installContext(reader);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/stats");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/stats", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("统计报表"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("加载统计报表"));
    TEST_ASSERT_EQUAL_UINT32(0, reader.readPageCalls);
}

void test_calibration_page_initial_render_does_not_read_session_records() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(88, testNowSeconds());
    CalibrationAttempt attempt{};
    attempt.attemptIndex = 0;
    attempt.sessionTraceSlot = 0;
    attempt.record = makeWebRecord(testNowSeconds(), 1200);
    attempt.targetHintMl = 1200;
    attempt.actualMl = 1190;
    attempt.status = CalibrationAttemptStatus::Valid;
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.calibrationFiles.calibrationSessionReads = 0;
    fixture.calibrationFiles.calibrationSessionTraceReads = 0;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("本次校准接水记录"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("加载接水记录"));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.calibrationFiles.calibrationSessionReads);
    TEST_ASSERT_EQUAL_UINT32(0, fixture.calibrationFiles.calibrationSessionTraceReads);
}

void test_metering_page_initial_render_shows_scheme_list_and_sample_library() {
    WebFixture fixture;
    saveLongTermWebSample(fixture.sampleStore, 1200, 45, 360, 12);
    fixture.calibrationFiles.longTermSampleBulkReads = 0;
    fixture.calibrationFiles.meteringSchemeRecordReads = 0;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/metering");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/metering", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("计量方案列表"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("长期样本库"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("打开方案列表"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("打开生成面板"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("加载方案列表"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("加载样本库"));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.calibrationFiles.longTermSampleBulkReads);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(3, fixture.calibrationFiles.meteringSchemeRecordReads);
}

void test_metering_page_keeps_only_metering_description_collapsed() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/metering");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/metering", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    std::size_t detailsCount = 0;
    std::size_t pos = body.find("<details");
    while (pos != std::string::npos) {
        ++detailsCount;
        pos = body.find("<details", pos + 1);
    }
    TEST_ASSERT_EQUAL_size_t(1, detailsCount);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("查看计量说明"));
}

void test_filter_reset_handler_rejects_missing_auth_before_context_work() {
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(false);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);
}

void test_filter_reset_handler_rejects_cross_origin_post() {
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(false);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(403, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("Forbidden", Esp32BaseWeb::nativeTestResponse().body.c_str());
}

void test_filter_reset_handler_returns_invalid_index_without_runtime_write() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
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
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/filters/reset");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("index", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/filters/reset", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/filters?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT32(originalStartTime, fixture.filters.record(0).startTime);
    TEST_ASSERT_EQUAL_UINT32(0, fixture.backend.filterRuntimeWrites);
}

void test_records_handler_redirects_trace_save_busy_before_trace_store_work() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/records");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "save");
    Esp32BaseWeb::nativeTestSetParam("trace", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/records", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/records?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_records_handler_redirects_trace_delete_busy_to_calibration_context() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/records");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "delete");
    Esp32BaseWeb::nativeTestSetParam("trace", "1");
    Esp32BaseWeb::nativeTestSetParam("from", "calibration");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/api/faucet/records", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_calibration_session_start_redirects_busy_to_calibration_page() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_calibration_session_start_redirects_success_from_idle() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?saved=session_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(fixture.app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Preparing),
                            static_cast<unsigned>(fixture.app.snapshot().calibrationStatus));
}

void test_calibration_session_start_recovers_missing_session_file_after_format() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.calibrationFiles.removeFile("/cal-session.bin"));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?saved=session_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.calibrationFiles.exists("/cal-session.bin"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Preparing),
                            static_cast<unsigned>(fixture.app.snapshot().calibrationStatus));
}

void test_calibration_session_start_rejects_duplicate_start_as_invalid_state() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.app.startCalibrationSessionForWeb(testNowSeconds()));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=invalid_state",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_calibration_post_rejects_missing_action_as_invalid_action() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=invalid_action",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_metering_scheme_write_redirects_busy_to_metering_page() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/metering");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "enable_metering_scheme");
    Esp32BaseWeb::nativeTestSetParam("id", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/metering", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/metering?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_presets_handler_rejects_missing_auth_before_context_work() {
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/presets");
    Esp32BaseWeb::nativeTestSetAuthenticated(false);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "select_next");

    dispatchPresetPost();

    TEST_ASSERT_EQUAL(401, Esp32BaseWeb::nativeTestResponse().code);
}

void test_presets_handler_rejects_cross_origin_post() {
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/faucet/presets");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(false);
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
    RUN_TEST(test_home_page_places_screen_status_in_machine_hero_footer);
    RUN_TEST(test_home_page_initial_render_does_not_read_record_pages);
    RUN_TEST(test_stats_page_shows_zero_preset_distribution_when_no_recent_records);
    RUN_TEST(test_stats_page_initial_render_does_not_read_record_pages);
    RUN_TEST(test_calibration_page_initial_render_does_not_read_session_records);
    RUN_TEST(test_metering_page_initial_render_shows_scheme_list_and_sample_library);
    RUN_TEST(test_metering_page_keeps_only_metering_description_collapsed);
    RUN_TEST(test_filter_reset_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_filter_reset_handler_rejects_cross_origin_post);
    RUN_TEST(test_filter_reset_handler_returns_invalid_index_without_runtime_write);
    RUN_TEST(test_filter_reset_handler_redirects_busy_before_runtime_write);
    RUN_TEST(test_records_handler_redirects_trace_save_busy_before_trace_store_work);
    RUN_TEST(test_records_handler_redirects_trace_delete_busy_to_calibration_context);
    RUN_TEST(test_calibration_session_start_redirects_busy_to_calibration_page);
    RUN_TEST(test_calibration_session_start_redirects_success_from_idle);
    RUN_TEST(test_calibration_session_start_recovers_missing_session_file_after_format);
    RUN_TEST(test_calibration_session_start_rejects_duplicate_start_as_invalid_state);
    RUN_TEST(test_calibration_post_rejects_missing_action_as_invalid_action);
    RUN_TEST(test_metering_scheme_write_redirects_busy_to_metering_page);
    RUN_TEST(test_presets_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_presets_handler_rejects_cross_origin_post);
    RUN_TEST(test_presets_handler_rejects_invalid_action);
    RUN_TEST(test_presets_handler_select_next_and_previous_return_status_json);
    RUN_TEST(test_presets_handler_selects_requested_enabled_preset);
    RUN_TEST(test_presets_handler_running_select_next_only_changes_next_preset);
    return UNITY_END();
}
