void formatStatsDayLabel(std::uint32_t dayIndex, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    dateFromDayIndex(dayIndex, year, month, day);
    std::snprintf(out, len, "%02u-%02u", static_cast<unsigned>(month), static_cast<unsigned>(day));
}

void sendDailyChart(const WaterUsageSummary& summary) {
    std::uint32_t maxVolume = 0;
    std::uint32_t maxDuration = 0;
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        maxVolume = std::max(maxVolume, summary.days[i].volumeMl);
        maxDuration = std::max(maxDuration, summary.days[i].durationSec);
    }
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><div class='chart-head'><h3>最近 30 天</h3>"
                            "<span class='chart-key volume-key'>出水量</span><span class='chart-key duration-key'>出水时长</span></div>"
                            "<p class='hint'>上下两图共用日期轴，灰色短线表示当天无记录。</p>"
                            "<svg viewBox='0 0 1000 210' role='img' aria-label='最近30天每日出水量和出水时长'>");
    constexpr std::uint32_t volumeTop = 14;
    constexpr std::uint32_t durationTop = 114;
    constexpr std::uint32_t chartHeight = 58;
    constexpr std::uint32_t volumeBaseY = volumeTop + chartHeight;
    constexpr std::uint32_t durationBaseY = durationTop + chartHeight;
    constexpr std::uint32_t left = 46;
    constexpr std::uint32_t step = 31;
    constexpr std::uint32_t barWidth = 14;
    char maxText[24]{};
    formatLiters(maxVolume, maxText, sizeof(maxText));
    char maxDurationText[24]{};
    formatDurationShort(maxDuration, maxDurationText, sizeof(maxDurationText));
    Esp32BaseWeb::sendChunk("<line class='axis' x1='40' y1='72' x2='975' y2='72'></line>");
    sendFmt("<text class='y-label' x='38' y='12'>%s</text>", maxText);
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const DailyUsageBucket& day = summary.days[i];
        const std::uint32_t barHeight = maxVolume == 0 ? 0 : (day.volumeMl * chartHeight) / maxVolume;
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const std::uint32_t y = volumeBaseY - barHeight;
        char label[8]{};
        char volume[24]{};
        char duration[24]{};
        formatStatsDayLabel(day.dayIndex, label, sizeof(label));
        formatLiters(day.volumeMl, volume, sizeof(volume));
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        const std::uint32_t avgMl = day.count == 0 ? 0 : day.volumeMl / day.count;
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu'><title>%s：%s · %s · %u 次 · 平均 %lu ml/次</title></rect>",
                day.count == 0 ? "bar empty-bar" : "bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? volumeBaseY - 2 : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 2 : barHeight),
                label,
                volume,
                duration,
                static_cast<unsigned>(day.count),
                static_cast<unsigned long>(avgMl));
    }
    Esp32BaseWeb::sendChunk("<line class='axis' x1='40' y1='172' x2='975' y2='172'></line>");
    sendFmt("<text class='y-label' x='38' y='112'>%s</text>", maxDurationText);
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const DailyUsageBucket& day = summary.days[i];
        const std::uint32_t barHeight = maxDuration == 0 ? 0 : (day.durationSec * chartHeight) / maxDuration;
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const std::uint32_t y = durationBaseY - barHeight;
        char label[8]{};
        char duration[24]{};
        formatStatsDayLabel(day.dayIndex, label, sizeof(label));
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu'><title>%s：%s · %u 次</title></rect>",
                day.count == 0 ? "duration-bar empty-bar" : "duration-bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? durationBaseY - 2 : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 2 : barHeight),
                label,
                duration,
                static_cast<unsigned>(day.count));
        if (i % 5 == 0 || i + 1 == summary.dayCount) {
            sendFmt("<text class='x-label' x='%lu' y='204'>%s</text>",
                    static_cast<unsigned long>(x + barWidth / 2),
                    label);
        }
    }
    Esp32BaseWeb::sendChunk("</svg></section>");
}

void sendTimeUnsyncedChartNotice() {
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><h3>最近 30 天出水量</h3>"
                            "<p class='hint'>时间未同步，暂不生成按真实日期统计的图表；同步后会自动显示今日往前 30 天。</p>"
                            "</section>");
}

std::uint32_t maxCountVolume(const CountVolumeBucket* values, std::size_t count) {
    std::uint32_t max = 0;
    for (std::size_t i = 0; i < count; ++i) {
        max = std::max(max, values[i].volumeMl);
    }
    return max;
}

