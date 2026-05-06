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

void sendAppLinks() {
    Esp32BaseWeb::sendChunk("<p><a href='/faucet'>Status</a><a href='/faucet/config'>Config</a>"
                            "<a href='/faucet/stats'>Stats</a><a href='/faucet/logs'>Logs</a>"
                            "<a href='/faucet/filters'>Filters</a><a href='/faucet/calibration'>Calibration</a></p>");
}

bool sendPageStart(const char* title) {
    if (!Esp32BaseWeb::checkAuth()) {
        return false;
    }
    Esp32BaseWeb::sendHeader(title);
    sendAppLinks();
    return true;
}

void sendPageEnd() {
    Esp32BaseWeb::sendFooter();
}

const char* stateText(WaterState state) {
    switch (state) {
        case WaterState::Idle:
            return "Idle";
        case WaterState::Confirm:
            return "Confirm";
        case WaterState::Running:
            return "Running";
        case WaterState::Paused:
            return "Paused";
        case WaterState::Error:
            return "Error";
    }
    return "Unknown";
}

const char* modeText(WaterMode mode) {
    switch (mode) {
        case WaterMode::Volume:
            return "Volume";
        case WaterMode::Time:
            return "Time";
        case WaterMode::Calibration:
            return "Calibration";
    }
    return "Unknown";
}

const char* resultText(WaterResult result) {
    switch (result) {
        case WaterResult::Completed:
            return "Completed";
        case WaterResult::StoppedByUser:
            return "Stopped";
        case WaterResult::SafetyStopped:
            return "Safety";
        case WaterResult::FlowError:
            return "Flow error";
        case WaterResult::PauseTimeout:
            return "Pause timeout";
    }
    return "Unknown";
}

void sendTextInput(const char* label, const char* name, unsigned long value) {
    sendFmt("%s<input name='%s' value='%lu'>", label, name, value);
}

void sendCheckbox(const char* label, const char* name, bool checked) {
    sendFmt("<label><input type='checkbox' name='%s' value='1'%s> %s</label><br>",
            name,
            checked ? " checked" : "",
            label);
}

