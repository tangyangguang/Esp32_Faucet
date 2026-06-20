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

class FakeAdcReader : public AdcReader {
public:
    AdcReadResult values[4]{};

    bool begin() override {
        return true;
    }

    bool setRange(AdcChannel, AdcRange) override {
        return true;
    }

    AdcReadResult readSingleEnded(AdcChannel channel) override {
        return values[static_cast<std::size_t>(channel)];
    }
};

AdcReadResult okMv(std::int16_t mv) {
    AdcReadResult result{};
    result.ok = true;
    result.millivolts = mv;
    return result;
}

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

std::size_t countOccurrences(const std::string& text, const char* needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    const std::size_t needleLen = std::strlen(needle);
    while (needleLen > 0 && (pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needleLen;
    }
    return count;
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

void saveWebSessionAttempt(CalibrationSessionTraceStore& traceStore,
                           CalibrationSessionRecord& session,
                           std::uint8_t slot,
                           CalibrationAttemptStatus status,
                           std::uint32_t actualMl) {
    CalibrationAttempt attempt{};
    attempt.attemptIndex = slot;
    attempt.targetHintMl = actualMl > 0 ? actualMl : 1000;
    attempt.record = makeWebRecord(testNowSeconds() + slot * 10UL, attempt.targetHintMl);
    attempt.record.pulseCount = 260 + static_cast<std::uint32_t>(slot) * 50UL;
    attempt.record.durationSec = 11;
    attempt.status = status;
    attempt.actualMl = status == CalibrationAttemptStatus::Valid ? actualMl : 0;

    if (status == CalibrationAttemptStatus::Valid || status == CalibrationAttemptStatus::PendingActual) {
        WaterPulseTraceSample samples[512]{};
        const std::size_t sampleCount = fillWebCalibrationSamples(samples, 512, 40, attempt.record.pulseCount - 40, 6);
        TEST_ASSERT_GREATER_THAN_size_t(0, sampleCount);

        CalibrationStoredTrace stored{};
        stored.pendingActual = true;
        stored.sessionId = session.sessionId;
        stored.attemptIndex = slot;
        stored.trace.traceId = slot + 1;
        stored.trace.startTime = attempt.record.startTime;
        stored.trace.record = attempt.record;
        stored.trace.sampleCount = sampleCount;
        stored.trace.totalPulses = attempt.record.pulseCount;
        stored.trace.actualMl = actualMl;
        stored.trace.pulseMinIntervalUs = kDefaultPulseMinIntervalUs;
        stored.trace.finalState = WaterPulseTraceState::Completed;
        stored.trace.finished = true;
        TEST_ASSERT_TRUE(traceStore.savePending(slot, stored, samples, sampleCount));
        if (status == CalibrationAttemptStatus::Valid) {
            TEST_ASSERT_TRUE(traceStore.commitValid(slot, actualMl, attempt.record.startTime + 10));
        }
        attempt.sessionTraceSlot = slot;
    } else {
        attempt.sessionTraceSlot = kCalibrationSessionTraceSlots;
    }

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
    FakeAdcReader adc;
    WaterSensorManager waterSensors{adc};
    AppController app{config,
                      statistics,
                      filters,
                      recordWriter,
                      nullptr,
                      &calibrations,
                      &sessionStore,
                      &traceStore,
                      &sampleStore,
                      &waterSensors};

    WebFixture() {
        adc.values[0] = okMv(1100);
        adc.values[1] = okMv(1634);
        adc.values[2] = okMv(24);
        waterSensors.configure(config);
        waterSensors.begin();
        TEST_ASSERT_TRUE(sessionStore.begin());
        TEST_ASSERT_TRUE(traceStore.begin());
        TEST_ASSERT_TRUE(sampleStore.begin());
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
        context.recordCalibrations = &calibrations;
        context.recordCalibrationWriter = &calibrations;
        context.meteringSchemes = &meteringSchemes;
        context.calibrationSessions = &sessionStore;
        context.calibrationSessionTraces = &traceStore;
        context.calibrationLongTermSamples = &sampleStore;
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

void enableTdsForFixture(WebFixture& fixture) {
    fixture.config.tdsEnabled = true;
    fixture.config.tdsKind = TdsKind::AnalogTdsAo;
    fixture.config.temperatureEnabled = true;
    fixture.config.temperatureKind = TemperatureKind::Ntc50kB3950;
    TEST_ASSERT_TRUE(fixture.app.applyConfig(fixture.config));
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

void test_home_page_labels_only_second_enabled_preset_as_p2() {
    WebFixture fixture;
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        fixture.config.presets[i].enabled = false;
    }
    fixture.config.presets[1].enabled = true;
    std::strncpy(fixture.config.presets[1].name, "OnlySecond", sizeof(fixture.config.presets[1].name) - 1);
    TEST_ASSERT_TRUE(fixture.app.applyConfig(fixture.config));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/index");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/index", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<strong id='nextPresetLabel'>P2 · OnlySecond"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("<strong id='nextPresetLabel'>P1/1"));
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

void test_stats_page_uses_runtime_period_totals_when_record_file_is_empty() {
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
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/stats?partial=report");
    Esp32BaseWeb::nativeTestSetParam("partial", "report");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/stats", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<span>今日</span><strong>0.21 L</strong>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<span>本月"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<strong>1.23 L</strong>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<span>总累计</span><strong>4.57 L</strong>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<span>过去 30 天日均</span><strong>0.00 L</strong>"));
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

void test_stats_page_initial_render_shows_complete_report() {
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
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("最近 30 天出水量"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("按预设分布"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("正在生成统计报表"));
    TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("partial=report"));
    TEST_ASSERT_GREATER_THAN_UINT32(0, reader.readPageCalls);
}

void test_calibration_home_shows_three_expanded_sections_without_flow_tables() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<h3>流量计校准</h3>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<h3>温度校准</h3>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<h3>水质校准</h3>"));
    TEST_ASSERT_TRUE(body.find("<h3>流量计校准</h3>") < body.find("<h3>温度校准</h3>"));
    TEST_ASSERT_TRUE(body.find("<h3>温度校准</h3>") < body.find("<h3>水质校准</h3>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("href='/faucet/calibration/flow'"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("本次校准接水记录"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("计量方案列表"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("<details"));
}

void test_calibration_page_initial_render_shows_tds_controls() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("水质校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前 TDS"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("1. 低值校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("2. 高值校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("单点校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("高级：单点校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("参考来源"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("采样电压"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("A0 电压"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("tds_start_low"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("tds_start_high"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("tds_start_single"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("tds_save"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("只接受 ppm"));
}

void test_tds_calibration_prioritizes_two_point_flow() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    fixture.app.tick(appInput({false, false, false, false}, 1000, 1000000));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    const std::size_t workflow = body.find("tds-workflow-card");
    const std::size_t single = body.find("tds-single-panel");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, workflow);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, single);
    TEST_ASSERT_TRUE(workflow < single);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("两点校准", workflow));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("保存两点结果", workflow));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("单点校准", single));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("tds-advanced"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存当前采样"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("取消采样"));
}

void test_temperature_calibration_uses_simple_card_and_celsius_input() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    fixture.app.tick(appInput({false, false, false, false}, 1000, 1000000));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("temperature-calibration-panel"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("temperature-calibration-summary"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前水温"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("校准状态"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("name='referenceC'"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("value='保存参考温度'"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("name='referenceCentiC'"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("0.01C"));
}

void test_temperature_calibration_disables_save_when_sensor_is_disabled() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    const std::size_t button = body.find("value='保存参考温度'");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, button);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("disabled", button));
}

void test_temperature_calibration_post_accepts_celsius_decimal_input() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    fixture.app.tick(appInput({false, false, false, false}, 1000, 1000000));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "temperature_save");
    Esp32BaseWeb::nativeTestSetParam("referenceC", "25.5");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?saved=temperature",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.app.config().temperatureCalibrated);
    TEST_ASSERT_TRUE(fixture.config.temperatureCalibrated);
}

void test_flow_calibration_center_initial_render_shows_current_parameter_workflow() {
    WebFixture fixture;
    saveLongTermWebSample(fixture.sampleStore, 1200, 45, 360, 12);
    fixture.calibrationFiles.longTermSampleBulkReads = 0;
    fixture.calibrationFiles.meteringSchemeRecordReads = 0;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<h2>流量计校准</h2>"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前计量参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("本次校准样本"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("生成推荐方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("请先生成参数"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("计量方案列表"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("长期样本库与参数生成"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("长期样本库"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("最多 6 次接水"));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(3, fixture.calibrationFiles.meteringSchemeRecordReads);
}

void test_flow_calibration_history_uses_parameter_language() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("历史参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("手工输入参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("复制参数"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("删除方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存为新方案"));
}

void test_flow_calibration_manual_input_prefills_copied_parameters() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("manual", "1");
    Esp32BaseWeb::nativeTestSetParam("startupPulseCount", "12");
    Esp32BaseWeb::nativeTestSetParam("startupVolumeMl", "345");
    Esp32BaseWeb::nativeTestSetParam("stablePulsePerLiter", "1234");
    Esp32BaseWeb::nativeTestSetParam("startupDurationMs", "3450");
    Esp32BaseWeb::nativeTestSetParam("stableFlowMlPerMin", "2100");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("<h2>手工输入参数</h2>"));
    const std::size_t startupPulseField = body.find("name='startupPulseCount'");
    const std::size_t startupVolumeField = body.find("name='startupVolumeMl'");
    const std::size_t stablePplField = body.find("name='stablePulsePerLiter'");
    const std::size_t startupDurationField = body.find("name='startupDurationSec'");
    const std::size_t stableFlowField = body.find("name='stableFlowMlPerMin'");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, startupPulseField);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, startupVolumeField);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, stablePplField);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, startupDurationField);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, stableFlowField);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("value='12'", startupPulseField));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("value='345'", startupVolumeField));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("value='1234'", stablePplField));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("value='3.450'", startupDurationField));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("value='2100'", stableFlowField));
}

