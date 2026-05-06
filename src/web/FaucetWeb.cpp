#ifndef NATIVE_BUILD

#include "web/FaucetWeb.h"

#include "app/AppController.h"
#include "app/AppConfig.h"
#include "app/ConfigStore.h"
#include "app/FilterStore.h"
#include "app/WaterLogFileStore.h"
#include "web/FaucetWebJson.h"
#include "web/FaucetWebRoutes.h"

#include <Esp32Base.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace faucet {
namespace {

FaucetWebContext g_context{};

bool requireContext();
bool getParam(const char* name, char* out, std::size_t len);
bool parseU32(const char* text, std::uint32_t& value);

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
    char buffer[256]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Esp32BaseWeb::sendChunk(buffer);
}

void formatLiters(std::uint32_t ml, char* out, std::size_t len) {
    const std::uint32_t centiliters = (ml + 5UL) / 10UL;
    std::snprintf(out, len, "%lu.%02lu L", static_cast<unsigned long>(centiliters / 100UL),
                  static_cast<unsigned long>(centiliters % 100UL));
}

void sendLiters(std::uint32_t ml) {
    char text[24]{};
    formatLiters(ml, text, sizeof(text));
    Esp32BaseWeb::sendChunk(text);
}

void sendAppStyles() {
    Esp32BaseWeb::sendChunk("<style>"
                            "body{background:#f6f7f9;color:#1f2933;max-width:880px;padding:16px}"
                            "h1{color:#111827}"
                            "h2{font-size:1.3rem;margin:0 0 16px;color:#111827}"
                            "h3{font-size:1.02rem;margin:0;color:#111827}"
                            "a,button,input[type=submit],input[type=button]{border-radius:6px;box-shadow:none}"
                            ".faucet-actions{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 16px}"
                            ".faucet-actions a{display:inline-flex;align-items:center;min-height:34px;line-height:1.2;margin:0;white-space:nowrap}"
                            "input:not([type=submit]):not([type=button]),select{width:100%;padding:9px 10px;margin:0;border:1px solid #d1d5db;border-radius:6px;box-sizing:border-box;background:#fff;font-size:1rem}"
                            "select{margin:4px 0 12px}"
                            ".panel{border:1px solid #e5e7eb;border-radius:8px;padding:14px;margin:0 0 14px;background:#fff}"
                            ".panel h3{padding-bottom:10px;margin-bottom:12px;border-bottom:1px solid #eef0f3}"
                            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px 16px}"
                            ".field{display:block;margin:0}"
                            ".field span{display:block;font-size:.9em;color:#374151;margin-bottom:6px}"
                            ".field input{margin:0}"
                            ".hint{display:block;color:#6b7280;font-size:.82em;margin:5px 0 0}"
                            ".check{display:flex;align-items:center;gap:6px;min-height:38px;margin:0}"
                            ".check input{width:auto}"
                            "form input[type=submit]{margin-top:4px;min-height:38px;padding:8px 18px}"
                            "table{width:100%;border-collapse:collapse;margin:0 0 16px;background:#fff;border:1px solid #e5e7eb;border-radius:8px;overflow:hidden}"
                            "td,th{padding:9px 10px;border-bottom:1px solid #eef0f3;text-align:left}"
                            "tr:last-child td{border-bottom:0}"
                            "@media(max-width:520px){body{padding:12px}.grid{grid-template-columns:1fr}.panel{padding:12px}}"
                            "</style>");
}

