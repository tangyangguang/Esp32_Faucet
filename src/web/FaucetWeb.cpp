#ifndef NATIVE_BUILD

#include "web/FaucetWeb.h"

#include "app/AppController.h"
#include "app/AppConfig.h"
#include "app/ConfigStore.h"
#include "app/FilterStore.h"
#include "app/WaterRecordCalibrationStore.h"
#include "app/WaterRecordStore.h"
#include "app/WaterPulseTraceStore.h"
#include "web/FaucetWebJson.h"
#include "web/FaucetWebParsing.h"
#include "web/FaucetWebRoutes.h"

#include <Esp32Base.h>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace faucet {
namespace {

constexpr std::uint32_t kChartDays = kUsageSummaryMaxDays;
constexpr std::size_t kHomeTodayRecordLimit = 5;
FaucetWebContext g_context{};

struct TodayOverview {
    bool timeReady = false;
    std::uint32_t count = 0;
    std::uint32_t volumeMl = 0;
    std::uint32_t durationSec = 0;
    WaterRecord latest[kHomeTodayRecordLimit]{};
    std::size_t latestCount = 0;
};

bool requireContext();
bool getParam(const char* name, char* out, std::size_t len);
bool persistConfig(const SystemConfig& config);
void handleRecordDetailPage();
void handleTraceCalibrationApi();
void formatWaterRecordTime(const WaterRecord& record, char* out, std::size_t len);

Esp32BaseWeb::Method toBaseMethod(FaucetWebMethod method) {
    switch (method) {
        case FaucetWebMethod::Get:
            return Esp32BaseWeb::METHOD_GET;
        case FaucetWebMethod::Post:
            return Esp32BaseWeb::METHOD_POST;
        case FaucetWebMethod::Any:
        default:
            return Esp32BaseWeb::METHOD_ANY;
    }
}

void sendFmt(const char* fmt, ...) {
    char buffer[512]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Esp32BaseWeb::sendChunk(buffer);
}

void sendHtmlAttrEscaped(const char* text) {
    for (const char* p = text ? text : ""; *p; ++p) {
        switch (*p) {
            case '&':
                Esp32BaseWeb::sendChunk("&amp;");
                break;
            case '<':
                Esp32BaseWeb::sendChunk("&lt;");
                break;
            case '>':
                Esp32BaseWeb::sendChunk("&gt;");
                break;
            case '"':
                Esp32BaseWeb::sendChunk("&quot;");
                break;
            case '\'':
                Esp32BaseWeb::sendChunk("&#39;");
                break;
            default:
                sendFmt("%c", *p);
                break;
        }
    }
}

void sendHtmlAttrEscapedBounded(const char* text, std::size_t maxLen) {
    if (!text) {
        return;
    }
    for (std::size_t i = 0; i < maxLen && text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '&':
                Esp32BaseWeb::sendChunk("&amp;");
                break;
            case '<':
                Esp32BaseWeb::sendChunk("&lt;");
                break;
            case '>':
                Esp32BaseWeb::sendChunk("&gt;");
                break;
            case '"':
                Esp32BaseWeb::sendChunk("&quot;");
                break;
            case '\'':
                Esp32BaseWeb::sendChunk("&#39;");
                break;
            default:
                sendFmt("%c", text[i]);
                break;
        }
    }
}

void sendHtmlEscapedBounded(const char* text, std::size_t maxLen) {
    if (!text) {
        return;
    }
    for (std::size_t i = 0; i < maxLen && text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '&':
                Esp32BaseWeb::sendChunk("&amp;");
                break;
            case '<':
                Esp32BaseWeb::sendChunk("&lt;");
                break;
            case '>':
                Esp32BaseWeb::sendChunk("&gt;");
                break;
            case '"':
                Esp32BaseWeb::sendChunk("&quot;");
                break;
            case '\'':
                Esp32BaseWeb::sendChunk("&#39;");
                break;
            default:
                sendFmt("%c", text[i]);
                break;
        }
    }
}

void formatLiters(std::uint32_t ml, char* out, std::size_t len) {
    const std::uint32_t centiliters = (ml + 5UL) / 10UL;
    std::snprintf(out, len, "%lu.%02lu L", static_cast<unsigned long>(centiliters / 100UL),
                  static_cast<unsigned long>(centiliters % 100UL));
}

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

void dateFromDayIndex(std::uint32_t day, std::uint16_t& year, std::uint8_t& month, std::uint8_t& monthDay) {
    year = 2000;
    while (true) {
        const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
        if (day < yearDays) {
            break;
        }
        day -= yearDays;
        ++year;
    }
    static constexpr std::uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    month = 1;
    while (month <= 12) {
        std::uint8_t days = monthDays[month - 1];
        if (month == 2 && isLeapYear(year)) {
            days = 29;
        }
        if (day < days) {
            break;
        }
        day -= days;
        ++month;
    }
    monthDay = static_cast<std::uint8_t>(day + 1);
}

void formatDayLabel(std::uint32_t day, char* out, std::size_t len) {
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t monthDay = 0;
    dateFromDayIndex(day, year, month, monthDay);
    std::snprintf(out, len, "%02u-%02u", static_cast<unsigned>(month), static_cast<unsigned>(monthDay));
}

std::uint32_t filterProgressPercent(const FilterRecord& filter, std::uint32_t usedDays) {
    if (filter.recommendDays == 0) {
        return 0;
    }
    const std::uint64_t percent = (static_cast<std::uint64_t>(usedDays) * 100ULL) / filter.recommendDays;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

std::uint32_t filterFlowProgressPercent(const FilterRecord& filter) {
    if (filter.lifeMl == 0) {
        return 0;
    }
    const std::uint64_t percent = (static_cast<std::uint64_t>(filter.usedMl) * 100ULL) / filter.lifeMl;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

const char* filterTriggerText(const FilterRecord& filter, std::uint32_t usedDays) {
    const std::uint32_t dayProgress = filterProgressPercent(filter, usedDays);
    const std::uint32_t flowProgress = filterFlowProgressPercent(filter);
    if (dayProgress < 100UL && flowProgress < 100UL) {
        return "正常";
    }
    return dayProgress >= flowProgress ? "天数到期" : "流量到期";
}

std::uint32_t daysToMonths(std::uint32_t days) {
    return (days + kDaysPerLifeMonth - 1UL) / kDaysPerLifeMonth;
}

std::uint32_t monthsToDays(std::uint32_t months) {
    const std::uint32_t maxMonths = kMaxFilterLifeDays / kDaysPerLifeMonth;
    if (months > maxMonths) {
        months = maxMonths;
    }
    return months * kDaysPerLifeMonth;
}

void formatLifeRange(const FilterRecord& filter, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (filter.recommendDays == 0) {
        std::snprintf(out, len, "未设置天数");
        return;
    }
    const std::uint32_t minMonths = daysToMonths(filter.recommendDays);
    const std::uint32_t maxMonths = daysToMonths(filter.maxDays);
    if (maxMonths > minMonths) {
        std::snprintf(out, len, "%lu～%lu个月", static_cast<unsigned long>(minMonths),
                      static_cast<unsigned long>(maxMonths));
    } else {
        std::snprintf(out, len, "%lu个月", static_cast<unsigned long>(minMonths));
    }
}

const char* filterStatusText(FilterLifeStatus status) {
    switch (status) {
        case FilterLifeStatus::RecommendReplace:
            return "建议更换";
        case FilterLifeStatus::Expired:
            return "已超期";
        case FilterLifeStatus::Normal:
        default:
            return "正常";
    }
}

const char* filterDisplayStatusText(const FilterRecord& filter, std::uint32_t usedDays) {
    if (!filter.enabled) {
        return "停用";
    }
    return filterStatusText(filterLifeStatus(filter, usedDays));
}

const char* filterStatusClass(const FilterRecord& filter, std::uint32_t usedDays) {
    if (!filter.enabled) {
        return "status-muted";
    }
    switch (filterLifeStatus(filter, usedDays)) {
        case FilterLifeStatus::RecommendReplace:
            return "status-warn";
        case FilterLifeStatus::Expired:
            return "status-error";
        case FilterLifeStatus::Normal:
        default:
            return "status-ok";
    }
}

const char* resultStatusClass(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "status-ok";
        case WaterResult::StoppedByUser:
            return "status-muted";
        case WaterResult::PauseTimeout:
            return "status-warn";
        case WaterResult::SafetyStopped:
        case WaterResult::FlowError:
        default:
            return "status-error";
    }
}

void sendLiters(std::uint32_t ml) {
    char text[24]{};
    formatLiters(ml, text, sizeof(text));
    Esp32BaseWeb::sendChunk(text);
}

void sendTargetValue(const WaterRecord& record) {
    if (record.mode == WaterMode::Time) {
        sendFmt("%lu 秒", static_cast<unsigned long>(record.targetValue));
        return;
    }
    sendLiters(record.targetValue);
}

bool waterRecordCanCalibrate(const WaterRecord& record) {
    return record.pulseCount > 0 &&
           (record.result == WaterResult::Completed || record.result == WaterResult::StoppedByUser);
}

void sendMetricCard(const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<section class='metric-card'><span>");
    Esp32BaseWeb::sendChunk(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::sendChunk(value);
    Esp32BaseWeb::sendChunk("</strong></section>");
}

void sendMetricCardClass(const char* label, const char* value, const char* className) {
    sendFmt("<section class='metric-card %s'><span>", className ? className : "");
    Esp32BaseWeb::sendChunk(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::sendChunk(value);
    Esp32BaseWeb::sendChunk("</strong></section>");
}

void formatSecondsValue(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (seconds >= 3600UL) {
        std::snprintf(out,
                      len,
                      "%lu 小时 %lu 分 %lu 秒",
                      static_cast<unsigned long>(seconds / 3600UL),
                      static_cast<unsigned long>((seconds / 60UL) % 60UL),
                      static_cast<unsigned long>(seconds % 60UL));
    } else if (seconds >= 60UL) {
        std::snprintf(out,
                      len,
                      "%lu 分 %lu 秒",
                      static_cast<unsigned long>(seconds / 60UL),
                      static_cast<unsigned long>(seconds % 60UL));
    } else {
        std::snprintf(out, len, "%lu 秒", static_cast<unsigned long>(seconds));
    }
}

void formatRecordTargetValue(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (record.mode == WaterMode::Time) {
        formatSecondsValue(record.targetValue, out, len);
        return;
    }
    formatLiters(record.targetValue, out, len);
}

void formatRecordPresetLabel(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    char target[24]{};
    formatRecordTargetValue(record, target, sizeof(target));
    if (record.selectedPreset < kPresetCount) {
        std::snprintf(out,
                      len,
                      "预设 %u · %s",
                      static_cast<unsigned>(record.selectedPreset) + 1U,
                      target);
    } else {
        std::snprintf(out, len, "未知预设 · %s", target);
    }
}

void formatDurationShort(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (seconds >= 3600UL) {
        std::snprintf(out, len, "%luh %lum", static_cast<unsigned long>(seconds / 3600UL),
                      static_cast<unsigned long>((seconds / 60UL) % 60UL));
    } else if (seconds >= 60UL) {
        std::snprintf(out, len, "%lum %lus", static_cast<unsigned long>(seconds / 60UL),
                      static_cast<unsigned long>(seconds % 60UL));
    } else {
        std::snprintf(out, len, "%lus", static_cast<unsigned long>(seconds));
    }
}

std::uint32_t percentOf(std::uint32_t value, std::uint32_t total) {
    if (total == 0) {
        return 0;
    }
    const std::uint64_t percent = (static_cast<std::uint64_t>(value) * 100ULL) / total;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

void sendStatBar(const char* label, const char* value, std::uint32_t percent) {
    sendFmt("<div class='stat-bar'><div class='stat-bar-head'><span>%s</span><strong>%s</strong></div>"
            "<div class='progress'><span style='width:%lu%%'></span></div></div>",
            label,
            value,
            static_cast<unsigned long>(percent));
}

void addSaturating(std::uint32_t& target, std::uint32_t value) {
    const std::uint32_t max = UINT32_MAX;
    target = max - target < value ? max : target + value;
}

bool waterTaskActive() {
    if (!g_context.app) {
        return false;
    }
    const WaterState state = g_context.app->snapshot().water.state;
    return state == WaterState::Confirm || state == WaterState::Running || state == WaterState::Paused;
}

bool sameWaterRecordIdentity(const WaterRecord& a, const WaterRecord& b) {
    return a.startTime == b.startTime && a.volumeMl == b.volumeMl && a.targetValue == b.targetValue &&
           a.pulseCount == b.pulseCount && a.durationSec == b.durationSec && a.selectedPreset == b.selectedPreset &&
           a.result == b.result;
}

std::uint32_t pulsePerLiterFromPulsePerMl(float pulsePerMl) {
    if (!std::isfinite(pulsePerMl) || pulsePerMl <= 0.0f) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::lround(pulsePerMl * 1000.0f));
}

bool findRecordCalibration(const WaterRecord& record, WaterRecordCalibration& calibration) {
    return g_context.recordCalibrations && g_context.recordCalibrations->ready() &&
           g_context.recordCalibrations->find(record, calibration);
}

void sendSignedLiters(std::int32_t ml) {
    if (ml < 0) {
        Esp32BaseWeb::sendChunk("-");
        sendLiters(static_cast<std::uint32_t>(-ml));
        return;
    }
    Esp32BaseWeb::sendChunk("+");
    sendLiters(static_cast<std::uint32_t>(ml));
}

bool shouldShowTargetDelta(const WaterRecord& record) {
    if (record.mode != WaterMode::Volume || record.targetValue == 0) {
        return false;
    }
    const std::int32_t diff =
        static_cast<std::int32_t>(record.volumeMl) - static_cast<std::int32_t>(record.targetValue);
    const std::uint32_t absDiff = diff < 0 ? static_cast<std::uint32_t>(-diff) : static_cast<std::uint32_t>(diff);
    return absDiff >= 100UL && absDiff * 100UL >= record.targetValue * 3UL;
}

void sendTargetDeltaHint(const WaterRecord& record) {
    if (!shouldShowTargetDelta(record)) {
        return;
    }
    const std::int32_t diff =
        static_cast<std::int32_t>(record.volumeMl) - static_cast<std::int32_t>(record.targetValue);
    Esp32BaseWeb::sendChunk("<small class='hint'>较目标 ");
    sendSignedLiters(diff);
    Esp32BaseWeb::sendChunk("</small>");
}

const char* calibrationKindText(WaterRecordCalibrationKind kind) {
    switch (kind) {
        case WaterRecordCalibrationKind::StartupCompensation:
            return "启动补偿";
        case WaterRecordCalibrationKind::PulsePerMl:
        default:
            return "稳态脉冲/升";
    }
}

const char* traceStateText(WaterPulseTraceState state) {
    switch (state) {
        case WaterPulseTraceState::Running:
            return "运行";
        case WaterPulseTraceState::Paused:
            return "暂停";
        case WaterPulseTraceState::Completed:
            return "完成";
        case WaterPulseTraceState::Stopped:
            return "停止";
        case WaterPulseTraceState::Error:
        default:
            return "异常";
    }
}

void formatKb(std::size_t bytes, char* out, std::size_t len) {
    const std::uint32_t tenths = static_cast<std::uint32_t>((bytes * 10U + 512U) / 1024U);
    std::snprintf(out, len, "%lu.%luKB", static_cast<unsigned long>(tenths / 10U),
                  static_cast<unsigned long>(tenths % 10U));
}

void sendSegmentedMeteringPanel() {
    const SystemConfig& config = *g_context.config;
    char overall[24]{};
    char startup[64]{};
    char stable[24]{};
    std::snprintf(overall,
                  sizeof(overall),
                  "%luP/L",
                  static_cast<unsigned long>(config.overallPulsePerLiter > 0
                                                 ? config.overallPulsePerLiter
                                                 : pulsePerLiterFromPulsePerMl(config.pulsePerMl)));
    if (config.startupDurationSec > 0 || config.startupPulseCount > 0 || config.startupVolumeMl > 0) {
        char volume[24]{};
        formatLiters(config.startupVolumeMl, volume, sizeof(volume));
        std::snprintf(startup,
                      sizeof(startup),
                      "%lus / %luP / %s / %luP/L",
                      static_cast<unsigned long>(config.startupDurationSec),
                      static_cast<unsigned long>(config.startupPulseCount),
                      volume,
                      static_cast<unsigned long>(config.startupPulsePerLiter));
    } else {
        std::snprintf(startup, sizeof(startup), "未校准");
    }
    std::snprintf(stable,
                  sizeof(stable),
                  "%s%luP/L",
                  config.stablePulsePerLiter == 0 ? "" : "",
                  static_cast<unsigned long>(config.stablePulsePerLiter));
    Esp32BaseWeb::sendChunk("<section class='panel metering-panel'><div class='panel-head'><h3>当前计量参数</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div><div class='metric-grid'>",
            config.segmentedMeteringCalibrated ? "status-ok" : "status-muted",
            config.segmentedMeteringCalibrated ? "已校准" : "诊断中");
    sendMetricCard("全程平均", overall);
    sendMetricCard("启动段", startup);
    sendMetricCard("稳态段", config.stablePulsePerLiter == 0 ? "未校准" : stable);
    Esp32BaseWeb::sendChunk("</div><p class='hint'>第一版分段计量只保存和展示诊断参数，不改变当前关阀控制。</p></section>");
}

void sendPulseTraceCachePanel() {
    if (!g_context.pulseTraces) {
        Esp32BaseWeb::sendChunk("<section class='panel trace-cache-panel'><div class='panel-head'><h3>脉冲明细缓存</h3>"
                                "<span class='status-pill status-error'>不可用</span></div>"
                                "<p class='err'>脉冲明细 RAM 分配失败，本次启动不记录脉冲明细。</p></section>");
        return;
    }
    const WaterPulseTraceStats stats = g_context.pulseTraces->stats();
    char used[24]{};
    char budget[24]{};
    formatKb(stats.usedBytes, used, sizeof(used));
    formatKb(stats.budgetBytes, budget, sizeof(budget));
    char oldest[40]{};
    char latest[40]{};
    if (stats.oldestStartTime > 0) {
        WaterRecord marker{stats.oldestStartTime, 0, 0, 0, 0, 0, WaterMode::Volume, WaterResult::Completed, 0, 0, 0.0f, {0, 0, 0, 0}};
        formatWaterRecordTime(marker, oldest, sizeof(oldest));
    }
    if (stats.latestStartTime > 0) {
        WaterRecord marker{stats.latestStartTime, 0, 0, 0, 0, 0, WaterMode::Volume, WaterResult::Completed, 0, 0, 0.0f, {0, 0, 0, 0}};
        formatWaterRecordTime(marker, latest, sizeof(latest));
    }
    Esp32BaseWeb::sendChunk("<section class='panel trace-cache-panel'><div class='panel-head'><h3>脉冲明细缓存</h3>");
    sendFmt("<div class='trace-head-meter'><div class='progress'><span style='width:%u%%'></span></div>"
            "<span class='muted'>%s / %s</span></div></div><div class='metric-grid'>",
            static_cast<unsigned>(stats.usagePercent),
            used,
            budget);
    char traces[24]{};
    char samples[24]{};
    char usage[24]{};
    std::snprintf(traces, sizeof(traces), "%lu 条", static_cast<unsigned long>(stats.traceCount));
    std::snprintf(samples, sizeof(samples), "%lu 个", static_cast<unsigned long>(stats.sampleCount));
    std::snprintf(usage, sizeof(usage), "%u%%", static_cast<unsigned>(stats.usagePercent));
    sendMetricCard("明细条数", traces);
    sendMetricCard("样本总数", samples);
    sendMetricCard("内存占用", used);
    sendMetricCard("使用率", usage);
    Esp32BaseWeb::sendChunk("</div><p class='hint'>");
    sendFmt("最早 %s · 最新 %s", oldest[0] ? oldest : "-", latest[0] ? latest : "-");
    Esp32BaseWeb::sendChunk("</p></section>");
}

std::uint32_t sumRealRecordVolumeSince(std::uint32_t startTime) {
    if (!g_context.records || !g_context.records->ready() || startTime < kMinRealDateSeconds) {
        return 0;
    }
    std::uint32_t total = 0;
    WaterRecord* records = new (std::nothrow) WaterRecord[kMaxRecordPageSize]{};
    if (!records) {
        return 0;
    }
    const std::size_t count = g_context.records->count();
    for (std::size_t offset = 0; offset < count; offset += kMaxRecordPageSize) {
        const std::size_t page = offset / kMaxRecordPageSize;
        const std::size_t read = g_context.records->readPage(page, kMaxRecordPageSize, records, kMaxRecordPageSize);
        if (read == 0) {
            break;
        }
        for (std::size_t i = 0; i < read; ++i) {
            if (waterRecordHasRealTime(records[i]) && records[i].startTime >= startTime && records[i].volumeMl > 0) {
                addSaturating(total, records[i].volumeMl);
            }
        }
    }
    delete[] records;
    return total;
}

void sendDailyChart(const WaterUsageSummary& summary) {
    std::uint32_t maxVolume = 0;
    std::uint32_t maxDuration = 0;
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        if (summary.days[i].volumeMl > maxVolume) {
            maxVolume = summary.days[i].volumeMl;
        }
        if (summary.days[i].durationSec > maxDuration) {
            maxDuration = summary.days[i].durationSec;
        }
    }
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><div class='chart-head'><h3>最近 30 天</h3>"
                            "<span class='chart-key volume-key'>出水量</span><span class='chart-key duration-key'>出水时长</span></div>"
                            "<p class='hint'>上下两图共用日期轴，灰色短线表示当天无记录。</p>"
                            "<svg viewBox='0 0 1000 210' role='img' aria-label='最近30天每日出水量和出水时长'>");
    constexpr std::uint32_t volumeTop = 14;
    constexpr std::uint32_t durationTop = 114;
    constexpr std::uint32_t chartHeight = 58;
    constexpr std::uint32_t volumeBaseY = volumeTop + chartHeight;
    constexpr std::uint32_t durationBaseY = durationTop + chartHeight;
    constexpr std::uint32_t left = 46;
    constexpr std::uint32_t step = 31;
    constexpr std::uint32_t barWidth = 14;
    char maxText[24]{};
    formatLiters(maxVolume, maxText, sizeof(maxText));
    char maxDurationText[24]{};
    formatDurationShort(maxDuration, maxDurationText, sizeof(maxDurationText));
    Esp32BaseWeb::sendChunk("<line class='axis' x1='40' y1='72' x2='975' y2='72'></line>");
    sendFmt("<text class='y-label' x='38' y='12'>%s</text>", maxText);
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const DailyUsageBucket& day = summary.days[i];
        const std::uint32_t barHeight = maxVolume == 0 ? 0 : (day.volumeMl * chartHeight) / maxVolume;
        const std::uint32_t y = volumeBaseY - barHeight;
        char volume[24]{};
        formatLiters(day.volumeMl, volume, sizeof(volume));
        char duration[24]{};
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        const std::uint32_t avgMl = day.count == 0 ? 0 : day.volumeMl / day.count;
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu'><title>",
                day.count == 0 ? "bar empty-bar" : "bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? volumeBaseY - 2 : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 2 : barHeight));
        char label[8]{};
        formatDayLabel(day.dayIndex, label, sizeof(label));
        sendFmt("%s：%s · %s · %u 次 · 平均 %lu ml/次</title></rect>",
                label,
                volume,
                duration,
                static_cast<unsigned>(day.count),
                static_cast<unsigned long>(avgMl));
    }
    Esp32BaseWeb::sendChunk("<line class='axis' x1='40' y1='172' x2='975' y2='172'></line>");
    sendFmt("<text class='y-label' x='38' y='112'>%s</text>", maxDurationText);
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const DailyUsageBucket& day = summary.days[i];
        const std::uint32_t barHeight = maxDuration == 0 ? 0 : (day.durationSec * chartHeight) / maxDuration;
        const std::uint32_t y = durationBaseY - barHeight;
        char label[8]{};
        char duration[24]{};
        formatDayLabel(day.dayIndex, label, sizeof(label));
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu'><title>%s：%s · %u 次</title></rect>",
                day.count == 0 ? "duration-bar empty-bar" : "duration-bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? durationBaseY - 2 : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 2 : barHeight),
                label,
                duration,
                static_cast<unsigned>(day.count));
        if (i % 5 == 0 || i + 1 == summary.dayCount) {
            sendFmt("<text class='x-label' x='%lu' y='204'>%s</text>",
                    static_cast<unsigned long>(x + barWidth / 2),
                    label);
        }
    }
    Esp32BaseWeb::sendChunk("</svg></section>");
}