std::uint32_t maxResultCount(const std::uint32_t* values, std::size_t count) {
    std::uint32_t max = 0;
    for (std::size_t i = 0; i < count; ++i) {
        max = std::max(max, values[i]);
    }
    return max;
}

void sendUsageBar(const char* label, std::uint32_t volumeMl, std::uint32_t count, std::uint32_t maxVolume) {
    char volume[24]{};
    formatLiters(volumeMl, volume, sizeof(volume));
    const std::uint32_t percent = maxVolume == 0 ? 0 : (volumeMl * 100UL) / maxVolume;
    Esp32BaseWeb::sendChunk("<div class='usage-row'><span>");
    sendHtmlEscapedBounded(label, std::strlen(label));
    sendFmt("</span><strong>%s · %lu 次</strong><div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            volume,
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent));
}

void sendCountBar(const char* label, std::uint32_t count, std::uint32_t maxCount) {
    const std::uint32_t percent = maxCount == 0 ? 0 : (count * 100UL) / maxCount;
    Esp32BaseWeb::sendChunk("<div class='usage-row'><span>");
    sendHtmlEscapedBounded(label, std::strlen(label));
    sendFmt("</span><strong>%lu 次</strong><div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent));
}

const char* resultLabel(std::size_t index) {
    switch (static_cast<WaterResult>(index)) {
        case WaterResult::Completed:
            return "正常完成";
        case WaterResult::StoppedByUser:
            return "用户停止";
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
    Esp32BaseWeb::sendChunk("<div class='usage-grid'><section class='usage-panel'><h3>按预设分布</h3>");
    const std::uint32_t maxPreset = maxCountVolume(summary.presetCounts, kPresetCount);
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (summary.presetCounts[i].count == 0) {
            continue;
        }
        char label[40]{};
        char name[kPresetNameLength]{};
        std::strncpy(name, config.presets[i].name, sizeof(name) - 1);
        std::snprintf(label, sizeof(label), "%u %s", static_cast<unsigned>(i + 1), name);
        sendUsageBar(label, summary.presetCounts[i].volumeMl, summary.presetCounts[i].count, maxPreset);
    }
    if (maxPreset == 0) {
        Esp32BaseWeb::sendChunk("<p class='hint'>最近 30 天没有可聚合的真实时间记录。</p>");
    }

    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>按时段分布</h3>");
    CountVolumeBucket timeRanges[6]{};
    static constexpr const char* rangeLabels[6] = {"深夜", "早晨", "上午", "中午", "下午", "晚上"};
    for (std::size_t h = 0; h < 24; ++h) {
        const std::size_t index = h < 6 ? 0 : h < 9 ? 1 : h < 12 ? 2 : h < 14 ? 3 : h < 18 ? 4 : 5;
        addSaturating(timeRanges[index].volumeMl, summary.hourBuckets[h].volumeMl);
        if (timeRanges[index].count <= UINT16_MAX - summary.hourBuckets[h].count) {
            timeRanges[index].count = static_cast<std::uint16_t>(timeRanges[index].count + summary.hourBuckets[h].count);
        } else {
            timeRanges[index].count = UINT16_MAX;
        }
    }
    const std::uint32_t maxTime = maxCountVolume(timeRanges, 6);
    for (std::size_t i = 0; i < 6; ++i) {
        sendUsageBar(rangeLabels[i], timeRanges[i].volumeMl, timeRanges[i].count, maxTime);
    }

    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>完成结果</h3>");
    const std::uint32_t maxResult = maxResultCount(summary.resultCounts, kUsageResultCount);
    for (std::size_t i = 0; i < kUsageResultCount; ++i) {
        sendCountBar(resultLabel(i), summary.resultCounts[i], maxResult);
    }

    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>单次出水分布</h3>");
    static constexpr const char* histLabels[kUsageVolumeHistCount] = {"<100 ml", "100-300 ml", "300-500 ml", "500-1000 ml", ">1000 ml"};
    const std::uint32_t maxHist = maxCountVolume(summary.volumeHist, kUsageVolumeHistCount);
    for (std::size_t i = 0; i < kUsageVolumeHistCount; ++i) {
        sendUsageBar(histLabels[i], summary.volumeHist[i].volumeMl, summary.volumeHist[i].count, maxHist);
    }
    Esp32BaseWeb::sendChunk("</section></div>");
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
    if (now >= kMinRealDateSeconds) {
        sendDailyChart(summary);
    } else {
        sendTimeUnsyncedChartNotice();
    }
    sendUsagePatterns(summary, *g_context.config);
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
