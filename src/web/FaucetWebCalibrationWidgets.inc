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

void sendManualParameterLink(const char* label, const MeteringSchemeRecord& scheme);

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
    sendManualParameterLink("修改当前参数", scheme);
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

void sendManualMeteringParameterPage(const MeteringSchemeRecord* scheme) {
    const char* title = "手工设置计量参数";
    Esp32BaseWeb::sendHeader(title);
    sendFmt("<h2>%s</h2><p><a class='btn-link' href='/faucet/calibration/flow'>返回流量计校准</a></p>", title);
    sendNoticeFromQuery();
    const MeteringParameters params = scheme ? scheme->params : defaultMeteringParameters();
    const bool hasValues = scheme != nullptr;
    Esp32BaseWeb::sendChunk("<section class='panel calibration-param-panel manual-param-panel'>"
                            "<form id='manualMeteringForm' class='manual-param-form' method='post' action='/faucet/calibration/flow' onsubmit='return once(this)'");
    sendFmt(" data-base-stable-ppl='%lu' data-base-stable-flow='%lu'>",
            static_cast<unsigned long>(params.stablePulsePerLiter),
            static_cast<unsigned long>(params.stableFlowMlPerMin));
    Esp32BaseWeb::sendChunk(
                            "<input type='hidden' name='action' value='create_metering_scheme'>"
                            "<div class='manual-param-section'><h3>参数信息</h3><div class='manual-param-grid'>");
    sendFmt("<label class='compact-field manual-param-field scheme-span-12'><span>名称</span><input name='name' maxlength='%u' required value='",
            static_cast<unsigned>(kMeteringSchemeNameLength - 1));
    if (scheme && scheme->name[0]) {
        sendHtmlAttrEscapedBounded(scheme->name, sizeof(scheme->name));
    } else if (!hasValues) {
        Esp32BaseWeb::sendChunk("手工计量参数");
    }
    Esp32BaseWeb::sendChunk("'></label></div></div>"
                            "<div class='manual-param-section'><h3>容量估算参数</h3><div class='manual-param-grid'>");
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>启动脉冲数</span><input name='startupPulseCount' type='number' min='0' max='%lu' step='1' required value='%lu'><small class='hint'>单位 P，0 表示不使用启动段。</small></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupPulseCount),
            static_cast<unsigned long>(params.startupPulseCount));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>启动水量</span><input name='startupVolumeMl' type='number' min='0' max='%lu' step='1' required value='%lu'><small class='hint'>单位 ml；无启动段时填 0。</small></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupVolumeMl),
            static_cast<unsigned long>(params.startupVolumeMl));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>稳态 P/L</span><input name='stablePulsePerLiter' type='number' min='%lu' max='%lu' step='1' required value='%lu'><small class='hint'>稳定出水阶段每升脉冲数；修改后会联动预计稳态流速。</small></label>",
            static_cast<unsigned long>(kMinSegmentedPulsePerLiter),
            static_cast<unsigned long>(kMaxSegmentedPulsePerLiter),
            static_cast<unsigned long>(params.stablePulsePerLiter));
    Esp32BaseWeb::sendChunk("</div></div>"
                            "<div class='manual-param-section'><h3>时间估算参数</h3>"
                            "<p class='muted'>只用于预计时间和预计流速展示，不参与实际容量、滤芯累计或统计累计。</p>"
                            "<div class='manual-param-grid'>");
    char startupDurationSec[24]{};
    formatStartupDurationSecondsInput(params.startupDurationMs, startupDurationSec, sizeof(startupDurationSec));
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>启动时长</span><input name='startupDurationSec' type='number' min='0' max='%lu' step='0.001' required value='%s'><small class='hint'>单位 秒，可精确到 0.001 秒。</small></label>",
            static_cast<unsigned long>(kMaxSegmentedStartupDurationMs / 1000U),
            startupDurationSec);
    sendFmt("<label class='compact-field manual-param-field scheme-span-4'><span>预计稳态流速</span><input name='stableFlowMlPerMin' type='number' min='%lu' max='%lu' step='1' required value='%lu'><small class='hint'>单位 ml/min，仅用于时间估算。</small></label>",
            static_cast<unsigned long>(kMinStableFlowMlPerMin),
            static_cast<unsigned long>(kMaxStableFlowMlPerMin),
            static_cast<unsigned long>(params.stableFlowMlPerMin));
    Esp32BaseWeb::sendChunk("</div></div>"
                            "<div class='form-actions manual-param-actions'>"
                            "<input class='primary' type='submit' value='保存为当前参数'>"
                            "<a class='btn-link' href='/faucet/calibration/flow'>取消</a></div></form></section>");
    Esp32BaseWeb::sendChunk("<script>"
                            "(function(){var f=document.getElementById('manualMeteringForm');if(!f)return;"
                            "var ppl=f.querySelector('[name=stablePulsePerLiter]'),flow=f.querySelector('[name=stableFlowMlPerMin]');"
                            "var basePpl=Number(f.getAttribute('data-base-stable-ppl')||0),baseFlow=Number(f.getAttribute('data-base-stable-flow')||0),manual=false;"
                            "if(!ppl||!flow||basePpl<=0||baseFlow<=0)return;"
                            "flow.addEventListener('input',function(){manual=true;});"
                            "ppl.addEventListener('input',function(){if(manual)return;var next=Number(ppl.value)||0;if(next<=0)return;var v=Math.round(baseFlow*basePpl/next);if(v>0)flow.value=String(v);});"
                            "})();"
                            "</script>");
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
                            "<a class='btn-link' href='/faucet/calibration/flow?manual=1'>手工设置参数</a></div>"
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
        Esp32BaseWeb::sendChunk("<tr><td colspan='6'>还没有历史参数。可以先手工设置一组参数。</td></tr>");
    }
    if (ready && schemes && count > 1) {
        std::sort(schemes, schemes + count, [](const MeteringSchemeRecord& a, const MeteringSchemeRecord& b) {
            if (a.createdAt != b.createdAt) {
                return a.createdAt > b.createdAt;
            }
            return a.id > b.id;
        });
    }
    for (std::size_t i = 0; i < count; ++i) {
        const MeteringSchemeRecord& scheme = schemes[i];
        char startupVolume[24]{};
        char startupDuration[24]{};
        char stableFlow[24]{};
        char minActual[20]{};
        char maxActual[20]{};
        char maxError[20]{};
        char createdAt[40]{};
        formatLiters(scheme.params.startupVolumeMl, startupVolume, sizeof(startupVolume));
        formatStartupDurationSeconds(scheme.params.startupDurationMs, startupDuration, sizeof(startupDuration));
        formatFlowLitersPerMin(scheme.params.stableFlowMlPerMin, stableFlow, sizeof(stableFlow));
        formatSchemeTimestamp(scheme.createdAt, createdAt, sizeof(createdAt));
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
        sendFmt("</b><small>%s</small><small>创建：%s</small></td><td>",
                meteringSchemeSourceName(scheme.sourceType),
                createdAt);
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
        sendManualParameterLink(scheme.id == activeId ? "修改当前参数" : "复制为当前参数", scheme);
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table>");
    if (ready && count == kMeteringSchemeStoreSlotCount) {
        Esp32BaseWeb::sendChunk("<p class='muted'>历史参数数量已满；下次新建参数会覆盖最早的非当前参数。</p>");
    }
    Esp32BaseWeb::sendChunk("</section>");
    delete[] schemes;
}

