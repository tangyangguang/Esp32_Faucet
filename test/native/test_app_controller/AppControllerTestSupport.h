#pragma once

#include "app/AppController.h"
#include "app/CalibrationSessionStore.h"
#include "app/CalibrationSessionTraceStore.h"
#include "app/MeteringSchemeStore.h"
#include "app/WaterSensorManager.h"
#include "../support/FakeAdcReader.h"
#include "../support/MemoryFileBackend.h"
#include "../support/MemoryRecordWriter.h"

#include <cstdint>
#include <vector>

namespace faucet_test {

faucet::SystemConfig enabledWaterSensorConfig();

bool prepareMeteringScheme(faucet::MeteringSchemeStore& store,
                           std::uint32_t stablePulsePerLiter,
                           faucet::MeteringSchemeRecord& active);

struct SensorAppFixture {
    faucet::SystemConfig config;
    faucet::StatisticsStore statistics;
    faucet::FilterStore filters;
    MemoryRecordWriter records;
    FakeAdcReader adc;
    faucet::WaterSensorManager sensors;
    faucet::AppController app;

    explicit SensorAppFixture(const faucet::SystemConfig& initialConfig = enabledWaterSensorConfig());

    void setDefaultSensorReadings();
};

faucet::AppTickInput input(faucet::ButtonLevels levels,
                           std::uint32_t nowMs,
                           std::uint32_t nowUs,
                           std::uint32_t nowSeconds);
faucet::AppTickInput offlineInput(faucet::ButtonLevels levels,
                                  std::uint32_t nowMs,
                                  std::uint32_t nowUs,
                                  std::uint32_t uptimeSeconds);

void pressAndReleaseOk(faucet::AppController& app, std::uint32_t baseMs);
void pressAndReleaseOkAt(faucet::AppController& app, std::uint32_t baseMs, std::uint32_t nowSeconds);
void longPressOk(faucet::AppController& app, std::uint32_t baseMs);
void pressAndReleasePlus(faucet::AppController& app, std::uint32_t baseMs);
void pressAndReleaseMinus(faucet::AppController& app, std::uint32_t baseMs);
void pressAndReleaseCancelAt(faucet::AppController& app, std::uint32_t baseMs, std::uint32_t nowSeconds);
void finishVolumeRun(faucet::AppController& app);
void applyTestMeteringScheme(faucet::AppController& app, std::uint32_t stablePulsePerLiter = 1000);

faucet::WaterRecord calibrationRecord(std::uint32_t startTime,
                                      std::uint32_t totalPulses,
                                      std::uint32_t actualMl);

void saveCalibrationSessionSample(faucet::CalibrationSessionTraceStore& traceStore,
                                  faucet::CalibrationSessionRecord& session,
                                  std::uint8_t slot,
                                  std::uint32_t startTime,
                                  std::uint32_t actualMl,
                                  std::uint32_t startupPulses,
                                  std::uint32_t stablePulses,
                                  std::uint32_t stableSeconds);

struct CalibrationAppFixture {
    faucet::SystemConfig config = faucet::makeDefaultConfig();
    faucet::StatisticsStore statistics;
    faucet::FilterStore filters;
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    faucet::MeteringSchemeStore schemes;
    faucet::MeteringSchemeRecord active{};
    faucet::CalibrationSessionFileStore sessionStore;
    faucet::CalibrationSessionTraceStore traceStore;
    faucet::WaterPulseTrace ramTraces[4]{};
    faucet::WaterPulseTraceBucketSample ramBuckets[4096]{};
    faucet::WaterPulseTraceSample ramStartupEdges[4096]{};
    faucet::WaterPulseTraceStore pulseTraces;
    faucet::AppController* app = nullptr;

    CalibrationAppFixture();
    ~CalibrationAppFixture();

    void createApp();
    void createAppWithoutMeteringStore();
};

struct CompactTraceCalibrationAppFixture {
    faucet::SystemConfig config = faucet::makeDefaultConfig();
    faucet::StatisticsStore statistics;
    faucet::FilterStore filters;
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    faucet::MeteringSchemeStore schemes;
    faucet::MeteringSchemeRecord active{};
    faucet::CalibrationSessionFileStore sessionStore;
    faucet::CalibrationSessionTraceStore traceStore;
    faucet::WaterPulseTrace ramTraces[1]{};
    faucet::WaterPulseTraceBucketSample ramBuckets[faucet::kPulseTraceMaxBucketsPerTrace]{};
    faucet::WaterPulseTraceSample ramStartupEdges[faucet::kPulseTraceMaxStartupEdgesPerTrace]{};
    faucet::WaterPulseTraceStore pulseTraces;
    faucet::AppController* app = nullptr;

    CompactTraceCalibrationAppFixture();
    ~CompactTraceCalibrationAppFixture();

    void createApp();
};

void savePendingRamCalibrationAttempt(CalibrationAppFixture& fixture,
                                      faucet::CalibrationSessionRecord& session,
                                      std::uint8_t slot,
                                      std::uint32_t startTime,
                                      std::uint32_t actualMl,
                                      std::uint32_t startupPulses,
                                      std::uint32_t stablePulses,
                                      std::uint32_t stableSeconds);

void savePendingRamCalibrationAttemptWithPulseEdges(CalibrationAppFixture& fixture,
                                                    faucet::CalibrationSessionRecord& session,
                                                    std::uint8_t slot,
                                                    std::uint32_t startTime,
                                                    std::uint32_t actualMl,
                                                    const std::uint32_t* elapsedUs,
                                                    std::size_t edgeCount,
                                                    std::uint32_t durationSec);

void saveOneValidOnePendingSession(CalibrationAppFixture& fixture, std::uint32_t nowSeconds);

}  // namespace faucet_test
