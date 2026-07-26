void test_app_controller_successful_record_writes_scheme_id() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes(backend, "/schemes.bin");
    MeteringSchemeRecord active{};
    TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 1000, active));
    AppController app(config, active, statistics, filters, records, schemes);

    finishVolumeRun(app);

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(active.id, records.records[0].meteringSchemeId);
}

void test_app_controller_starts_after_double_ok_and_opens_valve() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Confirm),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    pressAndReleaseOk(app, 300);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_UINT8(100, app.snapshot().valve.dutyPercent);
}

void test_app_controller_cancel_requires_normal_button_debounce() {
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

    app.tick(input({true, false, false, false}, 500, 500000, 1714502400));
    app.tick(input({false, false, false, false}, 500 + kButtonDebounceMs - 1,
                   (500 + kButtonDebounceMs - 1) * 1000UL, 1714502400));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);

    app.tick(input({true, false, false, false}, 600, 600000, 1714502400));
    app.tick(input({true, false, false, false}, 600 + kButtonDebounceMs,
                   (600 + kButtonDebounceMs) * 1000UL, 1714502400));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
}

void test_app_controller_confirm_and_running_start_volume_stays_zero_until_first_pulse() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    AppController app(config, statistics, filters, records);
    MeteringSchemeRecord scheme{};
    scheme.id = 99;
    scheme.recordUsed = true;
    std::snprintf(scheme.name, sizeof(scheme.name), "启动段测试");
    scheme.params = MeteringParameters{8, 130, 248, 5000, 1950};
    scheme.sourceType = MeteringSchemeSource::CalibrationSession;
    scheme.sampleCount = 2;
    TEST_ASSERT_TRUE(app.applyActiveMeteringScheme(scheme));

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Confirm),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().water.volumeMl);

    pressAndReleaseOk(app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().water.volumeMl);

    app.tick(input({false, false, false, false}, 900, 900000, 1714502300));
    TEST_ASSERT_EQUAL_UINT32(0, app.snapshot().water.volumeMl);

    app.onFlowPulse(1000000);
    app.tick(input({false, false, false, false}, 1200, 1200000, 1714502301));
    TEST_ASSERT_EQUAL_UINT32(16, app.snapshot().water.volumeMl);
}

void test_app_controller_completion_writes_record_statistics_and_filters() {
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

    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].volumeMl);
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].targetValue);
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, records.records[0].filteredPulseCount);
    TEST_ASSERT_EQUAL_UINT32(99, records.records[0].meteringSchemeId);
    TEST_ASSERT_EQUAL_UINT8(0, records.records[0].selectedPreset);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::Completed),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
    TEST_ASSERT_FALSE(app.consumePersistenceDirty());
    app.markPersistenceDirtyForRetry();
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
}

void test_app_controller_pushes_closed_valve_output_before_record_persistence() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    ObservingRecordWriter records;
    AppController app(config, statistics, filters, records);
    applyTestMeteringScheme(app);
    app.setValveOutputSink([](ValveOutput output) {
        if (!output.enabled) {
            gValveSinkSawClosedBeforeRecordAppend = true;
        }
    });

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    gValveSinkSawClosedBeforeRecordAppend = false;
    gRecordAppendObservedClosedValve = false;

    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_TRUE(gRecordAppendObservedClosedValve);
}

void test_app_controller_web_preset_switch_during_run_updates_next_preset_only() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.activePreset);

    TEST_ASSERT_TRUE(app.selectNextPresetForWeb());

    TEST_ASSERT_EQUAL_size_t(1, app.snapshot().water.selectedPreset);
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.activePreset);
    TEST_ASSERT_EQUAL_UINT32(1500, app.snapshot().water.targetValue);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(0, records.records[0].selectedPreset);
    TEST_ASSERT_EQUAL_UINT32(1500, records.records[0].targetValue);
    TEST_ASSERT_EQUAL_size_t(1, app.snapshot().water.selectedPreset);
}

void test_app_controller_local_plus_does_not_switch_preset_while_running() {
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

    pressAndReleasePlus(app, 500);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.selectedPreset);
    TEST_ASSERT_EQUAL_size_t(0, app.snapshot().water.activePreset);
}
