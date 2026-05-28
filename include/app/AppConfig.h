#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kMinVolumePresetMl = 100;
constexpr std::uint32_t kMaxVolumePresetMl = 20000;
constexpr std::uint32_t kMinTimePresetSec = 5;
constexpr std::uint32_t kMaxTimePresetSec = 1800;

constexpr std::uint32_t kDefaultConfirmTimeoutSec = 60;
constexpr std::uint32_t kDefaultMaxOutTimeSec = 1800;
constexpr std::uint32_t kDefaultMaxOutVolumeMl = 30000;
constexpr std::uint8_t kDefaultOverflowPercent = 10;
constexpr std::uint32_t kDefaultNoFlowTimeoutSec = 10;
constexpr std::uint32_t kDefaultHighFlowMlPerMin = 30000;
constexpr std::uint32_t kDefaultHighFlowDurationSec = 5;
constexpr std::uint32_t kDefaultPauseTimeoutSec = 300;
constexpr std::uint32_t kDefaultVolumeAdjustStepMl = 100;
constexpr std::uint32_t kMinVolumeAdjustStepMl = 10;
constexpr std::uint32_t kMaxVolumeAdjustStepMl = 1000;
constexpr std::uint32_t kDefaultTimeAdjustStepSec = 10;
constexpr std::uint32_t kMinTimeAdjustStepSec = 1;
constexpr std::uint32_t kMaxTimeAdjustStepSec = 300;
constexpr std::uint32_t kDefaultStartupCompensationMl = 0;
constexpr std::uint32_t kMaxStartupCompensationMl = 300;
constexpr std::uint32_t kDefaultPulseTraceMemoryKb = 50;
constexpr std::uint32_t kMinPulseTraceMemoryKb = 10;
constexpr std::uint32_t kMaxPulseTraceMemoryKb = 50;
constexpr std::uint32_t kMaxSegmentedPulsePerLiter = 5000;
constexpr std::uint32_t kMaxSegmentedStartupDurationSec = 600;
constexpr std::uint32_t kMaxSegmentedStartupPulseCount = 100000;
constexpr std::uint32_t kMaxSegmentedStartupVolumeMl = 20000;

constexpr float kDefaultPulsePerMl = 0.45f;
constexpr float kMinPulsePerMl = 0.05f;
constexpr float kMaxPulsePerMl = 5.0f;

constexpr std::uint32_t kDefaultValveFullPowerSec = 5;
constexpr std::uint8_t kDefaultValveHoldDutyPercent = 70;
constexpr std::uint8_t kMinValveHoldDutyPercent = 20;
constexpr std::uint8_t kMaxValveHoldDutyPercent = 100;

constexpr std::uint32_t kDefaultDisplaySleepSec = 60;
constexpr std::uint32_t kDefaultResultDisplaySec = 30;
constexpr std::uint8_t kDefaultLcdI2cAddress = 0x27;
constexpr std::uint16_t kDefaultRecordPageSize = 20;
constexpr std::uint16_t kMaxRecordPageSize = 200;
constexpr std::uint32_t kDaysPerLifeMonth = 30;
constexpr std::uint32_t kMaxFilterLifeDays = 3650;
constexpr std::uint32_t kMaxFilterLifeMl = 10000000;

enum class FilterLifeStatus : std::uint8_t {
    Normal = 0,
    RecommendReplace = 1,
    Expired = 2,
};

struct SystemConfig {
    std::uint32_t confirmTimeoutSec;
    std::uint32_t maxOutTimeSec;
    std::uint32_t maxOutVolumeMl;
    std::uint8_t overflowPercent;
    std::uint32_t noFlowTimeoutSec;
    std::uint32_t highFlowMlPerMin;
    std::uint32_t highFlowDurationSec;
    std::uint32_t pauseTimeoutSec;
    std::uint32_t volumeAdjustStepMl;
    std::uint32_t timeAdjustStepSec;
    std::uint32_t startupCompensationMl;
    std::uint32_t pulseTraceMemoryKb;
    std::uint32_t overallPulsePerLiter;
    std::uint32_t startupDurationSec;
    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t startupPulsePerLiter;
    std::uint32_t stablePulsePerLiter;
    bool segmentedMeteringCalibrated;
    float pulsePerMl;
    std::uint32_t valveFullPowerSec;
    std::uint8_t valveHoldDutyPercent;
    std::uint32_t displaySleepSec;
    std::uint32_t resultDisplaySec;
    std::uint8_t lcdI2cAddress;
    bool beepEnabled;
    PresetConfig presets[kPresetCount];
    FilterRecord filters[kFilterCount];
};

SystemConfig makeDefaultConfig();
void sanitizeConfig(SystemConfig& config);

std::uint16_t sanitizeRecordPageSize(std::uint16_t pageSize);
FilterLifeStatus filterLifeStatus(const FilterRecord& filter, std::uint32_t usedDays);

}  // namespace faucet
