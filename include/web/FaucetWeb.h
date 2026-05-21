#pragma once

#include "app/DisplayPresenter.h"

#include <cstdint>

namespace faucet {

class AppController;
class ConfigStore;
class FilterStore;
class WaterRecordReader;
struct SystemConfig;

using FaucetNowSeconds = std::uint32_t (*)();
using FaucetBootId = std::uint32_t (*)();
using FaucetApplySettings = void (*)(const SystemConfig&);
using FaucetCurrentDisplayFrame = DisplayFrame (*)();

struct FaucetWebContext {
    SystemConfig* config;
    ConfigStore* configStore;
    AppController* app;
    FilterStore* filters;
    const WaterRecordReader* records;
    FaucetNowSeconds nowSeconds;
    FaucetBootId bootId;
    FaucetApplySettings applySettings;
    FaucetCurrentDisplayFrame currentDisplayFrame;
};

void setFaucetWebContext(const FaucetWebContext& context);
bool registerFaucetWeb();

}  // namespace faucet
