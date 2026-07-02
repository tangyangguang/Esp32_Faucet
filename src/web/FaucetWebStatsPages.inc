void formatStatsDayLabel(std::uint32_t dayIndex, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    dateFromDayIndex(dayIndex, year, month, day);
    std::snprintf(out, len, "%u/%u", static_cast<unsigned>(month), static_cast<unsigned>(day));
}

void sendDailyVolumeChart(const WaterUsageSummary& summary) {
    std::uint32_t maxVolume = 0;
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        maxVolume = std::max(maxVolume, summary.days[i].volumeMl);
    }
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><div class='chart-head'><h3>最近 30 天</h3>"
                            "<span class='chart-key'>出水量</span></div>"
                            "<svg viewBox='0 0 1000 112' role='img' aria-label='最近30天每日出水量'>"
                            "<line class='axis' x1='40' y1='78' x2='975' y2='78'></line>");
    char maxText[24]{};
    formatLiters(maxVolume, maxText, sizeof(maxText));
    sendFmt("<text class='y-label' x='38' y='14'>%s</text>", maxText);
    constexpr std::uint32_t chartHeight = 58;
    constexpr std::uint32_t baseY = 78;
    constexpr std::uint32_t left = 46;
    constexpr std::uint32_t step = 31;
    constexpr std::uint32_t barWidth = 14;
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const DailyUsageBucket& day = summary.days[i];
        const std::uint32_t barHeight = maxVolume == 0 ? 0 : (day.volumeMl * chartHeight) / maxVolume;
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        const std::uint32_t y = baseY - barHeight;
        char label[8]{};
        char volume[24]{};
        char duration[24]{};
        formatStatsDayLabel(day.dayIndex, label, sizeof(label));
        formatLiters(day.volumeMl, volume, sizeof(volume));
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu'><title>%s：%s · %s · %u 次</title></rect>",
                day.count == 0 ? "bar empty-bar" : "bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? baseY - 2 : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 2 : barHeight),
                label,
                volume,
                duration,
                static_cast<unsigned>(day.count));
        if (i % 5 == 0 || i + 1 == summary.dayCount) {
            sendFmt("<text class='x-label' x='%lu' y='104'>%s</text>",
                    static_cast<unsigned long>(x + barWidth / 2),
                    label);
        }
    }
    Esp32BaseWeb::sendChunk("</svg></section>");
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
    sendDailyVolumeChart(summary);
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
