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

const char* presetTypeName(PresetType type) {
    return type == PresetType::Time ? "time" : "volume";
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

}  // namespace

bool writeStatusJson(const AppSnapshot& snapshot, char* out, std::size_t len) {
    return writeStatusJson(snapshot, false, out, len);
}

bool writeStatusJson(const AppSnapshot& snapshot, bool screenOn, char* out, std::size_t len) {
    const SystemConfig config = makeDefaultConfig();
    return writeStatusJson(snapshot, screenOn, config, out, len);
}

bool writeStatusJson(const AppSnapshot& snapshot,
                     bool screenOn,
                     const SystemConfig& config,
                     char* out,
                     std::size_t len) {
    return writeStatusJson(snapshot, screenOn, config, nullptr, out, len);
}

bool writeStatusJson(const AppSnapshot& snapshot,
                     bool screenOn,
                     const SystemConfig& config,
                     const ConfigRuntimeStatus* configStatus,
                     char* out,
                     std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"state\":\"%s\",\"valveOpen\":%s,\"volumeMl\":%lu,\"elapsedSec\":%lu,\"targetValue\":%lu,"
                  "\"lastResult\":\"%s\",\"mode\":\"%s\",\"selectedPreset\":%u,\"pulsePerLiter\":%lu,\"flowDroppedPulses\":%lu,"
                  "\"valveDutyPercent\":%u,\"valveFullPowerSec\":%lu,\"valveHoldDutyPercent\":%u,"
                  "\"screenOn\":%s,\"waterControl\":false",
                  waterStateName(snapshot.water.state),
                  snapshot.water.valveOpen ? "true" : "false",
                  static_cast<unsigned long>(snapshot.water.volumeMl),
                  static_cast<unsigned long>(snapshot.water.elapsedSec),
                  static_cast<unsigned long>(snapshot.water.targetValue),
                  waterResultName(snapshot.water.lastResult),
                  waterModeName(snapshot.water.mode),
                  static_cast<unsigned>(snapshot.water.selectedPreset),
                  static_cast<unsigned long>(snapshot.pulsePerLiter),
                  static_cast<unsigned long>(snapshot.flowDroppedPulses),
                  static_cast<unsigned>(snapshot.valve.dutyPercent),
                  static_cast<unsigned long>(config.valveFullPowerSec),
                  static_cast<unsigned>(config.valveHoldDutyPercent),
                  screenOn ? "true" : "false");
    if (configStatus) {
        writer.append(",\"config\":{\"status\":\"%s\",\"rawVersion\":%ld,\"currentVersion\":%ld,"
                      "\"readOnly\":%s,\"migrationWriteBack\":%s}",
                      configStatus->loadStatus ? configStatus->loadStatus : "unknown",
                      static_cast<long>(configStatus->rawVersion),
                      static_cast<long>(configStatus->currentVersion),
                      configStatus->readOnly ? "true" : "false",
                      configStatus->migrationWriteBack ? "true" : "false");
    }
    writer.append("}");
    return writer.ok();
}

bool writeStatsJson(const StatisticsRecord& record, char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"todayMl\":%lu,\"weekMl\":%lu,\"monthMl\":%lu,\"totalMl\":%lu,"
                  "\"dayKey\":%lu,\"weekKey\":%lu,\"monthKey\":%lu}",
                  static_cast<unsigned long>(record.todayMl),
                  static_cast<unsigned long>(record.weekMl),
                  static_cast<unsigned long>(record.monthMl),
                  static_cast<unsigned long>(record.totalMl),
                  static_cast<unsigned long>(record.lastDayKey),
                  static_cast<unsigned long>(record.lastWeekKey),
                  static_cast<unsigned long>(record.lastMonthKey));
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
        writer.append("%s{\"day\":%lu,\"volumeMl\":%lu,\"durationSec\":%lu,\"count\":%u}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned long>(summary.days[i].dayIndex),
                      static_cast<unsigned long>(summary.days[i].volumeMl),
                      static_cast<unsigned long>(summary.days[i].durationSec),
                      static_cast<unsigned>(summary.days[i].count));
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
    writer.append("]}");
    return writer.ok();
}

