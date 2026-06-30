#ifndef ESP32BASE_WEB_NATIVE_TEST
#define ESP32BASE_WEB_NATIVE_TEST 0
#endif

#if !defined(NATIVE_BUILD) || ESP32BASE_WEB_NATIVE_TEST

#include "web/FaucetWeb.h"

#include "app/AppController.h"
#include "app/AppConfig.h"
#include "app/ConfigStore.h"
#include "app/DateTimeUtils.h"
#include "app/FilterStore.h"
#include "app/MeteringSchemeStore.h"
#include "app/WaterRecordCalibrationStore.h"
#include "app/WaterRecordStore.h"
#include "app/WaterPulseTraceStore.h"
#include "web/FaucetWebAssets.h"
#include "web/FaucetWebJson.h"
#include "web/FaucetWebParsing.h"
#include "web/FaucetWebRoutes.h"

#if ESP32BASE_WEB_NATIVE_TEST
#include "web/Esp32BaseWeb.h"
#else
#include <Esp32Base.h>
#endif
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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
bool contextReady();
bool getParam(const char* name, char* out, std::size_t len);
bool persistPresetConfig(std::size_t index, const PresetConfig& preset);
bool ensureMeteringSchemesReady();
bool activeMeteringSchemeForWeb(MeteringSchemeRecord& output);
void handleRecordInfoPage();
void handleCalibrationDetailPage();
void handleCalibrationPage();
void handleFlowCalibrationPage();
void handleCalibrationPost();
void handleFlowCalibrationPost();
void handleCreateMeteringSchemeApi();
const char* calibrationSessionStatusText(CalibrationSessionStatus status);
bool calibrationSessionInactive(CalibrationSessionStatus status);
bool calibrationSessionStorageReady();
void redirectCalibrationFailure(const char* error);
void redirectFlowCalibrationFailure(const char* error);
void sendFlowCalibrationPostFailure(bool ajax, const char* failure);
void sendFlowCalibrationPostResult(bool ajax, bool ok, const char* success, const char* failure);
void sendDetailErrorPage(const char* title, const char* message, const char* backHref, const char* backLabel);
void formatWaterRecordTime(const WaterRecord& record, char* out, std::size_t len);
void formatWaterRecordListTime(const WaterRecord& record, char* out, std::size_t len);
void formatRecordTime(std::uint32_t seconds, char* out, std::size_t len);
void formatSecondsValue(std::uint32_t seconds, char* out, std::size_t len);
void formatFlowLitersPerMin(std::uint32_t flowMlPerMin, char* out, std::size_t len);
void sendNoticeFromQuery();
void sendPageEnd();
void sendManualParameterLink(const char* label, const MeteringSchemeRecord& scheme);

Esp32BaseWeb::Method toBaseMethod(FaucetWebMethod method) {
    switch (method) {
        case FaucetWebMethod::Get:
            return Esp32BaseWeb::METHOD_GET;
        case FaucetWebMethod::Post:
            return Esp32BaseWeb::METHOD_POST;
    }
    return Esp32BaseWeb::METHOD_GET;
}

void sendFmt(const char* fmt, ...) {
    char buffer[512]{};
    va_list args;
    va_start(args, fmt);
    va_list measureArgs;
    va_copy(measureArgs, args);
    const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0) {
        va_end(measureArgs);
        return;
    }
    if (static_cast<std::size_t>(written) >= sizeof(buffer)) {
        const std::size_t needed = static_cast<std::size_t>(written);
        char* dynamicBuffer = new (std::nothrow) char[needed + 1]{};
        if (!dynamicBuffer) {
            va_end(measureArgs);
            return;
        }
        std::vsnprintf(dynamicBuffer, needed + 1, fmt, measureArgs);
        va_end(measureArgs);
        Esp32BaseWeb::sendChunk(dynamicBuffer);
        delete[] dynamicBuffer;
        return;
    }
    va_end(measureArgs);
    Esp32BaseWeb::sendChunk(buffer);
}

void sendHtmlEscapedInternal(const char* text, std::size_t maxLen) {
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
                char ch[2] = {text[i], '\0'};
                Esp32BaseWeb::sendChunk(ch);
                break;
        }
    }
}

void sendHtmlAttrEscapedBounded(const char* text, std::size_t maxLen) {
    sendHtmlEscapedInternal(text ? text : "", maxLen);
}

void sendHtmlEscapedBounded(const char* text, std::size_t maxLen) {
    sendHtmlEscapedInternal(text ? text : "", maxLen);
}

bool isUrlUnreserved(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
           ch == '_' || ch == '.' || ch == '~';
}

