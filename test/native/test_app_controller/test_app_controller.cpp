#include <unity.h>

#include "app/AppController.h"
#include "app/CalibrationSampleStore.h"
#include "app/CalibrationSessionStore.h"
#include "app/MeteringSchemeStore.h"
#include "app/WaterSensorManager.h"
#include "app/WaterRecordCalibrationStore.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace faucet;

namespace {

class MemoryRecordWriter : public WaterRecordWriter {
public:
    bool ok = true;
    std::vector<WaterRecord> records;

    bool append(const WaterRecord& record) override {
        if (!ok) {
            return false;
        }
        records.push_back(record);
        return true;
    }
};

bool gValveSinkSawClosedBeforeRecordAppend = false;
bool gRecordAppendObservedClosedValve = false;

class ObservingRecordWriter : public WaterRecordWriter {
public:
    std::vector<WaterRecord> records;

    bool append(const WaterRecord& record) override {
        gRecordAppendObservedClosedValve = gValveSinkSawClosedBeforeRecordAppend;
        records.push_back(record);
        return true;
    }
};

class MemoryCalibrationWriter : public WaterRecordCalibrationWriter {
public:
    bool ok = true;
    std::vector<WaterRecordCalibration> calibrations;

    bool upsert(const WaterRecordCalibration& calibration) override {
        if (!ok) {
            return false;
        }
        calibrations.push_back(calibration);
        return true;
    }
};

class MemoryFileBackend : public WaterRecordFileBackend {
public:
    int failWriteAtCount = 0;
    const char* failWriteAtPath = nullptr;
    int failWriteAtPathCount = 0;

    bool exists(const char* path) override {
        return files.find(path ? path : "") != files.end();
    }

    std::int64_t fileSize(const char* path) override {
        const auto it = files.find(path ? path : "");
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }

    bool createSized(const char* path, std::size_t size) override {
        files[path ? path : ""] = std::vector<std::uint8_t>(size, 0);
        return path != nullptr;
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
        if (failWriteAtPath && std::strcmp(path, failWriteAtPath) == 0 && failWriteAtPathCount > 0) {
            --failWriteAtPathCount;
            return false;
        }
        if (failWriteAtCount > 0) {
            --failWriteAtCount;
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

SystemConfig enabledWaterSensorConfig() {
    SystemConfig config = makeDefaultConfig();
    config.temperatureEnabled = true;
    config.temperatureKind = TemperatureKind::Ntc50kB3950;
    config.tdsEnabled = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.tdsCalibrated = true;
    config.tdsCalibrationMode = TdsCalibrationMode::TwoPoint;
    config.tdsCalibrationRevision = 3;
    return config;
}

bool prepareMeteringScheme(MeteringSchemeStore& store,
                           std::uint32_t stablePulsePerLiter,
                           MeteringSchemeRecord& active) {
    if (!store.begin()) {
        return false;
    }
    std::uint32_t id = 0;
    if (!store.createManual("运行方案", MeteringParameters{0, 0, stablePulsePerLiter}, 1714502300, id)) {
        return false;
    }
    if (!store.setActiveScheme(id, 1714502301)) {
        return false;
    }
    return store.activeScheme(active);
}

AppTickInput input(ButtonLevels levels, std::uint32_t nowMs, std::uint32_t nowUs, std::uint32_t nowSeconds) {
    return AppTickInput{
        levels,
        nowMs,
        nowUs,
        nowSeconds,
        {20260506, 202619, 202605},
        true,
        nowSeconds >= kMinRealDateSeconds,
        7,
    };
}

AppTickInput offlineInput(ButtonLevels levels, std::uint32_t nowMs, std::uint32_t nowUs, std::uint32_t uptimeSeconds) {
    return AppTickInput{
        levels,
        nowMs,
        nowUs,
        uptimeSeconds,
        {0, 0, 0},
        false,
        false,
        42,
    };
}

void pressAndReleaseOk(AppController& app, std::uint32_t baseMs) {
    app.tick(input({false, true, false, false}, baseMs, baseMs * 1000UL, 1000 + baseMs / 1000));
    app.tick(input({false, true, false, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL, 1000));
    app.tick(input({false, false, false, false}, baseMs + 60 + kButtonDebounceMs, (baseMs + 60 + kButtonDebounceMs) * 1000UL, 1000));
}

void pressAndReleaseOkAt(AppController& app, std::uint32_t baseMs, std::uint32_t nowSeconds) {
    app.tick(input({false, true, false, false}, baseMs, baseMs * 1000UL, nowSeconds));
    app.tick(input({false, true, false, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, nowSeconds));
    app.tick(input({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL, nowSeconds));
    app.tick(input({false, false, false, false}, baseMs + 60 + kButtonDebounceMs, (baseMs + 60 + kButtonDebounceMs) * 1000UL, nowSeconds));
}

void longPressOk(AppController& app, std::uint32_t baseMs) {
    app.tick(input({false, true, false, false}, baseMs, baseMs * 1000UL, 1000));
    app.tick(input({false, true, false, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({false, true, false, false}, baseMs + kButtonDebounceMs + kButtonLongPressMs,
                   (baseMs + kButtonDebounceMs + kButtonLongPressMs) * 1000UL,
                   1001));
    app.tick(input({false, false, false, false}, baseMs + kButtonDebounceMs + kButtonLongPressMs + 20,
                   (baseMs + kButtonDebounceMs + kButtonLongPressMs + 20) * 1000UL,
                   1001));
    app.tick(input({false, false, false, false}, baseMs + 2 * kButtonDebounceMs + kButtonLongPressMs + 20,
                   (baseMs + 2 * kButtonDebounceMs + kButtonLongPressMs + 20) * 1000UL,
                   1001));
}

void pressAndReleasePlus(AppController& app, std::uint32_t baseMs) {
    app.tick(input({false, false, true, false}, baseMs, baseMs * 1000UL, 1000));
    app.tick(input({false, false, true, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL, 1000));
    app.tick(input({false, false, false, false}, baseMs + 60 + kButtonDebounceMs, (baseMs + 60 + kButtonDebounceMs) * 1000UL, 1000));
}

void pressAndReleaseMinus(AppController& app, std::uint32_t baseMs) {
    app.tick(input({false, false, false, true}, baseMs, baseMs * 1000UL, 1000));
    app.tick(input({false, false, false, true}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL, 1000));
    app.tick(input({false, false, false, false}, baseMs + 60 + kButtonDebounceMs, (baseMs + 60 + kButtonDebounceMs) * 1000UL, 1000));
}

void finishVolumeRun(AppController& app) {
    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));
}

void applyTestMeteringScheme(AppController& app, std::uint32_t stablePulsePerLiter = 1000) {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 99, "测试计量方案", MeteringParameters{0, 0, stablePulsePerLiter}, 1714502300);
    TEST_ASSERT_TRUE(app.applyActiveMeteringScheme(scheme));
}

WaterRecord calibrationRecord(std::uint32_t startTime, std::uint32_t totalPulses, std::uint32_t actualMl) {
    return WaterRecord{
        startTime,
        actualMl,
        actualMl,
        totalPulses,
        0,
        0,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };
}

std::size_t fillCalibrationSamples(WaterPulseTraceSample* samples,
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

void saveCalibrationSessionSample(CalibrationSessionTraceStore& traceStore,
                                  CalibrationSessionRecord& session,
                                  std::uint8_t slot,
                                  std::uint32_t startTime,
                                  std::uint32_t actualMl,
                                  std::uint32_t startupPulses,
                                  std::uint32_t stablePulses,
                                  std::uint32_t stableSeconds) {
    WaterPulseTraceSample samples[2048]{};
    const std::size_t sampleCount =
        fillCalibrationSamples(samples, 2048, startupPulses, stablePulses, stableSeconds);
    TEST_ASSERT_GREATER_THAN_size_t(0, sampleCount);
    const std::uint32_t totalPulses = startupPulses + stablePulses;
    WaterRecord record = calibrationRecord(startTime, totalPulses, actualMl);
    record.durationSec = 5 + stableSeconds;

    CalibrationStoredTrace stored{};
    stored.pendingActual = true;
    stored.sessionId = session.sessionId;
    stored.attemptIndex = slot;
    stored.trace.traceId = slot + 1;
    stored.trace.startTime = startTime;
    stored.trace.record = record;
    stored.trace.sampleCount = sampleCount;
    stored.trace.totalPulses = totalPulses;
    stored.trace.actualMl = actualMl;
    stored.trace.pulseMinIntervalUs = kDefaultPulseMinIntervalUs;
    stored.trace.finalState = WaterPulseTraceState::Completed;
    stored.trace.finished = true;
    TEST_ASSERT_TRUE(traceStore.savePending(slot, stored, samples, sampleCount));
    TEST_ASSERT_TRUE(traceStore.commitValid(slot, actualMl, startTime + 10));

    CalibrationAttempt attempt{};
    attempt.attemptIndex = slot;
    attempt.sessionTraceSlot = slot;
    attempt.record = record;
    attempt.targetHintMl = actualMl;
    attempt.actualMl = actualMl;
    attempt.status = CalibrationAttemptStatus::Valid;
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
}

struct CalibrationAppFixture {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters;
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes;
    MeteringSchemeRecord active{};
    CalibrationSessionFileStore sessionStore;
    CalibrationSessionTraceStore traceStore;
    CalibrationLongTermSampleStore sampleStore;
    WaterPulseTrace ramTraces[4]{};
    WaterPulseTraceSample ramSamples[4096]{};
    WaterPulseTraceStore pulseTraces;
    AppController* app = nullptr;

    CalibrationAppFixture()
        : filters(config.filters),
          schemes(backend, "/schemes.bin"),
          sessionStore(backend, "/cal-session.bin"),
          traceStore(backend, "/cal-traces.bin"),
          sampleStore(backend, "/cal-samples.bin"),
          pulseTraces(ramTraces, 4, ramSamples, 4096, 4) {
        statistics.reset({20260506, 202619, 202605});
        TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
        TEST_ASSERT_TRUE(sessionStore.begin());
        TEST_ASSERT_TRUE(traceStore.begin());
        TEST_ASSERT_TRUE(sampleStore.begin());
    }

    ~CalibrationAppFixture() {
        delete app;
    }

    void createApp() {
        app = new AppController(config,
                                active,
                                statistics,
                                filters,
                                records,
                                schemes,
                                &pulseTraces,
                                &calibrations,
                                &sessionStore,
                                &traceStore,
                                &sampleStore);
    }

    void createAppWithoutMeteringStore() {
        app = new AppController(config,
                                statistics,
                                filters,
                                records,
                                &pulseTraces,
                                &calibrations,
                                &sessionStore,
                                &traceStore,
                                &sampleStore);
    }
};

void savePendingRamCalibrationAttempt(CalibrationAppFixture& fixture,
                                      CalibrationSessionRecord& session,
                                      std::uint8_t slot,
                                      std::uint32_t startTime,
                                      std::uint32_t actualMl,
                                      std::uint32_t startupPulses,
                                      std::uint32_t stablePulses,
                                      std::uint32_t stableSeconds) {
    WaterPulseTraceSample samples[2048]{};
    const std::size_t sampleCount =
        fillCalibrationSamples(samples, 2048, startupPulses, stablePulses, stableSeconds);
    TEST_ASSERT_GREATER_THAN_size_t(0, sampleCount);
    const std::uint32_t traceId = fixture.pulseTraces.beginTrace(startTime, kDefaultPulseMinIntervalUs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, traceId);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        TEST_ASSERT_TRUE(fixture.pulseTraces.appendRawEdge(traceId, samples[i].elapsedUs));
    }

    const std::uint32_t totalPulses = startupPulses + stablePulses;
    WaterRecord record = calibrationRecord(startTime, totalPulses, actualMl);
    record.durationSec = 5 + stableSeconds;
    TEST_ASSERT_TRUE(fixture.pulseTraces.finishTrace(
        traceId, record, WaterPulseTraceState::Completed, record.durationSec * 1000000UL));

    CalibrationAttempt attempt{};
    attempt.attemptIndex = slot;
    attempt.sessionTraceSlot = slot;
    attempt.record = record;
    attempt.targetHintMl = actualMl;
    attempt.status = CalibrationAttemptStatus::PendingActual;
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
}

void saveOneValidOnePendingSession(CalibrationAppFixture& fixture, std::uint32_t nowSeconds) {
    CalibrationSessionRecord session = makeCalibrationSession(77, nowSeconds);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, nowSeconds + 1, 1500, 40, 210, 6);
    savePendingRamCalibrationAttempt(fixture, session, 1, nowSeconds + 10, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::AwaitingActual;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
}

void finishLocalCalibrationRun(CalibrationAppFixture& fixture,
                               std::uint32_t baseMs,
                               std::uint32_t nowSeconds,
                               std::uint32_t pulseCount,
                               std::uint32_t pulseIntervalUs = 5000) {
    pressAndReleaseOkAt(*fixture.app, baseMs, nowSeconds);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Running),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));

    const std::uint32_t firstPulseUs = (baseMs + 200) * 1000UL;
    for (std::uint32_t i = 0; i < pulseCount; ++i) {
        fixture.app->onFlowPulse(firstPulseUs + i * pulseIntervalUs);
    }
    const std::uint32_t finishMs = baseMs + 2200 + (pulseCount * pulseIntervalUs) / 1000UL;
    fixture.app->tick(input({false, false, false, false},
                            finishMs,
                            finishMs * 1000UL,
                            nowSeconds + finishMs / 1000UL));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::AwaitingActual),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Idle),
                            static_cast<unsigned>(fixture.app->snapshot().water.state));
}

}  // namespace

void test_app_controller_uses_active_scheme_parameters_for_flow_meter() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 1000, active));

    AppController app(config, active, statistics, filters, records, schemes);

    TEST_ASSERT_EQUAL_UINT32(1000, app.snapshot().pulsePerLiter);
}

void test_app_snapshot_contains_water_sensor_snapshot() {
    SystemConfig config = enabledWaterSensorConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    sensors.begin();
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);

    app.tick(input({false, false, false, false}, 1000, 1000000, 1714502400));

    const AppSnapshot snapshot = app.snapshot();
    TEST_ASSERT_TRUE(snapshot.sensors.inputVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(12001, snapshot.sensors.inputVoltageMv.value);
    TEST_ASSERT_TRUE(snapshot.sensors.temperatureCentiC.valid);
    TEST_ASSERT_TRUE(snapshot.sensors.tdsPpm.valid);
    TEST_ASSERT_TRUE(snapshot.temperatureSensorEnabled);
    TEST_ASSERT_TRUE(snapshot.tdsSensorEnabled);
}

