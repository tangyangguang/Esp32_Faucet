#pragma once

#ifndef NATIVE_BUILD

#include "app/ConfigStore.h"

namespace faucet {

class Esp32BaseConfigBackend : public ConfigBackend {
public:
    bool setInt(const char* ns, const char* key, std::int32_t value) override;
    std::int32_t getInt(const char* ns, const char* key, std::int32_t def) override;
    bool setBool(const char* ns, const char* key, bool value) override;
    bool getBool(const char* ns, const char* key, bool def) override;
    bool setStr(const char* ns, const char* key, const char* value) override;
    bool getStr(const char* ns, const char* key, char* out, std::size_t len, const char* def) override;
    bool clearNamespace(const char* ns) override;
};

}  // namespace faucet

#endif