void sendUrlQueryValueEscapedBounded(const char* text, std::size_t maxLen) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!text) {
        return;
    }
    for (std::size_t i = 0; i < maxLen && text[i] != '\0'; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (isUrlUnreserved(ch)) {
            char out[2] = {static_cast<char>(ch), '\0'};
            Esp32BaseWeb::sendChunk(out);
        } else {
            char out[4] = {'%', kHex[(ch >> 4U) & 0x0FU], kHex[ch & 0x0FU], '\0'};
            Esp32BaseWeb::sendChunk(out);
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
        char seconds[24]{};
        formatSecondsValue(record.targetValue, seconds, sizeof(seconds));
        Esp32BaseWeb::sendChunk(seconds);
        return;
    }
    sendLiters(record.targetValue);
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

void formatStartupDurationSeconds(std::uint32_t durationMs, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (durationMs % 1000U == 0) {
        std::snprintf(out, len, "%lu 秒", static_cast<unsigned long>(durationMs / 1000U));
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%03lu 秒",
                  static_cast<unsigned long>(durationMs / 1000U),
                  static_cast<unsigned long>(durationMs % 1000U));
}

void formatStartupDurationSecondsInput(std::uint32_t durationMs, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (durationMs % 1000U == 0) {
        std::snprintf(out, len, "%lu", static_cast<unsigned long>(durationMs / 1000U));
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%03lu",
                  static_cast<unsigned long>(durationMs / 1000U),
                  static_cast<unsigned long>(durationMs % 1000U));
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
    const char* mode = record.mode == WaterMode::Time ? "时间模式" : "容量模式";
    if (record.selectedPreset < kPresetCount) {
        std::snprintf(out,
                      len,
                      "预设 %u · %s · %s",
                      static_cast<unsigned>(record.selectedPreset) + 1U,
                      mode,
                      target);
    } else {
        std::snprintf(out, len, "%s · %s", mode, target);
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

void sendBusyJson(const char* context) {
    char json[80]{};
    std::snprintf(json, sizeof(json), "{\"error\":\"busy\",\"context\":\"%s\"}", context ? context : "web");
    Esp32BaseWeb::sendJson(409, json);
}

std::uint32_t recentAverageFlowMlPerMin() {
    if (!g_context.records || !g_context.records->ready()) {
        return 0;
    }
    std::unique_ptr<WaterRecord[]> records(new (std::nothrow) WaterRecord[kDefaultRecordPageSize]{});
    if (!records) {
        return 0;
    }
    const std::size_t read =
        g_context.records->readPage(0, kDefaultRecordPageSize, records.get(), kDefaultRecordPageSize);
    std::uint32_t totalMl = 0;
    std::uint32_t totalDurationSec = 0;
    for (std::size_t i = 0; i < read; ++i) {
        if (records[i].volumeMl == 0 || records[i].durationSec == 0) {
            continue;
        }
        addSaturating(totalMl, records[i].volumeMl);
        addSaturating(totalDurationSec, records[i].durationSec);
    }
    if (totalMl == 0 || totalDurationSec == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(totalMl) * 60ULL + totalDurationSec / 2ULL) / totalDurationSec);
}

std::uint32_t durationMsToDisplaySec(std::uint32_t durationMs) {
    if (durationMs == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(durationMs) + 500ULL) / 1000ULL);
}

std::uint32_t estimateDurationForVolumeTarget(std::uint32_t targetMl, const MeteringParameters& params) {
    return durationMsToDisplaySec(estimateDurationMsForVolumeMl(params, targetMl));
}

void applyTimeEstimateForTarget(AppSnapshot& snapshot,
                                std::uint32_t targetSec,
                                std::uint32_t& volumeMl,
                                std::uint32_t& pulseCount,
                                float& stablePulsePerSec,
                                const char*& reason) {
    volumeMl = 0;
    pulseCount = 0;
    stablePulsePerSec = 0.0f;
    reason = nullptr;
    if (!validMeteringSchemeParameters(snapshot.meteringParams)) {
        reason = "计量参数未就绪";
        return;
    }
    if (targetSec == 0) {
        reason = "目标时间为空";
        return;
    }
    const std::uint64_t durationMs = static_cast<std::uint64_t>(targetSec) * 1000ULL;
    volumeMl = estimateVolumeMlForDurationMs(
        snapshot.meteringParams,
        durationMs > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(durationMs));
    pulseCount = estimatePulsesForVolumeMl(snapshot.meteringParams, volumeMl);
    stablePulsePerSec =
        static_cast<float>(static_cast<double>(snapshot.meteringParams.stableFlowMlPerMin) *
                           static_cast<double>(snapshot.meteringParams.stablePulsePerLiter) / 60000.0);
    if (pulseCount == 0 || volumeMl == 0) {
        reason = "计量参数未就绪";
        pulseCount = 0;
        volumeMl = 0;
        stablePulsePerSec = 0.0f;
    }
}

void applyTargetDurationEstimate(AppSnapshot& snapshot, bool includeRecentFlow = true) {
    snapshot.targetEstimatedDurationSec = 0;
    snapshot.selectedPresetEstimatedDurationSec = 0;
    snapshot.targetEstimatedVolumeMl = 0;
    snapshot.targetEstimatedPulseCount = 0;
    snapshot.targetStablePulsePerSec = 0.0f;
    snapshot.targetEstimateReason = nullptr;
    snapshot.selectedPresetEstimatedVolumeMl = 0;
    snapshot.selectedPresetEstimatedPulseCount = 0;
    snapshot.selectedPresetStablePulsePerSec = 0.0f;
    snapshot.selectedPresetEstimateReason = nullptr;
    const std::uint32_t flowMlPerMin = includeRecentFlow ? recentAverageFlowMlPerMin() : 0;
    snapshot.recentAverageFlowMlPerMin = flowMlPerMin;
    if (snapshot.water.mode == WaterMode::Volume) {
        snapshot.targetEstimatedDurationSec =
            estimateDurationForVolumeTarget(snapshot.water.targetValue, snapshot.meteringParams);
        if (!validMeteringSchemeParameters(snapshot.meteringParams)) {
            snapshot.targetEstimateReason = "计量参数未就绪";
        }
    } else {
        applyTimeEstimateForTarget(snapshot,
                                   snapshot.water.targetValue,
                                   snapshot.targetEstimatedVolumeMl,
                                   snapshot.targetEstimatedPulseCount,
                                   snapshot.targetStablePulsePerSec,
                                   snapshot.targetEstimateReason);
    }
    if (g_context.config && snapshot.water.selectedPreset < kPresetCount) {
        const PresetConfig& preset = g_context.config->presets[snapshot.water.selectedPreset];
        if (preset.enabled && preset.type == PresetType::Volume) {
            snapshot.selectedPresetEstimatedDurationSec =
                estimateDurationForVolumeTarget(preset.value, snapshot.meteringParams);
            if (!validMeteringSchemeParameters(snapshot.meteringParams)) {
                snapshot.selectedPresetEstimateReason = "计量参数未就绪";
            }
        } else if (preset.enabled && preset.type == PresetType::Time) {
            applyTimeEstimateForTarget(snapshot,
                                       preset.value,
                                       snapshot.selectedPresetEstimatedVolumeMl,
                                       snapshot.selectedPresetEstimatedPulseCount,
                                       snapshot.selectedPresetStablePulsePerSec,
                                       snapshot.selectedPresetEstimateReason);
        }
    }
}

bool findRecordCalibration(const WaterRecord& record, WaterRecordCalibration& calibration) {
    return g_context.recordCalibrations && g_context.recordCalibrations->ready() &&
           g_context.recordCalibrations->find(record, calibration);
}

bool ensureMeteringSchemesReady() {
    return g_context.meteringSchemes && (g_context.meteringSchemes->ready() || g_context.meteringSchemes->begin());
}

const char* meteringSchemeSourceName(MeteringSchemeSource source) {
    switch (source) {
        case MeteringSchemeSource::Default:
            return "默认";
        case MeteringSchemeSource::CalibrationSession:
            return "校准生成";
        case MeteringSchemeSource::Manual:
            return "手工创建";
    }
    return "-";
}

const char* waterRecordFileStatusCode(WaterRecordFileStatus status) {
    switch (status) {
        case WaterRecordFileStatus::Ready:
            return "ready";
        case WaterRecordFileStatus::Unavailable:
            return "unavailable";
        case WaterRecordFileStatus::Missing:
            return "missing";
        case WaterRecordFileStatus::InvalidPath:
            return "invalid_path";
        case WaterRecordFileStatus::InvalidCapacity:
            return "invalid_capacity";
        case WaterRecordFileStatus::BackendFailure:
            return "backend_failure";
        case WaterRecordFileStatus::Corrupt:
            return "corrupt";
        case WaterRecordFileStatus::IncompatibleFormat:
            return "incompatible_format";
    }
    return "unknown";
}

const char* waterRecordFileStatusMessage(WaterRecordFileStatus status) {
    switch (status) {
        case WaterRecordFileStatus::Ready:
            return "记录存储正常。";
        case WaterRecordFileStatus::Missing:
            return "记录文件不存在，正在使用 RAM 临时记录；下次写入会尝试重新创建文件。";
        case WaterRecordFileStatus::InvalidPath:
            return "记录文件路径配置无效，正在使用 RAM 临时记录。";
        case WaterRecordFileStatus::InvalidCapacity:
            return "记录文件容量配置无效，正在使用 RAM 临时记录。";
        case WaterRecordFileStatus::BackendFailure:
            return "记录文件读写失败，正在使用 RAM 临时记录；请检查 Flash 文件系统。";
        case WaterRecordFileStatus::Corrupt:
            return "记录文件损坏或不完整，正在使用 RAM 临时记录。";
        case WaterRecordFileStatus::IncompatibleFormat:
            return "记录文件格式不兼容，正在使用 RAM 临时记录。";
        case WaterRecordFileStatus::Unavailable:
        default:
            return "记录存储不可用。";
    }
}

const char* appStorageStatusCode(AppStorageStatus status) {
    switch (status) {
        case AppStorageStatus::Ready:
            return "ready";
        case AppStorageStatus::Unavailable:
            return "unavailable";
        case AppStorageStatus::Missing:
            return "missing";
        case AppStorageStatus::InvalidPath:
            return "invalid_path";
        case AppStorageStatus::BackendFailure:
            return "backend_failure";
        case AppStorageStatus::Corrupt:
            return "corrupt";
        case AppStorageStatus::IncompatibleFormat:
            return "incompatible_format";
    }
    return "unknown";
}

const char* appStorageStatusMessage(AppStorageStatus status) {
    switch (status) {
        case AppStorageStatus::Ready:
            return "存储正常。";
        case AppStorageStatus::Missing:
            return "文件缺失；系统会在下次需要写入时尝试重新创建。";
        case AppStorageStatus::InvalidPath:
            return "文件路径配置无效。";
        case AppStorageStatus::BackendFailure:
            return "文件系统读写失败，请检查 Flash/LittleFS 状态。";
        case AppStorageStatus::Corrupt:
            return "文件损坏或未写完整；为保护已有数据，系统不会自动删除。";
        case AppStorageStatus::IncompatibleFormat:
            return "文件格式不兼容；为保护可识别数据，系统不会自动删除。";
        case AppStorageStatus::Unavailable:
        default:
            return "存储尚未就绪。";
    }
}

bool activeMeteringSchemeForWeb(MeteringSchemeRecord& output) {
    if (ensureMeteringSchemesReady() && g_context.meteringSchemes->activeScheme(output)) {
        return true;
    }
    if (g_context.app) {
        output = g_context.app->activeMeteringScheme();
        return output.recordUsed;
    }
    return false;
}

bool setAndApplyActiveMeteringSchemeForWeb(std::uint32_t schemeId) {
    if (!g_context.meteringSchemes || !g_context.app) {
        return false;
    }
    MeteringSchemeRecord next{};
    if (!g_context.meteringSchemes->findById(schemeId, next) || !validMeteringSchemeParameters(next.params)) {
        return false;
    }
    const std::uint32_t previousId = g_context.meteringSchemes->activeSchemeId();
    const MeteringSchemeRecord previous = g_context.app->activeMeteringScheme();
    const std::uint32_t now = g_context.nowSeconds ? g_context.nowSeconds() : 0;
    if (!g_context.meteringSchemes->setActiveScheme(schemeId)) {
        return false;
    }
    if (g_context.app->applyActiveMeteringScheme(next)) {
        return true;
    }
    if (previousId != 0 && previous.recordUsed) {
        g_context.meteringSchemes->setActiveScheme(previousId);
        g_context.app->applyActiveMeteringScheme(previous);
    }
    return false;
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

void formatSchemeTimestamp(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (seconds == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    if (seconds >= kMinRealDateSeconds) {
        formatRecordTime(seconds, out, len);
        return;
    }
    std::snprintf(out, len, "开机+%lu s", static_cast<unsigned long>(seconds));
}

void sendMeteringMetricCard(const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<div class='metering-metric'><span>");
    Esp32BaseWeb::sendChunk(label ? label : "");
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::sendChunk(value ? value : "-");
    Esp32BaseWeb::sendChunk("</strong></div>");
}

void sendActiveMeteringSchemeCard(const MeteringSchemeRecord& scheme, std::uint32_t activeId) {
    char startupPulses[24]{};
    char startupVolume[24]{};
    char stablePpl[24]{};
    char startupDuration[24]{};
    char stableFlow[24]{};
    char samples[24]{};
    char actualRange[56]{};
    char maxError[32]{};
    char meta[120]{};
    char minActual[24]{};
    char maxActual[24]{};
    char maxErrorMl[24]{};
    formatLiters(scheme.params.startupVolumeMl, startupVolume, sizeof(startupVolume));
    formatFlowLitersPerMin(scheme.params.stableFlowMlPerMin, stableFlow, sizeof(stableFlow));
    formatLiters(scheme.minActualMl, minActual, sizeof(minActual));
    formatLiters(scheme.maxActualMl, maxActual, sizeof(maxActual));
    formatLiters(scheme.maxErrorMl, maxErrorMl, sizeof(maxErrorMl));
    std::snprintf(startupPulses, sizeof(startupPulses), "%luP", static_cast<unsigned long>(scheme.params.startupPulseCount));
    std::snprintf(stablePpl, sizeof(stablePpl), "%luP/L", static_cast<unsigned long>(scheme.params.stablePulsePerLiter));
    formatStartupDurationSeconds(scheme.params.startupDurationMs, startupDuration, sizeof(startupDuration));
    std::snprintf(samples, sizeof(samples), "%u 条", static_cast<unsigned>(scheme.sampleCount));
    if (scheme.minActualMl > 0 || scheme.maxActualMl > 0) {
        std::snprintf(actualRange, sizeof(actualRange), "%s - %s", minActual, maxActual);
    } else {
        std::snprintf(actualRange, sizeof(actualRange), "-");
    }
    std::snprintf(maxError,
                  sizeof(maxError),
                  "%s / %u.%u%%",
                  maxErrorMl,
                  static_cast<unsigned>(scheme.maxErrorTenthPercent / 10U),
                  static_cast<unsigned>(scheme.maxErrorTenthPercent % 10U));
    std::snprintf(meta,
                  sizeof(meta),
                  "#%lu · %s",
                  static_cast<unsigned long>(scheme.id),
                  meteringSchemeSourceName(scheme.sourceType));

    Esp32BaseWeb::sendChunk("<section class='panel active-metering-card'><div class='panel-head'><div><h3><span>当前计量参数</span></h3><p class='active-metering-name'>");
    sendHtmlEscapedBounded(scheme.name, sizeof(scheme.name));
    Esp32BaseWeb::sendChunk("</p><p class='hint'>");
    Esp32BaseWeb::sendChunk(meta);
    Esp32BaseWeb::sendChunk("</p></div>");
    sendFmt("<span class='status-pill %s'>%s</span></div><div class='active-metering-metrics'>",
            scheme.id == activeId ? "status-ok" : "status-warn",
            scheme.id == activeId ? "当前参数" : "当前参数异常");
    sendMeteringMetricCard("启动脉冲", startupPulses);
    sendMeteringMetricCard("启动水量", startupVolume);
    sendMeteringMetricCard("稳态 P/L", stablePpl);
    sendMeteringMetricCard("启动时长", startupDuration);
    sendMeteringMetricCard("稳态流速", stableFlow);
    sendMeteringMetricCard("样本", samples);
    sendMeteringMetricCard("容量范围", actualRange);
    sendMeteringMetricCard("最大误差", maxError);
    Esp32BaseWeb::sendChunk("</div><div class='row-actions active-metering-actions'>");
    sendManualParameterLink("复制参数", scheme);
    Esp32BaseWeb::sendChunk("</div></section>");
}

void sendActiveMeteringSchemeSummaryPanel() {
    MeteringSchemeRecord active{};
    bool activeReady = false;
    if (g_context.app) {
        active = g_context.app->activeMeteringScheme();
        activeReady = active.recordUsed;
    }
    if (!activeReady && activeMeteringSchemeForWeb(active)) {
        activeReady = true;
    }
    if (activeReady) {
        sendActiveMeteringSchemeCard(active, active.id);
        return;
    }
    Esp32BaseWeb::sendChunk("<section class='panel active-metering-card'><div class='panel-head'><div><h3><span>当前计量参数</span></h3>"
                            "<p class='active-metering-name'>-</p><p class='hint'>计量方案尚未就绪。</p></div>"
                            "<span class='status-pill status-muted'>无可用参数</span></div></section>");
}

void sendManualParameterLink(const char* label, const MeteringSchemeRecord& scheme) {
    const MeteringParameters& params = scheme.params;
    sendFmt("<a class='btn-link' href='/faucet/calibration/flow?manual=1&name=");
    sendUrlQueryValueEscapedBounded(scheme.name[0] ? scheme.name : "手工参数", sizeof(scheme.name));
    sendFmt("&startupPulseCount=%lu&startupVolumeMl=%lu&stablePulsePerLiter=%lu&startupDurationMs=%lu&stableFlowMlPerMin=%lu'>%s</a>",
            static_cast<unsigned long>(params.startupPulseCount),
            static_cast<unsigned long>(params.startupVolumeMl),
            static_cast<unsigned long>(params.stablePulsePerLiter),
            static_cast<unsigned long>(params.startupDurationMs),
            static_cast<unsigned long>(params.stableFlowMlPerMin),
            label ? label : "复制参数");
}

void sendCalibrationParameterPanels() {
    MeteringSchemeRecord* schemes = new (std::nothrow) MeteringSchemeRecord[kMeteringSchemeStoreSlotCount]{};
    const bool ready = ensureMeteringSchemesReady();
    const std::size_t count =
        ready && schemes ? g_context.meteringSchemes->list(schemes, kMeteringSchemeStoreSlotCount) : 0;
    const std::uint32_t activeId = ready ? g_context.meteringSchemes->activeSchemeId() : 0;
    char createdText[24]{};
    std::uint32_t createdSchemeId = 0;
    if (getParam("createdScheme", createdText, sizeof(createdText))) {
        parseU32(createdText, createdSchemeId);
    }

    Esp32BaseWeb::sendChunk("<section id='metering-scheme-list' class='panel calibration-param-panel'><div class='panel-head'><h3>历史参数</h3>"
                            "<a class='btn-link' href='/faucet/calibration/flow?manual=1'>手工输入参数</a></div>"
                            "<table class='calibration-slot-table metering-scheme-table'><tr><th>参数</th><th>状态</th><th>启动阶段</th><th>稳态阶段</th><th>样本与误差</th><th>操作</th></tr>");
    if (!ready) {
        const AppStorageStatus status =
            g_context.meteringSchemes ? g_context.meteringSchemes->status() : AppStorageStatus::Unavailable;
        sendFmt("<tr><td colspan='6'>历史参数存储不可用：%s（%s）。</td></tr>",
                appStorageStatusMessage(status),
                appStorageStatusCode(status));
    } else if (!schemes) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='6'>内存不足，无法加载历史参数。</td></tr>");
    } else if (count == 0) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='6'>还没有历史参数。可以手工输入一组参数。</td></tr>");
    }
    for (std::size_t i = 0; i < count; ++i) {
        const MeteringSchemeRecord& scheme = schemes[i];
        char startupVolume[24]{};
        char startupDuration[24]{};
        char stableFlow[24]{};
        char minActual[20]{};
        char maxActual[20]{};
        char maxError[20]{};
        formatLiters(scheme.params.startupVolumeMl, startupVolume, sizeof(startupVolume));
        formatStartupDurationSeconds(scheme.params.startupDurationMs, startupDuration, sizeof(startupDuration));
        formatFlowLitersPerMin(scheme.params.stableFlowMlPerMin, stableFlow, sizeof(stableFlow));
        if (scheme.sampleCount > 0) {
            formatLiters(scheme.minActualMl, minActual, sizeof(minActual));
            formatLiters(scheme.maxActualMl, maxActual, sizeof(maxActual));
            formatLiters(scheme.maxErrorMl, maxError, sizeof(maxError));
        }

        sendFmt("<tr class='%s%s'><td><b>#%lu ",
                scheme.id == activeId ? "is-active " : "",
                scheme.id == createdSchemeId ? "scheme-created-row" : "",
                static_cast<unsigned long>(scheme.id));
        sendHtmlEscapedBounded(scheme.name, sizeof(scheme.name));
        sendFmt("</b><small>%s</small></td><td>", meteringSchemeSourceName(scheme.sourceType));
        if (scheme.id == activeId) {
            Esp32BaseWeb::sendChunk("<span class='status-pill status-ok'>正在使用</span>");
            if (scheme.id == createdSchemeId) {
                Esp32BaseWeb::sendChunk(" <span class='status-pill status-warn'>刚保存</span>");
            }
        } else {
            Esp32BaseWeb::sendChunk("<span class='status-pill status-muted'>可用</span>");
            if (scheme.id == createdSchemeId) {
                Esp32BaseWeb::sendChunk(" <span class='status-pill status-warn'>刚保存</span>");
            }
        }
        sendFmt("</td><td><div class='scheme-param-lines'><span>启动脉冲：%luP</span><span>启动水量：%s</span><span>启动时长：%s</span></div></td>"
                "<td><div class='scheme-param-lines'><span>容量计量：%luP/L</span><span>时间估算：%s</span></div></td>"
                "<td><div class='scheme-param-lines'>",
                static_cast<unsigned long>(scheme.params.startupPulseCount),
                startupVolume,
                startupDuration,
                static_cast<unsigned long>(scheme.params.stablePulsePerLiter),
                stableFlow);
        if (scheme.sampleCount > 0) {
            sendFmt("<span>样本：%u 条</span><span>容量范围：%s - %s</span><span>最大误差：%s / %u.%u%%</span>",
                    static_cast<unsigned>(scheme.sampleCount),
                    minActual,
                    maxActual,
                    maxError,
                    static_cast<unsigned>(scheme.maxErrorTenthPercent / 10U),
                    static_cast<unsigned>(scheme.maxErrorTenthPercent % 10U));
        } else {
            Esp32BaseWeb::sendChunk("<span>样本：无样本摘要</span><span>最大误差：-</span>");
        }
        Esp32BaseWeb::sendChunk("</div></td><td><div class='row-actions scheme-row-actions'>");
        sendManualParameterLink("复制参数", scheme);
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    if (ready && count == kMeteringSchemeStoreSlotCount) {
        Esp32BaseWeb::sendChunk("<p class='muted'>历史参数数量已满；下次新建参数会覆盖最早的非当前参数。</p>");
    }
    Esp32BaseWeb::sendChunk("</section>");
    delete[] schemes;
}

void sendManualMeteringParameterPage(const MeteringSchemeRecord* scheme) {
    const char* title = "手工输入参数";
    Esp32BaseWeb::sendHeader(title);
    sendFmt("<h2>%s</h2><p><a class='btn-link' href='/faucet/calibration/flow'>返回校准</a></p>", title);
    sendNoticeFromQuery();
    const MeteringParameters params = scheme ? scheme->params : MeteringParameters{};
    const bool hasValues = scheme != nullptr;
    Esp32BaseWeb::sendChunk("<section class='panel calibration-param-panel manual-param-panel'><form class='manual-param-form' method='post' action='/faucet/calibration/flow' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("<input type='hidden' name='action' value='create_metering_scheme'>");
    Esp32BaseWeb::sendChunk("<div class='manual-param-section'><h3>参数信息</h3><div class='manual-param-grid'>");
    sendFmt("<label class='compact-field manual-param-field scheme-span-12'><span>名称</span><input name='name' maxlength='%u' required value='",
            static_cast<unsigned>(kMeteringSchemeNameLength - 1));
    if (scheme && scheme->name[0]) {
        sendHtmlAttrEscapedBounded(scheme->name, sizeof(scheme->name));
    }
    Esp32BaseWeb::sendChunk("'></label>");
    Esp32BaseWeb::sendChunk("</div></div><div class='manual-param-section'><h3>容量估算计量参数</h3><div class='manual-param-grid'>");
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>启动脉冲数</span><input name='startupPulseCount' type='number' min='0' max='%lu' step='1' required%s",
            static_cast<unsigned long>(kMaxSegmentedStartupPulseCount),
            hasValues ? " value='" : "");
    if (hasValues) {
        sendFmt("%lu'", static_cast<unsigned long>(params.startupPulseCount));
    }
    sendFmt("><small class='hint'>单位 P，范围 0-%lu</small></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupPulseCount));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>启动水量</span><input name='startupVolumeMl' type='number' min='0' max='%lu' step='1' required%s",
            static_cast<unsigned long>(kMaxSegmentedStartupVolumeMl),
            hasValues ? " value='" : "");
    if (hasValues) {
        sendFmt("%lu'", static_cast<unsigned long>(params.startupVolumeMl));
    }
    sendFmt("><small class='hint'>单位 ml，范围 0-%lu</small></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupVolumeMl));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>稳态 P/L</span><input name='stablePulsePerLiter' type='number' min='%lu' max='%lu' step='1' required%s",
            static_cast<unsigned long>(kMinSegmentedPulsePerLiter),
            static_cast<unsigned long>(kMaxSegmentedPulsePerLiter),
            hasValues ? " value='" : "");
    if (hasValues) {
        sendFmt("%lu'", static_cast<unsigned long>(params.stablePulsePerLiter));
    }
    sendFmt("><small class='hint'>单位 P/L，范围 %lu-%lu</small></label>",
            static_cast<unsigned long>(kMinSegmentedPulsePerLiter),
            static_cast<unsigned long>(kMaxSegmentedPulsePerLiter));
    Esp32BaseWeb::sendChunk("</div></div><div class='manual-param-section'><h3>时间估算计量参数</h3><p class='muted'>仅用于预计时间和预计流速展示，不参与实际容量、滤芯累计或统计累计。</p><div class='manual-param-grid'>");
    char startupDurationSec[24]{};
    formatStartupDurationSecondsInput(params.startupDurationMs, startupDurationSec, sizeof(startupDurationSec));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>启动时长</span><input name='startupDurationSec' type='number' min='0' max='%lu' step='0.001' required%s",
            static_cast<unsigned long>(kMaxSegmentedStartupDurationMs / 1000U),
            hasValues ? " value='" : "");
    if (hasValues) {
        sendFmt("%s'", startupDurationSec);
    }
    sendFmt("><small class='hint'>单位 秒，范围 0-%lu，可精确到 0.001 秒</small></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupDurationMs / 1000U));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>预计稳态流速</span><input name='stableFlowMlPerMin' type='number' min='%lu' max='%lu' step='1' required%s",
            static_cast<unsigned long>(kMinStableFlowMlPerMin),
            static_cast<unsigned long>(kMaxStableFlowMlPerMin),
            hasValues ? " value='" : "");
    if (hasValues) {
        sendFmt("%lu'", static_cast<unsigned long>(params.stableFlowMlPerMin));
    }
    sendFmt("><small class='hint'>单位 ml/min，范围 %lu-%lu</small></label>",
            static_cast<unsigned long>(kMinStableFlowMlPerMin),
            static_cast<unsigned long>(kMaxStableFlowMlPerMin));
    Esp32BaseWeb::sendChunk("</div></div>");
    Esp32BaseWeb::sendChunk("<div class='form-actions manual-param-actions'><input class='primary' type='submit' value='保存参数'>");
    Esp32BaseWeb::sendChunk("<a class='btn-link' href='/faucet/calibration/flow'>取消</a></div></form></section>");
    sendPageEnd();
}

bool readMeteringManualPrefillFromQuery(MeteringParameters& params, bool& present) {
    present = false;
    char text[32]{};
    if (!getParam("startupPulseCount", text, sizeof(text))) {
        return true;
    }
    present = true;
    if (!parseU32(text, params.startupPulseCount) ||
        !getParam("startupVolumeMl", text, sizeof(text)) || !parseU32(text, params.startupVolumeMl) ||
        !getParam("stablePulsePerLiter", text, sizeof(text)) || !parseU32(text, params.stablePulsePerLiter) ||
        !getParam("startupDurationMs", text, sizeof(text)) || !parseU32(text, params.startupDurationMs) ||
        !getParam("stableFlowMlPerMin", text, sizeof(text)) || !parseU32(text, params.stableFlowMlPerMin)) {
        return false;
    }
    return validMeteringSchemeParameters(params);
}


std::uint32_t configuredPulseObservationWindowSec() {
    const std::uint32_t seconds =
        g_context.config ? g_context.config->pulseObservationWindowSec : kDefaultPulseObservationWindowSec;
    return std::min<std::uint32_t>(std::max<std::uint32_t>(seconds, kMinPulseObservationWindowSec),
                                   kMaxPulseObservationWindowSec);
}

const char* calibrationAttemptStatusText(CalibrationAttemptStatus status) {
    switch (status) {
        case CalibrationAttemptStatus::PendingActual:
            return "待填写实测";
        case CalibrationAttemptStatus::Valid:
            return "可用于校准";
        case CalibrationAttemptStatus::Skipped:
            return "已放弃";
        case CalibrationAttemptStatus::Invalid:
            return "不可用于校准";
        case CalibrationAttemptStatus::Removed:
            return "已移除";
        case CalibrationAttemptStatus::Empty:
            break;
    }
    return "未记录";
}

const char* calibrationInvalidReasonText(CalibrationInvalidReason reason) {
    switch (reason) {
        case CalibrationInvalidReason::TruncatedTrace:
            return "明细已截断";
        case CalibrationInvalidReason::MissingActualMl:
            return "缺少实测容量";
        case CalibrationInvalidReason::NoEffectivePulse:
            return "无有效脉冲";
        case CalibrationInvalidReason::AnalysisFailed:
            return "稳态识别失败";
        case CalibrationInvalidReason::ErrorResult:
            return "出水结果异常";
        case CalibrationInvalidReason::StorageFailed:
            return "存储失败";
        case CalibrationInvalidReason::None:
            break;
    }
    return "不满足有效样本条件";
}

void sendCalibrationAttemptTraceLink(const CalibrationAttempt& attempt) {
    if (attempt.sessionTraceSlot < kCalibrationSessionTraceSlots && g_context.calibrationSessionTraces &&
        g_context.calibrationSessionTraces->ready()) {
        CalibrationStoredTrace stored{};
        if (g_context.calibrationSessionTraces->load(attempt.sessionTraceSlot, stored)) {
            sendFmt("<a class='btn-link' href='/faucet/calibration/detail?slot=%u&bucket=1'>脉冲明细</a>",
                    static_cast<unsigned>(attempt.sessionTraceSlot));
            return;
        }
    }
    const WaterPulseTrace* trace =
        g_context.pulseTraces ? g_context.pulseTraces->findByRecord(attempt.record) : nullptr;
    if (!trace) {
        Esp32BaseWeb::sendChunk("<span class='hint'>明细不可用</span>");
        return;
    }
    sendFmt("<a class='btn-link' href='/faucet/calibration/detail?trace=%lu&bucket=1'>脉冲明细</a>",
            static_cast<unsigned long>(trace->traceId));
}

void sendCalibrationSessionAttemptRow(const CalibrationSessionRecord& session,
                                      const CalibrationAttempt& attempt,
                                      std::uint32_t samplePulseWindowSec) {
    char timeText[40]{};
    char durationText[24]{};
    char targetText[24]{};
    char estimateText[24]{};
    char actualText[24]{};
    formatWaterRecordListTime(attempt.record, timeText, sizeof(timeText));
    formatDurationShort(attempt.record.durationSec, durationText, sizeof(durationText));
    formatLitersMl(attempt.targetHintMl, targetText, sizeof(targetText));
    formatLitersMl(attempt.record.volumeMl, estimateText, sizeof(estimateText));
    if (attempt.actualMl > 0) {
        formatLitersMl(attempt.actualMl, actualText, sizeof(actualText));
    } else {
        std::snprintf(actualText, sizeof(actualText), "-");
    }

    CalibrationStoredTrace stored{};
    const bool hasStoredTrace = g_context.calibrationSessionTraces && g_context.calibrationSessionTraces->ready() &&
                                attempt.sessionTraceSlot < kCalibrationSessionTraceSlots &&
                                g_context.calibrationSessionTraces->load(attempt.sessionTraceSlot, stored);
    std::uint32_t firstWindowPulses = 0;
    bool firstWindowReady = false;
    if (hasStoredTrace && samplePulseWindowSec > 0 && stored.trace.bucketCount > 0) {
        WaterPulseTraceBucketSample* traceBuckets =
            new (std::nothrow) WaterPulseTraceBucketSample[stored.trace.bucketCount]{};
        if (traceBuckets) {
            const std::size_t copied = g_context.calibrationSessionTraces->readBuckets(
                attempt.sessionTraceSlot, traceBuckets, stored.trace.bucketCount);
            if (copied == stored.trace.bucketCount) {
                const std::size_t windowBuckets =
                    static_cast<std::size_t>((samplePulseWindowSec * 1000UL + kPulseTraceBucketMs - 1UL) /
                                             kPulseTraceBucketMs);
                const std::size_t count = std::min(copied, windowBuckets);
                for (std::size_t i = 0; i < count; ++i) {
                    firstWindowPulses += traceBuckets[i].pulseCount;
                }
                firstWindowReady = true;
            }
            delete[] traceBuckets;
        }
    }
    const bool stableReady =
        attempt.status == CalibrationAttemptStatus::Valid &&
        attempt.summary.usableForGeneration &&
        attempt.summary.stable &&
        attempt.summary.stablePulseCount > 0 &&
        !attempt.summary.truncated;
    sendFmt("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%luP</td><td>",
            timeText,
            durationText,
            attempt.targetHintMl > 0 ? targetText : "-",
            attempt.record.volumeMl > 0 ? estimateText : "-",
            actualText,
            static_cast<unsigned long>(attempt.record.pulseCount));
    if (firstWindowReady) {
        sendFmt("%luP", static_cast<unsigned long>(firstWindowPulses));
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    sendCalibrationAttemptTraceLink(attempt);
    const bool savedOnly = attempt.status == CalibrationAttemptStatus::Valid && !stableReady;
    sendFmt("</td><td><span class='status-pill %s'>%s</span></td><td>本次会话 #%u</td><td><span class='status-pill %s'>%s</span>",
            stableReady ? "status-ok" : (attempt.status == CalibrationAttemptStatus::Invalid ? "status-warn" : "status-muted"),
            stableReady ? "稳态可用" : (attempt.status == CalibrationAttemptStatus::Invalid ? calibrationInvalidReasonText(attempt.invalidReason) : (savedOnly ? "仅记录" : "-")),
            static_cast<unsigned>(attempt.attemptIndex + 1),
            attempt.status == CalibrationAttemptStatus::Valid ? "status-ok" :
                (attempt.status == CalibrationAttemptStatus::Invalid ? "status-warn" : "status-muted"),
            calibrationAttemptStatusText(attempt.status));
    Esp32BaseWeb::sendChunk("</td><td>");
    bool renderedAction = false;
    if (attempt.status == CalibrationAttemptStatus::Valid || attempt.status == CalibrationAttemptStatus::PendingActual) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit='return faucetSubmitFlowCalibrationAction(this)'>"
                                "<input type='hidden' name='action' value='remove_sample'>"
                                "<input type='hidden' name='attemptIndex' value='");
        sendFmt("%u", static_cast<unsigned>(attempt.attemptIndex));
        Esp32BaseWeb::sendChunk("'><input class='danger' type='submit' value='移除'></form>");
        renderedAction = true;
    }
    if (!renderedAction) {
        Esp32BaseWeb::sendChunk("<span class='hint'>无可用操作</span>");
    }
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void sendCalibrationSamplesPanel(std::uint32_t samplePulseWindowSec) {
    Esp32BaseWeb::sendChunk("<section id='calibration-samples' class='panel'><div class='panel-head'><h3>本次校准样本</h3></div>"
                            "<p class='hint'>这里记录当前校准会话的接水记录；有效样本会自动刷新推荐参数，异常或误操作样本可移除。</p>");
    sendFmt("<table class='calibration-sample-table'><tr><th>时间</th><th>时长</th><th>目标容量</th><th>估算出水</th><th>量杯实测</th><th>总脉冲</th><th>前 %u 秒脉冲</th><th>脉冲明细</th><th>稳态识别</th><th>样本来源</th><th>校准用途</th><th>操作</th></tr>",
            static_cast<unsigned>(samplePulseWindowSec));
    if (!g_context.calibrationSessions || !g_context.calibrationSessions->ready()) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='12'>校准会话存储未就绪，暂时不能读取本次接水记录。</td></tr></table></section>");
        return;
    }
    std::unique_ptr<CalibrationSessionRecord> session(new (std::nothrow) CalibrationSessionRecord);
    if (!session) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='12'>内存不足，暂时不能读取本次校准样本。</td></tr></table></section>");
        return;
    }
    if (!g_context.calibrationSessions->load(*session) || session->attemptCount == 0) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='12'>还没有本次校准样本。进入校准模式后，每次本地出水都会记录到这里。</td></tr></table></section>");
        return;
    }
    bool rendered = false;
    for (std::uint8_t i = 0; i < session->attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = session->attempts[i];
        if (attempt.status == CalibrationAttemptStatus::Empty) {
            continue;
        }
        sendCalibrationSessionAttemptRow(*session, attempt, samplePulseWindowSec);
        rendered = true;
    }
    if (!rendered) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='12'>还没有本次校准样本。进入校准模式后，每次本地出水都会记录到这里。</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table></section>");
}

