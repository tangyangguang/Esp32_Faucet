void formatMonthRange(const WaterUsageSummary& summary, char* out, std::size_t len) {
    if (!out || len == 0 || summary.monthStartDay == 0 || summary.todayDay == 0) {
        return;
    }
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    dateFromDayIndex(summary.monthStartDay, year, month, day);
    std::uint16_t endYear = 0;
    std::uint8_t endMonth = 0;
    std::uint8_t endDay = 0;
    dateFromDayIndex(summary.todayDay, endYear, endMonth, endDay);
    std::snprintf(out,
                  len,
                  "%u 月 %u ~ %u 日",
                  static_cast<unsigned>(month),
                  static_cast<unsigned>(day),
                  static_cast<unsigned>(endDay));
}

void sendMachineTaskCard(const char* valueId, const char* metaId, const char* label, const char* value, const char* meta) {
    sendFmt("<div class='machine-task-card'><span>%s</span><strong id='%s'>%s</strong><small id='%s'%s>%s</small></div>",
            label,
            valueId,
            value,
            metaId,
            (meta && meta[0]) ? "" : " style='display:none'",
            meta);
}

void sendMachineTaskCard(const char* wrapperId,
                         const char* valueId,
                         const char* metaId,
                         const char* label,
                         const char* value,
                         const char* meta,
                         bool hidden) {
    sendFmt("<div id='%s' class='machine-task-card'%s><span>%s</span><strong id='%s'>%s</strong><small id='%s'%s>%s</small></div>",
            wrapperId,
            hidden ? " style='display:none'" : "",
            label,
            valueId,
            value,
            metaId,
            (meta && meta[0]) ? "" : " style='display:none'",
            meta);
}

void sendMachineStatusItem(const char* valueId,
                           const char* label,
                           const char* value,
                           const char* wrapperId = nullptr,
                           bool hidden = false) {
    if (wrapperId != nullptr) {
        sendFmt("<span id='%s' class='machine-status-item'%s><span>%s</span><strong id='%s'>%s</strong></span>",
                wrapperId,
                hidden ? " style='display:none'" : "",
                label,
                valueId,
                value);
        return;
    }
    sendFmt("<span class='machine-status-item'><span>%s</span><strong id='%s'>%s</strong></span>",
            label,
            valueId,
            value);
}

void sendMachineStatusItemNote(const char* valueId, const char* noteId, const char* label, const char* value, const char* note) {
    sendFmt("<span class='machine-status-item'><span>%s</span><strong id='%s'>%s</strong><small id='%s' class='machine-status-note'>%s</small></span>",
            label,
            valueId,
            value,
            noteId,
            note);
}

void sendMachineStatusSensorItem(const char* valueId, const char* label, const char* value, const char* unit) {
    sendFmt("<span class='machine-status-item'><span>%s</span><strong id='%s'>%s</strong><small class='sensor-unit'>%s</small></span>",
            label,
            valueId,
            value,
            unit);
}

void sendMachineStatusSecondaryItem(const char* valueId, const char* label, const char* value) {
    sendFmt("<span class='machine-status-item'><span>%s</span><small id='%s' class='machine-status-note'>%s</small></span>",
            label,
            valueId,
            value);
}

void formatPresetTarget(const PresetConfig& preset, char* out, std::size_t len) {
    if (preset.type == PresetType::Time) {
        formatSecondsValue(preset.value, out, len);
        return;
    }
    formatLiters(preset.value, out, len);
}

void formatFlowLitersPerMinCompact(std::uint32_t flowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (flowMlPerMin == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%03luL/min",
                  static_cast<unsigned long>(flowMlPerMin / 1000U),
                  static_cast<unsigned long>(flowMlPerMin % 1000U));
}