void handleFaucetPage() {
    if (!sendPageStart("Faucet")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const AppSnapshot snapshot = g_context.app->snapshot();
    Esp32BaseWeb::sendChunk("<h2>Status</h2><table>");
    sendFmt("<tr><td>State</td><td>%s</td></tr>", stateText(snapshot.water.state));
    sendFmt("<tr><td>Preset</td><td>%u</td></tr>", static_cast<unsigned>(snapshot.water.selectedPreset + 1));
    sendFmt("<tr><td>Mode</td><td>%s</td></tr>", modeText(snapshot.water.mode));
    sendFmt("<tr><td>Volume</td><td>%lu ml</td></tr>", static_cast<unsigned long>(snapshot.water.volumeMl));
    sendFmt("<tr><td>Target</td><td>%lu</td></tr>", static_cast<unsigned long>(snapshot.water.targetValue));
    sendFmt("<tr><td>Valve</td><td>%s</td></tr>", snapshot.water.valveOpen ? "Open" : "Closed");
    Esp32BaseWeb::sendChunk("</table><h2>Stats</h2><table>");
    sendFmt("<tr><td>Today</td><td>%lu ml</td></tr>", static_cast<unsigned long>(snapshot.statistics.todayMl));
    sendFmt("<tr><td>Week</td><td>%lu ml</td></tr>", static_cast<unsigned long>(snapshot.statistics.weekMl));
    sendFmt("<tr><td>Month</td><td>%lu ml</td></tr>", static_cast<unsigned long>(snapshot.statistics.monthMl));
    sendFmt("<tr><td>Total</td><td>%lu ml</td></tr>", static_cast<unsigned long>(snapshot.statistics.totalMl));
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleConfigPage() {
    if (!sendPageStart("Faucet Config")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const SystemConfig& config = *g_context.config;
    Esp32BaseWeb::sendChunk("<h2>Config</h2><form method='post' action='/api/faucet/config' onsubmit='return once(this)'>");
    sendTextInput("Confirm timeout sec", "confirmTimeoutSec", config.confirmTimeoutSec);
    sendTextInput("Max out time sec", "maxOutTimeSec", config.maxOutTimeSec);
    sendTextInput("Max out volume ml", "maxOutVolumeMl", config.maxOutVolumeMl);
    sendTextInput("Overflow percent", "overflowPercent", config.overflowPercent);
    sendTextInput("No flow timeout sec", "noFlowTimeoutSec", config.noFlowTimeoutSec);
    sendTextInput("High flow ml/min", "highFlowMlPerMin", config.highFlowMlPerMin);
    sendTextInput("High flow duration sec", "highFlowDurationSec", config.highFlowDurationSec);
    sendTextInput("Pause timeout sec", "pauseTimeoutSec", config.pauseTimeoutSec);
    sendTextInput("Valve full power sec", "valveFullPowerSec", config.valveFullPowerSec);
    sendTextInput("Valve hold duty percent", "valveHoldDutyPercent", config.valveHoldDutyPercent);
    sendTextInput("OLED sleep sec", "oledSleepSec", config.oledSleepSec);
    sendCheckbox("Beep enabled", "beepEnabled", config.beepEnabled);
    Esp32BaseWeb::sendChunk("<input type='submit' value='Save'></form>");
    sendPageEnd();
}

void handlePresetsPage() {
    if (!sendPageStart("Faucet Presets")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    Esp32BaseWeb::sendChunk("<h2>Presets</h2>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const PresetConfig& preset = g_context.config->presets[i];
        sendFmt("<h3>Preset %u</h3><form method='post' action='/api/faucet/presets' onsubmit='return once(this)'>"
                "<input type='hidden' name='index' value='%u'>",
                static_cast<unsigned>(i + 1),
                static_cast<unsigned>(i));
        sendCheckbox("Enabled", "enabled", preset.enabled);
        Esp32BaseWeb::sendChunk("Name<input name='name' maxlength='15' value='");
        Esp32BaseWeb::writeHtmlEscaped(preset.name);
        Esp32BaseWeb::sendChunk("'>Type<input name='type' value='");
        Esp32BaseWeb::sendChunk(preset.type == PresetType::Time ? "time" : "volume");
        Esp32BaseWeb::sendChunk("'>");
        sendTextInput("Value", "value", preset.value);
        Esp32BaseWeb::sendChunk("<input type='submit' value='Save'></form>");
    }
    sendPageEnd();
}

void handleStatsPage() {
    if (!sendPageStart("Faucet Stats")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const StatisticsRecord& stats = g_context.app->snapshot().statistics;
    Esp32BaseWeb::sendChunk("<h2>Stats</h2><table>");
    sendFmt("<tr><td>Today</td><td>%lu ml</td></tr>", static_cast<unsigned long>(stats.todayMl));
    sendFmt("<tr><td>Week</td><td>%lu ml</td></tr>", static_cast<unsigned long>(stats.weekMl));
    sendFmt("<tr><td>Month</td><td>%lu ml</td></tr>", static_cast<unsigned long>(stats.monthMl));
    sendFmt("<tr><td>Total</td><td>%lu ml</td></tr>", static_cast<unsigned long>(stats.totalMl));
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleLogsPage() {
    if (!sendPageStart("Faucet Logs")) {
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
    Esp32BaseWeb::sendChunk("<h2>Logs</h2><form method='get' action='/faucet/logs'>Page<input name='page' value='");
    sendFmt("%lu", static_cast<unsigned long>(page));
    Esp32BaseWeb::sendChunk("'><input type='submit' value='Go'></form><table><tr><th>Time</th><th>Volume</th><th>Duration</th><th>Mode</th><th>Result</th></tr>");
    for (std::size_t i = 0; i < count; ++i) {
        sendFmt("<tr><td>%lu</td><td>%lu ml</td><td>%u s</td><td>%s</td><td>%s</td></tr>",
                static_cast<unsigned long>(records[i].startTime),
                static_cast<unsigned long>(records[i].volumeMl),
                static_cast<unsigned>(records[i].durationSec),
                modeText(records[i].mode),
                resultText(records[i].result));
    }
    Esp32BaseWeb::sendChunk("</table>");
    if (!ready) {
        Esp32BaseWeb::sendChunk("<p>Log storage unavailable.</p>");
    }
    sendPageEnd();
}

void handleFiltersPage() {
    if (!sendPageStart("Faucet Filters")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const std::uint32_t now = g_context.nowSeconds();
    Esp32BaseWeb::sendChunk("<h2>Filters</h2><table><tr><th>Name</th><th>Days</th><th>Flow</th><th>Reset</th></tr>");
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(filter.name);
        sendFmt("</td><td>%lu</td><td>%lu ml</td><td>"
                "<form method='post' action='/api/faucet/filters/reset' onsubmit=\"return confirm('Reset filter?')&&once(this)\">"
                "<input type='hidden' name='index' value='%u'><input type='submit' value='Reset'></form></td></tr>",
                static_cast<unsigned long>(g_context.filters->usedDays(i, now)),
                static_cast<unsigned long>(filter.usedMl),
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}

void handleCalibrationPage() {
    if (!sendPageStart("Faucet Calibration")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    const SystemConfig& config = *g_context.config;
    Esp32BaseWeb::sendChunk("<h2>Calibration</h2><form method='post' action='/api/faucet/calibration' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("Pulse per ml<input name='pulsePerMl' value='");
    sendFmt("%.3f", static_cast<double>(config.pulsePerMl));
    Esp32BaseWeb::sendChunk("'>");
    sendTextInput("Target ml", "targetMl", config.calibrationTargetMl);
    Esp32BaseWeb::sendChunk("<input type='submit' value='Save'></form>");
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