const char* calibrationSessionStatusCode(CalibrationSessionStatus status) {
    switch (status) {
        case CalibrationSessionStatus::Idle:
            return "idle";
        case CalibrationSessionStatus::Preparing:
            return "preparing";
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

void sendGeneratedMeteringCandidatePanel(const MeteringSchemeCandidate& candidate) {
    if (!candidate.ready) {
        return;
    }
    char startupPulses[24]{};
    char startupVolume[24]{};
    char stablePpl[24]{};
    char startupDuration[24]{};
    char stableFlow[24]{};
    char generatedAt[40]{};
    char samples[24]{};
    char minActual[24]{};
    char maxActual[24]{};
    char actualRange[56]{};
    char maxErrorMl[24]{};
    char maxError[40]{};
    std::snprintf(startupPulses,
                  sizeof(startupPulses),
                  "%luP",
                  static_cast<unsigned long>(candidate.params.startupPulseCount));
    formatLiters(candidate.params.startupVolumeMl, startupVolume, sizeof(startupVolume));
    std::snprintf(stablePpl,
                  sizeof(stablePpl),
                  "%luP/L",
                  static_cast<unsigned long>(candidate.params.stablePulsePerLiter));
    formatStartupDurationSeconds(candidate.params.startupDurationMs, startupDuration, sizeof(startupDuration));
    formatFlowLitersPerMin(candidate.params.stableFlowMlPerMin, stableFlow, sizeof(stableFlow));
    formatSchemeTimestamp(candidate.generatedAt, generatedAt, sizeof(generatedAt));
    std::snprintf(samples, sizeof(samples), "%u 条", static_cast<unsigned>(candidate.sampleCount));
    formatLiters(candidate.minActualMl, minActual, sizeof(minActual));
    formatLiters(candidate.maxActualMl, maxActual, sizeof(maxActual));
    if (candidate.minActualMl > 0 || candidate.maxActualMl > 0) {
        std::snprintf(actualRange, sizeof(actualRange), "%s - %s", minActual, maxActual);
    } else {
        std::snprintf(actualRange, sizeof(actualRange), "-");
    }
    formatLiters(candidate.maxErrorMl, maxErrorMl, sizeof(maxErrorMl));
    std::snprintf(maxError,
                  sizeof(maxError),
                  "%s / %u.%u%%",
                  maxErrorMl,
                  static_cast<unsigned>(candidate.maxErrorTenthPercent / 10U),
                  static_cast<unsigned>(candidate.maxErrorTenthPercent % 10U));

    Esp32BaseWeb::sendChunk("<div class='generated-metering-candidate'><div class='panel-head'><div><h4>推荐计量参数</h4>"
                            "<p class='hint'>确认前请核对这组自动生成的参数。</p></div>"
                            "<span class='status-pill status-ok'>待应用</span></div>"
                            "<div class='active-metering-metrics'>");
    sendMeteringMetricCard("启动脉冲", startupPulses);
    sendMeteringMetricCard("启动水量", startupVolume);
    sendMeteringMetricCard("稳态 P/L", stablePpl);
    sendMeteringMetricCard("启动时长", startupDuration);
    sendMeteringMetricCard("稳态流速", stableFlow);
    sendMeteringMetricCard("生成来源", meteringSchemeSourceName(candidate.sourceType));
    sendMeteringMetricCard("生成时间", generatedAt);
    sendMeteringMetricCard("样本数", samples);
    sendMeteringMetricCard("容量范围", actualRange);
    sendMeteringMetricCard("最大误差", maxError);
    Esp32BaseWeb::sendChunk("</div></div>");
}

void sendFlowCalibrationSessionPanel(const AppSnapshot& snapshot, bool taskActive) {
    const bool sessionActive = snapshot.calibrationStatus != CalibrationSessionStatus::Idle &&
                               snapshot.calibrationStatus != CalibrationSessionStatus::Applied &&
                               snapshot.calibrationStatus != CalibrationSessionStatus::Discarded &&
                               snapshot.calibrationStatus != CalibrationSessionStatus::Failed;
    const bool canStartSession = calibrationSessionInactive(snapshot.calibrationStatus) && !taskActive;
    const bool canDiscardSession = sessionActive && snapshot.calibrationStatus != CalibrationSessionStatus::Running && !taskActive;
    const bool canEnterActual = snapshot.calibrationStatus == CalibrationSessionStatus::AwaitingActual;
    const bool canApply = snapshot.calibrationStatus == CalibrationSessionStatus::Generated && !taskActive;
    const bool autoRefreshSession =
        snapshot.calibrationStatus == CalibrationSessionStatus::WaitingLocalRun ||
        snapshot.calibrationStatus == CalibrationSessionStatus::Running;

    sendFmt("<section id='calibration-session' class='panel calibration-session-panel' data-calibration-status='%s' data-calibration-valid-samples='%u'>"
            "<div class='panel-head'><h3>校准流程</h3><div class='calibration-session-badges'>",
            calibrationSessionStatusCode(snapshot.calibrationStatus),
            static_cast<unsigned>(snapshot.calibrationValidSampleCount));
    sendFmt("<span class='status-pill %s'%s>%s</span>",
            sessionActive ? "status-warn" : "status-muted",
            autoRefreshSession ? " data-calibration-refresh" : "",
            calibrationSessionStatusText(snapshot.calibrationStatus));
    Esp32BaseWeb::sendChunk("</div></div><p class='muted'>至少 2 条有效样本后会自动生成参数；3 条以上更稳。到设备旁按 OK 开始校准出水，按 CANCEL 停止；网页只录入量杯读数并处理样本。</p>");
    if (snapshot.calibrationStatus == CalibrationSessionStatus::WaitingLocalRun) {
        Esp32BaseWeb::sendChunk("<p class='ok'>设备正在等待本地 OK 开始校准出水。页面会自动刷新状态。</p>");
    } else if (snapshot.calibrationStatus == CalibrationSessionStatus::Running) {
        Esp32BaseWeb::sendChunk("<p class='warn'>校准出水中。请在设备本地按 CANCEL 停止，停止后网页会进入实测容量录入。</p>");
    } else if (snapshot.calibrationStatus == CalibrationSessionStatus::AwaitingActual) {
        Esp32BaseWeb::sendChunk("<p class='ok'>本次出水已停止，请按量杯读数填写实测容量。</p>");
    } else if (snapshot.calibrationStatus == CalibrationSessionStatus::Generated) {
        Esp32BaseWeb::sendChunk("<p class='ok'>已自动计算出参数建议；可以继续按 OK 补充样本，或确认使用这组参数。</p>");
    }
    Esp32BaseWeb::sendChunk("<div class='form-actions calibration-primary-actions'>");
    if (!sessionActive) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='start_session'>");
        sendFmt("<input class='primary' type='submit' value='开始校准流程'%s></form>",
                canStartSession ? "" : " disabled");
    } else {
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit=\"return confirm('确认退出并丢弃本次校准会话？')&&once(this)\">"
                                "<input type='hidden' name='action' value='discard_session'>");
        sendFmt("<input class='secondary' type='submit' value='退出校准流程'%s></form>",
                canDiscardSession ? "" : " disabled");
    }
    Esp32BaseWeb::sendChunk("</div>");
    if (canEnterActual) {
        Esp32BaseWeb::sendChunk("<form class='sample-calibration-form' method='post' action='/faucet/calibration/flow' onsubmit='return faucetSubmitFlowCalibrationAction(this)'>"
                                "<input type='hidden' name='action' value='save_actual'>"
                                "<label class='compact-field'><span>本次实测容量</span><span class='estimator-input-row'>"
                                "<input name='actualMl' type='number' min='");
        sendFmt("%lu", static_cast<unsigned long>(kCalibrationMinActualMl));
        Esp32BaseWeb::sendChunk("' max='");
        sendFmt("%lu", static_cast<unsigned long>(kMaxVolumePresetMl));
        Esp32BaseWeb::sendChunk("' step='1' required><span class='unit-label'>ml</span></span></label>"
                                "<div class='form-actions'><input class='primary' type='submit' value='保存有效样本'></form>"
                                "<form method='post' action='/faucet/calibration/flow' onsubmit=\"return confirm('确认放弃本次出水样本？')&&faucetSubmitFlowCalibrationAction(this)\">"
                                "<input type='hidden' name='action' value='skip_attempt'>"
                                "<input class='danger' type='submit' value='放弃本次样本'></form></div>");
    }
    if (snapshot.calibrationStatus == CalibrationSessionStatus::Generated) {
        sendGeneratedMeteringCandidatePanel(snapshot.calibrationCandidate);
    }
    Esp32BaseWeb::sendChunk("<div class='form-actions calibration-secondary-actions'>"
                            "<form method='post' action='/faucet/calibration/flow' onsubmit=\"return confirm('确认使用这组计量参数？')&&once(this)\">"
                            "<input type='hidden' name='action' value='apply_session'>");
    sendFmt("<input class='primary' type='submit' value='使用这组参数'%s></form></div></section>",
            canApply ? "" : " disabled");
}

void formatCentiTemperature(std::int32_t centi, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    const std::int32_t absCenti = centi < 0 ? -centi : centi;
    std::snprintf(out,
                  len,
                  "%s%ld.%02ld C",
                  centi < 0 ? "-" : "",
                  static_cast<long>(absCenti / 100),
                  static_cast<long>(absCenti % 100));
}

void formatSensorTemperature(const SensorValue& value, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!value.valid) {
        std::snprintf(out, len, "-");
        return;
    }
    formatCentiTemperature(value.value, out, len);
}

void formatSensorTemperatureNumber(const SensorValue& value, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!value.valid) {
        std::snprintf(out, len, "--");
        return;
    }
    const std::int32_t centi = value.value;
    const std::int32_t absCenti = centi < 0 ? -centi : centi;
    std::snprintf(out,
                  len,
                  "%s%ld.%02ld",
                  centi < 0 ? "-" : "",
                  static_cast<long>(absCenti / 100),
                  static_cast<long>(absCenti % 100));
}