void formatPresetEstimate(const PresetConfig& preset,
                          const MeteringParameters& params,
                          std::uint32_t durationSec,
                          std::uint32_t timeVolumeMl,
                          std::uint32_t timePulseCount,
                          float stablePulsePerSec,
                          const char* reason,
                          char* out,
                          std::size_t len) {
    if (preset.type == PresetType::Time) {
        if (timeVolumeMl > 0 && timePulseCount > 0 && stablePulsePerSec > 0.0f) {
            char volume[24]{};
            char flow[24]{};
            formatLiters(timeVolumeMl, volume, sizeof(volume));
            formatFlowLitersPerMinCompact(params.stableFlowMlPerMin, flow, sizeof(flow));
            std::snprintf(out,
                          len,
                          "预计 %s · %luP · 稳态 %s",
                          volume,
                          static_cast<unsigned long>(timePulseCount),
                          flow);
        } else {
            std::snprintf(out, len, "%s", reason ? reason : "计量参数未就绪");
        }
        return;
    }
    const MeteringTargetEstimate estimate = meteringEstimateForTarget(params, preset.value);
    if (!estimate.valid) {
        std::snprintf(out, len, "%s", reason ? reason : "计量参数未就绪");
        return;
    }
    char duration[24]{};
    if (durationSec > 0) {
        formatSecondsValue(durationSec, duration, sizeof(duration));
        std::snprintf(out,
                      len,
                      "预计 %luP/L · %luP · 约 %s",
                      static_cast<unsigned long>(estimate.fullRunPulsePerLiter),
                      static_cast<unsigned long>(estimate.pulseCount),
                      duration);
    } else {
        std::snprintf(out,
                      len,
                      "预计 %luP/L · %luP",
                      static_cast<unsigned long>(estimate.fullRunPulsePerLiter),
                      static_cast<unsigned long>(estimate.pulseCount));
    }
}

std::uint32_t recordFlowMlPerMin(std::uint32_t volumeMl, std::uint32_t durationSec) {
    if (volumeMl == 0 || durationSec == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(volumeMl) * 60ULL + durationSec / 2ULL) /
                                      durationSec);
}

std::uint32_t stableFlowMlPerMin(float stablePulsePerSec, const MeteringParameters& params) {
    if (stablePulsePerSec <= 0.0f || params.stablePulsePerLiter == 0) {
        return 0;
    }
    const double mlPerMin =
        static_cast<double>(stablePulsePerSec) * 60000.0 / static_cast<double>(params.stablePulsePerLiter);
    return mlPerMin <= 0.0 ? 0 : static_cast<std::uint32_t>(mlPerMin + 0.5);
}

void formatFlowLitersPerMin(std::uint32_t flowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (flowMlPerMin == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%03lu L/min",
                  static_cast<unsigned long>(flowMlPerMin / 1000U),
                  static_cast<unsigned long>(flowMlPerMin % 1000U));
}

void formatFlowNumber(std::uint32_t flowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    const std::uint32_t centiLitersPerMin = (flowMlPerMin + 5U) / 10U;
    if (centiLitersPerMin == 0) {
        std::snprintf(out, len, "-");
        return;
    }
    std::snprintf(out,
                  len,
                  "%lu.%02lu",
                  static_cast<unsigned long>(centiLitersPerMin / 100U),
                  static_cast<unsigned long>(centiLitersPerMin % 100U));
}

void formatFlowMeta(std::uint32_t runAverageFlowMlPerMin, char* out, std::size_t len) {
    if (!out || len == 0) {
        return;
    }
    char average[16]{};
    formatFlowNumber(runAverageFlowMlPerMin, average, sizeof(average));
    std::snprintf(out, len, "L/min · 本次平均 %s", average);
}

void sendNextPresetControl(const AppSnapshot& snapshot) {
    const SystemConfig& config = *g_context.config;
    const bool available = snapshot.water.selectedPreset < kPresetCount && config.presets[snapshot.water.selectedPreset].enabled;
    char target[24]{};
    char estimate[160]{};
    Esp32BaseWeb::sendChunk("<div id='nextPresetControl' class='next-preset-control'>"
                            "<button class='preset-step' type='button' aria-label='上一个预设' data-action='action=select_previous' onclick=\"faucetSelectPreset('select_previous')\">‹</button>"
                            "<div class='next-preset-copy'><span>下次预设</span><strong id='nextPresetLabel'>");
    if (available) {
        const PresetConfig& preset = config.presets[snapshot.water.selectedPreset];
        formatPresetTarget(preset, target, sizeof(target));
        sendFmt("P%u · ", static_cast<unsigned>(snapshot.water.selectedPreset + 1U));
        sendHtmlEscapedBounded(preset.name[0] ? preset.name : "未命名", sizeof(preset.name));
        sendFmt(" · %s", target);
        formatPresetEstimate(preset,
                             snapshot.meteringParams,
                             snapshot.selectedPresetEstimatedDurationSec,
                             snapshot.selectedPresetEstimatedVolumeMl,
                             snapshot.selectedPresetEstimatedPulseCount,
                             snapshot.selectedPresetStablePulsePerSec,
                             snapshot.selectedPresetEstimateReason,
                             estimate,
                             sizeof(estimate));
    } else {
        Esp32BaseWeb::sendChunk("无可用预设");
        std::snprintf(estimate, sizeof(estimate), "请先在预设页启用至少一项");
    }
    sendFmt("</strong><small id='nextPresetEstimate'%s>%s</small></div>"
            "<button class='preset-step' type='button' aria-label='下一个预设' data-action='action=select_next' onclick=\"faucetSelectPreset('select_next')\">›</button>"
            "</div>",
            estimate[0] ? "" : " style='display:none'",
            estimate);
}