void sendTimeUnsyncedChartNotice() {
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><h3>最近 30 天出水量</h3>"
                            "<p class='hint'>时间未同步，暂不生成按真实日期统计的图表；同步后会自动显示今日往前 30 天。</p>"
                            "</section>");
}

void formatRecordTime(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (seconds >= kMinRealDateSeconds) {
        char date[16]{};
        formatDate(seconds, date, sizeof(date));
        const std::uint32_t daySecond = seconds % 86400UL;
        std::snprintf(out, len, "%s %02lu:%02lu:%02lu",
                      date,
                      static_cast<unsigned long>(daySecond / 3600UL),
                      static_cast<unsigned long>((daySecond / 60UL) % 60UL),
                      static_cast<unsigned long>(daySecond % 60UL));
        return;
    }
    std::snprintf(out, len, "开机+%lu s", static_cast<unsigned long>(seconds));
}

void formatRecordTimeOfDay(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (seconds < kMinRealDateSeconds) {
        std::snprintf(out, len, "--:--");
        return;
    }
    const std::uint32_t daySecond = seconds % 86400UL;
    std::snprintf(out,
                  len,
                  "%02lu:%02lu:%02lu",
                  static_cast<unsigned long>(daySecond / 3600UL),
                  static_cast<unsigned long>((daySecond / 60UL) % 60UL),
                  static_cast<unsigned long>(daySecond % 60UL));
}

void formatWaterRecordTime(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (waterRecordHasRealTime(record)) {
        formatRecordTime(record.startTime, out, len);
        return;
    }
    if (waterRecordHasBootRelativeTime(record)) {
        std::snprintf(out, len, "未知时间 / 本次开机+%lu s", static_cast<unsigned long>(record.startTime));
        return;
    }
    std::snprintf(out, len, "历史未知时间");
}

void sendNoticeFromQuery() {
    char text[32]{};
    if (getParam("saved", text, sizeof(text))) {
        Esp32BaseWeb::sendChunk("<p class='ok'>已保存。</p>");
        return;
    }
    if (getParam("reset", text, sizeof(text))) {
        Esp32BaseWeb::sendChunk("<p class='ok'>已重置。</p>");
        return;
    }
    if (!getParam("error", text, sizeof(text))) {
        return;
    }
    const char* message = "操作失败，请检查输入后重试。";
    if (std::strcmp(text, "busy") == 0) {
        message = "设备不在待机状态，请回到待机后再保存配置。";
    } else if (std::strcmp(text, "invalid_index") == 0) {
        message = "编号无效，请返回列表重新选择。";
    } else if (std::strcmp(text, "invalid_value") == 0) {
        message = "数值超出允许范围，请按页面提示填写。";
    } else if (std::strcmp(text, "invalid_date") == 0) {
        message = "日期格式无效，请重新选择日期。";
    } else if (std::strcmp(text, "save_failed") == 0) {
        message = "保存失败，请稍后重试。";
    } else if (std::strcmp(text, "no_calibration_record") == 0) {
        message = "最新记录没有可用的原始脉冲，不能用于校准。";
    } else if (std::strcmp(text, "calibration_mark_failed") == 0) {
        message = "校准参数已尝试保存，但记录校准标记保存失败，请重试。";
    } else if (std::strcmp(text, "calibration_drift") == 0) {
        message = "新系数和旧系数偏差过大，请重新接水测量。";
    }
    Esp32BaseWeb::sendChunk("<p class='err'>");
    Esp32BaseWeb::sendChunk(message);
    Esp32BaseWeb::sendChunk("</p>");
}

