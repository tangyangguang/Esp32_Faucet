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

void setFilter(FilterRecord& filter, bool enabled, const char* name) {
    filter.enabled = enabled;
    filter.recommendDays = 180;
    filter.maxDays = 180;
    filter.lifeMl = 0;
    filter.startTime = 0;
    filter.usedMl = 0;
    filter.startBootId = 0;
    copyText(filter.name, name);
}

void setDefaultMeteringSlot(SystemConfig::MeteringSlot& slot, std::size_t index) {
    slot.valid = true;
    std::snprintf(slot.name, sizeof(slot.name), "参数槽 %u", static_cast<unsigned>(index + 1));
    slot.params = MeteringParameters{0, 0, kDefaultStablePulsePerLiter};
    std::snprintf(slot.creationNote,
                  sizeof(slot.creationNote),
                  "默认参数：启动脉冲数 0P，启动水量 0ml，稳态 P/L %lu。",
                  static_cast<unsigned long>(kDefaultStablePulsePerLiter));
    slot.lastModifiedNote[0] = '\0';
    slot.modifiedAt = 0;
}

void formatCoreParams(char* out, std::size_t len, const MeteringParameters& params) {
    std::snprintf(out,
                  len,
                  "启动脉冲数 %luP，启动水量 %luml，稳态 P/L %lu",
                  static_cast<unsigned long>(params.startupPulseCount),
                  static_cast<unsigned long>(params.startupVolumeMl),
                  static_cast<unsigned long>(params.stablePulsePerLiter));
}

void writeLastModified(SystemConfig::MeteringSlot& slot,
                       const MeteringParameters& before,
                       const MeteringParameters& after,
                       std::uint32_t nowSeconds) {
    char beforeText[80]{};
    char afterText[80]{};
    formatCoreParams(beforeText, sizeof(beforeText), before);
    formatCoreParams(afterText, sizeof(afterText), after);
    std::snprintf(slot.lastModifiedNote,
                  sizeof(slot.lastModifiedNote),
                  "最近修改：%lu；修改前：%s；修改后：%s。",
                  static_cast<unsigned long>(nowSeconds),
                  beforeText,
                  afterText);
    slot.modifiedAt = nowSeconds;
}

}  // namespace

bool validMeteringParameters(const MeteringParameters& params) {
    if (params.stablePulsePerLiter < kMinSegmentedPulsePerLiter ||
        params.stablePulsePerLiter > kMaxSegmentedPulsePerLiter ||
        params.startupPulseCount > kMaxSegmentedStartupPulseCount ||
        params.startupVolumeMl > kMaxSegmentedStartupVolumeMl) {
        return false;
    }
    return (params.startupPulseCount == 0 && params.startupVolumeMl == 0) ||
           (params.startupPulseCount > 0 && params.startupVolumeMl > 0);
}

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
    config.recentPulseTraceCount = kDefaultRecentPulseTraceCount;
    for (std::size_t i = 0; i < kMeteringSlotCount; ++i) {
        setDefaultMeteringSlot(config.meteringSlots[i], i);
    }
    config.meteringCandidate.ready = false;
    config.meteringCandidate.params = MeteringParameters{0, 0, kDefaultStablePulsePerLiter};
    config.meteringCandidate.note[0] = '\0';
    config.meteringCandidate.generatedAt = 0;
    config.activeMeteringSlot = 0;
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
    config.pulseMinIntervalUs =
        clampValue<std::uint32_t>(config.pulseMinIntervalUs, kMinPulseMinIntervalUs, kMaxPulseMinIntervalUs);
    config.recentPulseTraceCount =
        clampValue<std::uint32_t>(config.recentPulseTraceCount, kMinRecentPulseTraceCount, kMaxRecentPulseTraceCount);
    if (config.activeMeteringSlot >= kMeteringSlotCount) {
        config.activeMeteringSlot = 0;
    }
    for (std::size_t i = 0; i < kMeteringSlotCount; ++i) {
        SystemConfig::MeteringSlot& slot = config.meteringSlots[i];
        slot.name[kMeteringSlotNameLength - 1] = '\0';
        slot.creationNote[kMeteringNoteLength - 1] = '\0';
        slot.lastModifiedNote[kMeteringNoteLength - 1] = '\0';
        slot.params.startupPulseCount =
            clampValue<std::uint32_t>(slot.params.startupPulseCount, 0, kMaxSegmentedStartupPulseCount);
        slot.params.startupVolumeMl =
            clampValue<std::uint32_t>(slot.params.startupVolumeMl, 0, kMaxSegmentedStartupVolumeMl);
        slot.params.stablePulsePerLiter =
            clampValue<std::uint32_t>(slot.params.stablePulsePerLiter,
                                      kMinSegmentedPulsePerLiter,
                                      kMaxSegmentedPulsePerLiter);
        if (!slot.valid || !validMeteringParameters(slot.params)) {
            setDefaultMeteringSlot(slot, i);
        }
    }
    if (!config.meteringSlots[config.activeMeteringSlot].valid) {
        config.activeMeteringSlot = 0;
    }
    config.meteringCandidate.note[kMeteringNoteLength - 1] = '\0';
    if (!validMeteringParameters(config.meteringCandidate.params)) {
        config.meteringCandidate.ready = false;
        config.meteringCandidate.params = MeteringParameters{0, 0, kDefaultStablePulsePerLiter};
    }

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