bool writeConfigJson(const SystemConfig& config, char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"confirmTimeoutSec\":%lu,\"maxOutTimeSec\":%lu,\"maxOutVolumeMl\":%lu,"
                  "\"overflowPercent\":%u,\"noFlowTimeoutSec\":%lu,\"highFlowMlPerMin\":%lu,"
                  "\"highFlowDurationSec\":%lu,\"pauseTimeoutSec\":%lu,\"volumeAdjustStepMl\":%lu,"
                  "\"timeAdjustStepSec\":%lu,\"pulseMinIntervalUs\":%lu,"
                  "\"pulseMaxEffectiveHz\":%lu,\"recentPulseTraceCount\":%lu,"
                  "\"valveFullPowerSec\":%lu,"
                  "\"valveHoldDutyPercent\":%u,\"displaySleepSec\":%lu,\"resultDisplaySec\":%lu,"
                  "\"lcdI2cAddress\":%u,\"beepEnabled\":%s}",
                  static_cast<unsigned long>(config.confirmTimeoutSec),
                  static_cast<unsigned long>(config.maxOutTimeSec),
                  static_cast<unsigned long>(config.maxOutVolumeMl),
                  static_cast<unsigned>(config.overflowPercent),
                  static_cast<unsigned long>(config.noFlowTimeoutSec),
                  static_cast<unsigned long>(config.highFlowMlPerMin),
                  static_cast<unsigned long>(config.highFlowDurationSec),
                  static_cast<unsigned long>(config.pauseTimeoutSec),
                  static_cast<unsigned long>(config.volumeAdjustStepMl),
                  static_cast<unsigned long>(config.timeAdjustStepSec),
                  static_cast<unsigned long>(config.pulseMinIntervalUs),
                  static_cast<unsigned long>(1000000UL / config.pulseMinIntervalUs),
                  static_cast<unsigned long>(config.recentPulseTraceCount),
                  static_cast<unsigned long>(config.valveFullPowerSec),
                  static_cast<unsigned>(config.valveHoldDutyPercent),
                  static_cast<unsigned long>(config.displaySleepSec),
                  static_cast<unsigned long>(config.resultDisplaySec),
                  config.lcdI2cAddress,
                  config.beepEnabled ? "true" : "false");
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
                        char* out,
                        std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"storage\":\"%s\",\"page\":%u,\"pageSize\":%u,\"total\":%u,\"count\":%u,\"records\":[",
                  storageName ? storageName : "unavailable",
                  static_cast<unsigned>(pageIndex),
                  static_cast<unsigned>(pageSize),
                  static_cast<unsigned>(totalCount),
                  static_cast<unsigned>(recordCount));
    for (std::size_t i = 0; i < recordCount; ++i) {
        const WaterRecord& record = records[i];
        const std::uint32_t stablePulsePerLiterAtRun =
            record.pulsePerMlAtRun <= 0.0f
                ? 0
                : static_cast<std::uint32_t>(record.pulsePerMlAtRun * 1000.0f + 0.5f);
        writer.append("%s{\"startTime\":%lu,\"volumeMl\":%lu,\"durationSec\":%u,"
                      "\"mode\":\"%s\",\"result\":\"%s\",\"targetValue\":%lu,\"selectedPreset\":%u,"
                      "\"pulseCount\":%lu,\"rejectedPulseCount\":%lu,\"stablePulsePerLiterAtRun\":%lu}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned long>(record.startTime),
                      static_cast<unsigned long>(record.volumeMl),
                      static_cast<unsigned>(record.durationSec),
                      waterModeName(record.mode),
                      waterResultName(record.result),
                      static_cast<unsigned long>(record.targetValue),
                      static_cast<unsigned>(record.selectedPreset),
                      static_cast<unsigned long>(record.pulseCount),
                      static_cast<unsigned long>(record.rejectedPulseCount),
                      static_cast<unsigned long>(stablePulsePerLiterAtRun));
    }
    writer.append("]}");
    return writer.ok();
}

}  // namespace faucet
