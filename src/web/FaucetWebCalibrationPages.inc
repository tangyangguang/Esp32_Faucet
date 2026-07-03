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
            draft.id = 0;
            draft.recordUsed = true;
            std::snprintf(draft.name, sizeof(draft.name), "%s", prefillName);
            draft.params = prefill;
            draft.sourceType = MeteringSchemeSource::Manual;
            draft.createdAt = g_context.nowSeconds ? g_context.nowSeconds() : 0;
        }
        sendManualMeteringParameterPage(hasPrefill ? &draft : nullptr);
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
    bucketSeconds = sanitizeTraceBucketSeconds(bucketSeconds);

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

    sendPulseTraceDetailSections(*trace, buckets.get(), loadedBucketCount, bucketSeconds, detailPath, detailSourceParam);

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