const char* machineStatusNote(const AppSnapshot& snapshot) {
    switch (snapshot.water.state) {
        case WaterState::Idle:
            return "设备可用，等待按键启动";
        case WaterState::Confirm:
            return "等待确认，确认后开始出水";
        case WaterState::Running:
            return "正在出水，请留意容器";
        case WaterState::Paused:
            return "已暂停，等待继续或取消";
        case WaterState::Error:
            return resultText(snapshot.water.lastResult);
    }
    return "状态未知";
}

void sendMachineStatusCard(const AppSnapshot& snapshot, bool screenOn) {
    const bool shouldShowProgress = snapshot.water.state == WaterState::Running ||
                                    snapshot.water.state == WaterState::Paused ||
                                    snapshot.water.state == WaterState::Confirm;
    char targetValue[24]{};
    if (snapshot.water.mode == WaterMode::Time) {
        formatSecondsValue(snapshot.water.targetValue, targetValue, sizeof(targetValue));
    } else {
        formatLiters(snapshot.water.targetValue, targetValue, sizeof(targetValue));
    }
    char outValue[24]{};
    formatLiters(snapshot.water.volumeMl, outValue, sizeof(outValue));
    const std::uint32_t progressBase = snapshot.water.mode == WaterMode::Time ? snapshot.water.elapsedSec : snapshot.water.volumeMl;
    const std::uint32_t progressPercent = percentOf(progressBase, snapshot.water.targetValue);
    char progressText[48]{};
    if (snapshot.water.mode == WaterMode::Time) {
        char elapsed[20]{};
        formatSecondsValue(snapshot.water.elapsedSec, elapsed, sizeof(elapsed));
        std::snprintf(progressText, sizeof(progressText), "%s / %s", elapsed, targetValue);
    } else {
        std::snprintf(progressText, sizeof(progressText), "%s / %s", outValue, targetValue);
    }
    const std::uint32_t remaining =
        snapshot.water.mode == WaterMode::Time
            ? (snapshot.water.targetValue > snapshot.water.elapsedSec ? snapshot.water.targetValue - snapshot.water.elapsedSec : 0)
            : (snapshot.water.targetValue > snapshot.water.volumeMl ? snapshot.water.targetValue - snapshot.water.volumeMl : 0);
    char remainingValue[24]{};
    if (snapshot.water.mode == WaterMode::Time) {
        formatSecondsValue(remaining, remainingValue, sizeof(remainingValue));
    } else {
        formatLiters(remaining, remainingValue, sizeof(remainingValue));
    }
    char elapsedText[24]{};
    formatSecondsValue(snapshot.water.elapsedSec, elapsedText, sizeof(elapsedText));
    char targetMeta[96]{};
    if (snapshot.water.mode == WaterMode::Volume) {
        const MeteringTargetEstimate targetEstimate =
            meteringEstimateForTarget(snapshot.meteringParams, snapshot.water.targetValue);
        char durationText[24]{};
        if (!targetEstimate.valid) {
            std::snprintf(targetMeta,
                          sizeof(targetMeta),
                          "%s",
                          snapshot.targetEstimateReason ? snapshot.targetEstimateReason : "计量参数未就绪");
        } else if (snapshot.targetEstimatedDurationSec > 0) {
            formatSecondsValue(snapshot.targetEstimatedDurationSec, durationText, sizeof(durationText));
            std::snprintf(targetMeta,
                          sizeof(targetMeta),
                          "预计约 %s",
                          durationText);
        } else {
            std::snprintf(targetMeta, sizeof(targetMeta), "计量参数未就绪");
        }
    } else if (snapshot.targetEstimatedVolumeMl > 0 && snapshot.targetEstimatedPulseCount > 0 &&
               snapshot.targetStablePulsePerSec > 0.0f) {
        char estimatedVolume[24]{};
        formatLiters(snapshot.targetEstimatedVolumeMl, estimatedVolume, sizeof(estimatedVolume));
        std::snprintf(targetMeta, sizeof(targetMeta), "预计 %s", estimatedVolume);
    } else {
        std::snprintf(targetMeta,
                      sizeof(targetMeta),
                      "%s",
                      snapshot.targetEstimateReason ? snapshot.targetEstimateReason : "计量参数未就绪");
    }
    char outputMeta[32]{};
    std::snprintf(outputMeta, sizeof(outputMeta), "已运行 %s", elapsedText);
    char remainingMeta[24]{};
    std::snprintf(remainingMeta, sizeof(remainingMeta), "完成 %lu%%", static_cast<unsigned long>(progressPercent));
    char currentFlow[24]{};
    formatFlowNumber(snapshot.currentFlowMlPerMin, currentFlow, sizeof(currentFlow));
    char currentFlowMeta[40]{};
    formatFlowMeta(snapshot.runAverageFlowMlPerMin, currentFlowMeta, sizeof(currentFlowMeta));
    char droppedPulses[24]{};
    std::snprintf(droppedPulses, sizeof(droppedPulses), "%lu", static_cast<unsigned long>(snapshot.flowDroppedPulses));
    char valvePwmDuty[12]{};
    std::snprintf(valvePwmDuty, sizeof(valvePwmDuty), "%u%%", static_cast<unsigned>(snapshot.valve.dutyPercent));
    char valvePwmNote[32]{};
    std::snprintf(valvePwmNote,
                  sizeof(valvePwmNote),
                  "%lus全功率→%u%%保持",
                  static_cast<unsigned long>(g_context.config->valveFullPowerSec),
                  static_cast<unsigned>(g_context.config->valveHoldDutyPercent));
    char currentTemperature[24]{};
    if (snapshot.temperatureSensorEnabled && snapshot.sensors.temperatureCentiC.valid) {
        formatSensorTemperatureNumber(snapshot.sensors.temperatureCentiC, currentTemperature, sizeof(currentTemperature));
    } else {
        std::snprintf(currentTemperature, sizeof(currentTemperature), "--");
    }
    char currentTds[24]{};
    if (snapshot.tdsSensorEnabled && snapshot.sensors.tdsPpm.valid) {
        char tdsValue[12]{};
        formatSensorIntegerNumber(snapshot.sensors.tdsPpm, tdsValue, sizeof(tdsValue));
        std::snprintf(currentTds,
                      sizeof(currentTds),
                      "%s%s",
                      tdsValue,
                      snapshot.sensors.tdsCalibrated ? "" : "（未校准）");
    } else {
        std::snprintf(currentTds, sizeof(currentTds), "--");
    }
    char inputVoltage[24]{};
    if (snapshot.sensors.inputVoltageMv.valid) {
        const std::int32_t centivolts = (snapshot.sensors.inputVoltageMv.value + 5) / 10;
        std::snprintf(inputVoltage,
                      sizeof(inputVoltage),
                      "%ld.%02ld V",
                      static_cast<long>(centivolts / 100),
                      static_cast<long>(centivolts % 100));
    } else {
        std::snprintf(inputVoltage, sizeof(inputVoltage), "-- V");
    }

    const bool showResult = snapshot.water.state == WaterState::Error || snapshot.localMode == LocalUiMode::Result;
    const bool showRunningNote = snapshot.water.state == WaterState::Running;
    const char* machineLayoutClass = shouldShowProgress ? "machine-main" : "machine-main compact";
    sendFmt("<h2 class='home-section-title'>机器状态</h2><section class='panel machine-status'><div class='%s'><div class='machine-hero'>"
            "<div class='machine-hero-head'><strong>",
            machineLayoutClass);
    Esp32BaseWeb::sendChunk("<span id='machineState'>");
    Esp32BaseWeb::sendChunk(stateText(snapshot.water.state));
    Esp32BaseWeb::sendChunk("</span></strong><div class='machine-context'>");
    sendNextPresetControl(snapshot);
    Esp32BaseWeb::sendChunk("</div></div>");
    sendFmt("<div id='machineProgress' class='machine-progress'%s><div class='machine-progress-head'><span>出水进度</span><strong id='machineProgressText'>%s</strong></div>"
            "<div class='progress'><span id='machineProgressBar' style='width:%lu%%'></span></div>"
            "<p id='machineStatusNote' class='machine-alert machine-progress-alert'%s>%s</p></div>",
            shouldShowProgress ? "" : " style='display:none'",
            progressText,
            static_cast<unsigned long>(progressPercent),
            showRunningNote ? "" : " style='display:none'",
            machineStatusNote(snapshot));
    sendFmt("<span class='machine-screen-footer'><span>屏幕</span><span id='screenStatus'>%s</span></span>",
            screenOn ? "亮屏" : "休眠");
    Esp32BaseWeb::sendChunk("</div><div class='machine-overview'><div class='machine-task-grid'>");
    sendMachineTaskCard("targetValue", "targetMeta", "目标", targetValue, targetMeta);
    sendMachineTaskCard("outputValue", "outputMeta", "已出水", outValue, outputMeta);
    sendMachineTaskCard("remainingValue", "remainingMeta", "剩余", remainingValue, remainingMeta);
    sendMachineTaskCard("currentFlowCard", "currentFlowValue", "currentFlowMeta", "流速", currentFlow, currentFlowMeta, false);
    Esp32BaseWeb::sendChunk("</div><div class='machine-status-strip'>");
    sendMachineStatusItem("valveStatus", "阀门", snapshot.water.valveOpen ? "开" : "关");
    sendMachineStatusSensorItem("temperatureStatus", "水温", currentTemperature, "C");
    sendMachineStatusSensorItem("tdsStatus", "TDS", currentTds, "ppm");
    sendMachineStatusSecondaryItem("inputVoltageStatus", "电压", inputVoltage);
    sendMachineStatusItemNote("valvePwmDuty", "valvePwmNote", "PWM", valvePwmDuty, valvePwmNote);
    sendMachineStatusItem("resultStatus", "结果", resultText(snapshot.water.lastResult), "resultItem", !showResult);
    sendMachineStatusItem("droppedPulses", "丢弃脉冲", droppedPulses, "droppedPulsesItem", snapshot.flowDroppedPulses == 0);
    Esp32BaseWeb::sendChunk("</div></div></div></section>");
}

