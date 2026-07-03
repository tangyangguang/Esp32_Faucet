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
    std::uint16_t pageSize = sanitizeRecordPageSize(static_cast<std::uint16_t>(requestedPageSize));
    constexpr std::uint16_t kMaxRecordsPagePageSize = 50;
    if (pageSize > kMaxRecordsPagePageSize) {
        pageSize = kMaxRecordsPagePageSize;
    }
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
        Esp32BaseWeb::sendChunk("<p class='err'>当前没有可读取的出水记录。</p>");
    } else if (total == 0) {
        Esp32BaseWeb::sendChunk("<p class='ok'>暂无出水记录。</p>");
    }
    Esp32BaseWeb::sendChunk("<table><tr><th>时间</th><th>模式</th><th>目标</th><th>出水</th>"
                            "<th>用时</th><th>流速</th><th>水温</th><th>TDS</th><th>结果</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < count; ++i) {
        char startTime[40]{};
        formatWaterRecordListTime(records[i], startTime, sizeof(startTime));
        char recordFlow[24]{};
        const std::uint32_t averageFlow = recordFlowMlPerMin(records[i].volumeMl, records[i].durationSec);
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
        sendFmt("<a class='btn-link' href='/faucet/records/detail?start=%lu&boot=%lu&volume=%lu&target=%lu&pulses=%lu&filtered=%lu&duration=%lu&mode=%u&result=%u&preset=%u&temp=%d&tds=%u&sensorSamples=%u&sensorFlags=%u&scheme=%lu'>详情</a>",
                static_cast<unsigned long>(records[i].startTime),
                static_cast<unsigned long>(waterRecordBootId(records[i])),
                static_cast<unsigned long>(records[i].volumeMl),
                static_cast<unsigned long>(records[i].targetValue),
                static_cast<unsigned long>(records[i].pulseCount),
                static_cast<unsigned long>(records[i].filteredPulseCount),
                static_cast<unsigned long>(records[i].durationSec),
                static_cast<unsigned>(records[i].mode),
                static_cast<unsigned>(records[i].result),
                static_cast<unsigned>(records[i].selectedPreset),
                static_cast<int>(records[i].temperatureCentiC),
                static_cast<unsigned>(records[i].tdsPpm),
                static_cast<unsigned>(records[i].sensorSampleCount),
                static_cast<unsigned>(records[i].sensorFlags),
                static_cast<unsigned long>(records[i].meteringSchemeId));
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
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
    if (getParam("filtered", text, sizeof(text)) && parseU32(text, parsed)) {
        record.filteredPulseCount = parsed;
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
    if (getParam("temp", text, sizeof(text)) && parseI16(text, parsedI16)) {
        record.temperatureCentiC = parsedI16;
    }
    if (getParam("tds", text, sizeof(text)) && parseU32(text, parsed)) {
        record.tdsPpm = static_cast<std::uint16_t>(parsed);
    }
    if (getParam("sensorSamples", text, sizeof(text)) && parseU32(text, parsed)) {
        record.sensorSampleCount = parsed > 255U ? 255U : static_cast<std::uint8_t>(parsed);
    }
    if (getParam("sensorFlags", text, sizeof(text)) && parseU32(text, parsed)) {
        record.sensorFlags = static_cast<std::uint16_t>(parsed);
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
            static_cast<unsigned long>(record.filteredPulseCount));
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

#include "FaucetWebCalibrationPages.inc"

#include "FaucetWebFiltersPages.inc"