void test_flow_calibration_manual_save_becomes_active_parameter() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    const std::uint32_t oldActiveId = fixture.meteringSchemes.activeSchemeId();
    TEST_ASSERT_EQUAL_UINT32(99, fixture.app.activeMeteringScheme().id);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "create_metering_scheme");
    Esp32BaseWeb::nativeTestSetParam("name", "manual current");
    Esp32BaseWeb::nativeTestSetParam("startupPulseCount", "12");
    Esp32BaseWeb::nativeTestSetParam("startupVolumeMl", "345");
    Esp32BaseWeb::nativeTestSetParam("stablePulsePerLiter", "1234");
    Esp32BaseWeb::nativeTestSetParam("startupDurationSec", "3.450");
    Esp32BaseWeb::nativeTestSetParam("stableFlowMlPerMin", "2100");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL_UINT32(oldActiveId, fixture.meteringSchemes.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(fixture.meteringSchemes.activeSchemeId(), fixture.app.activeMeteringScheme().id);
    TEST_ASSERT_EQUAL_UINT32(1234, fixture.app.activeMeteringScheme().params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?saved=scheme_created",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_advanced_sample_library_does_not_present_primary_apply_flow() {
    WebFixture fixture;
    saveLongTermWebSample(fixture.sampleStore, 1200, 45, 360, 12);
    saveLongTermWebSample(fixture.sampleStore, 3600, 45, 1080, 36);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("advanced", "samples");
    Esp32BaseWeb::nativeTestSetParam("generated", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("高级样本库"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("辅助计算"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("带入手工输入"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存为新方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("应用到当前参数"));
}

void test_advanced_sample_library_rejects_old_save_generated_action() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    MeteringSchemeRecord before[4]{};
    const std::size_t beforeCount = fixture.meteringSchemes.list(before, 4, true);
    saveLongTermWebSample(fixture.sampleStore, 1200, 45, 360, 12);
    saveLongTermWebSample(fixture.sampleStore, 3600, 45, 1080, 36);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "save_generated_scheme");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_action",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    MeteringSchemeRecord after[4]{};
    TEST_ASSERT_EQUAL_size_t(beforeCount, fixture.meteringSchemes.list(after, 4, true));
}

void test_flow_calibration_rejects_old_set_active_action() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.begin());
    MeteringParameters params{12, 345, 1234, 3450, 2100};
    std::uint32_t historyId = 0;
    TEST_ASSERT_TRUE(fixture.meteringSchemes.createManual("history", params, testNowSeconds(), historyId));
    const std::uint32_t activeBefore = fixture.meteringSchemes.activeSchemeId();
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "set_active_metering_scheme");
    Esp32BaseWeb::nativeTestSetParam("id", "2");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_action",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_EQUAL_UINT32(activeBefore, fixture.meteringSchemes.activeSchemeId());
    TEST_ASSERT_NOT_EQUAL_UINT32(historyId, fixture.app.activeMeteringScheme().id);
}