void test_temperature_reference_calibration_sets_offset_from_raw_temperature() {
    SystemConfig config = enabledWaterSensorConfig();
    config.temperatureOffsetCentiC = 120;
    config.temperatureCalibrated = true;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    sensors.begin();
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);

    app.tick(input({false, false, false, false}, 1000, 1000000, 1714502400));

    const AppSnapshot before = app.snapshot();
    TEST_ASSERT_TRUE(before.sensors.temperatureRawCentiC.valid);
    TEST_ASSERT_TRUE(app.saveTemperatureCalibrationForWeb(
        static_cast<std::int16_t>(before.sensors.temperatureRawCentiC.value + 60)));
    TEST_ASSERT_TRUE(app.config().temperatureCalibrated);
    TEST_ASSERT_EQUAL_INT16(60, app.config().temperatureOffsetCentiC);
}

void test_temperature_reference_calibration_rejects_disabled_temperature_sensor() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    adc.values[1] = okMv(1650);
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    sensors.begin();
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);

    app.tick(input({false, false, false, false}, 1000, 1000000, 1714502400));

    TEST_ASSERT_FALSE(app.saveTemperatureCalibrationForWeb(2500));
    TEST_ASSERT_FALSE(app.config().temperatureCalibrated);
}

void test_app_records_sensor_summary_on_completed_run() {
    SystemConfig config = enabledWaterSensorConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    sensors.begin();
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    app.tick(input({false, false, false, false}, 1500, 1500000, 1714502397));
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_GREATER_THAN_UINT16(0, records.records[0].sensorSampleCount);
    TEST_ASSERT_INT_WITHIN(50, 2500, records.records[0].temperatureAvgCentiC);
    TEST_ASSERT_EQUAL_UINT16(10, records.records[0].tdsAvgPpm);
    TEST_ASSERT_EQUAL_UINT16(3, records.records[0].tdsCalibrationRevisionAtRun);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            records.records[0].tdsCalibrationModeAtRun);
}

void test_app_rejects_tds_calibration_when_running() {
    SystemConfig config = enabledWaterSensorConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);
    applyTestMeteringScheme(app);
    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);

    TEST_ASSERT_FALSE(app.startTdsCalibrationSessionForWeb(1714502401));
    TEST_ASSERT_FALSE(app.startTdsCalibrationPointForWeb(160, 1714502401));
}

void test_app_tds_point_calibration_apply_persists_to_config() {
    SystemConfig config = enabledWaterSensorConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    TEST_ASSERT_TRUE(sensors.begin());
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);

    TEST_ASSERT_TRUE(app.startTdsCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_TRUE(app.startTdsCalibrationPointForWeb(160, 1714502401));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        nowMs += 1000;
        app.tick(input({false, false, false, false}, nowMs, nowMs * 1000UL, 1714502401));
    }
    TEST_ASSERT_TRUE(app.saveTdsCalibrationPointForWeb(1714502420));
    TEST_ASSERT_TRUE(app.applyTdsCalibrationForWeb(1714502430));

    const SystemConfig updated = app.config();
    TEST_ASSERT_TRUE(updated.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::SinglePoint),
                            static_cast<std::uint8_t>(updated.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(4, updated.tdsCalibrationRevision);
}

void test_app_controller_successful_record_writes_scheme_id_and_marks_scheme_used_once() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 1000, active));
    AppController app(config, active, statistics, filters, records, schemes);

    finishVolumeRun(app);

    TEST_ASSERT_TRUE(app.lastRecordWriteOk());
    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(active.id, records.records[0].meteringSchemeId);
    MeteringSchemeRecord updated{};
    TEST_ASSERT_TRUE(schemes.findById(active.id, updated));
    TEST_ASSERT_FALSE(updated.deleted);
    TEST_ASSERT_TRUE(updated.usedEver);
}

void test_app_controller_record_write_failure_does_not_mark_scheme_used() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    records.ok = false;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 1000, active));
    AppController app(config, active, statistics, filters, records, schemes);

    finishVolumeRun(app);

    TEST_ASSERT_FALSE(app.lastRecordWriteOk());
    MeteringSchemeRecord updated{};
    TEST_ASSERT_TRUE(schemes.findById(active.id, updated));
    TEST_ASSERT_FALSE(updated.deleted);
    TEST_ASSERT_FALSE(updated.usedEver);
}

void test_app_controller_record_write_success_locks_active_scheme_even_if_used_mark_persist_fails() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 1000, active));
    AppController app(config, active, statistics, filters, records, schemes);

    backend.failWriteAtCount = 1;
    finishVolumeRun(app);

    TEST_ASSERT_TRUE(app.lastRecordWriteOk());
    TEST_ASSERT_TRUE(app.activeMeteringScheme().usedEver);
    MeteringSchemeRecord persisted{};
    TEST_ASSERT_TRUE(schemes.findById(active.id, persisted));
    TEST_ASSERT_FALSE(persisted.usedEver);
}

void test_app_controller_starts_after_double_ok_and_opens_valve() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Confirm),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    pressAndReleaseOk(app, 300);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_UINT8(100, app.snapshot().valve.dutyPercent);
}

void test_app_controller_cancel_raw_dominates_pending_ok_release() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Confirm),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    app.tick(input({false, true, false, false}, 300, 300000, 1714502400));
    app.tick(input({false, true, false, false}, 300 + kButtonDebounceMs, 330000, 1714502400));
    app.tick(input({false, false, false, false}, 360, 360000, 1714502400));
    app.tick(input({true, false, false, false}, 380, 380000, 1714502400));
    app.tick(input({true, false, false, false}, 390, 390000, 1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
}

void test_app_controller_confirm_and_running_start_volume_stays_zero_until_first_pulse() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 99, "启动段测试", MeteringParameters{8, 130, 248}, 1714502300);
    TEST_ASSERT_TRUE(app.applyActiveMeteringScheme(scheme));

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Confirm),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().water.volumeMl);

    pressAndReleaseOk(app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().water.volumeMl);

    app.tick(input({false, false, false, false}, 900, 900000, 1714502300));
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().water.volumeMl);

    app.onFlowPulse(1000000);
    app.tick(input({false, false, false, false}, 1200, 1200000, 1714502301));
    TEST_ASSERT_EQUAL_UINT32(16, app.snapshot().water.volumeMl);
}

void test_app_controller_completion_writes_record_statistics_and_filters() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);

    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].volumeMl);
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].targetValue);
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, records.records[0].rejectedPulseCount);
    TEST_ASSERT_EQUAL_UINT32(99, records.records[0].meteringSchemeId);
    TEST_ASSERT_EQUAL_UINT8(0, records.records[0].selectedPreset);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::Completed),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
    TEST_ASSERT_FALSE(app.consumePersistenceDirty());
    app.markPersistenceDirtyForRetry();
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
}

void test_app_controller_pushes_closed_valve_output_before_record_persistence() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    ObservingRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);
    app.setValveOutputSink([](ValveOutput output) {
        if (!output.enabled) {
            gValveSinkSawClosedBeforeRecordAppend = true;
        }
    });

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    gValveSinkSawClosedBeforeRecordAppend = false;
    gRecordAppendObservedClosedValve = false;

    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_TRUE(gRecordAppendObservedClosedValve);
}

