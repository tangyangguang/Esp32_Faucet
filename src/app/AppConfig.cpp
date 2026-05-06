#include "app/AppConfig.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
    return std::min(std::max(value, minValue), maxValue);
}

void copyName(char (&dest)[kNameLength], const char* src) {
    std::strncpy(dest, src, kNameLength - 1);
    dest[kNameLength - 1] = '\0';
}

void setPreset(PresetConfig& preset, bool enabled, PresetType type, std::uint32_t value, const char* name) {
    preset.enabled = enabled;
    preset.type = type;
    preset.value = value;
    copyName(preset.name, name);
}

void setFilter(FilterRecord& filter, bool enabled, const char* name) {
    filter.enabled = enabled;
    filter.recommendDays = 180;
    filter.maxDays = 180;
    filter.lifeMl = 0;
    filter.startTime = 0;
    filter.usedMl = 0;
    copyName(filter.name, name);
}

}  // namespace

SystemConfig makeDefaultConfig() {
    SystemConfig config{};
    config.confirmTimeoutSec = kDefaultConfirmTimeoutSec;
    config.maxOutTimeSec = kDefaultMaxOutTimeSec;
    config.maxOutVolumeMl = kDefaultMaxOutVolumeMl;
    config.overflowPercent = kDefaultOverflowPercent;
    config.noFlowTimeoutSec = kDefaultNoFlowTimeoutSec;
    config.highFlowMlPerMin = kDefaultHighFlowMlPerMin;
    config.highFlowDurationSec = kDefaultHighFlowDurationSec;
    config.pauseTimeoutSec = kDefaultPauseTimeoutSec;
    config.pulsePerMl = kDefaultPulsePerMl;
    std::copy(kDefaultCalibrationTargetsMl,
              kDefaultCalibrationTargetsMl + kCalibrationTargetCount,
              config.calibrationTargetsMl);
    config.valveFullPowerSec = kDefaultValveFullPowerSec;
    config.valveHoldDutyPercent = kDefaultValveHoldDutyPercent;
    config.oledSleepSec = kDefaultOledSleepSec;
    config.beepEnabled = true;

    setPreset(config.presets[0], true, PresetType::Volume, 1500, "1.5L");
    setPreset(config.presets[1], true, PresetType::Volume, 7500, "7.5L");
    for (std::size_t i = 2; i < kPresetCount; ++i) {
        setPreset(config.presets[i], false, PresetType::Volume, 1000, "Preset");
    }

    setFilter(config.filters[0], true, "Filter 1");
    for (std::size_t i = 1; i < kFilterCount; ++i) {
        char name[kNameLength]{};
        std::snprintf(name, sizeof(name), "Filter %u", static_cast<unsigned>(i + 1));
        setFilter(config.filters[i], false, name);
    }

    return config;
}

