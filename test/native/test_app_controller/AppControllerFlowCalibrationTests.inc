void test_app_controller_starting_calibration_from_idle_waits_for_local_run() {
    CalibrationAppFixture fixture;
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(fixture.app->snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(0, fixture.app->snapshot().calibrationAttemptCount);
    TEST_ASSERT_EQUAL_UINT8(0, fixture.app->snapshot().calibrationValidSampleCount);
    TEST_ASSERT_FALSE(fixture.app->snapshot().water.valveOpen);
}

void test_app_controller_starting_calibration_while_running_is_rejected() {
    CalibrationAppFixture fixture;
    fixture.createApp();

    fixture.app->resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(*fixture.app, 100);
    pressAndReleaseOk(*fixture.app, 300);

    TEST_ASSERT_FALSE(fixture.app->startCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(fixture.app->snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Idle),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
}

void test_app_controller_starting_calibration_twice_is_rejected() {
    CalibrationAppFixture fixture;
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_FALSE(fixture.app->startCalibrationSessionForWeb(1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(fixture.app->snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
}

void test_app_controller_flow_calibration_session_does_not_time_out() {
    CalibrationAppFixture fixture;
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));

    fixture.app->tick(input({false, false, false, false}, 86400000, 1000000UL, 1714588800));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(fixture.app->snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
}

void test_app_controller_generated_calibration_restores_candidate_and_stays_active_without_idle_timeout() {
    CalibrationAppFixture fixture;

    CalibrationSessionRecord session = makeCalibrationSession(79, 1714502400);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(fixture.traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::Generated;
    session.validSampleCount = countValidCalibrationSamples(session);
    session.updatedAt = 1714502500;
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));

    fixture.createApp();
    fixture.app->tick(input({false, false, false, false}, 1000, 1000000UL, 1714588800));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->snapshot().calibrationCandidate.ready);

    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.updatedAt = 1714505000;
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    delete fixture.app;
    fixture.app = nullptr;
    fixture.createApp();
    fixture.app->tick(input({false, false, false, false}, 3000, 3000000UL, 1714588800));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->snapshot().calibrationCandidate.ready);
}

void test_app_controller_reboot_drops_awaiting_actual_when_ram_trace_missing() {
    CalibrationAppFixture fixture;

    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    session.status = CalibrationSessionStatus::AwaitingActual;
    session.attemptCount = 1;
    session.attempts[0].attemptIndex = 0;
    session.attempts[0].sessionTraceSlot = 0;
    session.attempts[0].record = calibrationRecord(1714502410, 500, 500);
    session.attempts[0].targetHintMl = 500;
    session.attempts[0].status = CalibrationAttemptStatus::PendingActual;
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.createApp();

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Calibration),
                            static_cast<std::uint8_t>(fixture.app->snapshot().localMode));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.app->snapshot().calibrationAttemptCount);
    TEST_ASSERT_EQUAL_UINT8(0, fixture.app->snapshot().calibrationValidSampleCount);
}

void test_app_controller_local_ok_starts_calibration_run_and_completion_awaits_actual() {
    CalibrationAppFixture fixture;
    fixture.active.params = MeteringParameters{0, 0, 1000};
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));
    fixture.app->resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(*fixture.app, 300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Running),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->snapshot().water.valveOpen);

    for (std::uint32_t i = 0; i < 500; ++i) {
        fixture.app->onFlowPulse(1000000UL + i * 2000UL);
    }
    fixture.app->tick(input({true, false, false, false}, 1600, 1600000, 1714502401));
    fixture.app->tick(
        input({true, false, false, false}, 1600 + kButtonDebounceMs, (1600 + kButtonDebounceMs) * 1000UL, 1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::AwaitingActual),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_size_t(1, fixture.records.records.size());
    TEST_ASSERT_EQUAL_UINT32(500, fixture.statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(500, fixture.filters.record(0).usedMl);
    CalibrationStoredTrace pending{};
    TEST_ASSERT_FALSE(fixture.traceStore.load(0, pending));

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(520, 1714502402));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.app->snapshot().calibrationValidSampleCount);
    CalibrationStoredTrace valid{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(0, valid));
    TEST_ASSERT_TRUE(valid.valid);
    TEST_ASSERT_EQUAL_UINT32(520, valid.actualMl);
    WaterPulseTraceBucketSample copied[64]{};
    TEST_ASSERT_EQUAL_size_t(valid.trace.bucketCount, fixture.traceStore.readBuckets(0, copied, 64));
}

