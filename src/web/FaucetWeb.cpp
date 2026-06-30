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

#include "FaucetWebCalibrationWidgets.inc"

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

#include "FaucetWebHomePages.inc"

#include "FaucetWebStatsPages.inc"

#include "FaucetWebRecordsPages.inc"

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
        !g_context.records || !g_context.nowSeconds) {
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

#include "FaucetWebCalibrationActions.inc"

#include "FaucetWebFiltersPersistence.inc"

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

#include "FaucetWebCalibrationSession.inc"

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    sendCurrentStatusJson();
}

#include "FaucetWebBusinessApi.inc"

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

#include "FaucetWebFiltersApi.inc"

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
