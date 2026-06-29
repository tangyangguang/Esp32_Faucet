#include <unity.h>

#define ESP32BASE_WEB_NATIVE_TEST 1

#include "app/AppController.h"
#include "app/ConfigStore.h"
#include "app/WaterRecordCalibrationStore.h"
#include "web/FaucetWeb.h"

#include <algorithm>
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
        WaterPulseTraceSample samples[2048]{};
        WaterPulseTraceBucketSample buckets[kPulseTraceMaxBucketsPerTrace]{};
        const std::size_t sampleCount =
            fillWebCalibrationSamples(samples, 2048, startupPulses, attempt.record.pulseCount - startupPulses, stableSeconds);
        TEST_ASSERT_GREATER_THAN_size_t(0, sampleCount);
        std::size_t bucketCount = 0;
        std::size_t startupEdgeCount = 0;
        for (std::size_t i = 0; i < sampleCount; ++i) {
            const std::size_t bucketIndex = samples[i].elapsedUs / (kPulseTraceBucketMs * 1000UL);
            TEST_ASSERT_LESS_THAN_size_t(kPulseTraceMaxBucketsPerTrace, bucketIndex);
            ++buckets[bucketIndex].pulseCount;
            bucketCount = std::max(bucketCount, bucketIndex + 1);
            if (samples[i].elapsedUs < kPulseTraceStartupDetailMs * 1000UL) {
                ++startupEdgeCount;
            }
        }

        CalibrationStoredTrace stored{};
        stored.sessionId = session.sessionId;
        stored.attemptIndex = slot;
        stored.trace.traceId = slot + 1;
        stored.trace.startTime = attempt.record.startTime;
        stored.trace.record = attempt.record;
        stored.trace.bucketCount = bucketCount;
        stored.trace.startupEdgeCount = startupEdgeCount;
        stored.trace.totalPulses = attempt.record.pulseCount;
        stored.trace.actualMl = actualMl;
        stored.trace.pulseMinIntervalUs = kDefaultPulseMinIntervalUs;
        stored.trace.finalState = WaterPulseTraceState::Completed;
        stored.trace.finished = true;
        TEST_ASSERT_TRUE(traceStore.saveValid(
            slot, stored, buckets, bucketCount, samples, startupEdgeCount, actualMl, attempt.record.startTime + 10));
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
    WaterRecord recordsStorage[4]{};
    WaterRecordStore records{recordsStorage, 4};
    WaterRecordCalibration calibrationsStorage[4]{};
    WaterRecordCalibrationStore calibrations{calibrationsStorage, 4};
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
                      &calibrations,
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
        context.recordCalibrations = &calibrations;
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
    TEST_ASSERT_EQUAL_UINT32(0, reader.readPageCalls);
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

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
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

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    CalibrationSessionRecord loaded{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(loaded));
    TEST_ASSERT_EQUAL_UINT32(before.sessionId, loaded.sessionId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(before.status),
                            static_cast<unsigned>(loaded.status));
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
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?view=temperature&saved=temperature",
                             Esp32BaseWeb::nativeTestResponseHeader("Location"));
    TEST_ASSERT_TRUE(fixture.app.config().temperatureCalibrated);
    TEST_ASSERT_TRUE(fixture.config.temperatureCalibrated);
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
        Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, page.path);
        Esp32BaseWeb::nativeTestSetAuthenticated(true);
        Esp32BaseWeb::nativeTestSetSameOrigin(true);

        TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch(page.path, Esp32BaseWeb::METHOD_GET));

        TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
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

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/detail");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("slot", "0");
    Esp32BaseWeb::nativeTestSetParam("bucket", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/detail", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_GREATER_THAN_size_t(0, body.size());
}

void test_calibration_detail_reads_persisted_bucket_trace_without_ram_cache() {
    WebFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, testNowSeconds());
    session.status = CalibrationSessionStatus::WaitingLocalRun;
    saveWebCompactSessionAttempt(fixture.traceStore, session, 0, 1500);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    registerRoutes();

    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/detail");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("slot", "0");
    Esp32BaseWeb::nativeTestSetParam("bucket", "1");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/detail", Esp32BaseWeb::METHOD_GET));

    TEST_ASSERT_EQUAL(200, Esp32BaseWeb::nativeTestResponse().code);
    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_GREATER_THAN_size_t(0, body.size());
}

void test_tds_calibration_start_redirects_busy_to_calibration_page() {
    WebFixture fixture;
    enableTdsForFixture(fixture);
    setRunning(fixture.app);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
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
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
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
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "tds_apply_session");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));

    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_EQUAL_STRING("/faucet/calibration?view=tds&saved=tds_saved",
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
    RUN_TEST(test_home_page_initial_render_does_not_read_record_pages);
    RUN_TEST(test_after_format_fs_notification_resets_runtime_statistics);
    RUN_TEST(test_after_format_fs_notification_notifies_app_storage_rebuild);
    RUN_TEST(test_calibration_home_redirects_active_flow_session_to_workflow);
    RUN_TEST(test_flow_calibration_page_preserves_active_session_state);
    RUN_TEST(test_temperature_calibration_post_accepts_celsius_decimal_input);
    RUN_TEST(test_flow_calibration_manual_save_becomes_active_parameter);
    RUN_TEST(test_running_water_allows_read_only_business_pages);
    RUN_TEST(test_filter_reset_handler_rejects_missing_auth_before_context_work);
    RUN_TEST(test_filter_reset_handler_rejects_cross_origin_post);
    RUN_TEST(test_filter_reset_handler_returns_invalid_index_without_runtime_write);
    RUN_TEST(test_filter_reset_handler_redirects_busy_before_runtime_write);
    RUN_TEST(test_flow_calibration_session_start_redirects_busy_to_flow_center);
    RUN_TEST(test_flow_calibration_session_start_redirects_success_from_idle);
    RUN_TEST(test_calibration_session_start_recovers_missing_session_file_after_format);
    RUN_TEST(test_calibration_detail_reads_persisted_session_trace_without_ram_cache);
    RUN_TEST(test_calibration_detail_reads_persisted_bucket_trace_without_ram_cache);
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
