#ifndef NATIVE_BUILD

#include "app/Esp32BaseConfigBackend.h"

#include <Esp32Base.h>

namespace faucet {

bool Esp32BaseConfigBackend::setInt(const char* ns, const char* key, std::int32_t value) {
    return Esp32BaseConfig::setInt(ns, key, value);
}

std::int32_t Esp32BaseConfigBackend::getInt(const char* ns, const char* key, std::int32_t def) {
    return Esp32BaseConfig::getInt(ns, key, def);
}

bool Esp32BaseConfigBackend::setBool(const char* ns, const char* key, bool value) {
    return Esp32BaseConfig::setBool(ns, key, value);
}

bool Esp32BaseConfigBackend::getBool(const char* ns, const char* key, bool def) {
    return Esp32BaseConfig::getBool(ns, key, def);
}

bool Esp32BaseConfigBackend::setStr(const char* ns, const char* key, const char* value) {
    return Esp32BaseConfig::setStr(ns, key, value);
}

bool Esp32BaseConfigBackend::getStr(const char* ns, const char* key, char* out, std::size_t len, const char* def) {
    return Esp32BaseConfig::getStr(ns, key, out, len, def);
}

bool Esp32BaseConfigBackend::clearNamespace(const char* ns) {
    return Esp32BaseConfig::clearNamespace(ns);
}

}  // namespace faucet

#endif
