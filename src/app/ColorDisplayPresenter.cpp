#include "app/ColorDisplayPresenter.h"

#include "app/MeteringScheme.h"
#include "app/TimeUtils.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

std::uint32_t msFromSeconds(std::uint32_t seconds) {
    return seconds * 1000UL;
}

template <std::size_t N>
void copyText(char (&dest)[N], const char* src) {
    std::strncpy(dest, src ? src : "", N - 1);
    dest[N - 1] = '\0';
}

void clearFrame(ColorDisplayFrame& frame) {
    std::memset(&frame, 0, sizeof(frame));
}

void formatLiters(char* out, std::size_t len, std::uint32_t ml) {
    const std::uint32_t centiliters = (ml + 5UL) / 10UL;
    std::snprintf(out, len, "%lu.%02lu", static_cast<unsigned long>(centiliters / 100UL),
                  static_cast<unsigned long>(centiliters % 100UL));
}

void formatLitersWithUnit(char* out, std::size_t len, std::uint32_t ml) {
    char value[12]{};
    formatLiters(value, sizeof(value), ml);
    std::snprintf(out, len, "%sL", value);
}

void formatElapsed(char* out, std::size_t len, std::uint32_t seconds) {
    std::snprintf(out, len, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60UL),
                  static_cast<unsigned long>(seconds % 60UL));
}

void formatSecondsOrClock(char* out, std::size_t len, std::uint32_t seconds) {
    if (seconds < 100UL) {
        std::snprintf(out, len, "%lu", static_cast<unsigned long>(seconds));
        return;
    }
    formatElapsed(out, len, seconds);
}

void formatEstimatedDuration(char* out, std::size_t len, std::uint32_t seconds) {
    if (seconds < 100UL) {
        std::snprintf(out, len, "%lu秒", static_cast<unsigned long>(seconds));
        return;
    }
    formatElapsed(out, len, seconds);
}

std::uint32_t durationMsToDisplaySec(std::uint32_t durationMs) {
    if (durationMs == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(durationMs) + 500ULL) / 1000ULL);
}

std::uint32_t estimatedDurationSecForVolumeTarget(const AppSnapshot& snapshot) {
    if (snapshot.targetEstimatedDurationSec > 0) {
        return snapshot.targetEstimatedDurationSec;
    }
    return durationMsToDisplaySec(estimateDurationMsForVolumeMl(snapshot.meteringParams, snapshot.water.targetValue));
}

void formatFlowLpm(char* out, std::size_t len, std::uint32_t mlPerMin) {
    const std::uint32_t tenths = (mlPerMin + 50UL) / 100UL;
    std::snprintf(out, len, "%lu.%lu", static_cast<unsigned long>(tenths / 10UL),
                  static_cast<unsigned long>(tenths % 10UL));
}

void formatPresetTag(char* out, std::size_t len, std::size_t presetIndex) {
    std::snprintf(out, len, "P%u", static_cast<unsigned>(presetIndex + 1));
}

void formatPresetTitle(char* out, std::size_t len, std::size_t presetIndex, WaterMode mode) {
    std::snprintf(out,
                  len,
                  "预设%u · %s",
                  static_cast<unsigned>(presetIndex + 1),
                  mode == WaterMode::Volume ? "定量出水" : "定时出水");
}

void formatPresetOnly(char* out, std::size_t len, std::size_t presetIndex) {
    std::snprintf(out, len, "预设%u", static_cast<unsigned>(presetIndex + 1));
}

void formatTemperature(char* out, std::size_t len, const SensorValue& value) {
    if (!value.valid) {
        std::snprintf(out, len, "--");
        return;
    }
    const std::int32_t tenths = value.value >= 0 ? (value.value + 5) / 10 : (value.value - 5) / 10;
    const std::int32_t decimal = tenths % 10 < 0 ? -(tenths % 10) : tenths % 10;
    std::snprintf(out, len, "%ld.%ld", static_cast<long>(tenths / 10), static_cast<long>(decimal));
}

