#include "web/FaucetWebJson.h"

#include <cstdarg>
#include <cstdio>

namespace faucet {
namespace {

class JsonWriter {
public:
    JsonWriter(char* out, std::size_t len) : out_(out), len_(len), used_(0), ok_(out && len > 0) {
        if (ok_) {
            out_[0] = '\0';
        }
    }

    bool append(const char* fmt, ...) {
        if (!ok_) {
            return false;
        }
        va_list args;
        va_start(args, fmt);
        const int written = std::vsnprintf(out_ + used_, len_ - used_, fmt, args);
        va_end(args);
        if (written < 0 || static_cast<std::size_t>(written) >= len_ - used_) {
            ok_ = false;
            if (len_ > 0) {
                out_[len_ - 1] = '\0';
            }
            return false;
        }
        used_ += static_cast<std::size_t>(written);
        return true;
    }

    bool ok() const {
        return ok_;
    }

private:
    char* out_;
    std::size_t len_;
    std::size_t used_;
    bool ok_;
};

const char* waterStateName(WaterState state) {
    switch (state) {
        case WaterState::Idle:
            return "idle";
        case WaterState::Confirm:
            return "confirm";
        case WaterState::Running:
            return "running";
        case WaterState::Paused:
            return "paused";
        case WaterState::Error:
            return "error";
    }
    return "unknown";
}

const char* waterModeName(WaterMode mode) {
    switch (mode) {
        case WaterMode::Volume:
            return "volume";
        case WaterMode::Time:
            return "time";
    }
    return "unknown";
}

const char* waterResultName(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "completed";
        case WaterResult::StoppedByUser:
            return "stoppedByUser";
        case WaterResult::SafetyStopped:
            return "safetyStopped";
        case WaterResult::FlowError:
            return "flowError";
        case WaterResult::PauseTimeout:
            return "pauseTimeout";
    }
    return "unknown";
}

const char* calibrationStatusName(CalibrationSessionStatus status) {
    switch (status) {
        case CalibrationSessionStatus::Idle:
            return "idle";
        case CalibrationSessionStatus::WaitingLocalRun:
            return "waitingLocalRun";
        case CalibrationSessionStatus::Running:
            return "running";
        case CalibrationSessionStatus::AwaitingActual:
            return "awaitingActual";
        case CalibrationSessionStatus::ReadyToGenerate:
            return "readyToGenerate";
        case CalibrationSessionStatus::Generated:
            return "generated";
        case CalibrationSessionStatus::Applied:
            return "applied";
        case CalibrationSessionStatus::Discarded:
            return "discarded";
        case CalibrationSessionStatus::Failed:
            return "failed";
    }
    return "unknown";
}

const char* presetTypeName(PresetType type) {
    return type == PresetType::Time ? "time" : "volume";
}

WaterMode modeFromPreset(const PresetConfig& preset) {
    return preset.type == PresetType::Time ? WaterMode::Time : WaterMode::Volume;
}

std::size_t enabledPresetCount(const SystemConfig& config) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (config.presets[i].enabled) {
            ++count;
        }
    }
    return count;
}

std::size_t enabledPresetOrdinal(const SystemConfig& config, std::size_t index) {
    if (index >= kPresetCount || !config.presets[index].enabled) {
        return 0;
    }
    std::size_t ordinal = 0;
    for (std::size_t i = 0; i <= index && i < kPresetCount; ++i) {
        if (config.presets[i].enabled) {
            ++ordinal;
        }
    }
    return ordinal;
}

void appendEscaped(JsonWriter& writer, const char* text) {
    writer.append("\"");
    for (const char* p = text ? text : ""; *p; ++p) {
        const unsigned char ch = static_cast<unsigned char>(*p);
        switch (ch) {
            case '"':
            case '\\':
                writer.append("\\%c", ch);
                break;
            case '\b':
                writer.append("\\b");
                break;
            case '\f':
                writer.append("\\f");
                break;
            case '\n':
                writer.append("\\n");
                break;
            case '\r':
                writer.append("\\r");
                break;
            case '\t':
                writer.append("\\t");
                break;
            default:
                if (ch < 0x20) {
                    writer.append("\\u%04x", static_cast<unsigned>(ch));
                } else {
                    writer.append("%c", ch);
                }
                break;
        }
    }
    writer.append("\"");
}

