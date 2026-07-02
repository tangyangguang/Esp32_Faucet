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

void formatChartLiters(std::uint32_t ml, char* out, std::size_t len) {
    const std::uint32_t deciliters = (ml + 50UL) / 100UL;
    if (deciliters == 0) {
        std::snprintf(out, len, "0");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%lu",
                  static_cast<unsigned long>(deciliters / 10UL),
                  static_cast<unsigned long>(deciliters % 10UL));
}

std::uint32_t chartDistance(std::uint32_t a, std::uint32_t b) {
    return a > b ? a - b : b - a;
}

std::uint32_t chooseCountLabelY(std::uint32_t pointY, std::uint32_t barLabelY) {
    const std::uint32_t above = pointY > 16UL ? pointY - 11UL : pointY + 16UL;
    if (chartDistance(above, barLabelY) >= 14UL) {
        return above;
    }
    const std::uint32_t below = pointY + 15UL;
    if (chartDistance(below, barLabelY) >= 14UL) {
        return below;
    }
    return pointY > 24UL ? pointY - 24UL : pointY + 24UL;
}

void sendDailyChart(const WaterUsageSummary& summary) {
    std::uint32_t maxVolume = 0;
    std::uint32_t maxCount = 0;
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        maxVolume = std::max(maxVolume, summary.days[i].volumeMl);
        if (summary.days[i].count > maxCount) {
            maxCount = summary.days[i].count;
        }
    }
    const std::uint32_t chartMax = maxVolume == 0 ? 1000UL : maxVolume;
    const std::uint32_t countMax = maxCount == 0 ? 1UL : maxCount;
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><div class='chart-head'><h3>最近 30 天出水量</h3>"
                            "<span class='chart-unit'>每日出水量 (L) · 每日次数</span></div>"
                            "<svg viewBox='0 0 1080 292' role='img' aria-label='最近30天每日出水量和每日次数'>");
    constexpr std::uint32_t top = 56;
    constexpr std::uint32_t baseY = 222;
    constexpr std::uint32_t chartHeight = baseY - top;
    constexpr std::uint32_t left = 90;
    constexpr std::uint32_t step = 31;
    constexpr std::uint32_t barWidth = 16;
    Esp32BaseWeb::sendChunk("<line class='axis' x1='72' y1='222' x2='1030' y2='222'></line>"
                            "<line class='axis' x1='72' y1='48' x2='72' y2='222'></line>"
                            "<line class='count-axis' x1='1030' y1='48' x2='1030' y2='222'></line>");
    for (std::uint32_t tick = 0; tick < 5; ++tick) {
        const std::uint32_t tickValue = (chartMax * tick) / 4UL;
        const std::uint32_t y = baseY - (chartHeight * tick) / 4UL;
        char tickText[24]{};
        formatLiters(tickValue, tickText, sizeof(tickText));
        sendFmt("<line class='grid' x1='72' y1='%lu' x2='1030' y2='%lu'></line>"
                "<text class='chart-y-tick' x='62' y='%lu'>%s</text>",
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y),
                static_cast<unsigned long>(y + 4UL),
                tickText);
    }
    std::uint32_t previousCountTick = countMax + 1UL;
    for (std::uint32_t tick = 0; tick < 4; ++tick) {
        const std::uint32_t countValue = (countMax * tick + 1UL) / 3UL;
        if (tick > 0 && countValue == previousCountTick) {
            continue;
        }
        previousCountTick = countValue;
        const std::uint32_t y = baseY - (chartHeight * countValue) / countMax;
        sendFmt("<text class='count-y-tick' x='1040' y='%lu'>%lu 次</text>",
                static_cast<unsigned long>(y + 4UL),
                static_cast<unsigned long>(countValue));
    }
    for (std::size_t i = 0; i < summary.dayCount; ++i) {
        const DailyUsageBucket& day = summary.days[i];
        const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step;
        char label[8]{};
        char volume[24]{};
        char barLabel[12]{};
        char duration[24]{};
        formatStatsDayLabel(day.dayIndex, label, sizeof(label));
        formatLiters(day.volumeMl, volume, sizeof(volume));
        formatChartLiters(day.volumeMl, barLabel, sizeof(barLabel));
        formatDurationShort(day.durationSec, duration, sizeof(duration));
        const std::uint32_t avgMl = day.count == 0 ? 0 : day.volumeMl / day.count;
        const std::uint32_t barHeight = day.volumeMl == 0 ? 0 : (day.volumeMl * chartHeight) / chartMax;
        const std::uint32_t y = baseY - barHeight;
        const std::uint32_t center = x + barWidth / 2UL;
        sendFmt("<rect class='%s' x='%lu' y='%lu' width='%lu' height='%lu' rx='1'><title>"
                "%s：%s · %u 次 · %s · 平均 %lu ml/次</title></rect>"
                "<text class='bar-label' x='%lu' y='%lu'>%s</text>"
                "<text class='x-label' x='%lu' y='250' transform='rotate(-45 %lu 250)'>%s</text>",
                day.count == 0 ? "bar empty-bar" : "bar",
                static_cast<unsigned long>(x),
                static_cast<unsigned long>(barHeight == 0 ? baseY - 3UL : y),
                static_cast<unsigned long>(barWidth),
                static_cast<unsigned long>(barHeight == 0 ? 3UL : barHeight),
                label,
                volume,
                static_cast<unsigned>(day.count),
                duration,
                static_cast<unsigned long>(avgMl),
                static_cast<unsigned long>(center),
                static_cast<unsigned long>(barHeight == 0 ? baseY - 10UL : (y > 10UL ? y - 8UL : y)),
                barLabel,
                static_cast<unsigned long>(center),
                static_cast<unsigned long>(center),
                label);
    }
    if (summary.dayCount > 0) {
        Esp32BaseWeb::sendChunk("<polyline class='count-line-halo' points='");
        for (std::size_t i = 0; i < summary.dayCount; ++i) {
            const DailyUsageBucket& day = summary.days[i];
            const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step + barWidth / 2UL;
            const std::uint32_t y = baseY - (static_cast<std::uint32_t>(day.count) * chartHeight) / countMax;
            sendFmt("%s%lu,%lu",
                    i == 0 ? "" : " ",
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(y));
        }
        Esp32BaseWeb::sendChunk("'></polyline><polyline class='count-line' points='");
        for (std::size_t i = 0; i < summary.dayCount; ++i) {
            const DailyUsageBucket& day = summary.days[i];
            const std::uint32_t x = left + static_cast<std::uint32_t>(i) * step + barWidth / 2UL;
            const std::uint32_t y = baseY - (static_cast<std::uint32_t>(day.count) * chartHeight) / countMax;
            sendFmt("%s%lu,%lu",
                    i == 0 ? "" : " ",
                    static_cast<unsigned long>(x),
                    static_cast<unsigned long>(y));
        }
        Esp32BaseWeb::sendChunk("'></polyline>");
        for (std::size_t i = 0; i < summary.dayCount; ++i) {
            const DailyUsageBucket& day = summary.days[i];
            if (day.count == 0) {
                continue;
            }
            const std::uint32_t center = left + static_cast<std::uint32_t>(i) * step + barWidth / 2UL;
            const std::uint32_t y = baseY - (static_cast<std::uint32_t>(day.count) * chartHeight) / countMax;
            const std::uint32_t barHeight = day.volumeMl == 0 ? 0 : (day.volumeMl * chartHeight) / chartMax;
            const std::uint32_t barTop = baseY - barHeight;
            const std::uint32_t barLabelY = barHeight == 0 ? baseY - 10UL : (barTop > 10UL ? barTop - 8UL : barTop);
            const std::uint32_t countLabelY = chooseCountLabelY(y, barLabelY);
            char countText[12]{};
            std::snprintf(countText, sizeof(countText), "%u次", static_cast<unsigned>(day.count));
            sendFmt("<circle class='count-dot' cx='%lu' cy='%lu' r='1.7'><title>%u 次</title></circle>"
                    "<text class='count-label' x='%lu' y='%lu'>%s</text>",
                    static_cast<unsigned long>(center),
                    static_cast<unsigned long>(y),
                    static_cast<unsigned>(day.count),
                    static_cast<unsigned long>(center),
                    static_cast<unsigned long>(countLabelY),
                    countText);
        }
    }
    Esp32BaseWeb::sendChunk("</svg></section>");
}