TodayOverview collectTodayOverview(std::uint32_t now, std::uint32_t fallbackTodayMl) {
    TodayOverview overview{};
    overview.volumeMl = fallbackTodayMl;
    if (!g_context.records || !g_context.records->ready() || now < kMinRealDateSeconds || waterTaskActive()) {
        return overview;
    }
    overview.timeReady = true;
    const std::uint32_t todayStart = (now / 86400UL) * 86400UL;
    std::unique_ptr<WaterRecord[]> records(new (std::nothrow) WaterRecord[kDefaultRecordPageSize]{});
    if (!records) {
        return overview;
    }
    const std::size_t total = g_context.records->count();
    bool stopAfterPage = false;
    for (std::size_t offset = 0; offset < total && !stopAfterPage; offset += kDefaultRecordPageSize) {
        const std::size_t page = offset / kDefaultRecordPageSize;
        const std::size_t read =
            g_context.records->readPage(page, kDefaultRecordPageSize, records.get(), kDefaultRecordPageSize);
        if (read == 0) {
            break;
        }
        for (std::size_t i = 0; i < read; ++i) {
            if (!waterRecordHasRealTime(records[i])) {
                continue;
            }
            if (records[i].startTime > now) {
                continue;
            }
            if (records[i].startTime < todayStart) {
                stopAfterPage = true;
                break;
            }
            ++overview.count;
            overview.durationSec += records[i].durationSec;
            if (overview.latestCount < kHomeTodayRecordLimit) {
                overview.latest[overview.latestCount++] = records[i];
            }
        }
    }
    return overview;
}