void appendSensorValue(JsonWriter& writer, const SensorValue& value) {
    if (value.valid) {
        writer.append("%ld", static_cast<long>(value.value));
    } else {
        writer.append("null");
    }
}

void appendPresetSummary(JsonWriter& writer,
                         const char* name,
                         const SystemConfig& config,
                         std::size_t index,
                         const MeteringParameters& meteringParams,
                         std::uint32_t estimatedDurationSec,
                         std::uint32_t estimatedVolumeMl,
                         std::uint32_t estimatedPulseCount,
                         float stablePulsePerSec,
                         const char* estimateReason) {
    const bool available = index < kPresetCount && config.presets[index].enabled;
    const std::size_t count = enabledPresetCount(config);
    const std::size_t ordinal = enabledPresetOrdinal(config, index);
    writer.append(",\"%s\":{\"available\":%s,\"index\":%u,\"displayNumber\":%u,\"enabledOrdinal\":%u,\"enabledCount\":%u",
                  name,
                  available ? "true" : "false",
                  available ? static_cast<unsigned>(index) : 0U,
                  available ? static_cast<unsigned>(index + 1U) : 0U,
                  static_cast<unsigned>(ordinal),
                  static_cast<unsigned>(count));
    if (!available) {
        writer.append("}");
        return;
    }

    const PresetConfig& preset = config.presets[index];
    writer.append(",\"name\":");
    appendEscaped(writer, preset.name);
    writer.append(",\"mode\":\"%s\",\"targetValue\":%lu",
                  waterModeName(modeFromPreset(preset)),
                  static_cast<unsigned long>(preset.value));
    const MeteringTargetEstimate volumeEstimate =
        preset.type == PresetType::Volume ? meteringEstimateForTarget(meteringParams, preset.value)
                                          : MeteringTargetEstimate{};
    const bool timeEstimateValid =
        preset.type == PresetType::Time && estimatedVolumeMl > 0 && estimatedPulseCount > 0 && stablePulsePerSec > 0.0f;
    const bool estimateValid = preset.type == PresetType::Volume ? volumeEstimate.valid : timeEstimateValid;
    const std::uint32_t targetMl = preset.type == PresetType::Volume ? volumeEstimate.targetMl : estimatedVolumeMl;
    const std::uint32_t pulseCount =
        preset.type == PresetType::Volume ? volumeEstimate.pulseCount : estimatedPulseCount;
    const std::uint32_t estimateFullRunPulsePerLiter =
        preset.type == PresetType::Volume ? volumeEstimate.fullRunPulsePerLiter : fullRunPulsePerLiter(pulseCount, targetMl);
    writer.append(",\"targetEstimate\":{\"available\":%s,\"targetMl\":%lu,\"pulseCount\":%lu,"
                  "\"fullRunPulsePerLiter\":%lu,\"estimatedDurationSec\":%lu,\"stablePulsePerSec\":%.2f,\"reason\":",
                  estimateValid ? "true" : "false",
                  static_cast<unsigned long>(targetMl),
                  static_cast<unsigned long>(pulseCount),
                  static_cast<unsigned long>(estimateFullRunPulsePerLiter),
                  static_cast<unsigned long>(estimatedDurationSec),
                  static_cast<double>(preset.type == PresetType::Time ? stablePulsePerSec : 0.0f));
    appendEscaped(writer, estimateValid ? "" : (estimateReason ? estimateReason : ""));
    writer.append("}}");
}

}  // namespace

