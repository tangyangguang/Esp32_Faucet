#pragma once

#include <cstdint>

namespace faucet {

class AppController;
class ConfigStore;
class FilterStore;
class WaterLogFileStore;
struct SystemConfig;

using FaucetNowSeconds = std::uint32_t (*)();

struct FaucetWebContext {
    SystemConfig* config;
    ConfigStore* configStore;
    const AppController* app;
    FilterStore* filters;
    const WaterLogFileStore* logs;
    FaucetNowSeconds nowSeconds;
};

void setFaucetWebContext(const FaucetWebContext& context);
bool registerFaucetWeb();

}  // namespace faucet
