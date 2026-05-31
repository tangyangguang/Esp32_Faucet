#ifndef NATIVE_BUILD

#include "web/FaucetWeb.h"

#include "app/AppController.h"
#include "app/AppConfig.h"
#include "app/ConfigStore.h"
#include "app/FilterStore.h"
#include "app/MeteringSchemeStore.h"
#include "app/WaterRecordCalibrationStore.h"
#include "app/WaterRecordMeteringSnapshotStore.h"
#include "app/WaterRecordStore.h"
#include "app/WaterPulseTraceStore.h"
#include "web/FaucetWebJson.h"
#include "web/FaucetWebParsing.h"
#include "web/FaucetWebRoutes.h"

#include <Esp32Base.h>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#ifndef FAUCET_WEB_CSS_VERSION
#define FAUCET_WEB_CSS_VERSION __TIME__
#endif

namespace faucet {
namespace {

constexpr std::uint32_t kChartDays = kUsageSummaryMaxDays;
constexpr std::size_t kHomeTodayRecordLimit = 5;
constexpr std::size_t kRawTracePreviewLastSecond = 30;
constexpr std::uint16_t kSegmentedCalibrationRequiredSamples = 2;
constexpr std::size_t kSegmentedCalibrationMaxSamples = kSavedPulseTraceMaxCountLimit;
constexpr std::uint32_t kDefaultSamplePulseWindowSec = 10;
constexpr std::uint32_t kMinSamplePulseWindowSec = 1;
constexpr std::uint32_t kMaxSamplePulseWindowSec = 60;
FaucetWebContext g_context{};

struct TodayOverview {
    bool timeReady = false;
    std::uint32_t count = 0;
    std::uint32_t volumeMl = 0;
    std::uint32_t durationSec = 0;
    WaterRecord latest[kHomeTodayRecordLimit]{};
    std::size_t latestCount = 0;
};

struct SegmentedSampleDiagnostics {
    SegmentedCalibrationResult result{};
    std::uint16_t savedTraceCount = 0;
    std::uint16_t measuredSampleCount = 0;
    std::uint16_t validSampleCount = 0;
    std::uint32_t latestSampleTime = 0;
    std::uint32_t latestActualMl = 0;
};

bool requireContext();
bool contextReady();
bool getParam(const char* name, char* out, std::size_t len);
bool persistConfig(const SystemConfig& config);
bool activeMeteringSchemeForWeb(MeteringSchemeRecord& output);
void handleRecordDetailPage();
void handleRecordInfoPage();
void handleCalibrationPage();
void handleCalibrationPost();
void handleRecordCalibrationApi();
void handleGenerateSegmentedCalibrationApi();
void handleSaveGeneratedSchemeApi();
void handleDiscardGeneratedSchemeApi();
void handleCreateMeteringSchemeApi();
void handleEditMeteringSchemeApi();
void handleEnableMeteringSchemeApi();
void handleDisableMeteringSchemeApi();
void handleDeleteMeteringSchemeApi();
void handleDeleteLatestTraceApi();
void handleTraceCalibrationApi();
void handleTraceSaveApi();
void handleTraceDeleteApi();
void formatWaterRecordTime(const WaterRecord& record, char* out, std::size_t len);
void formatWaterRecordListTime(const WaterRecord& record, char* out, std::size_t len);
void sendNoticeFromQuery();
void sendPageEnd();

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

void formatLitersMl(std::uint32_t ml, char* out, std::size_t len) {
    std::snprintf(out, len, "%lu.%03lu L", static_cast<unsigned long>(ml / 1000UL),
                  static_cast<unsigned long>(ml % 1000UL));
}

void formatChartLiters(std::uint32_t ml, char* out, std::size_t len) {
    const std::uint32_t deciliters = (ml + 50UL) / 100UL;
    if (deciliters == 0) {
        std::snprintf(out, len, "0");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%lu",
                  static_cast<unsigned long>(deciliters / 10UL),
                  static_cast<unsigned long>(deciliters % 10UL));
}

std::uint32_t chartDistance(std::uint32_t a, std::uint32_t b) {
    return a > b ? a - b : b - a;
}

std::uint32_t chooseCountLabelY(std::uint32_t pointY, std::uint32_t barLabelY) {
    const std::uint32_t above = pointY > 16UL ? pointY - 11UL : pointY + 16UL;
    if (chartDistance(above, barLabelY) >= 14UL) {
        return above;
    }
    const std::uint32_t below = pointY + 15UL;
    if (chartDistance(below, barLabelY) >= 14UL) {
        return below;
    }
    return pointY > 24UL ? pointY - 24UL : pointY + 24UL;
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

void sendLitersMl(std::uint32_t ml) {
    char text[24]{};
    formatLitersMl(ml, text, sizeof(text));
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
    return record.pulseCount > 0 && waterResultAllowsCalibration(record.result);
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

void sendStatsMetricCard(const char* label, const char* value, const char* meta) {
    Esp32BaseWeb::sendChunk("<section class='metric-card stats-metric-card'><span>");
    Esp32BaseWeb::sendChunk(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::sendChunk(value);
    Esp32BaseWeb::sendChunk("</strong><small class='stats-card-meta'>");
    Esp32BaseWeb::sendChunk(meta);
    Esp32BaseWeb::sendChunk("</small></section>");
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

std::uint32_t historicalPulsePerLiter(float pulsePerMlAtRun) {
    if (!std::isfinite(pulsePerMlAtRun) || pulsePerMlAtRun <= 0.0f) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::lround(pulsePerMlAtRun * 1000.0f));
}

bool findRecordCalibration(const WaterRecord& record, WaterRecordCalibration& calibration) {
    return g_context.recordCalibrations && g_context.recordCalibrations->ready() &&
           g_context.recordCalibrations->find(record, calibration);
}

bool findRecordMeteringSnapshot(const WaterRecord& record, WaterRecordMeteringSnapshot& snapshot) {
    return g_context.recordMeteringSnapshots && g_context.recordMeteringSnapshots->ready() &&
           g_context.recordMeteringSnapshots->find(record, snapshot);
}

std::uint32_t actualMlForSegmentedSample(const WaterPulseTrace& trace) {
    if (trace.actualMl > 0) {
        return trace.actualMl;
    }
    WaterRecordCalibration calibration{};
    return findRecordCalibration(trace.record, calibration) ? calibration.actualMl : 0;
}

void indexPagePulseTraces(const WaterRecord* records,
                          std::size_t recordCount,
                          const WaterPulseTraceStore* store,
                          const WaterPulseTrace** output) {
    if (!output) {
        return;
    }
    for (std::size_t i = 0; i < recordCount; ++i) {
        output[i] = nullptr;
    }
    if (!records || !store || recordCount == 0) {
        return;
    }
    const std::size_t traceCount = store->count();
    for (std::size_t traceIndex = 0; traceIndex < traceCount; ++traceIndex) {
        const WaterPulseTrace* trace = store->traceAt(traceIndex);
        if (!trace || !trace->finished) {
            continue;
        }
        for (std::size_t i = 0; i < recordCount; ++i) {
            if (!output[i] && sameWaterRecordIdentity(trace->record, records[i])) {
                output[i] = trace;
            }
        }
    }
}

std::uint32_t measuredPulsePerLiter(const WaterRecord& record, const WaterRecordCalibration& calibration) {
    if (record.pulseCount == 0 || calibration.actualMl == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(record.pulseCount) * 1000ULL + calibration.actualMl / 2ULL) /
        calibration.actualMl);
}

bool saveRecordActualMeasurement(const WaterRecord& record, std::uint32_t actualMl) {
    if (!g_context.config || !g_context.recordCalibrationWriter) {
        return false;
    }
    WaterRecordCalibration calibration = makeWaterRecordCalibration(record);
    calibration.actualMl = actualMl;
    calibration.calibratedAt = g_context.nowSeconds ? g_context.nowSeconds() : 0;
    MeteringSchemeRecord activeScheme{};
    const MeteringParameters params =
        activeMeteringSchemeForWeb(activeScheme) ? activeScheme.params : defaultMeteringParameters();
    const float stablePulsePerMl = static_cast<float>(params.stablePulsePerLiter) / 1000.0f;
    calibration.oldPulsePerMl = stablePulsePerMl;
    calibration.newPulsePerMl = stablePulsePerMl;
    calibration.oldStartupCompensationMl = 0;
    calibration.newStartupCompensationMl = 0;
    calibration.kind = WaterRecordCalibrationKind::PulsePerMl;
    return g_context.recordCalibrationWriter->upsert(calibration);
}

bool ensureSavedPulseTracesReady() {
    if (!g_context.savedPulseTraces) {
        return false;
    }
    return g_context.savedPulseTraces->ready() || g_context.savedPulseTraces->begin();
}

bool syncTraceActualMeasurement(const WaterRecord& record, std::uint32_t actualMl) {
    bool updated = false;
    if (g_context.pulseTraces && g_context.pulseTraces->setActualMlByRecord(record, actualMl)) {
        updated = true;
    }
    if (ensureSavedPulseTracesReady()) {
        WaterPulseTrace savedTrace{};
        if (g_context.savedPulseTraces->findByRecord(record, savedTrace)) {
            updated = g_context.savedPulseTraces->setActualMl(savedTrace.traceId, actualMl) || updated;
        }
    }
    return updated;
}

bool copyRamTraceSamples(const WaterPulseTrace& trace, WaterPulseTraceSample* output, std::size_t outputCapacity) {
    if (!g_context.pulseTraces || !output || outputCapacity < trace.sampleCount) {
        return false;
    }
    for (std::size_t i = 0; i < trace.sampleCount; ++i) {
        const WaterPulseTraceSample* sample = g_context.pulseTraces->sampleAt(trace, i);
        if (!sample) {
            return false;
        }
        output[i] = *sample;
    }
    return true;
}

bool copySavedTraceSamples(const WaterPulseTrace& trace, WaterPulseTraceSample* output, std::size_t outputCapacity) {
    return g_context.savedPulseTraces && output && outputCapacity >= trace.sampleCount &&
           g_context.savedPulseTraces->readSamples(trace.traceId, output, outputCapacity) == trace.sampleCount;
}

bool firstSecondsPulseTotal(const WaterPulseTrace& trace,
                            bool savedSource,
                            std::uint32_t seconds,
                            std::uint32_t& totalPulses) {
    totalPulses = 0;
    if (seconds == 0 || trace.sampleCount == 0) {
        return true;
    }
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return false;
    }
    const bool copied = savedSource ? copySavedTraceSamples(trace, samples, trace.sampleCount)
                                    : copyRamTraceSamples(trace, samples, trace.sampleCount);
    if (copied) {
        std::size_t count = 0;
        const std::uint32_t endUs = seconds * 1000000UL;
        while (count < trace.sampleCount && samples[count].elapsedUs < endUs) {
            ++count;
        }
        totalPulses = effectivePulseCount(trace, samples, count);
    }
    delete[] samples;
    return copied;
}

bool segmentedSampleAlreadySeen(const WaterRecord* records, std::size_t count, const WaterRecord& record) {
    for (std::size_t i = 0; i < count; ++i) {
        if (sameWaterRecordIdentity(records[i], record)) {
            return true;
        }
    }
    return false;
}

bool appendSegmentedSampleFromTrace(const WaterPulseTrace& trace,
                                    bool savedSource,
                                    SegmentedCalibrationSample* samples,
                                    WaterRecord* seenRecords,
                                    std::size_t& seenCount,
                                    std::size_t& sampleCount,
                                    SegmentedSampleDiagnostics& diagnostics) {
    if (!samples || !seenRecords || seenCount >= kSegmentedCalibrationMaxSamples ||
        segmentedSampleAlreadySeen(seenRecords, seenCount, trace.record)) {
        return false;
    }
    seenRecords[seenCount] = trace.record;
    ++seenCount;

    const std::uint32_t actualMl = actualMlForSegmentedSample(trace);
    if (actualMl > 0) {
        ++diagnostics.measuredSampleCount;
    }
    if (sampleCount >= kSegmentedCalibrationMaxSamples || actualMl == 0 || trace.sampleCount < 6 ||
        trace.totalPulses == 0 || trace.truncated) {
        return false;
    }
    WaterPulseTraceSample* traceSamples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!traceSamples) {
        return false;
    }
    const bool copied = savedSource ? copySavedTraceSamples(trace, traceSamples, trace.sampleCount)
                                    : copyRamTraceSamples(trace, traceSamples, trace.sampleCount);
    if (!copied) {
        delete[] traceSamples;
        return false;
    }
    const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(trace, traceSamples, trace.sampleCount);
    delete[] traceSamples;
    if (!analysis.stable || analysis.stablePulseCount == 0) {
        return false;
    }
    samples[sampleCount] = SegmentedCalibrationSample{
        actualMl,
        trace.totalPulses,
        analysis.startupPulseCount,
        analysis.stablePulseCount,
        analysis.stableStartSec,
    };
    ++sampleCount;
    diagnostics.validSampleCount = static_cast<std::uint16_t>(sampleCount);
    if (trace.startTime >= diagnostics.latestSampleTime) {
        diagnostics.latestSampleTime = trace.startTime;
        diagnostics.latestActualMl = actualMl;
    }
    return true;
}

SegmentedSampleDiagnostics collectSegmentedSampleDiagnostics(bool includeRam) {
    SegmentedSampleDiagnostics diagnostics{};
    SegmentedCalibrationSample samples[kSegmentedCalibrationMaxSamples]{};
    WaterRecord seenRecords[kSegmentedCalibrationMaxSamples]{};
    std::size_t seenCount = 0;
    std::size_t sampleCount = 0;

    if (ensureSavedPulseTracesReady()) {
        WaterPulseTrace* savedTraces = new (std::nothrow) WaterPulseTrace[kSavedPulseTraceMaxCountLimit]{};
        if (savedTraces) {
            const std::size_t savedCount =
                g_context.savedPulseTraces->list(savedTraces, kSavedPulseTraceMaxCountLimit);
            diagnostics.savedTraceCount = static_cast<std::uint16_t>(
                std::min<std::size_t>(savedCount, static_cast<std::size_t>(UINT16_MAX)));
            for (std::size_t i = 0; i < savedCount && sampleCount < kSegmentedCalibrationMaxSamples; ++i) {
                appendSegmentedSampleFromTrace(
                    savedTraces[i], true, samples, seenRecords, seenCount, sampleCount, diagnostics);
            }
            delete[] savedTraces;
        }
    }

    if (includeRam && g_context.pulseTraces) {
        for (std::size_t offset = 0;
             offset < g_context.pulseTraces->count() && sampleCount < kSegmentedCalibrationMaxSamples;
             ++offset) {
            const std::size_t index = g_context.pulseTraces->count() - 1 - offset;
            const WaterPulseTrace* trace = g_context.pulseTraces->traceAt(index);
            if (trace) {
                appendSegmentedSampleFromTrace(*trace, false, samples, seenRecords, seenCount, sampleCount, diagnostics);
            }
        }
    }

    computeSegmentedCalibration(samples, sampleCount, diagnostics.result);
    diagnostics.validSampleCount = static_cast<std::uint16_t>(sampleCount);
    return diagnostics;
}

bool latestCalibratableRecord(WaterRecord& record) {
    return g_context.records && g_context.records->ready() && g_context.records->readPage(0, 1, &record, 1) == 1 &&
           waterRecordCanCalibrate(record);
}

bool saveLatestTraceToDevice(const WaterRecord& record,
                             std::uint32_t* savedTraceId,
                             WaterPulseTraceSaveStatus* status) {
    if (savedTraceId) {
        *savedTraceId = 0;
    }
    if (status) {
        *status = WaterPulseTraceSaveStatus::Ok;
    }
    if (!g_context.pulseTraces || !ensureSavedPulseTracesReady()) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::NotReady;
        }
        return false;
    }
    WaterPulseTrace savedTrace{};
    if (g_context.savedPulseTraces->findByRecord(record, savedTrace)) {
        if (savedTraceId) {
            *savedTraceId = savedTrace.traceId;
        }
        if (status) {
            *status = WaterPulseTraceSaveStatus::AlreadyExists;
        }
        return true;
    }
    WaterRecordCalibration calibration{};
    if (findRecordCalibration(record, calibration)) {
        g_context.pulseTraces->setActualMlByRecord(record, calibration.actualMl);
    }
    const WaterPulseTrace* trace = g_context.pulseTraces->findByRecord(record);
    if (!trace || trace->sampleCount == 0) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::InvalidInput;
        }
        return false;
    }
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{};
    if (!samples) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::WriteFailed;
        }
        return false;
    }
    const bool copied = copyRamTraceSamples(*trace, samples, trace->sampleCount);
    const bool saved = copied && g_context.savedPulseTraces->save(*trace, samples, trace->sampleCount, savedTraceId, status);
    delete[] samples;
    return saved;
}

bool saveRamTraceToDevice(std::uint32_t traceId,
                          std::uint32_t* savedTraceId,
                          WaterPulseTraceSaveStatus* status) {
    if (savedTraceId) {
        *savedTraceId = 0;
    }
    if (status) {
        *status = WaterPulseTraceSaveStatus::Ok;
    }
    if (!g_context.pulseTraces || !ensureSavedPulseTracesReady() || traceId == 0) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::NotReady;
        }
        return false;
    }
    const WaterPulseTrace* trace = g_context.pulseTraces->findById(traceId);
    if (!trace || !trace->finished || trace->sampleCount == 0) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::InvalidInput;
        }
        return false;
    }
    WaterRecordCalibration calibration{};
    if (findRecordCalibration(trace->record, calibration)) {
        g_context.pulseTraces->setActualMlByRecord(trace->record, calibration.actualMl);
        trace = g_context.pulseTraces->findById(traceId);
    }
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{};
    if (!samples) {
        if (status) {
            *status = WaterPulseTraceSaveStatus::WriteFailed;
        }
        return false;
    }
    const bool copied = copyRamTraceSamples(*trace, samples, trace->sampleCount);
    const bool saved = copied && g_context.savedPulseTraces->save(*trace, samples, trace->sampleCount, savedTraceId, status);
    delete[] samples;
    return saved;
}

bool deleteLatestTraceFromDevice(const WaterRecord& record) {
    if (!ensureSavedPulseTracesReady()) {
        return false;
    }
    WaterPulseTrace savedTrace{};
    return g_context.savedPulseTraces->findByRecord(record, savedTrace) &&
           g_context.savedPulseTraces->remove(savedTrace.traceId);
}

bool ensureMeteringSchemesReady() {
    return g_context.meteringSchemes && (g_context.meteringSchemes->ready() || g_context.meteringSchemes->begin());
}

const char* meteringSchemeSourceName(MeteringSchemeSource source) {
    switch (source) {
        case MeteringSchemeSource::Default:
            return "默认";
        case MeteringSchemeSource::Generated:
            return "样本生成";
        case MeteringSchemeSource::Manual:
            return "手工创建";
        case MeteringSchemeSource::Migrated:
            return "旧配置迁移";
    }
    return "-";
}

bool activeMeteringSchemeForWeb(MeteringSchemeRecord& output) {
    if (ensureMeteringSchemesReady() && g_context.meteringSchemes->activeScheme(output)) {
        return true;
    }
    if (g_context.app) {
        output = g_context.app->activeMeteringScheme();
        return output.valid;
    }
    return false;
}

bool meteringSchemeForSnapshot(const WaterRecordMeteringSnapshot& snapshot, MeteringSchemeRecord& output) {
    return ensureMeteringSchemesReady() && g_context.meteringSchemes->findById(snapshot.meteringSchemeId, output);
}

void sendMeteringSnapshotLabel(const WaterRecordMeteringSnapshot& snapshot, bool compact) {
    MeteringSchemeRecord scheme{};
    const bool schemeReady = meteringSchemeForSnapshot(snapshot, scheme);
    if (schemeReady && scheme.name[0]) {
        sendHtmlEscapedBounded(scheme.name, sizeof(scheme.name));
    } else {
        sendFmt("计量方案 %lu", static_cast<unsigned long>(snapshot.meteringSchemeId));
    }
    if (!compact) {
        sendFmt("<span class='inline-note'>ID #%lu · rev %lu</span>",
                static_cast<unsigned long>(snapshot.meteringSchemeId),
                static_cast<unsigned long>(snapshot.meteringSchemeRevision));
    }
}

bool storeSegmentedCandidate(const SegmentedCalibrationResult& result) {
    if (!ensureMeteringSchemesReady() || !result.valid) {
        return false;
    }
    MeteringSchemeCandidate candidate{};
    candidate.ready = true;
    candidate.params = MeteringParameters{result.startupPulseCount, result.startupVolumeMl, result.stablePulsePerLiter};
    candidate.generatedAt = g_context.nowSeconds ? g_context.nowSeconds() : 0;
    candidate.sampleCount = result.sampleCount;
    candidate.minActualMl = result.minActualMl;
    candidate.maxActualMl = result.maxActualMl;
    candidate.maxErrorMl = result.maxErrorMl;
    candidate.maxErrorPercent =
        result.maxActualMl > 0 ? static_cast<float>(result.maxErrorMl) * 100.0f / static_cast<float>(result.maxActualMl) : 0.0f;
    candidate.startupDurationMinSec = result.startupDurationSec;
    candidate.startupDurationMaxSec = result.startupDurationSec;
    candidate.startupDurationMedianSec = result.startupDurationSec;
    candidate.startupDurationAvgSec = result.startupDurationSec;
    std::snprintf(candidate.creationSummary,
                  sizeof(candidate.creationSummary),
                  "样本数量 %u，容量范围 %luml-%luml，最大误差 %luml，启动阶段平均 %lus。",
                  static_cast<unsigned>(result.sampleCount),
                  static_cast<unsigned long>(result.minActualMl),
                  static_cast<unsigned long>(result.maxActualMl),
                  static_cast<unsigned long>(result.maxErrorMl),
                  static_cast<unsigned long>(result.startupDurationSec));
    return g_context.meteringSchemes->saveCandidate(candidate);
}

bool generateSegmentedCalibrationResultFromSavedSamples() {
    if (!g_context.config) {
        return false;
    }
    const SegmentedSampleDiagnostics diagnostics = collectSegmentedSampleDiagnostics(false);
    if (!diagnostics.result.valid) {
        return false;
    }
    return storeSegmentedCandidate(diagnostics.result);
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
        case WaterPulseTraceState::PauseTimeout:
            return "暂停超时";
        case WaterPulseTraceState::SafetyStopped:
            return "安全停止";
        case WaterPulseTraceState::Error:
        default:
            return "异常";
    }
}