void test_app_controller_web_preset_switch_during_run_updates_next_preset_only() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.activePreset);

    TEST_ASSERT_TRUE(app.selectNextPresetForWeb());

    TEST_ASSERT_EQUAL_size_t(1, app.snapshot().water.selectedPreset);
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.activePreset);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(0, records.records[0].selectedPreset);
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].targetValue);
    TEST_ASSERT_EQUAL_size_t(1, app.snapshot().water.selectedPreset);
}

void test_app_controller_local_plus_does_not_switch_preset_while_running() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);

    pressAndReleasePlus(app, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.selectedPreset);
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.activePreset);
}

void test_app_controller_starting_calibration_from_idle_enters_preparing() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());
    AppController app(config,
                      statistics,
                      filters,
                      records,
                      nullptr,
                      nullptr,
                      &sessionStore,
                      &traceStore,
                      &sampleStore);
    applyTestMeteringScheme(app);

    TEST_ASSERT_TRUE(app.startCalibrationSessionForWeb(1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Preparing),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(0, app.snapshot().calibrationAttemptCount);
    TEST_ASSERT_EQUAL_UINT8(0, app.snapshot().calibrationValidSampleCount);
    TEST_ASSERT_FALSE(app.snapshot().water.valveOpen);
}

void test_app_controller_starting_calibration_while_running_is_rejected() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());
    AppController app(config, statistics, filters, records, nullptr, nullptr, &sessionStore, &traceStore, &sampleStore);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);

    TEST_ASSERT_FALSE(app.startCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Idle),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
}

void test_app_controller_starting_calibration_twice_is_rejected() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());
    AppController app(config, statistics, filters, records, nullptr, nullptr, &sessionStore, &traceStore, &sampleStore);
    applyTestMeteringScheme(app);

    TEST_ASSERT_TRUE(app.startCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_FALSE(app.startCalibrationSessionForWeb(1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Preparing),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
}

void test_app_controller_calibration_preparing_times_out_to_discarded() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());
    AppController app(config, statistics, filters, records, nullptr, nullptr, &sessionStore, &traceStore, &sampleStore);
    applyTestMeteringScheme(app);

    TEST_ASSERT_TRUE(app.startCalibrationSessionForWeb(1714502400));

    app.tick(input({false, false, false, false}, 1801000, 1801000000UL, 1714504201));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Discarded),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
}

void test_app_controller_calibration_ready_and_generated_time_out_from_last_action() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());

    CalibrationSessionRecord session = makeCalibrationSession(79, 1714502400);
    saveCalibrationSessionSample(traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    session.updatedAt = 1714502500;
    TEST_ASSERT_TRUE(sessionStore.save(session));

    AppController app(config,
                      active,
                      statistics,
                      filters,
                      records,
                      schemes,
                      nullptr,
                      &calibrations,
                      &sessionStore,
                      &traceStore,
                      &sampleStore);
    TEST_ASSERT_EQUAL_UINT32(1714502500 + kCalibrationIdleTimeoutSec,
                             app.snapshot().calibrationIdleExpiresAt);

    app.tick(input({false, false, false, false}, 1000, 1000000UL, 1714502500 + kCalibrationIdleTimeoutSec - 1));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::ReadyToGenerate),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
    app.tick(input({false, false, false, false}, 2000, 2000000UL, 1714502500 + kCalibrationIdleTimeoutSec));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Discarded),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));

    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.updatedAt = 1714505000;
    TEST_ASSERT_TRUE(sessionStore.save(session));
    AppController generated(config,
                            active,
                            statistics,
                            filters,
                            records,
                            schemes,
                            nullptr,
                            &calibrations,
                            &sessionStore,
                            &traceStore,
                            &sampleStore);
    TEST_ASSERT_TRUE(generated.generateCalibrationForWeb(1714505100));
    TEST_ASSERT_EQUAL_UINT32(1714505100 + kCalibrationIdleTimeoutSec,
                             generated.snapshot().calibrationIdleExpiresAt);
    generated.tick(input({false, false, false, false}, 3000, 3000000UL, 1714505100 + kCalibrationIdleTimeoutSec));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Discarded),
                            static_cast<unsigned>(generated.snapshot().calibrationStatus));
}

void test_app_controller_reboot_drops_awaiting_actual_when_ram_trace_missing() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[4096]{};
    WaterPulseTraceStore pulseTraces(traces, 1, samples, 4096, 1);
    MemoryFileBackend backend;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());

    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    session.status = CalibrationSessionStatus::AwaitingActual;
    session.attemptCount = 1;
    session.attempts[0].attemptIndex = 0;
    session.attempts[0].sessionTraceSlot = 0;
    session.attempts[0].record = calibrationRecord(1714502410, 500, 500);
    session.attempts[0].targetHintMl = 500;
    session.attempts[0].status = CalibrationAttemptStatus::PendingActual;
    TEST_ASSERT_TRUE(sessionStore.save(session));

    AppController rebooted(config,
                           statistics,
                           filters,
                           records,
                           &pulseTraces,
                           &calibrations,
                           &sessionStore,
                           &traceStore,
                           &sampleStore);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(rebooted.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(rebooted.snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(1, rebooted.snapshot().calibrationAttemptCount);
    TEST_ASSERT_EQUAL_UINT8(0, rebooted.snapshot().calibrationValidSampleCount);
}

void test_app_controller_local_ok_starts_calibration_run_and_completion_awaits_actual() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    WaterPulseTrace traces[1]{};
    WaterPulseTraceSample samples[4096]{};
    WaterPulseTraceStore pulseTraces(traces, 1, samples, 4096, 1);
    MemoryFileBackend backend;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());
    AppController app(config,
                      statistics,
                      filters,
                      records,
                      &pulseTraces,
                      &calibrations,
                      &sessionStore,
                      &traceStore,
                      &sampleStore);
    applyTestMeteringScheme(app);

    TEST_ASSERT_TRUE(app.startCalibrationSessionForWeb(1714502400));
    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Running),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(app.snapshot().water.valveOpen);

    for (std::uint32_t i = 0; i < 500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({true, false, false, false}, 1600, 1600000, 1714502401));
    app.tick(input({true, false, false, false}, 1600 + kButtonDebounceMs, (1600 + kButtonDebounceMs) * 1000UL, 1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::AwaitingActual),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(500, filters.record(0).usedMl);
    CalibrationStoredTrace pending{};
    TEST_ASSERT_FALSE(traceStore.load(0, pending));

    TEST_ASSERT_TRUE(app.submitCalibrationActualForWeb(520, 1714502402));
    TEST_ASSERT_EQUAL_UINT8(1, app.snapshot().calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_size_t(1, calibrations.calibrations.size());
    TEST_ASSERT_EQUAL_UINT32(520, calibrations.calibrations[0].actualMl);
    TEST_ASSERT_EQUAL_UINT32(records.records[0].pulseCount, calibrations.calibrations[0].pulseCount);
    CalibrationStoredTrace valid{};
    TEST_ASSERT_TRUE(traceStore.load(0, valid));
    TEST_ASSERT_TRUE(valid.valid);
    TEST_ASSERT_FALSE(valid.pendingActual);
    CalibrationStoredTrace longTermSamples[kCalibrationLongTermSampleSlots]{};
    TEST_ASSERT_EQUAL_size_t(0, sampleStore.list(longTermSamples, kCalibrationLongTermSampleSlots));

    std::uint32_t sampleId = 0;
    TEST_ASSERT_TRUE(app.saveCalibrationSessionSampleToLongTermForWeb(0, 1714502403, sampleId));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, sampleId);
    TEST_ASSERT_EQUAL_size_t(1, sampleStore.list(longTermSamples, kCalibrationLongTermSampleSlots));
    TEST_ASSERT_EQUAL_UINT32(sampleId, longTermSamples[0].sampleId);
    TEST_ASSERT_EQUAL_UINT32(520, longTermSamples[0].actualMl);

    WaterPulseTraceSample copied[4096]{};
    TEST_ASSERT_EQUAL_size_t(valid.trace.sampleCount, sampleStore.readSamples(sampleId, copied, 4096));
    TEST_ASSERT_TRUE(app.saveCalibrationSessionSampleToLongTermForWeb(0, 1714502404, sampleId));
    TEST_ASSERT_EQUAL_size_t(1, sampleStore.list(longTermSamples, kCalibrationLongTermSampleSlots));
}

