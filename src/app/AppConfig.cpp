#include "app/AppConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

constexpr std::int16_t kMinTemperatureOffsetCentiC = -1000;
constexpr std::int16_t kMaxTemperatureOffsetCentiC = 1000;
constexpr float kMinTdsScale = 0.05f;
constexpr float kMaxTdsScale = 20.0f;
constexpr std::int16_t kMinTdsOffsetPpm = -2000;
constexpr std::int16_t kMaxTdsOffsetPpm = 2000;

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
    return std::min(std::max(value, minValue), maxValue);
}

template <typename Enum>
bool enumInRange(Enum value, Enum minValue, Enum maxValue) {
    return static_cast<std::uint8_t>(value) >= static_cast<std::uint8_t>(minValue) &&
           static_cast<std::uint8_t>(value) <= static_cast<std::uint8_t>(maxValue);
}

template <std::size_t N>
void copyText(char (&dest)[N], const char* src) {
    std::strncpy(dest, src ? src : "", N - 1);
    dest[N - 1] = '\0';
}

void setPreset(PresetConfig& preset, bool enabled, PresetType type, std::uint32_t value, const char* name) {
    preset.enabled = enabled;
    preset.type = type;
    preset.value = value;
    copyText(preset.name, name);
}

void setFilter(FilterConfig& filter, bool enabled, const char* name) {
    filter.enabled = enabled;
    filter.recommendDays = 180;
    filter.maxDays = 180;
    filter.lifeMl = 0;
    copyText(filter.name, name);
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
    config.volumeAdjustStepMl = kDefaultVolumeAdjustStepMl;
    config.timeAdjustStepSec = kDefaultTimeAdjustStepSec;
    config.pulseMinIntervalUs = kDefaultPulseMinIntervalUs;
    config.valveFullPowerSec = kDefaultValveFullPowerSec;
    config.valveHoldDutyPercent = kDefaultValveHoldDutyPercent;
    config.valvePwmFrequencyHz = kDefaultValvePwmFrequencyHz;
    config.displaySleepSec = kDefaultDisplaySleepSec;
    config.resultDisplaySec = kDefaultResultDisplaySec;
    config.beepEnabled = true;
    config.temperatureKind = TemperatureKind::None;
    config.temperatureOffsetCentiC = 0;
    config.temperatureCalibrated = false;
    config.tdsKind = TdsKind::None;
    config.tdsScale = 1.0f;
    config.tdsOffsetPpm = 0;
    config.tdsCalibrated = false;
    config.tdsTemperatureCompensationEnabled = true;

    setPreset(config.presets[0], true, PresetType::Volume, 1500, "1.5L");
    setPreset(config.presets[1], true, PresetType::Volume, 7500, "7.5L");
    for (std::size_t i = 2; i < kPresetCount; ++i) {
        setPreset(config.presets[i], false, PresetType::Volume, 1000, "预设");
    }

    setFilter(config.filters[0], true, "第1级滤芯");
    for (std::size_t i = 1; i < kFilterCount; ++i) {
        char name[kFilterNameLength]{};
        std::snprintf(name, sizeof(name), "第%u级滤芯", static_cast<unsigned>(i + 1));
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
    config.volumeAdjustStepMl =
        clampValue<std::uint32_t>(config.volumeAdjustStepMl, kMinVolumeAdjustStepMl, kMaxVolumeAdjustStepMl);
    config.timeAdjustStepSec =
        clampValue<std::uint32_t>(config.timeAdjustStepSec, kMinTimeAdjustStepSec, kMaxTimeAdjustStepSec);
    config.pulseMinIntervalUs =
        clampValue<std::uint32_t>(config.pulseMinIntervalUs, kMinPulseMinIntervalUs, kMaxPulseMinIntervalUs);

    config.valveFullPowerSec = clampValue<std::uint32_t>(config.valveFullPowerSec, 1, 10);
    config.valveHoldDutyPercent = clampValue<std::uint8_t>(
        config.valveHoldDutyPercent, kMinValveHoldDutyPercent, kMaxValveHoldDutyPercent);
    config.valvePwmFrequencyHz = clampValue<std::uint32_t>(
        config.valvePwmFrequencyHz, kMinValvePwmFrequencyHz, kMaxValvePwmFrequencyHz);
    config.displaySleepSec =
        clampValue<std::uint32_t>(config.displaySleepSec, kMinDisplaySleepSec, kMaxDisplaySleepSec);
    config.resultDisplaySec = clampValue<std::uint32_t>(config.resultDisplaySec, 0, 60);
    if (!enumInRange(config.temperatureKind, TemperatureKind::None, TemperatureKind::Ntc50kB3950)) {
        config.temperatureKind = TemperatureKind::None;
    }
    config.temperatureOffsetCentiC = clampValue<std::int16_t>(
        config.temperatureOffsetCentiC, kMinTemperatureOffsetCentiC, kMaxTemperatureOffsetCentiC);
    config.temperatureCalibrated = config.temperatureCalibrated && temperatureSensorEnabled(config);
    if (!enumInRange(config.tdsKind, TdsKind::None, TdsKind::AnalogTdsAo)) {
        config.tdsKind = TdsKind::None;
    }
    if (!std::isfinite(config.tdsScale)) {
        config.tdsScale = 1.0f;
    }
    config.tdsScale = clampValue<float>(config.tdsScale, kMinTdsScale, kMaxTdsScale);
    config.tdsOffsetPpm = clampValue<std::int16_t>(config.tdsOffsetPpm, kMinTdsOffsetPpm, kMaxTdsOffsetPpm);
    config.tdsCalibrated = config.tdsCalibrated && tdsSensorEnabled(config);

    for (auto& preset : config.presets) {
        if (preset.type == PresetType::Volume) {
            preset.value = clampValue<std::uint32_t>(preset.value, kMinVolumePresetMl, kMaxVolumePresetMl);
        } else {
            preset.value = clampValue<std::uint32_t>(preset.value, kMinTimePresetSec, kMaxTimePresetSec);
        }
        preset.name[kPresetNameLength - 1] = '\0';
    }

    for (FilterConfig& filter : config.filters) {
        filter.name[kFilterNameLength - 1] = '\0';
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

std::uint16_t sanitizeRecordPageSize(std::uint16_t pageSize) {
    if (pageSize == 0) {
        return kDefaultRecordPageSize;
    }
    return clampValue<std::uint16_t>(pageSize, 1, kMaxRecordPageSize);
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
