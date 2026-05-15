#pragma once

#include <cstdint>

namespace faucet {

class AppController;
class ConfigStore;
class FilterStore;
class WaterLogReader;
struct SystemConfig;

using FaucetNowSeconds = std::uint32_t (*)();
using FaucetBootId = std::uint32_t (*)();
using FaucetApplySettings = void (*)(const SystemConfig&);

struct FaucetWebContext {
    SystemConfig* config;
    ConfigStore* configStore;
    AppController* app;
    FilterStore* filters;
    const WaterLogReader* logs;
    FaucetNowSeconds nowSeconds;
    FaucetBootId bootId;
    FaucetApplySettings applySettings;
};

void setFaucetWebContext(const FaucetWebContext& context);
bool registerFaucetWeb();

}  // namespace faucet