void test_app_controller_generates_calibration_session_candidate() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());

    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    saveCalibrationSessionSample(traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(sessionStore.save(session));

    AppController app(config,
                      active,
                      statistics,
                      filters,
                      records,
                      schemes,
                      nullptr,
                      &calibrations,
                      &sessionStore,
                      &traceStore,
                      &sampleStore);
    MeteringSchemeRecord beforeGenerate[4]{};
    const std::size_t beforeGenerateCount = schemes.list(beforeGenerate, 4, true);

    TEST_ASSERT_TRUE(app.generateCalibrationForWeb(1714502500));

    MeteringSchemeRecord list[4]{};
    TEST_ASSERT_EQUAL_size_t(beforeGenerateCount, schemes.list(list, 4, true));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
}

void test_app_controller_auto_generates_after_second_valid_calibration_sample() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));

    const AppSnapshot snapshot = fixture.app->snapshot();
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(snapshot.calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->applyGeneratedCalibrationForWeb(1714502600));
}

void test_app_controller_submit_actual_succeeds_when_auto_refresh_cannot_generate() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createAppWithoutMeteringStore();

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));

    const AppSnapshot snapshot = fixture.app->snapshot();
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(snapshot.calibrationStatus));
    TEST_ASSERT_EQUAL_size_t(1, fixture.calibrations.calibrations.size());

    CalibrationStoredTrace valid{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(1, valid));
    TEST_ASSERT_TRUE(valid.valid);
    TEST_ASSERT_FALSE(valid.pendingActual);
    TEST_ASSERT_EQUAL_UINT32(7500, valid.actualMl);

    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(persisted.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Valid),
                            static_cast<unsigned>(persisted.attempts[1].status));
}

void test_app_controller_removed_valid_sample_clears_generated_candidate() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();
    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));

    TEST_ASSERT_TRUE(fixture.app->removeCalibrationSessionSampleForWeb(1, 1714502550));

    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Removed),
                            static_cast<unsigned>(persisted.attempts[1].status));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.app->snapshot().calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_FALSE(fixture.app->applyGeneratedCalibrationForWeb(1714502600));
}

void test_app_controller_remove_sample_session_save_failure_keeps_original_trace() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();

    CalibrationStoredTrace before{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(0, before));
    TEST_ASSERT_TRUE(before.valid);
    TEST_ASSERT_FALSE(before.pendingActual);

    fixture.backend.failWriteAtPath = "/cal-session.bin";
    fixture.backend.failWriteAtPathCount = 1;

    TEST_ASSERT_FALSE(fixture.app->removeCalibrationSessionSampleForWeb(0, 1714502500));

    CalibrationStoredTrace after{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(0, after));
    TEST_ASSERT_TRUE(after.valid);
    TEST_ASSERT_FALSE(after.pendingActual);
    TEST_ASSERT_EQUAL_UINT32(before.actualMl, after.actualMl);
    TEST_ASSERT_EQUAL_UINT32(before.trace.totalPulses, after.trace.totalPulses);

    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Valid),
                            static_cast<unsigned>(persisted.attempts[0].status));
}

void test_app_controller_pending_actual_sample_can_be_removed() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->removeCalibrationSessionSampleForWeb(1, 1714502500));

    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Removed),
                            static_cast<unsigned>(persisted.attempts[1].status));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.app->snapshot().calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
}

void test_app_controller_reuses_removed_trace_slot_without_overwriting_valid_sample() {
    CalibrationAppFixture fixture;
    fixture.config.presets[0].value = 8500;
    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(fixture.traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.createApp();
    TEST_ASSERT_TRUE(fixture.app->generateCalibrationForWeb(1714502500));

    CalibrationStoredTrace slot1Before{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(1, slot1Before));
    TEST_ASSERT_TRUE(slot1Before.valid);
    TEST_ASSERT_FALSE(slot1Before.pendingActual);
    TEST_ASSERT_TRUE(fixture.app->removeCalibrationSessionSampleForWeb(0, 1714502550));

    finishLocalCalibrationRun(fixture, 1000, 1714502600, 2000, 27000);

    CalibrationStoredTrace slot1After{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(1, slot1After));
    TEST_ASSERT_TRUE(slot1After.valid);
    TEST_ASSERT_FALSE(slot1After.pendingActual);
    TEST_ASSERT_EQUAL_UINT32(slot1Before.actualMl, slot1After.actualMl);
    TEST_ASSERT_EQUAL_UINT32(slot1Before.trace.totalPulses, slot1After.trace.totalPulses);

    CalibrationSessionRecord pending{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(pending));
    TEST_ASSERT_EQUAL_UINT8(3, pending.attemptCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::PendingActual),
                            static_cast<unsigned>(pending.attempts[2].status));
    TEST_ASSERT_NOT_EQUAL_UINT8(pending.attempts[1].sessionTraceSlot, pending.attempts[2].sessionTraceSlot);
    const WaterPulseTrace* pendingTrace = fixture.pulseTraces.findByRecord(pending.attempts[2].record);
    TEST_ASSERT_NOT_NULL(pendingTrace);
    TEST_ASSERT_FALSE(pendingTrace->truncated);
    TEST_ASSERT_FALSE(pendingTrace->resumedAfterPause);
    TEST_ASSERT_GREATER_THAN_size_t(0, pendingTrace->sampleCount);
    TEST_ASSERT_GREATER_THAN_UINT32(0, pendingTrace->totalPulses);

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(9500, 1714502670));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(2, fixture.app->snapshot().calibrationValidSampleCount);
    TEST_ASSERT_TRUE(fixture.app->applyGeneratedCalibrationForWeb(1714502680));
}

void test_app_controller_remove_one_of_three_valid_samples_regenerates_candidate() {
    CalibrationAppFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(78, 1714502400);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(fixture.traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    saveCalibrationSessionSample(fixture.traceStore, session, 2, 1714502420, 9500, 40, 1900, 12);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.createApp();
    TEST_ASSERT_TRUE(fixture.app->generateCalibrationForWeb(1714502500));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));

    TEST_ASSERT_TRUE(fixture.app->removeCalibrationSessionSampleForWeb(1, 1714502550));

    TEST_ASSERT_EQUAL_UINT8(2, fixture.app->snapshot().calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->applyGeneratedCalibrationForWeb(1714502600));
}