void test_flow_calibration_notice_uses_history_sample_language() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("saved", "long_term_sample");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("样本已存入历史样本"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("样本已存入长期样本库"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("长期样本库已满"));
}

void test_flow_calibration_error_uses_history_sample_language() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("error", "long_term_sample_full");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("历史样本已满"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("长期样本库已满"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("请先生成参数"));
}

void test_flow_calibration_center_uses_no_collapsed_sections() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL(std::string::npos, body.find("<details"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("查看计量说明"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("开始校准流程"));
}

void test_running_water_allows_read_only_business_pages() {
    WebFixture fixture;
    CountingWaterRecordReader reader;
    fillCountingRecords(reader);
    fixture.installContext(reader);
    setRunning(fixture.app);

    struct PageCase {
        const char* path;
        const char* expected;
    };
    const PageCase pages[] = {
        {"/faucet/records", "<h2>记录</h2>"},
        {"/faucet/stats", "按预设分布"},
        {"/faucet/calibration", "水质校准"},
        {"/faucet/calibration/flow", "当前计量参数"},
    };
    for (const PageCase& page : pages) {
        registerRoutes();
        Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, page.path);
        Esp32BaseWeb::nativeTestSetAuthenticated(true);
        Esp32BaseWeb::nativeTestSetSameOrigin(true);

        TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch(page.path, Esp32BaseWeb::METHOD_GET));

        TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
        TEST_ASSERT_NOT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find(page.expected));
        TEST_ASSERT_EQUAL(std::string::npos, Esp32BaseWeb::nativeTestResponse().body.find("\"error\":\"busy\""));
    }
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