void sendTodayOverview(const TodayOverview& overview) {
    char today[24]{};
    char countText[20]{};
    char totalDuration[24]{};
    formatLiters(overview.volumeMl, today, sizeof(today));
    if (overview.timeReady) {
        std::snprintf(countText, sizeof(countText), "%lu 次", static_cast<unsigned long>(overview.count));
        formatSecondsValue(overview.durationSec, totalDuration, sizeof(totalDuration));
    } else {
        std::snprintf(countText, sizeof(countText), "-- 次");
        std::snprintf(totalDuration, sizeof(totalDuration), "--");
    }

    Esp32BaseWeb::sendChunk("<section id='todayOverview' class='today-overview'><h2 class='home-section-title'>今日概览</h2><div class='today-layout'><section class='panel today-summary-card'><span class='today-summary-label'>今日总量</span><strong class='today-total-main'>");
    Esp32BaseWeb::sendChunk(today);
    Esp32BaseWeb::sendChunk("</strong><span class='today-total-meta today-meta-line'><span class='today-meta-item'>接水 <span class='today-meta-value'>");
    Esp32BaseWeb::sendChunk(countText);
    Esp32BaseWeb::sendChunk("</span></span><span class='today-meta-item'>用时 <span class='today-meta-value'>");
    Esp32BaseWeb::sendChunk(totalDuration);
    Esp32BaseWeb::sendChunk("</span></span></span></section><section class='panel today-records'>");
    if (!overview.timeReady) {
        Esp32BaseWeb::sendChunk("<p class='hint'>时间同步后显示今天的接水时间和水量。</p>");
    } else if (overview.latestCount == 0) {
        Esp32BaseWeb::sendChunk("<p class='hint'>今天还没有接水记录。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<table class='today-record-table'><tr><th>时间</th><th>用时</th><th>实际出水</th><th>水温</th><th>TDS</th><th>预设目标</th><th>结果</th></tr>");
        for (std::size_t i = 0; i < overview.latestCount; ++i) {
            char startTime[12]{};
            char duration[24]{};
            char volume[24]{};
            char temperature[24]{};
            char tds[24]{};
            char preset[48]{};
            formatRecordTimeOfDay(overview.latest[i].startTime, startTime, sizeof(startTime));
            formatSecondsValue(overview.latest[i].durationSec, duration, sizeof(duration));
            formatLiters(overview.latest[i].volumeMl, volume, sizeof(volume));
            formatWaterRecordTemperature(overview.latest[i], temperature, sizeof(temperature));
            formatWaterRecordTds(overview.latest[i], tds, sizeof(tds));
            formatRecordPresetLabel(overview.latest[i], preset, sizeof(preset));
            sendFmt("<tr><td>%s</td><td class='record-duration'>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                    "<td><span class='status-pill %s'>%s</span></td></tr>",
                    startTime,
                    duration,
                    volume,
                    temperature,
                    tds,
                    preset,
                    resultStatusClass(overview.latest[i].result),
                    resultText(overview.latest[i].result));
        }
        Esp32BaseWeb::sendChunk("</table>");
    }
    Esp32BaseWeb::sendChunk("</section></div></section>");
}