const MeteringParameters& activeMeteringParameters(const SystemConfig& config) {
    const std::uint8_t slot = config.activeMeteringSlot < kMeteringSlotCount ? config.activeMeteringSlot : 0;
    return config.meteringSlots[slot].params;
}

bool enableMeteringSlot(SystemConfig& config, std::uint8_t slot) {
    if (slot >= kMeteringSlotCount || !config.meteringSlots[slot].valid ||
        !validMeteringParameters(config.meteringSlots[slot].params)) {
        return false;
    }
    config.activeMeteringSlot = slot;
    return true;
}

bool saveCandidateToMeteringSlot(SystemConfig& config, std::uint8_t slot, std::uint32_t nowSeconds) {
    if (slot >= kMeteringSlotCount || !config.meteringCandidate.ready ||
        !validMeteringParameters(config.meteringCandidate.params)) {
        return false;
    }
    SystemConfig::MeteringSlot& target = config.meteringSlots[slot];
    const MeteringParameters before = target.params;
    target.valid = true;
    target.params = config.meteringCandidate.params;
    if (target.name[0] == '\0') {
        std::snprintf(target.name, sizeof(target.name), "参数槽 %u", static_cast<unsigned>(slot + 1));
    }
    copyText(target.creationNote, config.meteringCandidate.note);
    if (target.creationNote[0] == '\0') {
        formatCoreParams(target.creationNote, sizeof(target.creationNote), target.params);
    }
    writeLastModified(target, before, target.params, nowSeconds);
    return true;
}

bool createManualMeteringSlot(SystemConfig& config,
                              std::uint8_t slot,
                              const MeteringParameters& params,
                              std::uint32_t nowSeconds) {
    if (slot >= kMeteringSlotCount || !validMeteringParameters(params)) {
        return false;
    }
    SystemConfig::MeteringSlot& target = config.meteringSlots[slot];
    const MeteringParameters before = target.params;
    target.valid = true;
    target.params = params;
    if (target.name[0] == '\0') {
        std::snprintf(target.name, sizeof(target.name), "参数槽 %u", static_cast<unsigned>(slot + 1));
    }
    char paramsText[80]{};
    formatCoreParams(paramsText, sizeof(paramsText), params);
    std::snprintf(target.creationNote, sizeof(target.creationNote), "手工创建：%s。", paramsText);
    writeLastModified(target, before, params, nowSeconds);
    return true;
}

bool updateMeteringSlot(SystemConfig& config,
                        std::uint8_t slot,
                        const MeteringParameters& params,
                        std::uint32_t nowSeconds) {
    if (slot >= kMeteringSlotCount || !config.meteringSlots[slot].valid || !validMeteringParameters(params)) {
        return false;
    }
    SystemConfig::MeteringSlot& target = config.meteringSlots[slot];
    const MeteringParameters before = target.params;
    target.params = params;
    writeLastModified(target, before, params, nowSeconds);
    return true;
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
