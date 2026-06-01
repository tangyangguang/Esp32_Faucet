#pragma once

#include "app/DisplayPresenter.h"

#include <cstdint>

namespace faucet {

class AppController;
class ConfigStore;
class FilterStore;
class MeteringSchemeStore;
class WaterRecordCalibrationReader;
class WaterRecordCalibrationWriter;
class WaterRecordMeteringSnapshotReader;
class WaterRecordMeteringSnapshotWriter;
class WaterRecordReader;
class WaterPulseTraceFileStore;
class WaterPulseTraceStore;
struct SystemConfig;

using FaucetNowSeconds = std::uint32_t (*)();
using FaucetBootId = std::uint32_t (*)();
using FaucetApplySettings = void (*)(const SystemConfig&);

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
    AppController* app;
    FilterStore* filters;
    const WaterRecordReader* records;
    const WaterRecordCalibrationReader* recordCalibrations;
    WaterRecordCalibrationWriter* recordCalibrationWriter;
    const WaterRecordMeteringSnapshotReader* recordMeteringSnapshots;
    WaterRecordMeteringSnapshotWriter* recordMeteringSnapshotWriter;
    MeteringSchemeStore* meteringSchemes;
    WaterPulseTraceStore* pulseTraces;
    WaterPulseTraceFileStore* savedPulseTraces;
    FaucetNowSeconds nowSeconds;
    FaucetBootId bootId;
    FaucetApplySettings applySettings;
    FaucetCurrentDisplayStatus currentDisplayStatus;
    FaucetCurrentRuntimeDiagnostics currentRuntimeDiagnostics;
};

void setFaucetWebContext(const FaucetWebContext& context);
bool registerFaucetWeb();

}  // namespace faucet
