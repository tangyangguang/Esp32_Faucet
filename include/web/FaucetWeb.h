#pragma once

#include "app/DisplayPresenter.h"

#include <cstdint>

namespace faucet {

class AppController;
class CalibrationLongTermSampleStore;
class CalibrationSessionFileStore;
class CalibrationSessionTraceStore;
class ConfigStore;
class FilterStore;
class MeteringSchemeStore;
class StatisticsStore;
class WaterRecordCalibrationReader;
class WaterRecordCalibrationWriter;
class WaterRecordReader;
class WaterPulseTraceFileStore;
class WaterPulseTraceStore;
struct SystemConfig;

using FaucetNowSeconds = std::uint32_t (*)();
using FaucetBootId = std::uint32_t (*)();
using FaucetApplySettings = void (*)(const SystemConfig&);
using FaucetAfterFormatFs = void (*)();

struct FaucetDisplayStatus {
    DisplayFrame logicalFrame;
    bool screenOn;
};

using FaucetCurrentDisplayStatus = FaucetDisplayStatus (*)();

struct FaucetRuntimeDiagnostics {
    std::uint32_t maxLoopIntervalUs;
    std::uint32_t maxAppTickUs;
    std::uint32_t maxBaseHandleUs;
};

using FaucetCurrentRuntimeDiagnostics = FaucetRuntimeDiagnostics (*)();

struct FaucetWebContext {
    SystemConfig* config;
    ConfigStore* configStore;
    StatisticsStore* statistics;
    AppController* app;
    FilterStore* filters;
    const WaterRecordReader* records;
    const WaterRecordCalibrationReader* recordCalibrations;
    WaterRecordCalibrationWriter* recordCalibrationWriter;
    MeteringSchemeStore* meteringSchemes;
    CalibrationSessionFileStore* calibrationSessions;
    CalibrationSessionTraceStore* calibrationSessionTraces;
    CalibrationLongTermSampleStore* calibrationLongTermSamples;
    WaterPulseTraceStore* pulseTraces;
    WaterPulseTraceFileStore* savedPulseTraces;
    FaucetNowSeconds nowSeconds;
    FaucetBootId bootId;
    FaucetApplySettings applySettings;
    FaucetAfterFormatFs afterFormatFs;
    FaucetCurrentDisplayStatus currentDisplayStatus;
    FaucetCurrentRuntimeDiagnostics currentRuntimeDiagnostics;
};

void setFaucetWebContext(const FaucetWebContext& context);
bool registerFaucetWeb();

}  // namespace faucet
