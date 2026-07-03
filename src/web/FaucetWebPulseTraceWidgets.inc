std::uint32_t sanitizeTraceBucketSeconds(std::uint32_t bucketSeconds) {
    return (bucketSeconds == 2 || bucketSeconds == 3 || bucketSeconds == 4 || bucketSeconds == 5) ? bucketSeconds : 1;
}

std::uint32_t estimateVolumeMlForPulsesForWeb(const MeteringParameters& params, std::uint32_t pulses) {
    if (pulses == 0 || params.stablePulsePerLiter == 0) {
        return 0;
    }
    if (params.startupPulseCount > 0 && pulses <= params.startupPulseCount) {
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(pulses) * params.startupVolumeMl +
                                          params.startupPulseCount / 2ULL) /
                                         params.startupPulseCount);
    }
    const std::uint32_t stablePulses =
        pulses > params.startupPulseCount ? pulses - params.startupPulseCount : pulses;
    const std::uint64_t stableMl =
        (static_cast<std::uint64_t>(stablePulses) * 1000ULL + params.stablePulsePerLiter / 2ULL) /
        params.stablePulsePerLiter;
    const std::uint64_t total = static_cast<std::uint64_t>(params.startupVolumeMl) + stableMl;
    return total > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(total);
}

void sendTraceFrequencyLinks(const char* detailPath,
                             const char* detailSourceParam,
                             std::uint32_t bucketSeconds) {
    constexpr std::uint32_t bucketsToShow[] = {1, 2, 3, 4, 5};
    Esp32BaseWeb::sendChunk("<div class='row-actions trace-frequency'><span class='trace-frequency-label'>聚合</span>");
    for (std::uint32_t bucket : bucketsToShow) {
        const char* linkClass = bucket == bucketSeconds ? "btn-link page-current" : "btn-link";
        sendFmt("<a class='%s' href='", linkClass);
        sendFmt("%s?%s&bucket=%lu", detailPath, detailSourceParam, static_cast<unsigned long>(bucket));
        sendFmt("'>%lus</a>", static_cast<unsigned long>(bucket));
    }
    Esp32BaseWeb::sendChunk("</div>");
}

void sendPulseTraceChartPath(const WaterPulseTraceBucket* buckets,
                             std::size_t bucketCount,
                             std::uint32_t maxValue,
                             const char* cssClass) {
    if (!buckets || bucketCount == 0 || maxValue == 0) {
        return;
    }
    constexpr std::uint32_t kLeft = 54;
    constexpr std::uint32_t kTop = 20;
    constexpr std::uint32_t kPlotWidth = 922;
    constexpr std::uint32_t kPlotHeight = 220;
    const std::size_t denom = bucketCount > 1 ? bucketCount - 1 : 1;
    sendFmt("<path class='%s' d='", cssClass);
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t x = kLeft + static_cast<std::uint32_t>((i * kPlotWidth) / denom);
        const std::uint32_t y = kTop + kPlotHeight -
                                static_cast<std::uint32_t>((static_cast<std::uint64_t>(buckets[i].pulseDelta) *
                                                            kPlotHeight) /
                                                           maxValue);
        sendFmt("%c%lu %lu", i == 0 ? 'M' : 'L', static_cast<unsigned long>(x), static_cast<unsigned long>(y));
    }
    Esp32BaseWeb::sendChunk("'/>");
}

void sendPulseTraceVolumePath(const WaterPulseTraceBucket* buckets,
                              std::size_t bucketCount,
                              const MeteringParameters& params,
                              std::uint32_t maxVolumeMl) {
    if (!buckets || bucketCount == 0 || maxVolumeMl == 0 || params.stablePulsePerLiter == 0) {
        return;
    }
    constexpr std::uint32_t kLeft = 54;
    constexpr std::uint32_t kTop = 20;
    constexpr std::uint32_t kPlotWidth = 922;
    constexpr std::uint32_t kPlotHeight = 220;
    const std::size_t denom = bucketCount > 1 ? bucketCount - 1 : 1;
    Esp32BaseWeb::sendChunk("<path class='volume-line' d='");
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t volumeMl = estimateVolumeMlForPulsesForWeb(params, buckets[i].cumulativePulses);
        const std::uint32_t x = kLeft + static_cast<std::uint32_t>((i * kPlotWidth) / denom);
        const std::uint32_t y =
            kTop + kPlotHeight -
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(std::min(volumeMl, maxVolumeMl)) * kPlotHeight) /
                                       maxVolumeMl);
        sendFmt("%c%lu %lu", i == 0 ? 'M' : 'L', static_cast<unsigned long>(x), static_cast<unsigned long>(y));
    }
    Esp32BaseWeb::sendChunk("'/>");
}

