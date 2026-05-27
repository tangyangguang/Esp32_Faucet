#pragma once

#include "app/DisplayPresenter.h"

#include <cstdint>

namespace faucet {

class AppController;
class ConfigStore;
class FilterStore;
class WaterRecordCalibrationReader;
class WaterRecordCalibrationWriter;
class WaterRecordReader;
struct SystemConfig;

using FaucetNowSeconds = std::uint32_t (*)();
using FaucetBootId = std::uint32_t (*)();
using FaucetApplySettings = void (*)(const SystemConfig&);

struct FaucetDisplayStatus {
    DisplayFrame logicalFrame;
    bool screenOn;
};

using FaucetCurrentDisplayStatus = FaucetDisplayStatus (*)();

struct FaucetWebContext {
    SystemConfig* config;
    ConfigStore* configStore;
    AppController* app;
    FilterStore* filters;
    const WaterRecordReader* records;
    const WaterRecordCalibrationReader* recordCalibrations;
    WaterRecordCalibrationWriter* recordCalibrationWriter;
    FaucetNowSeconds nowSeconds;
    FaucetBootId bootId;
    FaucetApplySettings applySettings;
    FaucetCurrentDisplayStatus currentDisplayStatus;
};

void setFaucetWebContext(const FaucetWebContext& context);
bool registerFaucetWeb();

}  // namespace faucet