void test_app_controller_applies_generated_session_scheme_and_keeps_old_scheme() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
    const std::uint32_t oldActiveId = active.id;
    CalibrationSessionFileStore sessionStore(backend, "/cal-session.bin");
    CalibrationSessionTraceStore traceStore(backend, "/cal-traces.bin");
    CalibrationLongTermSampleStore sampleStore(backend, "/cal-samples.bin");
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
    TEST_ASSERT_TRUE(sampleStore.begin());

    CalibrationSessionRecord session = makeCalibrationSession(78, 1714502400);
    saveCalibrationSessionSample(traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(sessionStore.save(session));

    AppController app(config,
                      active,
                      statistics,
                      filters,
                      records,
                      schemes,
                      nullptr,
                      &calibrations,
                      &sessionStore,
                      &traceStore,
                      &sampleStore);
    TEST_ASSERT_TRUE(app.generateCalibrationForWeb(1714502500));
    TEST_ASSERT_TRUE(app.applyGeneratedCalibrationForWeb(1714502600));

    TEST_ASSERT_NOT_EQUAL(oldActiveId, schemes.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(schemes.activeSchemeId(), app.activeMeteringScheme().id);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::CalibrationSession),
                            static_cast<unsigned>(app.activeMeteringScheme().sourceType));
    TEST_ASSERT_UINT32_WITHIN(5, 222, app.activeMeteringScheme().params.stablePulsePerLiter);
    MeteringSchemeRecord oldScheme{};
    TEST_ASSERT_TRUE(schemes.findById(oldActiveId, oldScheme));
    TEST_ASSERT_TRUE(oldScheme.recordUsed);
    TEST_ASSERT_FALSE(oldScheme.deleted);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Applied),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
}

void test_app_controller_applies_calibration_from_raw_record() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    AppController app(config, statistics, filters, records, nullptr, &calibrations);
    applyTestMeteringScheme(app);
    WaterRecord record{
        1714502400,
        7500,
        7500,
        9000,
        0,
        10,
        WaterMode::Volume,
        WaterResult::StoppedByUser,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(CalibrationApplyResult::Saved),
                            static_cast<std::uint8_t>(app.applyCalibrationFromRecord(record, 7500)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, static_cast<float>(app.activeMeteringScheme().params.stablePulsePerLiter) / 1000.0f);
    TEST_ASSERT_FALSE(app.consumeConfigDirty());
    TEST_ASSERT_EQUAL_size_t(1, calibrations.calibrations.size());
    TEST_ASSERT_EQUAL_UINT32(7500, calibrations.calibrations[0].actualMl);
}

void test_app_controller_small_record_calibration_keeps_metering_parameters() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    AppController app(config, statistics, filters, records, nullptr, &calibrations);
    applyTestMeteringScheme(app);
    WaterRecord record{
        1714502400,
        1000,
        1000,
        900,
        0,
        10,
        WaterMode::Volume,
        WaterResult::Completed,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(CalibrationApplyResult::Saved),
                            static_cast<std::uint8_t>(app.applyCalibrationFromRecord(record, 1000)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, static_cast<float>(app.activeMeteringScheme().params.stablePulsePerLiter) / 1000.0f);
    TEST_ASSERT_FALSE(app.consumeConfigDirty());
    TEST_ASSERT_EQUAL_size_t(1, calibrations.calibrations.size());
    TEST_ASSERT_EQUAL_UINT32(1000, calibrations.calibrations[0].actualMl);
}

void test_app_controller_pause_timeout_trace_is_not_marked_error_and_can_calibrate() {
    SystemConfig config = makeDefaultConfig();
    config.pauseTimeoutSec = 10;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[700]{};
    WaterPulseTraceStore pulseTraces(traces, 2, samples, 700, 2);
    AppController app(config, statistics, filters, records, &pulseTraces);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 600; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 1800, 1800000, 1714502400));
    pressAndReleaseOk(app, 1900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Paused),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    app.tick(input({false, false, false, false}, 12100, 12100000, 1714502412));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::PauseTimeout),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_FALSE(app.snapshot().calibrationReady);
    TEST_ASSERT_EQUAL_size_t(0, pulseTraces.count());
}

void test_app_controller_applies_calibration_from_pause_timeout_record() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    AppController app(config, statistics, filters, records, nullptr, &calibrations);
    applyTestMeteringScheme(app);
    WaterRecord record{
        1714502400,
        1470,
        7500,
        326,
        0,
        51,
        WaterMode::Volume,
        WaterResult::PauseTimeout,
        0,
        0,
        1,
        {0, 0, 0, 0},
    };

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(CalibrationApplyResult::Saved),
                            static_cast<std::uint8_t>(app.applyCalibrationFromRecord(record, 1470)));
    TEST_ASSERT_FALSE(app.consumeConfigDirty());
    TEST_ASSERT_EQUAL_size_t(1, calibrations.calibrations.size());
    TEST_ASSERT_EQUAL_UINT32(1470, calibrations.calibrations[0].actualMl);
}

void test_app_controller_offline_completion_marks_unknown_time_with_boot_id() {
    SystemConfig config = makeDefaultConfig();
    MemoryRecordWriter records;
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    app.tick(offlineInput({false, true, false, false}, 100, 100000, 1));
    app.tick(offlineInput({false, true, false, false}, 100 + kButtonDebounceMs, (100 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 160, 160000, 1));
    app.tick(offlineInput({false, false, false, false}, 160 + kButtonDebounceMs, (160 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, true, false, false}, 300, 300000, 1));
    app.tick(offlineInput({false, true, false, false}, 300 + kButtonDebounceMs, (300 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 360, 360000, 1));
    app.tick(offlineInput({false, false, false, false}, 360 + kButtonDebounceMs, (360 + kButtonDebounceMs) * 1000UL, 1));
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(2000000UL + i * 2000UL);
    }
    app.tick(offlineInput({false, false, false, false}, 5000, 5000000, 5));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].volumeMl);
    TEST_ASSERT_EQUAL_UINT32(1, records.records[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(42, waterRecordBootId(records.records[0]));
    TEST_ASSERT_EQUAL_UINT32(0, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(0, statistics.record().totalMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
}

void test_app_controller_offline_start_sync_before_completion_writes_real_time() {
    SystemConfig config = makeDefaultConfig();
    MemoryRecordWriter records;
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    app.tick(offlineInput({false, true, false, false}, 1000, 1000000, 1));
    app.tick(offlineInput({false, true, false, false}, 1000 + kButtonDebounceMs, (1000 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 1060, 1060000, 1));
    app.tick(offlineInput({false, false, false, false}, 1060 + kButtonDebounceMs, (1060 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, true, false, false}, 1300, 1300000, 1));
    app.tick(offlineInput({false, true, false, false}, 1300 + kButtonDebounceMs, (1300 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 1360, 1360000, 1));
    app.tick(offlineInput({false, false, false, false}, 1360 + kButtonDebounceMs, (1360 + kButtonDebounceMs) * 1000UL, 1));
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(2000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 815500004));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(815500000, records.records[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, waterRecordBootId(records.records[0]));
}

void test_app_controller_pause_resume_then_completion_updates_persistence_once() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[2000]{};
    WaterPulseTraceStore pulseTraces(traces, 2, samples, 2000, 2);
    AppController app(config, statistics, filters, records, &pulseTraces);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 700; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 2500, 2500000, 1714502400));
    TEST_ASSERT_EQUAL_UINT32(700, app.snapshot().water.volumeMl);

    pressAndReleaseOk(app, 2600);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Paused),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    pressAndReleasePlus(app, 2800);
    pressAndReleaseMinus(app, 3000);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);
    app.tick(input({false, false, false, false}, 20000, 20000000, 1714502418));
    TEST_ASSERT_EQUAL_size_t(0, records.records.size());

    pressAndReleaseOk(app, 20100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    for (std::uint32_t i = 700; i < 1500; ++i) {
        app.onFlowPulse(21000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 23000, 23000000, 1714502421));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::Completed),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
    TEST_ASSERT_EQUAL_size_t(0, pulseTraces.count());
}

