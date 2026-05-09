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
        case WaterMode::Calibration:
            return "calibration";
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
        if (*p == '"' || *p == '\\') {
            writer.append("\\%c", *p);
        } else if (static_cast<unsigned char>(*p) >= 0x20) {
            writer.append("%c", *p);
        }
    }
    writer.append("\"");
}

}  // namespace

bool writeStatusJson(const AppSnapshot& snapshot, char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"state\":\"%s\",\"valveOpen\":%s,\"volumeMl\":%lu,\"targetValue\":%lu,"
                  "\"mode\":\"%s\",\"selectedPreset\":%u,\"flowDroppedPulses\":%lu,\"waterControl\":false}",
                  waterStateName(snapshot.water.state),
                  snapshot.water.valveOpen ? "true" : "false",
                  static_cast<unsigned long>(snapshot.water.volumeMl),
                  static_cast<unsigned long>(snapshot.water.targetValue),
                  waterModeName(snapshot.water.mode),
                  static_cast<unsigned>(snapshot.water.selectedPreset),
                  static_cast<unsigned long>(snapshot.flowDroppedPulses));
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

bool writeConfigJson(const SystemConfig& config, char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"confirmTimeoutSec\":%lu,\"maxOutTimeSec\":%lu,\"maxOutVolumeMl\":%lu,"
                  "\"overflowPercent\":%u,\"noFlowTimeoutSec\":%lu,\"highFlowMlPerMin\":%lu,"
                  "\"highFlowDurationSec\":%lu,\"pauseTimeoutSec\":%lu,\"pulsePerMl\":%.3f,"
                  "\"calibrationTargetsMl\":[%lu,%lu,%lu,%lu],\"valveFullPowerSec\":%lu,"
                  "\"valveHoldDutyPercent\":%u,\"oledSleepSec\":%lu,\"beepEnabled\":%s}",
                  static_cast<unsigned long>(config.confirmTimeoutSec),
                  static_cast<unsigned long>(config.maxOutTimeSec),
                  static_cast<unsigned long>(config.maxOutVolumeMl),
                  static_cast<unsigned>(config.overflowPercent),
                  static_cast<unsigned long>(config.noFlowTimeoutSec),
                  static_cast<unsigned long>(config.highFlowMlPerMin),
                  static_cast<unsigned long>(config.highFlowDurationSec),
                  static_cast<unsigned long>(config.pauseTimeoutSec),
                  static_cast<double>(config.pulsePerMl),
                  static_cast<unsigned long>(config.calibrationTargetsMl[0]),
                  static_cast<unsigned long>(config.calibrationTargetsMl[1]),
                  static_cast<unsigned long>(config.calibrationTargetsMl[2]),
                  static_cast<unsigned long>(config.calibrationTargetsMl[3]),
                  static_cast<unsigned long>(config.valveFullPowerSec),
                  static_cast<unsigned>(config.valveHoldDutyPercent),
                  static_cast<unsigned long>(config.oledSleepSec),
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

bool writeCalibrationJson(const SystemConfig& config, char* out, std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"pulsePerMl\":%.3f,\"pulsePerLiter\":%.1f,\"targetsMl\":[%lu,%lu,%lu,%lu],"
                  "\"webCanStartCalibration\":false}",
                  static_cast<double>(config.pulsePerMl),
                  static_cast<double>(config.pulsePerMl * 1000.0f),
                  static_cast<unsigned long>(config.calibrationTargetsMl[0]),
                  static_cast<unsigned long>(config.calibrationTargetsMl[1]),
                  static_cast<unsigned long>(config.calibrationTargetsMl[2]),
                  static_cast<unsigned long>(config.calibrationTargetsMl[3]));
    return writer.ok();
}

bool writeWaterLogsJson(const WaterLogRecord* records,
                        std::size_t recordCount,
                        std::size_t pageIndex,
                        std::uint16_t pageSize,
                        std::size_t totalCount,
                        const char* storageName,
                        char* out,
                        std::size_t len) {
    JsonWriter writer(out, len);
    writer.append("{\"storage\":\"%s\",\"page\":%u,\"pageSize\":%u,\"total\":%u,\"count\":%u,\"logs\":[",
                  storageName ? storageName : "unavailable",
                  static_cast<unsigned>(pageIndex),
                  static_cast<unsigned>(pageSize),
                  static_cast<unsigned>(totalCount),
                  static_cast<unsigned>(recordCount));
    for (std::size_t i = 0; i < recordCount; ++i) {
        const WaterLogRecord& record = records[i];
        writer.append("%s{\"startTime\":%lu,\"volumeMl\":%lu,\"durationSec\":%u,"
                      "\"mode\":\"%s\",\"result\":\"%s\"}",
                      i == 0 ? "" : ",",
                      static_cast<unsigned long>(record.startTime),
                      static_cast<unsigned long>(record.volumeMl),
                      static_cast<unsigned>(record.durationSec),
                      waterModeName(record.mode),
                      waterResultName(record.result));
    }
    writer.append("]}");
    return writer.ok();
}

}  // namespace faucet
