#include "app/ColorDisplayPresenter.h"

#include "app/MeteringScheme.h"
#include "app/TimeUtils.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kVolumeEstimateWarmupMs = 2000;
constexpr std::uint32_t kVolumeEstimateIntervalMs = 1000;
constexpr std::uint32_t kVolumeEstimateToleranceMs = 2000;

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

std::uint32_t ceilDurationSec(std::uint32_t durationMs) {
    return durationMs / 1000U + (durationMs % 1000U != 0);
}

std::uint32_t estimatedDurationSecForVolumeTarget(const AppSnapshot& snapshot) {
    if (snapshot.targetEstimatedDurationSec > 0) {
        return snapshot.targetEstimatedDurationSec;
    }
    return durationMsToDisplaySec(estimateDurationMsForVolumeMl(snapshot.meteringParams, snapshot.water.targetValue));
}

void formatFlowLpm(char* out, std::size_t len, std::uint32_t mlPerMin) {
    const std::uint32_t hundredths = (mlPerMin + 5UL) / 10UL;
    std::snprintf(out, len, "%lu.%02lu", static_cast<unsigned long>(hundredths / 100UL),
                  static_cast<unsigned long>(hundredths % 100UL));
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
    const bool negative = value.value < 0;
    const std::int32_t absolute = negative ? -value.value : value.value;
    const std::int32_t tenths = (absolute + 5) / 10;
    std::snprintf(out,
                  len,
                  "%s%ld.%ld",
                  negative ? "-" : "",
                  static_cast<long>(tenths / 10),
                  static_cast<long>(tenths % 10));
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
    return sensor;
}

std::uint16_t encodeTrendValue(std::int32_t value, bool temperature) {
    if (!temperature) {
        return static_cast<std::uint16_t>(value);
    }
    const auto tenths = (value + (value < 0 ? -5 : 5)) / 10;
    return static_cast<std::uint16_t>(tenths + 32768);
}