bool writeStatusJson(const AppSnapshot& snapshot,
                     bool screenOn,
                     const SystemConfig& config,
                     const ConfigRuntimeStatus* configStatus,
                     char* out,
                     std::size_t len) {
    JsonWriter writer(out, len);
    const MeteringTargetEstimate volumeTargetEstimate =
        snapshot.water.mode == WaterMode::Volume
            ? meteringEstimateForTarget(snapshot.meteringParams, snapshot.water.targetValue)
            : MeteringTargetEstimate{};
    const bool timeTargetEstimateValid =
        snapshot.water.mode == WaterMode::Time && snapshot.targetEstimatedVolumeMl > 0 &&
        snapshot.targetEstimatedPulseCount > 0 && snapshot.targetStablePulsePerSec > 0.0f;
    const bool targetEstimateValid =
        snapshot.water.mode == WaterMode::Volume ? volumeTargetEstimate.valid : timeTargetEstimateValid;
    const std::uint32_t targetEstimateMl =
        snapshot.water.mode == WaterMode::Volume ? volumeTargetEstimate.targetMl : snapshot.targetEstimatedVolumeMl;
    const std::uint32_t targetEstimatePulses =
        snapshot.water.mode == WaterMode::Volume ? volumeTargetEstimate.pulseCount : snapshot.targetEstimatedPulseCount;
    const std::uint32_t targetEstimateFullRunPulsePerLiter =
        snapshot.water.mode == WaterMode::Volume
            ? volumeTargetEstimate.fullRunPulsePerLiter
            : fullRunPulsePerLiter(targetEstimatePulses, targetEstimateMl);
    writer.append("{\"state\":\"%s\",\"valveOpen\":%s,\"volumeMl\":%lu,\"elapsedSec\":%lu,\"targetValue\":%lu,"
                  "\"lastResult\":\"%s\",\"mode\":\"%s\",\"selectedPreset\":%u,\"activePreset\":%u,\"pulsePerLiter\":%lu,"
                  "\"metering\":{\"startupPulseCount\":%lu,\"startupVolumeMl\":%lu,\"stablePulsePerLiter\":%lu,"
                  "\"startupDurationMs\":%lu,\"stableFlowMlPerMin\":%lu},"
                  "\"calibration\":{\"status\":\"%s\",\"attemptCount\":%u,\"validSampleCount\":%u,"
                  "\"minActualMl\":%lu,\"maxActualMl\":%lu,\"canQuickGenerate\":%s,\"recommended\":%s},"
                  "\"targetEstimate\":{\"available\":%s,\"targetMl\":%lu,\"pulseCount\":%lu,"
                  "\"fullRunPulsePerLiter\":%lu,\"estimatedDurationSec\":%lu,\"stablePulsePerSec\":%.2f,\"reason\":",
                  waterStateName(snapshot.water.state),
                  snapshot.water.valveOpen ? "true" : "false",
                  static_cast<unsigned long>(snapshot.water.volumeMl),
                  static_cast<unsigned long>(snapshot.water.elapsedSec),
                  static_cast<unsigned long>(snapshot.water.targetValue),
                  waterResultName(snapshot.water.lastResult),
                  waterModeName(snapshot.water.mode),
                  static_cast<unsigned>(snapshot.water.selectedPreset),
                  static_cast<unsigned>(snapshot.water.activePreset),
                  static_cast<unsigned long>(snapshot.pulsePerLiter),
                  static_cast<unsigned long>(snapshot.meteringParams.startupPulseCount),
                  static_cast<unsigned long>(snapshot.meteringParams.startupVolumeMl),
                  static_cast<unsigned long>(snapshot.meteringParams.stablePulsePerLiter),
                  static_cast<unsigned long>(snapshot.meteringParams.startupDurationMs),
                  static_cast<unsigned long>(snapshot.meteringParams.stableFlowMlPerMin),
                  calibrationStatusName(snapshot.calibrationStatus),
                  static_cast<unsigned>(snapshot.calibrationAttemptCount),
                  static_cast<unsigned>(snapshot.calibrationValidSampleCount),
                  static_cast<unsigned long>(snapshot.calibrationMinActualMl),
                  static_cast<unsigned long>(snapshot.calibrationMaxActualMl),
                  snapshot.calibrationCanQuickGenerate ? "true" : "false",
                  snapshot.calibrationRecommended ? "true" : "false",
                  targetEstimateValid ? "true" : "false",
                  static_cast<unsigned long>(targetEstimateMl),
                  static_cast<unsigned long>(targetEstimatePulses),
                  static_cast<unsigned long>(targetEstimateFullRunPulsePerLiter),
                  static_cast<unsigned long>(snapshot.targetEstimatedDurationSec),
                  static_cast<double>(snapshot.water.mode == WaterMode::Time ? snapshot.targetStablePulsePerSec : 0.0f));
    appendEscaped(writer, targetEstimateValid ? "" : (snapshot.targetEstimateReason ? snapshot.targetEstimateReason : ""));
    writer.append("},"
                  "\"currentFlowMlPerMin\":%lu,\"instantFlowMlPerMin\":%lu,\"windowFlowMlPerMin\":%lu,"
                  "\"displayFlowMlPerMin\":%lu,\"runAverageFlowMlPerMin\":%lu,"
                  "\"recentAverageFlowMlPerMin\":%lu,\"flowDroppedPulses\":%lu,"
                  "\"maxLoopIntervalUs\":%lu,\"maxAppTickUs\":%lu,\"maxBaseHandleUs\":%lu,"
                  "\"valveDutyPercent\":%u,\"valveFullPowerSec\":%lu,\"valveHoldDutyPercent\":%u,"
                  "\"valvePwmFrequencyHz\":%lu,"
                  "\"screenOn\":%s,\"waterControl\":false",
                  static_cast<unsigned long>(snapshot.currentFlowMlPerMin),
                  static_cast<unsigned long>(snapshot.instantFlowMlPerMin),
                  static_cast<unsigned long>(snapshot.windowFlowMlPerMin),
                  static_cast<unsigned long>(snapshot.displayFlowMlPerMin),
                  static_cast<unsigned long>(snapshot.runAverageFlowMlPerMin),
                  static_cast<unsigned long>(snapshot.recentAverageFlowMlPerMin),
                  static_cast<unsigned long>(snapshot.flowDroppedPulses),
                  static_cast<unsigned long>(snapshot.maxLoopIntervalUs),
                  static_cast<unsigned long>(snapshot.maxAppTickUs),
                  static_cast<unsigned long>(snapshot.maxBaseHandleUs),
                  static_cast<unsigned>(snapshot.valve.dutyPercent),
                  static_cast<unsigned long>(config.valveFullPowerSec),
                  static_cast<unsigned>(config.valveHoldDutyPercent),
                  static_cast<unsigned long>(config.valvePwmFrequencyHz),
                  screenOn ? "true" : "false");
    writer.append(",\"sensor\":{\"inputVoltageMv\":");
    appendSensorValue(writer, snapshot.sensors.inputVoltageMv);
    writer.append(",\"inputVoltageCalibrated\":%s,\"temperature\":{\"enabled\":%s,\"currentCentiC\":",
                  snapshot.sensors.inputVoltageCalibrated ? "true" : "false",
                  snapshot.temperatureSensorEnabled ? "true" : "false");
    appendSensorValue(writer, snapshot.sensors.temperatureCentiC);
    writer.append(",\"calibrated\":%s},\"tds\":{\"enabled\":%s,\"currentPpm\":",
                  config.temperatureCalibrated ? "true" : "false",
                  snapshot.tdsSensorEnabled ? "true" : "false");
    appendSensorValue(writer, snapshot.sensors.tdsPpm);
    writer.append(",\"voltageMv\":");
    appendSensorValue(writer, snapshot.sensors.tdsVoltageMv);
    writer.append(",\"calibrated\":%s,\"temperatureCompensated\":%s,\"tempFallback25C\":%s},\"flags\":%u}",
                  snapshot.sensors.tdsCalibrated ? "true" : "false",
                  snapshot.sensors.tdsTemperatureCompensated ? "true" : "false",
                  snapshot.sensors.tdsTempFallback25C ? "true" : "false",
                  static_cast<unsigned>(snapshot.sensors.flags));
    appendPresetSummary(writer,
                        "nextPreset",
                        config,
                        snapshot.water.selectedPreset,
                        snapshot.meteringParams,
                        snapshot.selectedPresetEstimatedDurationSec,
                        snapshot.selectedPresetEstimatedVolumeMl,
                        snapshot.selectedPresetEstimatedPulseCount,
                        snapshot.selectedPresetStablePulsePerSec,
                        snapshot.selectedPresetEstimateReason);
    appendPresetSummary(writer,
                        "activeTaskPreset",
                        config,
                        snapshot.water.activePreset,
                        snapshot.meteringParams,
                        snapshot.targetEstimatedDurationSec,
                        snapshot.targetEstimatedVolumeMl,
                        snapshot.targetEstimatedPulseCount,
                        snapshot.targetStablePulsePerSec,
                        snapshot.targetEstimateReason);
    if (configStatus) {
        writer.append(",\"config\":{\"status\":\"%s\",\"rawVersion\":%ld,\"currentVersion\":%ld}",
                      configStatus->loadStatus ? configStatus->loadStatus : "unknown",
                      static_cast<long>(configStatus->rawVersion),
                      static_cast<long>(configStatus->currentVersion));
    }
    writer.append("}");
    return writer.ok();
}