void formatTds(char* out, std::size_t len, const SensorValue& value) {
    if (!value.valid) {
        std::snprintf(out, len, "---");
        return;
    }
    const std::int32_t ppm = value.value < 0 ? 0 : value.value;
    std::snprintf(out, len, "%ld", static_cast<long>(ppm));
}

std::uint16_t progressPermille(std::uint32_t done, std::uint32_t target) {
    if (target == 0) {
        return 0;
    }
    const std::uint32_t value = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(1000ULL, static_cast<std::uint64_t>(done) * 1000ULL / target));
    return static_cast<std::uint16_t>(value);
}

ColorDisplayMetric& addMetric(ColorDisplayFrame& frame, const char* label, const char* value, const char* unit = "") {
    ColorDisplayMetric& metric = frame.metrics[frame.metricCount++];
    copyText(metric.label, label);
    copyText(metric.value, value);
    copyText(metric.unit, unit);
    return metric;
}

ColorDisplaySensor& addSensor(ColorDisplayFrame& frame, const char* label, const char* value, const char* unit) {
    ColorDisplaySensor& sensor = frame.sensors[frame.sensorCount++];
    copyText(sensor.label, label);
    copyText(sensor.value, value);
    copyText(sensor.unit, unit);
    sensor.sampleCount = 0;
    return sensor;
}

void setHints(ColorDisplayFrame& frame, const char* h0, const char* h1, const char* h2 = nullptr) {
    frame.hintCount = h2 ? 3 : 2;
    copyText(frame.hints[0], h0);
    copyText(frame.hints[1], h1);
    if (h2) {
        copyText(frame.hints[2], h2);
    }
}

void addIdleMetrics(ColorDisplayFrame& frame, const AppSnapshot& snapshot) {
    char today[12]{};
    char temp[12]{};
    char tds[12]{};
    formatLitersWithUnit(today, sizeof(today), snapshot.statistics.todayMl);
    formatTemperature(temp, sizeof(temp), snapshot.sensors.temperatureCentiC);
    formatTds(tds, sizeof(tds), snapshot.sensors.tdsPpm);
    addMetric(frame, "今日累计", today);
    addMetric(frame, "水温", temp, "°C");
    addMetric(frame, "TDS", tds);
}

const char* resultState(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "已完成";
        case WaterResult::StoppedByUser:
            return "已停止";
        case WaterResult::PauseTimeout:
            return "暂停超时";
        case WaterResult::SafetyStopped:
            return "安全停止";
        case WaterResult::FlowError:
        default:
            return "异常停止";
    }
}

const char* alertReason(WaterResult result) {
    switch (result) {
        case WaterResult::FlowError:
            return "流量异常";
        case WaterResult::PauseTimeout:
            return "暂停超时";
        case WaterResult::SafetyStopped:
        default:
            return "安全异常";
    }
}

}  // namespace

ColorDisplayPresenter::ColorDisplayPresenter(std::uint32_t sleepTimeoutSec)
    : sleepTimeoutMs_(msFromSeconds(sleepTimeoutSec)),
      lastWakeMs_(0),
      lastWaterState_(WaterState::Idle),
      lastTrendSampleMs_(0),
      tdsTrend_{},
      tempTrend_{},
      trendCount_(0) {}

void ColorDisplayPresenter::configure(std::uint32_t sleepTimeoutSec) {
    sleepTimeoutMs_ = msFromSeconds(sleepTimeoutSec);
    resetTrends();
}

void ColorDisplayPresenter::wake(std::uint32_t nowMs) {
    lastWakeMs_ = nowMs;
}

void ColorDisplayPresenter::resetTrends() {
    lastTrendSampleMs_ = 0;
    trendCount_ = 0;
    std::memset(tdsTrend_, 0, sizeof(tdsTrend_));
    std::memset(tempTrend_, 0, sizeof(tempTrend_));
}