void sendPlainTextResponse(int status, const char* body) {
    Esp32BaseWeb::sendResponseHeader("Cache-Control", "no-store");
    Esp32BaseWeb::sendResponseHeader("X-Content-Type-Options", "nosniff");
    if (!Esp32BaseWeb::beginResponse(status, "text/plain; charset=utf-8", nullptr)) {
        return;
    }
    Esp32BaseWeb::sendChunk(body ? body : "");
    Esp32BaseWeb::endResponse();
}

void sendDurationUs(std::uint32_t us) {
    if (us >= 1000000UL) {
        sendFmt("%lu.%03lu s",
                static_cast<unsigned long>(us / 1000000UL),
                static_cast<unsigned long>((us % 1000000UL) / 1000UL));
    } else if (us >= 1000UL) {
        sendFmt("%lu.%03lu ms",
                static_cast<unsigned long>(us / 1000UL),
                static_cast<unsigned long>(us % 1000UL));
    } else {
        sendFmt("%lu us", static_cast<unsigned long>(us));
    }
}

std::size_t rawTracePreviewSampleCount(const WaterPulseTrace& trace) {
    const std::size_t previewLimit = kRawTracePreviewLastSecond;
    return std::min(trace.sampleCount, previewLimit);
}

void sendPulseTraceRawText(const WaterPulseTrace& trace,
                           const WaterPulseTraceSample* samples,
                           bool rawTraceShowAll) {
    Esp32BaseWeb::sendResponseHeader("Cache-Control", "no-store");
    Esp32BaseWeb::sendResponseHeader("X-Content-Type-Options", "nosniff");
    if (!Esp32BaseWeb::beginResponse(200, "text/plain; charset=utf-8", nullptr)) {
        return;
    }
    Esp32BaseWeb::sendChunk("序号\t距开始(us)\t与上一边沿间隔(us)\t有效累计\n");
    std::uint32_t effectiveCumulative = 0;
    std::uint32_t lastEffectiveElapsedUs = 0;
    const std::size_t sampleCount = rawTraceShowAll ? trace.sampleCount : rawTracePreviewSampleCount(trace);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const std::uint32_t intervalUs = i == 0 ? 0 : samples[i].elapsedUs - samples[i - 1].elapsedUs;
        bool effective = i == 0;
        if (i > 0 && samples[i].elapsedUs >= lastEffectiveElapsedUs &&
            samples[i].elapsedUs - lastEffectiveElapsedUs >= trace.pulseMinIntervalUs) {
            effective = true;
        }
        if (effective) {
            ++effectiveCumulative;
            lastEffectiveElapsedUs = samples[i].elapsedUs;
        }
        sendFmt("%lu\t%lu\t%lu\t%lu\n",
                static_cast<unsigned long>(i),
                static_cast<unsigned long>(samples[i].elapsedUs),
                static_cast<unsigned long>(intervalUs),
                static_cast<unsigned long>(effectiveCumulative));
    }
    if (!rawTraceShowAll && trace.sampleCount > sampleCount) {
        sendFmt("仅显示 0秒 到 %lu秒，共 %lu 行；完整明细请使用 all=1。\n",
                static_cast<unsigned long>(kRawTracePreviewLastSecond),
                static_cast<unsigned long>(sampleCount));
    }
    Esp32BaseWeb::endResponse();
}

std::uint32_t bucketRunningPulseDelta(const WaterPulseTraceSample* samples,
                                      std::size_t sampleCount,
                                      const WaterPulseTraceBucket& bucket) {
    (void)samples;
    (void)sampleCount;
    return bucket.pulseDelta;
}

void formatKb(std::size_t bytes, char* out, std::size_t len) {
    const std::uint32_t tenths = static_cast<std::uint32_t>((bytes * 10U + 512U) / 1024U);
    std::snprintf(out, len, "%lu.%luKB", static_cast<unsigned long>(tenths / 10U),
                  static_cast<unsigned long>(tenths % 10U));
}

void sendSegmentedMeteringPanel() {
    MeteringSchemeRecord activeScheme{};
    const bool activeReady = activeMeteringSchemeForWeb(activeScheme);
    const MeteringParameters active = activeReady ? activeScheme.params : defaultMeteringParameters();
    char stable[24]{};
    char startupPulse[24]{};
    char startupVolume[24]{};
    std::snprintf(stable, sizeof(stable), "%luP/L", static_cast<unsigned long>(active.stablePulsePerLiter));
    std::snprintf(startupPulse, sizeof(startupPulse), "%luP", static_cast<unsigned long>(active.startupPulseCount));
    formatLiters(active.startupVolumeMl, startupVolume, sizeof(startupVolume));
    Esp32BaseWeb::sendChunk("<section class='panel records-diagnostic-panel metering-status-diagnostic'><div class='diagnostic-head'><h3>计量状态</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div><div class='diagnostic-metric-grid three'>",
            activeReady ? "status-ok" : "status-muted",
            activeReady ? "当前启用" : "无可用方案");
    Esp32BaseWeb::sendChunk("<div class='diagnostic-metric'><span>当前方案</span><strong>");
    if (activeReady) {
        sendHtmlEscapedBounded(activeScheme.name, sizeof(activeScheme.name));
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</strong></div>");
    sendFmt("<div class='diagnostic-metric'><span>稳态P/L</span><strong>%s</strong></div>", stable);
    sendFmt("<div class='diagnostic-metric'><span>启动脉冲数</span><strong>%s</strong></div>", startupPulse);
    sendFmt("<div class='diagnostic-metric'><span>启动水量</span><strong>%s</strong></div>", startupVolume);
    Esp32BaseWeb::sendChunk("</div><div class='diagnostic-foot'>");
    if (activeReady) {
        sendFmt("<span>ID <b>#%lu</b></span><span>修订 <b>rev %lu</b></span><span>出水记录 <b>%lu</b> 条</span>",
                static_cast<unsigned long>(activeScheme.id),
                static_cast<unsigned long>(activeScheme.revision),
                static_cast<unsigned long>(activeScheme.useCount));
    } else {
        Esp32BaseWeb::sendChunk("<span>ID <b>-</b></span><span>修订 <b>-</b></span><span>出水记录 <b>-</b> 条</span>");
    }
    Esp32BaseWeb::sendChunk("</div></section>");
}

void sendPulseTraceCachePanel() {
    WaterPulseTraceStats stats{};
    if (!g_context.pulseTraces) {
        Esp32BaseWeb::sendChunk("<section class='panel records-diagnostic-panel trace-cache-diagnostic'><div class='diagnostic-head'><h3>RAM 最近明细</h3>"
                                "<span class='status-pill status-error'>RAM 不可用</span></div>"
                                "<div class='diagnostic-metric-grid'><div class='diagnostic-metric'><span>最近明细</span><strong>-</strong></div>"
                                "<div class='diagnostic-metric'><span>RAM 数据点</span><strong>-</strong></div></div>"
                                "<div class='diagnostic-foot'><span>RAM 占用 <b>-</b></span><span>单条上限 <b>-</b></span></div></section>");
    } else {
        stats = g_context.pulseTraces->stats();
        char used[24]{};
        formatKb(stats.usedBytes, used, sizeof(used));
        char traces[24]{};
        char samples[32]{};
        std::snprintf(traces,
                      sizeof(traces),
                      "%lu / %lu 条",
                      static_cast<unsigned long>(stats.traceCount),
                      static_cast<unsigned long>(g_context.config ? g_context.config->recentPulseTraceCount : stats.traceCapacity));
        std::snprintf(samples,
                      sizeof(samples),
                      "%lu / %lu 点",
                      static_cast<unsigned long>(stats.sampleCount),
                      static_cast<unsigned long>(stats.sampleCapacity));
        Esp32BaseWeb::sendChunk("<section class='panel records-diagnostic-panel trace-cache-diagnostic'><div class='diagnostic-head'><h3>RAM 最近明细</h3>"
                                "<span class='status-pill status-muted ram-badge'>RAM</span></div><div class='diagnostic-metric-grid'>");
        sendFmt("<div class='diagnostic-metric'><span>最近明细</span><strong>%s</strong></div>", traces);
        sendFmt("<div class='diagnostic-metric'><span>RAM 数据点</span><strong>%s</strong></div>", samples);
        sendFmt("</div><div class='diagnostic-foot'><span>RAM 占用 <b>%s</b></span><span>单条上限 <b>%lu 点</b></span></div></section>",
                used,
                static_cast<unsigned long>(stats.sampleCapacityPerTrace));
    }

    WaterPulseTraceFileStats savedStats{};
    const bool savedReady = ensureSavedPulseTracesReady();
    char savedCount[32]{};
    char savedSpace[48]{};
    std::snprintf(savedCount, sizeof(savedCount), "-");
    std::snprintf(savedSpace, sizeof(savedSpace), "-");
    if (savedReady) {
        savedStats = g_context.savedPulseTraces->stats();
        char savedUsed[24]{};
        char savedMax[24]{};
        std::snprintf(savedCount,
                      sizeof(savedCount),
                      "%lu / %lu 条",
                      static_cast<unsigned long>(savedStats.savedCount),
                      static_cast<unsigned long>(savedStats.maxCount));
        formatKb(savedStats.usedBytes, savedUsed, sizeof(savedUsed));
        formatKb(savedStats.maxBytes, savedMax, sizeof(savedMax));
        std::snprintf(savedSpace, sizeof(savedSpace), "%s / %s", savedUsed, savedMax);
    }
    Esp32BaseWeb::sendChunk("<section class='panel records-diagnostic-panel saved-trace-diagnostic'><div class='diagnostic-head'><h3>已保存明细</h3>"
                            "<span class='status-pill status-muted flash-badge'>Flash</span></div><div class='diagnostic-metric-grid'>");
    sendFmt("<div class='diagnostic-metric'><span>保存</span><strong>%s</strong></div>", savedCount);
    sendFmt("<div class='diagnostic-metric'><span>文件占用</span><strong>%s</strong></div>", savedSpace);
    if (savedReady) {
        sendFmt("</div><div class='diagnostic-foot'><span>单条最多 <b>%lu 点</b></span></div>",
                static_cast<unsigned long>(savedStats.sampleCapacityPerTrace));
    } else {
        Esp32BaseWeb::sendChunk("</div><div class='diagnostic-foot'><span>单条最多 <b>-</b></span></div>");
    }
    if (savedReady && savedStats.corrupt) {
        Esp32BaseWeb::sendChunk("<p class='err'>设备存储明细文件异常；不会影响启动和本次出水记录。</p>");
    }
    Esp32BaseWeb::sendChunk("</section>");
}

void sendCalibrationParameterPanels() {
    MeteringSchemeRecord* schemes = new (std::nothrow) MeteringSchemeRecord[10]{};
    const bool ready = ensureMeteringSchemesReady();
    const std::size_t count = ready && schemes ? g_context.meteringSchemes->list(schemes, 10, true) : 0;
    const std::uint32_t activeId = ready ? g_context.meteringSchemes->activeSchemeId() : 0;

    Esp32BaseWeb::sendChunk("<section class='panel calibration-param-panel'><div class='panel-head'><h3>流量计计量方案</h3>"
                            "<a class='btn-link' href='/faucet/calibration?scheme=new'>新建方案</a></div>"
                            "<table class='calibration-slot-table metering-scheme-table'><tr><th>方案</th><th>参数</th><th>适用条件</th><th>使用次数</th><th>操作</th></tr>");
    if (!ready) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='5'>计量方案存储不可用。</td></tr>");
    } else if (!schemes) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='5'>内存不足，无法加载方案列表。</td></tr>");
    }
    for (std::size_t i = 0; i < count; ++i) {
        const MeteringSchemeRecord& scheme = schemes[i];
        char startupVolume[24]{};
        formatLiters(scheme.params.startupVolumeMl, startupVolume, sizeof(startupVolume));
        sendFmt("<tr><td class='calibration-slot-index'><b>#%lu</b> ",
                static_cast<unsigned long>(scheme.id));
        if (scheme.id == activeId) {
            Esp32BaseWeb::sendChunk("<span class='status-pill status-ok'>当前启用</span> ");
        }
        if (!scheme.enabled) {
            Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>已禁用</span> ");
        }
        Esp32BaseWeb::sendChunk("<div class='calibration-slot-name'>");
        sendHtmlEscapedBounded(scheme.name, sizeof(scheme.name));
        sendFmt("</div><small>rev %lu · %s</small></td>",
                static_cast<unsigned long>(scheme.revision),
                meteringSchemeSourceName(scheme.sourceType));
        sendFmt("<td class='calibration-slot-values'><span><b>启动脉冲</b>%luP</span><span><b>启动水量</b>%s</span><span><b>稳态</b>%luP/L</span></td>",
                static_cast<unsigned long>(scheme.params.startupPulseCount),
                startupVolume,
                static_cast<unsigned long>(scheme.params.stablePulsePerLiter));
        Esp32BaseWeb::sendChunk("<td class='calibration-slot-note'>");
        if (scheme.meterLabel[0]) {
            Esp32BaseWeb::sendChunk("流量计：");
            sendHtmlEscapedBounded(scheme.meterLabel, sizeof(scheme.meterLabel));
            Esp32BaseWeb::sendChunk("<br>");
        }
        if (scheme.conditionLabel[0]) {
            Esp32BaseWeb::sendChunk("环境：");
            sendHtmlEscapedBounded(scheme.conditionLabel, sizeof(scheme.conditionLabel));
            Esp32BaseWeb::sendChunk("<br>");
        }
        sendHtmlEscapedBounded(scheme.lastModifiedSummary[0] ? scheme.lastModifiedSummary : scheme.creationSummary,
                               scheme.lastModifiedSummary[0] ? sizeof(scheme.lastModifiedSummary)
                                                              : sizeof(scheme.creationSummary));
        Esp32BaseWeb::sendChunk("</td>");
        sendFmt("<td class='scheme-use-count'><b>%lu</b><span>条</span>%s</td><td class='calibration-slot-edit'><div class='row-actions'>",
                static_cast<unsigned long>(scheme.useCount),
                scheme.usageStatsDirty ? "<span class='status-pill status-warn'>统计待核对</span>" : "");
        sendFmt("<a class='btn-link' href='/faucet/calibration?scheme=%lu'>编辑</a>",
                static_cast<unsigned long>(scheme.id));
        if (scheme.id != activeId && scheme.enabled) {
            sendFmt("<form method='post' action='/faucet/calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='enable_metering_scheme'><input type='hidden' name='id' value='%lu'><input class='primary' type='submit' value='启用'></form>",
                    static_cast<unsigned long>(scheme.id));
        }
        if (scheme.id != activeId && scheme.enabled) {
            sendFmt("<form method='post' action='/faucet/calibration' onsubmit=\"return confirm('确认禁用该计量方案？')&&once(this)\"><input type='hidden' name='action' value='disable_metering_scheme'><input type='hidden' name='id' value='%lu'><input class='secondary' type='submit' value='禁用'></form>",
                    static_cast<unsigned long>(scheme.id));
        }
        if (scheme.id != activeId && scheme.useCount == 0 && !scheme.usageStatsDirty) {
            sendFmt("<form method='post' action='/faucet/calibration' onsubmit=\"return confirm('确认删除未使用的计量方案？')&&once(this)\"><input type='hidden' name='action' value='delete_metering_scheme'><input type='hidden' name='id' value='%lu'><input class='danger' type='submit' value='删除'></form>",
                    static_cast<unsigned long>(scheme.id));
        }
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    if (ready && count == 10) {
        Esp32BaseWeb::sendChunk("<p class='muted'>页面最多显示最近 10 套可见方案；历史记录仍按方案 ID 和版本保存。</p>");
    }
    Esp32BaseWeb::sendChunk("</section>");
    delete[] schemes;
}

void sendMeteringSchemeEditPage(bool creating, const MeteringSchemeRecord* scheme) {
    const char* title = creating ? "新建计量方案" : "修改计量方案";
    Esp32BaseWeb::sendHeader(title);
    sendFmt("<h2>%s</h2><p><a class='btn-link' href='/faucet/calibration'>返回校准</a></p>", title);
    sendNoticeFromQuery();
    const MeteringParameters params = scheme ? scheme->params : defaultMeteringParameters();
    Esp32BaseWeb::sendChunk("<section class='panel calibration-param-panel'><form class='calibration-slot-form scheme-edit-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk(creating ? "<input type='hidden' name='action' value='create_metering_scheme'>"
                                     : "<input type='hidden' name='action' value='edit_metering_scheme'>");
    if (!creating && scheme) {
        sendFmt("<input type='hidden' name='id' value='%lu'>", static_cast<unsigned long>(scheme->id));
    }
    sendFmt("<label class='compact-field slot-name-field'><span>名称</span><input name='name' maxlength='%u' value='",
            static_cast<unsigned>(kMeteringSchemeNameLength - 1));
    if (scheme && scheme->name[0]) {
        sendHtmlAttrEscapedBounded(scheme->name, sizeof(scheme->name));
    } else {
        Esp32BaseWeb::sendChunk("手工方案");
    }
    Esp32BaseWeb::sendChunk("'></label>");
    sendFmt("<label class='compact-field'><span>启动脉冲数</span><input name='startupPulseCount' type='number' min='0' max='%lu' step='1' value='%lu'></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupPulseCount),
            static_cast<unsigned long>(params.startupPulseCount));
    sendFmt("<label class='compact-field'><span>启动水量</span><input name='startupVolumeMl' type='number' min='0' max='%lu' step='1' value='%lu'></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupVolumeMl),
            static_cast<unsigned long>(params.startupVolumeMl));
    sendFmt("<label class='compact-field'><span>稳态P/L</span><input name='stablePulsePerLiter' type='number' min='%lu' max='%lu' step='1' value='%lu'></label>",
            static_cast<unsigned long>(kMinSegmentedPulsePerLiter),
            static_cast<unsigned long>(kMaxSegmentedPulsePerLiter),
            static_cast<unsigned long>(params.stablePulsePerLiter));
    if (!creating && scheme) {
        sendFmt("<label class='compact-field'><span>流量计</span><input name='meterLabel' maxlength='%u' value='",
                static_cast<unsigned>(kMeteringSchemeMeterLabelLength - 1));
        sendHtmlAttrEscapedBounded(scheme->meterLabel, sizeof(scheme->meterLabel));
        sendFmt("'></label><label class='compact-field'><span>安装位置</span><input name='installationLabel' maxlength='%u' value='",
                static_cast<unsigned>(kMeteringSchemeInstallationLabelLength - 1));
        sendHtmlAttrEscapedBounded(scheme->installationLabel, sizeof(scheme->installationLabel));
        sendFmt("'></label><label class='compact-field'><span>水压/环境</span><input name='conditionLabel' maxlength='%u' value='",
                static_cast<unsigned>(kMeteringSchemeConditionLabelLength - 1));
        sendHtmlAttrEscapedBounded(scheme->conditionLabel, sizeof(scheme->conditionLabel));
        sendFmt("'></label><label class='compact-field slot-name-field'><span>备注</span><input name='userNote' maxlength='%u' value='",
                static_cast<unsigned>(kMeteringSchemeUserNoteLength - 1));
        sendHtmlAttrEscapedBounded(scheme->userNote, sizeof(scheme->userNote));
        Esp32BaseWeb::sendChunk("'></label>");
    }
    Esp32BaseWeb::sendChunk(creating ? "<div class='form-actions'><input class='primary' type='submit' value='保存为新方案'>"
                                     : "<div class='form-actions'><input class='primary' type='submit' value='保存修改'>");
    Esp32BaseWeb::sendChunk("<a class='btn-link' href='/faucet/calibration'>取消</a></div></form></section>");
    sendPageEnd();
}


void sendCalibrationFormulaPanel() {
    Esp32BaseWeb::sendChunk("<section class='panel calibration-formula-panel'><h3>参数说明与计算公式</h3>"
                            "<table class='kv'>"
                            "<tr><th>核心字段</th><td>运行时只使用启动脉冲数、启动水量、稳态P/L。</td></tr>"
                            "<tr><th>P = 0</th><td><code>估算出水量 = 0</code>。</td></tr>"
                            "<tr><th>0 &lt; P &lt;= Ns</th><td><code>估算出水量 = round(P × Vs / Ns)</code>。</td></tr>"
                            "<tr><th>P &gt; Ns</th><td><code>估算出水量 = Vs + round((P - Ns) × 1000 / Ps)</code>。</td></tr>"
                            "<tr><th>生成结果</th><td>点击生成方案后只得到待保存结果；保存为新方案前不会改变当前计量。</td></tr>"
                            "<tr><th>启用方案</th><td>启用只切换当前计量参数；历史出水记录保留当时使用的方案 ID、版本和参数快照。</td></tr>"
                            "</table></section>");
}

struct CalibrationSampleState {
    std::uint32_t actualMl = 0;
    std::uint32_t stableStartSec = 0;
    bool stable = false;
    bool qualityValid = false;
    bool truncated = false;
};

CalibrationSampleState inspectCalibrationSample(const WaterPulseTrace& trace, bool savedSource) {
    CalibrationSampleState state{};
    state.actualMl = actualMlForSegmentedSample(trace);
    state.truncated = trace.truncated;
    if (trace.truncated) {
        return state;
    }
    if (trace.sampleCount < 6 || trace.totalPulses == 0) {
        return state;
    }
    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace.sampleCount]{};
    if (!samples) {
        return state;
    }
    const bool copied = savedSource ? copySavedTraceSamples(trace, samples, trace.sampleCount)
                                    : copyRamTraceSamples(trace, samples, trace.sampleCount);
    if (copied) {
        const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(trace, samples, trace.sampleCount);
        state.stable = analysis.stable && analysis.stablePulseCount > 0;
        state.stableStartSec = analysis.stableStartSec;
        state.qualityValid = state.actualMl > 0 && state.stable;
    }
    delete[] samples;
    return state;
}

void sendCalibrationSampleStatusPills(const CalibrationSampleState& state, bool savedSource, const char* containerClass) {
    sendFmt("<span class='%s'>", containerClass);
    if (state.truncated) {
        Esp32BaseWeb::sendChunk("<span class='status-pill status-warn'>明细已截断</span>"
                                "<span class='status-pill status-muted'>不入库</span>");
        Esp32BaseWeb::sendChunk("</span>");
        return;
    }
    if (state.actualMl == 0) {
        Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>待确认容量</span>");
        Esp32BaseWeb::sendChunk("</span>");
        return;
    }
    Esp32BaseWeb::sendChunk("<span class='status-pill status-ok'>容量已确认</span>");
    if (!state.stable) {
        Esp32BaseWeb::sendChunk("<span class='status-pill status-warn'>稳态失败</span>");
        Esp32BaseWeb::sendChunk("</span>");
        return;
    }
    Esp32BaseWeb::sendChunk(savedSource ? "<span class='status-pill status-ok'>可参与生成</span>"
                                        : "<span class='status-pill status-warn'>可入库</span>");
    Esp32BaseWeb::sendChunk("</span>");
}