void test_flow_calibration_session_start_redirects_busy_to_flow_center() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_session_start_redirects_success_from_idle() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?saved=session_started",
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
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?saved=session_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.calibrationFiles.exists("/cal-session.bin"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Preparing),
                            static_cast<unsigned>(fixture.app.snapshot().calibrationStatus));
}

void test_calibration_session_start_rejects_duplicate_start_as_invalid_state() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.app.startCalibrationSessionForWeb(testNowSeconds()));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "start_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_state",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_remove_sample_redirects_success() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebSessionAttempt(fixture.traceStore, session, 0, CalibrationAttemptStatus::Valid, 1500);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    AppController reloaded(fixture.config,
                           fixture.statistics,
                           fixture.filters,
                           fixture.recordWriter,
                           nullptr,
                           &fixture.calibrations,
                           &fixture.sessionStore,
                           &fixture.traceStore,
                           &fixture.sampleStore,
                           &fixture.waterSensors);
    applyTestMeteringScheme(reloaded);
    fixture.installContext(reloaded, fixture.records);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "remove_sample");
    Esp32BaseWeb::nativeTestSetParam("attemptIndex", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?saved=sample_removed",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_remove_sample_redirects_invalid_value_for_bad_attempt_index() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "remove_sample");
    Esp32BaseWeb::nativeTestSetParam("attemptIndex", "999");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_value",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_remove_sample_redirects_invalid_value_for_missing_attempt_index() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "remove_sample");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_value",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_remove_sample_redirects_invalid_state_when_sample_not_removable() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebSessionAttempt(fixture.traceStore, session, 0, CalibrationAttemptStatus::Removed, 0);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    AppController reloaded(fixture.config,
                           fixture.statistics,
                           fixture.filters,
                           fixture.recordWriter,
                           nullptr,
                           &fixture.calibrations,
                           &fixture.sessionStore,
                           &fixture.traceStore,
                           &fixture.sampleStore,
                           &fixture.waterSensors);
    applyTestMeteringScheme(reloaded);
    fixture.installContext(reloaded, fixture.records);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "remove_sample");
    Esp32BaseWeb::nativeTestSetParam("attemptIndex", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=invalid_state",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_flow_calibration_remove_sample_redirects_busy_without_changing_sample() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebSessionAttempt(fixture.traceStore, session, 0, CalibrationAttemptStatus::Valid, 1500);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "remove_sample");
    Esp32BaseWeb::nativeTestSetParam("attemptIndex", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=busy",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    CalibrationSessionRecord after{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(after));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Valid),
                            static_cast<unsigned>(after.attempts[0].status));
}

