#pragma once

#include "app/AppConfig.h"
#include "app/AppController.h"
#include "app/FilterStore.h"

#include <cstddef>

namespace faucet {

bool writeStatusJson(const AppSnapshot& snapshot, char* out, std::size_t len);
bool writeStatsJson(const StatisticsRecord& record, char* out, std::size_t len);
bool writeConfigJson(const SystemConfig& config, char* out, std::size_t len);
bool writePresetsJson(const PresetConfig (&presets)[kPresetCount], char* out, std::size_t len);
bool writeFiltersJson(const FilterRecord (&filters)[kFilterCount], char* out, std::size_t len);
bool writeCalibrationJson(const SystemConfig& config, char* out, std::size_t len);
bool writeWaterLogsJson(const WaterLogRecord* records,
                        std::size_t recordCount,
                        std::size_t pageIndex,
                        std::uint16_t pageSize,
                        std::size_t totalCount,
                        const char* storageName,
                        char* out,
                        std::size_t len);

}  // namespace faucet
