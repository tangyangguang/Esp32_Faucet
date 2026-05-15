#pragma once

#include "app/AppConfig.h"
#include "app/StatisticsStore.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

class ConfigBackend {
public:
    virtual ~ConfigBackend() = default;

    virtual bool setInt(const char* ns, const char* key, std::int32_t value) = 0;
    virtual std::int32_t getInt(const char* ns, const char* key, std::int32_t def) = 0;
    virtual bool setBool(const char* ns, const char* key, bool value) = 0;
    virtual bool getBool(const char* ns, const char* key, bool def) = 0;
    virtual bool setStr(const char* ns, const char* key, const char* value) = 0;
    virtual bool getStr(const char* ns, const char* key, char* out, std::size_t len, const char* def) = 0;
    virtual bool clearNamespace(const char* ns) = 0;
};

class ConfigStore {
public:
    enum class LoadStatus : std::uint8_t {
        DefaultsNoVersion = 0,
        LoadedCurrent = 1,
        MigratedLegacy = 2,
        LoadedFutureVersionReadOnly = 3,
        UnsupportedVersionDefault = 4,
    };

    explicit ConfigStore(ConfigBackend& backend);

    SystemConfig loadSystemConfig();
    bool saveSystemConfig(const SystemConfig& config);
    bool resetSystemConfig();
    LoadStatus lastSystemConfigLoadStatus() const;
    bool systemConfigReadOnly() const;

    StatisticsRecord loadStatistics(const PeriodKeys& defaultKeys);
    bool saveStatistics(const StatisticsRecord& record);
    bool resetStatistics(const PeriodKeys& keys);

    void loadFilterRuntime(FilterRecord (&records)[kFilterCount]);
    bool saveFilterRuntime(const FilterRecord (&records)[kFilterCount]);
    bool resetFilterRuntime();
    std::uint16_t allocateBootId();

private:
    ConfigBackend& backend_;
    LoadStatus lastSystemStatus_;
    bool systemConfigReadOnly_;
};

}  // namespace faucet