void sendAppStyles() {
    Esp32BaseWeb::sendChunk("<style>");
    Esp32BaseWeb::sendChunk(":root{--bg:#fff;--surface:#fff;--line:#dde5e3;--text:#17202a;--muted:#68747d;--accent:#2f756f;--accent-soft:#e3f1ed;--warn:#a36b10}"
                            "body{max-width:1280px;background:var(--bg);color:var(--text);font-size:14px;line-height:1.42;padding:14px 18px;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,'PingFang SC','Microsoft YaHei',sans-serif}"
                            "h1,h2,h3{letter-spacing:0;color:var(--text)}h2{font-size:18px;margin:18px 0 10px}h3{font-size:15px;margin:0 0 8px}.page{margin:0}p{margin:0 0 8px}");
    Esp32BaseWeb::sendChunk("nav,.footerbar,.panel,.metric-card,.filter-card,.daily-chart,.usage-panel,table{background:var(--surface);border:1px solid var(--line);border-radius:8px;box-shadow:0 1px 2px rgba(16,24,40,.035)}"
                            "nav{display:flex;align-items:center;gap:6px;margin:0 0 18px;padding:8px 10px;overflow-x:auto}nav a{font-size:15px;font-weight:650;padding:8px 12px;margin:0;border-radius:7px;color:#25313f}.brand{font-weight:750}nav a.active{background:var(--accent-soft);color:#17635b}"
                            ".footerbar{margin-top:18px;padding:9px 12px}.syslinks a{background:#f1f4f4;color:var(--muted)}.heap{color:var(--muted)}");
    Esp32BaseWeb::sendChunk(".muted{color:var(--muted)}.hint{display:block;color:var(--muted);font-size:12px;margin:3px 0 0}.panel{padding:12px;margin:12px 0}.panel h3{padding-bottom:6px;margin-bottom:8px;border-bottom:1px solid #eef2f1}"
                            ".panel-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px;padding-bottom:7px;border-bottom:1px solid #eef2f1}.panel-head h3{padding:0;margin:0;border:0}");
    Esp32BaseWeb::sendChunk(".records-top-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin:0 0 14px;align-items:stretch}"
                            ".records-top-grid .panel{display:flex;flex-direction:column;margin:0}.records-top-grid .metric-grid{grid-template-columns:repeat(auto-fit,minmax(128px,1fr));gap:8px;margin:0}.records-top-grid .metric-card{min-height:44px;padding:9px 11px}.records-top-grid .metric-card span{font-size:12px;margin-bottom:3px}.records-top-grid .metric-card strong{font-size:16px}"
                            ".metering-panel .hint{margin-top:auto;padding-top:10px}.trace-cache-panel .metric-grid{grid-template-columns:repeat(2,minmax(0,1fr));margin-top:0}.trace-cache-panel .panel-head{margin-bottom:8px}.trace-head-meter{display:grid;grid-template-columns:minmax(120px,1fr) auto;gap:9px;align-items:center;min-width:230px}.trace-head-meter .progress{height:7px}.trace-badge{display:inline-flex;align-items:center;min-height:20px;margin-left:7px;padding:0 7px;border:1px solid #cfe4dc;border-radius:999px;background:var(--accent-soft);color:#17635b;font-size:12px;font-weight:700;vertical-align:middle}"
                            ".pulse-cell{font-variant-numeric:tabular-nums}.inline-note{display:inline-flex;align-items:center;min-height:20px;margin-left:6px;padding:0 7px;border-radius:999px;background:#eef3f2;color:var(--muted);font-size:12px;font-weight:650;white-space:nowrap}.inline-note.ok{background:#e8f4ee;color:#21634c}");
    Esp32BaseWeb::sendChunk(".pulse-detail-chart{padding:10px 0 2px;overflow-x:auto}.pulse-detail-chart svg{display:block;width:100%;min-width:760px;height:auto}.pulse-detail-chart .axis{stroke:#d9e0df;stroke-width:1}.pulse-detail-chart .grid-line{stroke:#edf2f1;stroke-width:1}.pulse-line{fill:none;stroke:var(--accent);stroke-width:3;stroke-linejoin:round;stroke-linecap:round}.cum-line{fill:none;stroke:#7c8fae;stroke-width:2.5;stroke-linejoin:round;stroke-linecap:round;opacity:.9}.pulse-dot{fill:var(--surface);stroke:var(--accent);stroke-width:2}.stable-line{stroke:#a36b10;stroke-width:2;stroke-dasharray:7 5}.chart-label{font-size:12px;fill:var(--muted)}.chart-legend{display:flex;align-items:center;gap:14px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin:6px 0 0}.legend-mark{display:inline-block;width:18px;height:3px;border-radius:999px;margin-right:5px;vertical-align:middle}.legend-pulse{background:var(--accent)}.legend-cum{background:#7c8fae}.legend-stable{background:#a36b10}.detail-data summary{cursor:pointer;font-weight:750;color:#355e66}.detail-data table{margin-top:10px}");
    Esp32BaseWeb::sendChunk(".grid,.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin:0 0 12px}"
                            ".metric-card{padding:12px 14px;min-height:54px}.metric-card.primary{border-color:#b8d7cf;background:#f7fbfa}.metric-card span{display:block;color:var(--muted);font-size:13px;font-weight:600;margin-bottom:4px}.metric-card strong{display:block;color:var(--text);font-size:18px;line-height:1.2;font-weight:750}"
                            ".machine-status{padding:14px 16px;margin:0 0 14px;border-color:#c8ddd7;background:#fbfefd}"
                            ".machine-main{display:grid;grid-template-columns:minmax(280px,.85fr) minmax(0,1.15fr);gap:16px;align-items:stretch}.machine-main.compact{grid-template-columns:minmax(250px,.58fr) minmax(0,1.42fr)}.machine-hero{display:flex;flex-direction:column;justify-content:center;min-height:118px}.machine-eyebrow{display:block;color:var(--muted);font-size:13px;font-weight:700;margin-bottom:4px}.machine-hero strong{display:block;font-size:31px;line-height:1.05;font-weight:800}.machine-note{margin:8px 0 0;color:#405059;font-weight:650}.machine-preset-line{margin:5px 0 0;color:var(--muted);font-weight:650}.machine-progress{margin-top:14px}.machine-progress-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:var(--muted);font-size:13px;font-weight:700;margin-bottom:7px}.progress{height:9px;background:#e7eeec;border-radius:999px;overflow:hidden}.progress span{display:block;height:100%;background:var(--accent);border-radius:999px}.machine-kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px;align-content:stretch}.machine-kpi,.machine-indicator{display:flex;flex-direction:column;justify-content:center;min-height:58px;padding:10px 11px;border:1px solid #edf2f1;border-radius:7px;background:#fff}.machine-kpi span{display:block;color:var(--muted);font-size:12px;font-weight:700;margin-bottom:3px}.machine-kpi strong{display:block;font-size:16px;line-height:1.2;font-weight:750}.machine-kpi.soft strong{font-size:14px;color:#405059}");
    Esp32BaseWeb::sendChunk(".today-layout{display:grid;grid-template-columns:minmax(190px,.28fr) minmax(0,1.72fr);gap:12px;margin:0 0 14px}.today-summary-card{display:flex;flex-direction:column;justify-content:flex-start;min-height:92px;padding:14px 16px}.today-summary-label{display:block;color:var(--muted);font-size:13px;font-weight:700;line-height:1.35;margin-bottom:6px}.today-total-main{display:block;color:var(--text);font-size:26px;line-height:1.05;font-weight:800}.today-total-meta{display:flex;align-items:center;flex-wrap:wrap;gap:3px 8px;color:var(--muted);font-size:13px;font-weight:600;margin-top:8px}.today-meta-item{display:inline-flex;align-items:baseline;gap:3px;white-space:nowrap}.today-meta-item+.today-meta-item:before{content:'·';margin-right:5px;color:#a2adb4}.today-meta-value{color:#52616b;font-weight:650}.today-records{padding:8px 10px;overflow-x:auto}.today-record-table{min-width:680px;margin:0;border:0;border-radius:0;box-shadow:none;background:transparent;font-size:13px}.today-record-table th,.today-record-table td{padding:6px 8px}.today-record-table th{background:transparent}.today-record-table .record-duration{white-space:nowrap}.today-record-table .status-pill{justify-content:center}");
    Esp32BaseWeb::sendChunk(".filter-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px;margin:0 0 12px}.filter-card{padding:12px 14px;min-height:128px}.filter-head{display:flex;align-items:flex-start;justify-content:space-between;gap:8px;margin-bottom:8px}.filter-head strong{font-size:16px;line-height:1.25;font-weight:750}"
                            ".filter-meta{display:grid;gap:4px;color:var(--muted);font-size:13px;margin-top:10px}.dual-progress{display:grid;gap:7px;margin:8px 0 10px}.filter-progress-row{display:grid;grid-template-columns:48px 1fr;gap:8px;align-items:center;color:var(--muted);font-size:12px}.filter-track{display:block;height:7px;background:#edf3f1;border:1px solid #d7e3e0;border-radius:999px;overflow:hidden}.filter-progress-fill{display:block;height:100%;border-radius:999px}.day-progress{background:var(--accent)}.flow-progress{background:#c9822c}");
    Esp32BaseWeb::sendChunk(".status-pill{display:inline-flex;align-items:center;min-height:22px;padding:0 9px;border-radius:999px;background:#eef2f2;color:#55616a;font-size:12px;font-weight:650;line-height:1;white-space:nowrap}.status-ok{background:#e8f4ee;color:#21634c;border-color:#bdddcf}.status-warn{background:#fff7e6;color:#7a520e;border-color:#eed28f}.status-error{background:#fff0ee;color:#9b3328;border-color:#efc1ba}.status-muted{background:#eef2f2;color:#66737c;border-color:#d8e0df}.warn{display:inline-block;background:#fff8e6;border:1px solid #ead28b;border-radius:8px;padding:7px 9px;color:#6b4a12;margin:0 0 10px}.filter-used-days{font-variant-numeric:tabular-nums}.filter-progress-label{display:grid;grid-template-columns:48px 1fr;gap:6px;align-items:center;color:var(--muted);font-size:12px}");
    Esp32BaseWeb::sendChunk(".stats-layout{display:grid;grid-template-columns:minmax(0,1fr);gap:10px;align-items:start}.daily-chart{padding:14px 16px;margin:0 0 12px;overflow-x:auto}.chart-head{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:2px}.chart-head h3{margin:0 4px 0 0}.chart-key{font-size:12px;color:var(--muted);font-weight:650}.chart-key:before{content:'';display:inline-block;width:9px;height:9px;border-radius:2px;margin-right:5px}.volume-key:before{background:var(--accent)}.duration-key:before{background:#7c8fae}");
    Esp32BaseWeb::sendChunk(".daily-chart svg{display:block;width:100%;height:auto;min-width:720px;margin-top:6px}.daily-chart .bar{fill:var(--accent)}.daily-chart .duration-bar{fill:#7c8fae}.daily-chart .empty-bar{fill:#cfd7d5}.daily-chart .axis{stroke:#d9e0df;stroke-width:1}.daily-chart .x-label,.daily-chart .y-label{font-size:11px;text-anchor:middle;fill:var(--muted)}");
    Esp32BaseWeb::sendChunk(".usage-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px;margin:0 0 12px}.usage-panel{padding:12px 14px}.usage-panel h3{margin-bottom:9px}.usage-row{display:grid;grid-template-columns:minmax(72px,1fr) auto;gap:5px 8px;align-items:center;margin:0 0 8px;color:var(--muted);font-size:13px}.usage-row strong{color:var(--text);font-weight:650}.usage-bar{grid-column:1/-1;height:5px;background:#e6ecea;border-radius:999px;overflow:hidden}.usage-bar i{display:block;height:100%;background:var(--accent);border-radius:999px}");
    Esp32BaseWeb::sendChunk(".form-grid{display:grid;grid-template-columns:repeat(12,1fr);gap:10px 12px;align-items:start}.span-2{grid-column:span 2}.span-3{grid-column:span 3}.span-4{grid-column:span 4}.span-5{grid-column:span 5}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:1/-1}"
                            ".field span,.check-title{display:block;font-size:12px;color:var(--muted);font-weight:650;margin-bottom:4px}.field input,.field select{margin-bottom:0}.check-line{display:inline-flex;align-items:center;gap:6px;min-height:32px;padding:0 8px;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--text);font-size:14px;white-space:nowrap}.check-line input{margin:0}");
    Esp32BaseWeb::sendChunk(".form-actions{display:flex;align-items:center;justify-content:flex-start;gap:6px;margin-top:10px;flex-wrap:wrap}.form-actions form{margin:0}.form-actions a,.btn-link,.page-link,.page-current,.row-actions a{display:inline-flex;align-items:center;justify-content:center;min-height:32px;padding:0 10px;border:1px solid var(--line);border-radius:6px;background:#f7f9fa;color:#355e66;font-size:13px;line-height:1.2;box-sizing:border-box}input.secondary{background:#f7f9fa;border:1px solid var(--line);color:#4c565d}.row-actions{display:flex;gap:5px;align-items:center;flex-wrap:wrap}");
    Esp32BaseWeb::sendChunk("table{width:100%;border-collapse:separate;border-spacing:0;margin:0 0 12px;overflow:hidden;font-size:13px}td,th{padding:8px 10px;border-bottom:1px solid #edf1f0;text-align:left;vertical-align:middle}tr:last-child td{border-bottom:0}th{background:#f8faf9;color:var(--muted);font-weight:700}.filters-table th:first-child{width:22%}.filters-table th:last-child{width:150px}.kv th{width:26%}");
    Esp32BaseWeb::sendChunk(".pager{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin:0 0 10px}.pager-links{display:flex;align-items:center;gap:5px;flex-wrap:wrap}.page-current{background:var(--accent-soft);color:#17635b;border-color:#cfe4dc}.page-disabled{color:#9aa3aa;background:#f4f6f6;pointer-events:none}.page-size{display:flex;align-items:center;gap:6px;color:var(--muted);font-size:13px}.page-size select{width:auto;min-width:80px}");
    Esp32BaseWeb::sendChunk(".disabled-row{background:#f7f8f8;color:#8a949b}.disabled-row td{color:#8a949b}.disabled-row .status-pill{background:#eef0f0;color:#7b858d}.disabled-row a{color:#6f7a82}"
                            "@media(max-width:920px){.records-top-grid{grid-template-columns:1fr}}"
                            "@media(max-width:820px){.machine-main,.machine-main.compact,.today-layout{grid-template-columns:1fr}.machine-hero{min-height:0}.machine-hero strong{font-size:26px}}"
                            "@media(max-width:720px){body{padding:10px}.form-grid{grid-template-columns:1fr}.span-2,.span-3,.span-4,.span-5,.span-6,.span-8,.span-12{grid-column:1/-1}.usage-grid{grid-template-columns:1fr}.daily-chart svg{min-width:680px}}"
                            "@media(max-width:520px){.grid,.metric-grid,.filter-cards,.machine-kpis{grid-template-columns:1fr}.metric-card{min-height:0}.pager{align-items:flex-start}.page-size{width:100%}.kv th{width:34%}}");
    Esp32BaseWeb::sendChunk("</style>");
}

bool sendPageStart(const char* title) {
    if (!Esp32BaseWeb::checkAuth()) {
        return false;
    }
    Esp32BaseWeb::sendHeader(title);
    return true;
}

void sendPageEnd() {
    Esp32BaseWeb::sendFooter();
}

const char* stateText(WaterState state) {
    switch (state) {
        case WaterState::Idle:
            return "待机";
        case WaterState::Confirm:
            return "确认";
        case WaterState::Running:
            return "出水中";
        case WaterState::Paused:
            return "暂停";
        case WaterState::Error:
            return "异常";
    }
    return "未知";
}

const char* modeText(WaterMode mode) {
    switch (mode) {
        case WaterMode::Volume:
            return "容量";
        case WaterMode::Time:
            return "时间";
    }
    return "未知";
}

const char* resultText(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "完成";
        case WaterResult::StoppedByUser:
            return "手动停止";
        case WaterResult::SafetyStopped:
            return "安全停止";
        case WaterResult::FlowError:
            return "流量异常";
        case WaterResult::PauseTimeout:
            return "暂停超时";
    }
    return "未知";
}

void sendTextInput(const char* label, const char* name, unsigned long value) {
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'></label>", label, name, value);
}

void sendVolumeInput(const char* label, const char* name, std::uint32_t value) {
    char liters[24]{};
    formatLiters(value, liters, sizeof(liters));
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'><small class='hint'>当前 %s</small></label>",
            label,
            name,
            static_cast<unsigned long>(value),
            liters);
}

void sendMonthInput(const char* label, const char* name, std::uint32_t days) {
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'><small class='hint'>按 30 天/月计算</small></label>",
            label,
            name,
            static_cast<unsigned long>(daysToMonths(days)));
}

void sendDateInput(const char* label, const char* name, std::uint32_t seconds) {
    char date[16]{};
    formatDate(seconds >= kMinRealDateSeconds ? seconds : 0, date, sizeof(date));
    sendFmt("<label class='field'><span>%s</span><input type='date' name='%s' value='%s'></label>",
            label,
            name,
            date);
}

void sendCheckbox(const char* label, const char* name, bool checked) {
    sendFmt("<label class='check-field'><span class='check-title'>%s</span><span class='check-line'><input type='checkbox' name='%s' value='1'%s>启用</span></label>",
            label,
            name,
            checked ? " checked" : "");
}

void formatMonthRange(const WaterUsageSummary& summary, char* out, std::size_t len) {
    if (!out || len == 0 || summary.monthStartDay == 0 || summary.todayDay == 0) {
        return;
    }
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    dateFromDayIndex(summary.monthStartDay, year, month, day);
    std::uint16_t endYear = 0;
    std::uint8_t endMonth = 0;
    std::uint8_t endDay = 0;
    dateFromDayIndex(summary.todayDay, endYear, endMonth, endDay);
    std::snprintf(out,
                  len,
                  "%u 月 %u ~ %u 日",
                  static_cast<unsigned>(month),
                  static_cast<unsigned>(day),
                  static_cast<unsigned>(endDay));
}

std::uint32_t maxCountVolume(const CountVolumeBucket* buckets, std::size_t count) {
    std::uint32_t max = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (buckets[i].volumeMl > max) {
            max = buckets[i].volumeMl;
        }
    }
    return max;
}

std::uint32_t maxResultCount(const std::uint32_t* values, std::size_t count) {
    std::uint32_t max = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (values[i] > max) {
            max = values[i];
        }
    }
    return max;
}

void sendUsageBar(const char* label, std::uint32_t volumeMl, std::uint32_t count, std::uint32_t maxVolume) {
    char volume[24]{};
    formatLiters(volumeMl, volume, sizeof(volume));
    const std::uint32_t percent = maxVolume == 0 ? 0 : (volumeMl * 100UL) / maxVolume;
    sendFmt("<div class='usage-row'><span>%s</span><strong>%s · %lu 次</strong>"
            "<div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            label,
            volume,
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent));
}

void sendCountBar(const char* label, std::uint32_t count, std::uint32_t maxCount) {
    const std::uint32_t percent = maxCount == 0 ? 0 : (count * 100UL) / maxCount;
    sendFmt("<div class='usage-row'><span>%s</span><strong>%lu 次</strong>"
            "<div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            label,
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent));
}

const char* resultLabel(std::size_t index) {
    switch (static_cast<WaterResult>(index)) {
        case WaterResult::Completed:
            return "正常完成";
        case WaterResult::StoppedByUser:
            return "用户停止";
        case WaterResult::SafetyStopped:
            return "安全停止";
        case WaterResult::FlowError:
            return "流量异常";
        case WaterResult::PauseTimeout:
            return "暂停超时";
    }
    return "未知";
}