bool sendPageStart(const char* title) {
    if (!Esp32BaseWeb::checkAuth()) {
        return false;
    }
    Esp32BaseWeb::sendHeader(title);
    sendAppStyles();
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
        case WaterMode::Calibration:
            return "校准";
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

void sendCheckbox(const char* label, const char* name, bool checked) {
    sendFmt("<label class='check'><input type='checkbox' name='%s' value='1'%s> %s</label>",
            name,
            checked ? " checked" : "",
            label);
}

void handleFaucetPage() {
    if (!sendPageStart("出水龙头")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const AppSnapshot snapshot = g_context.app->snapshot();
    Esp32BaseWeb::sendChunk("<h2>状态</h2><table>");
    sendFmt("<tr><td>运行状态</td><td>%s</td></tr>", stateText(snapshot.water.state));
    sendFmt("<tr><td>当前预设</td><td>%u</td></tr>", static_cast<unsigned>(snapshot.water.selectedPreset + 1));
    sendFmt("<tr><td>出水模式</td><td>%s</td></tr>", modeText(snapshot.water.mode));
    Esp32BaseWeb::sendChunk("<tr><td>已出水量</td><td>");
    sendLiters(snapshot.water.volumeMl);
    Esp32BaseWeb::sendChunk("</td></tr>");
    if (snapshot.water.mode == WaterMode::Time) {
        sendFmt("<tr><td>目标值</td><td>%lu 秒</td></tr>", static_cast<unsigned long>(snapshot.water.targetValue));
    } else {
        Esp32BaseWeb::sendChunk("<tr><td>目标值</td><td>");
        sendLiters(snapshot.water.targetValue);
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    sendFmt("<tr><td>电磁阀</td><td>%s</td></tr>", snapshot.water.valveOpen ? "开启" : "关闭");
    Esp32BaseWeb::sendChunk("</table><h2>统计</h2><table>");
    Esp32BaseWeb::sendChunk("<tr><td>今日</td><td>");
    sendLiters(snapshot.statistics.todayMl);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>本周</td><td>");
    sendLiters(snapshot.statistics.weekMl);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>本月</td><td>");
    sendLiters(snapshot.statistics.monthMl);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>总累计</td><td>");
    sendLiters(snapshot.statistics.totalMl);
    Esp32BaseWeb::sendChunk("</td></tr>");
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleConfigPage() {
    if (!sendPageStart("出水配置")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const SystemConfig& config = *g_context.config;
    Esp32BaseWeb::sendChunk("<h2>配置</h2><form method='post' action='/api/faucet/config' onsubmit='return once(this)'>"
                            "<section class='panel'><h3>安全限制</h3><div class='grid'>");
    sendTextInput("二次确认超时（秒）", "confirmTimeoutSec", config.confirmTimeoutSec);
    sendTextInput("最长出水时间（秒）", "maxOutTimeSec", config.maxOutTimeSec);
    sendVolumeInput("最大出水量（ml）", "maxOutVolumeMl", config.maxOutVolumeMl);
    sendTextInput("超量保护比例（%）", "overflowPercent", config.overflowPercent);
    Esp32BaseWeb::sendChunk("</div></section><section class='panel'><h3>流量保护</h3><div class='grid'>");
    sendTextInput("无流量超时（秒）", "noFlowTimeoutSec", config.noFlowTimeoutSec);
    sendTextInput("高流量阈值（ml/min）", "highFlowMlPerMin", config.highFlowMlPerMin);
    sendTextInput("高流量持续时间（秒）", "highFlowDurationSec", config.highFlowDurationSec);
    sendTextInput("暂停超时（秒）", "pauseTimeoutSec", config.pauseTimeoutSec);
    Esp32BaseWeb::sendChunk("</div></section><section class='panel'><h3>电磁阀</h3><div class='grid'>");
    sendTextInput("电磁阀全功率时间（秒）", "valveFullPowerSec", config.valveFullPowerSec);
    sendTextInput("电磁阀保持占空比（%）", "valveHoldDutyPercent", config.valveHoldDutyPercent);
    Esp32BaseWeb::sendChunk("</div></section><section class='panel'><h3>本地交互</h3><div class='grid'>");
    sendTextInput("OLED 熄屏时间（秒）", "oledSleepSec", config.oledSleepSec);
    Esp32BaseWeb::sendChunk("<div>");
    sendCheckbox("启用蜂鸣器", "beepEnabled", config.beepEnabled);
    Esp32BaseWeb::sendChunk("</div></div></section><input type='submit' value='保存'></form>");
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
    Esp32BaseWeb::sendChunk("<h2>预设</h2>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const PresetConfig& preset = g_context.config->presets[i];
        sendFmt("<h3>预设 %u</h3><form method='post' action='/api/faucet/presets' onsubmit='return once(this)'>"
                "<input type='hidden' name='index' value='%u'>",
                static_cast<unsigned>(i + 1),
                static_cast<unsigned>(i));
        sendCheckbox("启用", "enabled", preset.enabled);
        Esp32BaseWeb::sendChunk("名称<input name='name' maxlength='15' value='");
        Esp32BaseWeb::writeHtmlEscaped(preset.name);
        Esp32BaseWeb::sendChunk("'>类型<select name='type'>");
        sendFmt("<option value='volume'%s>容量</option>", preset.type == PresetType::Volume ? " selected" : "");
        sendFmt("<option value='time'%s>时间</option>", preset.type == PresetType::Time ? " selected" : "");
        Esp32BaseWeb::sendChunk("</select>");
        if (preset.type == PresetType::Volume) {
            sendVolumeInput("数值（ml）", "value", preset.value);
        } else {
            sendTextInput("数值（秒）", "value", preset.value);
        }
        Esp32BaseWeb::sendChunk("<input type='submit' value='保存'></form>");
    }
    sendPageEnd();
}

void handleStatsPage() {
    if (!sendPageStart("用水统计")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const StatisticsRecord& stats = g_context.app->snapshot().statistics;
    Esp32BaseWeb::sendChunk("<h2>统计</h2><table>");
    Esp32BaseWeb::sendChunk("<tr><td>今日</td><td>");
    sendLiters(stats.todayMl);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>本周</td><td>");
    sendLiters(stats.weekMl);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>本月</td><td>");
    sendLiters(stats.monthMl);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>总累计</td><td>");
    sendLiters(stats.totalMl);
    Esp32BaseWeb::sendChunk("</td></tr>");
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleLogsPage() {
    if (!sendPageStart("出水记录")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    char text[24]{};
    std::uint32_t page = 0;
    if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    constexpr std::uint16_t pageSize = 50;
    WaterLogRecord records[pageSize]{};
    const bool ready = g_context.logs->ready();
    const std::size_t count = ready ? g_context.logs->readPage(page, pageSize, records, pageSize) : 0;
    Esp32BaseWeb::sendChunk("<h2>记录</h2><form method='get' action='/faucet/logs'>页码<input name='page' value='");
    sendFmt("%lu", static_cast<unsigned long>(page));
    Esp32BaseWeb::sendChunk("'><input type='submit' value='查看'></form><table><tr><th>时间</th><th>出水量</th><th>时长</th><th>模式</th><th>结果</th></tr>");
    for (std::size_t i = 0; i < count; ++i) {
        sendFmt("<tr><td>%lu</td><td>", static_cast<unsigned long>(records[i].startTime));
        sendLiters(records[i].volumeMl);
        sendFmt("</td><td>%u s</td><td>%s</td><td>%s</td></tr>",
                static_cast<unsigned>(records[i].durationSec),
                modeText(records[i].mode),
                resultText(records[i].result));
    }
    Esp32BaseWeb::sendChunk("</table>");
    if (!ready) {
        Esp32BaseWeb::sendChunk("<p>记录存储不可用。</p>");
    }
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
    Esp32BaseWeb::sendChunk("<h2>滤芯</h2><table><tr><th>名称</th><th>已用天数</th><th>已用流量</th><th>重置</th></tr>");
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(filter.name);
        sendFmt("</td><td>%lu</td><td>", static_cast<unsigned long>(g_context.filters->usedDays(i, now)));
        sendLiters(filter.usedMl);
        sendFmt("</td><td>"
                "<form method='post' action='/api/faucet/filters/reset' onsubmit=\"return confirm('确认重置滤芯？')&&once(this)\">"
                "<input type='hidden' name='index' value='%u'><input type='submit' value='重置'></form></td></tr>",
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleCalibrationPage() {
    if (!sendPageStart("流量校准")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const SystemConfig& config = *g_context.config;
    Esp32BaseWeb::sendChunk("<h2>校准</h2><form method='post' action='/api/faucet/calibration' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("每 ml 脉冲数<input name='pulsePerMl' value='");
    sendFmt("%.3f", static_cast<double>(config.pulsePerMl));
    Esp32BaseWeb::sendChunk("'>");
    sendTextInput("校准目标（ml）", "targetMl", config.calibrationTargetMl);
    Esp32BaseWeb::sendChunk("<input type='submit' value='保存'></form>");
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
    if (!g_context.config || !g_context.configStore || !g_context.app || !g_context.filters || !g_context.logs ||
        !g_context.nowSeconds) {
        Esp32BaseWeb::sendJson(503, "{\"error\":\"context_not_ready\"}");
        return false;
    }
    return true;
}

bool getParam(const char* name, char* out, std::size_t len) {
    return Esp32BaseWeb::hasParam(name) && Esp32BaseWeb::getParam(name, out, len);
}

bool parseU32(const char* text, std::uint32_t& value) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (!end || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parseFloat(const char* text, float& value) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(text, &end);
    if (!end || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

bool parseBool(const char* text, bool& value) {
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "true") == 0 || std::strcmp(text, "on") == 0) {
        value = true;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "false") == 0 || std::strcmp(text, "off") == 0) {
        value = false;
        return true;
    }
    return false;
}

void applyU32Param(const char* name, std::uint32_t& value) {
    char text[24]{};
    std::uint32_t parsed = 0;
    if (getParam(name, text, sizeof(text)) && parseU32(text, parsed)) {
        value = parsed;
    }
}

void applyBoolParam(const char* name, bool& value) {
    char text[12]{};
    bool parsed = false;
    if (getParam(name, text, sizeof(text)) && parseBool(text, parsed)) {
        value = parsed;
    }
}

bool saveConfigAndReply(const char* kind) {
    sanitizeConfig(*g_context.config);
    const bool ok = g_context.configStore->saveSystemConfig(*g_context.config);
    Esp32BaseWeb::sendJson(ok ? 200 : 500,
                           ok ? "{\"ok\":true,\"restartRecommended\":true}" : "{\"error\":\"save_failed\"}");
    (void)kind;
    return ok;
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[256]{};
    sendJsonBuffer(writeStatusJson(g_context.app->snapshot(), json, sizeof(json)), json);
}

void handleConfigApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        applyU32Param("confirmTimeoutSec", g_context.config->confirmTimeoutSec);
        applyU32Param("maxOutTimeSec", g_context.config->maxOutTimeSec);
        applyU32Param("maxOutVolumeMl", g_context.config->maxOutVolumeMl);
        applyU32Param("noFlowTimeoutSec", g_context.config->noFlowTimeoutSec);
        applyU32Param("highFlowMlPerMin", g_context.config->highFlowMlPerMin);
        applyU32Param("highFlowDurationSec", g_context.config->highFlowDurationSec);
        applyU32Param("pauseTimeoutSec", g_context.config->pauseTimeoutSec);
        applyU32Param("valveFullPowerSec", g_context.config->valveFullPowerSec);
        applyU32Param("oledSleepSec", g_context.config->oledSleepSec);
        applyBoolParam("beepEnabled", g_context.config->beepEnabled);

        char text[24]{};
        std::uint32_t parsed = 0;
        if (getParam("overflowPercent", text, sizeof(text)) && parseU32(text, parsed)) {
            g_context.config->overflowPercent = static_cast<std::uint8_t>(parsed);
        }
        if (getParam("valveHoldDutyPercent", text, sizeof(text)) && parseU32(text, parsed)) {
            g_context.config->valveHoldDutyPercent = static_cast<std::uint8_t>(parsed);
        }
        saveConfigAndReply("config");
        return;
    }
    char json[640]{};
    sendJsonBuffer(writeConfigJson(*g_context.config, json, sizeof(json)), json);
}

void handlePresetsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kPresetCount) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
            return;
        }
        PresetConfig& preset = g_context.config->presets[index];
        bool enabled = false;
        if (getParam("enabled", text, sizeof(text)) && parseBool(text, enabled)) {
            preset.enabled = enabled;
        }
        if (getParam("type", text, sizeof(text))) {
            preset.type = std::strcmp(text, "time") == 0 ? PresetType::Time : PresetType::Volume;
        }
        std::uint32_t value = 0;
        if (getParam("value", text, sizeof(text)) && parseU32(text, value)) {
            preset.value = value;
        }
        Esp32BaseWeb::getParam("name", preset.name, sizeof(preset.name));
        saveConfigAndReply("presets");
        return;
    }
    char json[1536]{};
    sendJsonBuffer(writePresetsJson(g_context.config->presets, json, sizeof(json)), json);
}

void handleLogsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char text[24]{};
    std::uint32_t page = 0;
    std::uint32_t pageSize = kDefaultLogPageSize;
    if (getParam("page", text, sizeof(text))) {
        parseU32(text, page);
    }
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, pageSize);
    }
    if (pageSize > kMaxLogPageSize) {
        pageSize = kMaxLogPageSize;
    }

    const std::uint16_t sanitizedPageSize = sanitizeLogPageSize(static_cast<std::uint16_t>(pageSize));
    static WaterLogRecord records[kMaxLogPageSize]{};
    static char json[32768]{};
    const bool ready = g_context.logs->ready();
    const std::size_t readCount = ready ? g_context.logs->readPage(page, sanitizedPageSize, records, kMaxLogPageSize) : 0;
    const std::size_t totalCount = ready ? g_context.logs->count() : 0;
    sendJsonBuffer(writeWaterLogsJson(records,
                                      readCount,
                                      page,
                                      sanitizedPageSize,
                                      totalCount,
                                      ready,
                                      json,
                                      sizeof(json)),
                   json);
}

void handleStatsApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[256]{};
    sendJsonBuffer(writeStatsJson(g_context.app->snapshot().statistics, json, sizeof(json)), json);
}

void handleFiltersApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    char json[1024]{};
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

    if (!g_context.filters->resetFilter(index, g_context.nowSeconds())) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"reset_failed\"}");
        return;
    }
    const bool ok = g_context.configStore->saveFilterRuntime(g_context.filters->records());
    Esp32BaseWeb::sendJson(ok ? 200 : 500, ok ? "{\"ok\":true}" : "{\"error\":\"save_failed\"}");
}

void handleCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        char text[24]{};
        float pulsePerMl = 0.0f;
        if (getParam("pulsePerMl", text, sizeof(text)) && parseFloat(text, pulsePerMl)) {
            g_context.config->pulsePerMl = pulsePerMl;
        }
        std::uint32_t targetMl = 0;
        if (getParam("targetMl", text, sizeof(text)) && parseU32(text, targetMl)) {
            g_context.config->calibrationTargetMl = targetMl;
        }
        saveConfigAndReply("calibration");
        return;
    }
    char json[256]{};
    sendJsonBuffer(writeCalibrationJson(*g_context.config, json, sizeof(json)), json);
}

Esp32BaseWeb::Handler handlerFor(const FaucetWebRoute& route) {
    if (route.kind == FaucetWebRouteKind::Page) {
        if (std::strcmp(route.path, "/faucet") == 0) {
            return handleFaucetPage;
        }
        if (std::strcmp(route.path, "/faucet/config") == 0) {
            return handleConfigPage;
        }
        if (std::strcmp(route.path, "/faucet/logs") == 0) {
            return handleLogsPage;
        }
        if (std::strcmp(route.path, "/faucet/stats") == 0) {
            return handleStatsPage;
        }
        if (std::strcmp(route.path, "/faucet/filters") == 0) {
            return handleFiltersPage;
        }
        if (std::strcmp(route.path, "/faucet/calibration") == 0) {
            return handleCalibrationPage;
        }
        return handleFaucetPage;
    }
    if (std::strcmp(route.path, "/api/faucet/status") == 0) {
        return handleStatusApi;
    }
    if (std::strcmp(route.path, "/api/faucet/config") == 0) {
        return handleConfigApi;
    }
    if (std::strcmp(route.path, "/api/faucet/presets") == 0) {
        return handlePresetsApi;
    }
    if (std::strcmp(route.path, "/api/faucet/logs") == 0) {
        return handleLogsApi;
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
    if (std::strcmp(route.path, "/api/faucet/calibration") == 0) {
        return handleCalibrationApi;
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