void ColorDisplayPresenter::sampleTrends(const AppSnapshot& snapshot, std::uint32_t nowMs) {
    if (snapshot.water.state != WaterState::Running) {
        resetTrends();
        return;
    }
    if (!snapshot.sensors.tdsPpm.valid || !snapshot.sensors.temperatureCentiC.valid) {
        return;
    }
    if (trendCount_ > 0 && !elapsedAtLeast(nowMs, lastTrendSampleMs_, 1000UL)) {
        return;
    }
    if (trendCount_ == kColorDisplayTrendSamples) {
        for (std::uint8_t i = 1; i < kColorDisplayTrendSamples; ++i) {
            tdsTrend_[i - 1] = tdsTrend_[i];
            tempTrend_[i - 1] = tempTrend_[i];
        }
        trendCount_ = kColorDisplayTrendSamples - 1;
    }
    tdsTrend_[trendCount_] = static_cast<std::uint16_t>(
        snapshot.sensors.tdsPpm.value < 0 ? 0 : std::min<std::int32_t>(65535, snapshot.sensors.tdsPpm.value));
    const std::int32_t tempTenths = snapshot.sensors.temperatureCentiC.value >= 0
                                        ? (snapshot.sensors.temperatureCentiC.value + 5) / 10
                                        : 0;
    tempTrend_[trendCount_] = static_cast<std::uint16_t>(std::min<std::int32_t>(65535, tempTenths));
    ++trendCount_;
    lastTrendSampleMs_ = nowMs;
}

