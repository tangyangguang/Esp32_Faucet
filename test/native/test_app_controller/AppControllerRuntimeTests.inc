void test_app_controller_offline_completion_marks_unknown_time_with_boot_id() {
    SystemConfig config = makeDefaultConfig();
    MemoryRecordWriter records;
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    app.tick(offlineInput({false, true, false, false}, 100, 100000, 1));
    app.tick(offlineInput({false, true, false, false}, 100 + kButtonDebounceMs, (100 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 160, 160000, 1));
    app.tick(offlineInput({false, false, false, false}, 160 + kButtonDebounceMs, (160 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, true, false, false}, 300, 300000, 1));
    app.tick(offlineInput({false, true, false, false}, 300 + kButtonDebounceMs, (300 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 360, 360000, 1));
    app.tick(offlineInput({false, false, false, false}, 360 + kButtonDebounceMs, (360 + kButtonDebounceMs) * 1000UL, 1));
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(2000000UL + i * 2000UL);
    }
    app.tick(offlineInput({false, false, false, false}, 5000, 5000000, 5));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].volumeMl);
    TEST_ASSERT_EQUAL_UINT32(1, records.records[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(42, waterRecordBootId(records.records[0]));
    TEST_ASSERT_EQUAL_UINT32(0, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(0, statistics.record().totalMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
}

void test_app_controller_offline_start_sync_before_completion_writes_real_time() {
    SystemConfig config = makeDefaultConfig();
    MemoryRecordWriter records;
    StatisticsStore statistics;
    FilterStore filters(config.filters);
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    app.tick(offlineInput({false, true, false, false}, 1000, 1000000, 1));
    app.tick(offlineInput({false, true, false, false}, 1000 + kButtonDebounceMs, (1000 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 1060, 1060000, 1));
    app.tick(offlineInput({false, false, false, false}, 1060 + kButtonDebounceMs, (1060 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, true, false, false}, 1300, 1300000, 1));
    app.tick(offlineInput({false, true, false, false}, 1300 + kButtonDebounceMs, (1300 + kButtonDebounceMs) * 1000UL, 1));
    app.tick(offlineInput({false, false, false, false}, 1360, 1360000, 1));
    app.tick(offlineInput({false, false, false, false}, 1360 + kButtonDebounceMs, (1360 + kButtonDebounceMs) * 1000UL, 1));
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(2000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 815500004));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(815500000, records.records[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, waterRecordBootId(records.records[0]));
}

void test_app_controller_pause_resume_then_completion_updates_persistence_once() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    WaterPulseTrace traces[2]{};
    WaterPulseTraceBucketSample buckets[2000]{};
    WaterPulseTraceSample startupEdges[2000]{};
    WaterPulseTraceStore pulseTraces(traces, 2, buckets, 2000, startupEdges, 2000, 2);
    AppController app(config, statistics, filters, records, &pulseTraces);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 700; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 2500, 2500000, 1714502400));
    TEST_ASSERT_EQUAL_UINT32(700, app.snapshot().water.volumeMl);

    pressAndReleaseOk(app, 2600);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Paused),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    pressAndReleasePlus(app, 2800);
    pressAndReleaseMinus(app, 3000);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);
    app.tick(input({false, false, false, false}, 20000, 20000000, 1714502418));
    TEST_ASSERT_EQUAL_size_t(0, records.records.size());

    pressAndReleaseOk(app, 20100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    for (std::uint32_t i = 700; i < 1500; ++i) {
        app.onFlowPulse(21000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 23000, 23000000, 1714502421));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::Completed),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
    TEST_ASSERT_EQUAL_size_t(0, pulseTraces.count());
}

void test_app_controller_stop_down_closes_valve_and_records_user_stop() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 300; ++i) {
        app.onFlowPulse(1000000UL + i * 3000UL);
    }
    app.tick(input({false, false, false, false}, 1500, 1500000, 1714502400));

    app.tick(input({true, false, false, false}, 1600, 1600000, 1714502401));
    app.tick(input({true, false, false, false}, 1600 + kButtonDebounceMs, (1600 + kButtonDebounceMs) * 1000UL, 1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::StoppedByUser),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(300, records.records[0].volumeMl);
}

void test_app_controller_normal_output_does_not_collect_ram_pulse_trace() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    WaterPulseTrace traces[2]{};
    WaterPulseTraceBucketSample buckets[700]{};
    WaterPulseTraceSample startupEdges[700]{};
    WaterPulseTraceStore pulseTraces(traces, 2, buckets, 700, startupEdges, 700, 2);
    AppController app(config, statistics, filters, records, &pulseTraces);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 232; ++i) {
        app.onFlowPulse(1000000UL + i * 3000UL);
    }
    app.tick(input({false, false, false, false}, 1500, 1500000, 1714502400));

    TEST_ASSERT_TRUE(app.emergencyStop(1600));
    app.tick(input({true, false, false, false}, 1600, 1600000, 1714502401));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::StoppedByUser),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_size_t(0, pulseTraces.count());
    TEST_ASSERT_NULL(pulseTraces.findByRecord(records.records[0]));
}

void test_app_controller_emergency_stop_closes_valve_without_debounce() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);

    TEST_ASSERT_TRUE(app.emergencyStop(1000));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

void test_app_controller_applies_config_only_while_idle() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    SystemConfig updated = config;
    updated.presets[0].value = 2000;
    TEST_ASSERT_TRUE(app.canApplyConfig());
    TEST_ASSERT_TRUE(app.applyConfig(updated));
    TEST_ASSERT_EQUAL_UINT32(2000, app.snapshot().water.targetValue);

    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_FALSE(app.canApplyConfig());
    updated.presets[0].value = 3000;
    TEST_ASSERT_FALSE(app.applyConfig(updated));
}