void sendUsagePatterns(const WaterUsageSummary& summary, const SystemConfig& config) {
    Esp32BaseWeb::sendChunk("<section class='usage-panel'><h3>按预设分布</h3>");
    const std::uint32_t maxPreset = maxCountVolume(summary.presetCounts, kPresetCount);
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (summary.presetCounts[i].count == 0) {
            continue;
        }
        char label[40]{};
        char name[kPresetNameLength]{};
        std::strncpy(name, config.presets[i].name, sizeof(name) - 1);
        std::snprintf(label, sizeof(label), "%u %s", static_cast<unsigned>(i + 1), name);
        sendUsageBar(label, summary.presetCounts[i].volumeMl, summary.presetCounts[i].count, maxPreset);
    }
    if (maxPreset == 0) {
        Esp32BaseWeb::sendChunk("<p class='hint'>最近 30 天没有可聚合的真实时间记录。</p>");
    }
    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>完成结果</h3>");
    const std::uint32_t maxResult = maxResultCount(summary.resultCounts, kUsageResultCount);
    for (std::size_t i = 0; i < kUsageResultCount; ++i) {
        sendCountBar(resultLabel(i), summary.resultCounts[i], maxResult);
    }
    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>单次出水分布</h3>");
    static constexpr const char* histLabels[kUsageVolumeHistCount] = {"< 0.5 L", "0.5 - 2 L", "2 - 5 L", "5 - 10 L", ">= 10 L"};
    const std::uint32_t maxHist = maxCountVolume(summary.volumeHist, kUsageVolumeHistCount);
    for (std::size_t i = 0; i < kUsageVolumeHistCount; ++i) {
        sendUsageBar(histLabels[i], summary.volumeHist[i].volumeMl, summary.volumeHist[i].count, maxHist);
    }
    Esp32BaseWeb::sendChunk("</section>");
}

void sendMachineKpi(const char* label, const char* value, bool soft = false) {
    sendFmt("<div class='machine-kpi machine-indicator%s'><span>%s</span><strong>%s</strong></div>",
            soft ? " soft" : "",
            label,
            value);
}

void sendMachineKpiId(const char* id, const char* label, const char* value, bool soft = false) {
    sendFmt("<div class='machine-kpi machine-indicator%s'><span>%s</span><strong id='%s'>%s</strong></div>",
            soft ? " soft" : "",
            label,
            id,
            value);
}

void sendMachineKpiBlock(const char* wrapperId,
                         const char* valueId,
                         const char* label,
                         const char* value,
                         bool soft = false,
                         bool hidden = false) {
    sendFmt("<div id='%s' class='machine-kpi machine-indicator%s'%s><span>%s</span><strong id='%s'>%s</strong></div>",
            wrapperId,
            soft ? " soft" : "",
            hidden ? " style='display:none'" : "",
            label,
            valueId,
            value);
}

const char* machineStatusNote(const AppSnapshot& snapshot) {
    switch (snapshot.water.state) {
        case WaterState::Idle:
            return "设备可用，等待按键启动";
        case WaterState::Confirm:
            return "等待确认，确认后开始出水";
        case WaterState::Running:
            return "正在出水，请留意容器";
        case WaterState::Paused:
            return "已暂停，等待继续或取消";
        case WaterState::Error:
            return resultText(snapshot.water.lastResult);
    }
    return "状态未知";
}

void sendMachineStatusCard(const AppSnapshot& snapshot, bool screenOn) {
    const bool shouldShowProgress = snapshot.water.state == WaterState::Running ||
                                    snapshot.water.state == WaterState::Paused ||
                                    snapshot.water.state == WaterState::Confirm;
    char targetValue[24]{};
    if (snapshot.water.mode == WaterMode::Time) {
        formatSecondsValue(snapshot.water.targetValue, targetValue, sizeof(targetValue));
    } else {
        formatLiters(snapshot.water.targetValue, targetValue, sizeof(targetValue));
    }
    char currentPreset[48]{};
    std::snprintf(currentPreset,
                  sizeof(currentPreset),
                  "%u · %s · %s",
                  static_cast<unsigned>(snapshot.water.selectedPreset + 1),
                  modeText(snapshot.water.mode),
                  targetValue);
    char outValue[24]{};
    formatLiters(snapshot.water.volumeMl, outValue, sizeof(outValue));
    const std::uint32_t progressBase = snapshot.water.mode == WaterMode::Time ? snapshot.water.elapsedSec : snapshot.water.volumeMl;
    const std::uint32_t progressPercent = percentOf(progressBase, snapshot.water.targetValue);
    char progressText[48]{};
    if (snapshot.water.mode == WaterMode::Time) {
        char elapsed[20]{};
        formatSecondsValue(snapshot.water.elapsedSec, elapsed, sizeof(elapsed));
        std::snprintf(progressText, sizeof(progressText), "%s / %s", elapsed, targetValue);
    } else {
        std::snprintf(progressText, sizeof(progressText), "%s / %s", outValue, targetValue);
    }
    const std::uint32_t remaining =
        snapshot.water.mode == WaterMode::Time
            ? (snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0)
            : (snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0);
    char remainingValue[24]{};
    if (snapshot.water.mode == WaterMode::Time) {
        formatSecondsValue(remaining, remainingValue, sizeof(remainingValue));
    } else {
        formatLiters(remaining, remainingValue, sizeof(remainingValue));
    }
    char elapsedValue[24]{};
    formatSecondsValue(snapshot.water.elapsedSec, elapsedValue, sizeof(elapsedValue));
    char pulsePerLiter[24]{};
    if (snapshot.pulsePerLiter > 0) {
        std::snprintf(pulsePerLiter, sizeof(pulsePerLiter), "%lu 脉冲/L", static_cast<unsigned long>(snapshot.pulsePerLiter));
    } else {
        std::snprintf(pulsePerLiter, sizeof(pulsePerLiter), "未校准");
    }
    char droppedPulses[24]{};
    std::snprintf(droppedPulses, sizeof(droppedPulses), "%lu", static_cast<unsigned long>(snapshot.flowDroppedPulses));

    const bool showRemaining = snapshot.water.state == WaterState::Running || snapshot.water.state == WaterState::Paused ||
                               snapshot.water.state == WaterState::Confirm;
    const bool showElapsed = snapshot.water.state == WaterState::Running || snapshot.water.state == WaterState::Paused;
    const bool showResult = snapshot.water.state == WaterState::Error || snapshot.localMode == LocalUiMode::Result;
    const char* machineLayoutClass = shouldShowProgress ? "machine-main" : "machine-main compact";
    sendFmt("<h2>机器状态</h2><section class='panel machine-status'><div class='%s'><div class='machine-hero'>"
            "<span class='machine-eyebrow'>当前状态</span><strong>",
            machineLayoutClass);
    Esp32BaseWeb::sendChunk("<span id='machineState'>");
    Esp32BaseWeb::sendChunk(stateText(snapshot.water.state));
    sendFmt("</span></strong><p id='machineStatusNote' class='machine-note'>%s</p>"
            "<p class='machine-preset-line'>当前预设 <span id='machinePreset'>%s</span></p>",
            machineStatusNote(snapshot),
            currentPreset);
    sendFmt("<div id='machineProgress' class='machine-progress'%s><div class='machine-progress-head'><span>出水进度</span><strong id='machineProgressText'>%s</strong></div>"
            "<div class='progress'><span id='machineProgressBar' style='width:%lu%%'></span></div></div>",
            shouldShowProgress ? "" : " style='display:none'",
            progressText,
            static_cast<unsigned long>(progressPercent));
    Esp32BaseWeb::sendChunk("</div><div class='machine-kpis'>");
    sendMachineKpiId("targetValue", "目标值", targetValue);
    sendMachineKpiId("outputValue", "已出水", outValue);
    sendMachineKpiBlock("kpiRemaining",
                        "remainingValue",
                        snapshot.water.mode == WaterMode::Time ? "剩余时间" : "剩余量",
                        remainingValue,
                        false,
                        !showRemaining);
    sendMachineKpiBlock("kpiElapsed", "elapsedValue", "已运行", elapsedValue, false, !showElapsed);
    sendMachineKpiBlock("kpiResult", "resultStatus", "结束原因", resultText(snapshot.water.lastResult), false, !showResult);
    sendMachineKpiId("valveStatus", "阀门", snapshot.water.valveOpen ? "开" : "关");
    sendMachineKpiId("pulsePerLiter", "流量计校准系数", pulsePerLiter, true);
    if (snapshot.flowDroppedPulses > 0) {
        sendMachineKpi("丢弃脉冲", droppedPulses, true);
    }
    sendMachineKpiId("screenStatus", "屏幕状态", screenOn ? "亮屏" : "休眠", true);
    Esp32BaseWeb::sendChunk("</div></div></section>");
}

TodayOverview collectTodayOverview(std::uint32_t now, std::uint32_t fallbackTodayMl) {
    TodayOverview overview{};
    overview.volumeMl = fallbackTodayMl;
    if (!g_context.records || !g_context.records->ready() || now < kMinRealDateSeconds || waterTaskActive()) {
        return overview;
    }
    overview.timeReady = true;
    const std::uint32_t todayStart = (now / 86400UL) * 86400UL;
    WaterRecord records[kDefaultRecordPageSize]{};
    const std::size_t total = g_context.records->count();
    bool stopAfterPage = false;
    for (std::size_t offset = 0; offset < total && !stopAfterPage; offset += kDefaultRecordPageSize) {
        const std::size_t page = offset / kDefaultRecordPageSize;
        const std::size_t read = g_context.records->readPage(page, kDefaultRecordPageSize, records, kDefaultRecordPageSize);
        if (read == 0) {
            break;
        }
        for (std::size_t i = 0; i < read; ++i) {
            if (!waterRecordHasRealTime(records[i])) {
                continue;
            }
            if (records[i].startTime > now) {
                continue;
            }
            if (records[i].startTime < todayStart) {
                stopAfterPage = true;
                break;
            }
            ++overview.count;
            overview.durationSec += records[i].durationSec;
            if (overview.latestCount < kHomeTodayRecordLimit) {
                overview.latest[overview.latestCount++] = records[i];
            }
        }
    }
    return overview;
}

void sendTodayOverview(const TodayOverview& overview) {
    char today[24]{};
    char countText[20]{};
    char totalDuration[24]{};
    formatLiters(overview.volumeMl, today, sizeof(today));
    if (overview.timeReady) {
        std::snprintf(countText, sizeof(countText), "%lu 次", static_cast<unsigned long>(overview.count));
        formatSecondsValue(overview.durationSec, totalDuration, sizeof(totalDuration));
    } else {
        std::snprintf(countText, sizeof(countText), "-- 次");
        std::snprintf(totalDuration, sizeof(totalDuration), "--");
    }

    Esp32BaseWeb::sendChunk("<section id='todayOverview' class='today-overview'><h2>今日概览</h2><div class='today-layout'><section class='panel today-summary-card'><span class='today-summary-label'>今日总量</span><strong class='today-total-main'>");
    Esp32BaseWeb::sendChunk(today);
    Esp32BaseWeb::sendChunk("</strong><span class='today-total-meta today-meta-line'><span class='today-meta-item'>接水 <span class='today-meta-value'>");
    Esp32BaseWeb::sendChunk(countText);
    Esp32BaseWeb::sendChunk("</span></span><span class='today-meta-item'>用时 <span class='today-meta-value'>");
    Esp32BaseWeb::sendChunk(totalDuration);
    Esp32BaseWeb::sendChunk("</span></span></span></section><section class='panel today-records'>");
    if (!overview.timeReady) {
        Esp32BaseWeb::sendChunk("<p class='hint'>时间同步后显示今天的接水时间和水量。</p>");
    } else if (overview.latestCount == 0) {
        Esp32BaseWeb::sendChunk("<p class='hint'>今天还没有接水记录。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<table class='today-record-table'><tr><th>开始</th><th>停止</th><th>用时</th><th>实际出水</th><th>预设目标</th><th>结果</th></tr>");
        for (std::size_t i = 0; i < overview.latestCount; ++i) {
            char startTime[12]{};
            char stopTime[12]{};
            char duration[24]{};
            char volume[24]{};
            char preset[48]{};
            const std::uint32_t stopSecond = overview.latest[i].startTime + overview.latest[i].durationSec;
            formatRecordTimeOfDay(overview.latest[i].startTime, startTime, sizeof(startTime));
            formatRecordTimeOfDay(stopSecond, stopTime, sizeof(stopTime));
            formatSecondsValue(overview.latest[i].durationSec, duration, sizeof(duration));
            formatLiters(overview.latest[i].volumeMl, volume, sizeof(volume));
            formatRecordPresetLabel(overview.latest[i], preset, sizeof(preset));
            sendFmt("<tr><td>%s</td><td>%s</td><td class='record-duration'>%s</td><td>%s</td><td>%s</td>"
                    "<td><span class='status-pill %s'>%s</span></td></tr>",
                    startTime,
                    stopTime,
                    duration,
                    volume,
                    preset,
                    resultStatusClass(overview.latest[i].result),
                    resultText(overview.latest[i].result));
        }
        Esp32BaseWeb::sendChunk("</table>");
    }
    Esp32BaseWeb::sendChunk("</section></div></section>");
}

void sendHomeAutoRefreshScript() {
    Esp32BaseWeb::sendChunk("<script>"
                            "var faucetIdlePollMs=10000;"
                            "var faucetActivePollMs=1000;"
                            "var faucetHomeStatusTimer=0;"
                            "var faucetTodayTimer=0;"
                            "var faucetHomeActive=false;"
                            "function faucetLiters(ml){var c=Math.round((Number(ml)||0)/10);return Math.floor(c/100)+'.'+String(c%100).padStart(2,'0')+' L';}"
                            "function faucetSeconds(s){s=Number(s)||0;if(s>=3600){return Math.floor(s/3600)+' 小时 '+Math.floor((s%3600)/60)+' 分 '+(s%60)+' 秒';}if(s>=60){return Math.floor(s/60)+' 分 '+(s%60)+' 秒';}return s+' 秒';}"
                            "function faucetStateText(s){return {idle:'待机',confirm:'确认',running:'出水中',paused:'暂停',error:'异常'}[s]||'未知';}"
                            "function faucetModeText(m){return m==='time'?'时间':'容量';}"
                            "function faucetResultText(r){return {completed:'完成',stoppedByUser:'手动停止',safetyStopped:'安全停止',flowError:'流量异常',pauseTimeout:'暂停超时'}[r]||'未知';}"
                            "function faucetStatusNote(s,r){return {idle:'设备可用，等待按键启动',confirm:'等待确认，确认后开始出水',running:'正在出水，请留意容器',paused:'已暂停，等待继续或取消',error:faucetResultText(r)}[s]||'状态未知';}"
                            "function faucetSet(id,text){var e=document.getElementById(id);if(e){e.textContent=text;}}"
                            "function faucetToggle(id,show){var e=document.getElementById(id);if(e){e.style.display=show?'':'none';}}"
                            "function faucetIsActiveState(s){return s==='running'||s==='paused'||s==='confirm';}"
                            "function scheduleFaucetHomeStatus(ms){clearTimeout(faucetHomeStatusTimer);faucetHomeStatusTimer=setTimeout(updateFaucetHomeStatus,ms);}"
                            "function scheduleFaucetTodayOverview(ms){clearTimeout(faucetTodayTimer);faucetTodayTimer=setTimeout(updateFaucetTodayOverview,ms);}"
                            "function updateFaucetHomeStatus(){if(document.hidden){scheduleFaucetHomeStatus(faucetIdlePollMs);return;}"
                            "fetch('/api/faucet/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){"
                            "var target=s.mode==='time'?faucetSeconds(s.targetValue):faucetLiters(s.targetValue);"
                            "var out=faucetLiters(s.volumeMl);"
                            "var shown=faucetIsActiveState(s.state);"
                            "var remaining=s.mode==='time'?Math.max(0,(Number(s.targetValue)||0)-(Number(s.elapsedSec)||0)):Math.max(0,(Number(s.targetValue)||0)-(Number(s.volumeMl)||0));"
                            "faucetHomeActive=shown;"
                            "faucetSet('machineState',faucetStateText(s.state));"
                            "faucetSet('machineStatusNote',faucetStatusNote(s.state,s.lastResult));"
                            "faucetSet('machinePreset',(Number(s.selectedPreset)+1)+' · '+faucetModeText(s.mode)+' · '+target);"
                            "faucetSet('targetValue',target);faucetSet('outputValue',out);"
                            "faucetSet('remainingValue',s.mode==='time'?faucetSeconds(remaining):faucetLiters(remaining));"
                            "faucetSet('elapsedValue',faucetSeconds(s.elapsedSec));"
                            "faucetSet('resultStatus',faucetResultText(s.lastResult));"
                            "faucetSet('valveStatus',s.valveOpen?'开':'关');"
                            "faucetSet('pulsePerLiter',s.pulsePerLiter>0?s.pulsePerLiter+' 脉冲/L':'未校准');"
                            "faucetSet('screenStatus',s.screenOn?'亮屏':'休眠');"
                            "faucetToggle('kpiRemaining',shown);faucetToggle('kpiElapsed',s.state==='running'||s.state==='paused');faucetToggle('kpiResult',s.state==='error');"
                            "var main=document.querySelector('.machine-main');if(main){main.className=shown?'machine-main':'machine-main compact';}"
                            "var p=document.getElementById('machineProgress');if(p){p.style.display=shown?'block':'none';}"
                            "if(shown){var base=s.mode==='time'?s.elapsedSec:s.volumeMl;var pct=s.targetValue>0?Math.min(100,Math.floor(base*100/s.targetValue)):0;"
                            "faucetSet('machineProgressText',(s.mode==='time'?faucetSeconds(s.elapsedSec):out)+' / '+target);"
                            "var bar=document.getElementById('machineProgressBar');if(bar){bar.style.width=pct+'%';}}"
                            "scheduleFaucetHomeStatus(shown?faucetActivePollMs:faucetIdlePollMs);"
                            "}).catch(function(){scheduleFaucetHomeStatus(faucetIdlePollMs);});}"
                            "function updateFaucetTodayOverview(){if(document.hidden){scheduleFaucetTodayOverview(faucetIdlePollMs);return;}"
                            "fetch('/api/faucet/today',{cache:'no-store'}).then(function(r){if(!r.ok){throw new Error('busy');}return r.text();}).then(function(html){"
                            "var e=document.getElementById('todayOverview');if(e&&html.indexOf(\"id='todayOverview'\")>=0){e.outerHTML=html;}"
                            "scheduleFaucetTodayOverview(faucetHomeActive?5000:faucetIdlePollMs);"
                            "}).catch(function(){scheduleFaucetTodayOverview(faucetIdlePollMs);});}"
                            "document.addEventListener('visibilitychange',function(){if(!document.hidden){scheduleFaucetHomeStatus(200);scheduleFaucetTodayOverview(500);}});"
                            "scheduleFaucetHomeStatus(1000);"
                            "scheduleFaucetTodayOverview(2000);"
                            "</script>");
}