bool traceAlreadyListed(const WaterRecord* records, std::size_t count, const WaterRecord& record) {
    for (std::size_t i = 0; i < count; ++i) {
        if (sameWaterRecordIdentity(records[i], record)) {
            return true;
        }
    }
    return false;
}

bool latestRamTraceCanEnterSampleStore(const WaterRecord& record) {
    const WaterPulseTrace* ramTrace = g_context.pulseTraces ? g_context.pulseTraces->findByRecord(record) : nullptr;
    if (!ramTrace) {
        return false;
    }
    WaterPulseTrace savedTrace{};
    if (ensureSavedPulseTracesReady() && g_context.savedPulseTraces->findByRecord(record, savedTrace)) {
        return false;
    }
    return inspectCalibrationSample(*ramTrace, false).qualityValid;
}

bool generationResultRequested() {
    char text[32]{};
    if (getParam("generated", text, sizeof(text)) && std::strcmp(text, "1") == 0) {
        return true;
    }
    return getParam("saved", text, sizeof(text)) && std::strcmp(text, "generated") == 0;
}

bool calibrationContextRequested() {
    char text[24]{};
    return (getParam("from", text, sizeof(text)) && std::strcmp(text, "calibration") == 0) ||
           (getParam("returnTo", text, sizeof(text)) && std::strcmp(text, "calibration") == 0);
}

std::uint32_t selectedSamplePulseWindowSec() {
    char text[16]{};
    std::uint32_t seconds = kDefaultSamplePulseWindowSec;
    if (getParam("sampleSeconds", text, sizeof(text)) && parseU32(text, seconds)) {
        return std::min<std::uint32_t>(std::max<std::uint32_t>(seconds, kMinSamplePulseWindowSec),
                                       kMaxSamplePulseWindowSec);
    }
    return kDefaultSamplePulseWindowSec;
}

void sendTraceDetailLink(bool fromCalibration,
                         bool savedSource,
                         std::uint32_t traceId,
                         std::uint32_t bucket,
                         const char* label,
                         const char* className = "btn-link") {
    const char* detailPath = fromCalibration ? "/faucet/calibration/detail" : "/faucet/records/detail";
    sendFmt("<a class='%s' href='%s?%s%strace=%lu&bucket=%lu'>%s</a>",
            className,
            detailPath,
            fromCalibration ? "from=calibration&" : "",
            savedSource ? "saved=1&" : "",
            static_cast<unsigned long>(traceId),
            static_cast<unsigned long>(bucket),
            label);
}

void sendLatestCalibrationRecordPanel(bool canCalibrate, const WaterRecord& record) {
    Esp32BaseWeb::sendChunk("<section id='latest-record' class='panel latest-record-panel'><div class='panel-head'><h3>最后一条出水记录</h3></div>");
    if (!canCalibrate) {
        Esp32BaseWeb::sendChunk("<p class='err'>最新记录不可确认容量：没有可用原始脉冲或结束状态不适合校准。</p></section>");
        return;
    }

    char startTime[40]{};
    char targetText[32]{};
    formatWaterRecordListTime(record, startTime, sizeof(startTime));
    formatRecordTargetValue(record, targetText, sizeof(targetText));
    WaterRecordCalibration calibration{};
    const bool calibrated = findRecordCalibration(record, calibration);
    const std::uint32_t defaultActualMl = calibrated ? calibration.actualMl : record.volumeMl;
    const std::uint32_t pulsePerLiter = historicalPulsePerLiter(record.pulsePerMlAtRun);
    const WaterPulseTrace* ramTrace = g_context.pulseTraces ? g_context.pulseTraces->findByRecord(record) : nullptr;
    WaterPulseTrace savedTrace{};
    const bool hasSavedTrace =
        ensureSavedPulseTracesReady() && g_context.savedPulseTraces->findByRecord(record, savedTrace);
    const WaterPulseTrace* sampleTrace = hasSavedTrace ? &savedTrace : ramTrace;
    const bool sampleSavedSource = hasSavedTrace;
    CalibrationSampleState sampleState{};
    if (sampleTrace) {
        sampleState = inspectCalibrationSample(*sampleTrace, sampleSavedSource);
    } else if (calibrated) {
        sampleState.actualMl = calibration.actualMl;
    }

    Esp32BaseWeb::sendChunk("<table class='latest-record-table'><tr><th>时间</th><th>目标</th><th>估算出水</th><th>确认容量</th><th>脉冲</th><th>明细</th><th>样本状态</th><th>操作</th></tr><tr><td>");
    Esp32BaseWeb::sendChunk(startTime);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::sendChunk(targetText);
    Esp32BaseWeb::sendChunk("</td><td>");
    sendLiters(record.volumeMl);
    Esp32BaseWeb::sendChunk("</td><td>");
    if (calibrated) {
        sendLitersMl(calibration.actualMl);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    sendFmt("</td><td>%luP (%luP/L)</td><td>",
            static_cast<unsigned long>(record.pulseCount),
            static_cast<unsigned long>(pulsePerLiter));
    if (sampleTrace) {
        sendTraceDetailLink(true,
                            sampleSavedSource,
                            sampleTrace->traceId,
                            1,
                            sampleSavedSource ? "已保存到设备" : "RAM 中",
                            sampleSavedSource ? "trace-badge trace-source-link status-ok" : "trace-badge trace-source-link status-muted");
    } else {
        Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>无明细</span>");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    if (sampleTrace) {
        sendCalibrationSampleStatusPills(sampleState, sampleSavedSource, "latest-status-pills");
    } else {
        Esp32BaseWeb::sendChunk("<span class='latest-status-pills'>");
        Esp32BaseWeb::sendChunk(calibrated ? "<span class='status-pill status-ok'>容量已确认</span>"
                                           : "<span class='status-pill status-muted'>待确认容量</span>");
        Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>不参与分段生成</span></span>");
    }
    Esp32BaseWeb::sendChunk("</td><td><div class='row-actions latest-actions'><button class='btn-link' type='button' onclick='faucetShowLatestCalibration()'>确认/校准</button></div></td></tr>"
                            "<tr id='confirm-latest-volume' class='latest-calibration-edit-row'><td colspan='8'>"
                            "<form class='latest-calibration-form' method='post' action='/faucet/calibration' onsubmit='return faucetSubmitLatestCalibration(this)'>"
                            "<input type='hidden' name='action' value='calibrate'>"
                            "<input type='hidden' name='ajax' value='1'>"
                            "<label class='field latest-volume-field'><span>确认后的实际出水量（ml）</span>");
    sendFmt("<input name='actualMl' type='number' min='%lu' max='%lu' step='1' value='%lu'></label>",
            static_cast<unsigned long>(kMinVolumePresetMl),
            static_cast<unsigned long>(kMaxVolumePresetMl),
            static_cast<unsigned long>(defaultActualMl));
    Esp32BaseWeb::sendChunk("<span class='hint'>如果估算准确，直接确认即可；如果不准确，请改为量杯读数。</span>"
                            "<div class='form-actions'><input type='submit' value='确认容量'><button class='btn-link' type='button' onclick='faucetHideLatestCalibration()'>取消</button></div>"
                            "</form></td></tr></table>");
    Esp32BaseWeb::sendChunk("<p class='hint'>确认/校准容量：如果估算准确，直接确认即可；如果不准确，请改为量杯读数。确认后该容量可作为样本真值。"
                            "保存后会同步匹配明细并自动入库；已截断或已淘汰的明细不会入库，不会修改原始脉冲或当前启用参数。</p>");

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
        const std::int32_t estimateDiff =
            static_cast<std::int32_t>(record.volumeMl) - static_cast<std::int32_t>(calibration.actualMl);
        Esp32BaseWeb::sendChunk("<p class='hint'>上次校准记录：第 ");
        sendFmt("%u 次 / %s / 实测脉冲/升 %luP/L / 估算差 ",
                static_cast<unsigned>(calibration.calibrationCount),
                calibratedAt[0] ? calibratedAt : "未知",
                static_cast<unsigned long>(measuredPulsePerLiter(record, calibration)));
        sendSignedLiters(estimateDiff);
        Esp32BaseWeb::sendChunk(" / 控制参数未修改。</p>");
    }
    Esp32BaseWeb::sendChunk("</section>");
}