bool writeUsageSummaryJson(const WaterUsageSummary& summary,
                           std::uint32_t totalMl,
                           char* out,
                           std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"todayMl\":%lu,\"monthMl\":%lu,\"totalMl\":%lu,"
                  "\"last30DaysMl\":%lu,\"last30DaysDailyAverageMl\":%lu,"
                  "\"unknownCount\":%lu,\"unknownMl\":%lu,\"unknownDurationSec\":%lu,"
                  "\"todayDay\":%lu,\"monthStartDay\":%lu,\"dailySeries\":[",
                  static_cast<unsigned long>(summary.todayMl),
                  static_cast<unsigned long>(summary.monthMl),
                  static_cast<unsigned long>(totalMl),
                  static_cast<unsigned long>(summary.last30DaysMl),
                  static_cast<unsigned long>(summary.last30DaysDailyAverageMl),
                  static_cast<unsigned long>(summary.unknownCount),
                  static_cast<unsigned long>(summary.unknownMl),
                  static_cast<unsigned long>(summary.unknownDurationSec),
                  static_cast<unsigned long>(summary.todayDay),
                  static_cast<unsigned long>(summary.monthStartDay));
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        writer.append("%s{\"day\":%lu,\"volumeMl\":%lu,\"durationSec\":%lu,\"count\":%u,"
                      "\"temperatureAvgCentiC\":%d,\"temperatureMinCentiC\":%d,\"temperatureMaxCentiC\":%d,"
                      "\"tdsAvgPpm\":%u,\"tdsMinPpm\":%u,\"tdsMaxPpm\":%u,"
                      "\"sensorRecordCount\":%u,\"uncalibratedSensorRecordCount\":%u,"
                      "\"temperatureRecordCount\":%u,\"tdsRecordCount\":%u}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned long>(summary.days[i].dayIndex),
                      static_cast<unsigned long>(summary.days[i].volumeMl),
                      static_cast<unsigned long>(summary.days[i].durationSec),
                      static_cast<unsigned>(summary.days[i].count),
                      static_cast<int>(summary.days[i].temperatureAvgCentiC),
                      static_cast<int>(summary.days[i].temperatureMinCentiC),
                      static_cast<int>(summary.days[i].temperatureMaxCentiC),
                      static_cast<unsigned>(summary.days[i].tdsAvgPpm),
                      static_cast<unsigned>(summary.days[i].tdsMinPpm),
                      static_cast<unsigned>(summary.days[i].tdsMaxPpm),
                      static_cast<unsigned>(summary.days[i].sensorRecordCount),
                      static_cast<unsigned>(summary.days[i].uncalibratedSensorRecordCount),
                      static_cast<unsigned>(summary.days[i].temperatureRecordCount),
                      static_cast<unsigned>(summary.days[i].tdsRecordCount));
    }
    writer.append("],\"presetCounts\":[");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        writer.append("%s{\"index\":%u,\"volumeMl\":%lu,\"count\":%u}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned>(i),
                      static_cast<unsigned long>(summary.presetCounts[i].volumeMl),
                      static_cast<unsigned>(summary.presetCounts[i].count));
    }
    writer.append("],\"hourBuckets\":[");
    for (std::size_t i = 0; i < 24; ++i) {
        writer.append("%s{\"hour\":%u,\"volumeMl\":%lu,\"count\":%u}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned>(i),
                      static_cast<unsigned long>(summary.hourBuckets[i].volumeMl),
                      static_cast<unsigned>(summary.hourBuckets[i].count));
    }
    writer.append("],\"resultCounts\":[");
    for (std::size_t i = 0; i < kUsageResultCount; ++i) {
        writer.append("%s%lu", i == 0 ? "" : ",", static_cast<unsigned long>(summary.resultCounts[i]));
    }
    writer.append("],\"volumeHist\":[");
    for (std::size_t i = 0; i < kUsageVolumeHistCount; ++i) {
        writer.append("%s{\"bucket\":%u,\"volumeMl\":%lu,\"count\":%u}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned>(i),
                      static_cast<unsigned long>(summary.volumeHist[i].volumeMl),
                      static_cast<unsigned>(summary.volumeHist[i].count));
    }
    writer.append("],\"sensorRecordCount\":%lu,\"uncalibratedSensorRecordCount\":%lu,\"invalidSensorRecordCount\":%lu}",
                  static_cast<unsigned long>(summary.sensorRecordCount),
                  static_cast<unsigned long>(summary.uncalibratedSensorRecordCount),
                  static_cast<unsigned long>(summary.invalidSensorRecordCount));
    return writer.ok();
}