void sendFilterCards(std::uint32_t now) {
    Esp32BaseWeb::sendChunk("<h2>滤芯</h2><div class='filter-cards'>");
    std::uint32_t usedDaysByIndex[kFilterCount]{};
    std::size_t enabledCount = 0;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        if (!filter.enabled) {
            continue;
        }
        usedDaysByIndex[i] = filter.startTime >= kMinRealDateSeconds ? g_context.filters->usedDays(i, now) : 0;
        ++enabledCount;
    }
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        if (!filter.enabled) {
            continue;
        }
        const std::uint32_t usedDays = usedDaysByIndex[i];
        const std::uint32_t dayProgress = filterProgressPercent(filter, usedDays);
        const std::uint32_t flowProgress = filterFlowProgressPercent(filter);
        char usedFlow[24]{};
        char lifeFlow[24]{};
        char startDate[16]{};
        formatLiters(filter.usedMl, usedFlow, sizeof(usedFlow));
        formatLiters(filter.lifeMl, lifeFlow, sizeof(lifeFlow));
        formatDate(filter.startTime, startDate, sizeof(startDate));

        Esp32BaseWeb::sendChunk("<section class='filter-card'><div class='filter-head'><strong>");
        sendHtmlEscapedBounded(filter.name, sizeof(filter.name));
        sendFmt("</strong><span class='status-pill %s'>%s</span></div>"
                "<div class='dual-progress'><div class='filter-progress-row filter-progress-label'><b>天数</b>"
                "<span class='filter-track'><span class='filter-progress-fill day-progress' style='width:%lu%%'></span></span></div>",
                filterStatusClass(filter, usedDays),
                filterDisplayStatusText(filter, usedDays),
                static_cast<unsigned long>(dayProgress));
        if (filter.lifeMl > 0) {
            sendFmt("<div class='filter-progress-row filter-progress-label'><b>流量</b>"
                    "<span class='filter-track'><span class='filter-progress-fill flow-progress' style='width:%lu%%'></span></span></div>",
                    static_cast<unsigned long>(flowProgress));
        }
        Esp32BaseWeb::sendChunk("</div>");
        Esp32BaseWeb::sendChunk("<div class='filter-meta'>");
        if (filter.recommendDays > 0 && filter.maxDays > 0) {
            sendFmt("<span>天数：已用 %lu 天 / 建议 %lu 天 / 最长 %lu 天</span>",
                    static_cast<unsigned long>(usedDays),
                    static_cast<unsigned long>(filter.recommendDays),
                    static_cast<unsigned long>(filter.maxDays));
        } else if (filter.recommendDays > 0) {
            sendFmt("<span>天数：已用 %lu 天 / 建议 %lu 天</span>",
                    static_cast<unsigned long>(usedDays),
                    static_cast<unsigned long>(filter.recommendDays));
        } else if (filter.maxDays > 0) {
            sendFmt("<span>天数：已用 %lu 天 / 最长 %lu 天</span>",
                    static_cast<unsigned long>(usedDays),
                    static_cast<unsigned long>(filter.maxDays));
        } else {
            sendFmt("<span>天数：已用 %lu 天</span>", static_cast<unsigned long>(usedDays));
        }
        if (filter.lifeMl > 0) {
            sendFmt("<span>流量：已用 %s / 寿命 %s</span>", usedFlow, lifeFlow);
        } else {
            sendFmt("<span>流量：已用 %s</span>", usedFlow);
        }
        sendFmt("<span>上次更换 %s</span></div></section>", startDate[0] ? startDate : "未设置");
    }
    if (enabledCount == 0) {
        Esp32BaseWeb::sendChunk("<section class='filter-card muted'>当前没有启用的滤芯。</section>");
    }
    Esp32BaseWeb::sendChunk("</div>");
}

void handleFaucetPage() {
    if (!sendPageStart("首页")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const AppSnapshot snapshot = g_context.app->snapshot();
    const FaucetDisplayStatus displayStatus =
        g_context.currentDisplayStatus
            ? g_context.currentDisplayStatus()
            : FaucetDisplayStatus{DisplayFrame{DisplayPage::Sleep, false, {}, {}}, false};
    sendMachineStatusCard(snapshot, displayStatus.screenOn);
    sendTodayOverview(collectTodayOverview(g_context.nowSeconds(), snapshot.statistics.todayMl));
    sendFilterCards(g_context.nowSeconds());
    sendHomeAutoRefreshScript();
    sendPageEnd();
}

void handlePresetsPage() {
    if (!sendPageStart("出水预设")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }

    char text[24]{};
    std::uint32_t editIndex = UINT32_MAX;
    if (getParam("index", text, sizeof(text))) {
        parseU32(text, editIndex);
    }
    if (editIndex < kPresetCount) {
        const PresetConfig& preset = g_context.config->presets[editIndex];
        const bool timePreset = preset.type == PresetType::Time;
        sendFmt("<h2>预设 %u</h2><section class='panel'>"
                "<form method='post' action='/api/faucet/presets' onsubmit='return once(this)'>"
                "<input type='hidden' name='index' value='%u'><input type='hidden' name='return' value='/faucet/presets'>"
                "<div class='form-grid'>",
                static_cast<unsigned>(editIndex + 1),
                static_cast<unsigned>(editIndex));
        sendNoticeFromQuery();
        Esp32BaseWeb::sendChunk("<div class='span-2'>");
        sendCheckbox("启用", "enabled", preset.enabled);
        Esp32BaseWeb::sendChunk("</div><label class='field span-5'><span>名称</span><input name='name' maxlength='15' value='");
        sendHtmlAttrEscapedBounded(preset.name, sizeof(preset.name));
        Esp32BaseWeb::sendChunk("'><small class='hint'>最多 15 个字符</small></label><label class='field span-2'><span>类型</span><select id='presetType' name='type' onchange='presetTypeChanged()'>");
        sendFmt("<option value='volume'%s>容量</option>", preset.type == PresetType::Volume ? " selected" : "");
        sendFmt("<option value='time'%s>时间</option>", preset.type == PresetType::Time ? " selected" : "");
        Esp32BaseWeb::sendChunk("</select></label>");
        Esp32BaseWeb::sendChunk("<label class='field span-3'><span id='presetValueLabel'>");
        Esp32BaseWeb::sendChunk(timePreset ? "时长（秒）" : "出水量（ml）");
        sendFmt("</span><input id='presetValue' name='value' type='number' min='%lu' max='%lu' step='%lu' value='%lu'>",
                static_cast<unsigned long>(timePreset ? kMinTimePresetSec : kMinVolumePresetMl),
                static_cast<unsigned long>(timePreset ? kMaxTimePresetSec : kMaxVolumePresetMl),
                static_cast<unsigned long>(timePreset ? 1UL : 100UL),
                static_cast<unsigned long>(preset.value));
        Esp32BaseWeb::sendChunk("<small id='presetValueHint' class='hint'>");
        Esp32BaseWeb::sendChunk(timePreset ? "按秒计时，到时自动关阀" : "按毫升计量，到量自动关阀");
        Esp32BaseWeb::sendChunk("</small></label>");
        Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'>"
                                "<a href='/faucet/presets'>取消</a></div></form></section>"
                                "<script>"
                                "function presetTypeChanged(){"
                                "var t=document.getElementById('presetType').value;"
                                "var v=document.getElementById('presetValue');"
                                "document.getElementById('presetValueLabel').textContent=t=='time'?'时长（秒）':'出水量（ml）';"
                                "document.getElementById('presetValueHint').textContent=t=='time'?'按秒计时，到时自动关阀':'按毫升计量，到量自动关阀';"
                                "v.min=t=='time'?'5':'100';v.max=t=='time'?'1800':'20000';v.step=t=='time'?'1':'100';"
                                "}"
                                "</script>");
        sendPageEnd();
        return;
    }

    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<h2>预设</h2><table class='compact-table'><tr>"
                            "<th>序号</th><th>名称</th><th>类型</th><th>目标值</th><th>状态</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const PresetConfig& preset = g_context.config->presets[i];
        sendFmt("<tr%s><td>%u</td><td>",
                preset.enabled ? "" : " class='disabled-row'",
                static_cast<unsigned>(i + 1));
        sendHtmlEscapedBounded(preset.name, sizeof(preset.name));
        sendFmt("</td><td>%s</td><td>",
                preset.type == PresetType::Time ? "时间" : "容量");
        if (preset.type == PresetType::Time) {
            sendFmt("%lu 秒", static_cast<unsigned long>(preset.value));
        } else {
            sendLiters(preset.value);
        }
        sendFmt("</td><td><span class='status-pill'>%s</span></td>"
                "<td><div class='row-actions'><a href='/faucet/presets?index=%u'>编辑</a></div></td></tr>",
                preset.enabled ? "启用" : "停用",
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleStatsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }
    if (waterTaskActive()) {
        sendPageStart("统计");
        Esp32BaseWeb::sendChunk("<h2>统计</h2><section class='panel'><p class='err'>出水任务进行中，暂不生成统计报表。</p></section>");
        sendPageEnd();
        return;
    }
    const std::uint32_t now = g_context.nowSeconds();
    const WaterUsageSummary summary = aggregateWaterRecords(*g_context.records, now, kChartDays);
    const AppSnapshot snapshot = g_context.app->snapshot();
    char today[24]{};
    char month[24]{};
    char total[24]{};
    char average30[24]{};
    char monthRange[32]{};
    formatLiters(summary.todayMl, today, sizeof(today));
    formatLiters(summary.monthMl, month, sizeof(month));
    formatLiters(snapshot.statistics.totalMl, total, sizeof(total));
    formatLiters(summary.last30DaysDailyAverageMl, average30, sizeof(average30));
    formatMonthRange(summary, monthRange, sizeof(monthRange));
    Esp32BaseWeb::sendHeader("用水统计");
    Esp32BaseWeb::sendChunk("<h2>统计</h2>");
    if (summary.unknownCount > 0) {
        sendFmt("<p class='warn'>⚠ 含 %lu 条无时间记录，未纳入按日期图表。</p>",
                static_cast<unsigned long>(summary.unknownCount));
    }
    Esp32BaseWeb::sendChunk("<div class='stats-layout'><div><div class='metric-grid'>");
    sendMetricCard("今日", today);
    char monthTitle[48]{};
    std::snprintf(monthTitle, sizeof(monthTitle), "本月%s%s", monthRange[0] ? " " : "", monthRange);
    sendMetricCard(monthTitle, month);
    sendMetricCard("过去 30 天日均", average30);
    sendMetricCard("总累计", total);
    Esp32BaseWeb::sendChunk("</div></div>");
    if (now >= kMinRealDateSeconds) {
        sendDailyChart(summary);
    } else {
        sendTimeUnsyncedChartNotice();
    }
    Esp32BaseWeb::sendChunk("</div><div class='usage-grid'>");
    sendUsagePatterns(summary, *g_context.config);
    Esp32BaseWeb::sendChunk("</div>");
    sendPageEnd();
}

void handleRecordsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }
    char text[24]{};
    std::uint32_t page = 0;
    if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    std::uint32_t requestedPageSize = kDefaultRecordPageSize;
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, requestedPageSize);
    }
    const std::uint16_t pageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(requestedPageSize));
    WaterRecordFilter filter{};
    char startDate[16]{};
    char endDate[16]{};
    if (getParam("startDate", startDate, sizeof(startDate))) {
        if (!parseDate(startDate, filter.startTime)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=invalid_date");
            return;
        }
        filter.hasStart = filter.startTime > 0;
    }
    if (getParam("endDate", endDate, sizeof(endDate))) {
        if (!parseDate(endDate, filter.endTime)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=invalid_date");
            return;
        }
        filter.hasEnd = filter.endTime > 0;
        if (filter.hasEnd) {
            filter.endTime = UINT32_MAX - filter.endTime < 86399UL ? UINT32_MAX : filter.endTime + 86399UL;
        }
    }
    if (filter.hasStart && filter.hasEnd && filter.endTime < filter.startTime) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=invalid_date");
        return;
    }
    if ((filter.hasStart || filter.hasEnd) && waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=busy");
        return;
    }
    char filterQuery[96]{};
    if (filter.hasStart || filter.hasEnd) {
        char normalizedStart[16]{};
        char normalizedEnd[16]{};
        if (filter.hasStart) {
            formatDate(filter.startTime, normalizedStart, sizeof(normalizedStart));
        }
        if (filter.hasEnd) {
            formatDate(filter.endTime, normalizedEnd, sizeof(normalizedEnd));
        }
        std::snprintf(filterQuery,
                      sizeof(filterQuery),
                      "&startDate=%s&endDate=%s",
                      normalizedStart,
                      normalizedEnd);
    }
    WaterRecord* records = new (std::nothrow) WaterRecord[pageSize]{};
    if (!records) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    const bool ready = g_context.records->ready();
    std::size_t total = ready ? g_context.records->count() : 0;
    std::size_t count =
        ready ? queryWaterRecords(*g_context.records, filter, page, pageSize, records, pageSize, &total) : 0;
    const std::uint32_t filteredMaxPage = total == 0 ? 0 : static_cast<std::uint32_t>((total - 1) / pageSize);
    if (page > filteredMaxPage) {
        page = filteredMaxPage;
        count = ready ? queryWaterRecords(*g_context.records, filter, page, pageSize, records, pageSize, &total) : 0;
    }
    Esp32BaseWeb::sendHeader("出水记录");
    Esp32BaseWeb::sendChunk("<h2>记录</h2>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<div class='records-top-grid'>");
    sendSegmentedMeteringPanel();
    sendPulseTraceCachePanel();
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::sendChunk("<div class='pager'><div class='pager-links'>");
    const bool hasPrev = ready && total > 0 && page > 0;
    const bool hasNext = ready && total > 0 && page < filteredMaxPage;
    if (hasPrev) {
        sendFmt("<a class='page-link' href='/faucet/records?page=0&pageSize=%u%s'>首页</a>"
                "<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u%s'>上一页</a>",
                static_cast<unsigned>(pageSize),
                filterQuery,
                static_cast<unsigned long>(page - 1),
                static_cast<unsigned>(pageSize),
                filterQuery);
    } else {
        Esp32BaseWeb::sendChunk("<span class='page-link page-disabled'>首页</span><span class='page-link page-disabled'>上一页</span>");
    }
    sendFmt("<span class='page-current'>第 %lu / %lu 页</span>",
            static_cast<unsigned long>(page + 1),
            static_cast<unsigned long>(filteredMaxPage + 1));
    if (hasNext) {
        sendFmt("<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u%s'>下一页</a>"
                "<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u%s'>末页</a>",
                static_cast<unsigned long>(page + 1),
                static_cast<unsigned>(pageSize),
                filterQuery,
                static_cast<unsigned long>(filteredMaxPage),
                static_cast<unsigned>(pageSize),
                filterQuery);
    } else {
        Esp32BaseWeb::sendChunk("<span class='page-link page-disabled'>下一页</span><span class='page-link page-disabled'>末页</span>");
    }
    char shownStart[16]{};
    char shownEnd[16]{};
    if (filter.hasStart) {
        formatDate(filter.startTime, shownStart, sizeof(shownStart));
    }
    if (filter.hasEnd) {
        formatDate(filter.endTime, shownEnd, sizeof(shownEnd));
    }
    sendFmt("</div><form class='page-size' method='get' action='/faucet/records'>"
            "<input type='hidden' name='page' value='0'><span>开始</span><input type='date' name='startDate' value='%s'>"
            "<span>结束</span><input type='date' name='endDate' value='%s'><span>每页</span><select name='pageSize' onchange='this.form.submit()'>",
            shownStart,
            shownEnd);
    constexpr std::uint16_t sizes[] = {20, 30, 50, 100, 200};
    for (std::uint16_t size : sizes) {
        sendFmt("<option value='%u'%s>%u</option>",
                static_cast<unsigned>(size),
                pageSize == size ? " selected" : "",
                static_cast<unsigned>(size));
    }
    sendFmt("</select><span>条</span><input class='secondary' type='submit' value='筛选'></form></div><p class='hint'>共 %lu 条记录</p>",
            static_cast<unsigned long>(total));
    if (!ready) {
        Esp32BaseWeb::sendChunk("<p class='err'>记录存储不可用。</p>");
    } else if (total == 0) {
        Esp32BaseWeb::sendChunk("<p class='ok'>暂无出水记录。</p>");
    }
    WaterRecord newestRecord{};
    const bool newestRecordReady = ready && g_context.records->readPage(0, 1, &newestRecord, 1) == 1;
    Esp32BaseWeb::sendChunk("<table><tr><th>时间</th><th>模式</th><th>目标</th><th>出水</th>"
                            "<th>用时</th><th>脉冲</th><th>脉冲/升</th><th>结果</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < count; ++i) {
        char startTime[40]{};
        formatWaterRecordTime(records[i], startTime, sizeof(startTime));
        const bool latestRecord = newestRecordReady && sameWaterRecordIdentity(records[i], newestRecord);
        const bool canCalibrate = latestRecord && waterRecordCanCalibrate(records[i]);
        WaterRecordCalibration calibration{};
        const bool calibrated = findRecordCalibration(records[i], calibration);
        const WaterPulseTrace* trace = g_context.pulseTraces ? g_context.pulseTraces->findByRecord(records[i]) : nullptr;
        const std::uint32_t pulsePerLiter = pulsePerLiterFromPulsePerMl(records[i].pulsePerMlAtRun);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::sendChunk(startTime);
        if (trace) {
            sendFmt("<a class='trace-badge' href='/faucet/records/detail?trace=%lu&bucket=1'>明细</a>",
                    static_cast<unsigned long>(trace->traceId));
        }
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(modeText(records[i].mode));
        Esp32BaseWeb::sendChunk("</td><td>");
        sendTargetValue(records[i]);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendLiters(records[i].volumeMl);
        if (calibrated) {
            Esp32BaseWeb::sendChunk("<span class='inline-note ok'>实测 ");
            sendLiters(calibration.actualMl);
            Esp32BaseWeb::sendChunk("</span>");
        }
        sendFmt("</td><td>%u s</td><td class='pulse-cell'>%luP",
                static_cast<unsigned>(records[i].durationSec),
                static_cast<unsigned long>(records[i].pulseCount));
        sendFmt("</td><td>%luP/L", static_cast<unsigned long>(pulsePerLiter));
        if (records[i].rejectedPulseCount > 0) {
            sendFmt("<span class='inline-note'>滤 %luP</span>",
                    static_cast<unsigned long>(records[i].rejectedPulseCount));
        }
        sendFmt("</td><td><span class='status-pill %s'>%s</span>",
                resultStatusClass(records[i].result),
                resultText(records[i].result));
        Esp32BaseWeb::sendChunk("</td><td><div class='row-actions'>");
        if (canCalibrate) {
            sendFmt("<a class='btn-link' href='/faucet/records/calibration'>%s</a>", calibrated ? "重校" : "校准");
        } else if (calibrated) {
            Esp32BaseWeb::sendChunk("<span class='status-pill status-ok'>已校准</span>");
        } else {
            Esp32BaseWeb::sendChunk("<span class='muted'>-</span>");
        }
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    delete[] records;
    sendPageEnd();
}

void handleRecordCalibrationPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }

    WaterRecord record{};
    const bool canCalibrate =
        g_context.records && g_context.records->ready() && g_context.records->readPage(0, 1, &record, 1) == 1 &&
        waterRecordCanCalibrate(record);

    Esp32BaseWeb::sendHeader("记录校准");
    Esp32BaseWeb::sendChunk("<h2>记录校准</h2>");
    if (!canCalibrate) {
        Esp32BaseWeb::sendChunk("<p class='err'>最新记录不可校准。</p><p><a class='btn-link' href='/faucet/records'>返回记录</a></p>");
        sendPageEnd();
        return;
    }

    char startTime[40]{};
    formatWaterRecordTime(record, startTime, sizeof(startTime));
    WaterRecordCalibration calibration{};
    const bool calibrated = findRecordCalibration(record, calibration);
    const std::uint32_t defaultActualMl = calibrated ? calibration.actualMl : record.volumeMl;
    const std::uint32_t pulsePerLiter = pulsePerLiterFromPulsePerMl(record.pulsePerMlAtRun);
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>开始时间</th><td>");
    Esp32BaseWeb::sendChunk(startTime);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>校准状态</th><td>");
    if (calibrated) {
        sendFmt("<span class='status-pill status-ok'>已校准</span><span class='hint'>最近实测 ");
        sendLiters(calibration.actualMl);
        sendFmt("，第 %u 次校准</span>", static_cast<unsigned>(calibration.calibrationCount));
    } else {
        Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>未校准</span>");
    }
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>目标</th><td>");
    sendTargetValue(record);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>出水量</th><td>");
    sendLiters(record.volumeMl);
    sendTargetDeltaHint(record);
    sendFmt("</td></tr><tr><th>脉冲数</th><td>%lu</td></tr>"
            "<tr><th>过滤脉冲</th><td>%lu</td></tr>"
            "<tr><th>当时脉冲/升</th><td>%luP/L</td></tr>"
            "<tr><th>持续时间</th><td>%u s</td></tr>"
            "<tr><th>结束原因</th><td>%s</td></tr></table>",
            static_cast<unsigned long>(record.pulseCount),
            static_cast<unsigned long>(record.rejectedPulseCount),
            static_cast<unsigned long>(pulsePerLiter),
            static_cast<unsigned>(record.durationSec),
            resultText(record.result));
    if (calibrated) {
        char calibratedAt[40]{};
        formatWaterRecordTime(WaterRecord{calibration.calibratedAt,
                                          0,
                                          0,
                                          0,
                                          0,
                                          0,
                                          WaterMode::Volume,
                                          WaterResult::Completed,
                                          0,
                                          0,
                                          0.0f,
                                          {0, 0, 0, 0}},
                              calibratedAt,
                              sizeof(calibratedAt));
        const std::int32_t measuredDiff =
            static_cast<std::int32_t>(record.volumeMl) - static_cast<std::int32_t>(calibration.actualMl);
        Esp32BaseWeb::sendChunk("<section class='panel'><h3>上次校准</h3><table class='kv'>");
        Esp32BaseWeb::sendChunk("<tr><th>实测水量</th><td>");
        sendLiters(calibration.actualMl);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>实测差</th><td>");
        sendSignedLiters(measuredDiff);
        sendFmt("</td></tr><tr><th>校准类型</th><td>%s</td></tr>",
                calibrationKindText(calibration.kind));
        if (calibration.kind == WaterRecordCalibrationKind::StartupCompensation) {
            sendFmt("<tr><th>参数变化</th><td>启动补偿 %lu ml → %lu ml</td></tr>",
                    static_cast<unsigned long>(calibration.oldStartupCompensationMl),
                    static_cast<unsigned long>(calibration.newStartupCompensationMl));
        } else {
            sendFmt("<tr><th>参数变化</th><td>%luP/L → %luP/L</td></tr>",
                    static_cast<unsigned long>(pulsePerLiterFromPulsePerMl(calibration.oldPulsePerMl)),
                    static_cast<unsigned long>(pulsePerLiterFromPulsePerMl(calibration.newPulsePerMl)));
        }
        sendFmt("<tr><th>校准时间</th><td>%s</td></tr></table></section>",
                calibratedAt[0] ? calibratedAt : "未知");
    }
    Esp32BaseWeb::sendChunk("<section class='panel'><h3>实测出水量</h3>"
                            "<form method='post' action='/api/faucet/records/calibration' onsubmit='return once(this)'>"
                            "<label class='field'><span>实测出水量 (ml)</span>");
    sendFmt("<input name='actualMl' type='number' min='%lu' max='%lu' step='10' value='%lu'></label>",
            static_cast<unsigned long>(kMinVolumePresetMl),
            static_cast<unsigned long>(kMaxVolumePresetMl),
            static_cast<unsigned long>(defaultActualMl));
    Esp32BaseWeb::sendChunk("<p class='hint'>保存后会记录这条出水的实测出水量，并在记录页显示实测值和校准状态；分段计量请在曲线详情中用多条实测样本自动校准。</p>"
                            "<div class='form-actions'><input type='submit' value='");
    Esp32BaseWeb::sendChunk(calibrated ? "重新保存实测量" : "保存实测量");
    Esp32BaseWeb::sendChunk("'><a class='btn-link' href='/faucet/records'>取消</a></div></form></section>");
    sendPageEnd();
}

