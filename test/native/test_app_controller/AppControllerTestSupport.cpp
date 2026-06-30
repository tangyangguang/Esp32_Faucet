#include "AppControllerTestSupport.h"

#include "../support/CalibrationTraceTestSupport.h"

#include <cstdio>

#include <unity.h>

namespace faucet_test {

using namespace faucet;

MeteringSchemeCandidate testMeteringCandidate(std::uint32_t stablePulsePerLiter) {
    MeteringSchemeCandidate candidate{};
    candidate.ready = true;
    candidate.sourceType = MeteringSchemeSource::CalibrationSession;
    candidate.params = MeteringParameters{0, 0, stablePulsePerLiter, 0, 1950};
    candidate.generatedAt = 1714502300;
    candidate.sampleCount = 2;
    candidate.minActualMl = 1000;
    candidate.maxActualMl = 2000;
    return candidate;
}

MeteringSchemeRecord testMeteringSchemeRecord(std::uint32_t id,
                                              const char* name,
                                              const MeteringParameters& params) {
    MeteringSchemeRecord scheme{};
    scheme.id = id;
    scheme.recordUsed = true;
    std::snprintf(scheme.name, sizeof(scheme.name), "%s", name ? name : "测试计量参数");
    scheme.params = params;
    scheme.sourceType = MeteringSchemeSource::CalibrationSession;
    scheme.createdAt = 1714502300;
    scheme.sampleCount = 2;
    scheme.minActualMl = 1000;
    scheme.maxActualMl = 2000;
    return scheme;
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
    const MeteringSchemeCandidate candidate = testMeteringCandidate(stablePulsePerLiter);
    if (!store.saveCandidateAsNew(candidate, "运行参数", 1714502300, id)) {
        return false;
    }
    if (!store.setActiveScheme(id)) {
        return false;
    }
    return store.activeScheme(active);
}

SensorAppFixture::SensorAppFixture(const SystemConfig& initialConfig)
    : config(initialConfig),
      filters(config.filters),
      sensors(adc),
      app(config, statistics, filters, records, nullptr, nullptr, nullptr, &sensors) {
    statistics.reset({20260506, 202619, 202605});
    sensors.configure(config);
    TEST_ASSERT_TRUE(sensors.begin());
}

void SensorAppFixture::setDefaultSensorReadings() {
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
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
    app.tick(input({false, true, false, false},
                   baseMs + kButtonDebounceMs + kButtonLongPressMs,
                   (baseMs + kButtonDebounceMs + kButtonLongPressMs) * 1000UL,
                   1001));
    app.tick(input({false, false, false, false},
                   baseMs + kButtonDebounceMs + kButtonLongPressMs + 20,
                   (baseMs + kButtonDebounceMs + kButtonLongPressMs + 20) * 1000UL,
                   1001));
    app.tick(input({false, false, false, false},
                   baseMs + 2 * kButtonDebounceMs + kButtonLongPressMs + 20,
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

void pressAndReleaseCancelAt(AppController& app, std::uint32_t baseMs, std::uint32_t nowSeconds) {
    app.tick(input({true, false, false, false}, baseMs, baseMs * 1000UL, nowSeconds));
    app.tick(input({true, false, false, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, nowSeconds));
    app.tick(input({false, false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL, nowSeconds));
    app.tick(input({false, false, false, false},
                   baseMs + 60 + kButtonDebounceMs,
                   (baseMs + 60 + kButtonDebounceMs) * 1000UL,
                   nowSeconds));
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

void applyTestMeteringScheme(AppController& app, std::uint32_t stablePulsePerLiter) {
    const MeteringSchemeRecord scheme =
        testMeteringSchemeRecord(99, "测试计量参数", MeteringParameters{0, 0, stablePulsePerLiter, 0, 1950});
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

void saveCalibrationSessionSample(CalibrationSessionTraceStore& traceStore,
                                  CalibrationSessionRecord& session,
                                  std::uint8_t slot,
                                  std::uint32_t startTime,
                                  std::uint32_t actualMl,
                                  std::uint32_t startupPulses,
                                  std::uint32_t stablePulses,
                                  std::uint32_t stableSeconds) {
    const std::uint32_t totalPulses = startupPulses + stablePulses;
    WaterRecord record = calibrationRecord(startTime, totalPulses, actualMl);
    record.durationSec = 5 + stableSeconds;
    TEST_ASSERT_TRUE(saveCompletedCalibrationTrace(
        traceStore, slot, session.sessionId, record, actualMl, startupPulses, stablePulses, stableSeconds));

    CalibrationAttempt attempt{};
    attempt.attemptIndex = slot;
    attempt.sessionTraceSlot = slot;
    attempt.record = record;
    attempt.targetHintMl = actualMl;
    attempt.actualMl = actualMl;
    attempt.summary.actualMl = actualMl;
    attempt.summary.totalPulses = totalPulses;
    attempt.summary.rejectedPulses = record.rejectedPulseCount;
    attempt.summary.durationSec = record.durationSec;
    attempt.summary.stable = true;
    attempt.summary.startupPulseCount = startupPulses;
    attempt.summary.stablePulseCount = stablePulses;
    attempt.summary.stableStartSec = 5;
    attempt.summary.stablePulsePerSec = static_cast<float>(stablePulses) / static_cast<float>(stableSeconds);
    attempt.summary.usableForGeneration = stablePulses > 0 && actualMl >= kCalibrationMinActualMl;
    attempt.status = CalibrationAttemptStatus::Valid;
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
}

CalibrationAppFixture::CalibrationAppFixture()
    : filters(config.filters),
      schemes(backend, "/schemes.bin"),
      sessionStore(backend, "/cal-session.bin"),
      traceStore(backend, "/cal-traces.bin"),
      pulseTraces(ramTraces, 4, ramBuckets, 4096, ramStartupEdges, 4096, 4) {
    statistics.reset({20260506, 202619, 202605});
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
}

CalibrationAppFixture::~CalibrationAppFixture() {
    delete app;
}

void CalibrationAppFixture::createApp() {
    app = new AppController(config,
                            active,
                            statistics,
                            filters,
                            records,
                            schemes,
                            &pulseTraces,
                            &sessionStore,
                            &traceStore);
}

void CalibrationAppFixture::createAppWithoutMeteringStore() {
    app = new AppController(config,
                            statistics,
                            filters,
                            records,
                            &pulseTraces,
                            &sessionStore,
                            &traceStore);
}

CompactTraceCalibrationAppFixture::CompactTraceCalibrationAppFixture()
    : filters(config.filters),
      schemes(backend, "/schemes.bin"),
      sessionStore(backend, "/cal-session.bin"),
      traceStore(backend, "/cal-traces.bin"),
      pulseTraces(ramTraces,
                  1,
                  ramBuckets,
                  kPulseTraceMaxBucketsPerTrace,
                  ramStartupEdges,
                  kPulseTraceMaxStartupEdgesPerTrace,
                  1) {
    statistics.reset({20260506, 202619, 202605});
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 2100, active));
    TEST_ASSERT_TRUE(sessionStore.begin());
    TEST_ASSERT_TRUE(traceStore.begin());
}

CompactTraceCalibrationAppFixture::~CompactTraceCalibrationAppFixture() {
    delete app;
}

void CompactTraceCalibrationAppFixture::createApp() {
    app = new AppController(config,
                            active,
                            statistics,
                            filters,
                            records,
                            schemes,
                            &pulseTraces,
                            &sessionStore,
                            &traceStore);
}

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
        TEST_ASSERT_TRUE(fixture.pulseTraces.appendPulseEdge(traceId, samples[i].elapsedUs));
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

void savePendingRamCalibrationAttemptWithPulseEdges(CalibrationAppFixture& fixture,
                                                    CalibrationSessionRecord& session,
                                                    std::uint8_t slot,
                                                    std::uint32_t startTime,
                                                    std::uint32_t actualMl,
                                                    const std::uint32_t* elapsedUs,
                                                    std::size_t edgeCount,
                                                    std::uint32_t durationSec) {
    const std::uint32_t traceId = fixture.pulseTraces.beginTrace(startTime, kDefaultPulseMinIntervalUs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, traceId);
    for (std::size_t i = 0; i < edgeCount; ++i) {
        TEST_ASSERT_TRUE(fixture.pulseTraces.appendPulseEdge(traceId, elapsedUs[i]));
    }

    WaterRecord record = calibrationRecord(startTime, static_cast<std::uint32_t>(edgeCount), actualMl);
    record.durationSec = durationSec;
    TEST_ASSERT_TRUE(fixture.pulseTraces.finishTrace(
        traceId, record, WaterPulseTraceState::Completed, durationSec * 1000000UL));

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

}  // namespace faucet_test