bool writePresetsJson(const PresetConfig (&presets)[kPresetCount], char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"presets\":[");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        writer.append("%s{\"index\":%u,\"enabled\":%s,\"type\":\"%s\",\"value\":%lu,\"name\":",
                      i == 0 ? "" : ",",
                      static_cast<unsigned>(i),
                      presets[i].enabled ? "true" : "false",
                      presetTypeName(presets[i].type),
                      static_cast<unsigned long>(presets[i].value));
        appendEscaped(writer, presets[i].name);
        writer.append("}");
    }
    writer.append("]}");
    return writer.ok();
}

bool writeFiltersJson(const FilterRecord (&filters)[kFilterCount], char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"filters\":[");
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        writer.append("%s{\"index\":%u,\"enabled\":%s,\"name\":",
                      i == 0 ? "" : ",",
                      static_cast<unsigned>(i),
                      filters[i].enabled ? "true" : "false");
        appendEscaped(writer, filters[i].name);
        writer.append(",\"recommendDays\":%lu,\"maxDays\":%lu,\"lifeMl\":%lu,\"startTime\":%lu,\"usedMl\":%lu}",
                      static_cast<unsigned long>(filters[i].recommendDays),
                      static_cast<unsigned long>(filters[i].maxDays),
                      static_cast<unsigned long>(filters[i].lifeMl),
                      static_cast<unsigned long>(filters[i].startTime),
                      static_cast<unsigned long>(filters[i].usedMl));
    }
    writer.append("]}");
    return writer.ok();
}