void test_flow_calibration_sample_table_only_shows_remove_for_active_samples() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebSessionAttempt(fixture.traceStore, session, 0, CalibrationAttemptStatus::Valid, 1500);
    saveWebSessionAttempt(fixture.traceStore, session, 1, CalibrationAttemptStatus::PendingActual, 0);
    saveWebSessionAttempt(fixture.traceStore, session, 2, CalibrationAttemptStatus::Removed, 0);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_EQUAL_size_t(2, countOccurrences(body, "value='remove_sample'"));
    const std::size_t removedStatus = body.find("已移除");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, removedStatus);
    const std::size_t nextRemove = body.find("value='remove_sample'", removedStatus);
    TEST_ASSERT_EQUAL(std::string::npos, nextRemove);
}

void test_tds_calibration_start_redirects_busy_to_calibration_page() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "tds_start_single");
    Esp32BaseWeb::nativeTestSetParam("referencePpm", "10");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
}

void test_tds_calibration_start_redirects_success_from_idle() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "tds_start_single");
    Esp32BaseWeb::nativeTestSetParam("referencePpm", "10");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?saved=tds_calibration_started",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.app.tdsCalibrationSnapshot().active);
}

void test_tds_calibration_save_persists_config_after_stable_samples() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    TEST_ASSERT_TRUE(fixture.app.startTdsSinglePointCalibrationForWeb(10, testNowSeconds()));
    for (std::uint32_t i = 1; i <= 12; ++i) {
        fixture.waterSensors.tick(i * 1000UL);
    }
    TEST_ASSERT_TRUE(fixture.app.tdsCalibrationSnapshot().readyToSave);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "tds_save");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?saved=tds_saved",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    const SystemConfig persisted = fixture.configStore.loadSystemConfig();
    TEST_ASSERT_TRUE(persisted.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT16(1, persisted.tdsCalibrationRevision);
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

void test_metering_scheme_write_redirects_busy_to_flow_center() {
    WebFixture fixture;
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "create_metering_scheme");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration/flow?error=busy", Esp32BaseWeb::nativeTestResponseHeader("Location"));
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
    RUN_TEST(test_home_page_labels_only_second_enabled_preset_as_p2);
    RUN_TEST(test_home_page_initial_render_does_not_read_record_pages);
    RUN_TEST(test_stats_page_shows_zero_preset_distribution_when_no_recent_records);
    RUN_TEST(test_stats_page_uses_runtime_period_totals_when_record_file_is_empty);
    RUN_TEST(test_after_format_fs_notification_resets_runtime_statistics);
    RUN_TEST(test_after_format_fs_notification_notifies_app_storage_rebuild);
    RUN_TEST(test_stats_page_initial_render_shows_complete_report);
    RUN_TEST(test_calibration_home_shows_three_expanded_sections_without_flow_tables);
    RUN_TEST(test_calibration_page_initial_render_shows_tds_controls);
    RUN_TEST(test_tds_calibration_prioritizes_two_point_flow);
    RUN_TEST(test_temperature_calibration_uses_simple_card_and_celsius_input);
    RUN_TEST(test_temperature_calibration_disables_save_when_sensor_is_disabled);
    RUN_TEST(test_temperature_calibration_post_accepts_celsius_decimal_input);
    RUN_TEST(test_flow_calibration_center_initial_render_shows_current_parameter_workflow);
    RUN_TEST(test_flow_calibration_history_uses_parameter_language);
    RUN_TEST(test_flow_calibration_manual_input_prefills_copied_parameters);
    RUN_TEST(test_flow_calibration_manual_save_becomes_active_parameter);
    RUN_TEST(test_advanced_sample_library_does_not_present_primary_apply_flow);
    RUN_TEST(test_advanced_sample_library_rejects_old_save_generated_action);
    RUN_TEST(test_flow_calibration_rejects_old_set_active_action);
    RUN_TEST(test_flow_calibration_notice_uses_history_sample_language);
    RUN_TEST(test_flow_calibration_error_uses_history_sample_language);
    RUN_TEST(test_flow_calibration_center_uses_no_collapsed_sections);
    RUN_TEST(test_running_water_allows_read_only_business_pages);
    RUN_TEST(test_filter_reset_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_filter_reset_handler_rejects_cross_origin_post);
    RUN_TEST(test_filter_reset_handler_returns_invalid_index_without_runtime_write);
    RUN_TEST(test_filter_reset_handler_redirects_busy_before_runtime_write);
    RUN_TEST(test_flow_calibration_session_start_redirects_busy_to_flow_center);
    RUN_TEST(test_flow_calibration_session_start_redirects_success_from_idle);
    RUN_TEST(test_calibration_session_start_recovers_missing_session_file_after_format);
    RUN_TEST(test_calibration_session_start_rejects_duplicate_start_as_invalid_state);
    RUN_TEST(test_flow_calibration_remove_sample_redirects_success);
    RUN_TEST(test_flow_calibration_remove_sample_redirects_invalid_value_for_bad_attempt_index);
    RUN_TEST(test_flow_calibration_remove_sample_redirects_invalid_value_for_missing_attempt_index);
    RUN_TEST(test_flow_calibration_remove_sample_redirects_invalid_state_when_sample_not_removable);
    RUN_TEST(test_flow_calibration_remove_sample_redirects_busy_without_changing_sample);
    RUN_TEST(test_flow_calibration_sample_table_only_shows_remove_for_active_samples);
    RUN_TEST(test_tds_calibration_start_redirects_busy_to_calibration_page);
    RUN_TEST(test_tds_calibration_start_redirects_success_from_idle);
    RUN_TEST(test_tds_calibration_save_persists_config_after_stable_samples);
    RUN_TEST(test_calibration_post_rejects_missing_action_as_invalid_action);
    RUN_TEST(test_metering_scheme_write_redirects_busy_to_flow_center);
    RUN_TEST(test_presets_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_presets_handler_rejects_cross_origin_post);
    RUN_TEST(test_presets_handler_rejects_invalid_action);
    RUN_TEST(test_presets_handler_select_next_and_previous_return_status_json);
    RUN_TEST(test_presets_handler_selects_requested_enabled_preset);
    RUN_TEST(test_presets_handler_running_select_next_only_changes_next_preset);
    return UNITY_END();
}