void formatWaterRecordTemperature(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (record.sensorSampleCount == 0 || (record.sensorFlags & kWaterSensorFlagTempInvalid) != 0) {
        std::snprintf(out, len, "--");
        return;
    }
    formatCentiTemperature(record.temperatureAvgCentiC, out, len);
}

void formatWaterRecordTds(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (record.sensorSampleCount == 0 || (record.sensorFlags & kWaterSensorFlagTdsInvalid) != 0) {
        std::snprintf(out, len, "--");
        return;
    }
    std::snprintf(out, len, "%u ppm", static_cast<unsigned>(record.tdsAvgPpm));
}

void formatSensorIntegerNumber(const SensorValue& value, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!value.valid) {
        std::snprintf(out, len, "--");
        return;
    }
    std::snprintf(out, len, "%ld", static_cast<long>(value.value));
}

void formatSensorIntegerValue(const SensorValue& value, const char* unit, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!value.valid) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out, len, "%ld %s", static_cast<long>(value.value), unit ? unit : "");
}

bool parseI16(const char* text, std::int16_t& value) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < INT16_MIN || parsed > INT16_MAX) {
        return false;
    }
    value = static_cast<std::int16_t>(parsed);
    return true;
}

const char* tdsCalibrationModeText(TdsCalibrationMode mode);

const char* recordTdsCalibrationText(const WaterRecord& record) {
    if (record.sensorSampleCount == 0) {
        return "--";
    }
    if (record.tdsCalibratedAtRun != 0 && (record.sensorFlags & kWaterSensorFlagTdsUncalibrated) == 0) {
        return tdsCalibrationModeText(static_cast<TdsCalibrationMode>(record.tdsCalibrationModeAtRun));
    }
    return "未校准";
}

void sendWaterRecordSensorRows(const WaterRecord& record) {
    char temperatureAvg[24]{};
    char temperatureMin[24]{};
    char temperatureMax[24]{};
    char tdsAvg[24]{};
    formatWaterRecordTemperature(record, temperatureAvg, sizeof(temperatureAvg));
    if ((record.sensorFlags & kWaterSensorFlagTempInvalid) != 0 || record.sensorSampleCount == 0) {
        std::snprintf(temperatureMin, sizeof(temperatureMin), "--");
        std::snprintf(temperatureMax, sizeof(temperatureMax), "--");
    } else {
        formatCentiTemperature(record.temperatureMinCentiC, temperatureMin, sizeof(temperatureMin));
        formatCentiTemperature(record.temperatureMaxCentiC, temperatureMax, sizeof(temperatureMax));
    }
    formatWaterRecordTds(record, tdsAvg, sizeof(tdsAvg));

    Esp32BaseWeb::sendChunk("<section class='panel record-detail-card'><h3>水质传感器</h3><table class='kv'>");
    if (record.sensorSampleCount == 0) {
        Esp32BaseWeb::sendChunk("<tr><th>水温</th><td>--</td></tr>"
                                "<tr><th>TDS</th><td>--</td></tr>"
                                "<tr><th>样本数</th><td>0</td></tr>");
    } else {
        sendFmt("<tr><th>水温平均</th><td>%s</td></tr>"
                "<tr><th>水温范围</th><td>%s - %s</td></tr>"
                "<tr><th>TDS 平均</th><td>%s</td></tr>"
                "<tr><th>TDS 范围</th><td>",
                temperatureAvg,
                temperatureMin,
                temperatureMax,
                tdsAvg);
        if ((record.sensorFlags & kWaterSensorFlagTdsInvalid) != 0) {
            Esp32BaseWeb::sendChunk("--");
        } else {
            sendFmt("%u ppm - %u ppm",
                    static_cast<unsigned>(record.tdsMinPpm),
                    static_cast<unsigned>(record.tdsMaxPpm));
        }
        sendFmt("</td></tr>"
                "<tr><th>TDS 电压平均</th><td>%u mV</td></tr>"
                "<tr><th>样本数</th><td>%u</td></tr>"
                "<tr><th>TDS 校准</th><td>%s</td></tr>"
                "<tr><th>TDS 校准版本</th><td>%u</td></tr>"
                "<tr><th>温度补偿</th><td>%s</td></tr>"
                "<tr><th>25C 回退</th><td>%s</td></tr>"
                "<tr><th>传感器标志</th><td>0x%04x</td></tr>",
                static_cast<unsigned>(record.tdsVoltageAvgMv),
                static_cast<unsigned>(record.sensorSampleCount),
                recordTdsCalibrationText(record),
                static_cast<unsigned>(record.tdsCalibrationRevisionAtRun),
                record.tdsTemperatureCompensatedAtRun ? "开启" : "关闭",
                record.tdsTempFallback25CAtRun ? "是" : "否",
                static_cast<unsigned>(record.sensorFlags));
    }
    Esp32BaseWeb::sendChunk("</table></section>");
}

const char* tdsCalibrationModeText(TdsCalibrationMode mode) {
    switch (mode) {
        case TdsCalibrationMode::None:
            return "未校准";
        case TdsCalibrationMode::SinglePoint:
            return "单点";
        case TdsCalibrationMode::TwoPoint:
            return "两点";
        case TdsCalibrationMode::MultiPoint:
            return "多点";
    }
    return "未知";
}

void sendCalibrationCenterFlowCard(const AppSnapshot& snapshot) {
    MeteringSchemeRecord active{};
    const bool activeReady = activeMeteringSchemeForWeb(active);
    const MeteringParameters params = activeReady ? active.params : snapshot.meteringParams;
    const bool sessionActive = !calibrationSessionInactive(snapshot.calibrationStatus);
    Esp32BaseWeb::sendChunk("<section class='panel calibration-center-card'><div class='panel-head'><h3>当前计量参数</h3>");
    if (sessionActive) {
        sendFmt("<span class='status-pill status-warn'>%s</span></div>",
                calibrationSessionStatusText(snapshot.calibrationStatus));
    } else {
        sendFmt("<span class='status-pill status-ok'>%s</span></div>",
                activeReady && active.name[0] ? active.name : "默认参数");
    }
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    sendFmt("<div><span>启动脉冲</span><strong>%lu P</strong></div>",
            static_cast<unsigned long>(params.startupPulseCount));
    sendFmt("<div><span>启动水量</span><strong>%lu ml</strong></div>",
            static_cast<unsigned long>(params.startupVolumeMl));
    sendFmt("<div><span>启动时长</span><strong>%lu ms</strong></div>",
            static_cast<unsigned long>(params.startupDurationMs));
    sendFmt("<div><span>稳态 P/L</span><strong>%lu</strong></div>",
            static_cast<unsigned long>(params.stablePulsePerLiter));
    sendFmt("<div><span>稳态流速</span><strong>%lu ml/min</strong></div>",
            static_cast<unsigned long>(params.stableFlowMlPerMin));
    if (sessionActive) {
        sendFmt("<div><span>本次校准</span><strong>%u 样本</strong></div>",
                static_cast<unsigned>(snapshot.calibrationValidSampleCount));
    }
    Esp32BaseWeb::sendChunk("</div><div class='form-actions'>"
                            "<a class='btn-link primary' href='/faucet/calibration/flow'>");
    Esp32BaseWeb::sendChunk(sessionActive ? "继续流量计校准" : "进入流量计校准");
    Esp32BaseWeb::sendChunk("</a></div></section>");
}

void sendCalibrationCenterTemperatureCard(const AppSnapshot& snapshot, const SystemConfig& config) {
    char temperatureText[24]{};
    formatSensorTemperature(snapshot.sensors.temperatureCentiC, temperatureText, sizeof(temperatureText));
    const bool enabled = config.temperatureEnabled && config.temperatureKind == TemperatureKind::Ntc50kB3950;
    Esp32BaseWeb::sendChunk("<section class='panel calibration-center-card'><div class='panel-head'><h3>温度校准</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            enabled ? (config.temperatureCalibrated ? "status-ok" : "status-warn") : "status-muted",
            enabled ? (config.temperatureCalibrated ? "已校准" : "未校准") : "未启用");
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    sendFmt("<div><span>当前水温</span><strong>%s</strong></div>", enabled ? temperatureText : "未启用");
    sendFmt("<div><span>当前偏移</span><strong>%ld.%02ld C</strong></div>",
            static_cast<long>(config.temperatureOffsetCentiC / 100),
            static_cast<long>(std::abs(static_cast<int>(config.temperatureOffsetCentiC % 100))));
    Esp32BaseWeb::sendChunk("</div><p><a class='btn-link primary' href='/faucet/calibration?view=temperature'>进入温度校准</a></p></section>");
}

void sendCalibrationCenterTdsCard(const AppSnapshot& snapshot, const SystemConfig& config) {
    char tdsText[24]{};
    char voltageText[24]{};
    char tempText[24]{};
    formatSensorIntegerValue(snapshot.sensors.tdsPpm, "ppm", tdsText, sizeof(tdsText));
    formatSensorIntegerValue(snapshot.sensors.tdsVoltageMv, "mV", voltageText, sizeof(voltageText));
    formatSensorTemperature(snapshot.sensors.temperatureCentiC, tempText, sizeof(tempText));
    const bool enabled = config.tdsEnabled && config.tdsKind == TdsKind::AnalogTdsAo;
    Esp32BaseWeb::sendChunk("<section class='panel calibration-center-card'><div class='panel-head'><h3>水质校准</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            enabled ? (config.tdsCalibrated ? "status-ok" : "status-warn") : "status-muted",
            enabled ? (config.tdsCalibrated ? tdsCalibrationModeText(config.tdsCalibrationMode) : "未校准") : "未启用");
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    sendFmt("<div><span>当前 TDS</span><strong>%s</strong></div>", enabled ? tdsText : "未启用");
    sendFmt("<div><span>采样电压</span><strong>%s</strong></div>", enabled ? voltageText : "-");
    sendFmt("<div><span>水温补偿</span><strong>%s</strong></div>",
            config.tdsTemperatureCompensationEnabled ? tempText : "关闭");
    sendFmt("<div><span>scale</span><strong>%.3f</strong></div>", static_cast<double>(config.tdsScale));
    sendFmt("<div><span>offset</span><strong>%d ppm</strong></div>", static_cast<int>(config.tdsOffsetPpm));
    Esp32BaseWeb::sendChunk("</div><p><a class='btn-link primary' href='/faucet/calibration?view=tds'>进入水质校准</a></p></section>");
}