void sanitizeConfig(SystemConfig& config) {
    config.confirmTimeoutSec = clampValue<std::uint32_t>(config.confirmTimeoutSec, 3, 60);
    config.maxOutTimeSec = clampValue<std::uint32_t>(config.maxOutTimeSec, 30, 7200);
    config.maxOutVolumeMl = clampValue<std::uint32_t>(config.maxOutVolumeMl, 1000, 100000);
    config.overflowPercent = clampValue<std::uint8_t>(config.overflowPercent, 1, 50);
    config.noFlowTimeoutSec = clampValue<std::uint32_t>(config.noFlowTimeoutSec, 1, 30);
    config.highFlowMlPerMin = clampValue<std::uint32_t>(config.highFlowMlPerMin, 1000, 100000);
    config.highFlowDurationSec = clampValue<std::uint32_t>(config.highFlowDurationSec, 1, 30);
    config.pauseTimeoutSec = clampValue<std::uint32_t>(config.pauseTimeoutSec, 10, 3600);
    config.pulsePerMl = clampValue<float>(config.pulsePerMl, kMinPulsePerMl, kMaxPulsePerMl);
    bool anyCalibrationTarget = false;
    for (auto& target : config.calibrationTargetsMl) {
        if (target == kDisabledCalibrationTargetMl) {
            continue;
        }
        if (!isValidCalibrationTarget(target)) {
            target = kDisabledCalibrationTargetMl;
            continue;
        }
        anyCalibrationTarget = true;
    }
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        if (config.calibrationTargetsMl[i] == kDisabledCalibrationTargetMl) {
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (config.calibrationTargetsMl[j] == config.calibrationTargetsMl[i]) {
                config.calibrationTargetsMl[i] = kDisabledCalibrationTargetMl;
                break;
            }
        }
    }
    if (!anyCalibrationTarget) {
        std::copy(kDefaultCalibrationTargetsMl,
                  kDefaultCalibrationTargetsMl + kCalibrationTargetCount,
                  config.calibrationTargetsMl);
    }
    config.valveFullPowerSec = clampValue<std::uint32_t>(config.valveFullPowerSec, 1, 10);
    config.valveHoldDutyPercent = clampValue<std::uint8_t>(
        config.valveHoldDutyPercent, kMinValveHoldDutyPercent, kMaxValveHoldDutyPercent);
    config.oledSleepSec = clampValue<std::uint32_t>(config.oledSleepSec, 5, 300);

    for (auto& preset : config.presets) {
        if (preset.type == PresetType::Volume) {
            preset.value = clampValue<std::uint32_t>(preset.value, kMinVolumePresetMl, kMaxVolumePresetMl);
        } else {
            preset.value = clampValue<std::uint32_t>(preset.value, kMinTimePresetSec, kMaxTimePresetSec);
        }
        preset.name[kNameLength - 1] = '\0';
    }

    for (auto& filter : config.filters) {
        filter.name[kNameLength - 1] = '\0';
        filter.recommendDays = clampValue<std::uint32_t>(filter.recommendDays, 0, kMaxFilterLifeDays);
        filter.maxDays = clampValue<std::uint32_t>(filter.maxDays, 0, kMaxFilterLifeDays);
        if (filter.recommendDays == 0) {
            filter.maxDays = 0;
        } else if (filter.maxDays == 0 || filter.maxDays < filter.recommendDays) {
            filter.maxDays = filter.recommendDays;
        }
        filter.lifeMl = clampValue<std::uint32_t>(filter.lifeMl, 0, kMaxFilterLifeMl);
    }
}

bool isValidCalibrationTarget(std::uint32_t targetMl) {
    return targetMl >= kMinCalibrationTargetMl && targetMl <= kMaxCalibrationTargetMl;
}

bool hasEnabledCalibrationTarget(const std::uint32_t (&targets)[kCalibrationTargetCount]) {
    return firstEnabledCalibrationTarget(targets) != kDisabledCalibrationTargetMl;
}

bool isEnabledCalibrationTarget(const std::uint32_t (&targets)[kCalibrationTargetCount], std::uint32_t targetMl) {
    if (targetMl == kDisabledCalibrationTargetMl) {
        return false;
    }
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        if (targets[i] == targetMl && isValidCalibrationTarget(targetMl)) {
            return true;
        }
    }
    return false;
}

std::uint32_t firstEnabledCalibrationTarget(const std::uint32_t (&targets)[kCalibrationTargetCount]) {
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        if (targets[i] != kDisabledCalibrationTargetMl && isValidCalibrationTarget(targets[i])) {
            return targets[i];
        }
    }
    return kDisabledCalibrationTargetMl;
}

std::uint32_t nextEnabledCalibrationTarget(const std::uint32_t (&targets)[kCalibrationTargetCount], std::uint32_t currentMl) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        if (targets[i] == currentMl) {
            start = i + 1;
            break;
        }
    }
    for (std::size_t step = 0; step < kCalibrationTargetCount; ++step) {
        const std::size_t index = (start + step) % kCalibrationTargetCount;
        if (targets[index] != kDisabledCalibrationTargetMl && isValidCalibrationTarget(targets[index])) {
            return targets[index];
        }
    }
    return kDisabledCalibrationTargetMl;
}

std::uint16_t sanitizeLogPageSize(std::uint16_t pageSize) {
    if (pageSize == 0) {
        return kDefaultLogPageSize;
    }
    return clampValue<std::uint16_t>(pageSize, 1, kMaxLogPageSize);
}

FilterLifeStatus filterLifeStatus(const FilterRecord& filter, std::uint32_t usedDays) {
    if (!filter.enabled) {
        return FilterLifeStatus::Normal;
    }
    if (filter.lifeMl > 0 && filter.usedMl >= filter.lifeMl) {
        return FilterLifeStatus::Expired;
    }
    if (filter.recommendDays == 0) {
        return FilterLifeStatus::Normal;
    }
    if (filter.maxDays > 0 && usedDays >= filter.maxDays) {
        return FilterLifeStatus::Expired;
    }
    if (usedDays >= filter.recommendDays) {
        return FilterLifeStatus::RecommendReplace;
    }
    return FilterLifeStatus::Normal;
}

}  // namespace faucet