void sendTimeUnsyncedChartNotice() {
    Esp32BaseWeb::sendChunk("<section class='daily-chart'><h3>最近 30 天出水量</h3>"
                            "<p class='hint'>时间未同步，暂不生成按真实日期统计的图表；同步后会自动显示今日往前 30 天。</p>"
                            "</section>");
}

std::uint32_t roundedPercent(std::uint32_t value, std::uint32_t total) {
    if (total == 0) {
        return 0;
    }
    const std::uint64_t percent =
        (static_cast<std::uint64_t>(value) * 100ULL + static_cast<std::uint64_t>(total) / 2ULL) / total;
    return percent > 100ULL ? 100UL : static_cast<std::uint32_t>(percent);
}

void sendCountVolumeDistributionRow(const char* label, std::uint32_t count, std::uint32_t volumeMl, std::uint32_t totalCount) {
    char volume[24]{};
    formatLiters(volumeMl, volume, sizeof(volume));
    const std::uint32_t percent = roundedPercent(count, totalCount);
    Esp32BaseWeb::sendChunk("<div class='usage-row'><span>");
    sendHtmlEscapedBounded(label, std::strlen(label));
    sendFmt("</span><strong>%lu 次</strong><small>占 %lu%% · 合计 %s</small>"
            "<div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent),
            volume,
            static_cast<unsigned long>(percent));
}