void test_app_controller_saves_long_high_pulse_calibration_without_bucket_overflow() {
    CompactTraceCalibrationAppFixture fixture;
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));
    fixture.app->resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(*fixture.app, 300);

    for (std::uint32_t i = 0; i < 15750; ++i) {
        fixture.app->onFlowPulse(1000000UL + i * 14285UL);
    }
    fixture.app->tick(input({true, false, false, false}, 225000, 225000000UL, 1714502625));
    fixture.app->tick(input({true, false, false, false},
                            225000 + kButtonDebounceMs,
                            (225000 + kButtonDebounceMs) * 1000UL,
                            1714502625));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::AwaitingActual),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502630));

    CalibrationStoredTrace stored{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(0, stored));
    TEST_ASSERT_TRUE(stored.valid);
    TEST_ASSERT_EQUAL_UINT32(15750, stored.trace.totalPulses);
    TEST_ASSERT_TRUE(stored.trace.bucketCount > 400);
    TEST_ASSERT_TRUE(stored.trace.startupEdgeCount > 900);
    TEST_ASSERT_FALSE((stored.trace.flags & kPulseTraceFlagBucketOverflow) != 0);
}

void test_app_controller_calibration_run_ignores_preset_target_until_local_cancel() {
    CalibrationAppFixture fixture;
    fixture.config.presets[0].value = 1500;
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));
    fixture.app->resetInputs({false, false, false, false}, 0);
    pressAndReleaseOkAt(*fixture.app, 300, 1714502400);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Running),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.app->snapshot().water.targetValue);

    for (std::uint32_t i = 0; i < 500; ++i) {
        fixture.app->onFlowPulse(1000000UL + i * 5000UL);
    }
    fixture.app->tick(input({false, false, false, false}, 4200, 4200000UL, 1714502404));

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Running),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Running),
                            static_cast<unsigned>(fixture.app->snapshot().water.state));
    TEST_ASSERT_GREATER_THAN_UINT32(fixture.config.presets[0].value, fixture.app->snapshot().water.volumeMl);
    TEST_ASSERT_EQUAL_size_t(0, fixture.records.records.size());

    pressAndReleaseCancelAt(*fixture.app, 4300, 1714502404);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::AwaitingActual),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_size_t(1, fixture.records.records.size());
    TEST_ASSERT_EQUAL_UINT32(0, fixture.records.records[0].targetValue);
    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::AwaitingActual),
                            static_cast<unsigned>(persisted.status));
}

void test_app_controller_generates_calibration_session_candidate() {
    CalibrationAppFixture fixture;

    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(fixture.traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.createApp();
    MeteringSchemeRecord beforeGenerate[4]{};
    const std::size_t beforeGenerateCount = fixture.schemes.list(beforeGenerate, 4);

    MeteringSchemeRecord list[4]{};
    TEST_ASSERT_EQUAL_size_t(beforeGenerateCount, fixture.schemes.list(list, 4));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->snapshot().calibrationCandidate.ready);
}

void test_app_controller_auto_generates_after_second_valid_calibration_sample() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));

    const AppSnapshot snapshot = fixture.app->snapshot();
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(snapshot.calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->applyGeneratedCalibrationForWeb(1714502600));
}

void test_app_controller_saves_unstable_actual_without_counting_valid_sample() {
    CalibrationAppFixture fixture;
    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    const std::uint32_t unstableEdges[] = {
        0,
        1000000UL,
        2000000UL,
        3000000UL,
        4000000UL,
    };
    savePendingRamCalibrationAttemptWithPulseEdges(
        fixture, session, 1, 1714502410, 7500, unstableEdges, 5, 8);
    session.status = CalibrationSessionStatus::AwaitingActual;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));

    const AppSnapshot snapshot = fixture.app->snapshot();
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(snapshot.calibrationStatus));
    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Valid),
                            static_cast<unsigned>(persisted.attempts[1].status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationInvalidReason::None),
                            static_cast<unsigned>(persisted.attempts[1].invalidReason));
    TEST_ASSERT_FALSE(persisted.attempts[1].summary.usableForGeneration);
}

