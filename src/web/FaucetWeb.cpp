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
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace faucet {
namespace {

constexpr std::uint32_t kMinFilterDateSeconds = 631152000UL;  // 2020-01-01 in seconds since 2000-01-01.
FaucetWebContext g_context{};

bool requireContext();
bool getParam(const char* name, char* out, std::size_t len);
bool parseU32(const char* text, std::uint32_t& value);
bool parseDate(const char* text, std::uint32_t& seconds);
void formatDate(std::uint32_t seconds, char* out, std::size_t len);

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

void formatLiterValue(std::uint32_t ml, char* out, std::size_t len) {
    if (ml == 0) {
        std::snprintf(out, len, "0");
        return;
    }
    const std::uint32_t centiliters = (ml + 5UL) / 10UL;
    std::snprintf(out, len, "%lu.%02lu", static_cast<unsigned long>(centiliters / 100UL),
                  static_cast<unsigned long>(centiliters % 100UL));
}

std::uint32_t filterProgressPercent(const FilterRecord& filter, std::uint32_t usedDays) {
    if (filter.recommendDays == 0) {
        return 0;
    }
    const std::uint64_t percent = (static_cast<std::uint64_t>(usedDays) * 100ULL) / filter.recommendDays;
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

void sendLiters(std::uint32_t ml) {
    char text[24]{};
    formatLiters(ml, text, sizeof(text));
    Esp32BaseWeb::sendChunk(text);
}

void sendMetricCard(const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<section class='metric-card'><span>");
    Esp32BaseWeb::sendChunk(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::sendChunk(value);
    Esp32BaseWeb::sendChunk("</strong></section>");
}

void sendAppStyles() {
    Esp32BaseWeb::sendChunk("<style>"
                            "body{background:#f4f7f6;color:#202428;max-width:1080px;padding:10px 14px 14px;line-height:1.45;font-size:14px}"
                            "h1{color:#111;font-size:1rem;font-weight:400;margin:0 0 10px}"
                            "h2{font-size:.98rem;font-weight:400;margin:0 0 10px;color:#111}"
                            "h3{font-size:.94rem;font-weight:400;margin:0;color:#111}"
                            "p{margin:5px 0}"
                            "strong,b,th{font-weight:400}"
                            "nav{display:flex;align-items:center;gap:5px;margin:0 0 12px;padding:10px 12px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;box-shadow:0 1px 2px rgba(21,35,34,.04);overflow-x:auto}"
                            "nav a{display:inline-flex;align-items:center;min-height:34px;padding:0 11px;margin:0;background:transparent;color:#2f3947;border-radius:9px;text-decoration:none;font-size:.98rem;font-weight:600;white-space:nowrap}"
                            "nav a.brand{background:transparent;color:#2f3947}"
                            "nav a.active{background:#e9f3ef;color:#226b5f}"
                            "a,button,input[type=submit],input[type=button]{border-radius:4px;box-shadow:none}"
                            "a{background:transparent;color:#2d6f7a;padding:0;margin:0;text-decoration:none}"
                            "button,input[type=submit],input[type=button]{background:#2f6f73;border:1px solid #2f6f73;color:#fff;cursor:pointer}"
                            ".faucet-actions{display:flex;flex-wrap:wrap;gap:5px;margin:0 0 10px}"
                            ".faucet-actions a{display:inline-flex;align-items:center;min-height:28px;line-height:1.2;margin:0;white-space:nowrap}"
                            "input:not([type]),input[type=text],input[type=password],input[type=number],input[type=email],input[type=url],input[type=tel],input[type=search],input[type=date],select{width:100%;height:30px;padding:4px 8px;margin:0;border:1px solid #d7dde2;border-radius:4px;box-sizing:border-box;background:#fbfcfc;color:#202428;font-size:.94rem}"
                            "select{margin:0}"
                            ".panel{border:1px solid #e1e7e5;border-radius:8px;padding:8px 10px;margin:0 0 8px;background:#fff;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".panel h3{padding-bottom:6px;margin-bottom:7px;border-bottom:1px solid #edf0f2}"
                            ".panel-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:7px;padding-bottom:6px;border-bottom:1px solid #edf0f2}"
                            ".panel-head h3{padding:0;margin:0;border:0}"
                            ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:7px 10px}"
                            ".field{display:block;margin:0}"
                            ".field span{display:block;font-size:.82em;color:#56616b;margin-bottom:3px}"
                            ".field input{margin:0}"
                            ".hint{display:block;color:#69727a;font-size:.76em;margin:2px 0 0}"
                            ".status-pill{display:inline-block;padding:1px 7px;border-radius:999px;background:#f2f5f4;color:#4c565d;font-size:.76rem;line-height:1.35;white-space:nowrap}"
                            ".muted{color:#69727a}"
                            ".metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:7px;margin:0 0 10px}"
                            ".metric-card{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:7px 9px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".metric-card span{display:block;color:#69727a;font-size:.8rem;margin-bottom:2px}"
                            ".metric-card strong{display:block;color:#202428;font-size:.94rem;line-height:1.3}"
                            ".filter-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px;margin:0 0 10px}"
                            ".filter-card{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:8px 10px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".filter-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:6px}"
                            ".filter-head strong{color:#111}"
                            ".filter-meta{display:grid;grid-template-columns:1fr 1fr;gap:3px 8px;color:#56616b;font-size:.82rem;margin-top:6px}"
                            ".progress{height:5px;background:#e4e9ed;border-radius:999px;overflow:hidden}"
                            ".progress span{display:block;height:100%;background:#4f8a86;border-radius:999px}"
                            ".check-field{display:block;margin:0}"
                            ".check-title{display:block;font-size:.82em;color:#56616b;margin-bottom:3px}"
                            ".check-line{display:inline-flex;align-items:center;gap:6px;min-height:28px;padding:0 8px;border:1px solid #d7dde2;border-radius:4px;background:#fff;box-sizing:border-box;color:#202428;font-size:.92rem;white-space:nowrap}"
                            ".check-line input[type=checkbox]{margin:0;flex:0 0 auto}"
                            ".form-actions{display:flex;align-items:center;justify-content:flex-start;gap:6px;margin-top:6px;flex-wrap:wrap}"
                            ".form-actions form{margin:0}"
                            "form input[type=submit]{min-height:28px;padding:4px 12px;margin:0;font-size:.9rem;line-height:1.2}"
                            ".form-actions a{display:inline-flex;align-items:center;min-height:28px;padding:0 10px;border:1px solid #d7dde2;border-radius:4px;background:#f7f9fa;color:#355e66;font-size:.9rem;line-height:1.2}"
                            ".form-actions input.secondary{background:#f7f9fa;border-color:#d7dde2;color:#4c565d}"
                            "table{width:100%;border-collapse:collapse;margin:0 0 10px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;overflow:hidden;font-size:.92rem;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "td,th{padding:5px 8px;border-bottom:1px solid #edf0f2;text-align:left;vertical-align:middle}"
                            "th{background:#fafafa;color:#56616b}"
                            "tr:last-child td{border-bottom:0}"
                            "td .form-actions{margin-top:0;gap:5px;flex-wrap:nowrap}"
                            "td .form-actions a,td .form-actions input[type=submit]{min-height:26px;padding:3px 9px;font-size:.86rem}"
                            ".kv{margin:0 0 10px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;box-shadow:0 1px 2px rgba(21,35,34,.04);overflow:hidden}"
                            ".kv th,.kv td{padding:5px 8px;border-bottom:1px solid #edf0f2}.kv th{width:26%;color:#56616b;background:#fbfcfc}"
                            "form[action^='/esp32base'],#f,details,pre{background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:8px 10px;margin:0 0 8px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "form[action^='/esp32base'] input:not([type]),form[action^='/esp32base'] input[type=text],form[action^='/esp32base'] input[type=password],#f input:not([type]){margin:3px 0 7px}"
                            "input[type=file]{margin:3px 8px 7px 0;font-size:.9rem}"
                            "body>h3{background:#fff;border:1px solid #e1e7e5;border-bottom:0;border-radius:8px 8px 0 0;padding:8px 10px;margin:8px 0 0;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "body>h3+p{background:#fff;border-left:1px solid #e1e7e5;border-right:1px solid #e1e7e5;margin:0;padding:0 10px 6px}"
                            "body>h3+p+form[action^='/esp32base/tools']{border-top:0;border-radius:0 0 8px 8px;margin-top:0}"
                            ".ok,.err{display:block;background:#fff;border:1px solid #e1e7e5;border-radius:8px;padding:6px 10px;margin:0 0 8px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            ".footerbar{margin-top:10px;padding:6px 10px;background:#fff;border:1px solid #e1e7e5;border-radius:8px;box-shadow:0 1px 2px rgba(21,35,34,.04)}"
                            "@media(max-width:520px){body{padding:8px 10px 12px}nav{padding:8px 10px}.grid,.metric-grid,.filter-cards{grid-template-columns:1fr}.panel{padding:8px}.form-actions{gap:5px}.kv th{width:34%}}"
                            "</style>");
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

void sendMonthInput(const char* label, const char* name, std::uint32_t days) {
    sendFmt("<label class='field'><span>%s</span><input name='%s' value='%lu'><small class='hint'>按 30 天/月计算</small></label>",
            label,
            name,
            static_cast<unsigned long>(daysToMonths(days)));
}

void sendDateInput(const char* label, const char* name, std::uint32_t seconds) {
    char date[16]{};
    formatDate(seconds >= kMinFilterDateSeconds ? seconds : 0, date, sizeof(date));
    sendFmt("<label class='field'><span>%s</span><input type='date' name='%s' value='%s'></label>",
            label,
            name,
            date);
}

void sendCalibrationTargetInput(std::size_t index, std::uint32_t ml) {
    char value[16]{};
    char name[16]{};
    formatLiterValue(ml, value, sizeof(value));
    std::snprintf(name, sizeof(name), "target%uL", static_cast<unsigned>(index + 1));
    sendFmt("<label class='field'><span>候选容量 %u（L）</span><input name='%s' value='%s'><small class='hint'>0 表示不启用</small></label>",
            static_cast<unsigned>(index + 1),
            name,
            value);
}

void sendCheckbox(const char* label, const char* name, bool checked) {
    sendFmt("<label class='check-field'><span class='check-title'>%s</span><span class='check-line'><input type='checkbox' name='%s' value='1'%s>启用</span></label>",
            label,
            name,
            checked ? " checked" : "");
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
    char currentPreset[16]{};
    std::snprintf(currentPreset, sizeof(currentPreset), "%u", static_cast<unsigned>(snapshot.water.selectedPreset + 1));
    char targetValue[24]{};
    if (snapshot.water.mode == WaterMode::Time) {
        std::snprintf(targetValue, sizeof(targetValue), "%lu 秒", static_cast<unsigned long>(snapshot.water.targetValue));
    } else {
        formatLiters(snapshot.water.targetValue, targetValue, sizeof(targetValue));
    }
    char today[24]{};
    char week[24]{};
    char month[24]{};
    char total[24]{};
    formatLiters(snapshot.statistics.todayMl, today, sizeof(today));
    formatLiters(snapshot.statistics.weekMl, week, sizeof(week));
    formatLiters(snapshot.statistics.monthMl, month, sizeof(month));
    formatLiters(snapshot.statistics.totalMl, total, sizeof(total));

    Esp32BaseWeb::sendChunk("<h2>状态</h2><div class='metric-grid'>");
    sendMetricCard("运行状态", stateText(snapshot.water.state));
    sendMetricCard("当前预设", currentPreset);
    sendMetricCard("目标值", targetValue);
    sendMetricCard("今日用水", today);
    Esp32BaseWeb::sendChunk("</div><h2>统计</h2><div class='metric-grid'>");
    sendMetricCard("今日", today);
    sendMetricCard("本周", week);
    sendMetricCard("本月", month);
    sendMetricCard("总累计", total);
    Esp32BaseWeb::sendChunk("</div><h2>滤芯</h2><div class='filter-cards'>");
    bool anyFilter = false;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        if (!filter.enabled) {
            continue;
        }
        anyFilter = true;
        const std::uint32_t usedDays =
            filter.startTime >= kMinFilterDateSeconds ? g_context.filters->usedDays(i, g_context.nowSeconds()) : 0;
        char life[32]{};
        formatLifeRange(filter, life, sizeof(life));
        char usedFlow[24]{};
        formatLiters(filter.usedMl, usedFlow, sizeof(usedFlow));
        const std::uint32_t progress = filterProgressPercent(filter, usedDays);
        Esp32BaseWeb::sendChunk("<section class='filter-card'><div class='filter-head'><strong>");
        Esp32BaseWeb::writeHtmlEscaped(filter.name);
        sendFmt("</strong><span class='status-pill'>%s</span></div>",
                filterDisplayStatusText(filter, usedDays));
        if (filter.recommendDays > 0) {
            sendFmt("<div class='progress'><span style='width:%lu%%'></span></div>",
                    static_cast<unsigned long>(progress));
            sendFmt("<div class='filter-meta'><span>已用 %lu 天</span><span>寿命 %s</span><span>流量 %s</span><span>进度 %lu%%</span></div></section>",
                    static_cast<unsigned long>(usedDays),
                    life,
                    usedFlow,
                    static_cast<unsigned long>(progress));
        } else {
            Esp32BaseWeb::sendChunk("<p class='hint'>未设置周期</p>");
            sendFmt("<div class='filter-meta'><span>已用 %lu 天</span><span>寿命 %s</span><span>流量 %s</span></div></section>",
                    static_cast<unsigned long>(usedDays),
                    life,
                    usedFlow);
        }
    }
    if (!anyFilter) {
        Esp32BaseWeb::sendChunk("<section class='filter-card muted'>当前没有启用的滤芯。</section>");
    }
    Esp32BaseWeb::sendChunk("</div>");
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
    sendCheckbox("蜂鸣器", "beepEnabled", config.beepEnabled);
    Esp32BaseWeb::sendChunk("</div></section><div class='form-actions'><input type='submit' value='保存'></div></form>");
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
        sendFmt("<section class='panel'><div class='panel-head'><h3>预设 %u</h3><span class='status-pill'>%s</span></div>"
                "<form method='post' action='/api/faucet/presets' onsubmit='return once(this)'>"
                "<input type='hidden' name='index' value='%u'><div class='grid'>",
                static_cast<unsigned>(i + 1),
                preset.enabled ? "启用" : "停用",
                static_cast<unsigned>(i));
        sendCheckbox("启用", "enabled", preset.enabled);
        Esp32BaseWeb::sendChunk("<label class='field'><span>名称</span><input name='name' maxlength='15' value='");
        Esp32BaseWeb::writeHtmlEscaped(preset.name);
        Esp32BaseWeb::sendChunk("'><small class='hint'>最多 15 个字符</small></label><label class='field'><span>类型</span><select name='type'>");
        sendFmt("<option value='volume'%s>容量</option>", preset.type == PresetType::Volume ? " selected" : "");
        sendFmt("<option value='time'%s>时间</option>", preset.type == PresetType::Time ? " selected" : "");
        Esp32BaseWeb::sendChunk("</select></label>");
        if (preset.type == PresetType::Volume) {
            sendVolumeInput("数值（ml）", "value", preset.value);
        } else {
            sendTextInput("数值（秒）", "value", preset.value);
        }
        Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'></div></form></section>");
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
    std::uint32_t requestedPageSize = kDefaultLogPageSize;
    if (getParam("pageSize", text, sizeof(text))) {
        parseU32(text, requestedPageSize);
    }
    const std::uint16_t pageSize = sanitizeLogPageSize(static_cast<std::uint16_t>(requestedPageSize));
    WaterLogRecord records[kMaxLogPageSize]{};
    const bool ready = g_context.logs->ready();
    const std::size_t total = ready ? g_context.logs->count() : 0;
    const std::uint32_t maxPage = total == 0 ? 0 : static_cast<std::uint32_t>((total - 1) / pageSize);
    if (page > maxPage) {
        page = maxPage;
    }
    const std::size_t count = ready ? g_context.logs->readPage(page, pageSize, records, pageSize) : 0;
    Esp32BaseWeb::sendChunk("<h2>记录</h2><form method='get' action='/faucet/logs'><div class='grid'>");
    sendFmt("<label class='field'><span>页码</span><input name='page' value='%lu'></label>",
            static_cast<unsigned long>(page));
    Esp32BaseWeb::sendChunk("<label class='field'><span>每页条数</span><select name='pageSize'>");
    constexpr std::uint16_t sizes[] = {20, 30, 50, 100, 200};
    for (std::uint16_t size : sizes) {
        sendFmt("<option value='%u'%s>%u</option>",
                static_cast<unsigned>(size),
                pageSize == size ? " selected" : "",
                static_cast<unsigned>(size));
    }
    Esp32BaseWeb::sendChunk("</select></label></div><div class='form-actions'><input type='submit' value='查看'>");
    sendFmt("<a href='/faucet/logs?page=0&pageSize=%u'>首页</a>", static_cast<unsigned>(pageSize));
    if (page > 0) {
        sendFmt("<a href='/faucet/logs?page=%lu&pageSize=%u'>上一页</a>",
                static_cast<unsigned long>(page - 1),
                static_cast<unsigned>(pageSize));
    }
    if (page < maxPage) {
        sendFmt("<a href='/faucet/logs?page=%lu&pageSize=%u'>下一页</a>",
                static_cast<unsigned long>(page + 1),
                static_cast<unsigned>(pageSize));
    }
    sendFmt("</div><small class='hint'>第 %lu / %lu 页，共 %lu 条</small></form>"
            "<table><tr><th>时间</th><th>出水量</th><th>时长</th><th>模式</th><th>结果</th></tr>",
            static_cast<unsigned long>(page + 1),
            static_cast<unsigned long>(maxPage + 1),
            static_cast<unsigned long>(total));
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
    Esp32BaseWeb::sendChunk("<h2>滤芯</h2><table><tr><th>名称</th><th>启用</th><th>已用天数</th><th>已用流量</th><th>寿命</th><th>状态</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        const std::uint32_t usedDays = filter.startTime >= kMinFilterDateSeconds ? g_context.filters->usedDays(i, now) : 0;
        char life[32]{};
        formatLifeRange(filter, life, sizeof(life));
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(filter.name);
        sendFmt("</td><td>%s</td><td>%lu</td><td>",
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
        sendFmt("</td><td>%s</td><td><div class='form-actions'><a href='/faucet/filters/edit?index=%u'>设置</a>",
                filterDisplayStatusText(filter, usedDays),
                static_cast<unsigned>(i));
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/faucet/filters/reset' "
                                "onsubmit=\"return confirm('确认重置滤芯？')&&once(this)\">");
        sendFmt("<input type='hidden' name='index' value='%u'>", static_cast<unsigned>(i));
        Esp32BaseWeb::sendChunk("<input class='secondary' type='submit' value='重置'></form></div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
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
    sendFmt("<h2>第 %u 级滤芯设置</h2>"
            "<section class='panel'><form method='post' action='/api/faucet/filters' onsubmit='return once(this)'>"
            "<input type='hidden' name='index' value='%u'><div class='grid'>",
            static_cast<unsigned>(index + 1),
            static_cast<unsigned>(index));
    sendCheckbox("启用", "enabled", filter.enabled);
    Esp32BaseWeb::sendChunk("<label class='field'><span>名称</span><input name='name' maxlength='15' value='");
    Esp32BaseWeb::writeHtmlEscaped(filter.name);
    Esp32BaseWeb::sendChunk("'><small class='hint'>最多 15 个字符</small></label>");
    sendMonthInput("建议更换周期（月）", "recommendMonths", filter.recommendDays);
    sendMonthInput("最长使用周期（月）", "maxMonths", filter.maxDays);
    sendVolumeInput("寿命流量（ml）", "lifeMl", filter.lifeMl);
    sendDateInput("上次更换日期", "startDate", filter.startTime);
    Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'>"
                            "<a href='/faucet/filters'>取消</a></div></form></section>");
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
    Esp32BaseWeb::sendChunk("<h2>校准</h2><section class='panel'><h3>三段式本地校准</h3>"
                            "<p><strong>准备：</strong>在本页配置候选容量和当前每升信号数，本地长按 OK 进入校准，NEXT 选择启用容量。</p>"
                            "<p><strong>采样：</strong>本地按 OK 二次确认并开始出水，到量杯刻度后按 OK 停止采样。</p>"
                            "<p><strong>保存：</strong>至少两次采样一致后，本地按 OK 保存新系数；STOP 可随时取消并关阀。</p>"
                            "<p class='hint'>Web 端只允许查看和手动修改参数，不能远程启动出水校准。</p></section>"
                            "<form method='post' action='/api/faucet/calibration' onsubmit='return once(this)'>"
                            "<section class='panel'><h3>校准参数</h3><div class='grid'>");
    Esp32BaseWeb::sendChunk("<label class='field'><span>每升信号数（脉冲/L）</span><input name='pulsePerLiter' value='");
    sendFmt("%.1f", static_cast<double>(config.pulsePerMl * 1000.0f));
    Esp32BaseWeb::sendChunk("'><small class='hint'>内部按 pulse/ml 保存和计算</small></label>");
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        sendCalibrationTargetInput(i, config.calibrationTargetsMl[i]);
    }
    Esp32BaseWeb::sendChunk("</div></section><div class='form-actions'><input type='submit' value='保存'></div></form>");
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

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month) {
    static constexpr std::uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return month >= 1 && month <= 12 ? days[month - 1] : 0;
}

std::uint32_t daysSince2000(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    static constexpr std::uint16_t kDaysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    std::uint32_t days = 0;
    for (std::uint16_t y = 2000; y < year; ++y) {
        days += isLeapYear(y) ? 366UL : 365UL;
    }
    std::uint32_t value = days + kDaysBeforeMonth[month - 1] + day - 1U;
    if (month > 2 && isLeapYear(year)) {
        ++value;
    }
    return value;
}

bool parseDate(const char* text, std::uint32_t& seconds) {
    if (!text || !*text) {
        seconds = 0;
        return true;
    }
    unsigned year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (std::sscanf(text, "%u-%u-%u", &year, &month, &day) != 3 || year < 2020 || year > 2099 || month < 1 ||
        month > 12) {
        return false;
    }
    const std::uint8_t maxDay = daysInMonth(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month));
    if (day < 1 || day > maxDay) {
        return false;
    }
    seconds = daysSince2000(static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month), static_cast<std::uint8_t>(day)) *
              86400UL;
    return true;
}

void formatDate(std::uint32_t seconds, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (seconds == 0) {
        return;
    }
    std::uint32_t day = seconds / 86400UL;
    std::uint16_t year = 2000;
    while (true) {
        const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
        if (day < yearDays) {
            break;
        }
        day -= yearDays;
        ++year;
    }
    std::uint8_t month = 1;
    while (month <= 12) {
        const std::uint8_t monthDays = daysInMonth(year, month);
        if (day < monthDays) {
            break;
        }
        day -= monthDays;
        ++month;
    }
    std::snprintf(out, len, "%04u-%02u-%02u", static_cast<unsigned>(year), static_cast<unsigned>(month),
                  static_cast<unsigned>(day + 1));
}

bool parseFloat(const char* text, float& value) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(text, &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseLitersToMl(const char* text, std::uint32_t& value) {
    float liters = 0.0f;
    if (!parseFloat(text, liters) || liters < 0.0f) {
        return false;
    }
    if (liters == 0.0f) {
        value = 0;
        return true;
    }
    const float ml = liters * 1000.0f;
    if (ml > static_cast<float>(UINT32_MAX)) {
        return false;
    }
    value = static_cast<std::uint32_t>(ml + 0.5f);
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
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        char text[24]{};
        std::uint32_t index = 0;
        if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kFilterCount) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_index\"}");
            return;
        }

        FilterRecord record = g_context.filters->record(index);
        record.enabled = Esp32BaseWeb::hasParam("enabled");
        Esp32BaseWeb::getParam("name", record.name, sizeof(record.name));
        std::uint32_t months = 0;
        if (getParam("recommendMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.recommendDays = monthsToDays(months);
        }
        if (getParam("maxMonths", text, sizeof(text)) && parseU32(text, months)) {
            record.maxDays = monthsToDays(months);
        }
        applyU32Param("lifeMl", record.lifeMl);
        if (getParam("startDate", text, sizeof(text)) && !parseDate(text, record.startTime)) {
            Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid_date\"}");
            return;
        }
        record.name[kNameLength - 1] = '\0';

        g_context.config->filters[index] = record;
        sanitizeConfig(*g_context.config);
        if (!g_context.filters->updateFilter(index, g_context.config->filters[index])) {
            Esp32BaseWeb::sendJson(500, "{\"error\":\"update_failed\"}");
            return;
        }
        const bool ok = g_context.configStore->saveSystemConfig(*g_context.config) &&
                        g_context.configStore->saveFilterRuntime(g_context.filters->records());
        Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?saved=1" : "/faucet/filters?error=save_failed");
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

    const std::uint32_t now = g_context.nowSeconds();
    if (!g_context.filters->resetFilter(index, now >= kMinFilterDateSeconds ? now : 0)) {
        Esp32BaseWeb::sendJson(500, "{\"error\":\"reset_failed\"}");
        return;
    }
    const bool ok = g_context.configStore->saveFilterRuntime(g_context.filters->records());
    Esp32BaseWeb::redirectSeeOther(ok ? "/faucet/filters?reset=1" : "/faucet/filters?error=save_failed");
}

void handleCalibrationApi() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        char text[24]{};
        float pulsePerLiter = 0.0f;
        if (getParam("pulsePerLiter", text, sizeof(text)) && parseFloat(text, pulsePerLiter)) {
            g_context.config->pulsePerMl = pulsePerLiter / 1000.0f;
        }
        for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
            char name[16]{};
            std::snprintf(name, sizeof(name), "target%uL", static_cast<unsigned>(i + 1));
            std::uint32_t targetMl = 0;
            if (getParam(name, text, sizeof(text)) && parseLitersToMl(text, targetMl)) {
                g_context.config->calibrationTargetsMl[i] = targetMl;
            }
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
        if (std::strcmp(route.path, "/faucet/presets") == 0) {
            return handlePresetsPage;
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
    if (std::strcmp(route.path, "/faucet/filters/edit") == 0) {
        return handleFilterEditPage;
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