void sendTodayOverviewPlaceholder(std::uint32_t fallbackTodayMl) {
    char today[24]{};
    formatLiters(fallbackTodayMl, today, sizeof(today));
    Esp32BaseWeb::sendChunk("<section id='todayOverview' class='today-overview'><h2 class='home-section-title'>今日概览</h2>"
                            "<div class='today-layout'><section class='panel today-summary-card'>"
                            "<span class='today-summary-label'>今日总量</span><strong class='today-total-main'>");
    Esp32BaseWeb::sendChunk(today);
    Esp32BaseWeb::sendChunk("</strong><span class='today-total-meta today-meta-line'>"
                            "<span class='today-meta-item'>接水 <span class='today-meta-value'>-- 次</span></span>"
                            "<span class='today-meta-item'>用时 <span class='today-meta-value'>--</span></span>"
                            "</span></section><section class='panel today-records'>"
                            "<p class='hint'>正在加载今天的记录。</p></section></div></section>");
}

void sendFilterCards(std::uint32_t now) {
    Esp32BaseWeb::sendChunk("<h2 class='home-section-title'>滤芯</h2><div class='filter-cards'>");
    std::uint32_t usedDaysByIndex[kFilterCount]{};
    std::size_t enabledCount = 0;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        if (!filter.enabled) {
            continue;
        }
        usedDaysByIndex[i] = filter.startTime >= kMinRealDateSeconds ? g_context.filters->usedDays(i, now) : 0;
        ++enabledCount;
    }
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        const FilterRecord& filter = g_context.filters->record(i);
        if (!filter.enabled) {
            continue;
        }
        const std::uint32_t usedDays = usedDaysByIndex[i];
        const std::uint32_t dayProgress = filterProgressPercent(filter, usedDays);
        const std::uint32_t flowProgress = filterFlowProgressPercent(filter);
        char usedFlow[24]{};
        char lifeFlow[24]{};
        char startDate[16]{};
        formatLiters(filter.usedMl, usedFlow, sizeof(usedFlow));
        formatLiters(filter.lifeMl, lifeFlow, sizeof(lifeFlow));
        formatDate(filter.startTime, startDate, sizeof(startDate));

        Esp32BaseWeb::sendChunk("<section class='filter-card'><div class='filter-head'><strong>");
        sendHtmlEscapedBounded(filter.name, sizeof(filter.name));
        sendFmt("</strong><span class='status-pill %s'>%s</span></div>"
                "<div class='dual-progress'><div class='filter-progress-row filter-progress-label'><b>天数</b>"
                "<span class='filter-track'><span class='filter-progress-fill day-progress' style='width:%lu%%'></span></span></div>",
                filterStatusClass(filter, usedDays),
                filterDisplayStatusText(filter, usedDays),
                static_cast<unsigned long>(dayProgress));
        if (filter.lifeMl > 0) {
            sendFmt("<div class='filter-progress-row filter-progress-label'><b>流量</b>"
                    "<span class='filter-track'><span class='filter-progress-fill flow-progress' style='width:%lu%%'></span></span></div>",
                    static_cast<unsigned long>(flowProgress));
        }
        Esp32BaseWeb::sendChunk("</div>");
        Esp32BaseWeb::sendChunk("<div class='filter-meta'>");
        if (filter.recommendDays > 0 && filter.maxDays > 0) {
            sendFmt("<span>天数：已用 %lu 天 / 建议 %lu 天 / 最长 %lu 天</span>",
                    static_cast<unsigned long>(usedDays),
                    static_cast<unsigned long>(filter.recommendDays),
                    static_cast<unsigned long>(filter.maxDays));
        } else if (filter.recommendDays > 0) {
            sendFmt("<span>天数：已用 %lu 天 / 建议 %lu 天</span>",
                    static_cast<unsigned long>(usedDays),
                    static_cast<unsigned long>(filter.recommendDays));
        } else if (filter.maxDays > 0) {
            sendFmt("<span>天数：已用 %lu 天 / 最长 %lu 天</span>",
                    static_cast<unsigned long>(usedDays),
                    static_cast<unsigned long>(filter.maxDays));
        } else {
            sendFmt("<span>天数：已用 %lu 天</span>", static_cast<unsigned long>(usedDays));
        }
        if (filter.lifeMl > 0) {
            sendFmt("<span>流量：已用 %s / 寿命 %s</span>", usedFlow, lifeFlow);
        } else {
            sendFmt("<span>流量：已用 %s</span>", usedFlow);
        }
        sendFmt("<span>上次更换 %s</span></div></section>", startDate[0] ? startDate : "未设置");
    }
    if (enabledCount == 0) {
        Esp32BaseWeb::sendChunk("<section class='filter-card muted'>当前没有启用的滤芯。</section>");
    }
    Esp32BaseWeb::sendChunk("</div>");
}