void sendCountDistributionRow(const char* label, std::uint32_t count, std::uint32_t totalCount) {
    const std::uint32_t percent = roundedPercent(count, totalCount);
    Esp32BaseWeb::sendChunk("<div class='usage-row'><span>");
    sendHtmlEscapedBounded(label, std::strlen(label));
    sendFmt("</span><strong>%lu 次</strong><small>占 %lu%%</small>"
            "<div class='usage-bar'><i style='width:%lu%%'></i></div></div>",
            static_cast<unsigned long>(count),
            static_cast<unsigned long>(percent),
            static_cast<unsigned long>(percent));
}

const char* resultLabel(std::size_t index) {
    switch (static_cast<WaterResult>(index)) {
        case WaterResult::Completed:
            return "正常完成";
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

void sendUsagePatterns(const WaterUsageSummary& summary, const SystemConfig& config) {
    Esp32BaseWeb::sendChunk("<div class='distribution-head'><h2>最近 30 天分布</h2>"
                            "<span class='distribution-scope'>以下占比均按最近 30 天记录次数统计</span></div>"
                            "<div class='usage-grid'>");
    Esp32BaseWeb::sendChunk("<section class='usage-panel'><h3>按预设分布</h3>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const PresetConfig& preset = config.presets[i];
        if (!preset.enabled && summary.presetCounts[i].count == 0) {
            continue;
        }
        char target[24]{};
        if (preset.type == PresetType::Time) {
            formatSecondsValue(preset.value, target, sizeof(target));
        } else {
            formatLiters(preset.value, target, sizeof(target));
        }
        const char* type = preset.type == PresetType::Time ? "时间" : "容量";
        char label[56]{};
        std::snprintf(label, sizeof(label), "P%u · %s · %s", static_cast<unsigned>(i + 1), type, target);
        sendCountVolumeDistributionRow(label,
                                       summary.presetCounts[i].count,
                                       summary.presetCounts[i].volumeMl,
                                       summary.last30DaysCount);
    }

    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>按容量段分布</h3>");
    static constexpr const char* histLabels[kUsageVolumeHistCount] = {"0.5 L 以下", "0.5 - 2 L", "2 - 5 L", "5 - 10 L", "10 L 以上"};
    for (std::size_t i = 0; i < kUsageVolumeHistCount; ++i) {
        sendCountVolumeDistributionRow(histLabels[i],
                                       summary.volumeHist[i].count,
                                       summary.volumeHist[i].volumeMl,
                                       summary.last30DaysCount);
    }
    Esp32BaseWeb::sendChunk("</section><section class='usage-panel'><h3>按完成结果分布</h3>");
    for (std::size_t i = 0; i < kUsageResultCount; ++i) {
        sendCountDistributionRow(resultLabel(i), summary.resultCounts[i], summary.last30DaysCount);
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
    Esp32BaseWeb::sendChunk("<div class='stats-layout'><div><div class='metric-grid'>");
    sendStatsMetricCard("今日", today, todayMeta);
    char monthTitle[48]{};
    std::snprintf(monthTitle, sizeof(monthTitle), "本月%s%s", monthRange[0] ? " " : "", monthRange);
    sendStatsMetricCard(monthTitle, month, monthMeta);
    sendStatsMetricCard("过去 30 天日均", average30, average30Meta);
    sendStatsMetricCard("总累计", total, totalMeta);
    Esp32BaseWeb::sendChunk("</div></div>");
    if (now >= kMinRealDateSeconds) {
        sendDailyChart(summary);
    } else {
        sendTimeUnsyncedChartNotice();
    }
    Esp32BaseWeb::sendChunk("</div>");
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