void test_app_controller_emits_beep_patterns_for_actions_and_completion() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::Click),
                            static_cast<std::uint8_t>(app.consumeBeepPattern()));
    pressAndReleaseOk(app, 300);
    app.consumeBeepPattern();
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::Done),
                            static_cast<std::uint8_t>(app.consumeBeepPattern()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BeepPattern::None),
                            static_cast<std::uint8_t>(app.consumeBeepPattern()));
}

void test_app_controller_reports_record_write_failure_without_losing_statistics() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    records.ok = false;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_TRUE(records.records.empty());
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
}

void test_app_controller_adjusts_volume_target_with_configured_step_without_ok_long_toggle() {
    SystemConfig config = makeDefaultConfig();
    config.volumeAdjustStepMl = 250;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);
    TEST_ASSERT_EQUAL_UINT32(250, app.snapshot().adjustmentStepMl);

    pressAndReleasePlus(app, 300);
    TEST_ASSERT_EQUAL_UINT32(1750, app.snapshot().water.targetValue);
    longPressOk(app, 500);
    TEST_ASSERT_EQUAL_UINT32(250, app.snapshot().adjustmentStepMl);
    pressAndReleaseMinus(app, 1700);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);

    pressAndReleaseOk(app, 1900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

void test_app_controller_adjusts_time_target_with_configured_step() {
    SystemConfig config = makeDefaultConfig();
    config.presets[0].type = PresetType::Time;
    config.presets[0].value = 60;
    config.timeAdjustStepSec = 15;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterMode::Time),
                            static_cast<std::uint8_t>(app.snapshot().water.mode));
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().water.targetValue);
    TEST_ASSERT_EQUAL_UINT32(15, app.snapshot().timeAdjustmentStepSec);

    pressAndReleasePlus(app, 300);
    TEST_ASSERT_EQUAL_UINT32(75, app.snapshot().water.targetValue);
    longPressOk(app, 500);
    TEST_ASSERT_EQUAL_UINT32(15, app.snapshot().timeAdjustmentStepSec);
    pressAndReleaseMinus(app, 1700);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().water.targetValue);
}

void test_app_controller_stopped_volume_does_not_clamp_next_confirm_adjustment() {
    SystemConfig config = makeDefaultConfig();
    config.presets[0].value = 1500;
    config.presets[1].value = 1000;
    config.resultDisplaySec = 0;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1390; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));
    pressAndReleaseOk(app, 5100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Paused),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    app.tick(input({true, false, false, false}, 5300, 5300000, 1714502401));
    app.tick(input({true, false, false, false}, 5300 + kButtonDebounceMs, (5300 + kButtonDebounceMs) * 1000UL, 1714502401));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    pressAndReleasePlus(app, 5500);
    TEST_ASSERT_EQUAL_size_t(1, app.snapshot().water.selectedPreset);
    pressAndReleaseOk(app, 5700);
    TEST_ASSERT_EQUAL_UINT32(1000, app.snapshot().water.targetValue);
    pressAndReleasePlus(app, 5900);
    TEST_ASSERT_EQUAL_UINT32(1100, app.snapshot().water.targetValue);
}

void test_app_controller_result_display_exits_after_configured_timeout() {
    SystemConfig config = makeDefaultConfig();
    config.resultDisplaySec = 2;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    app.tick(input({false, false, false, false}, 7000, 7000000, 1714502402));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
}

void test_app_controller_result_ok_hold_stays_on_result_without_saving_record_calibration() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    AppController app(config, statistics, filters, records, nullptr, &calibrations);
    applyTestMeteringScheme(app);

    finishVolumeRun(app);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_TRUE(app.snapshot().lastResultRecordAvailable);

    app.tick(input({false, true, false, false}, 6000, 6000000, 1714502401));
    app.tick(input({false, true, false, false}, 6000 + kButtonDebounceMs + kButtonLongPressMs,
                   (6000 + kButtonDebounceMs + kButtonLongPressMs) * 1000UL,
                   1714502402));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    app.tick(input({false, true, false, false}, 11000, 11000000, 1714502406));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Result),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_EQUAL_size_t(0, calibrations.calibrations.size());
}

void test_app_controller_snapshot_reports_current_flow_rate() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app, 1000);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    app.onFlowPulse(1000000UL);
    app.onFlowPulse(2000000UL);
    app.tick(input({false, false, false, false}, 2100, 2100000UL, 1714502400));

    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().instantFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(60, app.snapshot().displayFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(120, app.snapshot().runAverageFlowMlPerMin);

    app.tick(input({false, false, false, false}, 5100, 5100000UL, 1714502403));

    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().instantFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().displayFlowMlPerMin);
}

void test_app_controller_uses_window_flow_for_high_flow_safety() {
    SystemConfig config = makeDefaultConfig();
    config.highFlowMlPerMin = 1000;
    config.highFlowDurationSec = 1;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app, 1000);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    app.onFlowPulse(1000000UL);
    app.onFlowPulse(1001000UL);
    app.tick(input({false, false, false, false}, 1100, 1100000UL, 1714502400));
    TEST_ASSERT_GREATER_THAN_UINT32(1000, app.snapshot().instantFlowMlPerMin);
    TEST_ASSERT_LESS_THAN_UINT32(1000, app.snapshot().windowFlowMlPerMin);

    app.tick(input({false, false, false, false}, 2200, 2200000UL, 1714502401));
    TEST_ASSERT_FALSE(records.records.size() > 0 &&
                      records.records.back().result == WaterResult::FlowError);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
}

