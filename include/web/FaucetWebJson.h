#pragma once

#include "app/AppConfig.h"
#include "app/AppController.h"
#include "app/FilterStore.h"
#include "app/WaterRecordStore.h"

#include <cstddef>

namespace faucet {

struct ConfigRuntimeStatus {
    const char* loadStatus;
    std::int32_t rawVersion;
    std::int32_t currentVersion;
    bool readOnly;
    bool migrationWriteBack;
};

bool writeStatusJson(const AppSnapshot& snapshot, char* out, std::size_t len);
bool writeStatusJson(const AppSnapshot& snapshot, bool screenOn, char* out, std::size_t len);
bool writeStatusJson(const AppSnapshot& snapshot,
                     bool screenOn,
                     const SystemConfig& config,
                     char* out,
                     std::size_t len);
bool writeStatusJson(const AppSnapshot& snapshot,
                     bool screenOn,
                     const SystemConfig& config,
                     const ConfigRuntimeStatus* configStatus,
                     char* out,
                     std::size_t len);
bool writeStatsJson(const StatisticsRecord& record, char* out, std::size_t len);
bool writeUsageSummaryJson(const WaterUsageSummary& summary,
                           std::uint32_t totalMl,
                           char* out,
                           std::size_t len);
bool writeConfigJson(const SystemConfig& config, char* out, std::size_t len);
bool writePresetsJson(const PresetConfig (&presets)[kPresetCount], char* out, std::size_t len);
bool writeFiltersJson(const FilterRecord (&filters)[kFilterCount], char* out, std::size_t len);
bool writeWaterRecordsJson(const WaterRecord* records,
                           std::size_t recordCount,
                           std::size_t pageIndex,
                           std::uint16_t pageSize,
                           std::size_t totalCount,
                           const char* storageName,
                           const char* storageStatus,
                           char* out,
                           std::size_t len);

}  // namespace faucet
