#pragma once

#include "app/ConfigStore.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace faucet_test {

class FakeConfigBackend : public faucet::ConfigBackend {
public:
    bool failWrites = false;
    std::string failKey;
    std::uint32_t filterRuntimeWrites = 0;

    bool setInt(const char* ns, const char* key, std::int32_t value) override {
        if (writeFails(ns, key)) {
            return false;
        }
        ints[makeKey(ns, key)] = value;
        countFilterRuntimeWrite(ns);
        return true;
    }

    std::int32_t getInt(const char* ns, const char* key, std::int32_t def) override {
        const auto it = ints.find(makeKey(ns, key));
        return it == ints.end() ? def : it->second;
    }

    bool setBool(const char* ns, const char* key, bool value) override {
        if (writeFails(ns, key)) {
            return false;
        }
        bools[makeKey(ns, key)] = value;
        return true;
    }

    bool getBool(const char* ns, const char* key, bool def) override {
        const auto it = bools.find(makeKey(ns, key));
        return it == bools.end() ? def : it->second;
    }

    bool setStr(const char* ns, const char* key, const char* value) override {
        if (writeFails(ns, key)) {
            return false;
        }
        strings[makeKey(ns, key)] = value ? value : "";
        countFilterRuntimeWrite(ns);
        return true;
    }

    bool getStr(const char* ns, const char* key, char* out, std::size_t len, const char* def) override {
        if (!out || len == 0) {
            return false;
        }
        const auto it = strings.find(makeKey(ns, key));
        const std::string value = it == strings.end() ? std::string(def ? def : "") : it->second;
        std::strncpy(out, value.c_str(), len - 1);
        out[len - 1] = '\0';
        return it != strings.end();
    }

    bool clearNamespace(const char* ns) override {
        const std::string prefix = std::string(ns ? ns : "") + "/";
        erasePrefix(ints, prefix);
        erasePrefix(bools, prefix);
        erasePrefix(strings, prefix);
        return true;
    }

private:
    bool writeFails(const char* ns, const char* key) const {
        return failWrites || (!failKey.empty() && failKey == makeKey(ns, key));
    }

    static std::string makeKey(const char* ns, const char* key) {
        return std::string(ns ? ns : "") + "/" + (key ? key : "");
    }

    void countFilterRuntimeWrite(const char* ns) {
        if (std::strcmp(ns ? ns : "", "faucet_run") == 0) {
            ++filterRuntimeWrites;
        }
    }

    template <typename T>
    void erasePrefix(std::map<std::string, T>& values, const std::string& prefix) {
        for (auto it = values.begin(); it != values.end();) {
            if (it->first.rfind(prefix, 0) == 0) {
                it = values.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::map<std::string, std::int32_t> ints;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strings;
};

}  // namespace faucet_test