void sendTdsCalibrationPointsTable(const TdsCalibrationSessionSnapshot& session) {
    Esp32BaseWeb::sendChunk("<h4>已保存校准点</h4><table><tr><th>#</th><th>参考 ppm</th><th>原始 ppm</th><th>采样电压</th><th>操作</th></tr>");
    if (session.pointCount == 0) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='5'>暂无校准点</td></tr>");
    }
    for (std::uint8_t i = 0; i < session.pointCount && i < kTdsCalibrationMaxPoints; ++i) {
        const TdsCalibrationPointSnapshot& point = session.points[i];
        sendFmt("<tr><td>%u</td><td>%u</td><td>%u</td><td>%u mV</td><td>"
                "<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                "<input type='hidden' name='action' value='tds_remove_point'>"
                "<input type='hidden' name='index' value='%u'>"
                "<input class='secondary' type='submit' value='删除'></form></td></tr>",
                static_cast<unsigned>(i + 1U),
                static_cast<unsigned>(point.referencePpm),
                static_cast<unsigned>(point.rawPpm),
                static_cast<unsigned>(point.voltageMv),
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>");
}

void handleTemperatureCalibrationPage() {
    const AppSnapshot snapshot = g_context.app->snapshot();
    const SystemConfig& config = *g_context.config;
    char rawText[24]{};
    char calibratedText[24]{};
    formatSensorTemperature(snapshot.sensors.temperatureRawCentiC, rawText, sizeof(rawText));
    formatSensorTemperature(snapshot.sensors.temperatureCentiC, calibratedText, sizeof(calibratedText));
    const bool enabled = config.temperatureEnabled && config.temperatureKind == TemperatureKind::Ntc50kB3950;
    Esp32BaseWeb::sendHeader("温度校准");
    Esp32BaseWeb::sendChunk("<h2>温度校准</h2><p><a class='btn-link' href='/faucet/calibration'>返回校准中心</a></p>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<section class='panel temperature-calibration-panel'><div class='panel-head'><h3>当前温度状态</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            enabled ? (config.temperatureCalibrated ? "status-ok" : "status-warn") : "status-muted",
            enabled ? (config.temperatureCalibrated ? "已校准" : "未校准") : "未启用");
    Esp32BaseWeb::sendChunk("<div class='temperature-calibration-summary'>");
    sendFmt("<div><span>原始温度</span><strong>%s</strong></div>", enabled ? rawText : "未启用");
    sendFmt("<div><span>当前水温</span><strong>%s</strong></div>", enabled ? calibratedText : "未启用");
    sendFmt("<div><span>当前偏移</span><strong>%ld.%02ld C</strong></div>",
            static_cast<long>(config.temperatureOffsetCentiC / 100),
            static_cast<long>(std::abs(static_cast<int>(config.temperatureOffsetCentiC % 100))));
    Esp32BaseWeb::sendChunk("</div><form class='temperature-calibration-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='temperature_save'>"
                            "<label class='compact-field'><span>温度计读数</span><span class='estimator-input-row'>"
                            "<input name='referenceC' type='number' step='0.1' min='0' max='90' required>"
                            "<span class='unit-label'>C</span></span></label>"
                            "<p class='hint'>预览偏移只作辅助，保存时以后端当前原始温度重新计算。</p><div class='form-actions'>");
    sendFmt("<input class='primary' type='submit' value='保存温度校准'%s>",
            enabled && snapshot.sensors.temperatureRawCentiC.valid ? "" : " disabled");
    Esp32BaseWeb::sendChunk("<a class='btn-link' href='/faucet/calibration'>取消</a></div></form></section>");
    sendCalibrationPageScript();
    sendPageEnd();
}

void handleTdsCalibrationPage() {
    const AppSnapshot snapshot = g_context.app->snapshot();
    const SystemConfig& config = *g_context.config;
    const TdsCalibrationSessionSnapshot session =
        g_context.app ? g_context.app->tdsCalibrationSnapshot() : TdsCalibrationSessionSnapshot{};
    char tdsText[24]{};
    char voltageText[24]{};
    char tempText[24]{};
    formatSensorIntegerValue(snapshot.sensors.tdsPpm, "ppm", tdsText, sizeof(tdsText));
    formatSensorIntegerValue(snapshot.sensors.tdsVoltageMv, "mV", voltageText, sizeof(voltageText));
    formatSensorTemperature(snapshot.sensors.temperatureCentiC, tempText, sizeof(tempText));
    const bool enabled = config.tdsEnabled && config.tdsKind == TdsKind::AnalogTdsAo;
    const bool canWrite = enabled && !waterTaskActive();

    Esp32BaseWeb::sendHeader("水质校准");
    Esp32BaseWeb::sendChunk("<h2>水质校准</h2><p><a class='btn-link' href='/faucet/calibration'>返回校准中心</a></p>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<section class='panel tds-calibration-panel'><div class='panel-head'><h3>当前水质状态</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            enabled ? (config.tdsCalibrated ? "status-ok" : "status-warn") : "status-muted",
            enabled ? (config.tdsCalibrated ? tdsCalibrationModeText(config.tdsCalibrationMode) : "未校准") : "未启用");
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    sendFmt("<div><span>当前 TDS</span><strong>%s</strong></div>", enabled ? tdsText : "未启用");
    sendFmt("<div><span>采样电压</span><strong>%s</strong></div>", enabled ? voltageText : "-");
    sendFmt("<div><span>水温补偿</span><strong>%s</strong></div>",
            config.tdsTemperatureCompensationEnabled ? tempText : "关闭");
    sendFmt("<div><span>scale</span><strong>%.3f</strong></div>", static_cast<double>(config.tdsScale));
    sendFmt("<div><span>offset</span><strong>%d ppm</strong></div>", static_cast<int>(config.tdsOffsetPpm));
    Esp32BaseWeb::sendChunk("</div></section><section class='panel tds-point-session'");
    if (session.samplingActive && !session.readyToSave) {
        Esp32BaseWeb::sendChunk(" data-tds-calibration-refresh");
    }
    Esp32BaseWeb::sendChunk("><div class='panel-head'><h3>本次校准</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            session.samplingActive ? "status-warn" : (session.sessionActive ? "status-ok" : "status-muted"),
            session.samplingActive ? "采样中" : (session.sessionActive ? "已开始" : "未开始"));
    Esp32BaseWeb::sendChunk("<div class='tds-step-card'>");
    if (!enabled) {
        Esp32BaseWeb::sendChunk("<p class='hint'>TDS 模拟传感器未启用，不能进行水质校准。</p>");
    } else if (!session.sessionActive) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='tds_start_session'>"
                                "<div class='form-actions'>");
        sendFmt("<input class='primary' type='submit' value='开始校准'%s>",
                canWrite ? "" : " disabled");
        Esp32BaseWeb::sendChunk("</div></form>");
    } else if (session.samplingActive && !session.readyToSave) {
        sendFmt("<p><strong>参考 %u ppm</strong>，已采样 %u 次，原始均值 %u ppm。</p>",
                static_cast<unsigned>(session.referencePpm),
                static_cast<unsigned>(session.sampleCount),
                static_cast<unsigned>(session.rawAvgPpm));
        Esp32BaseWeb::sendChunk("<p class='hint'>正在等待读数稳定，稳定后页面会显示保存按钮。</p>"
                                "<div class='form-actions'><input class='secondary' type='submit' value='正在采集' disabled></div>");
    } else if (session.readyToSave) {
        sendFmt("<p><strong>参考 %u ppm</strong>，已采样 %u 次，原始均值 %u ppm。</p>",
                static_cast<unsigned>(session.referencePpm),
                static_cast<unsigned>(session.sampleCount),
                static_cast<unsigned>(session.rawAvgPpm));
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='tds_save_point'>"
                                "<div class='form-actions'>");
        sendFmt("<input class='primary' type='submit' value='保存这个校准点'%s>",
                canWrite ? "" : " disabled");
        Esp32BaseWeb::sendChunk("</div></form>");
    } else if (session.full) {
        Esp32BaseWeb::sendChunk("<p class='hint'>本次校准点已满，可以使用当前结果或删除一个校准点后重采。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<form class='tds-capture-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='tds_start_point'>"
                                "<label class='compact-field'><span>参考 TDS(ppm)</span><span class='estimator-input-row'>"
                                "<input name='referencePpm' type='number' min='0' max='2000' step='1' required>"
                                "<span class='unit-label'>ppm</span></span></label>"
                                "<div class='form-actions'>");
        sendFmt("<input class='primary' type='submit' value='开始采集校准点'%s>",
                canWrite ? "" : " disabled");
        Esp32BaseWeb::sendChunk("</div></form>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    sendTdsCalibrationPointsTable(session);
    Esp32BaseWeb::sendChunk("<div class='tds-workflow-card'><h4>自动生成结果</h4>");
    if (session.candidateReady) {
        sendFmt("<p>点数 %u，参考跨度 %u ppm，原始跨度 %u ppm，scale %.3f，offset %d ppm。</p>",
                static_cast<unsigned>(session.pointCount),
                static_cast<unsigned>(session.referenceSpanPpm),
                static_cast<unsigned>(session.rawSpanPpm),
                static_cast<double>(session.candidateScale),
                static_cast<int>(session.candidateOffsetPpm));
    } else {
        Esp32BaseWeb::sendChunk("<p>保存至少一个稳定校准点后自动生成结果。</p>");
    }
    Esp32BaseWeb::sendChunk("<div class='form-actions'><form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='tds_apply_session'>");
    sendFmt("<input class='primary' type='submit' value='使用这组参数'%s></form>",
            canWrite && session.candidateReady ? "" : " disabled");
    Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='tds_discard_session'>");
    sendFmt("<input class='secondary' type='submit' value='丢弃本次校准'%s></form></div></div></section>",
            canWrite && session.sessionActive ? "" : " disabled");
    sendCalibrationPageScript();
    sendPageEnd();
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
        const bool restored = std::strcmp(text, "restored") == 0;
        const bool sampleRemoved = std::strcmp(text, "sample_removed") == 0;
        const bool tdsStarted = std::strcmp(text, "tds_started") == 0;
        const bool tdsPointStarted = std::strcmp(text, "tds_point_started") == 0;
        const bool tdsPointSaved = std::strcmp(text, "tds_point_saved") == 0;
        const bool tdsSaved = std::strcmp(text, "tds_saved") == 0;
        const bool tdsDiscarded = std::strcmp(text, "tds_discarded") == 0;
        Esp32BaseWeb::sendChunk("<p class='ok'>");
        Esp32BaseWeb::sendChunk(actualOnly   ? "校准已保存。"
                                : generated  ? "参数已生成。"
                                : restored           ? "已恢复上一套参数。"
                                : sampleRemoved      ? "样本已移除。"
                                : tdsStarted         ? "水质校准已开始。"
                                : tdsPointStarted    ? "校准点采集已开始，页面会自动刷新到可保存状态。"
                                : tdsPointSaved      ? "校准点已保存。"
                                : tdsSaved           ? "水质校准参数已应用。"
                                : tdsDiscarded       ? "本次水质校准已丢弃。"
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
    } else if (std::strcmp(text, "invalid_action") == 0) {
        message = "操作无效，请刷新页面后重试。";
    } else if (std::strcmp(text, "invalid_state") == 0) {
        message = "现在不允许执行这个操作，请刷新页面后按当前步骤继续。";
    } else if (std::strcmp(text, "calibration_storage_unavailable") == 0) {
        message = "校准存储未就绪，请检查设备存储空间或重启后再试。";
    } else if (std::strcmp(text, "invalid_date") == 0) {
        message = "日期格式无效，请重新选择日期。";
    } else if (std::strcmp(text, "save_failed") == 0) {
        message = "保存失败，请稍后重试。";
    } else if (std::strcmp(text, "metering_storage_unavailable") == 0) {
        message = "计量方案存储不可用；请检查存储状态，系统不会自动删除原文件。";
    } else if (std::strcmp(text, "no_calibration_record") == 0) {
        message = "最新记录没有可用的有效脉冲，不能用于校准。";
    } else if (std::strcmp(text, "calibration_unchanged") == 0) {
        message = "实际出水量未变化，未保存校准。";
    } else if (std::strcmp(text, "calibration_drift") == 0) {
        message = "新系数和旧系数偏差过大，请重新接水测量。";
    } else if (std::strcmp(text, "sample_not_enough") == 0) {
        message = "可用样本不足，至少需要两条有效样本。";
    } else if (std::strcmp(text, "no_generated_result") == 0) {
        message = "还没有可使用的参数，请先完成至少两条有效校准样本。";
    } else if (std::strcmp(text, "no_previous") == 0) {
        message = "没有可恢复的上一套参数。";
    }
    Esp32BaseWeb::sendChunk("<p class='err'>");
    Esp32BaseWeb::sendChunk(message);
    Esp32BaseWeb::sendChunk("</p>");
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

void sendMachineTaskCard(const char* valueId, const char* metaId, const char* label, const char* value, const char* meta) {
    sendFmt("<div class='machine-task-card'><span>%s</span><strong id='%s'>%s</strong><small id='%s'%s>%s</small></div>",
            label,
            valueId,
            value,
            metaId,
            (meta && meta[0]) ? "" : " style='display:none'",
            meta);
}

void sendMachineTaskCard(const char* wrapperId,
                         const char* valueId,
                         const char* metaId,
                         const char* label,
                         const char* value,
                         const char* meta,
                         bool hidden) {
    sendFmt("<div id='%s' class='machine-task-card'%s><span>%s</span><strong id='%s'>%s</strong><small id='%s'%s>%s</small></div>",
            wrapperId,
            hidden ? " style='display:none'" : "",
            label,
            valueId,
            value,
            metaId,
            (meta && meta[0]) ? "" : " style='display:none'",
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

void sendMachineStatusSensorItem(const char* valueId, const char* label, const char* value, const char* unit) {
    sendFmt("<span class='machine-status-item'><span>%s</span><strong id='%s'>%s</strong><small class='sensor-unit'>%s</small></span>",
            label,
            valueId,
            value,
            unit);
}

void formatPresetTarget(const PresetConfig& preset, char* out, std::size_t len) {
    if (preset.type == PresetType::Time) {
        formatSecondsValue(preset.value, out, len);
        return;
    }
    formatLiters(preset.value, out, len);
}

void formatFlowLitersPerMinCompact(std::uint32_t flowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (flowMlPerMin == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%03luL/min",
                  static_cast<unsigned long>(flowMlPerMin / 1000U),
                  static_cast<unsigned long>(flowMlPerMin % 1000U));
}

void formatPresetEstimate(const PresetConfig& preset,
                          const MeteringParameters& params,
                          std::uint32_t durationSec,
                          std::uint32_t timeVolumeMl,
                          std::uint32_t timePulseCount,
                          float stablePulsePerSec,
                          const char* reason,
                          char* out,
                          std::size_t len) {
    if (preset.type == PresetType::Time) {
        if (timeVolumeMl > 0 && timePulseCount > 0 && stablePulsePerSec > 0.0f) {
            char volume[24]{};
            char flow[24]{};
            formatLiters(timeVolumeMl, volume, sizeof(volume));
            formatFlowLitersPerMinCompact(params.stableFlowMlPerMin, flow, sizeof(flow));
            std::snprintf(out,
                          len,
                          "预计 %s · %luP · 稳态 %s",
                          volume,
                          static_cast<unsigned long>(timePulseCount),
                          flow);
        } else {
            std::snprintf(out, len, "%s", reason ? reason : "计量参数未就绪");
        }
        return;
    }
    const MeteringTargetEstimate estimate = meteringEstimateForTarget(params, preset.value);
    if (!estimate.valid) {
        std::snprintf(out, len, "%s", reason ? reason : "计量参数未就绪");
        return;
    }
    char duration[24]{};
    if (durationSec > 0) {
        formatSecondsValue(durationSec, duration, sizeof(duration));
        std::snprintf(out,
                      len,
                      "预计 %luP/L · %luP · 约 %s",
                      static_cast<unsigned long>(estimate.fullRunPulsePerLiter),
                      static_cast<unsigned long>(estimate.pulseCount),
                      duration);
    } else {
        std::snprintf(out,
                      len,
                      "预计 %luP/L · %luP",
                      static_cast<unsigned long>(estimate.fullRunPulsePerLiter),
                      static_cast<unsigned long>(estimate.pulseCount));
    }
}

std::uint32_t recordFlowMlPerMin(std::uint32_t volumeMl, std::uint32_t durationSec) {
    if (volumeMl == 0 || durationSec == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(volumeMl) * 60ULL + durationSec / 2ULL) /
                                      durationSec);
}

std::uint32_t stableFlowMlPerMin(float stablePulsePerSec, const MeteringParameters& params) {
    if (stablePulsePerSec <= 0.0f || params.stablePulsePerLiter == 0) {
        return 0;
    }
    const double mlPerMin =
        static_cast<double>(stablePulsePerSec) * 60000.0 / static_cast<double>(params.stablePulsePerLiter);
    return mlPerMin <= 0.0 ? 0 : static_cast<std::uint32_t>(mlPerMin + 0.5);
}

void formatFlowLitersPerMin(std::uint32_t flowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (flowMlPerMin == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%03lu L/min",
                  static_cast<unsigned long>(flowMlPerMin / 1000U),
                  static_cast<unsigned long>(flowMlPerMin % 1000U));
}

void formatFlowNumber(std::uint32_t flowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    const std::uint32_t centiLitersPerMin = (flowMlPerMin + 5U) / 10U;
    if (centiLitersPerMin == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%02lu",
                  static_cast<unsigned long>(centiLitersPerMin / 100U),
                  static_cast<unsigned long>(centiLitersPerMin % 100U));
}

void formatFlowMeta(std::uint32_t runAverageFlowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    char average[16]{};
    formatFlowNumber(runAverageFlowMlPerMin, average, sizeof(average));
    std::snprintf(out, len, "L/min · 本次平均 %s", average);
}

void sendNextPresetControl(const AppSnapshot& snapshot) {
    const SystemConfig& config = *g_context.config;
    const bool available = snapshot.water.selectedPreset < kPresetCount && config.presets[snapshot.water.selectedPreset].enabled;
    char target[24]{};
    char estimate[160]{};
    Esp32BaseWeb::sendChunk("<div id='nextPresetControl' class='next-preset-control'>"
                            "<button class='preset-step' type='button' aria-label='上一个预设' data-action='action=select_previous' onclick=\"faucetSelectPreset('select_previous')\">‹</button>"
                            "<div class='next-preset-copy'><span>下次预设</span><strong id='nextPresetLabel'>");
    if (available) {
        const PresetConfig& preset = config.presets[snapshot.water.selectedPreset];
        formatPresetTarget(preset, target, sizeof(target));
        sendFmt("P%u · ", static_cast<unsigned>(snapshot.water.selectedPreset + 1U));
        sendHtmlEscapedBounded(preset.name[0] ? preset.name : "未命名", sizeof(preset.name));
        sendFmt(" · %s", target);
        formatPresetEstimate(preset,
                             snapshot.meteringParams,
                             snapshot.selectedPresetEstimatedDurationSec,
                             snapshot.selectedPresetEstimatedVolumeMl,
                             snapshot.selectedPresetEstimatedPulseCount,
                             snapshot.selectedPresetStablePulsePerSec,
                             snapshot.selectedPresetEstimateReason,
                             estimate,
                             sizeof(estimate));
    } else {
        Esp32BaseWeb::sendChunk("无可用预设");
        std::snprintf(estimate, sizeof(estimate), "请先在预设页启用至少一项");
    }
    sendFmt("</strong><small id='nextPresetEstimate'%s>%s</small></div>"
            "<button class='preset-step' type='button' aria-label='下一个预设' data-action='action=select_next' onclick=\"faucetSelectPreset('select_next')\">›</button>"
            "</div>",
            estimate[0] ? "" : " style='display:none'",
            estimate);
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
    char targetMeta[96]{};
    if (snapshot.water.mode == WaterMode::Volume) {
        const MeteringTargetEstimate targetEstimate =
            meteringEstimateForTarget(snapshot.meteringParams, snapshot.water.targetValue);
        char durationText[24]{};
        if (!targetEstimate.valid) {
            std::snprintf(targetMeta,
                          sizeof(targetMeta),
                          "%s",
                          snapshot.targetEstimateReason ? snapshot.targetEstimateReason : "计量参数未就绪");
        } else if (snapshot.targetEstimatedDurationSec > 0) {
            formatSecondsValue(snapshot.targetEstimatedDurationSec, durationText, sizeof(durationText));
            std::snprintf(targetMeta,
                          sizeof(targetMeta),
                          "预计约 %s",
                          durationText);
        } else {
            std::snprintf(targetMeta, sizeof(targetMeta), "计量参数未就绪");
        }
    } else if (snapshot.targetEstimatedVolumeMl > 0 && snapshot.targetEstimatedPulseCount > 0 &&
               snapshot.targetStablePulsePerSec > 0.0f) {
        char estimatedVolume[24]{};
        formatLiters(snapshot.targetEstimatedVolumeMl, estimatedVolume, sizeof(estimatedVolume));
        std::snprintf(targetMeta, sizeof(targetMeta), "预计 %s", estimatedVolume);
    } else {
        std::snprintf(targetMeta,
                      sizeof(targetMeta),
                      "%s",
                      snapshot.targetEstimateReason ? snapshot.targetEstimateReason : "计量参数未就绪");
    }
    char outputMeta[32]{};
    std::snprintf(outputMeta, sizeof(outputMeta), "已运行 %s", elapsedText);
    char remainingMeta[24]{};
    std::snprintf(remainingMeta, sizeof(remainingMeta), "完成 %lu%%", static_cast<unsigned long>(progressPercent));
    char currentFlow[24]{};
    formatFlowNumber(snapshot.currentFlowMlPerMin, currentFlow, sizeof(currentFlow));
    char currentFlowMeta[40]{};
    formatFlowMeta(snapshot.runAverageFlowMlPerMin, currentFlowMeta, sizeof(currentFlowMeta));
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
    char currentTemperature[24]{};
    if (snapshot.temperatureSensorEnabled && snapshot.sensors.temperatureCentiC.valid) {
        formatSensorTemperatureNumber(snapshot.sensors.temperatureCentiC, currentTemperature, sizeof(currentTemperature));
    } else {
        std::snprintf(currentTemperature, sizeof(currentTemperature), "--");
    }
    char currentTds[24]{};
    if (snapshot.tdsSensorEnabled && snapshot.sensors.tdsPpm.valid) {
        formatSensorIntegerNumber(snapshot.sensors.tdsPpm, currentTds, sizeof(currentTds));
    } else {
        std::snprintf(currentTds, sizeof(currentTds), "--");
    }

    const bool showResult = snapshot.water.state == WaterState::Error || snapshot.localMode == LocalUiMode::Result;
    const bool showRunningNote = snapshot.water.state == WaterState::Running;
    const char* machineLayoutClass = shouldShowProgress ? "machine-main" : "machine-main compact";
    sendFmt("<h2>机器状态</h2><section class='panel machine-status'><div class='%s'><div class='machine-hero'>"
            "<div class='machine-hero-head'><strong>",
            machineLayoutClass);
    Esp32BaseWeb::sendChunk("<span id='machineState'>");
    Esp32BaseWeb::sendChunk(stateText(snapshot.water.state));
    Esp32BaseWeb::sendChunk("</span></strong><div class='machine-context'>");
    sendNextPresetControl(snapshot);
    Esp32BaseWeb::sendChunk("</div></div>");
    sendFmt("<div id='machineProgress' class='machine-progress'%s><div class='machine-progress-head'><span>出水进度</span><strong id='machineProgressText'>%s</strong></div>"
            "<div class='progress'><span id='machineProgressBar' style='width:%lu%%'></span></div>"
            "<p id='machineStatusNote' class='machine-alert machine-progress-alert'%s>%s</p></div>",
            shouldShowProgress ? "" : " style='display:none'",
            progressText,
            static_cast<unsigned long>(progressPercent),
            showRunningNote ? "" : " style='display:none'",
            machineStatusNote(snapshot));
    sendFmt("<span class='machine-screen-footer'><span>屏幕</span><span id='screenStatus'>%s</span></span>",
            screenOn ? "亮屏" : "休眠");
    Esp32BaseWeb::sendChunk("</div><div class='machine-overview'><div class='machine-task-grid'>");
    sendMachineTaskCard("targetValue", "targetMeta", "目标", targetValue, targetMeta);
    sendMachineTaskCard("outputValue", "outputMeta", "已出水", outValue, outputMeta);
    sendMachineTaskCard("remainingValue", "remainingMeta", "剩余", remainingValue, remainingMeta);
    sendMachineTaskCard("currentFlowCard", "currentFlowValue", "currentFlowMeta", "流速", currentFlow, currentFlowMeta, false);
    Esp32BaseWeb::sendChunk("</div><div class='machine-status-strip'>");
    sendMachineStatusItem("valveStatus", "阀门", snapshot.water.valveOpen ? "开" : "关");
    sendMachineStatusSensorItem("temperatureStatus", "水温", currentTemperature, "C");
    sendMachineStatusSensorItem("tdsStatus", "TDS", currentTds, "ppm");
    sendMachineStatusItemNote("valvePwmDuty", "valvePwmNote", "PWM", valvePwmDuty, valvePwmNote);
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
    std::unique_ptr<WaterRecord[]> records(new (std::nothrow) WaterRecord[kDefaultRecordPageSize]{});
    if (!records) {
        return overview;
    }
    const std::size_t total = g_context.records->count();
    bool stopAfterPage = false;
    for (std::size_t offset = 0; offset < total && !stopAfterPage; offset += kDefaultRecordPageSize) {
        const std::size_t page = offset / kDefaultRecordPageSize;
        const std::size_t read =
            g_context.records->readPage(page, kDefaultRecordPageSize, records.get(), kDefaultRecordPageSize);
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
        Esp32BaseWeb::sendChunk("<table class='today-record-table'><tr><th>时间</th><th>用时</th><th>实际出水</th><th>水温</th><th>TDS</th><th>预设目标</th><th>结果</th></tr>");
        for (std::size_t i = 0; i < overview.latestCount; ++i) {
            char startTime[12]{};
            char duration[24]{};
            char volume[24]{};
            char temperature[24]{};
            char tds[24]{};
            char preset[48]{};
            formatRecordTimeOfDay(overview.latest[i].startTime, startTime, sizeof(startTime));
            formatSecondsValue(overview.latest[i].durationSec, duration, sizeof(duration));
            formatLiters(overview.latest[i].volumeMl, volume, sizeof(volume));
            formatWaterRecordTemperature(overview.latest[i], temperature, sizeof(temperature));
            formatWaterRecordTds(overview.latest[i], tds, sizeof(tds));
            formatRecordPresetLabel(overview.latest[i], preset, sizeof(preset));
            sendFmt("<tr><td>%s</td><td class='record-duration'>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                    "<td><span class='status-pill %s'>%s</span></td></tr>",
                    startTime,
                    duration,
                    volume,
                    temperature,
                    tds,
                    preset,
                    resultStatusClass(overview.latest[i].result),
                    resultText(overview.latest[i].result));
        }
        Esp32BaseWeb::sendChunk("</table>");
    }
    Esp32BaseWeb::sendChunk("</section></div></section>");
}

void sendTodayOverviewPlaceholder(std::uint32_t fallbackTodayMl) {
    char today[24]{};
    formatLiters(fallbackTodayMl, today, sizeof(today));
    Esp32BaseWeb::sendChunk("<section id='todayOverview' class='today-overview'><h2>今日概览</h2>"
                            "<div class='today-layout'><section class='panel today-summary-card'>"
                            "<span class='today-summary-label'>今日总量</span><strong class='today-total-main'>");
    Esp32BaseWeb::sendChunk(today);
    Esp32BaseWeb::sendChunk("</strong><span class='today-total-meta today-meta-line'>"
                            "<span class='today-meta-item'>接水 <span class='today-meta-value'>-- 次</span></span>"
                            "<span class='today-meta-item'>用时 <span class='today-meta-value'>--</span></span>"
                            "</span></section><section class='panel today-records'>"
                            "<p class='hint'>正在加载今天的记录。</p></section></div></section>");
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
    AppSnapshot snapshot = g_context.app->snapshot();
    if (g_context.currentRuntimeDiagnostics) {
        const FaucetRuntimeDiagnostics diagnostics = g_context.currentRuntimeDiagnostics();
        snapshot.maxLoopIntervalUs = diagnostics.maxLoopIntervalUs;
        snapshot.maxAppTickUs = diagnostics.maxAppTickUs;
        snapshot.maxBaseHandleUs = diagnostics.maxBaseHandleUs;
    }
    applyTargetDurationEstimate(snapshot, false);
    const FaucetDisplayStatus displayStatus =
        g_context.currentDisplayStatus
            ? g_context.currentDisplayStatus()
            : FaucetDisplayStatus{false};
    sendMachineStatusCard(snapshot, displayStatus.screenOn);
    sendTodayOverviewPlaceholder(snapshot.statistics.todayMl);
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

void sendStatsReportPanel() {
    const std::uint32_t now = g_context.nowSeconds();
    std::unique_ptr<WaterUsageSummary> summaryStorage(new (std::nothrow) WaterUsageSummary);
    if (!summaryStorage ||
        !aggregateWaterRecordsInto(*g_context.records, now, kChartDays, false, *summaryStorage)) {
        Esp32BaseWeb::sendChunk("<section id='stats-report' class='stats-report'><p class='warn'>统计内存不足，请稍后重试。</p></section>");
        return;
    }
    const WaterUsageSummary& summary = *summaryStorage;
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
    formatLiters(snapshot.statistics.todayMl, today, sizeof(today));
    formatLiters(snapshot.statistics.monthMl, month, sizeof(month));
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
    Esp32BaseWeb::sendChunk("<section id='stats-report' class='stats-report'>");
    if (summary.unknownCount > 0) {
        sendFmt("<p class='warn'>含 %lu 条无时间记录，未纳入按日期统计。</p>",
                static_cast<unsigned long>(summary.unknownCount));
    }
    Esp32BaseWeb::sendChunk("<div class='metric-grid'>");
    sendStatsMetricCard("今日", today, todayMeta);
    char monthTitle[48]{};
    std::snprintf(monthTitle, sizeof(monthTitle), "本月%s%s", monthRange[0] ? " " : "", monthRange);
    sendStatsMetricCard(monthTitle, month, monthMeta);
    sendStatsMetricCard("过去 30 天日均", average30, average30Meta);
    sendStatsMetricCard("总累计", total, totalMeta);
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::sendChunk("<section class='panel usage-panel'><h3>按预设分布</h3>"
                            "<table><tr><th>预设</th><th>次数</th><th>出水量</th></tr>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (summary.presetCounts[i].count == 0 && !g_context.config->presets[i].enabled) {
            continue;
        }
        char volume[24]{};
        formatLiters(summary.presetCounts[i].volumeMl, volume, sizeof(volume));
        sendFmt("<tr><td>P%u · ", static_cast<unsigned>(i + 1U));
        sendHtmlEscapedBounded(g_context.config->presets[i].name, sizeof(g_context.config->presets[i].name));
        sendFmt("</td><td>%lu 次</td><td>%s</td></tr>",
                static_cast<unsigned long>(summary.presetCounts[i].count),
                volume);
    }
    Esp32BaseWeb::sendChunk("</table></section>");
    Esp32BaseWeb::sendChunk("</section>");
}

void handleStatsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }
    Esp32BaseWeb::sendHeader("用水统计");
    Esp32BaseWeb::sendChunk("<h2>统计</h2>");
    sendStatsReportPanel();
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
    const WaterRecordFileStatus recordStatus = g_context.records->status();
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
    if (recordStatus != WaterRecordFileStatus::Ready) {
        sendFmt("<p class='%s'>%s</p>",
                ready ? "warn" : "err",
                waterRecordFileStatusMessage(recordStatus));
    }
    if (!ready) {
        Esp32BaseWeb::sendChunk("<p class='err'>当前没有可读取的临时记录。</p>");
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
    Esp32BaseWeb::sendChunk("<table><tr><th>时间</th><th>模式</th><th>目标</th><th>出水</th>"
                            "<th>用时</th><th>流速</th><th>水温</th><th>TDS</th><th>结果</th><th>操作</th></tr>");
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
        const std::uint32_t displayVolumeMl = calibrated ? calibration.actualMl : records[i].volumeMl;
        char recordFlow[24]{};
        const std::uint32_t averageFlow = recordFlowMlPerMin(displayVolumeMl, records[i].durationSec);
        formatFlowLitersPerMin(averageFlow, recordFlow, sizeof(recordFlow));
        char temperature[24]{};
        char tds[24]{};
        formatWaterRecordTemperature(records[i], temperature, sizeof(temperature));
        formatWaterRecordTds(records[i], tds, sizeof(tds));
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
        sendFmt("</td><td>%u s</td><td class='record-flow-cell'>",
                static_cast<unsigned>(records[i].durationSec));
        if (averageFlow > 0) {
            Esp32BaseWeb::sendChunk(recordFlow);
        } else {
            Esp32BaseWeb::sendChunk("<span class='muted'>-</span>");
        }
        sendFmt("</td><td>%s</td><td>%s", temperature, tds);
        sendFmt("</td><td><span class='status-pill %s'>%s</span>",
                resultStatusClass(records[i].result),
                resultText(records[i].result));
        Esp32BaseWeb::sendChunk("</td><td><div class='row-actions'>");
        sendFmt("<a class='btn-link' href='/faucet/records/detail?start=%lu&boot=%lu&volume=%lu&target=%lu&pulses=%lu&rejected=%lu&duration=%lu&mode=%u&result=%u&preset=%u&tempAvg=%d&tempMin=%d&tempMax=%d&tdsAvg=%u&tdsMin=%u&tdsMax=%u&tdsMv=%u&sensorSamples=%u&sensorFlags=%u&tdsRev=%u&tdsMode=%u&tdsCal=%u&tdsComp=%u&tdsFallback=%u&scheme=%lu'>详情</a>",
                static_cast<unsigned long>(records[i].startTime),
                static_cast<unsigned long>(waterRecordBootId(records[i])),
                static_cast<unsigned long>(records[i].volumeMl),
                static_cast<unsigned long>(records[i].targetValue),
                static_cast<unsigned long>(records[i].pulseCount),
                static_cast<unsigned long>(records[i].rejectedPulseCount),
                static_cast<unsigned long>(records[i].durationSec),
                static_cast<unsigned>(records[i].mode),
                static_cast<unsigned>(records[i].result),
                static_cast<unsigned>(records[i].selectedPreset),
                static_cast<int>(records[i].temperatureAvgCentiC),
                static_cast<int>(records[i].temperatureMinCentiC),
                static_cast<int>(records[i].temperatureMaxCentiC),
                static_cast<unsigned>(records[i].tdsAvgPpm),
                static_cast<unsigned>(records[i].tdsMinPpm),
                static_cast<unsigned>(records[i].tdsMaxPpm),
                static_cast<unsigned>(records[i].tdsVoltageAvgMv),
                static_cast<unsigned>(records[i].sensorSampleCount),
                static_cast<unsigned>(records[i].sensorFlags),
                static_cast<unsigned>(records[i].tdsCalibrationRevisionAtRun),
                static_cast<unsigned>(records[i].tdsCalibrationModeAtRun),
                static_cast<unsigned>(records[i].tdsCalibratedAtRun),
                static_cast<unsigned>(records[i].tdsTemperatureCompensatedAtRun),
                static_cast<unsigned>(records[i].tdsTempFallback25CAtRun),
                static_cast<unsigned long>(records[i].meteringSchemeId));
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    delete[] pageCalibrations;
    delete[] pageCalibrated;
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
    std::int16_t parsedI16 = 0;
    if (getParam("start", text, sizeof(text)) && parseU32(text, parsed)) {
        record.startTime = parsed;
    }
    if (getParam("boot", text, sizeof(text)) && parseU32(text, parsed)) {
        markWaterRecordBootId(record, parsed);
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
    if (getParam("tempAvg", text, sizeof(text)) && parseI16(text, parsedI16)) {
        record.temperatureAvgCentiC = parsedI16;
    }
    if (getParam("tempMin", text, sizeof(text)) && parseI16(text, parsedI16)) {
        record.temperatureMinCentiC = parsedI16;
    }
    if (getParam("tempMax", text, sizeof(text)) && parseI16(text, parsedI16)) {
        record.temperatureMaxCentiC = parsedI16;
    }
    if (getParam("tdsAvg", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsAvgPpm = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("tdsMin", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsMinPpm = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("tdsMax", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsMaxPpm = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("tdsMv", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsVoltageAvgMv = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("sensorSamples", text, sizeof(text)) && parseU32(text, parsed)) {
        record.sensorSampleCount = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("sensorFlags", text, sizeof(text)) && parseU32(text, parsed)) {
        record.sensorFlags = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("tdsRev", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsCalibrationRevisionAtRun = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("tdsMode", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsCalibrationModeAtRun = static_cast<std::uint8_t>(parsed);
    }
    if (getParam("tdsCal", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsCalibratedAtRun = static_cast<std::uint8_t>(parsed);
    }
    if (getParam("tdsComp", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsTemperatureCompensatedAtRun = static_cast<std::uint8_t>(parsed);
    }
    if (getParam("tdsFallback", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsTempFallback25CAtRun = static_cast<std::uint8_t>(parsed);
    }
    if (getParam("scheme", text, sizeof(text)) && parseU32(text, parsed)) {
        record.meteringSchemeId = parsed;
    }
    char startTime[40]{};
    formatWaterRecordListTime(record, startTime, sizeof(startTime));
    char duration[24]{};
    formatSecondsValue(record.durationSec, duration, sizeof(duration));
    char averageFlow[24]{};
    formatFlowLitersPerMin(recordFlowMlPerMin(record.volumeMl, record.durationSec), averageFlow, sizeof(averageFlow));
    char task[64]{};
    formatRecordPresetLabel(record, task, sizeof(task));
    sendPageStart("接水详情");
    Esp32BaseWeb::sendChunk("<h2>接水详情</h2><div class='form-actions'><a class='btn-link' href='/faucet/records'>返回记录</a></div>"
                            "<div class='record-detail-grid'><section class='panel record-detail-card'><h3>出水结果</h3><table class='kv'>");
    sendFmt("<tr><th>开始时间</th><td>%s</td></tr>"
            "<tr><th>持续时间</th><td>%s</td></tr>"
            "<tr><th>结果</th><td><span class='status-pill %s'>%s</span></td></tr>",
            startTime,
            duration,
            resultStatusClass(record.result),
            resultText(record.result));
    sendFmt("</table></section>"
            "<section class='panel record-detail-card'><h3>任务与出水</h3><table class='kv'>"
            "<tr><th>任务</th><td>%s</td></tr>"
            "<tr><th>目标值</th><td>",
            task);
    sendTargetValue(record);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>估算出水</th><td>");
    sendLiters(record.volumeMl);
    sendFmt("</td></tr><tr><th>平均流速</th><td>%s</td></tr></table></section>", averageFlow);
    sendWaterRecordSensorRows(record);
    Esp32BaseWeb::sendChunk("<section class='panel record-detail-card'><h3>计量与脉冲</h3><table class='kv'>");
    sendFmt("<tr><th>有效脉冲</th><td>%lu P</td></tr>"
            "<tr><th>被过滤脉冲</th><td>%lu P</td></tr>",
            static_cast<unsigned long>(record.pulseCount),
            static_cast<unsigned long>(record.rejectedPulseCount));
    MeteringSchemeRecord meteringScheme{};
    if (ensureMeteringSchemesReady() && g_context.meteringSchemes->findById(record.meteringSchemeId, meteringScheme)) {
        char startupDuration[24]{};
        formatStartupDurationSeconds(meteringScheme.params.startupDurationMs, startupDuration, sizeof(startupDuration));
        Esp32BaseWeb::sendChunk("<tr><th>计量方案</th><td>");
        if (meteringScheme.name[0]) {
            sendHtmlEscapedBounded(meteringScheme.name, sizeof(meteringScheme.name));
        } else {
            sendFmt("计量方案 %lu", static_cast<unsigned long>(record.meteringSchemeId));
        }
        sendFmt("<span class='inline-note'>ID #%lu</span></td></tr>"
                "<tr><th>启动脉冲数</th><td>%lu P</td></tr>"
                "<tr><th>启动水量</th><td>%lu ml</td></tr>"
                "<tr><th>稳态 P/L</th><td>%lu P/L</td></tr>"
                "<tr><th>启动时长</th><td>%s</td></tr>"
                "<tr><th>预计稳态流速</th><td>%lu ml/min</td></tr>",
                static_cast<unsigned long>(meteringScheme.id),
                static_cast<unsigned long>(meteringScheme.params.startupPulseCount),
                static_cast<unsigned long>(meteringScheme.params.startupVolumeMl),
                static_cast<unsigned long>(meteringScheme.params.stablePulsePerLiter),
                startupDuration,
                static_cast<unsigned long>(meteringScheme.params.stableFlowMlPerMin));
        if (record.mode == WaterMode::Volume) {
            const MeteringTargetEstimate targetEstimate =
                meteringEstimateForTarget(meteringScheme.params, record.targetValue);
            if (targetEstimate.valid) {
                sendFmt("<tr><th>目标预计总脉冲</th><td>%lu P</td></tr>"
                        "<tr><th>目标全程平均 P/L</th><td>%lu P/L</td></tr>",
                        static_cast<unsigned long>(targetEstimate.pulseCount),
                        static_cast<unsigned long>(targetEstimate.fullRunPulsePerLiter));
            }
        }
    } else if (record.meteringSchemeId != 0) {
        sendFmt("<tr><th>计量方案</th><td>计量方案 ID #%lu 已被覆盖，历史参数不可查看</td></tr>",
                static_cast<unsigned long>(record.meteringSchemeId));
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>计量方案</th><td>记录格式不兼容，历史参数不可查看</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table></section></div>");
    sendPageEnd();
}

void handleCalibrationPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }
    char view[24]{};
    if (getParam("view", view, sizeof(view))) {
        if (std::strcmp(view, "temperature") == 0) {
            handleTemperatureCalibrationPage();
            return;
        }
        if (std::strcmp(view, "tds") == 0) {
            handleTdsCalibrationPage();
            return;
        }
    }

    const AppSnapshot snapshot = g_context.app->snapshot();
    if (!calibrationSessionInactive(snapshot.calibrationStatus)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow");
        return;
    }

    Esp32BaseWeb::sendHeader("校准");
    Esp32BaseWeb::sendChunk("<h2>校准</h2>");
    sendNoticeFromQuery();

    sendCalibrationCenterFlowCard(snapshot);
    sendCalibrationCenterTemperatureCard(snapshot, *g_context.config);
    sendCalibrationCenterTdsCard(snapshot, *g_context.config);
    sendCalibrationPageScript();
    sendPageEnd();
}

void handleFlowCalibrationPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!requireContext()) {
        return;
    }
    char text[32]{};
    if (getParam("partial", text, sizeof(text))) {
        if (!Esp32BaseWeb::beginResponse(200, "text/html; charset=utf-8", nullptr)) {
            return;
        }
        if (std::strcmp(text, "session") == 0) {
            sendFlowCalibrationSessionPanel(g_context.app->snapshot(), waterTaskActive());
        } else if (std::strcmp(text, "samples") == 0) {
            sendCalibrationSamplesPanel(configuredPulseObservationWindowSec());
        }
        Esp32BaseWeb::endResponse();
        return;
    }
    if (getParam("manual", text, sizeof(text))) {
        MeteringParameters prefill{};
        bool hasPrefill = false;
        if (!readMeteringManualPrefillFromQuery(prefill, hasPrefill)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=invalid_value");
            return;
        }
        MeteringSchemeRecord draft{};
        if (hasPrefill) {
            char name[kMeteringSchemeNameLength]{};
            const char* prefillName = getParam("name", name, sizeof(name)) && name[0] ? name : "手工参数";
            initializeManualMeteringScheme(draft, 0, prefillName, prefill, g_context.nowSeconds ? g_context.nowSeconds() : 0);
        }
        sendManualMeteringParameterPage(hasPrefill ? &draft : nullptr);
        return;
    }
    if (getParam("scheme", text, sizeof(text))) {
        if (std::strcmp(text, "new") == 0) {
            sendManualMeteringParameterPage(nullptr);
            return;
        }
        std::uint32_t id = 0;
        MeteringSchemeRecord scheme{};
        if (!parseU32(text, id) || !ensureMeteringSchemesReady() || !g_context.meteringSchemes->findById(id, scheme)) {
            Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=invalid_value");
            return;
        }
        sendManualMeteringParameterPage(&scheme);
        return;
    }

    const AppSnapshot snapshot = g_context.app->snapshot();
    const bool taskActive = waterTaskActive();

    Esp32BaseWeb::sendHeader("流量计校准");
    Esp32BaseWeb::sendChunk("<h2>流量计校准</h2>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<p class='muted'>本页负责录入实测容量、管理样本和应用参数；出水与停水只能在设备本地按键完成。</p>");
    sendFlowCalibrationSessionPanel(snapshot, taskActive);
    sendCalibrationSamplesPanel(configuredPulseObservationWindowSec());
    Esp32BaseWeb::sendChunk("<div class='calibration-param-layout'>");
    sendActiveMeteringSchemeSummaryPanel();
    Esp32BaseWeb::sendChunk("</div>");
    sendCalibrationParameterPanels();
    sendCalibrationPageScript();
    sendPageEnd();
}

void sendDetailErrorPage(const char* title, const char* message, const char* backHref, const char* backLabel) {
    Esp32BaseWeb::sendHeader(title);
    sendFmt("<h2>%s</h2><p class='err'>%s</p>", title ? title : "详情", message ? message : "请求无效。");
    if (backHref && backLabel) {
        sendFmt("<p><a class='btn-link' href='%s'>%s</a></p>", backHref, backLabel);
    }
    sendPageEnd();
}

void handleCalibrationDetailPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    char text[24]{};
    constexpr const char* backHref = "/faucet/calibration";
    constexpr const char* backLabel = "返回校准";
    constexpr const char* detailPath = "/faucet/calibration/detail";

    if (!contextReady()) {
        sendDetailErrorPage("脉冲明细", "上下文未就绪。", nullptr, nullptr);
        return;
    }
    if (waterTaskActive()) {
        sendBusyJson("record_detail");
        return;
    }

    bool useSessionTrace = false;
    std::uint32_t sessionTraceSlot = 0;
    std::uint32_t traceId = 0;
    if (getParam("slot", text, sizeof(text))) {
        useSessionTrace = parseU32(text, sessionTraceSlot) && sessionTraceSlot < kCalibrationSessionTraceSlots;
    }
    if (!useSessionTrace && (!getParam("trace", text, sizeof(text)) || !parseU32(text, traceId))) {
        sendDetailErrorPage("脉冲明细", "明细编号无效。", backHref, backLabel);
        return;
    }

    std::uint32_t bucketSeconds = 1;
    if (getParam("bucket", text, sizeof(text))) {
        parseU32(text, bucketSeconds);
    }
    if (bucketSeconds != 2 && bucketSeconds != 3 && bucketSeconds != 4 && bucketSeconds != 5) {
        bucketSeconds = 1;
    }

    const WaterPulseTrace* trace = nullptr;
    WaterPulseTrace sessionTrace{};
    std::unique_ptr<WaterPulseTraceBucketSample[]> buckets;
    std::unique_ptr<WaterPulseTraceSample[]> startupEdges;
    std::size_t loadedBucketCount = 0;
    std::size_t loadedStartupEdgeCount = 0;

    if (useSessionTrace) {
        if (!g_context.calibrationSessionTraces || !g_context.calibrationSessionTraces->ready()) {
            sendDetailErrorPage("脉冲明细", "校准会话脉冲数据不可用。", backHref, backLabel);
            return;
        }
        CalibrationStoredTrace stored{};
        if (!g_context.calibrationSessionTraces->load(static_cast<std::uint8_t>(sessionTraceSlot), stored)) {
            sendDetailErrorPage("脉冲明细", "该校准会话脉冲明细不存在。", backHref, backLabel);
            return;
        }
        sessionTrace = stored.trace;
        trace = &sessionTrace;
        traceId = trace->traceId;
        if (trace->bucketCount > 0) {
            buckets.reset(new (std::nothrow) WaterPulseTraceBucketSample[trace->bucketCount]{});
            if (buckets) {
                loadedBucketCount = g_context.calibrationSessionTraces->readBuckets(
                    static_cast<std::uint8_t>(sessionTraceSlot), buckets.get(), trace->bucketCount);
            }
        }
        if (trace->startupEdgeCount > 0) {
            startupEdges.reset(new (std::nothrow) WaterPulseTraceSample[trace->startupEdgeCount]{});
            if (startupEdges) {
                loadedStartupEdgeCount = g_context.calibrationSessionTraces->readStartupEdges(
                    static_cast<std::uint8_t>(sessionTraceSlot), startupEdges.get(), trace->startupEdgeCount);
            }
        }
    } else {
        if (!g_context.pulseTraces) {
            sendDetailErrorPage("脉冲明细", "脉冲明细缓存不可用。", backHref, backLabel);
            return;
        }
        trace = g_context.pulseTraces->findById(traceId);
        if (trace && trace->bucketCount > 0) {
            buckets.reset(new (std::nothrow) WaterPulseTraceBucketSample[trace->bucketCount]{});
            if (buckets) {
                for (std::size_t i = 0; i < trace->bucketCount; ++i) {
                    const WaterPulseTraceBucketSample* bucket = g_context.pulseTraces->bucketAt(*trace, i);
                    buckets[i] = bucket ? *bucket : WaterPulseTraceBucketSample{};
                }
                loadedBucketCount = trace->bucketCount;
            }
        }
        if (trace && trace->startupEdgeCount > 0) {
            startupEdges.reset(new (std::nothrow) WaterPulseTraceSample[trace->startupEdgeCount]{});
            if (startupEdges) {
                for (std::size_t i = 0; i < trace->startupEdgeCount; ++i) {
                    const WaterPulseTraceSample* edge = g_context.pulseTraces->startupEdgeAt(*trace, i);
                    startupEdges[i] = edge ? *edge : WaterPulseTraceSample{};
                }
                loadedStartupEdgeCount = trace->startupEdgeCount;
            }
        }
    }

    if (!trace) {
        sendDetailErrorPage("脉冲明细", "该脉冲明细不存在。", backHref, backLabel);
        return;
    }
    if ((trace->bucketCount > 0 && (!buckets || loadedBucketCount != trace->bucketCount)) ||
        (trace->startupEdgeCount > 0 && (!startupEdges || loadedStartupEdgeCount != trace->startupEdgeCount))) {
        sendDetailErrorPage("脉冲明细", "内存不足，无法读取脉冲明细。", nullptr, nullptr);
        return;
    }

    char detailSourceParam[32]{};
    if (useSessionTrace) {
        std::snprintf(detailSourceParam, sizeof(detailSourceParam), "slot=%u", static_cast<unsigned>(sessionTraceSlot));
    } else {
        std::snprintf(detailSourceParam, sizeof(detailSourceParam), "trace=%lu", static_cast<unsigned long>(traceId));
    }
    char startTime[40]{};
    formatWaterRecordTime(trace->record, startTime, sizeof(startTime));

    Esp32BaseWeb::sendHeader("脉冲明细");
    sendFmt("<h2>脉冲明细</h2><div class='form-actions'><a class='btn-link' href='%s'>%s</a></div>",
            backHref,
            backLabel);
    Esp32BaseWeb::sendChunk("<section class='panel'><h3>明细概况</h3><table class='kv'>");
    sendFmt("<tr><th>开始时间</th><td>%s</td></tr><tr><th>持续时间</th><td>%lu s</td></tr>"
            "<tr><th>有效脉冲</th><td>%lu</td></tr><tr><th>最小间隔过滤</th><td>%lu</td></tr>"
            "<tr><th>时间桶</th><td>%lu 个，每桶 %lums</td></tr><tr><th>启动边沿</th><td>%lu 个</td></tr>",
            startTime,
            static_cast<unsigned long>(trace->record.durationSec),
            static_cast<unsigned long>(trace->totalPulses),
            static_cast<unsigned long>(trace->minIntervalFilteredCount),
            static_cast<unsigned long>(trace->bucketCount),
            static_cast<unsigned long>(kPulseTraceBucketMs),
            static_cast<unsigned long>(trace->startupEdgeCount));
    Esp32BaseWeb::sendChunk("</table></section>");

    Esp32BaseWeb::sendChunk("<section id='pulse-trend' class='panel'><div class='panel-head'><h3>时间桶明细</h3><div class='row-actions'>");
    constexpr std::uint32_t bucketsToShow[] = {1, 2, 3, 4, 5};
    for (std::uint32_t bucket : bucketsToShow) {
        const char* linkClass = bucket == bucketSeconds ? "btn-link page-current" : "btn-link";
        sendFmt("<a class='%s' href='%s?%s&bucket=%lu'>%lus</a>",
                linkClass,
                detailPath,
                detailSourceParam,
                static_cast<unsigned long>(bucket),
                static_cast<unsigned long>(bucket));
    }
    Esp32BaseWeb::sendChunk("</div></div>");
    sendFmt("<p class='hint'>原始明细按 %lums 时间桶保存；本页只展示轻量表格，不生成图表。</p>"
            "<table><tr><th>时间</th><th>脉冲</th><th>累计</th><th>状态</th></tr>",
            static_cast<unsigned long>(kPulseTraceBucketMs));
    std::uint32_t cumulative = 0;
    for (std::size_t i = 0; i < trace->bucketCount; ++i) {
        const std::uint32_t startMs = static_cast<std::uint32_t>(i * kPulseTraceBucketMs);
        const std::uint32_t endMs = startMs + kPulseTraceBucketMs;
        cumulative += buckets[i].pulseCount;
        sendFmt("<tr><td>%lu.%03lu-%lu.%03lu s</td><td>%u</td><td>%lu</td><td>%s</td></tr>",
                static_cast<unsigned long>(startMs / 1000UL),
                static_cast<unsigned long>(startMs % 1000UL),
                static_cast<unsigned long>(endMs / 1000UL),
                static_cast<unsigned long>(endMs % 1000UL),
                static_cast<unsigned>(buckets[i].pulseCount),
                static_cast<unsigned long>(cumulative),
                i >= loadedBucketCount ? "未保存" : "已保存");
    }
    Esp32BaseWeb::sendChunk("</table></section>");

    Esp32BaseWeb::sendChunk("<section class='panel'><h3>启动边沿</h3>");
    sendFmt("<p class='hint'>当前显示 %lu/%lu 条启动边沿。</p><table><tr><th>序号</th><th>距任务开始</th><th>与上一边沿间隔</th></tr>",
            static_cast<unsigned long>(loadedStartupEdgeCount),
            static_cast<unsigned long>(trace->startupEdgeCount));
    for (std::size_t i = 0; i < loadedStartupEdgeCount; ++i) {
        const std::uint32_t intervalUs = i == 0 ? 0 : startupEdges[i].elapsedUs - startupEdges[i - 1].elapsedUs;
        sendFmt("<tr><td>%lu</td><td>", static_cast<unsigned long>(i));
        sendDurationUs(startupEdges[i].elapsedUs);
        Esp32BaseWeb::sendChunk("</td><td>");
        if (i == 0) {
            Esp32BaseWeb::sendChunk("首个边沿");
        } else {
            sendDurationUs(intervalUs);
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table></section>");
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
        sendHtmlAttrEscapedBounded(todayDate, sizeof(todayDate));
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
    if (!g_context.config || !g_context.configStore || !g_context.statistics || !g_context.app || !g_context.filters ||
        !g_context.records || !g_context.recordCalibrations || !g_context.nowSeconds) {
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

void handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void*) {
    if (!result.formatSuccess || !g_context.configStore || !g_context.statistics || !g_context.app) {
        return;
    }

    const StatisticsRecord current = g_context.app->snapshot().statistics;
    const PeriodKeys keys{current.lastDayKey, current.lastWeekKey, current.lastMonthKey};
    g_context.statistics->reset(keys);
    if (!g_context.configStore->resetStatistics(keys)) {
        g_context.app->markPersistenceDirtyForRetry();
    }
    if (g_context.afterFormatFs) {
        g_context.afterFormatFs();
    }
}

const char* configLoadStatusName(ConfigStore::LoadStatus status) {
    switch (status) {
        case ConfigStore::LoadStatus::Defaults:
            return "defaults";
        case ConfigStore::LoadStatus::LoadedCurrent:
            return "loaded_current";
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

bool persistPresetConfig(std::size_t index, const PresetConfig& preset) {
    if (!g_context.app->canApplyConfig() || index >= kPresetCount) {
        return false;
    }

    SystemConfig safe = g_context.configStore->loadSystemConfig();
    safe.presets[index] = preset;
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

bool savePresetConfigAndReply(std::size_t index, const PresetConfig& preset) {
    if (!g_context.app->canApplyConfig()) {
        Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\",\"restartRecommended\":true}");
        return false;
    }
    const bool ok = persistPresetConfig(index, preset);
    Esp32BaseWeb::sendJson(ok ? 200 : 500,
                           ok ? "{\"ok\":true,\"restartRecommended\":true}" : "{\"error\":\"save_failed\"}");
    return ok;
}

bool readTdsCalibrationInput(std::uint16_t& referencePpm, bool allowZero) {
    char text[32]{};
    std::uint32_t value = 0;
    if (!getParam("referencePpm", text, sizeof(text)) || !parseU32(text, value) ||
        value > 2000 || (!allowZero && value == 0)) {
        return false;
    }
    referencePpm = static_cast<std::uint16_t>(value);
    return true;
}

bool readTemperatureCalibrationInput(std::int16_t& referenceCentiC) {
    char text[32]{};
    float referenceC = 0.0f;
    if (!getParam("referenceC", text, sizeof(text)) || !parseFloat(text, referenceC) || referenceC < 0.0f ||
        referenceC > 90.0f) {
        return false;
    }
    referenceCentiC = static_cast<std::int16_t>(referenceC * 100.0f + 0.5f);
    return true;
}

void redirectTemperatureCalibrationResult(const char* saved) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=temperature&saved=%s", saved ? saved : "temperature");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTemperatureCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=temperature&error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTdsCalibrationResult(bool ok, const char* success, const char* failure) {
    char url[96]{};
    if (ok) {
        std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&saved=%s", success ? success : "tds_saved");
    } else {
        std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&error=%s", failure ? failure : "save_failed");
    }
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTdsCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void persistTdsCalibrationResult(bool ok, const char* success, const char* failure) {
    if (!ok || !g_context.app || !g_context.config || !g_context.configStore) {
        redirectTdsCalibrationFailure(failure);
        return;
    }
    const SystemConfig updated = g_context.app->config();
    if (!g_context.configStore->saveSystemConfig(updated)) {
        redirectTdsCalibrationFailure("save_failed");
        return;
    }
    *g_context.config = updated;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    redirectTdsCalibrationResult(true, success, failure);
}

void handleCalibrationPost() {
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (!Esp32BaseWeb::checkPostAllowed("faucet_calibration")) {
        return;
    }
    char text[32]{};
    if (!getParam("action", text, sizeof(text))) {
        redirectCalibrationFailure("invalid_action");
        return;
    }
    if (std::strcmp(text, "temperature_save") == 0) {
        std::int16_t reference = 0;
        if (!readTemperatureCalibrationInput(reference)) {
            redirectTemperatureCalibrationFailure("invalid_value");
            return;
        }
        const bool updated = g_context.app && g_context.app->saveTemperatureCalibrationForWeb(reference);
        if (!updated || !g_context.configStore->saveSystemConfig(g_context.app->config())) {
            redirectTemperatureCalibrationFailure("save_failed");
            return;
        }
        *g_context.config = g_context.app->config();
        if (g_context.applySettings) {
            g_context.applySettings(*g_context.config);
        }
        redirectTemperatureCalibrationResult("temperature");
        return;
    }
    if (std::strcmp(text, "tds_start_session") == 0) {
        if (!g_context.app || waterTaskActive()) {
            redirectTdsCalibrationFailure("busy");
            return;
        }
        redirectTdsCalibrationResult(g_context.app->startTdsCalibrationSessionForWeb(
                                         g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_started",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_start_point") == 0) {
        std::uint16_t referencePpm = 0;
        if (!readTdsCalibrationInput(referencePpm, true)) {
            redirectTdsCalibrationFailure("invalid_value");
            return;
        }
        redirectTdsCalibrationResult(g_context.app &&
                                         g_context.app->startTdsCalibrationPointForWeb(
                                             referencePpm,
                                             g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_point_started",
                                     "busy");
        return;
    }
    if (std::strcmp(text, "tds_save_point") == 0) {
        redirectTdsCalibrationResult(g_context.app &&
                                         g_context.app->saveTdsCalibrationPointForWeb(
                                             g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_point_saved",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_remove_point") == 0) {
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kTdsCalibrationMaxPoints) {
            redirectTdsCalibrationFailure("invalid_value");
            return;
        }
        redirectTdsCalibrationResult(g_context.app &&
                                         g_context.app->removeTdsCalibrationPointForWeb(
                                             static_cast<std::uint8_t>(index),
                                             g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                     "tds_point_removed",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_discard_session") == 0) {
        redirectTdsCalibrationResult(g_context.app && g_context.app->discardTdsCalibrationForWeb(),
                                     "tds_discarded",
                                     "invalid_state");
        return;
    }
    if (std::strcmp(text, "tds_apply_session") == 0) {
        persistTdsCalibrationResult(g_context.app &&
                                        g_context.app->applyTdsCalibrationForWeb(
                                            g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                    "tds_saved",
                                    "invalid_state");
        return;
    }
    redirectCalibrationFailure("invalid_action");
}

void handleFlowCalibrationPost() {
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (!Esp32BaseWeb::checkPostAllowed("faucet_metering")) {
        return;
    }
    char text[32]{};
    const bool ajax = Esp32BaseWeb::hasParam("ajax");
    const std::uint32_t now = g_context.nowSeconds ? g_context.nowSeconds() : 0;
    if (!getParam("action", text, sizeof(text))) {
        sendFlowCalibrationPostFailure(ajax, "invalid_value");
        return;
    }
    if (std::strcmp(text, "start_session") == 0) {
        if (waterTaskActive()) {
            sendFlowCalibrationPostFailure(ajax, "busy");
            return;
        }
        if (!calibrationSessionStorageReady()) {
            sendFlowCalibrationPostFailure(ajax, "calibration_storage_unavailable");
            return;
        }
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->startCalibrationSessionForWeb(now),
                                      "session_started",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "discard_session") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->discardCalibrationSessionForWeb(now),
                                      "session_discarded",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "save_actual") == 0) {
        std::uint32_t actualMl = 0;
        if (!getParam("actualMl", text, sizeof(text)) || !parseU32(text, actualMl) ||
            actualMl < kCalibrationMinActualMl || actualMl > kMaxVolumePresetMl) {
            sendFlowCalibrationPostFailure(ajax, "invalid_value");
            return;
        }
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->submitCalibrationActualForWeb(actualMl, now),
                                      "actual",
                                      "save_failed");
        return;
    }
    if (std::strcmp(text, "skip_attempt") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->skipCalibrationAttemptForWeb(now),
                                      "attempt_skipped",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "remove_sample") == 0) {
        std::uint32_t attemptIndex = 0;
        if (!getParam("attemptIndex", text, sizeof(text)) || !parseU32(text, attemptIndex) ||
            attemptIndex >= kCalibrationMaxAttempts) {
            sendFlowCalibrationPostFailure(ajax, "invalid_value");
            return;
        }
        if (waterTaskActive()) {
            sendFlowCalibrationPostFailure(ajax, "busy");
            return;
        }
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->removeCalibrationSessionSampleForWeb(
                                                           static_cast<std::uint8_t>(attemptIndex), now),
                                      "sample_removed",
                                      "invalid_state");
        return;
    }
    if (std::strcmp(text, "generate_session") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->generateCalibrationForWeb(now),
                                      "generated",
                                      "sample_not_enough");
        return;
    }
    if (std::strcmp(text, "apply_session") == 0) {
        sendFlowCalibrationPostResult(ajax,
                                      g_context.app && g_context.app->applyGeneratedCalibrationForWeb(now),
                                      "applied",
                                      "no_generated_result");
        return;
    }
    if (std::strcmp(text, "create_metering_scheme") == 0) {
        handleCreateMeteringSchemeApi();
        return;
    }
    sendFlowCalibrationPostFailure(ajax, "invalid_value");
}

bool persistFilterConfig(const FilterRecord& record, std::size_t index) {
    if (!g_context.app->canApplyConfig() || index >= kFilterCount) {
        return false;
    }

    std::unique_ptr<SystemConfig> safe(new (std::nothrow) SystemConfig(g_context.configStore->loadSystemConfig()));
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
    runtime[index].startTime = record.startTime;
    runtime[index].usedMl = record.usedMl;
    runtime[index].startBootId = record.startBootId;

    if (!g_context.configStore->saveSystemConfig(*safe) || !g_context.configStore->saveFilterRuntime(runtime)) {
        return false;
    }
    FilterRecord liveRecord = safe->filters[index];
    liveRecord.startTime = runtime[index].startTime;
    liveRecord.usedMl = runtime[index].usedMl;
    liveRecord.startBootId = runtime[index].startBootId;
    if (!g_context.app->applyConfig(*safe) || !g_context.filters->updateFilter(index, liveRecord)) {
        return false;
    }
    *g_context.config = *safe;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    return true;
}

void sendCurrentStatusJson() {
    const FaucetDisplayStatus displayStatus =
        g_context.currentDisplayStatus
            ? g_context.currentDisplayStatus()
            : FaucetDisplayStatus{false};
    const ConfigRuntimeStatus configStatus{
        configLoadStatusName(g_context.configStore->lastSystemConfigLoadStatus()),
        g_context.configStore->lastSystemConfigRawVersion(),
        g_context.configStore->currentSystemConfigVersion(),
    };
    AppSnapshot snapshot = g_context.app->snapshot();
    applyTargetDurationEstimate(snapshot, false);
    static char json[4096]{};
    json[0] = '\0';
    sendJsonBuffer(
        writeStatusJson(snapshot, displayStatus.screenOn, *g_context.config, &configStatus, json, sizeof(json)),
        json);
}

const char* calibrationSessionStatusText(CalibrationSessionStatus status) {
    switch (status) {
        case CalibrationSessionStatus::Idle:
            return "未开始";
        case CalibrationSessionStatus::Preparing:
            return "准备中";
        case CalibrationSessionStatus::WaitingLocalRun:
            return "等待本地出水";
        case CalibrationSessionStatus::Running:
            return "本地出水中";
        case CalibrationSessionStatus::AwaitingActual:
            return "等待输入实际容量";
        case CalibrationSessionStatus::ReadyToGenerate:
            return "可生成";
        case CalibrationSessionStatus::Generated:
            return "已生成，待应用";
        case CalibrationSessionStatus::Applied:
            return "已应用";
        case CalibrationSessionStatus::Discarded:
            return "已放弃";
        case CalibrationSessionStatus::Failed:
            return "失败";
    }
    return "未知";
}

bool calibrationSessionInactive(CalibrationSessionStatus status) {
    return status == CalibrationSessionStatus::Idle || status == CalibrationSessionStatus::Applied ||
           status == CalibrationSessionStatus::Discarded || status == CalibrationSessionStatus::Failed;
}

bool calibrationSessionStorageReady() {
    if (!g_context.calibrationSessions || !g_context.calibrationSessionTraces) {
        return false;
    }
    if (!g_context.calibrationSessions->ready() && !g_context.calibrationSessions->begin()) {
        return false;
    }
    if (!g_context.calibrationSessionTraces->ready() && !g_context.calibrationSessionTraces->begin()) {
        return false;
    }
    return g_context.calibrationSessions->ready() && g_context.calibrationSessionTraces->ready();
}

void redirectCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectFlowCalibrationFailure(const char* error) {
    char url[112]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration/flow?error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}

void sendFlowCalibrationPostFailure(bool ajax, const char* failure) {
    if (!ajax) {
        redirectFlowCalibrationFailure(failure);
        return;
    }
    char json[80]{};
    std::snprintf(json, sizeof(json), "{\"error\":\"%s\"}", failure ? failure : "save_failed");
    Esp32BaseWeb::sendJson(400, json);
}

void sendFlowCalibrationPostResult(bool ajax, bool ok, const char* success, const char* failure) {
    if (!ok) {
        sendFlowCalibrationPostFailure(ajax, failure);
        return;
    }
    if (ajax) {
        Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
        return;
    }
    char url[112]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration/flow?saved=%s", success ? success : "1");
    Esp32BaseWeb::redirectSeeOther(url);
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    sendCurrentStatusJson();
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
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("faucet_presets") || !requireContext()) {
            return;
        }
        char text[24]{};
        if (getParam("action", text, sizeof(text))) {
            bool ok = false;
            if (std::strcmp(text, "select_previous") == 0) {
                ok = g_context.app->selectPreviousPresetForWeb();
            } else if (std::strcmp(text, "select_next") == 0) {
                ok = g_context.app->selectNextPresetForWeb();
            } else {
                std::uint32_t index = 0;
                if (std::strcmp(text, "select") == 0 && getParam("index", text, sizeof(text)) &&
                    parseU32(text, index)) {
                    ok = g_context.app->selectPresetForWeb(index);
                } else {
                    Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_action\"}");
                    return;
                }
            }
            if (!ok) {
                Esp32BaseWeb::sendJson(409, "{\"error\":\"preset_unavailable\"}");
                return;
            }
            sendCurrentStatusJson();
            return;
        }
        const bool browserForm = Esp32BaseWeb::hasParam("return");
        if (!g_context.app->canApplyConfig()) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=busy");
                return;
            }
            Esp32BaseWeb::sendJson(409, "{\"error\":\"busy\",\"restartRecommended\":true}");
            return;
        }
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kPresetCount) {
            if (browserForm) {
                Esp32BaseWeb::redirectSeeOther("/faucet/presets?error=invalid_index");
                return;
            }
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
            return;
        }
        PresetConfig preset = g_context.config->presets[index];
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
            const bool ok = persistPresetConfig(index, preset);
            Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/presets?saved=1" : "/faucet/presets?error=save_failed");
        } else {
            savePresetConfigAndReply(index, preset);
        }
        return;
    }
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writePresetsJson(g_context.config->presets, json, sizeof(json)), json);
}

void handleRecordsApi() {
    char text[32]{};
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        Esp32BaseWeb::sendJson(405, "{\"error\":\"method_not_allowed\"}");
        return;
    }
    if (waterTaskActive()) {
        sendBusyJson("records_api");
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
    if (getParam("startDate", dateText, sizeof(dateText))) {
        if (!parseDate(dateText, filter.startTime)) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_date\"}");
            return;
        }
        filter.hasStart = filter.startTime > 0;
    }
    if (getParam("endDate", dateText, sizeof(dateText))) {
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

    const std::uint16_t sanitizedPageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(pageSize));
    WaterRecord* records = new (std::nothrow) WaterRecord[kMaxRecordPageSize]{};
    char* json = new (std::nothrow) char[32768]{};
    if (!records || !json) {
        delete[] records;
        delete[] json;
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    const WaterRecordFileStatus recordStatus = g_context.records->status();
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
                                      waterRecordFileStatusCode(recordStatus),
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
    char* json = new (std::nothrow) char[32768]{};
    if (!json) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    std::unique_ptr<WaterUsageSummary> summary(new (std::nothrow) WaterUsageSummary);
    if (!summary || !aggregateWaterRecordsInto(*g_context.records, g_context.nowSeconds(), kChartDays, false, *summary)) {
        delete[] json;
        Esp32BaseWeb::sendJson(500, "{\"error\":\"oom\"}");
        return;
    }
    const std::uint32_t totalMl = g_context.app->snapshot().statistics.totalMl;
    sendJsonBuffer(writeUsageSummaryJson(*summary, totalMl, json, 32768), json);
    delete[] json;
}

bool readMeteringParamsFromRequest(MeteringParameters& params) {
    char text[32]{};
    float startupDurationSec = 0.0f;
    if (!(getParam("startupPulseCount", text, sizeof(text)) && parseU32(text, params.startupPulseCount) &&
          getParam("startupVolumeMl", text, sizeof(text)) && parseU32(text, params.startupVolumeMl) &&
          getParam("stablePulsePerLiter", text, sizeof(text)) && parseU32(text, params.stablePulsePerLiter) &&
          getParam("startupDurationSec", text, sizeof(text)) && parseFloat(text, startupDurationSec) &&
          getParam("stableFlowMlPerMin", text, sizeof(text)) && parseU32(text, params.stableFlowMlPerMin))) {
        return false;
    }
    if (startupDurationSec < 0.0f ||
        startupDurationSec > static_cast<float>(kMaxSegmentedStartupDurationMs) / 1000.0f) {
        return false;
    }
    params.startupDurationMs = static_cast<std::uint32_t>(startupDurationSec * 1000.0f + 0.5f);
    return validMeteringSchemeParameters(params);
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
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=busy");
        return;
    }
    if (!g_context.app || !g_context.app->canApplyConfig()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=busy");
        return;
    }
    if (!ensureMeteringSchemesReady()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=metering_storage_unavailable");
        return;
    }
    MeteringParameters params{};
    if (!readMeteringParamsFromRequest(params)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=invalid_value");
        return;
    }
    char name[kMeteringSchemeNameLength]{};
    if (!getParam("name", name, sizeof(name)) || name[0] == '\0') {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=invalid_value");
        return;
    }
    std::uint32_t newId = 0;
    if (!g_context.meteringSchemes->createManual(name, params, g_context.nowSeconds ? g_context.nowSeconds() : 0, newId)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=save_failed");
        return;
    }
    if (!setAndApplyActiveMeteringSchemeForWeb(newId)) {
        Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?error=save_failed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration/flow?saved=scheme_created");
}

void handleFiltersApi() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("faucet_filters") || !requireContext()) {
            return;
        }
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
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writeFiltersJson(g_context.filters->records(), json, sizeof(json)), json);
}