std::uint32_t configuredPulseObservationWindowSec() {
    return kDefaultPulseObservationWindowSec;
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
    sendFmt("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class='calibration-value-cell'>%s</td><td>%luP</td><td>",
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
    sendFmt("</td><td><span class='status-pill %s'>%s</span></td><td>本次会话 #%u</td><td><span class='status-pill %s'>%s</span></td><td>",
            stableReady ? "status-ok" : (attempt.status == CalibrationAttemptStatus::Invalid ? "status-warn" : "status-muted"),
            stableReady ? "稳态可用" : (attempt.status == CalibrationAttemptStatus::Invalid ? calibrationInvalidReasonText(attempt.invalidReason) : (savedOnly ? "仅记录" : "-")),
            static_cast<unsigned>(attempt.attemptIndex + 1),
            attempt.status == CalibrationAttemptStatus::Valid ? "status-ok" :
                (attempt.status == CalibrationAttemptStatus::Invalid ? "status-warn" : "status-muted"),
            calibrationAttemptStatusText(attempt.status));
    if (attempt.status == CalibrationAttemptStatus::Valid && attempt.actualMl > 0) {
        sendFmt("<input class='secondary' type='button' value='修改' data-edit-modal='flowActualEditModal' "
                "data-edit-index='%u' data-edit-value='%lu' data-edit-context='样本 #%u，当前实测 %s' "
                "onclick='return faucetOpenCalibrationEdit(this)'>",
                static_cast<unsigned>(attempt.attemptIndex),
                static_cast<unsigned long>(attempt.actualMl),
                static_cast<unsigned>(attempt.attemptIndex + 1U),
                actualText);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void sendCalibrationSamplesPanel(std::uint32_t samplePulseWindowSec) {
    Esp32BaseWeb::sendChunk("<section id='calibration-samples' class='panel'><div class='panel-head'><h3>本次校准样本</h3></div>"
                            "<p class='hint'>这里记录当前校准会话的接水记录；有效样本会自动刷新推荐参数，放弃的样本只作为本次会话记录保留。</p>");
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
    Esp32BaseWeb::sendChunk("</table>"
                            "<div id='flowActualEditModal' class='calibration-edit-modal' role='dialog' aria-modal='true' aria-hidden='true' onclick='faucetCalibrationEditBackdrop(event)'>"
                            "<div class='calibration-edit-card'><div class='panel-head'><h3>修改量杯实测容量</h3></div>"
                            "<p class='calibration-edit-context' data-edit-context></p>"
                            "<form class='calibration-edit-form' method='post' action='/faucet/calibration/flow' onsubmit='return faucetSubmitFlowCalibrationAction(this)'>"
                            "<input type='hidden' name='action' value='update_actual'><input type='hidden' name='attemptIndex' data-edit-index>"
                            "<label class='compact-field'><span>实际容量</span><span class='estimator-input-row'>");
    sendFmt("<input name='actualMl' data-edit-value type='number' min='%lu' max='%lu' step='1' required><span class='unit-label'>mL</span></span></label>",
            static_cast<unsigned long>(kCalibrationMinActualMl),
            static_cast<unsigned long>(kMaxVolumePresetMl));
    Esp32BaseWeb::sendChunk("<div class='form-actions'><input class='secondary' type='button' value='取消' onclick=\"return faucetCloseCalibrationEdit('flowActualEditModal')\">"
                            "<input class='primary' type='submit' value='确定'></div></form></div></div></section>");
}

const char* calibrationSessionStatusCode(CalibrationSessionStatus status) {
    switch (status) {
        case CalibrationSessionStatus::Idle:
            return "idle";
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

void sendMeteringQualityWarnings(std::uint16_t sampleCount,
                                 std::uint32_t minActualMl,
                                 std::uint32_t maxActualMl,
                                 std::uint32_t maxErrorMl,
                                 std::uint16_t maxErrorTenthPercent) {
    const bool sampleFew = sampleCount > 0 && sampleCount < kCalibrationRecommendedSamples;
    const std::uint32_t spanMl = maxActualMl > minActualMl ? maxActualMl - minActualMl : 0;
    const bool spanNarrow = maxActualMl > 0 && minActualMl > 0 && spanMl < kCalibrationRecommendedVolumeSpanMl;
    const bool errorHigh = maxErrorMl > kDefaultCalibrationMaxErrorMl ||
                           maxErrorTenthPercent > kDefaultCalibrationMaxRelativeErrorTenthPercent;
    if (!sampleFew && !spanNarrow && !errorHigh) {
        Esp32BaseWeb::sendChunk("<p class='hint'>质量提示：样本覆盖和误差摘要未发现明显风险。</p>");
        return;
    }
    Esp32BaseWeb::sendChunk("<div class='metering-quality-warnings'>");
    if (sampleFew) {
        sendFmt("<p class='warn'>当前只有 %u 条有效样本，允许应用，但建议补充到 %u 条以上后再长期使用。</p>",
                static_cast<unsigned>(sampleCount),
                static_cast<unsigned>(kCalibrationRecommendedSamples));
    }
    if (spanNarrow) {
        char spanText[24]{};
        formatLiters(spanMl, spanText, sizeof(spanText));
        sendFmt("<p class='warn'>样本容量跨度只有 %s，建议覆盖目标附近和更大容量样本。</p>", spanText);
    }
    if (errorHigh) {
        char maxErrorText[24]{};
        formatLiters(maxErrorMl, maxErrorText, sizeof(maxErrorText));
        sendFmt("<p class='warn'>样本回算最大误差为 %s / %u.%u%%，建议补充样本后再确认。</p>",
                maxErrorText,
                static_cast<unsigned>(maxErrorTenthPercent / 10U),
                static_cast<unsigned>(maxErrorTenthPercent % 10U));
    }
    Esp32BaseWeb::sendChunk("</div>");
}

void sendMeteringCandidateTable(const MeteringParameters& params,
                                MeteringSchemeSource sourceType,
                                std::uint32_t generatedAt,
                                std::uint16_t sampleCount,
                                std::uint32_t minActualMl,
                                std::uint32_t maxActualMl,
                                std::uint32_t maxErrorValueMl,
                                std::uint16_t maxErrorTenthPercent,
                                const char* statusText,
                                const char* statusClass) {
    char startupPulses[24]{};
    char startupVolume[24]{};
    char stablePpl[24]{};
    char startupDuration[24]{};
    char stableFlow[24]{};
    char generatedAtText[40]{};
    char samples[24]{};
    char minActual[24]{};
    char maxActual[24]{};
    char actualRange[56]{};
    char maxErrorText[24]{};
    char maxError[40]{};
    std::snprintf(startupPulses,
                  sizeof(startupPulses),
                  "%luP",
                  static_cast<unsigned long>(params.startupPulseCount));
    formatLiters(params.startupVolumeMl, startupVolume, sizeof(startupVolume));
    std::snprintf(stablePpl,
                  sizeof(stablePpl),
                  "%luP/L",
                  static_cast<unsigned long>(params.stablePulsePerLiter));
    formatStartupDurationSeconds(params.startupDurationMs, startupDuration, sizeof(startupDuration));
    formatFlowLitersPerMin(params.stableFlowMlPerMin, stableFlow, sizeof(stableFlow));
    formatSchemeTimestamp(generatedAt, generatedAtText, sizeof(generatedAtText));
    std::snprintf(samples, sizeof(samples), "%u 条", static_cast<unsigned>(sampleCount));
    formatLiters(minActualMl, minActual, sizeof(minActual));
    formatLiters(maxActualMl, maxActual, sizeof(maxActual));
    if (minActualMl > 0 || maxActualMl > 0) {
        std::snprintf(actualRange, sizeof(actualRange), "%s - %s", minActual, maxActual);
    } else {
        std::snprintf(actualRange, sizeof(actualRange), "-");
    }
    formatLiters(maxErrorValueMl, maxErrorText, sizeof(maxErrorText));
    std::snprintf(maxError,
                  sizeof(maxError),
                  "%s / %u.%u%%",
                  maxErrorText,
                  static_cast<unsigned>(maxErrorTenthPercent / 10U),
                  static_cast<unsigned>(maxErrorTenthPercent % 10U));

    Esp32BaseWeb::sendChunk("<div class='generated-metering-candidate'><div class='panel-head'><div><h4>推荐计量参数</h4>"
                            "<p class='hint'>确认前请核对这组自动生成的参数。</p></div>");
    sendFmt("<span class='status-pill %s'>%s</span></div>"
            "<table class='metering-candidate-table'>"
            "<tr><th>启动脉冲</th><td>%s</td><th>启动水量</th><td>%s</td></tr>"
            "<tr><th>稳态 P/L</th><td>%s</td><th>启动时长</th><td>%s</td></tr>"
            "<tr><th>稳态流速</th><td>%s</td><th>样本数</th><td>%s</td></tr>"
            "<tr><th>容量范围</th><td>%s</td><th>最大误差</th><td>%s</td></tr>"
            "<tr><th>生成来源</th><td>%s</td><th>生成时间</th><td>%s</td></tr>"
            "</table>",
            statusClass ? statusClass : "status-ok",
            statusText ? statusText : "待应用",
            startupPulses,
            startupVolume,
            stablePpl,
            startupDuration,
            stableFlow,
            samples,
            actualRange,
            maxError,
            meteringSchemeSourceName(sourceType),
            generatedAtText);
    sendMeteringQualityWarnings(sampleCount, minActualMl, maxActualMl, maxErrorValueMl, maxErrorTenthPercent);
    Esp32BaseWeb::sendChunk("</div>");
}

void sendGeneratedMeteringCandidatePanel(const MeteringSchemeCandidate& candidate) {
    if (!candidate.ready) {
        return;
    }
    sendMeteringCandidateTable(candidate.params,
                               candidate.sourceType,
                               candidate.generatedAt,
                               candidate.sampleCount,
                               candidate.minActualMl,
                               candidate.maxActualMl,
                               candidate.maxErrorMl,
                               candidate.maxErrorTenthPercent,
                               "待应用",
                               "status-ok");
}

bool loadAppliedCalibrationScheme(MeteringSchemeRecord& scheme) {
    if (!g_context.calibrationSessions || !g_context.calibrationSessions->ready() || !ensureMeteringSchemesReady()) {
        return false;
    }
    CalibrationSessionRecord session{};
    if (!g_context.calibrationSessions->load(session) || session.status != CalibrationSessionStatus::Applied ||
        session.appliedSchemeId == 0) {
        return false;
    }
    return g_context.meteringSchemes->findById(session.appliedSchemeId, scheme);
}

void sendAppliedMeteringCandidatePanel(const MeteringSchemeRecord& scheme) {
    sendMeteringCandidateTable(scheme.params,
                               scheme.sourceType,
                               scheme.createdAt,
                               scheme.sampleCount,
                               scheme.minActualMl,
                               scheme.maxActualMl,
                               scheme.maxErrorMl,
                               scheme.maxErrorTenthPercent,
                               "已应用",
                               "status-muted");
}

void sendFlowCalibrationSessionPanel(const AppSnapshot& snapshot, bool taskActive) {
    const bool sessionActive = snapshot.calibrationStatus != CalibrationSessionStatus::Idle &&
                               snapshot.calibrationStatus != CalibrationSessionStatus::Applied &&
                               snapshot.calibrationStatus != CalibrationSessionStatus::Discarded &&
                               snapshot.calibrationStatus != CalibrationSessionStatus::Failed;
    const bool canStartSession = calibrationSessionInactive(snapshot.calibrationStatus) && !taskActive;
    const bool canDiscardSession = sessionActive && snapshot.calibrationStatus != CalibrationSessionStatus::Running && !taskActive;
    const bool canEnterActual = snapshot.calibrationStatus == CalibrationSessionStatus::AwaitingActual;
    const bool hasCandidate = snapshot.calibrationCandidate.ready;
    const bool canApply =
        (snapshot.calibrationStatus == CalibrationSessionStatus::Generated ||
         snapshot.calibrationStatus == CalibrationSessionStatus::Applied) &&
        hasCandidate && !taskActive;
    const bool canRegenerate =
        (snapshot.calibrationStatus == CalibrationSessionStatus::WaitingLocalRun ||
         snapshot.calibrationStatus == CalibrationSessionStatus::ReadyToGenerate ||
         snapshot.calibrationStatus == CalibrationSessionStatus::Generated ||
         snapshot.calibrationStatus == CalibrationSessionStatus::Applied) &&
        snapshot.calibrationValidSampleCount >= kCalibrationMinQuickSamples && !taskActive;
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
    } else if (snapshot.calibrationStatus == CalibrationSessionStatus::Applied) {
        Esp32BaseWeb::sendChunk("<p class='ok'>这组推荐参数已应用；可以用保留样本重新生成参数，或开始新校准。</p>");
    }
    Esp32BaseWeb::sendChunk("<div class='form-actions calibration-primary-actions'>");
    if (!sessionActive) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit='return once(this)'>"
                                "<input type='hidden' name='action' value='start_session'>");
        sendFmt("<input class='primary' type='submit' value='%s'%s></form>",
                snapshot.calibrationStatus == CalibrationSessionStatus::Applied ? "开始新校准" : "开始校准",
                canStartSession ? "" : " disabled");
    } else {
        Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit=\"return confirm('确认放弃本次校准会话？')&&once(this)\">"
                                "<input type='hidden' name='action' value='discard_session'>");
        sendFmt("<input class='secondary' type='submit' value='放弃本次校准'%s></form>",
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
    } else if (snapshot.calibrationStatus == CalibrationSessionStatus::Applied) {
        MeteringSchemeRecord applied{};
        if (loadAppliedCalibrationScheme(applied)) {
            sendAppliedMeteringCandidatePanel(applied);
        } else {
            Esp32BaseWeb::sendChunk("<div class='generated-metering-candidate'><div class='panel-head'><div><h4>推荐计量参数</h4>"
                                    "<p class='hint'>已应用参数记录不可查看，可能已被历史参数轮转覆盖。</p></div>"
                                    "<span class='status-pill status-muted'>已应用</span></div></div>");
        }
        if (hasCandidate) {
            sendGeneratedMeteringCandidatePanel(snapshot.calibrationCandidate);
        }
    }
    if (canRegenerate || hasCandidate) {
        Esp32BaseWeb::sendChunk("<div class='form-actions calibration-secondary-actions'>");
        if (canRegenerate) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit='return once(this)'>"
                                    "<input type='hidden' name='action' value='regenerate_session'>");
            sendFmt("<input class='secondary' type='submit' value='%s'></form>",
                    hasCandidate ? "再次重新生成" : "根据现有样本重新生成参数");
        }
        if (hasCandidate) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/faucet/calibration/flow' onsubmit=\"return confirm('确认放弃这组待应用参数？')&&once(this)\">"
                                    "<input type='hidden' name='action' value='discard_candidate'>"
                                    "<input class='secondary' type='submit' value='放弃这组参数'></form>");
        }
        Esp32BaseWeb::sendChunk("</div>");
    }
    if (snapshot.calibrationStatus == CalibrationSessionStatus::Generated ||
        (snapshot.calibrationStatus == CalibrationSessionStatus::Applied && hasCandidate)) {
        Esp32BaseWeb::sendChunk("<div class='form-actions calibration-secondary-actions'>"
                                "<form method='post' action='/faucet/calibration/flow' onsubmit=\"return confirm('确认使用这组计量参数？')&&once(this)\">"
                                "<input type='hidden' name='action' value='apply_session'>");
        sendFmt("<input class='primary' type='submit' value='使用这组参数'%s></form></div>",
                canApply ? "" : " disabled");
    }
    Esp32BaseWeb::sendChunk("</section>");
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
    const bool negative = value.value < 0;
    const std::int32_t absCenti = negative ? -value.value : value.value;
    const std::int32_t tenths = (absCenti + 5) / 10;
    std::snprintf(out,
                  len,
                  "%s%ld.%ld",
                  negative ? "-" : "",
                  static_cast<long>(tenths / 10),
                  static_cast<long>(tenths % 10));
}