void handleRecordDetailPage() {
    if (!sendPageStart("脉冲明细")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    if (!g_context.pulseTraces) {
        Esp32BaseWeb::sendChunk("<h2>脉冲明细</h2><p class='err'>脉冲明细缓存不可用。</p><p><a class='btn-link' href='/faucet/records'>返回记录</a></p>");
        sendPageEnd();
        return;
    }
    char text[24]{};
    std::uint32_t traceId = 0;
    if (!getParam("trace", text, sizeof(text)) || !parseU32(text, traceId)) {
        Esp32BaseWeb::sendChunk("<h2>脉冲明细</h2><p class='err'>明细编号无效。</p><p><a class='btn-link' href='/faucet/records'>返回记录</a></p>");
        sendPageEnd();
        return;
    }
    std::uint32_t bucketSeconds = 1;
    if (getParam("bucket", text, sizeof(text))) {
        parseU32(text, bucketSeconds);
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }
    const WaterPulseTrace* trace = g_context.pulseTraces->findById(traceId);
    if (!trace) {
        Esp32BaseWeb::sendChunk("<h2>脉冲明细</h2><p class='err'>该脉冲明细已被 RAM 缓存淘汰。</p><p><a class='btn-link' href='/faucet/records'>返回记录</a></p>");
        sendPageEnd();
        return;
    }

    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{};
    WaterPulseTraceBucket* buckets = new (std::nothrow) WaterPulseTraceBucket[trace->sampleCount]{};
    if (!samples || !buckets) {
        delete[] samples;
        delete[] buckets;
        Esp32BaseWeb::sendChunk("<p class='err'>内存不足，无法生成脉冲明细。</p>");
        sendPageEnd();
        return;
    }
    for (std::size_t i = 0; i < trace->sampleCount; ++i) {
        const WaterPulseTraceSample* sample = g_context.pulseTraces->sampleAt(*trace, i);
        samples[i] = sample ? *sample : WaterPulseTraceSample{};
    }
    const std::size_t bucketCount =
        aggregateWaterPulseTrace(*trace, samples, trace->sampleCount, bucketSeconds, buckets, trace->sampleCount);
    const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(*trace, samples, trace->sampleCount);

    char startTime[40]{};
    formatWaterRecordTime(trace->record, startTime, sizeof(startTime));
    const std::size_t traceBytes = sizeof(WaterPulseTrace) + trace->sampleCount * sizeof(WaterPulseTraceSample);
    char traceKb[24]{};
    formatKb(traceBytes, traceKb, sizeof(traceKb));
    Esp32BaseWeb::sendChunk("<h2>脉冲明细</h2><p><a class='btn-link' href='/faucet/records'>返回记录</a></p>");
    Esp32BaseWeb::sendChunk("<section class='panel'><h3>明细概况</h3><table class='kv'>");
    sendFmt("<tr><th>开始时间</th><td>%s</td></tr>"
            "<tr><th>目标</th><td>",
            startTime);
    sendTargetValue(trace->record);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>固件估算</th><td>");
    sendLiters(trace->record.volumeMl);
    sendFmt("</td></tr><tr><th>总脉冲</th><td>%lu</td></tr>"
            "<tr><th>持续时间</th><td>%lu s</td></tr>"
            "<tr><th>样本数</th><td>%lu</td></tr>"
            "<tr><th>本条占用</th><td>%s</td></tr>",
            static_cast<unsigned long>(trace->totalPulses),
            static_cast<unsigned long>(trace->sampleCount),
            static_cast<unsigned long>(trace->sampleCount),
            traceKb);
    if (analysis.stable) {
        sendFmt("<tr><th>稳态开始</th><td>第 %lu 秒，启动段 %lu 脉冲，稳态 %.2f P/s，置信度 %u%%</td></tr>",
                static_cast<unsigned long>(analysis.stableStartSec),
                static_cast<unsigned long>(analysis.startupPulseCount),
                static_cast<double>(analysis.stablePulsePerSec),
                static_cast<unsigned>(analysis.confidence));
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>稳态识别</th><td>无法识别</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table></section>");

    Esp32BaseWeb::sendChunk("<section class='panel'><div class='panel-head'><h3>聚合频率</h3><div class='row-actions'>");
    constexpr std::uint32_t bucketsToShow[] = {1, 2, 3, 5};
    for (std::uint32_t bucket : bucketsToShow) {
        sendFmt("<a class='btn-link' href='/faucet/records/detail?trace=%lu&bucket=%lu'>%lus</a>",
                static_cast<unsigned long>(traceId),
                static_cast<unsigned long>(bucket),
                static_cast<unsigned long>(bucket));
    }
    std::uint32_t maxDelta = 1;
    std::uint32_t maxCumulative = 1;
    for (std::size_t i = 0; i < bucketCount; ++i) {
        maxDelta = std::max(maxDelta, buckets[i].pulseDelta);
        maxCumulative = std::max(maxCumulative, buckets[i].cumulativePulses);
    }
    const std::uint32_t left = 54;
    const std::uint32_t top = 28;
    const std::uint32_t baseY = 224;
    const std::uint32_t chartHeight = 176;
    const std::uint32_t chartWidth = 900;
    const std::uint32_t maxEndSec =
        bucketCount == 0 ? 1 : std::max<std::uint32_t>(1, buckets[bucketCount - 1].startSec + buckets[bucketCount - 1].durationSec);
    Esp32BaseWeb::sendChunk("</div></div><div class='pulse-detail-chart'><svg viewBox='0 0 1000 300' role='img' aria-label='脉冲明细折线图'>"
                            "<line class='axis' x1='54' y1='224' x2='954' y2='224'></line>"
                            "<line class='axis' x1='54' y1='28' x2='54' y2='224'></line>");
    for (std::uint32_t i = 1; i <= 4; ++i) {
        const std::uint32_t y = baseY - (chartHeight * i) / 4;
        sendFmt("<line class='grid-line' x1='54' y1='%lu' x2='954' y2='%lu'></line>",
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y));
    }
    sendFmt("<text class='chart-label' x='54' y='248'>0s</text>"
            "<text class='chart-label' x='918' y='248'>%lus</text>"
            "<text class='chart-label' x='58' y='20'>每桶最高 %luP / 累计 %luP</text>",
            static_cast<unsigned long>(maxEndSec),
            static_cast<unsigned long>(maxDelta),
            static_cast<unsigned long>(maxCumulative));
    Esp32BaseWeb::sendChunk("<polyline class='pulse-line' points='");
    sendFmt("%lu,%lu", static_cast<unsigned long>(left), static_cast<unsigned long>(baseY));
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t x = left + (endSec * chartWidth) / maxEndSec;
        const std::uint32_t y = baseY - (buckets[i].pulseDelta * chartHeight) / maxDelta;
        sendFmt(" %lu,%lu", static_cast<unsigned long>(x), static_cast<unsigned long>(y));
    }
    Esp32BaseWeb::sendChunk("'></polyline><polyline class='cum-line' points='");
    sendFmt("%lu,%lu", static_cast<unsigned long>(left), static_cast<unsigned long>(baseY));
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t x = left + (endSec * chartWidth) / maxEndSec;
        const std::uint32_t y = baseY - (buckets[i].cumulativePulses * chartHeight) / maxCumulative;
        sendFmt(" %lu,%lu", static_cast<unsigned long>(x), static_cast<unsigned long>(y));
    }
    Esp32BaseWeb::sendChunk("'></polyline>");
    if (analysis.stable) {
        const std::uint32_t stableX = left + (analysis.stableStartSec * chartWidth) / maxEndSec;
        sendFmt("<line class='stable-line' x1='%lu' y1='%lu' x2='%lu' y2='%lu'></line>"
                "<text class='chart-label' x='%lu' y='42'>稳态 %lus</text>",
                static_cast<unsigned long>(stableX),
                static_cast<unsigned long>(top),
                static_cast<unsigned long>(stableX),
                static_cast<unsigned long>(baseY),
                static_cast<unsigned long>(stableX + 6),
                static_cast<unsigned long>(analysis.stableStartSec));
    }
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t x = left + (endSec * chartWidth) / maxEndSec;
        const std::uint32_t y = baseY - (buckets[i].pulseDelta * chartHeight) / maxDelta;
        sendFmt("<circle class='pulse-dot' cx='%lu' cy='%lu' r='2.6'><title>%lu-%lus: %luP / 累计 %luP / %s</title></circle>",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(buckets[i].startSec),
                static_cast<unsigned long>(endSec),
                static_cast<unsigned long>(buckets[i].pulseDelta),
                static_cast<unsigned long>(buckets[i].cumulativePulses),
                traceStateText(buckets[i].state));
    }
    Esp32BaseWeb::sendChunk("</svg></div><div class='chart-legend'>"
                            "<span><i class='legend-mark legend-pulse'></i>每桶脉冲</span>"
                            "<span><i class='legend-mark legend-cum'></i>累计脉冲</span>"
                            "<span><i class='legend-mark legend-stable'></i>稳态开始</span>"
                            "</div></section><details class='panel detail-data'><summary>查看明细数据</summary>"
                            "<table><tr><th>时间</th><th>脉冲</th><th>累计</th><th>状态</th></tr>");
    for (std::size_t i = 0; i < bucketCount; ++i) {
        sendFmt("<tr><td>%lu-%lus</td><td>%luP</td><td>%luP</td><td>%s</td></tr>",
                static_cast<unsigned long>(buckets[i].startSec),
                static_cast<unsigned long>(buckets[i].startSec + buckets[i].durationSec),
                static_cast<unsigned long>(buckets[i].pulseDelta),
                static_cast<unsigned long>(buckets[i].cumulativePulses),
                traceStateText(buckets[i].state));
    }
    Esp32BaseWeb::sendChunk("</table></details>");

    const std::uint32_t defaultActualMl = trace->actualMl > 0 ? trace->actualMl : trace->record.volumeMl;
    Esp32BaseWeb::sendChunk("<section class='panel'><h3>实测容量与自动校准</h3>"
                            "<form method='post' action='/api/faucet/records/trace-calibration' onsubmit='return once(this)'>");
    sendFmt("<input type='hidden' name='trace' value='%lu'><label class='field'><span>实测出水量 (ml)</span>"
            "<input name='actualMl' type='number' min='%lu' max='%lu' step='10' value='%lu'></label>",
            static_cast<unsigned long>(traceId),
            static_cast<unsigned long>(kMinVolumePresetMl),
            static_cast<unsigned long>(kMaxVolumePresetMl),
            static_cast<unsigned long>(defaultActualMl));
    Esp32BaseWeb::sendChunk("<input type='hidden' name='manualSamples' value='1'><table><tr><th>用于自动校准</th><th>明细</th><th>实测</th><th>脉冲</th></tr>");
    for (std::size_t offset = 0; offset < g_context.pulseTraces->count(); ++offset) {
        const std::size_t traceIndex = g_context.pulseTraces->count() - 1 - offset;
        const WaterPulseTrace* candidate = g_context.pulseTraces->traceAt(traceIndex);
        if (!candidate || candidate->actualMl == 0 || candidate->totalPulses == 0) {
            continue;
        }
        char candidateTime[40]{};
        formatWaterRecordTime(candidate->record, candidateTime, sizeof(candidateTime));
        sendFmt("<tr><td><label class='check-line'><input type='checkbox' name='sample_%lu' checked>使用</label></td><td>%s</td><td>",
                static_cast<unsigned long>(candidate->traceId),
                candidateTime[0] ? candidateTime : "-");
        sendLiters(candidate->actualMl);
        sendFmt("</td><td>%luP</td></tr>", static_cast<unsigned long>(candidate->totalPulses));
    }
    Esp32BaseWeb::sendChunk("</table><p class='hint'>保存后会把本条脉冲明细作为分段校准样本；至少两条容量差异明显的有效样本可生成启动段和稳态段参数。</p>"
                            "<div class='form-actions'><input type='submit' value='保存并自动校准'></div></form></section>");
    delete[] samples;
    delete[] buckets;
    sendPageEnd();
}

void handleFiltersPage() {
    if (!sendPageStart("滤芯状态")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const std::uint32_t now = g_context.nowSeconds();
    char todayDate[16]{};
    formatDate(now >= kMinRealDateSeconds ? now : 0, todayDate, sizeof(todayDate));
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<h2>滤芯</h2><table class='filters-table'><tr><th>名称</th><th>启用状态</th><th>已用天数 (天)</th><th>已用流量 (L)</th><th>寿命规则</th><th>状态</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        const std::uint32_t usedDays = filter.startTime >= kMinRealDateSeconds ? g_context.filters->usedDays(i, now) : 0;
        char life[32]{};
        formatLifeRange(filter, life, sizeof(life));
        sendFmt("<tr%s data-filter-start='%lu' data-filter-boot='%lu' data-filter-enabled='%u' data-filter-recommend-days='%lu' data-filter-max-days='%lu' data-filter-life-ml='%lu' data-filter-used-ml='%lu'><td>",
                filter.enabled ? "" : " class='disabled-row'",
                static_cast<unsigned long>(filter.startTime),
                static_cast<unsigned long>(filter.startBootId),
                filter.enabled ? 1U : 0U,
                static_cast<unsigned long>(filter.recommendDays),
                static_cast<unsigned long>(filter.maxDays),
                static_cast<unsigned long>(filter.lifeMl),
                static_cast<unsigned long>(filter.usedMl));
        sendHtmlEscapedBounded(filter.name, sizeof(filter.name));
        sendFmt("</td><td>%s</td><td class='filter-used-days'>%lu 天</td><td>",
                filter.enabled ? "启用" : "停用",
                static_cast<unsigned long>(usedDays));
        sendLiters(filter.usedMl);
        sendFmt("</td><td>%s", life);
        Esp32BaseWeb::sendChunk(" / ");
        if (filter.lifeMl > 0) {
            sendLiters(filter.lifeMl);
        } else {
            Esp32BaseWeb::sendChunk("未设置流量");
        }
        sendFmt("</td><td><span class='status-pill filter-status %s'>%s</span></td><td><div class='row-actions'><a href='/faucet/filters/edit?index=%u'>设置</a>",
                filterStatusClass(filter, usedDays),
                filterDisplayStatusText(filter, usedDays),
                static_cast<unsigned>(i));
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/faucet/filters/reset' data-filter-name='");
        sendHtmlAttrEscapedBounded(filter.name, sizeof(filter.name));
        Esp32BaseWeb::sendChunk("' data-reset-date='");
        sendHtmlAttrEscaped(todayDate);
        Esp32BaseWeb::sendChunk("' onsubmit=\"return confirmFilterReset(this)&&once(this)\">");
        sendFmt("<input type='hidden' name='index' value='%u'>", static_cast<unsigned>(i));
        Esp32BaseWeb::sendChunk("<input class='secondary' type='submit' value='重置'></form></div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table><script>"
                            "function confirmFilterReset(form){"
                            "var name=form.getAttribute('data-filter-name')||'滤芯';"
                            "var date=form.getAttribute('data-reset-date')||'';"
                            "var msg=date?('确认将【'+name+'】更换日期重置为 '+date+'，并清空已用流量？'):"
                            "('当前时间未同步。确认将【'+name+'】更换日期记录为本次开机未知时间，并清空已用流量？');"
                            "return confirm(msg);"
                            "}"
                            "(function(){"
                            "var unix2000=946684800;"
                            "document.querySelectorAll('tr[data-filter-start]').forEach(function(row){"
                            "var start=parseInt(row.getAttribute('data-filter-start')||'0',10);"
                            "var boot=parseInt(row.getAttribute('data-filter-boot')||'0',10);"
                            "if(boot){return;}"
                            "if(!start){return;}"
                            "var now=Math.floor(Date.now()/1000)-unix2000;"
                            "if(!isFinite(now)||now<=start){return;}"
                            "var days=Math.floor((now-start)/86400);"
                            "var used=row.querySelector('.filter-used-days');"
                            "if(used){used.textContent=days+' 天';}"
                            "var status=row.querySelector('.filter-status');"
                            "if(!status){return;}"
                            "var enabled=row.getAttribute('data-filter-enabled')==='1';"
                            "var rec=parseInt(row.getAttribute('data-filter-recommend-days')||'0',10);"
                            "var max=parseInt(row.getAttribute('data-filter-max-days')||'0',10);"
                            "var life=parseInt(row.getAttribute('data-filter-life-ml')||'0',10);"
                            "var flow=parseInt(row.getAttribute('data-filter-used-ml')||'0',10);"
                            "var text='正常';"
                            "var cls='status-ok';"
                            "if(!enabled){text='停用';}"
                            "if(!enabled){cls='status-muted';}"
                            "else if((life>0&&flow>=life)||(max>0&&days>=max)){text='已超期';cls='status-error';}"
                            "else if(rec>0&&days>=rec){text='建议更换';cls='status-warn';}"
                            "status.textContent=text;"
                            "status.classList.remove('status-ok','status-warn','status-error','status-muted');"
                            "status.classList.add(cls);"
                            "});"
                            "})();"
                            "</script>");
    sendPageEnd();
}

void handleFilterEditPage() {
    if (!sendPageStart("滤芯设置")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }

    char text[24]{};
    std::uint32_t index = 0;
    if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
        Esp32BaseWeb::sendChunk("<h2>滤芯设置</h2><section class='panel'><p>滤芯编号无效。</p>"
                                "<p><a href='/faucet/filters'>返回滤芯列表</a></p></section>");
        sendPageEnd();
        return;
    }

    const FilterRecord& filter = g_context.filters->record(index);
    sendNoticeFromQuery();
    sendFmt("<h2>第 %u 级滤芯设置</h2>"
            "<section class='panel'><form method='post' action='/api/faucet/filters' onsubmit='return once(this)'>"
            "<input type='hidden' name='index' value='%u'><input type='hidden' name='return' value='/faucet/filters'>"
            "<div class='form-grid'>",
            static_cast<unsigned>(index + 1),
            static_cast<unsigned>(index));
    Esp32BaseWeb::sendChunk("<div class='span-2'>");
    sendCheckbox("启用", "enabled", filter.enabled);
    Esp32BaseWeb::sendChunk("</div><label class='field span-6'><span>名称</span><input name='name' maxlength='");
    sendFmt("%u", static_cast<unsigned>(kFilterNameMaxChars));
    Esp32BaseWeb::sendChunk("' value='");
    sendHtmlAttrEscapedBounded(filter.name, sizeof(filter.name));
    Esp32BaseWeb::sendChunk("'><small class='hint'>最多 30 个字符，用于列表和状态页显示</small></label>"
                            "<div class='span-4'></div>");
    Esp32BaseWeb::sendChunk("<div class='span-3'>");
    sendMonthInput("建议更换周期（月）", "recommendMonths", filter.recommendDays);
    Esp32BaseWeb::sendChunk("</div><div class='span-3'>");
    sendMonthInput("最长使用周期（月）", "maxMonths", filter.maxDays);
    Esp32BaseWeb::sendChunk("</div><div class='span-3'>");
    sendVolumeInput("寿命流量（ml）", "lifeMl", filter.lifeMl);
    Esp32BaseWeb::sendChunk("<small class='hint'>0 表示不按流量判断</small></div><div class='span-3'>");
    sendDateInput("上次更换日期", "startDate", filter.startTime);
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'>"
                            "<a href='/faucet/filters'>取消</a></div></form></section>");
    sendPageEnd();
}

void handleApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendJson(200, "{\"ok\":true,\"waterControl\":false}");
}

bool sendJsonBuffer(bool ok, const char* json) {
    if (!ok) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"buffer_too_small\"}");
        return false;
    }
    Esp32BaseWeb::sendJson(200, json);
    return true;
}

bool requireContext() {
    if (!g_context.config || !g_context.configStore || !g_context.app || !g_context.filters || !g_context.records ||
        !g_context.recordCalibrations || !g_context.recordCalibrationWriter || !g_context.nowSeconds) {
        Esp32BaseWeb::sendJson(503, "{\"error\":\"context_not_ready\"}");
        return false;
    }
    return true;
}

bool getParam(const char* name, char* out, std::size_t len) {
    return Esp32BaseWeb::hasParam(name) && Esp32BaseWeb::getParam(name, out, len);
}

void applyU32Param(const char* name, std::uint32_t& value) {
    char text[24]{};
    std::uint32_t parsed = 0;
    if (getParam(name, text, sizeof(text)) && parseU32(text, parsed)) {
        value = parsed;
    }
}

bool checkboxParam(const char* name) {
    return Esp32BaseWeb::hasParam(name);
}


bool persistConfig(const SystemConfig& config) {
    if (!g_context.app->canApplyConfig()) {
        return false;
    }

    SystemConfig safe = config;
    sanitizeConfig(safe);
    if (!g_context.configStore->saveSystemConfig(safe)) {
        return false;
    }
    if (!g_context.app->applyConfig(safe)) {
        return false;
    }
    *g_context.config = safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}

bool saveConfigAndReply(const SystemConfig& config) {
    if (!g_context.app->canApplyConfig()) {
        Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\",\"restartRecommended\":true}");
        return false;
    }
    const bool ok = persistConfig(config);
    Esp32BaseWeb::sendJson(ok ? 200 : 500,
                           ok ? "{\"ok\":true,\"restartRecommended\":true}" : "{\"error\":\"save_failed\"}");
    return ok;
}