void handleFiltersResetApi() {
    if (!Esp32BaseWeb::checkPostAllowed("faucet_filter_reset") || !requireContext()) {
        return;
    }
    if (waterTaskActive()) {
        Esp32BaseWeb::redirectSeeOther("/faucet/filters?error=busy");
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
    switch (route.handler) {
        case FaucetWebHandler::HomePage:
            return handleFaucetPage;
        case FaucetWebHandler::RecordsPage:
            return handleRecordsPage;
        case FaucetWebHandler::StatsPage:
            return handleStatsPage;
        case FaucetWebHandler::PresetsPage:
            return handlePresetsPage;
        case FaucetWebHandler::FiltersPage:
            return handleFiltersPage;
        case FaucetWebHandler::CalibrationPage:
            return handleCalibrationPage;
        case FaucetWebHandler::AppCss:
            return handleAppCss;
        case FaucetWebHandler::CalibrationPost:
            return handleCalibrationPost;
        case FaucetWebHandler::FlowCalibrationPage:
            return handleFlowCalibrationPage;
        case FaucetWebHandler::FlowCalibrationPost:
            return handleFlowCalibrationPost;
        case FaucetWebHandler::FilterEditPage:
            return handleFilterEditPage;
        case FaucetWebHandler::RecordDetailPage:
            return handleRecordInfoPage;
        case FaucetWebHandler::CalibrationDetailPage:
            return handleCalibrationDetailPage;
        case FaucetWebHandler::StatusApi:
            return handleStatusApi;
        case FaucetWebHandler::TodayOverviewApi:
            return handleTodayOverviewApi;
        case FaucetWebHandler::PresetsApi:
            return handlePresetsApi;
        case FaucetWebHandler::RecordsApi:
            return handleRecordsApi;
        case FaucetWebHandler::StatsApi:
            return handleStatsApi;
        case FaucetWebHandler::FiltersApi:
            return handleFiltersApi;
        case FaucetWebHandler::FiltersResetApi:
            return handleFiltersResetApi;
    }
    return nullptr;
}

}  // namespace

void setFaucetWebContext(const FaucetWebContext& context) {
    g_context = context;
}

bool registerFaucetWeb() {
    if (faucetWebRouteCount() > kFaucetWebMaxRoutes) {
        return false;
    }

    Esp32BaseWeb::setHeadExtraCallback(sendAppStylesheetLink);
    Esp32BaseWeb::setAfterFormatFsCallback(handleAfterFormatFs);

    bool ok = true;
    const FaucetWebRoute* routes = faucetWebRoutes();
    for (std::size_t i = 0; i < faucetWebRouteCount(); ++i) {
        Esp32BaseWeb::Handler handler = handlerFor(routes[i]);
        if (!handler) {
            ok = false;
            continue;
        }
        if (routes[i].title && routes[i].method == FaucetWebMethod::Get) {
            ok = Esp32BaseWeb::addPage(routes[i].path, routes[i].title, handler) && ok;
        } else {
            ok = Esp32BaseWeb::addRoute(routes[i].path, toBaseMethod(routes[i].method), handler) && ok;
        }
    }
    return ok;
}

}  // namespace faucet

#endif