void handleFaucetPage() {
    if (!sendPageStart("首页")) {
        return;
    }
    if (!requireContext()) {
        sendPageEnd();
        return;
    }
    AppSnapshot snapshot = g_context.app->snapshot();
    if (g_context.currentRuntimeDiagnostics) {
        const FaucetRuntimeDiagnostics diagnostics = g_context.currentRuntimeDiagnostics();
        snapshot.maxLoopIntervalUs = diagnostics.maxLoopIntervalUs;
        snapshot.maxAppTickUs = diagnostics.maxAppTickUs;
        snapshot.maxBaseHandleUs = diagnostics.maxBaseHandleUs;
    }
    applyTargetDurationEstimate(snapshot, false);
    const FaucetDisplayStatus displayStatus =
        g_context.currentDisplayStatus
            ? g_context.currentDisplayStatus()
            : FaucetDisplayStatus{false};
    sendMachineStatusCard(snapshot, displayStatus.screenOn);
    sendTodayOverviewPlaceholder(snapshot.statistics.todayMl);
    sendFilterCards(g_context.nowSeconds());
    sendHomeAutoRefreshScript();
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

    char text[24]{};
    std::uint32_t editIndex = UINT32_MAX;
    if (getParam("index", text, sizeof(text))) {
        parseU32(text, editIndex);
    }
    if (editIndex < kPresetCount) {
        const PresetConfig& preset = g_context.config->presets[editIndex];
        const bool timePreset = preset.type == PresetType::Time;
        sendFmt("<h2>预设 %u</h2><section class='panel'>"
                "<form method='post' action='/api/faucet/presets' onsubmit='return once(this)'>"
                "<input type='hidden' name='index' value='%u'><input type='hidden' name='return' value='/faucet/presets'>"
                "<div class='form-grid'>",
                static_cast<unsigned>(editIndex + 1),
                static_cast<unsigned>(editIndex));
        sendNoticeFromQuery();
        Esp32BaseWeb::sendChunk("<div class='span-2'>");
        sendCheckbox("启用", "enabled", preset.enabled);
        Esp32BaseWeb::sendChunk("</div><label class='field span-5'><span>名称</span><input name='name' maxlength='15' value='");
        sendHtmlAttrEscapedBounded(preset.name, sizeof(preset.name));
        Esp32BaseWeb::sendChunk("'><small class='hint'>最多 15 个字符</small></label><label class='field span-2'><span>类型</span><select id='presetType' name='type' onchange='presetTypeChanged()'>");
        sendFmt("<option value='volume'%s>容量</option>", preset.type == PresetType::Volume ? " selected" : "");
        sendFmt("<option value='time'%s>时间</option>", preset.type == PresetType::Time ? " selected" : "");
        Esp32BaseWeb::sendChunk("</select></label>");
        Esp32BaseWeb::sendChunk("<label class='field span-3'><span id='presetValueLabel'>");
        Esp32BaseWeb::sendChunk(timePreset ? "时长（秒）" : "出水量（ml）");
        sendFmt("</span><input id='presetValue' name='value' type='number' min='%lu' max='%lu' step='%lu' value='%lu'>",
                static_cast<unsigned long>(timePreset ? kMinTimePresetSec : kMinVolumePresetMl),
                static_cast<unsigned long>(timePreset ? kMaxTimePresetSec : kMaxVolumePresetMl),
                static_cast<unsigned long>(timePreset ? 1UL : 100UL),
                static_cast<unsigned long>(preset.value));
        Esp32BaseWeb::sendChunk("<small id='presetValueHint' class='hint'>");
        Esp32BaseWeb::sendChunk(timePreset ? "按秒计时，到时自动关阀" : "按毫升计量，到量自动关阀");
        Esp32BaseWeb::sendChunk("</small></label>");
        Esp32BaseWeb::sendChunk("</div><div class='form-actions'><input type='submit' value='保存'>"
                                "<a href='/faucet/presets'>取消</a></div></form></section>"
                                "<script>"
                                "function presetTypeChanged(){"
                                "var t=document.getElementById('presetType').value;"
                                "var v=document.getElementById('presetValue');"
                                "document.getElementById('presetValueLabel').textContent=t=='time'?'时长（秒）':'出水量（ml）';"
                                "document.getElementById('presetValueHint').textContent=t=='time'?'按秒计时，到时自动关阀':'按毫升计量，到量自动关阀';"
                                "v.min=t=='time'?'5':'100';v.max=t=='time'?'1800':'20000';v.step=t=='time'?'1':'100';"
                                "}"
                                "</script>");
        sendPageEnd();
        return;
    }

    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<h2>预设</h2><table class='compact-table'><tr>"
                            "<th>序号</th><th>名称</th><th>类型</th><th>目标值</th><th>状态</th><th>操作</th></tr>");
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        const PresetConfig& preset = g_context.config->presets[i];
        sendFmt("<tr%s><td>%u</td><td>",
                preset.enabled ? "" : " class='disabled-row'",
                static_cast<unsigned>(i + 1));
        sendHtmlEscapedBounded(preset.name, sizeof(preset.name));
        sendFmt("</td><td>%s</td><td>",
                preset.type == PresetType::Time ? "时间" : "容量");
        if (preset.type == PresetType::Time) {
            sendFmt("%lu 秒", static_cast<unsigned long>(preset.value));
        } else {
            sendLiters(preset.value);
        }
        sendFmt("</td><td><span class='status-pill'>%s</span></td>"
                "<td><div class='row-actions'><a href='/faucet/presets?index=%u'>编辑</a></div></td></tr>",
                preset.enabled ? "启用" : "停用",
                static_cast<unsigned>(i));
    }
    Esp32BaseWeb::sendChunk("</table>");
    sendPageEnd();
}