bool persistFilterConfig(const FilterRecord& record, std::size_t index) {
    if (!g_context.app->canApplyConfig() || index >= kFilterCount) {
        return false;
    }

    SystemConfig* safe = new (std::nothrow) SystemConfig(*g_context.config);
    if (!safe) {
        return false;
    }
    safe->filters[index] = record;
    sanitizeConfig(*safe);

    FilterRecord runtime[kFilterCount]{};
    const FilterRecord* current = g_context.filters->records();
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        runtime[i] = current[i];
    }
    runtime[index] = safe->filters[index];

    if (!g_context.configStore->saveSystemConfig(*safe) || !g_context.configStore->saveFilterRuntime(runtime)) {
        delete safe;
        return false;
    }
    if (!g_context.filters->updateFilter(index, safe->filters[index]) || !g_context.app->applyConfig(*safe)) {
        delete safe;
        return false;
    }
    *g_context.config = *safe;
    delete safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    const FaucetDisplayStatus displayStatus =
        g_context.currentDisplayStatus
            ? g_context.currentDisplayStatus()
            : FaucetDisplayStatus{DisplayFrame{DisplayPage::Sleep, false, {}, {}}, false};
    char json[320]{};
    sendJsonBuffer(writeStatusJson(g_context.app->snapshot(), displayStatus.screenOn, json, sizeof(json)), json);
}

void handleTodayOverviewApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\"}");
        return;
    }
    if (!Esp32BaseWeb::beginResponse(200, "text/html; charset=utf-8", nullptr)) {
        return;
    }
    const AppSnapshot snapshot = g_context.app->snapshot();
    sendTodayOverview(collectTodayOverview(g_context.nowSeconds(), snapshot.statistics.todayMl));
    Esp32BaseWeb::endResponse();
}

void handlePresetsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        const bool browserForm = Esp32BaseWeb::hasParam("return");
        if (!g_context.app->canApplyConfig()) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=busy");
                return;
            }
            Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\",\"restartRecommended\":true}");
            return;
        }
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kPresetCount) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=invalid_index");
                return;
            }
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
            return;
        }
        SystemConfig* candidate = new (std::nothrow) SystemConfig(*g_context.config);
        if (!candidate) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=save_failed");
                return;
            }
            Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
            return;
        }
        PresetConfig& preset = candidate->presets[index];
        preset.enabled = checkboxParam("enabled");
        if (getParam("type", text, sizeof(text))) {
            preset.type = std::strcmp(text, "time") == 0 ? PresetType::Time : PresetType::Volume;
        }
        std::uint32_t value = 0;
        const bool validValue = getParam("value", text, sizeof(text)) && parseU32(text, value) &&
                                ((preset.type == PresetType::Time && value >= kMinTimePresetSec &&
                                  value <= kMaxTimePresetSec) ||
                                 (preset.type == PresetType::Volume && value >= kMinVolumePresetMl &&
                                  value <= kMaxVolumePresetMl));
        if (!validValue) {
            delete candidate;
            if (browserForm) {
                char location[80]{};
                std::snprintf(location,
                              sizeof(location),
                              "/faucet/presets?index=%lu&error=invalid_value",
                              static_cast<unsigned long>(index));
                Esp32BaseWeb::redirectSeeOther(location);
                return;
            }
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_value\"}");
            return;
        }
        preset.value = value;
        Esp32BaseWeb::getParam("name", preset.name, sizeof(preset.name));
        if (browserForm) {
            const bool ok = persistConfig(*candidate);
            delete candidate;
            Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/presets?saved=1" : "/faucet/presets?error=save_failed");
        } else {
            saveConfigAndReply(*candidate);
            delete candidate;
        }
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writePresetsJson(g_context.config->presets, json, sizeof(json)), json);
}

void handleRecordsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char text[24]{};
    std::uint32_t page = 0;
    std::uint32_t pageSize = kDefaultRecordPageSize;
    if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, pageSize);
    }
    if (pageSize > kMaxRecordPageSize) {
        pageSize = kMaxRecordPageSize;
    }
    WaterRecordFilter filter{};
    char dateText[16]{};
    if ((getParam("startDate", dateText, sizeof(dateText)) || getParam("from", dateText, sizeof(dateText)))) {
        if (!parseDate(dateText, filter.startTime)) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_date\"}");
            return;
        }
        filter.hasStart = filter.startTime > 0;
    }
    if ((getParam("endDate", dateText, sizeof(dateText)) || getParam("to", dateText, sizeof(dateText)))) {
        if (!parseDate(dateText, filter.endTime)) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_date\"}");
            return;
        }
        filter.hasEnd = filter.endTime > 0;
        if (filter.hasEnd) {
            filter.endTime = UINT32_MAX - filter.endTime < 86399UL ? UINT32_MAX : filter.endTime + 86399UL;
        }
    }
    if (filter.hasStart && filter.hasEnd && filter.endTime < filter.startTime) {
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_date\"}");
        return;
    }
    if ((filter.hasStart || filter.hasEnd) && waterTaskActive()) {
        Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\"}");
        return;
    }

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(pageSize));
    WaterRecord* records = new (std::nothrow) WaterRecord[kMaxRecordPageSize]{};
    char* json = new (std::nothrow) char[32768]{};
    if (!records || !json) {
        delete[] records;
        delete[] json;
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    const bool ready = g_context.records->ready();
    std::size_t totalCount = 0;
    const std::size_t readCount =
        ready ? queryWaterRecords(*g_context.records, filter, page, sanitizedPageSize, records, kMaxRecordPageSize, &totalCount) : 0;
    sendJsonBuffer(writeWaterRecordsJson(records,
                                      readCount,
                                      page,
                                      sanitizedPageSize,
                                      totalCount,
                                      g_context.records->storageName(),
                                      json,
                                      32768),
                   json);
    delete[] records;
    delete[] json;
}

void handleStatsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\"}");
        return;
    }
    char* json = new (std::nothrow) char[8192]{};
    if (!json) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    const WaterUsageSummary summary = aggregateWaterRecords(*g_context.records, g_context.nowSeconds(), kChartDays);
    const std::uint32_t totalMl = g_context.app->snapshot().statistics.totalMl;
    sendJsonBuffer(writeUsageSummaryJson(summary, totalMl, json, 8192), json);
    delete[] json;
}

void handleRecordCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }

    WaterRecord record{};
    if (!g_context.records || !g_context.records->ready() || g_context.records->readPage(0, 1, &record, 1) != 1 ||
        !waterRecordCanCalibrate(record)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=no_calibration_record");
        return;
    }

    char text[32]{};
    std::uint32_t actualMl = 0;
    if (!getParam("actualMl", text, sizeof(text)) || !parseU32(text, actualMl)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=invalid_value");
        return;
    }

    const SystemConfig oldConfig = g_context.app->config();
    const CalibrationApplyResult result = g_context.app->applyCalibrationFromRecord(record, actualMl);
    if (result == CalibrationApplyResult::Saved) {
        *g_context.config = g_context.app->config();
        if (!g_context.configStore->saveSystemConfig(*g_context.config)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=save_failed");
            return;
        }
        WaterRecordCalibration calibration = makeWaterRecordCalibration(record);
        calibration.actualMl = actualMl;
        calibration.calibratedAt = g_context.nowSeconds ? g_context.nowSeconds() : 0;
        calibration.oldPulsePerMl = oldConfig.pulsePerMl;
        calibration.newPulsePerMl = g_context.config->pulsePerMl;
        calibration.oldStartupCompensationMl = oldConfig.startupCompensationMl;
        calibration.newStartupCompensationMl = g_context.config->startupCompensationMl;
        calibration.kind = oldConfig.startupCompensationMl != g_context.config->startupCompensationMl
                               ? WaterRecordCalibrationKind::StartupCompensation
                               : WaterRecordCalibrationKind::PulsePerMl;
        if (!g_context.recordCalibrationWriter->upsert(calibration)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=calibration_mark_failed");
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/faucet/records?saved=1");
        return;
    }

    const char* error = result == CalibrationApplyResult::TooMuchDrift       ? "calibration_drift"
                        : result == CalibrationApplyResult::NotAvailable    ? "busy"
                        : result == CalibrationApplyResult::InvalidRecord   ? "no_calibration_record"
                                                                             : "invalid_value";
    char url[80]{};
    std::snprintf(url, sizeof(url), "/faucet/records?error=%s", error);
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleTraceCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!g_context.pulseTraces) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=no_calibration_record");
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    char text[32]{};
    std::uint32_t traceId = 0;
    std::uint32_t actualMl = 0;
    if (!getParam("trace", text, sizeof(text)) || !parseU32(text, traceId) ||
        !getParam("actualMl", text, sizeof(text)) || !parseU32(text, actualMl) ||
        actualMl < kMinVolumePresetMl || actualMl > kMaxVolumePresetMl) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=invalid_value");
        return;
    }
    if (!g_context.pulseTraces->setActualMl(traceId, actualMl)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=no_calibration_record");
        return;
    }

    SegmentedCalibrationSample calibrationSamples[8]{};
    std::size_t calibrationCount = 0;
    const bool manualSamples = Esp32BaseWeb::hasParam("manualSamples");
    for (std::size_t offset = 0; offset < g_context.pulseTraces->count() && calibrationCount < 8; ++offset) {
        const std::size_t index = g_context.pulseTraces->count() - 1 - offset;
        const WaterPulseTrace* trace = g_context.pulseTraces->traceAt(index);
        if (!trace || trace->actualMl == 0 || trace->sampleCount < 4 || trace->totalPulses == 0) {
            continue;
        }
        if (manualSamples && trace->traceId != traceId) {
            char key[24]{};
            std::snprintf(key, sizeof(key), "sample_%lu", static_cast<unsigned long>(trace->traceId));
            if (!Esp32BaseWeb::hasParam(key)) {
                continue;
            }
        }
        WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{};
        if (!samples) {
            continue;
        }
        for (std::size_t i = 0; i < trace->sampleCount; ++i) {
            const WaterPulseTraceSample* sample = g_context.pulseTraces->sampleAt(*trace, i);
            samples[i] = sample ? *sample : WaterPulseTraceSample{};
        }
        const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(*trace, samples, trace->sampleCount);
        delete[] samples;
        if (!analysis.stable || analysis.stablePulseCount == 0) {
            continue;
        }
        calibrationSamples[calibrationCount++] = SegmentedCalibrationSample{
            trace->actualMl,
            trace->totalPulses,
            analysis.startupPulseCount,
            analysis.stablePulseCount,
            analysis.stableStartSec,
        };
    }

    SegmentedCalibrationResult result{};
    if (computeSegmentedCalibration(calibrationSamples, calibrationCount, result)) {
        SystemConfig next = *g_context.config;
        next.overallPulsePerLiter = result.overallPulsePerLiter;
        next.startupDurationSec = result.startupDurationSec;
        next.startupPulseCount = result.startupPulseCount;
        next.startupVolumeMl = result.startupVolumeMl;
        next.startupPulsePerLiter = result.startupPulsePerLiter;
        next.stablePulsePerLiter = result.stablePulsePerLiter;
        next.segmentedMeteringCalibrated = true;
        if (!persistConfig(next)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=save_failed");
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/faucet/records?saved=1");
        return;
    }

    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/records/detail?trace=%lu&bucket=1&error=invalid_value",
                  static_cast<unsigned long>(traceId));
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleFiltersApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!g_context.app->canApplyConfig()) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=busy");
            return;
        }
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
            Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=invalid_index");
            return;
        }

        FilterRecord record = g_context.filters->record(index);
        record.enabled = checkboxParam("enabled");
        Esp32BaseWeb::getParam("name", record.name, sizeof(record.name));
        std::uint32_t months = 0;
        if (getParam("recommendMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.recommendDays = monthsToDays(months);
        }
        if (getParam("maxMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.maxDays = monthsToDays(months);
        }
        applyU32Param("lifeMl", record.lifeMl);
        if (getParam("startDate", text, sizeof(text))) {
            if (!parseDate(text, record.startTime)) {
                char location[80]{};
                std::snprintf(location,
                              sizeof(location),
                              "/faucet/filters/edit?index=%lu&error=invalid_date",
                              static_cast<unsigned long>(index));
                Esp32BaseWeb::redirectSeeOther(location);
                return;
            }
            record.startBootId = 0;
            record.usedMl = sumRealRecordVolumeSince(record.startTime);
        }
        record.name[kFilterNameLength - 1] = '\0';

        const bool ok = persistFilterConfig(record, index);
        Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?saved=1" : "/faucet/filters?error=save_failed");
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writeFiltersJson(g_context.filters->records(), json, sizeof(json)), json);
}

void handleFiltersResetApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char text[24]{};
    std::uint32_t index = 0;
    if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
        return;
    }

    const std::uint32_t now = g_context.nowSeconds();
    FilterRecord record = g_context.filters->record(index);
    record.startTime = now;
    record.usedMl = 0;
    record.startBootId = now >= kMinRealDateSeconds ? 0 : (g_context.bootId ? g_context.bootId() : 0);
    if (!g_context.filters->updateFilter(index, record)) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"reset_failed\"}");
        return;
    }
    const bool ok = g_context.configStore->saveFilterRuntime(g_context.filters->records());
    Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?reset=1" : "/faucet/filters?error=save_failed");
}

Esp32BaseWeb::Handler handlerFor(const FaucetWebRoute& route) {
    if (route.kind == FaucetWebRouteKind::Page) {
        if (std::strcmp(route.path, "/faucet") == 0) {
            return handleFaucetPage;
        }
        if (std::strcmp(route.path, "/faucet/presets") == 0) {
            return handlePresetsPage;
        }
        if (std::strcmp(route.path, "/faucet/records") == 0) {
            return handleRecordsPage;
        }
        if (std::strcmp(route.path, "/faucet/stats") == 0) {
            return handleStatsPage;
        }
        if (std::strcmp(route.path, "/faucet/filters") == 0) {
            return handleFiltersPage;
        }
        return handleFaucetPage;
    }
    if (std::strcmp(route.path, "/api/faucet/status") == 0) {
        return handleStatusApi;
    }
    if (std::strcmp(route.path, "/api/faucet/today") == 0) {
        return handleTodayOverviewApi;
    }
    if (std::strcmp(route.path, "/faucet/filters/edit") == 0) {
        return handleFilterEditPage;
    }
    if (std::strcmp(route.path, "/faucet/records/calibration") == 0) {
        return handleRecordCalibrationPage;
    }
    if (std::strcmp(route.path, "/faucet/records/detail") == 0) {
        return handleRecordDetailPage;
    }
    if (std::strcmp(route.path, "/api/faucet/presets") == 0) {
        return handlePresetsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/records") == 0) {
        return handleRecordsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/stats") == 0) {
        return handleStatsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/filters") == 0) {
        return handleFiltersApi;
    }
    if (std::strcmp(route.path, "/api/faucet/filters/reset") == 0) {
        return handleFiltersResetApi;
    }
    if (std::strcmp(route.path, "/api/faucet/records/calibration") == 0) {
        return handleRecordCalibrationApi;
    }
    if (std::strcmp(route.path, "/api/faucet/records/trace-calibration") == 0) {
        return handleTraceCalibrationApi;
    }
    return handleApi;
}

}  // namespace

void setFaucetWebContext(const FaucetWebContext& context) {
    g_context = context;
}

bool registerFaucetWeb() {
    if (!faucetWebRoutesFitEsp32Base()) {
        return false;
    }

    Esp32BaseWeb::setHeadExtraCallback(sendAppStyles);

    bool ok = true;
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (!faucetWebRouteAllowed(routes[i].path)) {
            ok = false;
            continue;
        }
        if (routes[i].kind == FaucetWebRouteKind::Page) {
            ok = Esp32BaseWeb::addPage(routes[i].path, routes[i].title, handlerFor(routes[i])) && ok;
        } else {
            ok = Esp32BaseWeb::addRoute(routes[i].path, toBaseMethod(routes[i].method), handlerFor(routes[i])) && ok;
        }
    }
    return ok;
}

}  // namespace faucet

#endif
