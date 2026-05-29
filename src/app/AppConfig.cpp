#include "app/AppConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
    return std::min(std::max(value, minValue), maxValue);
}

template <std::size_t N>
void copyName(char (&dest)[N], const char* src) {
    std::strncpy(dest, src, N - 1);
    dest[N - 1] = '\0';
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
    filter.startBootId = 0;
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
    config.volumeAdjustStepMl = kDefaultVolumeAdjustStepMl;
    config.timeAdjustStepSec = kDefaultTimeAdjustStepSec;
    config.startupCompensationMl = kDefaultStartupCompensationMl;
    config.pulseTraceMemoryKb = kDefaultPulseTraceMemoryKb;
    config.overallPulsePerLiter = 0;
    config.startupDurationSec = 0;
    config.startupPulseCount = 0;
    config.startupVolumeMl = 0;
    config.startupPulsePerLiter = 0;
    config.stablePulsePerLiter = 0;
    config.segmentedMeteringCalibrated = false;
    config.segmentedCandidateReady = false;
    config.candidateOverallPulsePerLiter = 0;
    config.candidateStartupDurationSec = 0;
    config.candidateStartupPulseCount = 0;
    config.candidateStartupVolumeMl = 0;
    config.candidateStartupPulsePerLiter = 0;
    config.candidateStablePulsePerLiter = 0;
    config.candidateSampleCount = 0;
    config.candidateMinActualMl = 0;
    config.candidateMaxActualMl = 0;
    config.candidateMaxErrorMl = 0;
    config.candidateGeneratedAt = 0;
    config.segmentedPreviousReady = false;
    config.previousSegmentedMeteringCalibrated = false;
    config.previousPulsePerMl = 0.0f;
    config.previousStartupCompensationMl = 0;
    config.previousOverallPulsePerLiter = 0;
    config.previousStartupDurationSec = 0;
    config.previousStartupPulseCount = 0;
    config.previousStartupVolumeMl = 0;
    config.previousStartupPulsePerLiter = 0;
    config.previousStablePulsePerLiter = 0;
    config.pulsePerMl = kDefaultPulsePerMl;
    config.valveFullPowerSec = kDefaultValveFullPowerSec;
    config.valveHoldDutyPercent = kDefaultValveHoldDutyPercent;
    config.displaySleepSec = kDefaultDisplaySleepSec;
    config.resultDisplaySec = kDefaultResultDisplaySec;
    config.lcdI2cAddress = kDefaultLcdI2cAddress;
    config.beepEnabled = true;

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
    config.startupCompensationMl =
        clampValue<std::uint32_t>(config.startupCompensationMl, 0, kMaxStartupCompensationMl);
    config.pulseTraceMemoryKb =
        clampValue<std::uint32_t>(config.pulseTraceMemoryKb, kMinPulseTraceMemoryKb, kMaxPulseTraceMemoryKb);
    config.overallPulsePerLiter =
        clampValue<std::uint32_t>(config.overallPulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.startupDurationSec =
        clampValue<std::uint32_t>(config.startupDurationSec, 0, kMaxSegmentedStartupDurationSec);
    config.startupPulseCount =
        clampValue<std::uint32_t>(config.startupPulseCount, 0, kMaxSegmentedStartupPulseCount);
    config.startupVolumeMl =
        clampValue<std::uint32_t>(config.startupVolumeMl, 0, kMaxSegmentedStartupVolumeMl);
    config.startupPulsePerLiter =
        clampValue<std::uint32_t>(config.startupPulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.stablePulsePerLiter =
        clampValue<std::uint32_t>(config.stablePulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    if (config.segmentedMeteringCalibrated &&
        (config.startupDurationSec == 0 || config.startupPulseCount == 0 || config.stablePulsePerLiter == 0)) {
        config.segmentedMeteringCalibrated = false;
    }
    config.candidateOverallPulsePerLiter =
        clampValue<std::uint32_t>(config.candidateOverallPulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.candidateStartupDurationSec =
        clampValue<std::uint32_t>(config.candidateStartupDurationSec, 0, kMaxSegmentedStartupDurationSec);
    config.candidateStartupPulseCount =
        clampValue<std::uint32_t>(config.candidateStartupPulseCount, 0, kMaxSegmentedStartupPulseCount);
    config.candidateStartupVolumeMl =
        clampValue<std::uint32_t>(config.candidateStartupVolumeMl, 0, kMaxSegmentedStartupVolumeMl);
    config.candidateStartupPulsePerLiter =
        clampValue<std::uint32_t>(config.candidateStartupPulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.candidateStablePulsePerLiter =
        clampValue<std::uint32_t>(config.candidateStablePulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.candidateSampleCount =
        clampValue<std::uint32_t>(config.candidateSampleCount, 0, kMaxSegmentedCandidateSamples);
    config.candidateMinActualMl =
        clampValue<std::uint32_t>(config.candidateMinActualMl, 0, kMaxVolumePresetMl);
    config.candidateMaxActualMl =
        clampValue<std::uint32_t>(config.candidateMaxActualMl, 0, kMaxVolumePresetMl);
    config.candidateMaxErrorMl =
        clampValue<std::uint32_t>(config.candidateMaxErrorMl, 0, kMaxVolumePresetMl);
    if (config.segmentedCandidateReady &&
        (config.candidateSampleCount < 2 || config.candidateStablePulsePerLiter == 0 ||
         config.candidateStartupDurationSec == 0 || config.candidateStartupPulseCount == 0 ||
         config.candidateMaxActualMl <= config.candidateMinActualMl + 500UL)) {
        config.segmentedCandidateReady = false;
    }
    config.previousOverallPulsePerLiter =
        clampValue<std::uint32_t>(config.previousOverallPulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.previousPulsePerMl = std::isfinite(config.previousPulsePerMl)
                                    ? clampValue<float>(config.previousPulsePerMl, 0.0f, kMaxPulsePerMl)
                                    : 0.0f;
    config.previousStartupCompensationMl =
        clampValue<std::uint32_t>(config.previousStartupCompensationMl, 0, kMaxStartupCompensationMl);
    config.previousStartupDurationSec =
        clampValue<std::uint32_t>(config.previousStartupDurationSec, 0, kMaxSegmentedStartupDurationSec);
    config.previousStartupPulseCount =
        clampValue<std::uint32_t>(config.previousStartupPulseCount, 0, kMaxSegmentedStartupPulseCount);
    config.previousStartupVolumeMl =
        clampValue<std::uint32_t>(config.previousStartupVolumeMl, 0, kMaxSegmentedStartupVolumeMl);
    config.previousStartupPulsePerLiter =
        clampValue<std::uint32_t>(config.previousStartupPulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    config.previousStablePulsePerLiter =
        clampValue<std::uint32_t>(config.previousStablePulsePerLiter, 0, kMaxSegmentedPulsePerLiter);
    if (config.segmentedPreviousReady &&
        (config.previousPulsePerMl <= 0.0f ||
         (config.previousSegmentedMeteringCalibrated &&
          (config.previousStartupDurationSec == 0 || config.previousStartupPulseCount == 0 ||
           config.previousStablePulsePerLiter == 0)))) {
        config.segmentedPreviousReady = false;
    }
    config.pulsePerMl = std::isfinite(config.pulsePerMl)
                            ? clampValue<float>(config.pulsePerMl, kMinPulsePerMl, kMaxPulsePerMl)
                            : kDefaultPulsePerMl;
    config.valveFullPowerSec = clampValue<std::uint32_t>(config.valveFullPowerSec, 1, 10);
    config.valveHoldDutyPercent = clampValue<std::uint8_t>(
        config.valveHoldDutyPercent, kMinValveHoldDutyPercent, kMaxValveHoldDutyPercent);
    config.displaySleepSec = clampValue<std::uint32_t>(config.displaySleepSec, 5, 300);
    config.resultDisplaySec = clampValue<std::uint32_t>(config.resultDisplaySec, 0, 60);
    config.lcdI2cAddress = clampValue<std::uint8_t>(config.lcdI2cAddress, 0x03, 0x77);

    for (auto& preset : config.presets) {
        if (preset.type == PresetType::Volume) {
            preset.value = clampValue<std::uint32_t>(preset.value, kMinVolumePresetMl, kMaxVolumePresetMl);
        } else {
            preset.value = clampValue<std::uint32_t>(preset.value, kMinTimePresetSec, kMaxTimePresetSec);
        }
        preset.name[kPresetNameLength - 1] = '\0';
    }

    for (auto& filter : config.filters) {
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
