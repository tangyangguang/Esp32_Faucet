#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kMinVolumePresetMl = 100;
constexpr std::uint32_t kMaxVolumePresetMl = 20000;
constexpr std::uint32_t kMinTimePresetSec = 5;
constexpr std::uint32_t kMaxTimePresetSec = 1800;

constexpr std::uint32_t kDefaultConfirmTimeoutSec = 10;
constexpr std::uint32_t kDefaultMaxOutTimeSec = 1800;
constexpr std::uint32_t kDefaultMaxOutVolumeMl = 30000;
constexpr std::uint8_t kDefaultOverflowPercent = 10;
constexpr std::uint32_t kDefaultNoFlowTimeoutSec = 3;
constexpr std::uint32_t kDefaultHighFlowMlPerMin = 30000;
constexpr std::uint32_t kDefaultHighFlowDurationSec = 5;
constexpr std::uint32_t kDefaultPauseTimeoutSec = 300;

constexpr float kDefaultPulsePerMl = 0.45f;
constexpr float kMinPulsePerMl = 0.05f;
constexpr float kMaxPulsePerMl = 5.0f;
constexpr std::uint32_t kDefaultCalibrationTargetMl = 1000;
constexpr std::uint8_t kMinCalibrationSamples = 2;
constexpr std::uint8_t kCalibrationTolerancePercent = 5;

constexpr std::uint32_t kDefaultValveFullPowerSec = 3;
constexpr std::uint8_t kDefaultValveHoldDutyPercent = 30;
constexpr std::uint8_t kMinValveHoldDutyPercent = 20;
constexpr std::uint8_t kMaxValveHoldDutyPercent = 100;

constexpr std::uint32_t kDefaultOledSleepSec = 30;
constexpr std::uint16_t kDefaultLogPageSize = 50;
constexpr std::uint16_t kMaxLogPageSize = 200;
constexpr std::uint32_t kMaxFilterLifeDays = 3650;
constexpr std::uint32_t kMaxFilterLifeMl = 10000000;

struct SystemConfig {
    std::uint32_t confirmTimeoutSec;
    std::uint32_t maxOutTimeSec;
    std::uint32_t maxOutVolumeMl;
    std::uint8_t overflowPercent;
    std::uint32_t noFlowTimeoutSec;
    std::uint32_t highFlowMlPerMin;
    std::uint32_t highFlowDurationSec;
    std::uint32_t pauseTimeoutSec;
    float pulsePerMl;
    std::uint32_t calibrationTargetMl;
    std::uint32_t valveFullPowerSec;
    std::uint8_t valveHoldDutyPercent;
    std::uint32_t oledSleepSec;
    bool beepEnabled;
    PresetConfig presets[kPresetCount];
    FilterRecord filters[kFilterCount];
};

SystemConfig makeDefaultConfig();
void sanitizeConfig(SystemConfig& config);

bool isValidCalibrationTarget(std::uint32_t targetMl);
std::uint16_t sanitizeLogPageSize(std::uint16_t pageSize);

}  // namespace faucet