void formatWaterRecordTemperature(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (record.sensorSampleCount == 0 ||
        (record.sensorFlags &
         (kWaterSensorFlagAdcOffline | kWaterSensorFlagTempInvalid | kWaterSensorFlagTempUnavailable)) != 0) {
        std::snprintf(out, len, "--");
        return;
    }
    formatCentiTemperature(record.temperatureCentiC, out, len);
}

void formatWaterRecordTds(const WaterRecord& record, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (record.sensorSampleCount == 0 ||
        (record.sensorFlags &
         (kWaterSensorFlagAdcOffline | kWaterSensorFlagTdsAdcOverflow | kWaterSensorFlagTdsInvalid |
          kWaterSensorFlagTdsUnavailable)) != 0) {
        std::snprintf(out, len, "--");
        return;
    }
    std::snprintf(out, len, "%u ppm", static_cast<unsigned>(record.tdsPpm));
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

const char* recordTdsCalibrationText(const WaterRecord& record) {
    if (record.sensorSampleCount == 0) {
        return "--";
    }
    if ((record.sensorFlags & kWaterSensorFlagTdsUncalibrated) == 0) {
        return "已校准";
    }
    return "未校准";
}

void sendWaterRecordSensorRows(const WaterRecord& record) {
    char temperature[24]{};
    char tds[24]{};
    formatWaterRecordTemperature(record, temperature, sizeof(temperature));
    formatWaterRecordTds(record, tds, sizeof(tds));

    Esp32BaseWeb::sendChunk("<section class='panel record-detail-card'><h3>水质传感器</h3><table class='kv'>");
    if (record.sensorSampleCount == 0) {
        Esp32BaseWeb::sendChunk("<tr><th>水温</th><td>--</td></tr>"
                                "<tr><th>TDS</th><td>--</td></tr>"
                                "<tr><th>窗口样本数</th><td>0</td></tr>");
    } else {
        sendFmt("<tr><th>水温</th><td>%s</td></tr>"
                "<tr><th>TDS</th><td>%s</td></tr>"
                "<tr><th>窗口样本数</th><td>%u</td></tr>"
                "<tr><th>TDS 校准</th><td>%s</td></tr>"
                "<tr><th>传感器标志</th><td>0x%04x</td></tr>",
                temperature,
                tds,
                static_cast<unsigned>(record.sensorSampleCount),
                recordTdsCalibrationText(record),
                static_cast<unsigned>(record.sensorFlags));
    }
    Esp32BaseWeb::sendChunk("</table></section>");
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
    const bool enabled = config.temperatureKind == TemperatureKind::NtcBeta;
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
    const bool enabled = config.tdsKind == TdsKind::AnalogTdsAo;
    Esp32BaseWeb::sendChunk("<section class='panel calibration-center-card'><div class='panel-head'><h3>水质校准</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            enabled ? (config.tdsCalibrated ? "status-ok" : "status-warn") : "status-muted",
            enabled ? (config.tdsCalibrated ? "已校准" : "未校准") : "未启用");
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    sendFmt("<div><span>当前 TDS</span><strong>%s</strong></div>", enabled ? tdsText : "未启用");
    sendFmt("<div><span>采样电压</span><strong>%s</strong></div>", enabled ? voltageText : "-");
    sendFmt("<div><span>水温补偿</span><strong>%s</strong></div>",
            config.tdsTemperatureCompensationEnabled ? tempText : "关闭");
    sendFmt("<div><span>比例</span><strong>%.3f</strong></div>", static_cast<double>(config.tdsScale));
    sendFmt("<div><span>修正</span><strong>%d ppm</strong></div>", static_cast<int>(config.tdsOffsetPpm));
    Esp32BaseWeb::sendChunk("</div><p><a class='btn-link primary' href='/faucet/calibration?view=tds'>进入水质校准</a></p></section>");
}

void sendCalibrationCenterInputVoltageCard(const AppSnapshot& snapshot, const SystemConfig& config) {
    const InputVoltageCalibration& calibration = config.inputVoltageCalibration;
    Esp32BaseWeb::sendChunk("<section class='panel calibration-center-card'><div class='panel-head'><h3>输入电压校准</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            calibration.calibrated ? "status-ok" : "status-warn",
            calibration.calibrated ? "已校准" : "未校准");
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    if (snapshot.sensors.inputVoltageMv.valid) {
        sendFmt("<div><span>当前输入</span><strong>%ld.%03ld V</strong></div>",
                static_cast<long>(snapshot.sensors.inputVoltageMv.value / 1000),
                static_cast<long>(snapshot.sensors.inputVoltageMv.value % 1000));
    } else {
        Esp32BaseWeb::sendChunk("<div><span>当前输入</span><strong>-- V</strong></div>");
    }
    sendFmt("<div><span>校准点</span><strong>%u / %u</strong></div>",
            static_cast<unsigned>(calibration.pointCount),
            static_cast<unsigned>(kInputVoltageCalibrationMaxPoints));
    Esp32BaseWeb::sendChunk("</div><p><a class='btn-link primary' href='/faucet/calibration?view=voltage'>进入输入电压校准</a></p></section>");
}

void sendInputVoltageCurrentPanel(const AppSnapshot& snapshot, const SystemConfig& config) {
    const InputVoltageCalibration& calibration = config.inputVoltageCalibration;
    const bool canCapture = !waterTaskActive() && snapshot.sensors.inputVoltageStable &&
                            calibration.pointCount < kInputVoltageCalibrationMaxPoints;
    Esp32BaseWeb::sendChunk("<section id='voltage-calibration-reading' class='panel'");
    if (!snapshot.sensors.inputVoltageStable &&
        calibration.pointCount < kInputVoltageCalibrationMaxPoints) {
        Esp32BaseWeb::sendChunk(
            " data-sensor-calibration-refresh='/faucet/calibration?partial=voltage-reading'");
    }
    Esp32BaseWeb::sendChunk("><div class='panel-head'><h3>当前读数</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div><div class='tds-calibration-summary'>",
            calibration.calibrated ? "status-ok" : "status-warn",
            calibration.calibrated ? "已校准" : "未校准");
    if (snapshot.sensors.inputVoltageAdcRaw.valid) {
        sendFmt("<div><span>ADC 原始值</span><strong>%ld</strong></div>",
                static_cast<long>(snapshot.sensors.inputVoltageAdcRaw.value));
        sendFmt("<div><span>ADC 电压</span><strong>%ld mV</strong></div>",
                static_cast<long>(snapshot.sensors.inputVoltageAdcMv.value));
        sendFmt("<div><span>理论输入</span><strong>%ld.%03ld V</strong></div>",
                static_cast<long>(snapshot.sensors.inputVoltageTheoreticalMv.value / 1000),
                static_cast<long>(snapshot.sensors.inputVoltageTheoreticalMv.value % 1000));
        sendFmt("<div><span>当前输入</span><strong>%ld.%03ld V</strong></div>",
                static_cast<long>(snapshot.sensors.inputVoltageMv.value / 1000),
                static_cast<long>(snapshot.sensors.inputVoltageMv.value % 1000));
    } else {
        Esp32BaseWeb::sendChunk("<div><span>ADC 原始值</span><strong>--</strong></div><div><span>当前输入</span><strong>-- V</strong></div>");
    }
    sendFmt("<div><span>采样状态</span><strong>%s（%u 次，波动 %lu mV）</strong></div>",
            snapshot.sensors.inputVoltageStable ? "稳定" : "等待稳定",
            static_cast<unsigned>(snapshot.sensors.inputVoltageSampleCount),
            static_cast<unsigned long>(snapshot.sensors.inputVoltageWindowSpanMv));
    sendFmt("<div><span>换算参数</span><strong>比例 %.4f，修正 %ld mV</strong></div>",
            static_cast<double>(calibration.gainPpm) / 1000000.0,
            static_cast<long>(calibration.offsetMillivolts));
    Esp32BaseWeb::sendChunk("</div><p class='hint'>系统使用最近 5 次（约 5 秒）ADC 原始值的中位数；波动超限时等待稳定，不允许保存。用万用表测量输入端实际电压后录入即可。</p>"
                            "<form method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='voltage_save_point'>"
                            "<label class='compact-field'><span>万用表实际电压</span><span class='estimator-input-row'>"
                            "<input name='actualVoltage' type='number' min='1' max='60' step='0.001' required><span class='unit-label'>V</span></span></label><div class='form-actions'>");
    sendFmt("<input class='primary' type='submit' value='保存当前校准点'%s>",
            canCapture ? "" : " disabled");
    Esp32BaseWeb::sendChunk("</div></form></section>");
}

void handleInputVoltageCalibrationPage() {
    const AppSnapshot snapshot = g_context.app->snapshot();
    const SystemConfig& config = *g_context.config;
    const InputVoltageCalibration& calibration = config.inputVoltageCalibration;
    Esp32BaseWeb::sendHeader("输入电压校准");
    Esp32BaseWeb::sendChunk("<h2>输入电压校准</h2><p><a class='btn-link' href='/faucet/calibration'>返回校准中心</a></p>");
    sendNoticeFromQuery();
    sendInputVoltageCurrentPanel(snapshot, config);
    Esp32BaseWeb::sendChunk("<section class='panel'><div class='panel-head'><h3>已保存校准点</h3>");
    sendFmt("<span class='status-pill status-muted'>%u / %u</span></div>",
            static_cast<unsigned>(calibration.pointCount),
            static_cast<unsigned>(kInputVoltageCalibrationMaxPoints));
    if (calibration.pointCount == 0) {
        Esp32BaseWeb::sendChunk("<p class='hint'>尚无校准点，当前首页电压会标记为参考值。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<table><tr><th>#</th><th>ADC 原始值</th><th>ADC mV</th><th>理论电压</th><th>实际电压</th><th>操作</th></tr>");
        for (std::uint8_t i = 0; i < calibration.pointCount; ++i) {
            const InputVoltageCalibrationPoint& point = calibration.points[i];
            sendFmt("<tr><td>%u</td><td>%d<br><small>%d~%d</small></td><td>%lu</td><td>%lu.%03lu V</td>"
                    "<td class='calibration-value-cell'>%lu.%03lu V</td><td><div class='row-actions'>"
                    "<input class='secondary' type='button' value='修改' data-edit-modal='voltagePointEditModal' "
                    "data-edit-index='%u' data-edit-value='%lu.%03lu' data-edit-context='当前实际电压 %lu.%03lu V，理论电压 %lu.%03lu V' "
                    "onclick='return faucetOpenCalibrationEdit(this)'>"
                    "<form method='post' action='/faucet/calibration' onsubmit=\"return confirm('确认删除这个输入电压校准点？')&&once(this)\">"
                    "<input type='hidden' name='action' value='voltage_remove_point'><input type='hidden' name='index' value='%u'>"
                    "<input class='secondary' type='submit' value='删除'></form></div></td></tr>",
                    static_cast<unsigned>(i + 1U),
                    static_cast<int>(point.adcRaw),
                    static_cast<int>(point.adcRawMin),
                    static_cast<int>(point.adcRawMax),
                    static_cast<unsigned long>(point.adcMillivolts),
                    static_cast<unsigned long>(point.theoreticalInputMillivolts / 1000U),
                    static_cast<unsigned long>(point.theoreticalInputMillivolts % 1000U),
                    static_cast<unsigned long>(point.actualInputMillivolts / 1000U),
                    static_cast<unsigned long>(point.actualInputMillivolts % 1000U),
                    static_cast<unsigned>(i),
                    static_cast<unsigned long>(point.actualInputMillivolts / 1000U),
                    static_cast<unsigned long>(point.actualInputMillivolts % 1000U),
                    static_cast<unsigned long>(point.actualInputMillivolts / 1000U),
                    static_cast<unsigned long>(point.actualInputMillivolts % 1000U),
                    static_cast<unsigned long>(point.theoreticalInputMillivolts / 1000U),
                    static_cast<unsigned long>(point.theoreticalInputMillivolts % 1000U),
                    static_cast<unsigned>(i));
        }
        Esp32BaseWeb::sendChunk("</table><form method='post' action='/faucet/calibration' onsubmit=\"return confirm('确认清空全部输入电压校准点？')&&once(this)\">"
                                "<input type='hidden' name='action' value='voltage_clear'><div class='form-actions'><input class='secondary' type='submit' value='清空全部'></div></form>");
    }
    Esp32BaseWeb::sendChunk("<div id='voltagePointEditModal' class='calibration-edit-modal' role='dialog' aria-modal='true' aria-hidden='true' onclick='faucetCalibrationEditBackdrop(event)'>"
                            "<div class='calibration-edit-card'><div class='panel-head'><h3>修改实际电压</h3></div>"
                            "<p class='calibration-edit-context' data-edit-context></p>"
                            "<form class='calibration-edit-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='voltage_update_point'><input type='hidden' name='index' data-edit-index>"
                            "<label class='compact-field'><span>万用表实际电压</span><span class='estimator-input-row'>"
                            "<input name='actualVoltage' data-edit-value type='number' min='1' max='60' step='0.001' required><span class='unit-label'>V</span></span></label>"
                            "<div class='form-actions'><input class='secondary' type='button' value='取消' onclick=\"return faucetCloseCalibrationEdit('voltagePointEditModal')\">"
                            "<input class='primary' type='submit' value='确定'></div></form></div></div></section>");
    sendCalibrationPageScript();
    sendPageEnd();
}

void sendTdsCalibrationPointsTable(const TdsCalibrationSessionSnapshot& session) {
    Esp32BaseWeb::sendChunk("<h4>已保存校准点</h4><table><tr><th>#</th><th>参考 ppm</th><th>原始 ppm</th><th>采样电压</th><th>操作</th></tr>");
    if (session.pointCount == 0) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='5'>暂无校准点</td></tr>");
    }
    for (std::uint8_t i = 0; i < session.pointCount && i < kTdsCalibrationMaxPoints; ++i) {
        const TdsCalibrationPointSnapshot& point = session.points[i];
        sendFmt("<tr><td>%u</td><td class='calibration-value-cell'>%u ppm</td><td>%u</td><td>%u mV</td><td><div class='row-actions'>"
                "<input class='secondary' type='button' value='修改' data-edit-modal='tdsPointEditModal' "
                "data-edit-index='%u' data-edit-value='%u' data-edit-context='当前参考值 %u ppm，原始读数 %u ppm' "
                "onclick='return faucetOpenCalibrationEdit(this)'>"
                "<form method='post' action='/faucet/calibration' onsubmit=\"return confirm('确认删除这个 TDS 校准点？')&&once(this)\">"
                "<input type='hidden' name='action' value='tds_remove_point'>"
                "<input type='hidden' name='index' value='%u'>"
                "<input class='secondary' type='submit' value='删除'></form></div></td></tr>",
                static_cast<unsigned>(i + 1U),
                static_cast<unsigned>(point.referencePpm),
                static_cast<unsigned>(point.rawPpm),
                static_cast<unsigned>(point.voltageMv),
                static_cast<unsigned>(i),
                static_cast<unsigned>(point.referencePpm),
                static_cast<unsigned>(point.referencePpm),
                static_cast<unsigned>(point.rawPpm),
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>"
                            "<div id='tdsPointEditModal' class='calibration-edit-modal' role='dialog' aria-modal='true' aria-hidden='true' onclick='faucetCalibrationEditBackdrop(event)'>"
                            "<div class='calibration-edit-card'><div class='panel-head'><h3>修改参考 TDS</h3></div>"
                            "<p class='calibration-edit-context' data-edit-context></p>"
                            "<form class='calibration-edit-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>"
                            "<input type='hidden' name='action' value='tds_update_point'><input type='hidden' name='index' data-edit-index>"
                            "<label class='compact-field'><span>参考 TDS</span><span class='estimator-input-row'>"
                            "<input name='referencePpm' data-edit-value type='number' min='0' max='2000' step='1' required><span class='unit-label'>ppm</span></span></label>"
                            "<div class='form-actions'><input class='secondary' type='button' value='取消' onclick=\"return faucetCloseCalibrationEdit('tdsPointEditModal')\">"
                            "<input class='primary' type='submit' value='确定'></div></form></div></div>");
}

void sendTdsCalibrationStep(const TdsCalibrationSessionSnapshot& session,
                            bool enabled,
                            bool canWrite) {
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
}

void sendTdsCalibrationLivePanel(const TdsCalibrationSessionSnapshot& session,
                                 bool enabled,
                                 bool canWrite) {
    Esp32BaseWeb::sendChunk("<div id='tds-calibration-live'");
    if (session.samplingActive && !session.readyToSave) {
        Esp32BaseWeb::sendChunk(
            " data-sensor-calibration-refresh='/faucet/calibration?partial=tds-live'");
    }
    Esp32BaseWeb::sendChunk("><div class='panel-head'><h3>本次校准</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            session.samplingActive ? "status-warn" : (session.sessionActive ? "status-ok" : "status-muted"),
            session.samplingActive ? "采样中" : (session.sessionActive ? "已开始" : "未开始"));
    sendTdsCalibrationStep(session, enabled, canWrite);
    Esp32BaseWeb::sendChunk("</div>");
}

void handleTemperatureCalibrationPage() {
    const AppSnapshot snapshot = g_context.app->snapshot();
    const SystemConfig& config = *g_context.config;
    char rawText[24]{};
    char calibratedText[24]{};
    formatSensorTemperature(snapshot.sensors.temperatureRawCentiC, rawText, sizeof(rawText));
    formatSensorTemperature(snapshot.sensors.temperatureCentiC, calibratedText, sizeof(calibratedText));
    const bool enabled = config.temperatureKind == TemperatureKind::NtcBeta;
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
    const bool enabled = config.tdsKind == TdsKind::AnalogTdsAo;
    const bool canWrite = enabled && !waterTaskActive();

    Esp32BaseWeb::sendHeader("水质校准");
    Esp32BaseWeb::sendChunk("<h2>水质校准</h2><p><a class='btn-link' href='/faucet/calibration'>返回校准中心</a></p>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<section class='panel tds-calibration-panel'><div class='panel-head'><h3>当前水质状态</h3>");
    sendFmt("<span class='status-pill %s'>%s</span></div>",
            enabled ? (config.tdsCalibrated ? "status-ok" : "status-warn") : "status-muted",
            enabled ? (config.tdsCalibrated ? "已校准" : "未校准") : "未启用");
    Esp32BaseWeb::sendChunk("<div class='tds-calibration-summary'>");
    sendFmt("<div><span>当前 TDS</span><strong>%s</strong></div>", enabled ? tdsText : "未启用");
    sendFmt("<div><span>采样电压</span><strong>%s</strong></div>", enabled ? voltageText : "-");
    sendFmt("<div><span>水温补偿</span><strong>%s</strong></div>",
            config.tdsTemperatureCompensationEnabled ? tempText : "关闭");
    sendFmt("<div><span>比例</span><strong>%.3f</strong></div>", static_cast<double>(config.tdsScale));
    sendFmt("<div><span>修正</span><strong>%d ppm</strong></div>", static_cast<int>(config.tdsOffsetPpm));
    Esp32BaseWeb::sendChunk("</div></section><section class='panel tds-point-session'>");
    sendTdsCalibrationLivePanel(session, enabled, canWrite);
    sendTdsCalibrationPointsTable(session);
    Esp32BaseWeb::sendChunk("<div class='tds-workflow-card'><h4>自动生成结果</h4>");
    if (session.candidateReady) {
        sendFmt("<p>点数 %u，参考跨度 %u ppm，原始跨度 %u ppm，比例 %.3f，修正 %d ppm。</p>",
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