void test_app_controller_stop_down_closes_valve_and_records_user_stop() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 300; ++i) {
        app.onFlowPulse(1000000UL + i * 3000UL);
    }
    app.tick(input({false, false, false, false}, 1500, 1500000, 1714502400));

    app.tick(input({true, false, false, false}, 1600, 1600000, 1714502401));
    app.tick(input({true, false, false, false}, 1600 + kButtonDebounceMs, (1600 + kButtonDebounceMs) * 1000UL, 1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::StoppedByUser),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(300, records.records[0].volumeMl);
}

void test_app_controller_normal_output_does_not_collect_ram_pulse_trace() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    WaterPulseTrace traces[2]{};
    WaterPulseTraceSample samples[700]{};
    WaterPulseTraceStore pulseTraces(traces, 2, samples, 700, 2);
    AppController app(config, statistics, filters, records, &pulseTraces);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 232; ++i) {
        app.onFlowPulse(1000000UL + i * 3000UL);
    }
    app.tick(input({false, false, false, false}, 1500, 1500000, 1714502400));

    TEST_ASSERT_TRUE(app.emergencyStop(1600));
    app.tick(input({true, false, false, false}, 1600, 1600000, 1714502401));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::StoppedByUser),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_size_t(0, pulseTraces.count());
    TEST_ASSERT_NULL(pulseTraces.findByRecord(records.records[0]));
}

void test_app_controller_emergency_stop_closes_valve_without_debounce() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);

    TEST_ASSERT_TRUE(app.emergencyStop(1000));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

void test_app_controller_applies_config_only_while_idle() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    SystemConfig updated = config;
    updated.presets[0].value = 2000;
    TEST_ASSERT_TRUE(app.canApplyConfig());
    TEST_ASSERT_TRUE(app.applyConfig(updated));
    TEST_ASSERT_EQUAL_UINT32(2000, app.snapshot().water.targetValue);

    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_FALSE(app.canApplyConfig());
    updated.presets[0].value = 3000;
    TEST_ASSERT_FALSE(app.applyConfig(updated));
}

void test_app_controller_emits_beep_patterns_for_actions_and_completion() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::Click),
                            static_cast<std::uint8_t>(app.consumeBeepPattern()));
    pressAndReleaseOk(app, 300);
    app.consumeBeepPattern();
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::Done),
                            static_cast<std::uint8_t>(app.consumeBeepPattern()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::None),
                            static_cast<std::uint8_t>(app.consumeBeepPattern()));
}

void test_app_controller_reports_record_write_failure_without_losing_statistics() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    records.ok = false;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_FALSE(app.lastRecordWriteOk());
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
}

void test_app_controller_adjusts_volume_target_with_configured_step_without_ok_long_toggle() {
    SystemConfig config = makeDefaultConfig();
    config.volumeAdjustStepMl = 250;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);
    TEST_ASSERT_EQUAL_UINT32(250, app.snapshot().adjustmentStepMl);

    pressAndReleasePlus(app, 300);
    TEST_ASSERT_EQUAL_UINT32(1750, app.snapshot().water.targetValue);
    longPressOk(app, 500);
    TEST_ASSERT_EQUAL_UINT32(250, app.snapshot().adjustmentStepMl);
    pressAndReleaseMinus(app, 1700);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);

    pressAndReleaseOk(app, 1900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

void test_app_controller_adjusts_time_target_with_configured_step() {
    SystemConfig config = makeDefaultConfig();
    config.presets[0].type = PresetType::Time;
    config.presets[0].value = 60;
    config.timeAdjustStepSec = 15;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterMode::Time),
                            static_cast<std::uint8_t>(app.snapshot().water.mode));
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().water.targetValue);
    TEST_ASSERT_EQUAL_UINT32(15, app.snapshot().timeAdjustmentStepSec);

    pressAndReleasePlus(app, 300);
    TEST_ASSERT_EQUAL_UINT32(75, app.snapshot().water.targetValue);
    longPressOk(app, 500);
    TEST_ASSERT_EQUAL_UINT32(15, app.snapshot().timeAdjustmentStepSec);
    pressAndReleaseMinus(app, 1700);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().water.targetValue);
}

void test_app_controller_stopped_volume_does_not_clamp_next_confirm_adjustment() {
    SystemConfig config = makeDefaultConfig();
    config.presets[0].value = 1500;
    config.presets[1].value = 1000;
    config.resultDisplaySec = 0;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1390; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));
    pressAndReleaseOk(app, 5100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Paused),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    app.tick(input({true, false, false, false}, 5300, 5300000, 1714502401));
    app.tick(input({true, false, false, false}, 5300 + kButtonDebounceMs, (5300 + kButtonDebounceMs) * 1000UL, 1714502401));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    pressAndReleasePlus(app, 5500);
    TEST_ASSERT_EQUAL_size_t(1, app.snapshot().water.selectedPreset);
    pressAndReleaseOk(app, 5700);
    TEST_ASSERT_EQUAL_UINT32(1000, app.snapshot().water.targetValue);
    pressAndReleasePlus(app, 5900);
    TEST_ASSERT_EQUAL_UINT32(1100, app.snapshot().water.targetValue);
}

void test_app_controller_result_display_exits_after_configured_timeout() {
    SystemConfig config = makeDefaultConfig();
    config.resultDisplaySec = 2;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    app.tick(input({false, false, false, false}, 7000, 7000000, 1714502402));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
}

void test_app_controller_result_ok_hold_enters_local_record_calibration_after_5s() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    AppController app(config, statistics, filters, records, nullptr, &calibrations);
    applyTestMeteringScheme(app);

    finishVolumeRun(app);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_TRUE(app.snapshot().calibrationReady);

    app.tick(input({false, true, false, false}, 6000, 6000000, 1714502401));
    app.tick(input({false, true, false, false}, 6000 + kButtonDebounceMs + kButtonLongPressMs,
                   (6000 + kButtonDebounceMs + kButtonLongPressMs) * 1000UL,
                   1714502402));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    app.tick(input({false, true, false, false}, 11000, 11000000, 1714502406));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::RecordCalibration),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().calibrationActualMl);
    TEST_ASSERT_EQUAL_UINT32(100, app.snapshot().calibrationStepMl);
}