void sendCalibrationSampleRow(const WaterPulseTrace& trace, bool savedSource, std::uint32_t samplePulseWindowSec) {
    char startTime[40]{};
    formatWaterRecordListTime(trace.record, startTime, sizeof(startTime));
    const CalibrationSampleState state = inspectCalibrationSample(trace, savedSource);
    std::uint32_t firstWindowPulses = 0;
    const bool firstWindowReady = firstSecondsPulseTotal(trace, savedSource, samplePulseWindowSec, firstWindowPulses);
    Esp32BaseWeb::sendChunk("<tr><td>");
    Esp32BaseWeb::sendChunk(startTime);
    Esp32BaseWeb::sendChunk("</td><td>");
    if (state.actualMl > 0) {
        sendLitersMl(state.actualMl);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    sendFmt("</td><td>%luP / %lu点</td><td>",
            static_cast<unsigned long>(trace.totalPulses),
            static_cast<unsigned long>(trace.sampleCount));
    if (firstWindowReady) {
        sendFmt("%luP", static_cast<unsigned long>(firstWindowPulses));
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    if (state.stable) {
        sendFmt("第 %lus", static_cast<unsigned long>(state.stableStartSec));
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    sendTraceDetailLink(true,
                        savedSource,
                        trace.traceId,
                        1,
                        savedSource ? "已保存到设备" : "RAM 中",
                        savedSource ? "trace-badge trace-source-link status-ok" : "trace-badge trace-source-link status-muted");
    Esp32BaseWeb::sendChunk("</td><td>");
    sendCalibrationSampleStatusPills(state, savedSource, "sample-status-pills");
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void sendCalibrationSamplesPanel(std::uint32_t samplePulseWindowSec) {
    Esp32BaseWeb::sendChunk("<section id='calibration-samples' class='panel'><div class='panel-head'><h3>样本</h3></div>"
                            "<p class='hint'>这里展示有脉冲明细的数据。RAM 临时样本重启会丢失；只有已保存到设备、容量已确认且稳态识别成功的样本才参与方案生成。</p>");
    WaterPulseTrace* savedTraces = nullptr;
    std::size_t savedCount = 0;
    if (ensureSavedPulseTracesReady()) {
        savedTraces = new (std::nothrow) WaterPulseTrace[kSavedPulseTraceMaxCountLimit]{};
        if (savedTraces) {
            savedCount = g_context.savedPulseTraces->list(savedTraces, kSavedPulseTraceMaxCountLimit);
        }
    }
    const std::size_t ramCount = g_context.pulseTraces ? g_context.pulseTraces->count() : 0;
    Esp32BaseWeb::sendChunk("<form class='sample-window-form' method='get' action='/faucet/calibration'>"
                            "<label class='field sample-window-field'><span>前几秒脉冲总数</span>");
    sendFmt("<input name='sampleSeconds' type='number' min='1' max='60' step='1' value='%u'></label>"
            "<input type='submit' value='应用'></form>",
            static_cast<unsigned>(samplePulseWindowSec));
    Esp32BaseWeb::sendChunk("<p class='hint'>用于观察启动阶段脉冲分布；范围 1-60 秒，默认 10 秒。</p>");
    if (savedCount == 0 && ramCount == 0) {
        delete[] savedTraces;
        Esp32BaseWeb::sendChunk("<p class='hint'>还没有脉冲明细样本。</p></section>");
        return;
    }
    sendFmt("<table class='calibration-sample-table'><tr><th>时间</th><th>确认容量</th><th>脉冲</th><th>前 %u 秒脉冲</th><th>稳态</th><th>来源</th><th>状态</th></tr>",
            static_cast<unsigned>(samplePulseWindowSec));
    WaterRecord listed[kSavedPulseTraceMaxCountLimit]{};
    std::size_t listedCount = 0;
    for (std::size_t i = 0; i < savedCount; ++i) {
        sendCalibrationSampleRow(savedTraces[i], true, samplePulseWindowSec);
        if (listedCount < kSavedPulseTraceMaxCountLimit) {
            listed[listedCount++] = savedTraces[i].record;
        }
    }
    if (g_context.pulseTraces) {
        for (std::size_t offset = 0; offset < ramCount; ++offset) {
            const std::size_t index = ramCount - 1 - offset;
            const WaterPulseTrace* trace = g_context.pulseTraces->traceAt(index);
            if (!trace || !trace->finished || traceAlreadyListed(listed, listedCount, trace->record)) {
                continue;
            }
            sendCalibrationSampleRow(*trace, false, samplePulseWindowSec);
        }
    }
    Esp32BaseWeb::sendChunk("</table></section>");
    delete[] savedTraces;
}

void sendCalibrationGenerationPanel(bool canCalibrate, const WaterRecord& record) {
    const SegmentedSampleDiagnostics diagnostics = collectSegmentedSampleDiagnostics(false);
    const bool canGenerate = diagnostics.result.valid;
    MeteringSchemeCandidate candidate{};
    const bool showGeneratedResult = generationResultRequested();
    const bool candidateReady = showGeneratedResult && ensureMeteringSchemesReady() &&
                                g_context.meteringSchemes->loadCandidate(candidate) && candidate.ready;
    char sampleCountText[32]{};
    char sampleRange[48]{};
    char sampleNeed[48]{};
    char error[32]{};
    std::snprintf(sampleCountText,
                  sizeof(sampleCountText),
                  "%u/%u 条",
                  static_cast<unsigned>(diagnostics.validSampleCount),
                  static_cast<unsigned>(kSegmentedCalibrationRequiredSamples));
    if (diagnostics.result.valid) {
        char minActual[20]{};
        char maxActual[20]{};
        char maxError[20]{};
        formatLiters(diagnostics.result.minActualMl, minActual, sizeof(minActual));
        formatLiters(diagnostics.result.maxActualMl, maxActual, sizeof(maxActual));
        formatLiters(diagnostics.result.maxErrorMl, maxError, sizeof(maxError));
        std::snprintf(sampleRange, sizeof(sampleRange), "%s - %s", minActual, maxActual);
        std::snprintf(error, sizeof(error), "%s", maxError);
        std::snprintf(sampleNeed, sizeof(sampleNeed), "0条");
    } else {
        std::snprintf(sampleRange, sizeof(sampleRange), "-");
        std::snprintf(error, sizeof(error), "-");
        if (diagnostics.validSampleCount < kSegmentedCalibrationRequiredSamples) {
            std::snprintf(sampleNeed,
                          sizeof(sampleNeed),
                          "%u条可生成样本",
                          static_cast<unsigned>(kSegmentedCalibrationRequiredSamples - diagnostics.validSampleCount));
        } else {
            std::snprintf(sampleNeed, sizeof(sampleNeed), "容量差异>0.50 L的样本");
        }
    }
    Esp32BaseWeb::sendChunk("<section id='scheme-generation' class='panel'><h3>计量方案生成</h3>"
                            "<p class='hint'>手动执行：只扫描已保存到设备、容量已确认且稳态识别成功的样本，生成方案后显示待保存结果；保存为新方案前不会改变当前出水估算。</p>");
    Esp32BaseWeb::sendChunk("<section class='sample-coverage-diagnostic'><div class='diagnostic-head'><h3>样本覆盖</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div><div class='diagnostic-metric-grid'>",
            canGenerate ? "status-ok" : "status-muted",
            canGenerate ? "可生成" : "样本不足");
    sendFmt("<div class='diagnostic-metric'><span>可生成样本</span><strong>%s</strong></div>", sampleCountText);
    sendFmt("<div class='diagnostic-metric'><span>容量范围</span><strong>%s</strong></div>", sampleRange);
    sendFmt("<div class='diagnostic-metric'><span>拟合误差</span><strong>%s</strong></div>", error);
    sendFmt("<div class='diagnostic-metric'><span>还需</span><strong>%s</strong></div>", sampleNeed);
    Esp32BaseWeb::sendChunk("</div><div class='diagnostic-foot'>");
    sendFmt("<span>已保存明细 <b>%u条</b></span><span>已测容量 <b>%u条</b></span>",
            static_cast<unsigned>(diagnostics.savedTraceCount),
            static_cast<unsigned>(diagnostics.measuredSampleCount));
    sendFmt("<span>还需 <b>%s</b></span>", sampleNeed);
    Esp32BaseWeb::sendChunk("</div></section>");
    if (candidateReady) {
        char generatedStartupVolume[24]{};
        char generatedMinActual[20]{};
        char generatedMaxActual[20]{};
        char generatedRange[48]{};
        char generatedError[20]{};
        formatLiters(candidate.params.startupVolumeMl, generatedStartupVolume, sizeof(generatedStartupVolume));
        formatLiters(candidate.minActualMl, generatedMinActual, sizeof(generatedMinActual));
        formatLiters(candidate.maxActualMl, generatedMaxActual, sizeof(generatedMaxActual));
        formatLiters(candidate.maxErrorMl, generatedError, sizeof(generatedError));
        std::snprintf(generatedRange, sizeof(generatedRange), "%s - %s", generatedMinActual, generatedMaxActual);
        Esp32BaseWeb::sendChunk("<section class='generated-scheme-result'><div class='panel-head'><h3>生成结果</h3>"
                                "<span class='status-pill status-warn'>已生成，待保存</span></div>"
                                "<table class='generated-scheme-table'><tr><th>启动脉冲数</th><th>启动水量</th><th>稳态P/L</th><th>样本</th><th>容量范围</th><th>最大误差</th></tr>");
        sendFmt("<tr><td>%luP</td><td>%s</td><td>%luP/L</td><td>%u 条</td><td>%s</td><td>%s</td></tr></table>"
                "<p class='generated-note'>",
                static_cast<unsigned long>(candidate.params.startupPulseCount),
                generatedStartupVolume,
                static_cast<unsigned long>(candidate.params.stablePulsePerLiter),
                static_cast<unsigned>(candidate.sampleCount),
                generatedRange,
                generatedError);
        sendHtmlEscapedBounded(candidate.creationSummary, sizeof(candidate.creationSummary));
        sendFmt(" 启动阶段平均 %lus。</p>",
                static_cast<unsigned long>(candidate.startupDurationAvgSec));
        Esp32BaseWeb::sendChunk("<div class='form-actions'><form class='inline-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='save_generated_scheme'>"
                                "<label class='compact-field generated-name-field'><span>新方案名称</span><input name='name' maxlength='31' value='样本生成方案'></label>"
                                "<input class='primary' type='submit' value='保存为新方案'></form>"
                                "<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='discard_generated_scheme'>"
                                "<input class='secondary' type='submit' value='放弃生成结果'></form></div></section>");
    }
    (void)canCalibrate;
    (void)record;
    Esp32BaseWeb::sendChunk("<div class='form-actions'>"
                            "<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='generate_segmented'>");
    Esp32BaseWeb::sendChunk("<input type='submit' value='生成方案'></form>");
    Esp32BaseWeb::sendChunk("</div></section>");
}

void sendCalibrationPageScript() {
    Esp32BaseWeb::sendChunk("<script>"
                            "function faucetShowLatestCalibration(){var r=document.getElementById('confirm-latest-volume');if(r)r.classList.add('is-open');}"
                            "function faucetHideLatestCalibration(){var r=document.getElementById('confirm-latest-volume');if(r)r.classList.remove('is-open');}"
                            "function faucetCalibrationErrorMessage(code){var m={busy:'设备正在出水或确认中，请回到待机后再保存。',invalid_value:'实际出水量超出允许范围，请按量杯读数填写。',no_calibration_record:'最新记录不可确认容量：没有可用记录、原始脉冲或结束状态不适合校准。',calibration_mark_failed:'实测容量写入失败，可能是校准记录存储不可用或 Flash 写入失败。','HTTP 401':'认证已失效，请刷新页面重新登录。','HTTP 403':'认证被拒绝，请检查 Web 登录状态。','HTTP 404':'保存接口路径不存在，请刷新页面后重试。','HTTP 500':'设备端保存接口异常，请查看日志。','HTTP 503':'设备尚未就绪，请稍后重试。'};return m[code]||(code?'操作失败：'+code:'操作失败，请检查页面状态后重试。');}"
                            "function faucetReadCalibrationError(r){return r.text().then(function(t){try{return (JSON.parse(t)||{}).error||('HTTP '+r.status);}catch(e){return 'HTTP '+r.status;}});}"
                            "function faucetResetLatestCalibrationForm(f){f.dataset.busy='';var b=f.querySelector('[type=submit]');if(b)b.disabled=false;}"
                            "function faucetReplaceCalibrationSection(id,url){return fetch(url,{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();}).then(function(html){var old=document.getElementById(id);if(!old)return;var box=document.createElement('div');box.innerHTML=html;var next=box.querySelector('#'+id);if(next)old.replaceWith(next);});}"
                            "function faucetFetchLatestCalibration(){return fetch('/faucet/calibration?partial=latest',{cache:'no-store',credentials:'same-origin'});}"
                            "function faucetRefreshCalibrationPanels(){return Promise.all([faucetFetchLatestCalibration().then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();}).then(function(html){var old=document.getElementById('latest-record');if(!old)return;var box=document.createElement('div');box.innerHTML=html;var next=box.querySelector('#latest-record');if(next)old.replaceWith(next);}),faucetReplaceCalibrationSection('scheme-generation','/faucet/calibration?partial=generation'),faucetReplaceCalibrationSection('calibration-samples','/faucet/calibration?partial=samples')]);}"
                            "function faucetSubmitLatestCalibration(f){if(typeof once==='function'&&!once(f))return false;fetch('/faucet/calibration',{method:'POST',body:new FormData(f),cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)return faucetReadCalibrationError(r).then(function(code){throw new Error(code);});return r.json();}).then(function(){return faucetRefreshCalibrationPanels().catch(function(){faucetResetLatestCalibrationForm(f);alert('校准已保存，但页面刷新失败，请手动刷新查看最新状态。');});}).catch(function(e){faucetResetLatestCalibrationForm(f);alert('保存失败：'+faucetCalibrationErrorMessage(e.message));});return false;}"
                            "</script>");
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
    std::uint32_t maxCount = 0;
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        if (summary.days[i].volumeMl > maxVolume) {
            maxVolume = summary.days[i].volumeMl;
        }
        if (summary.days[i].count > maxCount) {
            maxCount = summary.days[i].count;
        }
    }
    const std::uint32_t chartMax = maxVolume == 0 ? 1000UL : maxVolume;
    const std::uint32_t countMax = maxCount == 0 ? 1UL : maxCount;
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><div class='chart-head'><h3>最近 30 天出水量</h3>"
                            "<span class='chart-unit'>每日出水量 (L) · 每日次数</span></div>"
                            "<svg viewBox='0 0 1080 292' role='img' aria-label='最近30天每日出水量和每日次数'>");
    constexpr std::uint32_t top = 56;
    constexpr std::uint32_t baseY = 222;
    constexpr std::uint32_t chartHeight = baseY - top;
    constexpr std::uint32_t left = 90;
    constexpr std::uint32_t step = 31;
    constexpr std::uint32_t barWidth = 16;
    Esp32BaseWeb::sendChunk("<line class='axis' x1='72' y1='222' x2='1030' y2='222'></line>"
                            "<line class='axis' x1='72' y1='48' x2='72' y2='222'></line>"
                            "<line class='count-axis' x1='1030' y1='48' x2='1030' y2='222'></line>");
    for (std::uint32_t tick = 0; tick < 5; ++tick) {
        const std::uint32_t tickValue = (chartMax * tick) / 4UL;
        const std::uint32_t y = baseY - (chartHeight * tick) / 4UL;
        char tickText[24]{};
        formatLiters(tickValue, tickText, sizeof(tickText));
        sendFmt("<line class='grid' x1='72' y1='%lu' x2='1030' y2='%lu'></line>"
                "<text class='chart-y-tick' x='62' y='%lu'>%s</text>",
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y + 4UL),
                tickText);
    }
    std::uint32_t previousCountTick = countMax + 1UL;
    for (std::uint32_t tick = 0; tick < 4; ++tick) {
        const std::uint32_t countValue = (countMax * tick + 1UL) / 3UL;
        if (tick > 0 && countValue == previousCountTick) {
            continue;
        }
        previousCountTick = countValue;
        const std::uint32_t y = baseY - (chartHeight * countValue) / countMax;
        sendFmt("<text class='count-y-tick' x='1040' y='%lu'>%lu 次</text>",
                static_cast<unsigned long>(y + 4UL),
                static_cast<unsigned long>(countValue));
    }
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const DailyUsageBucket& day = summary.days[i];
        char label[8]{};
        char volume[24]{};
        char barLabel[12]{};
        char duration[24]{};
        formatDayLabel(day.dayIndex, label, sizeof(label));
        formatLiters(day.volumeMl, volume, sizeof(volume));
        formatChartLiters(day.volumeMl, barLabel, sizeof(barLabel));
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        const std::uint32_t avgMl = day.count == 0 ? 0 : day.volumeMl / day.count;
        const std::uint32_t barHeight = day.volumeMl == 0 ? 0 : (day.volumeMl * chartHeight) / chartMax;
        const std::uint32_t y = baseY - barHeight;
        const std::uint32_t center = x + barWidth / 2UL;
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu' rx='1'><title>"
                "%s：%s · %u 次 · %s · 平均 %lu ml/次</title></rect>"
                "<text class='bar-label' x='%lu' y='%lu'>%s</text>"
                "<text class='x-label' x='%lu' y='250' transform='rotate(-45 %lu 250)'>%s</text>",
                day.count == 0 ? "bar empty-bar" : "bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? baseY - 3UL : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 3UL : barHeight),
                label,
                volume,
                static_cast<unsigned>(day.count),
                duration,
                static_cast<unsigned long>(avgMl),
                static_cast<unsigned long>(center),
                static_cast<unsigned long>(barHeight == 0 ? baseY - 10UL : (y > 10UL ? y - 8UL : y)),
                barLabel,
                static_cast<unsigned long>(center),
                static_cast<unsigned long>(center),
                label);
    }
    if (summary.dayCount > 0) {
        Esp32BaseWeb::sendChunk("<polyline class='count-line-halo' points='");
        for (std::size_t i = 0; i < summary.dayCount; ++i) {
            const DailyUsageBucket& day = summary.days[i];
            const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step + barWidth / 2UL;
            const std::uint32_t y = baseY - (static_cast<std::uint32_t>(day.count) * chartHeight) / countMax;
            sendFmt("%s%lu,%lu",
                    i == 0 ? "" : " ",
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(y));
        }
        Esp32BaseWeb::sendChunk("'></polyline><polyline class='count-line' points='");
        for (std::size_t i = 0; i < summary.dayCount; ++i) {
            const DailyUsageBucket& day = summary.days[i];
            const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step + barWidth / 2UL;
            const std::uint32_t y = baseY - (static_cast<std::uint32_t>(day.count) * chartHeight) / countMax;
            sendFmt("%s%lu,%lu",
                    i == 0 ? "" : " ",
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(y));
        }
        Esp32BaseWeb::sendChunk("'></polyline>");
        for (std::size_t i = 0; i < summary.dayCount; ++i) {
            const DailyUsageBucket& day = summary.days[i];
            if (day.count == 0) {
                continue;
            }
            const std::uint32_t center = left + static_cast<std::uint32_t>(i) * step + barWidth / 2UL;
            const std::uint32_t y = baseY - (static_cast<std::uint32_t>(day.count) * chartHeight) / countMax;
            const std::uint32_t barHeight = day.volumeMl == 0 ? 0 : (day.volumeMl * chartHeight) / chartMax;
            const std::uint32_t barTop = baseY - barHeight;
            const std::uint32_t barLabelY = barHeight == 0 ? baseY - 10UL : (barTop > 10UL ? barTop - 8UL : barTop);
            const std::uint32_t countLabelY = chooseCountLabelY(y, barLabelY);
            char countText[12]{};
            std::snprintf(countText, sizeof(countText), "%u次", static_cast<unsigned>(day.count));
            sendFmt("<circle class='count-dot' cx='%lu' cy='%lu' r='1.7'><title>%u 次</title></circle>"
                    "<text class='count-label' x='%lu' y='%lu'>%s</text>",
                    static_cast<unsigned long>(center),
                    static_cast<unsigned long>(y),
                    static_cast<unsigned>(day.count),
                    static_cast<unsigned long>(center),
                    static_cast<unsigned long>(countLabelY),
                    countText);
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

void formatWaterRecordListTime(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (waterRecordHasRealTime(record)) {
        std::uint16_t year = 0;
        std::uint8_t month = 0;
        std::uint8_t monthDay = 0;
        dateFromDayIndex(record.startTime / 86400UL, year, month, monthDay);
        const std::uint32_t daySecond = record.startTime % 86400UL;
        std::snprintf(out,
                      len,
                      "%02u-%02u %02lu:%02lu",
                      static_cast<unsigned>(month),
                      static_cast<unsigned>(monthDay),
                      static_cast<unsigned long>(daySecond / 3600UL),
                      static_cast<unsigned long>((daySecond / 60UL) % 60UL));
        return;
    }
    if (waterRecordHasBootRelativeTime(record)) {
        std::snprintf(out, len, "开机+%lu s", static_cast<unsigned long>(record.startTime));
        return;
    }
    std::snprintf(out, len, "未知");
}

void sendNoticeFromQuery() {
    char text[32]{};
    if (getParam("saved", text, sizeof(text))) {
        const bool actualOnly = std::strcmp(text, "actual") == 0;
        const bool generated = std::strcmp(text, "generated") == 0;
        const bool generatedDiscarded = std::strcmp(text, "generated_discarded") == 0;
        const bool restored = std::strcmp(text, "restored") == 0;
        const bool traceSaved = std::strcmp(text, "trace") == 0;
        const bool traceDeleted = std::strcmp(text, "trace_deleted") == 0;
        Esp32BaseWeb::sendChunk("<p class='ok'>");
        Esp32BaseWeb::sendChunk(actualOnly   ? "校准已保存。"
                                : generated  ? "计量方案生成结果已生成。"
                                : generatedDiscarded ? "生成结果已放弃。"
                                : restored           ? "已恢复上一套参数。"
                                : traceSaved         ? "明细已保存到设备。"
                                : traceDeleted       ? "已保存明细已删除。"
                                                     : "已保存。");
        Esp32BaseWeb::sendChunk("</p>");
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
    } else if (std::strcmp(text, "saved_trace_full") == 0) {
        message = "已保存脉冲明细已达上限，请先删除不需要的明细。";
    } else if (std::strcmp(text, "saved_trace_corrupt") == 0) {
        message = "设备存储明细文件异常，已停止写入以保护已有数据。";
    } else if (std::strcmp(text, "saved_trace_read_failed") == 0) {
        message = "已保存脉冲明细读取失败，文件可能未写完整或已损坏。";
    } else if (std::strcmp(text, "no_calibration_record") == 0) {
        message = "最新记录没有可用的原始脉冲，不能用于校准。";
    } else if (std::strcmp(text, "calibration_mark_failed") == 0) {
        message = "实测记录保存失败，请重试。";
    } else if (std::strcmp(text, "calibration_unchanged") == 0) {
        message = "实际出水量未变化，未保存校准。";
    } else if (std::strcmp(text, "calibration_drift") == 0) {
        message = "新系数和旧系数偏差过大，请重新接水测量。";
    } else if (std::strcmp(text, "sample_not_enough") == 0) {
        message = "可生成样本不足，至少需要两条容量差异明显、已保存到设备、容量已确认且稳态识别成功的样本。";
    } else if (std::strcmp(text, "no_generated_result") == 0) {
        message = "还没有可保存的生成结果，请先生成方案。";
    } else if (std::strcmp(text, "no_previous") == 0) {
        message = "没有可恢复的上一套参数。";
    }
    Esp32BaseWeb::sendChunk("<p class='err'>");
    Esp32BaseWeb::sendChunk(message);
    Esp32BaseWeb::sendChunk("</p>");
}

void sendAppCss() {
    Esp32BaseWeb::sendChunk(":root{--bg:#fbfcfb;--surface:#fff;--line:#e2ebe8;--text:#243039;--muted:#6b777f;--accent:#3d837b;--accent-soft:#edf6f3;--warn:#a36b10}"
                            "body{max-width:1120px;background:var(--bg);color:var(--text);font-size:14px;line-height:1.42;padding:14px 18px;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,'PingFang SC','Microsoft YaHei',sans-serif}"
                            "h1,h2,h3{letter-spacing:0;color:var(--text)}h2{font-size:18px;margin:18px 0 10px}h3{font-size:15px;margin:0 0 8px}.page{margin:0}p{margin:0 0 8px}");
    Esp32BaseWeb::sendChunk("nav,.footerbar,.panel,.metric-card,.filter-card,.daily-chart,.usage-panel,table{background:var(--surface);border:1px solid var(--line);border-radius:6px;box-shadow:0 1px 2px rgba(20,34,38,.025)}"
                            "nav{display:flex;align-items:center;gap:6px;margin:0 0 18px;padding:8px 10px;overflow-x:auto}nav a{font-size:15px;font-weight:650;padding:8px 12px;margin:0;border-radius:7px;color:#25313f}.brand{font-weight:750}nav a.active{background:var(--accent-soft);color:#1f635e}"
                            ".footerbar{margin-top:18px;padding:9px 12px}.syslinks a{background:#f1f4f4;color:var(--muted)}.heap{color:var(--muted)}");
    Esp32BaseWeb::sendChunk(".muted{color:var(--muted)}.hint{display:block;color:var(--muted);font-size:12px;margin:3px 0 0}.panel{padding:12px;margin:12px 0}.panel h3{padding-bottom:6px;margin-bottom:8px;border-bottom:1px solid #eef2f1}"
                            ".panel-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px;padding-bottom:7px;border-bottom:1px solid #eef2f1}.panel-head h3{padding:0;margin:0;border:0}");
    Esp32BaseWeb::sendChunk(".records-top-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0;margin:0 0 10px;align-items:stretch;background:var(--surface);border:1px solid var(--line);border-radius:6px;box-shadow:0 1px 2px rgba(20,34,38,.025);overflow:hidden}"
                            ".records-top-grid .records-diagnostic-panel{display:flex;flex-direction:column;min-width:0;margin:0;padding:10px 12px;border:0;border-left:1px solid #edf2f1;border-radius:0;box-shadow:none}.records-top-grid .records-diagnostic-panel:first-child{border-left:0}"
                            ".diagnostic-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px}.diagnostic-head h3{padding:0;margin:0;border:0;font-size:13px;font-weight:750;white-space:nowrap}.diagnostic-metric-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px 10px;margin:0}.diagnostic-metric-grid.three{grid-template-columns:repeat(2,minmax(0,1fr))}.diagnostic-metric{min-width:0}.diagnostic-metric span{display:block;margin-bottom:2px;color:var(--muted);font-size:11px;font-weight:600}.diagnostic-metric strong{display:block;color:var(--text);font-size:14px;line-height:1.2;font-weight:650;font-variant-numeric:tabular-nums;white-space:nowrap;overflow-wrap:normal}.metering-status-diagnostic .diagnostic-metric strong,.sample-coverage-diagnostic .diagnostic-metric strong{font-size:15px}.sample-coverage-diagnostic{margin:8px 0 10px}.diagnostic-foot{display:flex;flex-wrap:wrap;gap:4px 10px;margin-top:auto;padding-top:7px;border-top:1px solid #f1f4f3;color:var(--muted);font-size:11px;line-height:1.35;font-variant-numeric:tabular-nums}.diagnostic-foot b{color:#52616b;font-weight:650;white-space:nowrap}.ram-badge{background:#eef6f8;color:#246270}.flash-badge{background:#f5f1e8;color:#73520f}.trace-badge{display:inline-flex;align-items:center;min-height:22px;padding:0 8px;border:1px solid #cfe4dc;border-radius:999px;background:var(--accent-soft);color:#17635b;font-size:12px;font-weight:700;line-height:1;white-space:nowrap;vertical-align:middle}.trace-source-link{text-decoration:none}.trace-source-link:hover,.trace-source-link:focus-visible{background:#10574e;border-color:#10574e;color:#fff}"
                            ".pulse-cell{font-variant-numeric:tabular-nums}.inline-note{display:inline-flex;align-items:center;min-height:20px;margin-left:6px;padding:0 7px;border-radius:999px;background:#eef3f2;color:var(--muted);font-size:12px;font-weight:500;white-space:nowrap}.inline-note.ok,.measured-note{background:#e8f4ee;color:#21634c}");
    Esp32BaseWeb::sendChunk(".pulse-detail-chart{padding:10px 0 2px;overflow-x:auto}.pulse-detail-chart svg{display:block;width:100%;min-width:760px;height:auto}.pulse-detail-chart .axis{stroke:#d9e0df;stroke-width:1}.pulse-detail-chart .grid-line{stroke:#edf2f1;stroke-width:1}.pulse-line{fill:none;stroke:var(--accent);stroke-width:3;stroke-linejoin:round;stroke-linecap:round}.raw-line{fill:none;stroke:#8fb5bd;stroke-width:2;stroke-linejoin:round;stroke-linecap:round;opacity:.62}.pulse-dot{fill:var(--surface);stroke:var(--accent);stroke-width:2}.raw-dot{fill:#eef7f7;stroke:#8fb5bd;stroke-width:1.4;opacity:.72}.pause-window{fill:#f2e7cd;opacity:.42}.pause-boundary{stroke:#9c6a12;stroke-width:2;stroke-dasharray:7 5;opacity:.7}.stable-line{stroke:#a36b10;stroke-width:2;stroke-dasharray:7 5}.chart-label{font-size:12px;fill:var(--muted)}.chart-y-label{text-anchor:end}.chart-raw-y-label{text-anchor:start;fill:#8fb5bd}.chart-x-label{text-anchor:middle}.chart-legend{display:flex;align-items:center;gap:14px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin:6px 0 0}.legend-mark{display:inline-block;width:18px;height:3px;border-radius:999px;margin-right:5px;vertical-align:middle}.legend-pulse{background:var(--accent)}.legend-raw{background:#8fb5bd;opacity:.62}.legend-paused{background:transparent;border-top:3px dashed #9c6a12;height:0;border-radius:0}.legend-stable{background:#a36b10}.trace-frequency{margin-left:auto}.trace-frequency-label{color:var(--muted);font-size:12px;font-weight:650;margin-right:3px}.trace-frequency a.page-current{background:var(--accent);border-color:var(--accent);color:#fff;font-weight:750}");
    Esp32BaseWeb::sendChunk(".grid,.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin:0 0 12px}"
                            ".metric-card{padding:12px 14px;min-height:54px}.metric-card.primary{border-color:#b8d7cf;background:#f7fbfa}.metric-card span{display:block;color:var(--muted);font-size:13px;font-weight:500;margin-bottom:4px}.metric-card strong{display:block;color:var(--text);font-size:18px;line-height:1.2;font-weight:500}"
                            ".machine-status{padding:14px 16px;margin:0 0 14px;border-color:#d8e1e6;background:#fbfcfd}"
                            ".machine-main{display:grid;grid-template-columns:minmax(280px,.36fr) minmax(0,.64fr);gap:16px;align-items:stretch}.machine-main.compact{grid-template-columns:minmax(250px,.36fr) minmax(0,.64fr)}.machine-hero{display:flex;flex-direction:column;justify-content:center;min-height:106px}.machine-hero-head{display:grid;grid-template-columns:max-content minmax(0,1fr);align-items:center;gap:14px}.machine-hero strong{display:block;font-size:31px;line-height:1.05;font-weight:700}.machine-context{display:flex;flex-direction:column;gap:3px;min-width:0}.machine-alert{margin:0;color:#8a6f3d;font-size:13px;font-weight:400;line-height:1.35}.machine-preset-line{margin:0;color:var(--muted);font-size:13px;font-weight:400;line-height:1.35}.machine-progress{margin-top:12px}.machine-progress-head{display:flex;align-items:center;justify-content:space-between;gap:10px;color:var(--muted);font-size:13px;font-weight:400;margin-bottom:7px}.progress{height:9px;background:#e2e9e7;border-radius:999px;overflow:hidden}.progress span{display:block;height:100%;background:var(--accent);border-radius:999px}.machine-overview{display:flex;flex-direction:column;gap:8px;min-width:0}.machine-task-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.machine-task-card{display:flex;flex-direction:column;justify-content:center;min-height:68px;padding:11px 12px;border:1px solid #dde6eb;border-radius:7px;background:#fff;box-shadow:0 1px 2px rgba(16,24,40,.025)}.machine-task-card span{display:block;color:var(--muted);font-size:12px;font-weight:400;margin-bottom:3px}.machine-task-card strong{display:block;color:var(--text);font-size:17px;line-height:1.2;font-weight:600}.machine-task-card small{display:block;margin-top:4px;color:var(--muted);font-size:12px;line-height:1.2;font-weight:400}.machine-status-strip{display:flex;align-items:center;gap:7px;flex-wrap:wrap}.machine-status-item{display:inline-flex;align-items:center;gap:5px;min-height:28px;padding:0 9px;border:1px solid #dce4ea;border-radius:999px;background:#f7f9fb;color:#66737c;font-size:12px;font-weight:400;line-height:1}.machine-status-item strong{color:#35424c;font-size:13px;font-weight:600;line-height:1}.machine-status-note{color:#7a858e;font-size:11px;font-weight:400;line-height:1}");
    Esp32BaseWeb::sendChunk(".today-layout{display:grid;grid-template-columns:minmax(190px,.28fr) minmax(0,1.72fr);gap:12px;margin:0 0 14px}.today-summary-card{display:flex;flex-direction:column;justify-content:flex-start;min-height:92px;padding:14px 16px}.today-summary-label{display:block;color:var(--muted);font-size:13px;font-weight:400;line-height:1.35;margin-bottom:6px}.today-total-main{display:block;color:var(--text);font-size:26px;line-height:1.05;font-weight:700}.today-total-meta{display:flex;align-items:center;flex-wrap:wrap;gap:3px 8px;color:var(--muted);font-size:13px;font-weight:400;margin-top:8px}.today-meta-item{display:inline-flex;align-items:baseline;gap:3px;white-space:nowrap}.today-meta-item+.today-meta-item:before{content:'·';margin-right:5px;color:#a2adb4}.today-meta-value{color:#52616b;font-weight:500}.today-records{padding:8px 10px;overflow-x:auto}.today-record-table{min-width:680px;margin:0;border:0;border-radius:0;box-shadow:none;background:transparent;font-size:13px}.today-record-table th,.today-record-table td{padding:6px 8px}.today-record-table th{background:transparent}.today-record-table .record-duration{white-space:nowrap}.today-record-table .status-pill{justify-content:center}");
    Esp32BaseWeb::sendChunk(".filter-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px;margin:0 0 12px}.filter-card{padding:12px 14px;min-height:128px}.filter-head{display:flex;align-items:flex-start;justify-content:space-between;gap:8px;margin-bottom:8px}.filter-head strong{font-size:16px;line-height:1.25;font-weight:750}"
                            ".filter-meta{display:grid;gap:4px;color:var(--muted);font-size:13px;margin-top:10px}.dual-progress{display:grid;gap:7px;margin:8px 0 10px}.filter-progress-row{display:grid;grid-template-columns:48px 1fr;gap:8px;align-items:center;color:var(--muted);font-size:12px}.filter-track{display:block;height:7px;background:#edf3f1;border:1px solid #d7e3e0;border-radius:999px;overflow:hidden}.filter-progress-fill{display:block;height:100%;border-radius:999px}.day-progress{background:var(--accent)}.flow-progress{background:#c9822c}");
    Esp32BaseWeb::sendChunk(".status-pill{display:inline-flex;align-items:center;min-height:22px;padding:0 9px;border-radius:999px;background:#eef2f2;color:#55616a;font-size:12px;font-weight:650;line-height:1;white-space:nowrap}.status-ok{background:#e8f4ee;color:#21634c;border-color:#bdddcf}.status-warn{background:#fff7e6;color:#7a520e;border-color:#eed28f}.status-error{background:#fff0ee;color:#9b3328;border-color:#efc1ba}.status-muted{background:#eef2f2;color:#66737c;border-color:#d8e0df}.warn{display:inline-block;background:#fff8e6;border:1px solid #ead28b;border-radius:8px;padding:7px 9px;color:#6b4a12;margin:0 0 10px}.filter-used-days{font-variant-numeric:tabular-nums}.filter-progress-label{display:grid;grid-template-columns:48px 1fr;gap:6px;align-items:center;color:var(--muted);font-size:12px}");
    Esp32BaseWeb::sendChunk(".stats-layout{display:grid;grid-template-columns:minmax(0,1fr);gap:10px;align-items:start}.stats-layout .metric-card{border-radius:6px}.stats-metric-card span{font-weight:500}.stats-metric-card strong{font-weight:600}.stats-card-meta{display:block;margin-top:8px;color:var(--muted);font-size:12px;font-weight:400;line-height:1.25}.daily-chart{padding:16px 18px 14px;margin:0 0 12px;overflow-x:auto;border-radius:6px}.chart-head{display:flex;align-items:baseline;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:6px}.chart-head h3{margin:0;font-size:16px;font-weight:600}.chart-unit{color:var(--muted);font-size:12px;font-weight:400}");
    Esp32BaseWeb::sendChunk(".daily-chart svg{display:block;width:100%;height:auto;min-width:980px;margin-top:2px}.daily-chart .bar{fill:var(--accent)}.daily-chart .empty-bar{fill:#d5ddda}.daily-chart .axis,.daily-chart .count-axis{stroke:#d6e0dd;stroke-width:1}.daily-chart .grid{stroke:#edf4f2;stroke-width:1}.daily-chart .chart-y-tick{font-size:11px;text-anchor:end;fill:var(--muted);font-weight:400}.daily-chart .count-y-tick{font-size:11px;text-anchor:start;fill:#9aa6ab;font-weight:400}.daily-chart .x-label{font-size:11px;text-anchor:end;fill:var(--muted);font-weight:400}.daily-chart .bar-label{font-size:11px;text-anchor:middle;fill:var(--muted);font-weight:400}.daily-chart .count-line-halo{fill:none;stroke:#fff;stroke-width:2;stroke-linejoin:round;stroke-linecap:round;opacity:.55}.daily-chart .count-line{fill:none;stroke:#acbbc1;stroke-width:1.15;stroke-linejoin:round;stroke-linecap:round}.daily-chart .count-dot{fill:#acbbc1;stroke:#fff;stroke-width:.6}.daily-chart .count-label{font-size:10px;text-anchor:middle;fill:#7d8b92;font-weight:400}");
    Esp32BaseWeb::sendChunk(".distribution-head{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin:18px 0 10px}.distribution-head h2{margin:0;font-size:16px;font-weight:600}.distribution-scope{color:var(--muted);font-size:12px;font-weight:400}.usage-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px;margin:0 0 12px}.usage-panel{padding:14px 16px;border-radius:6px}.usage-panel h3{margin-bottom:12px;font-size:15px;font-weight:600}.usage-row{display:grid;grid-template-columns:minmax(72px,1fr) auto;gap:6px 10px;align-items:center;margin:0 0 10px;color:var(--muted);font-size:13px}.usage-row strong{color:var(--text);font-weight:500}.usage-row small{grid-column:1/-1;margin-top:-4px;color:var(--muted);font-size:12px;font-weight:400}.usage-bar{grid-column:1/-1;height:5px;background:#edf3f1;border-radius:4px;overflow:hidden}.usage-bar i{display:block;height:100%;background:var(--accent);border-radius:4px}");
    Esp32BaseWeb::sendChunk(".form-grid{display:grid;grid-template-columns:repeat(12,1fr);gap:10px 12px;align-items:start}.span-2{grid-column:span 2}.span-3{grid-column:span 3}.span-4{grid-column:span 4}.span-5{grid-column:span 5}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:1/-1}"
                            ".field span,.check-title{display:block;font-size:12px;color:var(--muted);font-weight:650;margin-bottom:4px}.field input,.field select{margin-bottom:0}.check-line{display:inline-flex;align-items:center;gap:6px;min-height:32px;padding:0 8px;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--text);font-size:14px;white-space:nowrap}.check-line input{margin:0}");
    Esp32BaseWeb::sendChunk(".form-actions{display:flex;align-items:center;justify-content:flex-start;gap:6px;margin-top:10px;flex-wrap:wrap}.form-actions form{margin:0}.form-actions a,.btn-link,.page-link,.page-current,.row-actions a{display:inline-flex;align-items:center;justify-content:center;min-height:32px;padding:0 10px;border:1px solid var(--line);border-radius:6px;background:#f7f9fa;color:#355e66;font:inherit;font-size:13px;line-height:1.2;box-sizing:border-box;text-decoration:none;cursor:pointer}input.secondary{background:#f7f9fa;border:1px solid var(--line);color:#4c565d}input.secondary:hover,input.secondary:focus-visible{background:#10574e;border-color:#10574e;color:#fff}.btn-link:hover,.btn-link:focus-visible,.form-actions a:hover,.form-actions a:focus-visible,.page-link:hover,.page-link:focus-visible,.row-actions a:hover,.row-actions a:focus-visible{background:#10574e;border-color:#10574e;color:#fff;text-decoration:none}.row-actions{display:flex;gap:5px;align-items:center;flex-wrap:wrap}.latest-actions{gap:6px}.latest-record-panel{overflow-x:auto}.latest-record-table{min-width:900px}.latest-calibration-edit-row{display:none;background:#fbfcfb}.latest-calibration-edit-row.is-open{display:table-row}.latest-calibration-edit-row td{border-top:1px solid #dce8e5}.latest-calibration-form{display:grid;grid-template-columns:minmax(160px,220px) minmax(0,1fr);gap:8px 12px;align-items:end;max-width:720px;margin:0}.latest-volume-field{margin:0}.latest-volume-field input{width:140px;margin:0;text-align:right}.latest-calibration-form .hint{grid-column:2;margin:0}.latest-calibration-form .form-actions{grid-column:1/-1;margin-top:4px}.sample-window-form{display:flex;align-items:flex-end;gap:8px;flex-wrap:wrap;margin:8px 0 4px}.sample-window-field{margin:0}.sample-window-field input{width:96px;margin:0;text-align:right}.latest-status-pills,.sample-status-pills{display:flex;align-items:center;gap:5px;flex-wrap:wrap}.calibration-formula-panel code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;color:#36505b;background:#f3f6f5;border-radius:4px;padding:1px 4px}");
    Esp32BaseWeb::sendChunk(".calibration-param-layout{display:grid;grid-template-columns:1fr;gap:12px;margin:0 0 12px}.calibration-param-layout .calibration-param-panel{margin:0}.calibration-param-panel{overflow-x:auto}.calibration-slot-table{table-layout:fixed;min-width:860px;margin:0;border:0;border-radius:0;box-shadow:none;background:transparent}.calibration-slot-table th:nth-child(1){width:160px}.calibration-slot-table th:nth-child(2){width:260px}.calibration-slot-table th:nth-child(3){width:auto}.calibration-slot-table th:nth-child(4){width:92px}.calibration-slot-table th:nth-child(5){width:230px}.calibration-slot-index{font-weight:700}.calibration-slot-index .status-pill{display:inline-flex;margin:4px 4px 0 0}.calibration-slot-name{font-weight:650;white-space:normal}.calibration-slot-values{min-width:0}.calibration-slot-values span{display:inline-flex;align-items:center;min-height:24px;margin:0 5px 4px 0;padding:0 7px;border:1px solid #dce6e3;border-radius:999px;background:#f8fbfa;font-variant-numeric:tabular-nums;white-space:nowrap}.calibration-slot-values b{margin-right:4px;color:var(--muted);font-size:11px}.calibration-slot-note{color:#536068;font-size:12px;line-height:1.35;overflow-wrap:anywhere}.scheme-use-count{font-variant-numeric:tabular-nums}.scheme-use-count b{font-size:18px;margin-right:3px}.scheme-use-count span{color:var(--muted)}.scheme-use-count .status-pill{display:flex;width:max-content;margin-top:5px}.calibration-slot-edit .row-actions{gap:5px}.generated-scheme-result{margin-top:10px}.generated-scheme-table{table-layout:fixed;margin:0;border:0;border-radius:0;box-shadow:none;background:transparent}.generated-scheme-table th,.generated-scheme-table td{white-space:nowrap}.generated-note{margin:8px 0 0;color:#536068;font-size:12px;line-height:1.45}.inline-form{display:flex;align-items:end;gap:6px;flex-wrap:wrap}.generated-name-field input{width:190px}.calibration-slot-form{display:grid;grid-template-columns:minmax(128px,1.3fr) repeat(3,98px) 82px;gap:5px;align-items:end;margin:0}.compact-field{display:block;margin:0}.compact-field span{display:block;margin:0 0 3px;color:var(--muted);font-size:11px;font-weight:650;line-height:1.1}.compact-field input{width:100%;height:30px;min-height:30px;margin:0;padding:0 7px;box-sizing:border-box;font:inherit}.calibration-slot-form input[type=number]{text-align:right}.calibration-slot-form input[type=submit]{height:30px;min-height:30px;margin:0;padding:0 8px;font-size:12px}");
    Esp32BaseWeb::sendChunk("table{width:100%;border-collapse:separate;border-spacing:0;margin:0 0 12px;overflow:hidden;font-size:13px}td,th{padding:8px 10px;border-bottom:1px solid #edf1f0;text-align:left;vertical-align:middle}tr:last-child td{border-bottom:0}th{background:#f8faf9;color:var(--muted);font-weight:700}.filters-table th:first-child{width:22%}.filters-table th:last-child{width:150px}.kv th{width:26%}");
    Esp32BaseWeb::sendChunk(".pager{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;margin:0 0 10px}.pager-links{display:flex;align-items:center;gap:5px;flex-wrap:wrap}.page-current{background:var(--accent-soft);color:#17635b;border-color:#cfe4dc}.page-disabled{color:#9aa3aa;background:#f4f6f6;pointer-events:none}.page-size{display:flex;align-items:center;gap:6px;color:var(--muted);font-size:13px;line-height:32px}.page-size select{width:auto;min-width:80px}.page-size select,.page-size input{height:32px;min-height:32px;margin:0;padding:0 9px;box-sizing:border-box;font:inherit;line-height:32px}.page-size input[name=pageNo]{width:58px;text-align:center}");
    Esp32BaseWeb::sendChunk(".disabled-row{background:#f7f8f8;color:#8a949b}.disabled-row td{color:#8a949b}.disabled-row .status-pill{background:#eef0f0;color:#7b858d}.disabled-row a{color:#6f7a82}"
                            "@media(max-width:1040px){.records-top-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.records-top-grid .records-diagnostic-panel{border-left:1px solid #edf2f1;border-top:1px solid #edf2f1}.records-top-grid .records-diagnostic-panel:nth-child(odd){border-left:0}.records-top-grid .records-diagnostic-panel:nth-child(-n+2){border-top:0}}"
                            "@media(max-width:820px){.machine-main,.machine-main.compact,.today-layout{grid-template-columns:1fr}.machine-hero{min-height:0}.machine-hero strong{font-size:26px}.machine-hero-head{grid-template-columns:1fr;align-items:start;gap:5px}.machine-task-grid{grid-template-columns:repeat(auto-fit,minmax(150px,1fr))}}"
                            "@media(max-width:720px){body{padding:10px}.form-grid{grid-template-columns:1fr}.span-2,.span-3,.span-4,.span-5,.span-6,.span-8,.span-12{grid-column:1/-1}.usage-grid{grid-template-columns:1fr}.daily-chart svg{min-width:680px}}"
                            "@media(max-width:620px){.records-top-grid{grid-template-columns:1fr}.records-top-grid .records-diagnostic-panel{border-left:0;border-top:1px solid #edf2f1}.records-top-grid .records-diagnostic-panel:first-child{border-top:0}.latest-calibration-form{grid-template-columns:1fr}.latest-calibration-form .hint{grid-column:1}}"
                            "@media(max-width:520px){.grid,.metric-grid,.diagnostic-metric-grid,.diagnostic-metric-grid.three,.filter-cards,.machine-task-grid{grid-template-columns:1fr}.metric-card{min-height:0}.pager{align-items:flex-start}.page-size{width:100%}.kv th{width:34%}}");
}

void sendAppStylesheetLink() {
    Esp32BaseWeb::sendChunk("<link rel='stylesheet' href='/faucet/app.css?v=" FAUCET_WEB_CSS_VERSION "'>");
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

std::uint32_t roundedPercent(std::uint32_t value, std::uint32_t total) {
    if (total == 0) {
        return 0;
    }
    const std::uint64_t percent =
        (static_cast<std::uint64_t>(value) * 100ULL + static_cast<std::uint64_t>(total) / 2ULL) / total;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

void sendCountVolumeDistributionRow(const char* label, std::uint32_t count, std::uint32_t volumeMl, std::uint32_t totalCount) {
    char volume[24]{};
    formatLiters(volumeMl, volume, sizeof(volume));
    const std::uint32_t percent = roundedPercent(count, totalCount);
    sendFmt("<div class='usage-row'><span>%s</span><strong>%lu 次</strong><small>占 %lu%% · 合计 %s</small>"
            "<div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            label,
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent),
            volume,
            static_cast<unsigned long>(percent));
}

void sendCountDistributionRow(const char* label, std::uint32_t count, std::uint32_t totalCount) {
    const std::uint32_t percent = roundedPercent(count, totalCount);
    sendFmt("<div class='usage-row'><span>%s</span><strong>%lu 次</strong><small>占 %lu%%</small>"
            "<div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            label,
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent),
            static_cast<unsigned long>(percent));
}

const char* resultLabel(std::size_t index) {
    switch (static_cast<WaterResult>(index)) {
        case WaterResult::Completed:
            return "正常完成";
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

void sendUsagePatterns(const WaterUsageSummary& summary, const SystemConfig& config) {
    Esp32BaseWeb::sendChunk("<div class='distribution-head'><h2>最近 30 天分布</h2>"
                            "<span class='distribution-scope'>以下占比均按最近 30 天记录次数统计</span></div>"
                            "<div class='usage-grid'>");
    Esp32BaseWeb::sendChunk("<section class='usage-panel'><h3>按预设分布</h3>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (summary.presetCounts[i].count == 0) {
            continue;
        }
        char label[40]{};
        char name[kPresetNameLength]{};
        std::strncpy(name, config.presets[i].name, sizeof(name) - 1);
        std::snprintf(label, sizeof(label), "%u %s", static_cast<unsigned>(i + 1), name);
        sendCountVolumeDistributionRow(label, summary.presetCounts[i].count, summary.presetCounts[i].volumeMl, summary.last30DaysCount);
    }
    if (summary.last30DaysMl == 0) {
        Esp32BaseWeb::sendChunk("<p class='hint'>最近 30 天没有可聚合的真实时间记录。</p>");
    }
    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>按容量段分布</h3>");
    static constexpr const char* histLabels[kUsageVolumeHistCount] = {"0.5 L 以下", "0.5 - 2 L", "2 - 5 L", "5 - 10 L", "10 L 以上"};
    for (std::size_t i = 0; i < kUsageVolumeHistCount; ++i) {
        sendCountVolumeDistributionRow(histLabels[i], summary.volumeHist[i].count, summary.volumeHist[i].volumeMl, summary.last30DaysCount);
    }
    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>按完成结果分布</h3>");
    for (std::size_t i = 0; i < kUsageResultCount; ++i) {
        sendCountDistributionRow(resultLabel(i), summary.resultCounts[i], summary.last30DaysCount);
    }
    Esp32BaseWeb::sendChunk("</section></div>");
}

void sendMachineTaskCard(const char* valueId, const char* metaId, const char* label, const char* value, const char* meta) {
    sendFmt("<div class='machine-task-card'><span>%s</span><strong id='%s'>%s</strong><small id='%s'>%s</small></div>",
            label,
            valueId,
            value,
            metaId,
            meta);
}

void sendMachineStatusItem(const char* valueId,
                           const char* label,
                           const char* value,
                           const char* wrapperId = nullptr,
                           bool hidden = false) {
    if (wrapperId != nullptr) {
        sendFmt("<span id='%s' class='machine-status-item'%s><span>%s</span><strong id='%s'>%s</strong></span>",
                wrapperId,
                hidden ? " style='display:none'" : "",
                label,
                valueId,
                value);
        return;
    }
    sendFmt("<span class='machine-status-item'><span>%s</span><strong id='%s'>%s</strong></span>",
            label,
            valueId,
            value);
}

void sendMachineStatusItemNote(const char* valueId, const char* noteId, const char* label, const char* value, const char* note) {
    sendFmt("<span class='machine-status-item'><span>%s</span><strong id='%s'>%s</strong><small id='%s' class='machine-status-note'>%s</small></span>",
            label,
            valueId,
            value,
            noteId,
            note);
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
                  "P%u · %s · %s",
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
    char elapsedText[24]{};
    formatSecondsValue(snapshot.water.elapsedSec, elapsedText, sizeof(elapsedText));
    char targetMeta[16]{};
    std::snprintf(targetMeta, sizeof(targetMeta), "%s模式", modeText(snapshot.water.mode));
    char outputMeta[32]{};
    std::snprintf(outputMeta, sizeof(outputMeta), "已运行 %s", elapsedText);
    char remainingMeta[24]{};
    std::snprintf(remainingMeta, sizeof(remainingMeta), "完成 %lu%%", static_cast<unsigned long>(progressPercent));
    char pulsePerLiter[24]{};
    if (snapshot.pulsePerLiter > 0) {
        std::snprintf(pulsePerLiter, sizeof(pulsePerLiter), "%lu脉冲/L", static_cast<unsigned long>(snapshot.pulsePerLiter));
    } else {
        std::snprintf(pulsePerLiter, sizeof(pulsePerLiter), "未校准");
    }
    char droppedPulses[24]{};
    std::snprintf(droppedPulses, sizeof(droppedPulses), "%lu", static_cast<unsigned long>(snapshot.flowDroppedPulses));
    char valvePwmDuty[12]{};
    std::snprintf(valvePwmDuty, sizeof(valvePwmDuty), "%u%%", static_cast<unsigned>(snapshot.valve.dutyPercent));
    char valvePwmNote[32]{};
    std::snprintf(valvePwmNote,
                  sizeof(valvePwmNote),
                  "%lus全功率→%u%%保持",
                  static_cast<unsigned long>(g_context.config->valveFullPowerSec),
                  static_cast<unsigned>(g_context.config->valveHoldDutyPercent));

    const bool showResult = snapshot.water.state == WaterState::Error || snapshot.localMode == LocalUiMode::Result;
    const bool showRunningNote = snapshot.water.state == WaterState::Running;
    const char* machineLayoutClass = shouldShowProgress ? "machine-main" : "machine-main compact";
    sendFmt("<h2>机器状态</h2><section class='panel machine-status'><div class='%s'><div class='machine-hero'>"
            "<div class='machine-hero-head'><strong>",
            machineLayoutClass);
    Esp32BaseWeb::sendChunk("<span id='machineState'>");
    Esp32BaseWeb::sendChunk(stateText(snapshot.water.state));
    sendFmt("</span></strong><div class='machine-context'><p id='machineStatusNote' class='machine-alert'%s>%s</p>"
            "<p class='machine-preset-line'>当前预设 <span id='machinePreset'>%s</span></p></div></div>",
            showRunningNote ? "" : " style='display:none'",
            machineStatusNote(snapshot),
            currentPreset);
    sendFmt("<div id='machineProgress' class='machine-progress'%s><div class='machine-progress-head'><span>出水进度</span><strong id='machineProgressText'>%s</strong></div>"
            "<div class='progress'><span id='machineProgressBar' style='width:%lu%%'></span></div></div>",
            shouldShowProgress ? "" : " style='display:none'",
            progressText,
            static_cast<unsigned long>(progressPercent));
    Esp32BaseWeb::sendChunk("</div><div class='machine-overview'><div class='machine-task-grid'>");
    sendMachineTaskCard("targetValue", "targetMeta", "目标", targetValue, targetMeta);
    sendMachineTaskCard("outputValue", "outputMeta", "已出水", outValue, outputMeta);
    sendMachineTaskCard("remainingValue", "remainingMeta", "剩余", remainingValue, remainingMeta);
    Esp32BaseWeb::sendChunk("</div><div class='machine-status-strip'>");
    sendMachineStatusItem("valveStatus", "阀门", snapshot.water.valveOpen ? "开" : "关");
    sendMachineStatusItemNote("valvePwmDuty", "valvePwmNote", "PWM", valvePwmDuty, valvePwmNote);
    sendMachineStatusItem("pulsePerLiter", "流量计", pulsePerLiter);
    sendMachineStatusItem("screenStatus", "屏幕", screenOn ? "亮屏" : "休眠");
    sendMachineStatusItem("resultStatus", "结果", resultText(snapshot.water.lastResult), "resultItem", !showResult);
    sendMachineStatusItem("droppedPulses", "丢弃脉冲", droppedPulses, "droppedPulsesItem", snapshot.flowDroppedPulses == 0);
    Esp32BaseWeb::sendChunk("</div></div></div></section>");
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
                            "var base=s.mode==='time'?s.elapsedSec:s.volumeMl;"
                            "var pct=s.targetValue>0?Math.min(100,Math.floor(base*100/s.targetValue)):0;"
                            "faucetHomeActive=shown;"
                            "faucetSet('machineState',faucetStateText(s.state));"
                            "faucetSet('machineStatusNote',faucetStatusNote(s.state,s.lastResult));"
                            "faucetToggle('machineStatusNote',s.state==='running');"
                            "faucetSet('machinePreset','P'+(Number(s.selectedPreset)+1)+' · '+faucetModeText(s.mode)+' · '+target);"
                            "faucetSet('targetValue',target);faucetSet('outputValue',out);"
                            "faucetSet('remainingValue',s.mode==='time'?faucetSeconds(remaining):faucetLiters(remaining));"
                            "faucetSet('targetMeta',faucetModeText(s.mode)+'模式');"
                            "faucetSet('outputMeta','已运行 '+faucetSeconds(s.elapsedSec));"
                            "faucetSet('remainingMeta','完成 '+pct+'%');"
                            "faucetSet('resultStatus',faucetResultText(s.lastResult));"
                            "faucetSet('valveStatus',s.valveOpen?'开':'关');"
                            "faucetSet('valvePwmDuty',s.valveDutyPercent+'%');"
                            "faucetSet('valvePwmNote',s.valveFullPowerSec+'s全功率→'+s.valveHoldDutyPercent+'%保持');"
                            "faucetSet('pulsePerLiter',s.pulsePerLiter>0?s.pulsePerLiter+'脉冲/L':'未校准');"
                            "faucetSet('screenStatus',s.screenOn?'亮屏':'休眠');"
                            "faucetSet('droppedPulses',Number(s.flowDroppedPulses)||0);"
                            "faucetToggle('resultItem',s.state==='error');"
                            "faucetToggle('droppedPulsesItem',(Number(s.flowDroppedPulses)||0)>0);"
                            "var main=document.querySelector('.machine-main');if(main){main.className=shown?'machine-main':'machine-main compact';}"
                            "var p=document.getElementById('machineProgress');if(p){p.style.display=shown?'block':'none';}"
                            "if(shown){"
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
    char todayMeta[32]{};
    char monthMeta[48]{};
    char average30Meta[48]{};
    char totalMeta[32]{};
    formatLiters(summary.todayMl, today, sizeof(today));
    formatLiters(summary.monthMl, month, sizeof(month));
    formatLiters(snapshot.statistics.totalMl, total, sizeof(total));
    formatLiters(summary.last30DaysDailyAverageMl, average30, sizeof(average30));
    formatMonthRange(summary, monthRange, sizeof(monthRange));
    const std::uint32_t monthDays =
        summary.monthStartDay > 0 && summary.todayDay >= summary.monthStartDay
            ? summary.todayDay - summary.monthStartDay + 1UL
            : 30UL;
    const float monthDailyCount = monthDays == 0 ? 0.0f : static_cast<float>(summary.monthCount) / monthDays;
    const float last30DailyCount = static_cast<float>(summary.last30DaysCount) / 30.0f;
    std::snprintf(todayMeta, sizeof(todayMeta), "今日 %lu 次", static_cast<unsigned long>(summary.todayCount));
    std::snprintf(monthMeta,
                  sizeof(monthMeta),
                  "日均 %.1f 次 · 总共 %lu 次",
                  static_cast<double>(monthDailyCount),
                  static_cast<unsigned long>(summary.monthCount));
    std::snprintf(average30Meta,
                  sizeof(average30Meta),
                  "日均 %.1f 次 · 总共 %lu 次",
                  static_cast<double>(last30DailyCount),
                  static_cast<unsigned long>(summary.last30DaysCount));
    std::snprintf(totalMeta, sizeof(totalMeta), "累计 %lu 次", static_cast<unsigned long>(summary.totalCount));
    Esp32BaseWeb::sendHeader("用水统计");
    Esp32BaseWeb::sendChunk("<h2>统计</h2>");
    if (summary.unknownCount > 0) {
        sendFmt("<p class='warn'>⚠ 含 %lu 条无时间记录，未纳入按日期图表。</p>",
                static_cast<unsigned long>(summary.unknownCount));
    }
    Esp32BaseWeb::sendChunk("<div class='stats-layout'><div><div class='metric-grid'>");
    sendStatsMetricCard("今日", today, todayMeta);
    char monthTitle[48]{};
    std::snprintf(monthTitle, sizeof(monthTitle), "本月%s%s", monthRange[0] ? " " : "", monthRange);
    sendStatsMetricCard(monthTitle, month, monthMeta);
    sendStatsMetricCard("过去 30 天日均", average30, average30Meta);
    sendStatsMetricCard("总累计", total, totalMeta);
    Esp32BaseWeb::sendChunk("</div></div>");
    if (now >= kMinRealDateSeconds) {
        sendDailyChart(summary);
    } else {
        sendTimeUnsyncedChartNotice();
    }
    Esp32BaseWeb::sendChunk("</div>");
    sendUsagePatterns(summary, *g_context.config);
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
    std::uint32_t requestedPageNo = 0;
    if (getParam("pageNo", text, sizeof(text)) && parseU32(text, requestedPageNo) && requestedPageNo > 0) {
        page = requestedPageNo - 1;
    } else if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    std::uint32_t requestedPageSize = kDefaultRecordPageSize;
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, requestedPageSize);
    }
    const std::uint16_t pageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(requestedPageSize));
    WaterRecordFilter filter{};
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
    Esp32BaseWeb::sendChunk("<div class='pager'><div class='pager-links'>");
    const bool hasPrev = ready && total > 0 && page > 0;
    const bool hasNext = ready && total > 0 && page < filteredMaxPage;
    if (hasPrev) {
        sendFmt("<a class='page-link' href='/faucet/records?page=0&pageSize=%u'>首页</a>"
                "<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u'>上一页</a>",
                static_cast<unsigned>(pageSize),
                static_cast<unsigned long>(page - 1),
                static_cast<unsigned>(pageSize));
    } else {
        Esp32BaseWeb::sendChunk("<span class='page-link page-disabled'>首页</span><span class='page-link page-disabled'>上一页</span>");
    }
    sendFmt("<span class='page-current'>第 %lu / %lu 页</span>",
            static_cast<unsigned long>(page + 1),
            static_cast<unsigned long>(filteredMaxPage + 1));
    if (hasNext) {
        sendFmt("<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u'>下一页</a>"
                "<a class='page-link' href='/faucet/records?page=%lu&pageSize=%u'>末页</a>",
                static_cast<unsigned long>(page + 1),
                static_cast<unsigned>(pageSize),
                static_cast<unsigned long>(filteredMaxPage),
                static_cast<unsigned>(pageSize));
    } else {
        Esp32BaseWeb::sendChunk("<span class='page-link page-disabled'>下一页</span><span class='page-link page-disabled'>末页</span>");
    }
    sendFmt("</div><form class='page-size' method='get' action='/faucet/records'>"
            "<span>每页</span><select name='pageSize' onchange='this.form.submit()'>");
    constexpr std::uint16_t sizes[] = {10, 15, 20, 30, 50};
    for (std::uint16_t size : sizes) {
        sendFmt("<option value='%u'%s>%u</option>",
                static_cast<unsigned>(size),
                pageSize == size ? " selected" : "",
                static_cast<unsigned>(size));
    }
    sendFmt("</select><span>条</span><span>跳到第</span>"
            "<input name='pageNo' type='number' min='1' max='%lu' step='1' value='%lu'><span>页</span>"
            "<input class='secondary' type='submit' value='跳转'></form></div><p class='hint'>共 %lu 条记录</p>",
            static_cast<unsigned long>(filteredMaxPage + 1),
            static_cast<unsigned long>(page + 1),
            static_cast<unsigned long>(total));
    if (!ready) {
        Esp32BaseWeb::sendChunk("<p class='err'>记录存储不可用。</p>");
    } else if (total == 0) {
        Esp32BaseWeb::sendChunk("<p class='ok'>暂无出水记录。</p>");
    }
    WaterRecordCalibration* pageCalibrations = nullptr;
    bool* pageCalibrated = nullptr;
    bool pageCalibrationIndexReady = false;
    if (count > 0 && g_context.recordCalibrations && g_context.recordCalibrations->ready()) {
        pageCalibrations = new (std::nothrow) WaterRecordCalibration[count]{};
        pageCalibrated = new (std::nothrow) bool[count]{};
        if (pageCalibrations && pageCalibrated) {
            g_context.recordCalibrations->findAny(records, count, pageCalibrations, pageCalibrated);
            pageCalibrationIndexReady = true;
        } else {
            delete[] pageCalibrations;
            delete[] pageCalibrated;
            pageCalibrations = nullptr;
            pageCalibrated = nullptr;
        }
    }
    WaterRecordMeteringSnapshot* pageMeteringSnapshots = nullptr;
    bool* pageMeteringSnapshotFound = nullptr;
    bool pageMeteringSnapshotIndexReady = false;
    if (count > 0 && g_context.recordMeteringSnapshots && g_context.recordMeteringSnapshots->ready()) {
        pageMeteringSnapshots = new (std::nothrow) WaterRecordMeteringSnapshot[count]{};
        pageMeteringSnapshotFound = new (std::nothrow) bool[count]{};
        if (pageMeteringSnapshots && pageMeteringSnapshotFound) {
            g_context.recordMeteringSnapshots->findAny(
                records, count, pageMeteringSnapshots, pageMeteringSnapshotFound);
            pageMeteringSnapshotIndexReady = true;
        } else {
            delete[] pageMeteringSnapshots;
            delete[] pageMeteringSnapshotFound;
            pageMeteringSnapshots = nullptr;
            pageMeteringSnapshotFound = nullptr;
        }
    }
    const WaterPulseTrace** pageTraces = nullptr;
    bool pageTraceIndexReady = false;
    if (count > 0 && g_context.pulseTraces) {
        pageTraces = new (std::nothrow) const WaterPulseTrace*[count]{};
        if (pageTraces) {
            indexPagePulseTraces(records, count, g_context.pulseTraces, pageTraces);
            pageTraceIndexReady = true;
        }
    }
    const bool savedPulseTracesReady = ensureSavedPulseTracesReady();
    WaterPulseTrace* pageSavedTraces = nullptr;
    bool* pageSavedTraceFound = nullptr;
    bool pageSavedTraceIndexReady = false;
    if (count > 0 && savedPulseTracesReady) {
        pageSavedTraces = new (std::nothrow) WaterPulseTrace[count]{};
        pageSavedTraceFound = new (std::nothrow) bool[count]{};
        if (pageSavedTraces && pageSavedTraceFound) {
            g_context.savedPulseTraces->findByRecords(records, count, pageSavedTraces, pageSavedTraceFound);
            pageSavedTraceIndexReady = true;
        } else {
            delete[] pageSavedTraces;
            delete[] pageSavedTraceFound;
            pageSavedTraces = nullptr;
            pageSavedTraceFound = nullptr;
        }
    }
    Esp32BaseWeb::sendChunk("<table><tr><th>时间</th><th>模式</th><th>目标</th><th>出水</th>"
                            "<th>用时</th><th>脉冲</th><th>计量方案</th><th>结果</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < count; ++i) {
        char startTime[40]{};
        formatWaterRecordListTime(records[i], startTime, sizeof(startTime));
        WaterRecordCalibration calibration{};
        bool calibrated = false;
        if (pageCalibrationIndexReady) {
            calibrated = pageCalibrated[i];
            if (calibrated) {
                calibration = pageCalibrations[i];
            }
        } else {
            calibrated = findRecordCalibration(records[i], calibration);
        }
        const WaterPulseTrace* trace = pageTraceIndexReady ? pageTraces[i]
                                                           : (g_context.pulseTraces
                                                                  ? g_context.pulseTraces->findByRecord(records[i])
                                                                  : nullptr);
        WaterPulseTrace savedTrace{};
        bool hasSavedTrace = false;
        if (pageSavedTraceIndexReady) {
            hasSavedTrace = pageSavedTraceFound[i];
            if (hasSavedTrace) {
                savedTrace = pageSavedTraces[i];
            }
        } else if (savedPulseTracesReady) {
            hasSavedTrace = g_context.savedPulseTraces->findByRecord(records[i], savedTrace);
        }
        const std::uint32_t pulsePerLiter = historicalPulsePerLiter(records[i].pulsePerMlAtRun);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::sendChunk(startTime);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(modeText(records[i].mode));
        Esp32BaseWeb::sendChunk("</td><td>");
        sendTargetValue(records[i]);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendLiters(records[i].volumeMl);
        if (calibrated) {
            Esp32BaseWeb::sendChunk("<span class='inline-note measured-note'>实测 ");
            sendLitersMl(calibration.actualMl);
            Esp32BaseWeb::sendChunk("</span>");
        }
        sendFmt("</td><td>%u s</td><td class='pulse-cell'>%luP (%luP/L)",
                static_cast<unsigned>(records[i].durationSec),
                static_cast<unsigned long>(records[i].pulseCount),
                static_cast<unsigned long>(pulsePerLiter));
        if (calibrated) {
            sendFmt("<span class='inline-note ok'>实测 %luP/L</span>",
                    static_cast<unsigned long>(measuredPulsePerLiter(records[i], calibration)));
        }
        if (records[i].rejectedPulseCount > 0) {
            sendFmt(" / 滤%luP",
                    static_cast<unsigned long>(records[i].rejectedPulseCount));
        }
        if (trace) {
            sendFmt("<a class='trace-badge' href='/faucet/records/detail?trace=%lu&bucket=1'>明细</a>",
                    static_cast<unsigned long>(trace->traceId));
        } else if (hasSavedTrace) {
            sendFmt("<a class='trace-badge' href='/faucet/records/detail?saved=1&trace=%lu&bucket=1'>已存明细</a>",
                    static_cast<unsigned long>(savedTrace.traceId));
        }
        WaterRecordMeteringSnapshot meteringSnapshot{};
        bool hasMeteringSnapshot = false;
        if (pageMeteringSnapshotIndexReady) {
            hasMeteringSnapshot = pageMeteringSnapshotFound[i];
            if (hasMeteringSnapshot) {
                meteringSnapshot = pageMeteringSnapshots[i];
            }
        } else {
            hasMeteringSnapshot = findRecordMeteringSnapshot(records[i], meteringSnapshot);
        }
        if (hasMeteringSnapshot) {
            Esp32BaseWeb::sendChunk("</td><td>");
            sendMeteringSnapshotLabel(meteringSnapshot, true);
            sendFmt("<span class='inline-note'>%luP/L</span>",
                    static_cast<unsigned long>(meteringSnapshot.params.stablePulsePerLiter));
        } else {
            Esp32BaseWeb::sendChunk("</td><td><span class='muted'>-</span>");
        }
        sendFmt("</td><td><span class='status-pill %s'>%s</span>",
                resultStatusClass(records[i].result),
                resultText(records[i].result));
        Esp32BaseWeb::sendChunk("</td><td><div class='row-actions'>");
        sendFmt("<a class='btn-link' href='/faucet/records/detail?info=1&start=%lu&volume=%lu&target=%lu&pulses=%lu&rejected=%lu&duration=%lu&mode=%u&result=%u&preset=%u'>详情</a>",
                static_cast<unsigned long>(records[i].startTime),
                static_cast<unsigned long>(records[i].volumeMl),
                static_cast<unsigned long>(records[i].targetValue),
                static_cast<unsigned long>(records[i].pulseCount),
                static_cast<unsigned long>(records[i].rejectedPulseCount),
                static_cast<unsigned long>(records[i].durationSec),
                static_cast<unsigned>(records[i].mode),
                static_cast<unsigned>(records[i].result),
                static_cast<unsigned>(records[i].selectedPreset));
        if (calibrated) {
            Esp32BaseWeb::sendChunk("<span class='status-pill status-ok'>已校准</span>");
        } else {
            Esp32BaseWeb::sendChunk("<span class='muted'>-</span>");
        }
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    delete[] pageCalibrations;
    delete[] pageCalibrated;
    delete[] pageMeteringSnapshots;
    delete[] pageMeteringSnapshotFound;
    delete[] pageTraces;
    delete[] pageSavedTraces;
    delete[] pageSavedTraceFound;
    delete[] records;
    sendPageEnd();
}

void handleRecordInfoPage() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char text[24]{};
    WaterRecord record{};
    std::uint32_t parsed = 0;
    if (getParam("start", text, sizeof(text)) && parseU32(text, parsed)) {
        record.startTime = parsed;
    }
    if (getParam("volume", text, sizeof(text)) && parseU32(text, parsed)) {
        record.volumeMl = parsed;
    }
    if (getParam("target", text, sizeof(text)) && parseU32(text, parsed)) {
        record.targetValue = parsed;
    }
    if (getParam("pulses", text, sizeof(text)) && parseU32(text, parsed)) {
        record.pulseCount = parsed;
    }
    if (getParam("rejected", text, sizeof(text)) && parseU32(text, parsed)) {
        record.rejectedPulseCount = parsed;
    }
    if (getParam("duration", text, sizeof(text)) && parseU32(text, parsed)) {
        record.durationSec = parsed;
    }
    if (getParam("mode", text, sizeof(text)) && parseU32(text, parsed)) {
        record.mode = parsed == static_cast<std::uint32_t>(WaterMode::Time) ? WaterMode::Time : WaterMode::Volume;
    }
    if (getParam("result", text, sizeof(text)) && parseU32(text, parsed)) {
        record.result = static_cast<WaterResult>(parsed);
    }
    if (getParam("preset", text, sizeof(text)) && parseU32(text, parsed)) {
        record.selectedPreset = static_cast<std::uint8_t>(parsed);
    }
    char startTime[40]{};
    formatWaterRecordListTime(record, startTime, sizeof(startTime));
    sendPageStart("接水详情");
    Esp32BaseWeb::sendChunk("<h2>接水详情</h2><div class='form-actions'><a class='btn-link' href='/faucet/records'>返回记录</a></div>"
                            "<section class='panel'><h3>记录信息</h3><table class='kv'>");
    sendFmt("<tr><th>开始时间</th><td>%s</td></tr>", startTime);
    Esp32BaseWeb::sendChunk("<tr><th>模式</th><td>");
    Esp32BaseWeb::sendChunk(modeText(record.mode));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>目标</th><td>");
    sendTargetValue(record);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>估算出水</th><td>");
    sendLiters(record.volumeMl);
    sendFmt("</td></tr><tr><th>持续时间</th><td>%lu s</td></tr>"
            "<tr><th>有效脉冲</th><td>%lu P</td></tr>"
            "<tr><th>被过滤脉冲</th><td>%lu P</td></tr>"
            "<tr><th>运行结果</th><td>%s</td></tr>"
            "<tr><th>预设序号</th><td>%u</td></tr>",
            static_cast<unsigned long>(record.durationSec),
            static_cast<unsigned long>(record.pulseCount),
            static_cast<unsigned long>(record.rejectedPulseCount),
            resultText(record.result),
            static_cast<unsigned>(record.selectedPreset));
    WaterRecordMeteringSnapshot meteringSnapshot{};
    if (findRecordMeteringSnapshot(record, meteringSnapshot)) {
        Esp32BaseWeb::sendChunk("<tr><th>计量方案</th><td>");
        sendMeteringSnapshotLabel(meteringSnapshot, false);
        sendFmt("</td></tr><tr><th>计量参数快照</th><td>启动 %luP / ",
                static_cast<unsigned long>(meteringSnapshot.params.startupPulseCount));
        sendLiters(meteringSnapshot.params.startupVolumeMl);
        sendFmt(" / 稳态 %luP/L</td></tr>",
                static_cast<unsigned long>(meteringSnapshot.params.stablePulsePerLiter));
    }
    Esp32BaseWeb::sendChunk("</table></section>");
    sendPageEnd();
}

void handleCalibrationPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        handleCalibrationPost();
        return;
    }

    WaterRecord record{};
    const bool canCalibrate = latestCalibratableRecord(record);
    const std::uint32_t samplePulseWindowSec = selectedSamplePulseWindowSec();
    char text[32]{};
    if (getParam("partial", text, sizeof(text))) {
        if (!Esp32BaseWeb::beginResponse(200, "text/html; charset=utf-8", nullptr)) {
            return;
        }
        if (std::strcmp(text, "latest") == 0) {
            sendLatestCalibrationRecordPanel(canCalibrate, record);
        } else if (std::strcmp(text, "generation") == 0) {
            sendCalibrationGenerationPanel(canCalibrate, record);
        } else if (std::strcmp(text, "samples") == 0) {
            sendCalibrationSamplesPanel(samplePulseWindowSec);
        }
        Esp32BaseWeb::endResponse();
        return;
    }
    if (getParam("scheme", text, sizeof(text))) {
        if (std::strcmp(text, "new") == 0) {
            sendMeteringSchemeEditPage(true, nullptr);
            return;
        }
        std::uint32_t id = 0;
        MeteringSchemeRecord scheme{};
        if (!parseU32(text, id) || !ensureMeteringSchemesReady() || !g_context.meteringSchemes->findById(id, scheme)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
            return;
        }
        sendMeteringSchemeEditPage(false, &scheme);
        return;
    }

    Esp32BaseWeb::sendHeader("校准工作台");
    Esp32BaseWeb::sendChunk("<h2>校准工作台</h2>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<div class='records-top-grid records-diagnostic-strip'>");
    sendSegmentedMeteringPanel();
    sendPulseTraceCachePanel();
    Esp32BaseWeb::sendChunk("</div>");

    sendLatestCalibrationRecordPanel(canCalibrate, record);
    sendCalibrationGenerationPanel(canCalibrate, record);

    Esp32BaseWeb::sendChunk("<div class='calibration-param-layout'>");
    sendCalibrationParameterPanels();
    Esp32BaseWeb::sendChunk("</div>");
    sendCalibrationFormulaPanel();
    sendCalibrationSamplesPanel(samplePulseWindowSec);
    sendCalibrationPageScript();
    sendPageEnd();
}

void handleRecordDetailPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (Esp32BaseWeb::hasParam("info")) {
        handleRecordInfoPage();
        return;
    }
    char text[24]{};
    const bool fromCalibration = calibrationContextRequested();
    const char* backHref = fromCalibration ? "/faucet/calibration" : "/faucet/records";
    const char* backLabel = fromCalibration ? "返回校准" : "返回记录";
    const char* detailPath = fromCalibration ? "/faucet/calibration/detail" : "/faucet/records/detail";
    const char* contextParam = fromCalibration ? "from=calibration&" : "";
    bool rawRequest = false;
    if (getParam("raw", text, sizeof(text))) {
        std::uint32_t rawValue = 0;
        rawRequest = parseU32(text, rawValue) && rawValue != 0;
    }
    bool rawTraceShowAll = false;
    if (getParam("all", text, sizeof(text))) {
        std::uint32_t allValue = 0;
        rawTraceShowAll = parseU32(text, allValue) && allValue != 0;
    }
    if (!contextReady()) {
        if (rawRequest) {
            sendPlainTextResponse(503, "上下文未就绪。\n");
        } else {
            Esp32BaseWeb::sendHeader("脉冲明细");
            Esp32BaseWeb::sendChunk("<h2>脉冲明细</h2><p class='err'>上下文未就绪。</p>");
            sendPageEnd();
        }
        return;
    }
    if (!g_context.pulseTraces && !g_context.savedPulseTraces) {
        if (rawRequest) {
            sendPlainTextResponse(503, "脉冲明细缓存不可用。\n");
            return;
        }
        Esp32BaseWeb::sendHeader("脉冲明细");
        sendFmt("<h2>脉冲明细</h2><p class='err'>脉冲明细缓存不可用。</p><p><a class='btn-link' href='%s'>%s</a></p>",
                backHref,
                backLabel);
        sendPageEnd();
        return;
    }
    std::uint32_t traceId = 0;
    if (!getParam("trace", text, sizeof(text)) || !parseU32(text, traceId)) {
        if (rawRequest) {
            sendPlainTextResponse(400, "明细编号无效。\n");
            return;
        }
        Esp32BaseWeb::sendHeader("脉冲明细");
        sendFmt("<h2>脉冲明细</h2><p class='err'>明细编号无效。</p><p><a class='btn-link' href='%s'>%s</a></p>",
                backHref,
                backLabel);
        sendPageEnd();
        return;
    }
    std::uint32_t bucketSeconds = 1;
    if (getParam("bucket", text, sizeof(text))) {
        parseU32(text, bucketSeconds);
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }
    bool savedSource = false;
    if (getParam("saved", text, sizeof(text))) {
        std::uint32_t savedValue = 0;
        savedSource = parseU32(text, savedValue) && savedValue != 0;
    }
    WaterPulseTrace savedTrace{};
    const WaterPulseTrace* trace = nullptr;
    if (savedSource) {
        if (ensureSavedPulseTracesReady() && g_context.savedPulseTraces->findById(traceId, savedTrace)) {
            trace = &savedTrace;
        }
    } else if (g_context.pulseTraces) {
        trace = g_context.pulseTraces->findById(traceId);
    }
    if (!trace) {
        if (rawRequest) {
            sendPlainTextResponse(404, "该脉冲明细不存在或已被 RAM 缓存淘汰。\n");
            return;
        }
        Esp32BaseWeb::sendHeader("脉冲明细");
        sendFmt("<h2>脉冲明细</h2><p class='err'>该脉冲明细不存在或已被 RAM 缓存淘汰。</p><p><a class='btn-link' href='%s'>%s</a></p>",
                backHref,
                backLabel);
        sendPageEnd();
        return;
    }

    WaterPulseTraceSample* samples = new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{};
    if (!samples) {
        delete[] samples;
        if (rawRequest) {
            sendPlainTextResponse(500, "内存不足，无法生成脉冲明细。\n");
            return;
        }
        Esp32BaseWeb::sendHeader("脉冲明细");
        Esp32BaseWeb::sendChunk("<p class='err'>内存不足，无法生成脉冲明细。</p>");
        sendPageEnd();
        return;
    }
    if (savedSource) {
        if (!g_context.savedPulseTraces ||
            g_context.savedPulseTraces->readSamples(trace->traceId, samples, trace->sampleCount) != trace->sampleCount) {
            delete[] samples;
            if (rawRequest) {
                sendPlainTextResponse(500, "已保存明细读取失败。\n");
                return;
            }
            Esp32BaseWeb::sendHeader("脉冲明细");
            Esp32BaseWeb::sendChunk("<p class='err'>已保存明细读取失败。</p>");
            sendPageEnd();
            return;
        }
    } else {
        for (std::size_t i = 0; i < trace->sampleCount; ++i) {
            const WaterPulseTraceSample* sample = g_context.pulseTraces ? g_context.pulseTraces->sampleAt(*trace, i) : nullptr;
            samples[i] = sample ? *sample : WaterPulseTraceSample{};
        }
    }
    if (rawRequest) {
        sendPulseTraceRawText(*trace, samples, rawTraceShowAll);
        delete[] samples;
        return;
    }
    WaterPulseTraceBucket* buckets = new (std::nothrow) WaterPulseTraceBucket[trace->sampleCount]{};
    if (!buckets) {
        delete[] samples;
        Esp32BaseWeb::sendHeader("脉冲明细");
        Esp32BaseWeb::sendChunk("<p class='err'>内存不足，无法生成脉冲明细。</p>");
        sendPageEnd();
        return;
    }
    const std::size_t bucketCount =
        aggregateWaterPulseTrace(*trace, samples, trace->sampleCount, bucketSeconds, buckets, trace->sampleCount);
    const WaterPulseTraceAnalysis analysis = analyzeWaterPulseTrace(*trace, samples, trace->sampleCount);
    std::uint32_t maxDelta = 1;
    std::uint32_t maxRawDelta = 1;
    for (std::size_t i = 0; i < bucketCount; ++i) {
        maxDelta = std::max(maxDelta, buckets[i].pulseDelta);
        maxRawDelta = std::max(maxRawDelta, buckets[i].rawEdgeDelta);
    }
    const std::uint32_t rawEdgeCount = static_cast<std::uint32_t>(trace->sampleCount);
    const std::uint32_t effectivePulseCountValue = effectivePulseCount(*trace, samples, trace->sampleCount);
    const std::uint32_t filteredEdgeCount =
        rawEdgeCount >= effectivePulseCountValue ? rawEdgeCount - effectivePulseCountValue : 0;
    const std::uint32_t effectiveRateTenths =
        rawEdgeCount == 0 ? 0 : static_cast<std::uint32_t>((effectivePulseCountValue * 1000ULL + rawEdgeCount / 2U) / rawEdgeCount);
    std::uint32_t pauseTotalUs = 0;
    for (std::uint8_t i = 0; i < trace->pauseWindowCount; ++i) {
        const WaterPulseTracePauseWindow& window = trace->pauseWindows[i];
        if (window.endElapsedUs > window.startElapsedUs) {
            pauseTotalUs += window.endElapsedUs - window.startElapsedUs;
        }
    }

    char startTime[40]{};
    formatWaterRecordTime(trace->record, startTime, sizeof(startTime));
    const std::size_t traceBytes = sizeof(WaterPulseTrace) + trace->sampleCount * sizeof(WaterPulseTraceSample);
    char traceKb[24]{};
    formatKb(traceBytes, traceKb, sizeof(traceKb));
    const bool savedStoreReady = ensureSavedPulseTracesReady();
    WaterPulseTrace savedTraceForRecord{};
    const bool alreadySaved =
        savedStoreReady && g_context.savedPulseTraces->findByRecord(trace->record, savedTraceForRecord);
    const std::uint32_t traceActualMl = actualMlForSegmentedSample(*trace);
    const bool traceActualSynced = trace->actualMl > 0;
    const bool traceActualFromRecord = !traceActualSynced && traceActualMl > 0;
    Esp32BaseWeb::sendHeader("脉冲明细");
    sendFmt("<h2>脉冲明细</h2><div class='form-actions'><a class='btn-link' href='%s'>%s</a>",
            backHref,
            backLabel);
    if (savedSource || alreadySaved) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/faucet/records' onsubmit=\"return confirm('确认删除这条已保存的脉冲明细？')&&once(this)\">"
                                "<input type='hidden' name='action' value='delete'>");
        if (fromCalibration) {
            Esp32BaseWeb::sendChunk("<input type='hidden' name='returnTo' value='calibration'>");
        }
        sendFmt("<input type='hidden' name='trace' value='%lu'><input class='secondary' type='submit' value='删除已保存明细'></form>",
                static_cast<unsigned long>(savedSource ? trace->traceId : savedTraceForRecord.traceId));
    } else if (savedStoreReady) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/faucet/records' onsubmit=\"return once(this)\">"
                                "<input type='hidden' name='action' value='save'>");
        if (fromCalibration) {
            Esp32BaseWeb::sendChunk("<input type='hidden' name='returnTo' value='calibration'>");
        }
        sendFmt("<input type='hidden' name='trace' value='%lu'><input class='secondary' type='submit' value='保存明细'></form>",
                static_cast<unsigned long>(trace->traceId));
    } else {
        Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>样本库不可用</span>");
    }
    Esp32BaseWeb::sendChunk("</div>");

    Esp32BaseWeb::sendChunk("<section class='panel sample-status-panel'><h3>样本状态</h3><table class='kv'>");
    if (traceActualSynced) {
        Esp32BaseWeb::sendChunk("<tr><th>状态</th><td><span class='status-pill status-ok'>样本已入库</span></td></tr>");
    } else if (traceActualFromRecord) {
        Esp32BaseWeb::sendChunk("<tr><th>状态</th><td><span class='status-pill status-ok'>记录已校准</span></td></tr>");
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>状态</th><td><span class='status-pill status-muted'>待确认容量</span></td></tr>"
                                "<tr><th>下一步</th><td>请在校准页确认这条记录的实际出水量。</td></tr>");
    }
    if (traceActualMl > 0) {
        Esp32BaseWeb::sendChunk("<tr><th>实测容量</th><td>");
        sendLitersMl(traceActualMl);
        Esp32BaseWeb::sendChunk("</td></tr>");
        Esp32BaseWeb::sendChunk(traceActualFromRecord
                                    ? "<tr><th>来源</th><td>来自确认/校准容量记录。</td></tr>"
                                    : "<tr><th>来源</th><td>来自脉冲明细样本库。</td></tr>");
    }
    if (trace->resumedAfterPause) {
        Esp32BaseWeb::sendChunk("<tr><th>可用性</th><td>暂停后恢复出水，属于多段出水，不参与启动段和分段校准拟合。</td></tr>");
    } else if (trace->truncated) {
        Esp32BaseWeb::sendChunk("<tr><th>可用性</th><td>明细已截断，不入库，不参与生成校准参数。</td></tr>");
    } else if (traceActualMl > 0 && analysis.stable) {
        sendFmt("<tr><th>可用性</th><td>可用于拟合，稳态从第 %lu 秒开始。</td></tr>",
                static_cast<unsigned long>(analysis.stableStartSec));
    } else if (analysis.stable) {
        sendFmt("<tr><th>可用性</th><td>脉冲稳定，输入实测容量后可用于拟合；稳态从第 %lu 秒开始。</td></tr>",
                static_cast<unsigned long>(analysis.stableStartSec));
    } else if (traceActualMl > 0) {
        Esp32BaseWeb::sendChunk("<tr><th>可用性</th><td>实测容量已记录，但稳态识别失败，暂不能用于拟合。</td></tr>");
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>可用性</th><td>缺少实测容量且稳态识别失败，暂不能用于拟合。</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table><p class='hint'>确认容量统一从校准页保存；确认最后一条记录容量后会自动入库。只有已保存到设备、容量已确认且未截断的样本才参与方案生成。</p></section>");

    Esp32BaseWeb::sendChunk("<section class='panel'><h3>明细概况</h3><table class='kv'>");
    sendFmt("<tr><th>开始时间</th><td>%s</td></tr>"
            "<tr><th>目标</th><td>",
            startTime);
    sendTargetValue(trace->record);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>固件估算</th><td>");
    sendLiters(trace->record.volumeMl);
    sendFmt("</td></tr><tr><th>有效脉冲</th><td>%lu</td></tr>"
            "<tr><th>原始边沿</th><td>%lu</td></tr>"
            "<tr><th>被过滤边沿</th><td>%lu</td></tr>"
            "<tr><th>有效率</th><td>%lu.%lu%%</td></tr>"
            "<tr><th>最高频率</th><td>有效 %lu 脉冲/%lus，原始 %lu 边沿/%lus</td></tr>"
            "<tr><th>暂停次数</th><td>%u</td></tr>"
            "<tr><th>暂停总时长</th><td>",
            static_cast<unsigned long>(effectivePulseCountValue),
            static_cast<unsigned long>(rawEdgeCount),
            static_cast<unsigned long>(filteredEdgeCount),
            static_cast<unsigned long>(effectiveRateTenths / 10U),
            static_cast<unsigned long>(effectiveRateTenths % 10U),
            static_cast<unsigned long>(maxDelta),
            static_cast<unsigned long>(bucketSeconds),
            static_cast<unsigned long>(maxRawDelta),
            static_cast<unsigned long>(bucketSeconds),
            static_cast<unsigned>(trace->pauseWindowCount));
    sendDurationUs(pauseTotalUs);
    sendFmt("</td></tr><tr><th>暂停后恢复</th><td>%s</td></tr>"
            "<tr><th>持续时间</th><td>%lu s</td></tr>"
            "<tr><th>样本数</th><td>%lu</td></tr>"
            "<tr><th>本条占用</th><td>%s</td></tr>",
            trace->resumedAfterPause ? "是" : "否",
            static_cast<unsigned long>(trace->record.durationSec),
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

    Esp32BaseWeb::sendChunk("<section class='panel'><div class='panel-head'><h3>脉冲趋势</h3><div class='row-actions trace-frequency'><span class='trace-frequency-label'>聚合频率</span>");
    constexpr std::uint32_t bucketsToShow[] = {1, 2, 3, 4, 5};
    for (std::uint32_t bucket : bucketsToShow) {
        const char* linkClass = bucket == bucketSeconds ? "btn-link page-current" : "btn-link";
        sendFmt("<a class='%s' aria-current='%s' href='%s?%s%strace=%lu&bucket=%lu'>%lus</a>",
                linkClass,
                bucket == bucketSeconds ? "true" : "false",
                detailPath,
                contextParam,
                savedSource ? "saved=1&" : "",
                static_cast<unsigned long>(traceId),
                static_cast<unsigned long>(bucket),
                static_cast<unsigned long>(bucket));
    }
    const std::uint32_t left = 54;
    const std::uint32_t top = 28;
    const std::uint32_t baseY = 224;
    const std::uint32_t chartHeight = 176;
    const std::uint32_t chartWidth = 900;
    const std::uint32_t maxEndSec =
        bucketCount == 0 ? 1 : std::max<std::uint32_t>(1, buckets[bucketCount - 1].startSec + buckets[bucketCount - 1].durationSec);
    const std::uint64_t maxChartUs = static_cast<std::uint64_t>(maxEndSec) * 1000000ULL;
    auto chartXForElapsedUs = [&](std::uint32_t elapsedUs) -> std::uint32_t {
        const std::uint64_t clamped = std::min<std::uint64_t>(elapsedUs, maxChartUs);
        return left + static_cast<std::uint32_t>((clamped * chartWidth) / std::max<std::uint64_t>(1, maxChartUs));
    };
    Esp32BaseWeb::sendChunk("</div></div><div class='pulse-detail-chart'><svg viewBox='0 0 1000 300' role='img' aria-label='脉冲明细折线图'>"
                            "<line class='axis' x1='54' y1='224' x2='954' y2='224'></line>"
                            "<line class='axis' x1='54' y1='28' x2='54' y2='224'></line>"
                            "<line class='axis' x1='954' y1='28' x2='954' y2='224'></line>");
    for (std::uint8_t i = 0; i < trace->pauseWindowCount; ++i) {
        const WaterPulseTracePauseWindow& window = trace->pauseWindows[i];
        const std::uint32_t endUs =
            window.endElapsedUs > window.startElapsedUs ? window.endElapsedUs : static_cast<std::uint32_t>(std::min<std::uint64_t>(maxChartUs, UINT32_MAX));
        const std::uint32_t x1 = chartXForElapsedUs(window.startElapsedUs);
        std::uint32_t x2 = chartXForElapsedUs(endUs);
        if (x2 <= x1) {
            x2 = x1 + 1U;
        }
        sendFmt("<rect class='pause-window' x='%lu' y='%lu' width='%lu' height='%lu'><title>暂停区间 %lu.%03lu s - %lu.%03lu s</title></rect>"
                "<line class='pause-boundary' x1='%lu' y1='%lu' x2='%lu' y2='%lu'></line>"
                "<line class='pause-boundary' x1='%lu' y1='%lu' x2='%lu' y2='%lu'></line>",
                static_cast<unsigned long>(x1),
                static_cast<unsigned long>(top),
                static_cast<unsigned long>(x2 - x1),
                static_cast<unsigned long>(baseY - top),
                static_cast<unsigned long>(window.startElapsedUs / 1000000UL),
                static_cast<unsigned long>((window.startElapsedUs % 1000000UL) / 1000UL),
                static_cast<unsigned long>(endUs / 1000000UL),
                static_cast<unsigned long>((endUs % 1000000UL) / 1000UL),
                static_cast<unsigned long>(x1),
                static_cast<unsigned long>(top),
                static_cast<unsigned long>(x1),
                static_cast<unsigned long>(baseY),
                static_cast<unsigned long>(x2),
                static_cast<unsigned long>(top),
                static_cast<unsigned long>(x2),
                static_cast<unsigned long>(baseY));
    }
    for (std::uint32_t i = 1; i <= 4; ++i) {
        const std::uint32_t y = baseY - (chartHeight * i) / 4;
        sendFmt("<line class='grid-line' x1='54' y1='%lu' x2='954' y2='%lu'></line>",
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y));
    }
    for (std::uint32_t i = 0; i <= 4; ++i) {
        const std::uint32_t y = baseY - (chartHeight * i) / 4;
        const std::uint32_t value = (maxDelta * i + 2U) / 4U;
        sendFmt("<text class='chart-label chart-y-label' x='48' y='%lu'>%lu</text>",
                static_cast<unsigned long>(y + 4U),
                static_cast<unsigned long>(value));
    }
    for (std::uint32_t i = 0; i <= 4; ++i) {
        const std::uint32_t y = baseY - (chartHeight * i) / 4;
        const std::uint32_t value = (maxRawDelta * i + 2U) / 4U;
        sendFmt("<text class='chart-label chart-raw-y-label' x='960' y='%lu'>%lu</text>",
                static_cast<unsigned long>(y + 4U),
                static_cast<unsigned long>(value));
    }
    for (std::uint32_t i = 0; i <= 4; ++i) {
        const std::uint32_t x = left + (chartWidth * i) / 4;
        const std::uint32_t value = (maxEndSec * i + 2U) / 4U;
        sendFmt("<text class='chart-label chart-x-label' x='%lu' y='248'>%lus</text>",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(value));
    }
    sendFmt("<text class='chart-label' x='58' y='20'>有效最高 %lu 脉冲 / 原始最高 %lu 边沿</text>",
            static_cast<unsigned long>(maxDelta),
            static_cast<unsigned long>(maxRawDelta));
    bool prevRawValid = false;
    std::uint32_t prevRawX = left;
    std::uint32_t prevRawY = baseY;
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t chartDelta = buckets[i].rawEdgeDelta;
        const std::uint32_t startSec = buckets[i].startSec;
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t startX = left + (startSec * chartWidth) / maxEndSec;
        const std::uint32_t x = left + (endSec * chartWidth) / maxEndSec;
        const std::uint32_t y = baseY - (chartDelta * chartHeight) / maxRawDelta;
        const std::uint32_t lineStartX = prevRawValid ? prevRawX : startX;
        const std::uint32_t lineStartY = prevRawValid ? prevRawY : baseY;
        sendFmt("<line class='raw-line' x1='%lu' y1='%lu' x2='%lu' y2='%lu'></line>",
                static_cast<unsigned long>(lineStartX),
                static_cast<unsigned long>(lineStartY),
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(y));
        prevRawValid = true;
        prevRawX = x;
        prevRawY = y;
    }
    bool prevPulseValid = false;
    std::uint32_t prevPulseX = left;
    std::uint32_t prevPulseY = baseY;
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t chartDelta = bucketRunningPulseDelta(samples, trace->sampleCount, buckets[i]);
        const std::uint32_t startSec = buckets[i].startSec;
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t startX = left + (startSec * chartWidth) / maxEndSec;
        const std::uint32_t x = left + (endSec * chartWidth) / maxEndSec;
        const std::uint32_t y = baseY - (chartDelta * chartHeight) / maxDelta;
        const std::uint32_t lineStartX = prevPulseValid ? prevPulseX : startX;
        const std::uint32_t lineStartY = prevPulseValid ? prevPulseY : baseY;
        sendFmt("<line class='pulse-line' x1='%lu' y1='%lu' x2='%lu' y2='%lu'></line>",
                static_cast<unsigned long>(lineStartX),
                static_cast<unsigned long>(lineStartY),
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(y));
        prevPulseValid = true;
        prevPulseX = x;
        prevPulseY = y;
    }
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
    std::uint32_t effectiveCumulative = 0;
    std::uint32_t rawCumulative = 0;
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t chartDelta = bucketRunningPulseDelta(samples, trace->sampleCount, buckets[i]);
        const std::uint32_t rawDelta = buckets[i].rawEdgeDelta;
        effectiveCumulative += chartDelta;
        rawCumulative += rawDelta;
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t x = left + (endSec * chartWidth) / maxEndSec;
        const std::uint32_t y = baseY - (chartDelta * chartHeight) / maxDelta;
        const std::uint32_t rawY = baseY - (rawDelta * chartHeight) / maxRawDelta;
        sendFmt("<circle class='raw-dot' cx='%lu' cy='%lu' r='2.1'><title>第%lu秒: 原始边沿 %lu / 原始累计 %lu</title></circle>",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(rawY),
                static_cast<unsigned long>(buckets[i].startSec),
                static_cast<unsigned long>(rawDelta),
                static_cast<unsigned long>(rawCumulative));
        sendFmt("<circle class='pulse-dot' cx='%lu' cy='%lu' r='2.8'><title>第%lu秒: 有效脉冲 %lu / 有效累计 %lu / %s</title></circle>",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(buckets[i].startSec),
                static_cast<unsigned long>(chartDelta),
                static_cast<unsigned long>(effectiveCumulative),
                traceStateText(buckets[i].state));
    }
    Esp32BaseWeb::sendChunk("</svg></div><div class='chart-legend'>"
                            "<span><i class='legend-mark legend-pulse'></i>有效脉冲</span>"
                            "<span><i class='legend-mark legend-raw'></i>原始边沿</span>"
                            "<span><i class='legend-mark legend-paused'></i>暂停区间</span>"
                            "<span><i class='legend-mark legend-stable'></i>稳态开始</span>"
                            "</div></section>");
    const char* rawSavedParam = savedSource ? "saved=1&" : "";
    const std::size_t rawPreviewCount = rawTracePreviewSampleCount(*trace);
    Esp32BaseWeb::sendChunk("<section class='panel detail-data'><div class='panel-head'><h3>原始明细</h3><div class='row-actions'>");
    sendFmt("<a class='btn-link' target='_blank' rel='noopener' href='%s?raw=1&%s%strace=%lu'>显示原始明细</a>",
            detailPath,
            contextParam,
            rawSavedParam,
            static_cast<unsigned long>(traceId));
    if (trace->sampleCount > rawPreviewCount) {
        sendFmt("<a class='btn-link' target='_blank' rel='noopener' href='%s?raw=1&%s%strace=%lu&all=1'>显示所有明细</a>",
                detailPath,
                contextParam,
                rawSavedParam,
                static_cast<unsigned long>(traceId));
    }
    sendFmt("</div></div><p class='hint'>默认展示前 %lu 个原始边沿；原始边沿共 %lu 个，当前展示 %lu 个。</p>"
            "<table class='raw-trace-table'><tr><th>序号</th><th>距开始</th><th>与上一边沿间隔</th><th>有效累计</th></tr>",
            static_cast<unsigned long>(kRawTracePreviewLastSecond),
            static_cast<unsigned long>(trace->sampleCount),
            static_cast<unsigned long>(rawPreviewCount));
    std::uint32_t rawPreviewEffective = 0;
    std::uint32_t lastEffectiveElapsedUs = 0;
    for (std::size_t i = 0; i < rawPreviewCount; ++i) {
        const std::uint32_t intervalUs = i == 0 ? 0 : samples[i].elapsedUs - samples[i - 1].elapsedUs;
        bool effective = i == 0;
        if (i > 0 && samples[i].elapsedUs >= lastEffectiveElapsedUs &&
            samples[i].elapsedUs - lastEffectiveElapsedUs >= trace->pulseMinIntervalUs) {
            effective = true;
        }
        if (effective) {
            ++rawPreviewEffective;
            lastEffectiveElapsedUs = samples[i].elapsedUs;
        }
        sendFmt("<tr><td>%lu</td><td>", static_cast<unsigned long>(i));
        sendDurationUs(samples[i].elapsedUs);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendDurationUs(intervalUs);
        sendFmt("</td><td>%lu</td></tr>", static_cast<unsigned long>(rawPreviewEffective));
    }
    Esp32BaseWeb::sendChunk("</table></section>");

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