void test_app_controller_generated_calibration_can_continue_collecting_samples() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();
    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));

    pressAndReleaseOkAt(*fixture.app, 2000, 1714502600);

    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Running),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(WaterState::Running),
                            static_cast<unsigned>(fixture.app->snapshot().water.state));
}

void test_app_controller_submit_actual_succeeds_when_auto_refresh_cannot_generate() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createAppWithoutMeteringStore();

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502500));

    const AppSnapshot snapshot = fixture.app->snapshot();
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(snapshot.calibrationStatus));
    CalibrationStoredTrace valid{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(1, valid));
    TEST_ASSERT_TRUE(valid.valid);
    TEST_ASSERT_EQUAL_UINT32(7500, valid.actualMl);

    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(persisted.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Valid),
                            static_cast<unsigned>(persisted.attempts[1].status));
}

void test_app_controller_pending_actual_sample_can_be_skipped() {
    CalibrationAppFixture fixture;
    saveOneValidOnePendingSession(fixture, 1714502400);
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->skipCalibrationAttemptForWeb(1714502500));

    CalibrationSessionRecord persisted{};
    TEST_ASSERT_TRUE(fixture.sessionStore.load(persisted));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationAttemptStatus::Skipped),
                            static_cast<unsigned>(persisted.attempts[1].status));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.app->snapshot().calibrationValidSampleCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
}

void test_app_controller_applies_generated_session_scheme_and_keeps_old_scheme() {
    CalibrationAppFixture fixture;
    const std::uint32_t oldActiveId = fixture.active.id;

    CalibrationSessionRecord session = makeCalibrationSession(78, 1714502400);
    saveCalibrationSessionSample(fixture.traceStore, session, 0, 1714502401, 1500, 40, 210, 6);
    saveCalibrationSessionSample(fixture.traceStore, session, 1, 1714502410, 7500, 40, 1540, 11);
    session.status = CalibrationSessionStatus::ReadyToGenerate;
    session.validSampleCount = countValidCalibrationSamples(session);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->snapshot().calibrationCandidate.ready);
    TEST_ASSERT_TRUE(fixture.app->applyGeneratedCalibrationForWeb(1714502600));

    TEST_ASSERT_NOT_EQUAL(oldActiveId, fixture.schemes.activeSchemeId());
    TEST_ASSERT_EQUAL_UINT32(fixture.schemes.activeSchemeId(), fixture.app->activeMeteringScheme().id);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::CalibrationSession),
                            static_cast<unsigned>(fixture.app->activeMeteringScheme().sourceType));
    TEST_ASSERT_UINT32_WITHIN(5, 222, fixture.app->activeMeteringScheme().params.stablePulsePerLiter);
    MeteringSchemeRecord oldScheme{};
    TEST_ASSERT_TRUE(fixture.schemes.findById(oldActiveId, oldScheme));
    TEST_ASSERT_TRUE(oldScheme.recordUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Applied),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
}

void test_app_controller_pause_timeout_trace_is_not_marked_error_and_can_calibrate() {
    SystemConfig config = makeDefaultConfig();
    config.pauseTimeoutSec = 10;
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
    for (std::uint32_t i = 0; i < 600; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false, false}, 1800, 1800000, 1714502400));
    pressAndReleaseOk(app, 1900);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Paused),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    app.tick(input({false, false, false, false}, 12100, 12100000, 1714502412));

    TEST_ASSERT_EQUAL_size_t(1, records.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::PauseTimeout),
                            static_cast<std::uint8_t>(records.records[0].result));
    TEST_ASSERT_TRUE(app.snapshot().lastResultRecordAvailable);
    TEST_ASSERT_EQUAL_size_t(0, pulseTraces.count());
}
