#pragma once

#ifndef NATIVE_BUILD

#include "app/ConfigStore.h"
#include "app/AppConfig.h"

namespace faucet {

class AppController;

using FaucetAppConfigApplySettings = void (*)(const SystemConfig&);

struct FaucetAppConfigContext {
    SystemConfig* config;
    ConfigStore* configStore;
    AppController* app;
    FaucetAppConfigApplySettings applySettings;
};

bool registerFaucetAppConfig();
void setFaucetAppConfigContext(const FaucetAppConfigContext& context);

}  // namespace faucet

#endif