void sendPulseTraceDetailSections(const WaterPulseTrace& trace,
                                  const WaterPulseTraceBucketSample* compactBuckets,
                                  std::size_t compactBucketCount,
                                  std::uint32_t bucketSeconds,
                                  const char* detailPath,
                                  const char* detailSourceParam) {
    if (!compactBuckets || compactBucketCount == 0) {
        Esp32BaseWeb::sendChunk("<section id='pulse-trend' class='panel'><h3>脉冲趋势</h3><p class='hint'>脉冲明细不可用，当前只显示数值详情。</p></section>");
        return;
    }
    bucketSeconds = sanitizeTraceBucketSeconds(bucketSeconds);
    std::unique_ptr<WaterPulseTraceBucket[]> buckets(new (std::nothrow) WaterPulseTraceBucket[compactBucketCount]{});
    if (!buckets) {
        Esp32BaseWeb::sendChunk("<section id='pulse-trend' class='panel'><h3>脉冲趋势</h3><p class='err'>内存不足，无法生成脉冲图表。</p></section>");
        return;
    }
    const std::size_t bucketCount =
        aggregateWaterPulseTrace(trace, compactBuckets, compactBucketCount, bucketSeconds, buckets.get(), compactBucketCount);
    const WaterPulseTraceAnalysis analysis =
        analyzeWaterPulseTrace(trace, compactBuckets, compactBucketCount, defaultSegmentedCalibrationOptions());
    MeteringSchemeRecord scheme{};
    const bool hasScheme = trace.record.meteringSchemeId != 0 && ensureMeteringSchemesReady() &&
                           g_context.meteringSchemes->findById(trace.record.meteringSchemeId, scheme);
    MeteringTargetEstimate targetEstimate{};
    if (hasScheme && trace.record.mode == WaterMode::Volume) {
        targetEstimate = meteringEstimateForTarget(scheme.params, trace.record.targetValue);
    }

    std::uint32_t maxPulses = 1;
    std::uint32_t maxVolumeMl = std::max(trace.record.volumeMl, trace.actualMl);
    if (targetEstimate.valid) {
        maxVolumeMl = std::max(maxVolumeMl, targetEstimate.targetMl);
    }
    for (std::size_t i = 0; i < bucketCount; ++i) {
        maxPulses = std::max(maxPulses, buckets[i].pulseDelta);
        if (hasScheme) {
            maxVolumeMl = std::max(maxVolumeMl, estimateVolumeMlForPulsesForWeb(scheme.params, buckets[i].cumulativePulses));
        }
    }
    if (maxVolumeMl == 0) {
        maxVolumeMl = 1;
    }

    char startupDuration[24]{};
    char stableFlow[24]{};
    if (hasScheme) {
        formatStartupDurationSeconds(scheme.params.startupDurationMs, startupDuration, sizeof(startupDuration));
        formatFlowLitersPerMin(scheme.params.stableFlowMlPerMin, stableFlow, sizeof(stableFlow));
    }
    Esp32BaseWeb::sendChunk("<section id='pulse-trend' class='panel'><div class='panel-head'><h3>脉冲趋势</h3>");
    sendTraceFrequencyLinks(detailPath, detailSourceParam, bucketSeconds);
    Esp32BaseWeb::sendChunk("</div><table class='kv pulse-trace-diagnostics'>");
    sendFmt("<tr><th>实际有效脉冲</th><td>%lu P</td></tr>"
            "<tr><th>聚合频率</th><td>%lu 秒 / 原始 %lu ms 桶</td></tr>",
            static_cast<unsigned long>(trace.totalPulses),
            static_cast<unsigned long>(bucketSeconds),
            static_cast<unsigned long>(kPulseTraceBucketMs));
    if (targetEstimate.valid) {
        sendFmt("<tr><th>目标预计总脉冲</th><td>%lu P</td></tr>"
                "<tr><th>目标全程平均 P/L</th><td>%lu P/L</td></tr>",
                static_cast<unsigned long>(targetEstimate.pulseCount),
                static_cast<unsigned long>(targetEstimate.fullRunPulsePerLiter));
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>目标预计总脉冲</th><td>-</td></tr>");
    }
    if (hasScheme) {
        Esp32BaseWeb::sendChunk("<tr><th>使用计量参数</th><td>");
        if (scheme.name[0]) {
            sendHtmlEscapedBounded(scheme.name, sizeof(scheme.name));
        } else {
            sendFmt("计量方案 %lu", static_cast<unsigned long>(scheme.id));
        }
        sendFmt(" <span class='inline-note'>ID #%lu</span></td></tr>"
                "<tr><th>参数摘要</th><td>启动 %luP / %lu ml，稳态 %lu P/L，启动时长 %s，稳态流速 %s</td></tr>",
                static_cast<unsigned long>(scheme.id),
                static_cast<unsigned long>(scheme.params.startupPulseCount),
                static_cast<unsigned long>(scheme.params.startupVolumeMl),
                static_cast<unsigned long>(scheme.params.stablePulsePerLiter),
                startupDuration,
                stableFlow);
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>使用计量参数</th><td>历史参数不可查看</td></tr>");
    }
    if (analysis.stable) {
        const std::uint32_t stablePulsePerSecTenth =
            analysis.stablePulsePerSec <= 0.0f
                ? 0
                : static_cast<std::uint32_t>(analysis.stablePulsePerSec * 10.0f + 0.5f);
        sendFmt("<tr><th>稳态识别开始点</th><td>%lu 秒，稳态 %lu.%lu P/s</td></tr>",
                static_cast<unsigned long>(analysis.stableStartSec),
                static_cast<unsigned long>(stablePulsePerSecTenth / 10UL),
                static_cast<unsigned long>(stablePulsePerSecTenth % 10UL));
    } else {
        Esp32BaseWeb::sendChunk("<tr><th>稳态识别开始点</th><td>-</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table><div class='pulse-detail-chart'><svg viewBox='0 0 1000 300' role='img' aria-label='脉冲明细折线图'>"
                            "<line class='axis' x1='54' y1='240' x2='976' y2='240'/><line class='axis' x1='54' y1='20' x2='54' y2='240'/>");
    for (std::uint8_t i = 1; i <= 4; ++i) {
        const std::uint32_t y = 240U - static_cast<std::uint32_t>((i * 220U) / 4U);
        const std::uint32_t label = static_cast<std::uint32_t>((static_cast<std::uint64_t>(maxPulses) * i) / 4ULL);
        sendFmt("<line class='grid-line' x1='54' y1='%lu' x2='976' y2='%lu'/><text class='chart-label chart-y-label' x='48' y='%lu'>%luP</text>",
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y + 4U),
                static_cast<unsigned long>(label));
    }
    if (bucketCount > 0) {
        const std::uint32_t totalSec = std::max<std::uint32_t>(
            trace.record.durationSec,
            static_cast<std::uint32_t>((compactBucketCount * kPulseTraceBucketMs + 999UL) / 1000UL));
        for (std::uint8_t i = 0; i <= 4; ++i) {
            const std::uint32_t x = 54U + static_cast<std::uint32_t>((i * 922U) / 4U);
            const std::uint32_t sec = static_cast<std::uint32_t>((static_cast<std::uint64_t>(totalSec) * i) / 4ULL);
            sendFmt("<text class='chart-label chart-x-label' x='%lu' y='268'>%lus</text>",
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(sec));
        }
        sendPulseTraceVolumePath(buckets.get(), bucketCount, scheme.params, hasScheme ? maxVolumeMl : 0);
        sendPulseTraceChartPath(buckets.get(), bucketCount, maxPulses, "pulse-line");
        if (bucketCount <= 120) {
            const std::size_t denom = bucketCount > 1 ? bucketCount - 1 : 1;
            for (std::size_t i = 0; i < bucketCount; ++i) {
                const std::uint32_t x = 54U + static_cast<std::uint32_t>((i * 922U) / denom);
                const std::uint32_t y =
                    240U - static_cast<std::uint32_t>((static_cast<std::uint64_t>(buckets[i].pulseDelta) * 220ULL) /
                                                      maxPulses);
                sendFmt("<circle class='pulse-dot' cx='%lu' cy='%lu' r='3'/>",
                        static_cast<unsigned long>(x),
                        static_cast<unsigned long>(y));
            }
        }
        if (analysis.stable && totalSec > 0) {
            const std::uint32_t x =
                54U + static_cast<std::uint32_t>((static_cast<std::uint64_t>(std::min(analysis.stableStartSec, totalSec)) *
                                                  922ULL) /
                                                 totalSec);
            sendFmt("<line class='stable-line' x1='%lu' y1='20' x2='%lu' y2='240'/><text class='chart-label' x='%lu' y='18'>稳态开始</text>",
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(std::min<std::uint32_t>(x + 6U, 920U)));
        }
    }
    Esp32BaseWeb::sendChunk("</svg></div><div class='chart-legend'>"
                            "<span><i class='legend-mark legend-pulse'></i>有效脉冲趋势</span>"
                            "<span><i class='legend-mark legend-volume'></i>累计估算出水量</span>"
                            "<span><i class='legend-mark legend-stable'></i>稳态识别开始点</span></div>"
                            "<table><tr><th>时间</th><th>脉冲</th><th>累计脉冲</th><th>累计估算出水</th></tr>");
    for (std::size_t i = 0; i < bucketCount; ++i) {
        const std::uint32_t startSec = buckets[i].startSec;
        const std::uint32_t endSec = buckets[i].startSec + buckets[i].durationSec;
        const std::uint32_t volumeMl =
            hasScheme ? estimateVolumeMlForPulsesForWeb(scheme.params, buckets[i].cumulativePulses) : 0;
        char volumeText[24]{};
        if (hasScheme) {
            formatLitersMl(volumeMl, volumeText, sizeof(volumeText));
        } else {
            std::snprintf(volumeText, sizeof(volumeText), "-");
        }
        sendFmt("<tr><td>%lu-%lu s</td><td>%lu P</td><td>%lu P</td><td>%s</td></tr>",
                static_cast<unsigned long>(startSec),
                static_cast<unsigned long>(endSec),
                static_cast<unsigned long>(buckets[i].pulseDelta),
                static_cast<unsigned long>(buckets[i].cumulativePulses),
                volumeText);
    }
    Esp32BaseWeb::sendChunk("</table></section>");
}