void setHints(ColorDisplayFrame& frame, const char* h0, const char* h1 = nullptr, const char* h2 = nullptr) {
    frame.hintCount = h2 ? 3 : (h1 ? 2 : 1);
    copyText(frame.hints[0], h0);
    if (h1) {
        copyText(frame.hints[1], h1);
    }
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
    : sleepTimeoutMs_(secondsToMillis(sleepTimeoutSec)),
      lastWakeMs_(0) {}

void ColorDisplayPresenter::configure(std::uint32_t sleepTimeoutSec) {
    sleepTimeoutMs_ = secondsToMillis(sleepTimeoutSec);
}

void ColorDisplayPresenter::wake(std::uint32_t nowMs) {
    lastWakeMs_ = nowMs;
}

void ColorDisplayPresenter::Range::include(const Range& other) {
    if (other.valid()) {
        low = std::min(low, other.low);
        high = std::max(high, other.high);
    }
}

void ColorDisplayPresenter::resetTrends() {
    secondsPerBucket_ = 1;
    lastSampleSec_ = 0;
    bucketCount_ = 0;
    for (std::size_t i = 0; i < kColorDisplaySensorCount; ++i) {
        extrema_[i] = {};
        latest_[i] = {};
    }
}

void ColorDisplayPresenter::observe(const AppSnapshot& snapshot) {
    observeVolumeEstimate(snapshot);
    const WaterState state = snapshot.localMode == LocalUiMode::Calibration ? WaterState::Idle : snapshot.water.state;
    const bool newRun = state == WaterState::Running &&
                        lastWaterState_ != WaterState::Running && lastWaterState_ != WaterState::Paused;
    if (newRun) {
        resetTrends();
    } else if (state == WaterState::Running && snapshot.sensors.sampleSequence != observedSequence_) {
        appendTrend(snapshot.water.elapsedSec, snapshot.sensors);
    }
    lastWaterState_ = state;
    observedSequence_ = snapshot.sensors.sampleSequence;
}

void ColorDisplayPresenter::observeVolumeEstimate(const AppSnapshot& snapshot) {
    const auto& water = snapshot.water;
    const auto flow = snapshot.displayFlowMlPerMin;
    if (snapshot.localMode != LocalUiMode::Normal || water.state != WaterState::Running ||
        water.mode != WaterMode::Volume || water.volumeMl >= water.targetValue || flow == 0) {
        nextVolumeEstimateMs_ = 0;
        volumeFinishEstimateMs_ = 0;
        volumeEstimateDirection_ = 0;
        return;
    }

    // Use active run time so the countdown shares the controller's time base.
    const auto elapsedMs = water.elapsedMs;
    if (nextVolumeEstimateMs_ == 0) {
        nextVolumeEstimateMs_ = elapsedMs + kVolumeEstimateWarmupMs;
        return;
    }
    const bool expired = volumeFinishEstimateMs_ != 0 && elapsedMs >= volumeFinishEstimateMs_;
    if (!expired && elapsedMs < nextVolumeEstimateMs_) {
        return;
    }
    nextVolumeEstimateMs_ = elapsedMs + kVolumeEstimateIntervalMs;

    const auto remainingMl = water.targetValue - water.volumeMl;
    const auto candidateMs = elapsedMs + (static_cast<std::uint64_t>(remainingMl) * 60000U + flow - 1U) / flow;
    if (candidateMs > secondsToMillis(snapshot.maxOutTimeSec)) {
        volumeFinishEstimateMs_ = 0;
        volumeEstimateDirection_ = 0;
        return;
    }
    if (volumeFinishEstimateMs_ == 0 || expired) {
        volumeFinishEstimateMs_ = static_cast<std::uint32_t>(candidateMs);
        volumeEstimateDirection_ = 0;
        return;
    }

    // Ignore small changes and require two consecutive larger changes in the same direction.
    const auto errorMs = static_cast<std::int64_t>(candidateMs) - volumeFinishEstimateMs_;
    const std::int64_t toleranceMs = kVolumeEstimateToleranceMs;
    const std::int8_t direction = errorMs > toleranceMs ? 1 : errorMs < -toleranceMs ? -1 : 0;
    if (direction != 0 && direction == volumeEstimateDirection_) {
        volumeFinishEstimateMs_ = static_cast<std::uint32_t>(candidateMs);
        volumeEstimateDirection_ = 0;
    } else {
        volumeEstimateDirection_ = direction;
    }
}

void ColorDisplayPresenter::mergeTrendBuckets() {
    for (std::size_t read = 0, write = 0; read < bucketCount_; read += 2U, ++write) {
        Bucket merged = buckets_[read];
        if (read + 1U < bucketCount_) {
            const Bucket& next = buckets_[read + 1U];
            merged.gaps |= next.gaps;
            for (std::size_t i = 0; i < kColorDisplaySensorCount; ++i) {
                merged.values[i].include(next.values[i]);
            }
        }
        buckets_[write] = merged;
    }
    bucketCount_ = (bucketCount_ + 1U) / 2U;
    secondsPerBucket_ *= 2U;
}

void ColorDisplayPresenter::appendTrend(std::uint32_t elapsedSec, const WaterSensorSnapshot& sensors) {
    while (elapsedSec / secondsPerBucket_ >= kTrendBuckets) {
        mergeTrendBuckets();
    }
    const std::size_t index = elapsedSec / secondsPerBucket_;
    // Unobserved seconds stay empty and force a break when compressed.
    const bool missingSeconds = (bucketCount_ > 0 && elapsedSec > lastSampleSec_ + 1U) ||
                                (bucketCount_ == 0 && elapsedSec > 1U);
    constexpr std::uint8_t kBothChannels = (1U << kColorDisplaySensorCount) - 1U;
    while (bucketCount_ <= index) {
        Bucket& bucket = buckets_[bucketCount_++];
        bucket = {};
        if (bucketCount_ - 1U < index) {
            bucket.gaps = kBothChannels;
        }
    }
    Bucket& bucket = buckets_[index];
    if (missingSeconds) {
        bucket.gaps |= kBothChannels;
    }
    latest_[0] = sensors.temperatureCentiC;
    latest_[1] = sensors.tdsPpm;
    for (std::size_t i = 0; i < kColorDisplaySensorCount; ++i) {
        if (!latest_[i].valid) {
            bucket.gaps |= 1U << i;
            continue;
        }
        const auto encoded = encodeTrendValue(latest_[i].value, i == 0);
        Range point;
        point.low = point.high = encoded;
        bucket.values[i].include(point);
        extrema_[i].include(point);
    }
    lastSampleSec_ = elapsedSec;
}

void ColorDisplayPresenter::renderTrends(ColorDisplayFrame& frame, std::uint32_t elapsedSec) const {
    formatElapsed(frame.trendTime, sizeof(frame.trendTime), elapsedSec);
    const std::uint32_t axisEnd = std::max(elapsedSec, std::max(lastSampleSec_, 1U));
    const auto columnAt = [axisEnd](std::uint32_t sec) {
        return static_cast<std::uint8_t>(sec * (kColorDisplayTrendColumns - 1U) / axisEnd);
    };
    for (std::size_t channel = 0; channel < kColorDisplaySensorCount; ++channel) {
        const bool temperature = channel == 0;
        const auto format = temperature ? formatTemperature : formatTds;
        ColorDisplaySensor& sensor = addSensor(frame, temperature ? "水温" : "TDS", "", temperature ? "°C" : "ppm");
        format(sensor.value, sizeof(sensor.value), latest_[channel]);
        const Range& range = extrema_[channel];
        const std::int32_t offset = temperature ? 32768 : 0;
        const std::int32_t scale = temperature ? 10 : 1;
        SensorValue extreme;
        extreme.valid = range.valid();
        extreme.value = (range.low - offset) * scale;
        format(sensor.low, sizeof(sensor.low), extreme);
        extreme.value = (range.high - offset) * scale;
        format(sensor.high, sizeof(sensor.high), extreme);
        if (!range.valid()) {
            continue;
        }
        // A stable sensor gets a flat line. Small changes use a minimum
        // vertical scale so quantization noise does not fill the whole chart.
        const auto span = std::max<int>(range.high - range.low, 10);
        const auto plotTop = (range.high + range.low + span) / 2;
        const auto yAt = [plotTop, span](std::int32_t value) {
            return static_cast<std::uint8_t>(1 + (plotTop - value) * (kColorDisplayTrendHeight - 1) / span);
        };
        const std::uint8_t gapMask = 1U << channel;
        for (std::size_t index = 0; index < bucketCount_; ++index) {
            const Bucket& bucket = buckets_[index];
            const auto bucketEnd = static_cast<std::uint32_t>((index + 1U) * secondsPerBucket_ - 1U);
            auto& column = sensor.columns[columnAt(std::min(bucketEnd, lastSampleSec_))];
            const Range& values = bucket.values[channel];
            std::uint8_t top = column.top & ~kColorDisplayTrendGap;
            const std::uint8_t gap = (bucket.gaps & gapMask) ? kColorDisplayTrendGap : 0U;
            if (values.valid()) {
                const std::uint8_t y = yAt(values.high);
                top = top == kColorDisplayTrendEmpty ? y : std::min(top, y);
                column.bottom = std::max(column.bottom, yAt(values.low));
            }
            column.top = top | gap | (column.top & kColorDisplayTrendGap);
        }
        if (latest_[channel].valid) {
            sensor.latestColumn = columnAt(lastSampleSec_);
            sensor.latestY = yAt(encodeTrendValue(latest_[channel].value, temperature));
        }
    }
}

void ColorDisplayPresenter::renderWaterPage(ColorDisplayFrame& frame, const AppSnapshot& snapshot) const {
    const bool result = snapshot.localMode == LocalUiMode::Result;
    const bool paused = snapshot.water.state == WaterState::Paused;
    const auto& finished = snapshot.lastDisplayResult;
    const WaterMode mode = result ? finished.mode : snapshot.water.mode;
    const bool byVolume = mode == WaterMode::Volume;
    const auto target = result ? finished.targetValue : snapshot.water.targetValue;
    const auto volume = result ? finished.volumeMl : snapshot.water.volumeMl;
    const auto elapsed = result ? finished.durationSec : snapshot.water.elapsedSec;
    const bool completed = result && finished.result == WaterResult::Completed;
    const bool abnormal = result && !completed && finished.result != WaterResult::StoppedByUser;

    frame.progressPermille = completed ? 1000U : std::min<std::uint16_t>(
        990U, progressPermille(byVolume ? volume : elapsed, target));
    copyText(frame.title, result ? "本次出水" : "已出水");
    formatLiters(frame.mainValue, sizeof(frame.mainValue), volume);
    copyText(frame.mainUnit, "L");
    char goal[12]{};
    if (byVolume) {
        formatLiters(goal, sizeof(goal), target);
        std::snprintf(frame.tag, sizeof(frame.tag), "定量 %s L", goal);
    } else {
        formatElapsed(goal, sizeof(goal), target);
        std::snprintf(frame.tag, sizeof(frame.tag), "定时 %s", goal);
    }

    if (result) {
        frame.page = completed ? ColorDisplayPage::ResultCompleted : abnormal ? ColorDisplayPage::ResultAlert
                                                                              : ColorDisplayPage::ResultStopped;
        copyText(frame.state, abnormal ? alertReason(finished.result) : resultState(finished.result));
        copyText(frame.status, abnormal ? "阀关" : "用时");
        formatElapsed(frame.timer, sizeof(frame.timer), elapsed);
        setHints(frame, "取消 返回");
    } else if (paused) {
        frame.page = byVolume ? ColorDisplayPage::PausedVolume : ColorDisplayPage::PausedTime;
        copyText(frame.state, "已暂停");
        copyText(frame.status, "阀关");
        formatElapsed(frame.timer, sizeof(frame.timer), elapsed);
        setHints(frame, "确认 继续", "取消 停止");
    } else {
        frame.page = byVolume ? ColorDisplayPage::RunningVolume : ColorDisplayPage::RunningTime;
        copyText(frame.state, "出水中");
        copyText(frame.status, byVolume ? "约剩" : "剩余");
        if (!byVolume) {
            formatElapsed(frame.timer, sizeof(frame.timer), target > elapsed ? target - elapsed : 0U);
        } else if (volumeFinishEstimateMs_ > snapshot.water.elapsedMs) {
            formatElapsed(frame.timer, sizeof(frame.timer),
                          ceilDurationSec(volumeFinishEstimateMs_ - snapshot.water.elapsedMs));
            formatElapsed(goal, sizeof(goal), ceilDurationSec(volumeFinishEstimateMs_));
            std::snprintf(frame.subtitle, sizeof(frame.subtitle), "总计约 %s", goal);
        }
        if (!frame.timer[0]) {
            copyText(frame.timer, "--");
        }
    }
    renderTrends(frame, elapsed);
}

ColorDisplayFrame ColorDisplayPresenter::render(const AppSnapshot& snapshot,
                                                std::uint32_t nowMs,
                                                bool networkOnline) const {
    ColorDisplayFrame frame{};
    clearFrame(frame);
    frame.page = ColorDisplayPage::StandbyVolume;
    frame.on = true;

    if (snapshot.localMode == LocalUiMode::Calibration) {
        frame.page = ColorDisplayPage::CalibrationReady;
        copyText(frame.state, "校准");
        char tag[8]{};
        std::snprintf(tag, sizeof(tag), "%u/3", static_cast<unsigned>(snapshot.calibrationValidSampleCount));
        copyText(frame.tag, tag);
        if (snapshot.calibrationStatus == CalibrationSessionStatus::Running ||
            snapshot.water.state == WaterState::Running) {
            copyText(frame.tag, "出水中");
            copyText(frame.title, "校准出水中");
            formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.volumeMl);
            copyText(frame.mainUnit, "L");
            char elapsed[12]{};
            char remaining[12]{};
            char flow[12]{};
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            const std::uint32_t remainingSec =
                snapshot.maxOutTimeSec > snapshot.water.elapsedSec
                    ? snapshot.maxOutTimeSec - snapshot.water.elapsedSec
                    : 0;
            formatElapsed(remaining, sizeof(remaining), remainingSec);
            formatFlowLpm(flow, sizeof(flow), snapshot.displayFlowMlPerMin);
            addMetric(frame, "已用", elapsed);
            addMetric(frame, "最长剩余", remaining);
            addMetric(frame, "实时流速", flow, "L/min");
            setHints(frame, "取消 停止");
            return frame;
        }
        if (snapshot.calibrationStatus == CalibrationSessionStatus::AwaitingActual) {
            copyText(frame.tag, "待录入");
            copyText(frame.title, "等待网页录入");
            formatLiters(frame.mainValue, sizeof(frame.mainValue), snapshot.water.volumeMl);
            copyText(frame.mainUnit, "L");
            char elapsed[12]{};
            formatElapsed(elapsed, sizeof(elapsed), snapshot.water.elapsedSec);
            addMetric(frame, "本次用时", elapsed);
            addMetric(frame, "有效样本", tag);
            addMetric(frame, "下一步", "网页");
            setHints(frame, "取消 放弃");
            return frame;
        }
        copyText(frame.title, "等待本地出水");
        std::snprintf(frame.mainValue, sizeof(frame.mainValue), "%u", static_cast<unsigned>(snapshot.calibrationValidSampleCount));
        copyText(frame.mainUnit, "条");
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
        setHints(frame, "确认 开始", "取消 退出");
        return frame;
    }

    if (snapshot.localMode == LocalUiMode::Result ||
        snapshot.water.state == WaterState::Running || snapshot.water.state == WaterState::Paused) {
        renderWaterPage(frame, snapshot);
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
            frame.page = snapshot.water.mode == WaterMode::Volume ? ColorDisplayPage::StandbyVolume
                                                                  : ColorDisplayPage::StandbyTime;
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
            setHints(frame, "取消 返回");
            return frame;
    }
}

}  // namespace faucet