bool writeWaterRecordsJson(const WaterRecord* records,
                        std::size_t recordCount,
                        std::size_t pageIndex,
                        std::uint16_t pageSize,
                        std::size_t totalCount,
                        const char* storageName,
                        const char* storageStatus,
                        char* out,
                        std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"storage\":\"%s\",\"storageStatus\":\"%s\",\"page\":%u,\"pageSize\":%u,\"total\":%u,\"count\":%u,\"records\":[",
                  storageName ? storageName : "unavailable",
                  storageStatus ? storageStatus : "unavailable",
                  static_cast<unsigned>(pageIndex),
                  static_cast<unsigned>(pageSize),
                  static_cast<unsigned>(totalCount),
                  static_cast<unsigned>(recordCount));
    for (std::size_t i = 0; i < recordCount; ++i) {
        const WaterRecord& record = records[i];
        const std::uint32_t averageFlowMlPerMin =
            record.durationSec == 0
                ? 0
                : static_cast<std::uint32_t>(
                      (static_cast<std::uint64_t>(record.volumeMl) * 60ULL + record.durationSec / 2ULL) /
                      record.durationSec);
        writer.append("%s{\"startTime\":%lu,\"volumeMl\":%lu,\"durationSec\":%u,"
                      "\"mode\":\"%s\",\"result\":\"%s\",\"targetValue\":%lu,\"selectedPreset\":%u,"
                      "\"pulseCount\":%lu,\"filteredPulseCount\":%lu,\"meteringSchemeId\":%lu,"
                      "\"averageFlowMlPerMin\":%lu,"
                      "\"temperatureCentiC\":%d,\"tdsPpm\":%u,"
                      "\"sensorSampleCount\":%u,\"sensorFlags\":%u}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned long>(record.startTime),
                      static_cast<unsigned long>(record.volumeMl),
                      static_cast<unsigned>(record.durationSec),
                      waterModeName(record.mode),
                      waterResultName(record.result),
                      static_cast<unsigned long>(record.targetValue),
                      static_cast<unsigned>(record.selectedPreset),
                      static_cast<unsigned long>(record.pulseCount),
                      static_cast<unsigned long>(record.filteredPulseCount),
                      static_cast<unsigned long>(record.meteringSchemeId),
                      static_cast<unsigned long>(averageFlowMlPerMin),
                      static_cast<int>(record.temperatureCentiC),
                      static_cast<unsigned>(record.tdsPpm),
                      static_cast<unsigned>(record.sensorSampleCount),
                      static_cast<unsigned>(record.sensorFlags));
    }
    writer.append("]}");
    return writer.ok();
}

}  // namespace faucet