void test_app_controller_local_record_calibration_adjusts_and_saves_actual() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    AppController app(config, statistics, filters, records, nullptr, &calibrations);
    applyTestMeteringScheme(app);

    finishVolumeRun(app);
    app.tick(input({false, true, false, false}, 6000, 6000000, 1714502401));
    app.tick(input({false, true, false, false}, 11000, 11000000, 1714502406));

    pressAndReleasePlus(app, 11200);
    TEST_ASSERT_EQUAL_UINT32(1600, app.snapshot().calibrationActualMl);
    pressAndReleaseMinus(app, 11400);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().calibrationActualMl);
    pressAndReleasePlus(app, 11600);
    pressAndReleaseOk(app, 11800);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_size_t(1, calibrations.calibrations.size());
    TEST_ASSERT_EQUAL_UINT32(1600, calibrations.calibrations[0].actualMl);
}

void test_app_controller_snapshot_reports_current_flow_rate() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app, 1000);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    app.onFlowPulse(1000000UL);
    app.onFlowPulse(2000000UL);
    app.tick(input({false, false, false, false}, 2100, 2100000UL, 1714502400));

    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().instantFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().displayFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(120, app.snapshot().runAverageFlowMlPerMin);

    app.tick(input({false, false, false, false}, 5100, 5100000UL, 1714502403));

    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().instantFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().displayFlowMlPerMin);
}

void test_app_controller_uses_window_flow_for_high_flow_safety() {
    SystemConfig config = makeDefaultConfig();
    config.highFlowMlPerMin = 1000;
    config.highFlowDurationSec = 1;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app, 1000);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    app.onFlowPulse(1000000UL);
    app.onFlowPulse(1001000UL);
    app.tick(input({false, false, false, false}, 1100, 1100000UL, 1714502400));
    TEST_ASSERT_GREATER_THAN_UINT32(1000, app.snapshot().instantFlowMlPerMin);
    TEST_ASSERT_LESS_THAN_UINT32(1000, app.snapshot().windowFlowMlPerMin);

    app.tick(input({false, false, false, false}, 2200, 2200000UL, 1714502401));
    TEST_ASSERT_FALSE(records.records.size() > 0 &&
                      records.records.back().result == WaterResult::FlowError);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_app_controller_uses_active_scheme_parameters_for_flow_meter);
    RUN_TEST(test_app_snapshot_contains_water_sensor_snapshot);
    RUN_TEST(test_temperature_reference_calibration_sets_offset_from_raw_temperature);
    RUN_TEST(test_temperature_reference_calibration_rejects_disabled_temperature_sensor);
    RUN_TEST(test_app_records_sensor_summary_on_completed_run);
    RUN_TEST(test_app_rejects_tds_calibration_when_running);
    RUN_TEST(test_app_tds_point_calibration_apply_persists_to_config);
    RUN_TEST(test_app_controller_successful_record_writes_scheme_id_and_marks_scheme_used_once);
    RUN_TEST(test_app_controller_record_write_failure_does_not_mark_scheme_used);
    RUN_TEST(test_app_controller_record_write_success_locks_active_scheme_even_if_used_mark_persist_fails);
    RUN_TEST(test_app_controller_starts_after_double_ok_and_opens_valve);
    RUN_TEST(test_app_controller_cancel_raw_dominates_pending_ok_release);
    RUN_TEST(test_app_controller_confirm_and_running_start_volume_stays_zero_until_first_pulse);
    RUN_TEST(test_app_controller_completion_writes_record_statistics_and_filters);
    RUN_TEST(test_app_controller_pushes_closed_valve_output_before_record_persistence);
    RUN_TEST(test_app_controller_web_preset_switch_during_run_updates_next_preset_only);
    RUN_TEST(test_app_controller_local_plus_does_not_switch_preset_while_running);
    RUN_TEST(test_app_controller_offline_completion_marks_unknown_time_with_boot_id);
    RUN_TEST(test_app_controller_offline_start_sync_before_completion_writes_real_time);
    RUN_TEST(test_app_controller_pause_resume_then_completion_updates_persistence_once);
    RUN_TEST(test_app_controller_stop_down_closes_valve_and_records_user_stop);
    RUN_TEST(test_app_controller_normal_output_does_not_collect_ram_pulse_trace);
    RUN_TEST(test_app_controller_emergency_stop_closes_valve_without_debounce);
    RUN_TEST(test_app_controller_applies_config_only_while_idle);
    RUN_TEST(test_app_controller_emits_beep_patterns_for_actions_and_completion);
    RUN_TEST(test_app_controller_reports_record_write_failure_without_losing_statistics);
    RUN_TEST(test_app_controller_adjusts_volume_target_with_configured_step_without_ok_long_toggle);
    RUN_TEST(test_app_controller_adjusts_time_target_with_configured_step);
    RUN_TEST(test_app_controller_stopped_volume_does_not_clamp_next_confirm_adjustment);
    RUN_TEST(test_app_controller_starting_calibration_from_idle_enters_preparing);
    RUN_TEST(test_app_controller_starting_calibration_while_running_is_rejected);
    RUN_TEST(test_app_controller_starting_calibration_twice_is_rejected);
    RUN_TEST(test_app_controller_calibration_preparing_times_out_to_discarded);
    RUN_TEST(test_app_controller_calibration_ready_and_generated_time_out_from_last_action);
    RUN_TEST(test_app_controller_reboot_drops_awaiting_actual_when_ram_trace_missing);
    RUN_TEST(test_app_controller_local_ok_starts_calibration_run_and_completion_awaits_actual);
    RUN_TEST(test_app_controller_generates_calibration_session_candidate);
    RUN_TEST(test_app_controller_auto_generates_after_second_valid_calibration_sample);
    RUN_TEST(test_app_controller_submit_actual_succeeds_when_auto_refresh_cannot_generate);
    RUN_TEST(test_app_controller_removed_valid_sample_clears_generated_candidate);
    RUN_TEST(test_app_controller_remove_sample_session_save_failure_keeps_original_trace);
    RUN_TEST(test_app_controller_pending_actual_sample_can_be_removed);
    RUN_TEST(test_app_controller_reuses_removed_trace_slot_without_overwriting_valid_sample);
    RUN_TEST(test_app_controller_remove_one_of_three_valid_samples_regenerates_candidate);
    RUN_TEST(test_app_controller_applies_generated_session_scheme_and_keeps_old_scheme);
    RUN_TEST(test_app_controller_applies_calibration_from_raw_record);
    RUN_TEST(test_app_controller_small_record_calibration_keeps_metering_parameters);
    RUN_TEST(test_app_controller_pause_timeout_trace_is_not_marked_error_and_can_calibrate);
    RUN_TEST(test_app_controller_applies_calibration_from_pause_timeout_record);
    RUN_TEST(test_app_controller_result_display_exits_after_configured_timeout);
    RUN_TEST(test_app_controller_result_ok_hold_enters_local_record_calibration_after_5s);
    RUN_TEST(test_app_controller_local_record_calibration_adjusts_and_saves_actual);
    RUN_TEST(test_app_controller_snapshot_reports_current_flow_rate);
    RUN_TEST(test_app_controller_uses_window_flow_for_high_flow_safety);
    return UNITY_END();
}