void handleAppCss() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendResponseHeader("Cache-Control", "public, max-age=86400");
    Esp32BaseWeb::sendResponseHeader("X-Content-Type-Options", "nosniff");
    if (!Esp32BaseWeb::beginResponse(200, "text/css; charset=utf-8", nullptr)) {
        return;
    }
    sendAppCss();
    Esp32BaseWeb::endResponse();
}

bool sendJsonBuffer(bool ok, const char* json) {
    if (!ok) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"buffer_too_small\"}");
        return false;
    }
    Esp32BaseWeb::sendJson(200, json);
    return true;
}

bool contextReady() {
    if (!g_context.config || !g_context.configStore || !g_context.app || !g_context.filters || !g_context.records ||
        !g_context.recordCalibrations || !g_context.recordCalibrationWriter || !g_context.nowSeconds) {
        return false;
    }
    return true;
}

bool requireContext() {
    if (!contextReady()) {
        Esp32BaseWeb::sendJson(503, "{\"error\":\"context_not_ready\"}");
        return false;
    }
    return true;
}

const char* configLoadStatusName(ConfigStore::LoadStatus status) {
    switch (status) {
        case ConfigStore::LoadStatus::DefaultsNoVersion:
            return "defaults_no_version";
        case ConfigStore::LoadStatus::LoadedCurrent:
            return "loaded_current";
        case ConfigStore::LoadStatus::MigratedLegacy:
            return "migrated_legacy";
        case ConfigStore::LoadStatus::LoadedFutureVersionReadOnly:
            return "future_version_read_only";
        case ConfigStore::LoadStatus::UnsupportedVersionDefault:
            return "unsupported_version_default";
    }
    return "unknown";
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

void mergeChangedConfigFields(const SystemConfig& before, const SystemConfig& submitted, SystemConfig& target) {
    if (submitted.confirmTimeoutSec != before.confirmTimeoutSec) {
        target.confirmTimeoutSec = submitted.confirmTimeoutSec;
    }
    if (submitted.maxOutTimeSec != before.maxOutTimeSec) {
        target.maxOutTimeSec = submitted.maxOutTimeSec;
    }
    if (submitted.maxOutVolumeMl != before.maxOutVolumeMl) {
        target.maxOutVolumeMl = submitted.maxOutVolumeMl;
    }
    if (submitted.overflowPercent != before.overflowPercent) {
        target.overflowPercent = submitted.overflowPercent;
    }
    if (submitted.noFlowTimeoutSec != before.noFlowTimeoutSec) {
        target.noFlowTimeoutSec = submitted.noFlowTimeoutSec;
    }
    if (submitted.highFlowMlPerMin != before.highFlowMlPerMin) {
        target.highFlowMlPerMin = submitted.highFlowMlPerMin;
    }
    if (submitted.highFlowDurationSec != before.highFlowDurationSec) {
        target.highFlowDurationSec = submitted.highFlowDurationSec;
    }
    if (submitted.pauseTimeoutSec != before.pauseTimeoutSec) {
        target.pauseTimeoutSec = submitted.pauseTimeoutSec;
    }
    if (submitted.volumeAdjustStepMl != before.volumeAdjustStepMl) {
        target.volumeAdjustStepMl = submitted.volumeAdjustStepMl;
    }
    if (submitted.timeAdjustStepSec != before.timeAdjustStepSec) {
        target.timeAdjustStepSec = submitted.timeAdjustStepSec;
    }
    if (submitted.pulseMinIntervalUs != before.pulseMinIntervalUs) {
        target.pulseMinIntervalUs = submitted.pulseMinIntervalUs;
    }
    if (submitted.recentPulseTraceCount != before.recentPulseTraceCount) {
        target.recentPulseTraceCount = submitted.recentPulseTraceCount;
    }
    if (submitted.valveFullPowerSec != before.valveFullPowerSec) {
        target.valveFullPowerSec = submitted.valveFullPowerSec;
    }
    if (submitted.valveHoldDutyPercent != before.valveHoldDutyPercent) {
        target.valveHoldDutyPercent = submitted.valveHoldDutyPercent;
    }
    if (submitted.displaySleepSec != before.displaySleepSec) {
        target.displaySleepSec = submitted.displaySleepSec;
    }
    if (submitted.resultDisplaySec != before.resultDisplaySec) {
        target.resultDisplaySec = submitted.resultDisplaySec;
    }
    if (submitted.lcdI2cAddress != before.lcdI2cAddress) {
        target.lcdI2cAddress = submitted.lcdI2cAddress;
    }
    if (submitted.beepEnabled != before.beepEnabled) {
        target.beepEnabled = submitted.beepEnabled;
    }
    if (std::memcmp(submitted.presets, before.presets, sizeof(before.presets)) != 0) {
        std::memcpy(target.presets, submitted.presets, sizeof(target.presets));
    }
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        FilterRecord submittedBase = submitted.filters[i];
        FilterRecord beforeBase = before.filters[i];
        submittedBase.startTime = beforeBase.startTime;
        submittedBase.usedMl = beforeBase.usedMl;
        submittedBase.startBootId = beforeBase.startBootId;
        if (std::memcmp(&submittedBase, &beforeBase, sizeof(FilterRecord)) != 0) {
            const std::uint32_t startTime = target.filters[i].startTime;
            const std::uint32_t usedMl = target.filters[i].usedMl;
            const std::uint32_t startBootId = target.filters[i].startBootId;
            target.filters[i] = submitted.filters[i];
            target.filters[i].startTime = startTime;
            target.filters[i].usedMl = usedMl;
            target.filters[i].startBootId = startBootId;
        }
    }
}