ColorDisplayFrame ColorDisplayPresenter::render(const AppSnapshot& snapshot,
                                                std::uint32_t nowMs,
                                                bool networkOnline) {
    ColorDisplayFrame frame{};
    clearFrame(frame);
    frame.page = ColorDisplayPage::StandbyVolume;
    frame.on = true;

    if (snapshot.water.state != WaterState::Running || lastWaterState_ != WaterState::Running) {
        if (snapshot.water.state != WaterState::Running) {
            resetTrends();
        }
    }
    lastWaterState_ = snapshot.water.state;
    sampleTrends(snapshot, nowMs);

    if (snapshot.localMode == LocalUiMode::RecordCalibration) {
        frame.page = ColorDisplayPage::CalibrationEntry;
        copyText(frame.state, "本地校准");
        copyText(frame.tag, "实测");
        copyText(frame.title, "量杯实测");
        formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.calibrationActualMl);
        copyText(frame.mainUnit, "L");
        char recorded[12]{};
        char step[12]{};
        formatLitersWithUnit(recorded, sizeof(recorded), snapshot.water.volumeMl);
        formatLitersWithUnit(step, sizeof(step), snapshot.calibrationStepMl);
        addMetric(frame, "记录出水", recorded);
        addMetric(frame, "步进", step);
        addMetric(frame, "脉冲有效", snapshot.water.volumeMl > 0 ? "是" : "否");
        char sample[8]{};
        std::snprintf(sample, sizeof(sample), "%u/3", static_cast<unsigned>(snapshot.calibrationValidSampleCount + 1));
        addMetric(frame, "样本", sample);
        setHints(frame, "加减", "保存", "放弃");
        return frame;
    }

    if (snapshot.localMode == LocalUiMode::Calibration) {
        frame.page = ColorDisplayPage::CalibrationReady;
        copyText(frame.state, "本地校准");
        char tag[8]{};
        std::snprintf(tag, sizeof(tag), "%u/3", static_cast<unsigned>(snapshot.calibrationValidSampleCount));
        copyText(frame.tag, tag);
        copyText(frame.title, "校准出水");
        formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.targetValue);
        copyText(frame.mainUnit, "L");
        char valid[12]{};
        std::snprintf(valid, sizeof(valid), "%u条", static_cast<unsigned>(snapshot.calibrationValidSampleCount));
        addMetric(frame, "有效样本", valid);
        addMetric(frame, "建议样本", "3条");
        char tds[12]{};
        char temp[12]{};
        formatTds(tds, sizeof(tds), snapshot.sensors.tdsPpm);
        formatTemperature(temp, sizeof(temp), snapshot.sensors.temperatureCentiC);
        addMetric(frame, "TDS", tds);
        addMetric(frame, "水温", temp, "°C");
        setHints(frame, "出水", "退出", "本地");
        return frame;
    }

    if (snapshot.localMode == LocalUiMode::Result) {
        frame.page = snapshot.water.lastResult == WaterResult::Completed ? ColorDisplayPage::ResultCompleted
                                                                         : ColorDisplayPage::ResultStopped;
        copyText(frame.state, resultState(snapshot.water.lastResult));
        copyText(frame.tag, "结果");
        copyText(frame.title, "本次出水");
        formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.volumeMl);
        copyText(frame.mainUnit, "L");
        char value[16]{};
        if (snapshot.water.lastResult == WaterResult::Completed) {
            char elapsed[12]{};
            char flow[12]{};
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            formatFlowLpm(flow, sizeof(flow), snapshot.runAverageFlowMlPerMin);
            addMetric(frame, "用时", elapsed);
            addMetric(frame, "均流速", flow, "L/min");
        } else {
            formatLitersWithUnit(value, sizeof(value), snapshot.water.targetValue);
            addMetric(frame, "目标", value);
            char elapsed[12]{};
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            addMetric(frame, "用时", elapsed);
        }
        char tds[12]{};
        char temp[12]{};
        const bool hasRecordSensors =
            snapshot.lastResultRecordAvailable && snapshot.lastResultRecord.sensorSampleCount > 0;
        SensorValue resultTds{};
        resultTds.valid = hasRecordSensors;
        resultTds.value = static_cast<std::int32_t>(snapshot.lastResultRecord.tdsAvgPpm);
        SensorValue resultTemp{};
        resultTemp.valid = hasRecordSensors;
        resultTemp.value = static_cast<std::int32_t>(snapshot.lastResultRecord.temperatureAvgCentiC);
        formatTds(tds, sizeof(tds), hasRecordSensors ? resultTds : snapshot.sensors.tdsPpm);
        formatTemperature(temp, sizeof(temp), hasRecordSensors ? resultTemp : snapshot.sensors.temperatureCentiC);
        addMetric(frame, "均TDS", tds);
        addMetric(frame, "均水温", temp, "°C");
        setHints(frame, "返回", "校准", "30s");
        return frame;
    }

    if (sleepTimeoutMs_ > 0 && elapsedAtLeast(nowMs, lastWakeMs_, sleepTimeoutMs_) &&
        snapshot.water.state == WaterState::Idle) {
        frame.page = ColorDisplayPage::Sleep;
        frame.on = false;
        copyText(frame.subtitle, "按键唤醒");
        return frame;
    }

    switch (snapshot.water.state) {
        case WaterState::Idle: {
            frame.page = networkOnline ? (snapshot.water.mode == WaterMode::Volume ? ColorDisplayPage::StandbyVolume
                                                                                   : ColorDisplayPage::StandbyTime)
                                       : ColorDisplayPage::StandbyOffline;
            copyText(frame.state, "待机");
            char presetTitle[32]{};
            char presetTag[8]{};
            formatPresetTitle(presetTitle, sizeof(presetTitle), snapshot.water.selectedPreset, snapshot.water.mode);
            formatPresetTag(presetTag, sizeof(presetTag), snapshot.water.selectedPreset);
            copyText(frame.tag, networkOnline ? presetTag : "离线");
            copyText(frame.title, presetTitle);
            if (snapshot.water.mode == WaterMode::Volume) {
                formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.targetValue);
                copyText(frame.mainUnit, "L");
                copyText(frame.subtitle, networkOnline ? "加/减切换 · 确认进入" : "本地可用 · 确认进入");
            } else {
                formatSecondsOrClock(frame.mainValue, sizeof(frame.mainValue), snapshot.water.targetValue);
                copyText(frame.mainUnit, "秒");
                char estimate[32]{};
                char liters[12]{};
                formatLitersWithUnit(liters, sizeof(liters), snapshot.selectedPresetEstimatedVolumeMl);
                std::snprintf(estimate, sizeof(estimate), "预计出水约 %s", liters);
                copyText(frame.subtitle, estimate);
            }
            addIdleMetrics(frame, snapshot);
            return frame;
        }

        case WaterState::Confirm:
            frame.page = snapshot.water.mode == WaterMode::Volume ? ColorDisplayPage::ConfirmVolume
                                                                  : ColorDisplayPage::ConfirmTime;
            copyText(frame.state, "确认出水");
            copyText(frame.tag, "60s");
            copyText(frame.title, snapshot.water.mode == WaterMode::Volume ? "本次目标" : "本次时长");
            if (snapshot.water.mode == WaterMode::Volume) {
                formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.targetValue);
                copyText(frame.mainUnit, "L");
                char subtitle[32]{};
                const std::uint32_t estimateSec = estimatedDurationSecForVolumeTarget(snapshot);
                if (estimateSec > 0) {
                    char duration[12]{};
                    formatEstimatedDuration(duration, sizeof(duration), estimateSec);
                    std::snprintf(subtitle,
                                  sizeof(subtitle),
                                  "预设%u · 约%s",
                                  static_cast<unsigned>(snapshot.water.activePreset + 1),
                                  duration);
                } else {
                    formatPresetOnly(subtitle, sizeof(subtitle), snapshot.water.activePreset);
                }
                copyText(frame.subtitle, subtitle);
                setHints(frame, "确认", "加减", "取消");
            } else {
                formatElapsed(frame.mainValue, sizeof(frame.mainValue), snapshot.water.targetValue);
                char subtitle[32]{};
                if (snapshot.targetEstimatedVolumeMl > 0 && snapshot.targetEstimatedPulseCount > 0 &&
                    snapshot.targetStablePulsePerSec > 0.0f) {
                    char liters[12]{};
                    formatLitersWithUnit(liters, sizeof(liters), snapshot.targetEstimatedVolumeMl);
                    std::snprintf(subtitle,
                                  sizeof(subtitle),
                                  "预设%u · 约%s",
                                  static_cast<unsigned>(snapshot.water.activePreset + 1),
                                  liters);
                } else {
                    formatPresetOnly(subtitle, sizeof(subtitle), snapshot.water.activePreset);
                }
                copyText(frame.subtitle, subtitle);
                setHints(frame, "确认", "加减", "取消");
            }
            return frame;

        case WaterState::Running: {
            frame.page = snapshot.water.mode == WaterMode::Volume ? ColorDisplayPage::RunningVolume
                                                                  : ColorDisplayPage::RunningTime;
            copyText(frame.state, snapshot.water.mode == WaterMode::Volume ? "出水中" : "定时出水");
            char tag[16]{};
            char elapsed[12]{};
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            std::snprintf(tag,
                          sizeof(tag),
                          "P%u · %s",
                          static_cast<unsigned>(snapshot.water.activePreset + 1),
                          elapsed);
            copyText(frame.tag, tag);
            const std::uint32_t remainVolume =
                snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0;
            const std::uint32_t remainSec =
                snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0;
            if (snapshot.water.mode == WaterMode::Volume) {
                copyText(frame.title, "已出水");
                formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.volumeMl);
                copyText(frame.mainUnit, "L");
                frame.progressPermille = progressPermille(snapshot.water.volumeMl, snapshot.water.targetValue);
                char remain[12]{};
                formatLiters(remain, sizeof(remain), remainVolume);
                addMetric(frame, "剩余水量", remain, "L");
            } else {
                copyText(frame.title, "剩余时间");
                formatElapsed(frame.mainValue, sizeof(frame.mainValue), remainSec);
                frame.progressPermille = progressPermille(snapshot.water.elapsedSec, snapshot.water.targetValue);
                char out[12]{};
                formatLiters(out, sizeof(out), snapshot.water.volumeMl);
                addMetric(frame, "已出水", out, "L");
            }
            char flow[12]{};
            formatFlowLpm(flow, sizeof(flow), snapshot.displayFlowMlPerMin);
            addMetric(frame, "实时流速", flow, "L/min");
            if (snapshot.water.mode == WaterMode::Volume) {
                char eta[12]{};
                const std::uint32_t etaSec =
                    snapshot.displayFlowMlPerMin == 0
                        ? 0
                        : static_cast<std::uint32_t>(
                              (static_cast<std::uint64_t>(remainVolume) * 60ULL + snapshot.displayFlowMlPerMin / 2ULL) /
                              snapshot.displayFlowMlPerMin);
                formatElapsed(eta, sizeof(eta), etaSec);
                addMetric(frame, "预计完成", eta);
            } else {
                char total[12]{};
                const std::uint32_t estimatedTotal =
                    snapshot.targetEstimatedVolumeMl > 0 ? snapshot.targetEstimatedVolumeMl : snapshot.water.volumeMl;
                formatLiters(total, sizeof(total), estimatedTotal);
                addMetric(frame, "预计总量", total, "L");
            }
            char tds[12]{};
            char temp[12]{};
            formatTds(tds, sizeof(tds), snapshot.sensors.tdsPpm);
            formatTemperature(temp, sizeof(temp), snapshot.sensors.temperatureCentiC);
            ColorDisplaySensor& tdsSensor = addSensor(frame, "TDS · 近30秒", tds, "ppm");
            ColorDisplaySensor& tempSensor = addSensor(frame, "水温", temp, "°C");
            tdsSensor.sampleCount = trendCount_;
            tempSensor.sampleCount = trendCount_;
            for (std::uint8_t i = 0; i < trendCount_; ++i) {
                tdsSensor.samples[i] = tdsTrend_[i];
                tempSensor.samples[i] = tempTrend_[i];
            }
            return frame;
        }

        case WaterState::Paused: {
            frame.page = snapshot.water.mode == WaterMode::Volume ? ColorDisplayPage::PausedVolume
                                                                  : ColorDisplayPage::PausedTime;
            copyText(frame.state, "暂停");
            copyText(frame.tag, "阀关");
            copyText(frame.title, "已暂停");
            copyText(frame.status, "阀门已关闭");
            char tds[12]{};
            char temp[12]{};
            formatTds(tds, sizeof(tds), snapshot.sensors.tdsPpm);
            formatTemperature(temp, sizeof(temp), snapshot.sensors.temperatureCentiC);
            std::snprintf(frame.subtitle, sizeof(frame.subtitle), "TDS %s · %s°C", tds, temp);
            char out[12]{};
            char remain[12]{};
            char elapsed[12]{};
            formatLitersWithUnit(out, sizeof(out), snapshot.water.volumeMl);
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            if (snapshot.water.mode == WaterMode::Volume) {
                const std::uint32_t remainVolume =
                    snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0;
                formatLitersWithUnit(remain, sizeof(remain), remainVolume);
                addMetric(frame, "已出水", out);
                addMetric(frame, "剩余", remain);
                addMetric(frame, "已用", elapsed);
                char eta[12]{};
                const std::uint32_t etaFlow =
                    snapshot.displayFlowMlPerMin > 0 ? snapshot.displayFlowMlPerMin : snapshot.meteringParams.stableFlowMlPerMin;
                const std::uint32_t etaSec =
                    etaFlow == 0
                        ? 0
                        : static_cast<std::uint32_t>((static_cast<std::uint64_t>(remainVolume) * 60ULL + etaFlow / 2ULL) /
                                                     etaFlow);
                formatElapsed(eta, sizeof(eta), etaSec);
                addMetric(frame, "还需约", eta);
            } else {
                const std::uint32_t remainSec =
                    snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0;
                formatElapsed(remain, sizeof(remain), remainSec);
                addMetric(frame, "剩余时间", remain);
                addMetric(frame, "已出水", out);
                addMetric(frame, "已用", elapsed);
                char total[12]{};
                formatLitersWithUnit(total, sizeof(total), snapshot.targetEstimatedVolumeMl);
                addMetric(frame, "预计总量", total);
            }
            setHints(frame, "继续", "结束");
            return frame;
        }

        case WaterState::Error:
        default:
            frame.page = ColorDisplayPage::Alert;
            copyText(frame.state, "异常停止");
            copyText(frame.tag, "阀关");
            copyText(frame.title, "已关阀");
            copyText(frame.status, alertReason(snapshot.water.lastResult));
            char out[12]{};
            char target[12]{};
            formatLitersWithUnit(out, sizeof(out), snapshot.water.volumeMl);
            formatLitersWithUnit(target, sizeof(target), snapshot.water.targetValue);
            addMetric(frame, "本次出水", out);
            addMetric(frame, "目标", target);
            setHints(frame, "返回", "待机", "排查");
            return frame;
    }
}

}  // namespace faucet