bool persistConfig(const SystemConfig& config) {
    if (!g_context.app->canApplyConfig()) {
        return false;
    }

    SystemConfig safe = g_context.configStore->loadSystemConfig();
    if (g_context.configStore->systemConfigReadOnly()) {
        return false;
    }
    mergeChangedConfigFields(*g_context.config, config, safe);
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

void handleCalibrationPost() {
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    char text[24]{};
    if (!getParam("action", text, sizeof(text))) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    if (std::strcmp(text, "calibrate") == 0) {
        handleRecordCalibrationApi();
        return;
    }
    if (std::strcmp(text, "delete_latest_trace") == 0) {
        handleDeleteLatestTraceApi();
        return;
    }
    if (std::strcmp(text, "generate_segmented") == 0) {
        handleGenerateSegmentedCalibrationApi();
        return;
    }
    if (std::strcmp(text, "save_generated_scheme") == 0) {
        handleSaveGeneratedSchemeApi();
        return;
    }
    if (std::strcmp(text, "discard_generated_scheme") == 0) {
        handleDiscardGeneratedSchemeApi();
        return;
    }
    if (std::strcmp(text, "create_metering_scheme") == 0) {
        handleCreateMeteringSchemeApi();
        return;
    }
    if (std::strcmp(text, "edit_metering_scheme") == 0) {
        handleEditMeteringSchemeApi();
        return;
    }
    if (std::strcmp(text, "enable_metering_scheme") == 0) {
        handleEnableMeteringSchemeApi();
        return;
    }
    if (std::strcmp(text, "disable_metering_scheme") == 0) {
        handleDisableMeteringSchemeApi();
        return;
    }
    if (std::strcmp(text, "delete_metering_scheme") == 0) {
        handleDeleteMeteringSchemeApi();
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
}

bool persistFilterConfig(const FilterRecord& record, std::size_t index) {
    if (!g_context.app->canApplyConfig() || index >= kFilterCount) {
        return false;
    }

    SystemConfig* safe = new (std::nothrow) SystemConfig(g_context.configStore->loadSystemConfig());
    if (!safe) {
        return false;
    }
    if (g_context.configStore->systemConfigReadOnly()) {
        delete safe;
        return false;
    }
    safe->filters[index] = record;
    sanitizeConfig(*safe);

    FilterRecord runtime[kFilterCount]{};
    const FilterRecord* current = g_context.filters->records();
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        runtime[i] = current[i];
    }
    runtime[index].startTime = record.startTime;
    runtime[index].usedMl = record.usedMl;
    runtime[index].startBootId = record.startBootId;

    if (!g_context.configStore->saveSystemConfig(*safe) || !g_context.configStore->saveFilterRuntime(runtime)) {
        delete safe;
        return false;
    }
    FilterRecord liveRecord = safe->filters[index];
    liveRecord.startTime = runtime[index].startTime;
    liveRecord.usedMl = runtime[index].usedMl;
    liveRecord.startBootId = runtime[index].startBootId;
    if (!g_context.app->applyConfig(*safe) || !g_context.filters->updateFilter(index, liveRecord)) {
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
    const ConfigRuntimeStatus configStatus{
        configLoadStatusName(g_context.configStore->lastSystemConfigLoadStatus()),
        g_context.configStore->lastSystemConfigRawVersion(),
        g_context.configStore->currentSystemConfigVersion(),
        g_context.configStore->systemConfigReadOnly(),
        g_context.configStore->lastSystemConfigMigrationWriteBack(),
    };
    char json[576]{};
    sendJsonBuffer(
        writeStatusJson(g_context.app->snapshot(), displayStatus.screenOn, *g_context.config, &configStatus, json, sizeof(json)),
        json);
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
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!getParam("action", text, sizeof(text))) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"missing_action\"}");
            return;
        }
        if (std::strcmp(text, "calibrate") == 0) {
            handleRecordCalibrationApi();
            return;
        }
        if (std::strcmp(text, "generate_segmented") == 0) {
            handleGenerateSegmentedCalibrationApi();
            return;
        }
        if (std::strcmp(text, "save_generated_scheme") == 0) {
            handleSaveGeneratedSchemeApi();
            return;
        }
        if (std::strcmp(text, "discard_generated_scheme") == 0) {
            handleDiscardGeneratedSchemeApi();
            return;
        }
        if (std::strcmp(text, "create_metering_scheme") == 0) {
            handleCreateMeteringSchemeApi();
            return;
        }
        if (std::strcmp(text, "edit_metering_scheme") == 0) {
            handleEditMeteringSchemeApi();
            return;
        }
        if (std::strcmp(text, "enable_metering_scheme") == 0) {
            handleEnableMeteringSchemeApi();
            return;
        }
        if (std::strcmp(text, "disable_metering_scheme") == 0) {
            handleDisableMeteringSchemeApi();
            return;
        }
        if (std::strcmp(text, "delete_metering_scheme") == 0) {
            handleDeleteMeteringSchemeApi();
            return;
        }
        if (std::strcmp(text, "delete") == 0) {
            handleTraceDeleteApi();
            return;
        }
        if (std::strcmp(text, "save") == 0) {
            handleTraceSaveApi();
            return;
        }
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_action\"}");
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
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
    const bool ajax = Esp32BaseWeb::hasParam("ajax");
    auto fail = [ajax](int status, const char* json, const char* redirectUrl) {
        if (ajax) {
            Esp32BaseWeb::sendJson(status, json);
        } else {
            Esp32BaseWeb::redirectSeeOther(redirectUrl);
        }
    };
    auto ok = [ajax]() {
        if (ajax) {
            Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
        } else {
            Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=actual");
        }
    };
    if (waterTaskActive()) {
        fail(409, "{\"error\":\"busy\"}", "/faucet/calibration?error=busy");
        return;
    }

    WaterRecord record{};
    if (!g_context.records || !g_context.records->ready() || g_context.records->readPage(0, 1, &record, 1) != 1 ||
        !waterRecordCanCalibrate(record)) {
        fail(404, "{\"error\":\"no_calibration_record\"}", "/faucet/calibration?error=no_calibration_record");
        return;
    }

    char text[32]{};
    std::uint32_t actualMl = 0;
    if (!getParam("actualMl", text, sizeof(text)) || !parseU32(text, actualMl)) {
        fail(400, "{\"error\":\"invalid_value\"}", "/faucet/calibration?error=invalid_value");
        return;
    }
    if (actualMl < kMinVolumePresetMl || actualMl > kMaxVolumePresetMl) {
        fail(400, "{\"error\":\"invalid_value\"}", "/faucet/calibration?error=invalid_value");
        return;
    }
    WaterRecordCalibration calibration{};
    const bool calibrated = findRecordCalibration(record, calibration);
    const std::uint32_t defaultActualMl = calibrated ? calibration.actualMl : record.volumeMl;
    if (calibrated && actualMl == defaultActualMl) {
        syncTraceActualMeasurement(record, actualMl);
        std::uint32_t ignoredTraceId = 0;
        WaterPulseTraceSaveStatus ignoredStatus = WaterPulseTraceSaveStatus::Ok;
        saveLatestTraceToDevice(record, &ignoredTraceId, &ignoredStatus);
        ok();
        return;
    }

    if (!saveRecordActualMeasurement(record, actualMl)) {
        fail(500, "{\"error\":\"calibration_mark_failed\"}", "/faucet/calibration?error=calibration_mark_failed");
        return;
    }
    syncTraceActualMeasurement(record, actualMl);
    std::uint32_t ignoredTraceId = 0;
    WaterPulseTraceSaveStatus ignoredStatus = WaterPulseTraceSaveStatus::Ok;
    saveLatestTraceToDevice(record, &ignoredTraceId, &ignoredStatus);
    ok();
}

void handleDeleteLatestTraceApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    WaterRecord record{};
    if (!latestCalibratableRecord(record)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=no_calibration_record");
        return;
    }
    if (!deleteLatestTraceFromDevice(record)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=trace_deleted");
}

void handleGenerateSegmentedCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!generateSegmentedCalibrationResultFromSavedSamples()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=sample_not_enough");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=generated");
}

void handleSaveGeneratedSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?generated=1&error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?generated=1&error=save_failed");
        return;
    }
    MeteringSchemeCandidate candidate{};
    if (!g_context.meteringSchemes->loadCandidate(candidate) || !candidate.ready) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=no_generated_result");
        return;
    }
    char name[kMeteringSchemeNameLength]{};
    Esp32BaseWeb::getParam("name", name, sizeof(name));
    if (name[0] == '\0') {
        std::snprintf(name, sizeof(name), "样本生成方案");
    }
    std::uint32_t newId = 0;
    if (!g_context.meteringSchemes->saveCandidateAsNew(name, g_context.nowSeconds ? g_context.nowSeconds() : 0, newId)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?generated=1&error=save_failed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=scheme_created");
}

void handleDiscardGeneratedSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    MeteringSchemeCandidate empty{};
    if (!g_context.meteringSchemes->saveCandidate(empty)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=generated_discarded");
}

bool readMeteringParamsFromRequest(MeteringParameters& params) {
    char text[32]{};
    return getParam("startupPulseCount", text, sizeof(text)) && parseU32(text, params.startupPulseCount) &&
           getParam("startupVolumeMl", text, sizeof(text)) && parseU32(text, params.startupVolumeMl) &&
           getParam("stablePulsePerLiter", text, sizeof(text)) && parseU32(text, params.stablePulsePerLiter) &&
           validMeteringSchemeParameters(params);
}

bool readMeteringSchemeId(std::uint32_t& id) {
    char text[32]{};
    return getParam("id", text, sizeof(text)) && parseU32(text, id) && id > 0;
}

void handleCreateMeteringSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    MeteringParameters params{};
    if (!readMeteringParamsFromRequest(params)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    char name[kMeteringSchemeNameLength]{};
    Esp32BaseWeb::getParam("name", name, sizeof(name));
    if (name[0] == '\0') {
        std::snprintf(name, sizeof(name), "手工方案");
    }
    std::uint32_t newId = 0;
    if (!g_context.meteringSchemes->createManual(name, params, g_context.nowSeconds ? g_context.nowSeconds() : 0, newId)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=scheme_created");
}

void handleEditMeteringSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    std::uint32_t id = 0;
    MeteringParameters params{};
    if (!readMeteringSchemeId(id) || !readMeteringParamsFromRequest(params)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    MeteringSchemeRecord edited{};
    if (!g_context.meteringSchemes->findById(id, edited)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    Esp32BaseWeb::getParam("name", edited.name, sizeof(edited.name));
    Esp32BaseWeb::getParam("meterLabel", edited.meterLabel, sizeof(edited.meterLabel));
    Esp32BaseWeb::getParam("installationLabel", edited.installationLabel, sizeof(edited.installationLabel));
    Esp32BaseWeb::getParam("conditionLabel", edited.conditionLabel, sizeof(edited.conditionLabel));
    Esp32BaseWeb::getParam("userNote", edited.userNote, sizeof(edited.userNote));
    edited.params = params;
    if (!g_context.meteringSchemes->updateScheme(edited, g_context.nowSeconds ? g_context.nowSeconds() : 0)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    if (id == g_context.meteringSchemes->activeSchemeId()) {
        MeteringSchemeRecord active{};
        if (g_context.meteringSchemes->activeScheme(active) && g_context.app) {
            g_context.app->applyActiveMeteringScheme(active);
        }
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=scheme");
}

void handleEnableMeteringSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive() || !g_context.app || !g_context.app->canApplyConfig()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    std::uint32_t id = 0;
    if (!readMeteringSchemeId(id)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    if (!g_context.meteringSchemes->enableScheme(id, g_context.nowSeconds ? g_context.nowSeconds() : 0)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    MeteringSchemeRecord active{};
    if (!g_context.meteringSchemes->activeScheme(active) || !g_context.app->applyActiveMeteringScheme(active)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=scheme_enabled");
}

void handleDisableMeteringSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    std::uint32_t id = 0;
    if (!readMeteringSchemeId(id) || !g_context.meteringSchemes->disableScheme(id, g_context.nowSeconds ? g_context.nowSeconds() : 0)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=scheme_disabled");
}

void handleDeleteMeteringSchemeApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=save_failed");
        return;
    }
    std::uint32_t id = 0;
    if (!readMeteringSchemeId(id) || !g_context.meteringSchemes->deleteScheme(id)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=invalid_value");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=scheme_deleted");
}

void handleTraceCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    char text[32]{};
    std::uint32_t traceId = 0;
    if (getParam("action", text, sizeof(text))) {
        if (std::strcmp(text, "delete") == 0) {
            handleTraceDeleteApi();
            return;
        }
    }
    if (!g_context.pulseTraces) {
        Esp32BaseWeb::redirectSeeOther("/faucet/records?error=no_calibration_record");
        return;
    }
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
        if (!storeSegmentedCandidate(result)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/records?error=save_failed");
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=generated");
        return;
    }

    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/records/detail?trace=%lu&bucket=1&error=invalid_value",
                  static_cast<unsigned long>(traceId));
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleTraceDeleteApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    const bool fromCalibration = calibrationContextRequested();
    const char* listPath = fromCalibration ? "/faucet/calibration" : "/faucet/records";
    if (!ensureSavedPulseTracesReady()) {
        char url[80]{};
        std::snprintf(url, sizeof(url), "%s?error=save_failed", listPath);
        Esp32BaseWeb::redirectSeeOther(url);
        return;
    }
    char text[32]{};
    std::uint32_t traceId = 0;
    if (!getParam("trace", text, sizeof(text)) || !parseU32(text, traceId)) {
        char url[80]{};
        std::snprintf(url, sizeof(url), "%s?error=invalid_value", listPath);
        Esp32BaseWeb::redirectSeeOther(url);
        return;
    }
    if (!g_context.savedPulseTraces->remove(traceId)) {
        char url[80]{};
        std::snprintf(url, sizeof(url), "%s?error=save_failed", listPath);
        Esp32BaseWeb::redirectSeeOther(url);
        return;
    }
    char url[80]{};
    std::snprintf(url, sizeof(url), "%s?saved=trace_deleted", listPath);
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleTraceSaveApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    const bool fromCalibration = calibrationContextRequested();
    const char* listPath = fromCalibration ? "/faucet/calibration" : "/faucet/records";
    char text[32]{};
    std::uint32_t traceId = 0;
    if (!getParam("trace", text, sizeof(text)) || !parseU32(text, traceId)) {
        char url[80]{};
        std::snprintf(url, sizeof(url), "%s?error=invalid_value", listPath);
        Esp32BaseWeb::redirectSeeOther(url);
        return;
    }
    std::uint32_t savedTraceId = 0;
    WaterPulseTraceSaveStatus status = WaterPulseTraceSaveStatus::Ok;
    const bool ok = saveRamTraceToDevice(traceId, &savedTraceId, &status);
    if (ok) {
        char url[120]{};
        std::snprintf(url,
                      sizeof(url),
                      "%s/detail?%ssaved=1&trace=%lu&bucket=1",
                      fromCalibration ? "/faucet/calibration" : "/faucet/records",
                      fromCalibration ? "from=calibration&" : "",
                      static_cast<unsigned long>(savedTraceId));
        Esp32BaseWeb::redirectSeeOther(url);
        return;
    }
    const char* error = status == WaterPulseTraceSaveStatus::LimitReached ? "trace_store_full"
                        : status == WaterPulseTraceSaveStatus::CorruptStore ? "trace_store_corrupt"
                                                                            : "save_failed";
    char url[96]{};
    std::snprintf(url, sizeof(url), "%s?error=%s", listPath, error);
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
        if (std::strcmp(route.path, "/faucet/calibration") == 0) {
            return handleCalibrationPage;
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
    if (std::strcmp(route.path, "/faucet/app.css") == 0) {
        return handleAppCss;
    }
    if (std::strcmp(route.path, "/faucet/filters/edit") == 0) {
        return handleFilterEditPage;
    }
    if (std::strcmp(route.path, "/faucet/records/detail") == 0 ||
        std::strcmp(route.path, "/faucet/calibration/detail") == 0) {
        return handleRecordDetailPage;
    }
    if (std::strcmp(route.path, "/faucet/calibration") == 0) {
        return handleCalibrationPost;
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

    Esp32BaseWeb::setHeadExtraCallback(sendAppStylesheetLink);

    bool ok = true;
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        if (!faucetWebRouteAllowed(routes[i].path)) {
            ok = false;
            continue;
        }
        if (routes[i].kind == FaucetWebRouteKind::Page && routes[i].method == FaucetWebMethod::Get) {
            ok = Esp32BaseWeb::addPage(routes[i].path, routes[i].title, handlerFor(routes[i])) && ok;
        } else {
            ok = Esp32BaseWeb::addRoute(routes[i].path, toBaseMethod(routes[i].method), handlerFor(routes[i])) && ok;
        }
    }
    return ok;
}

}  // namespace faucet

#endif
