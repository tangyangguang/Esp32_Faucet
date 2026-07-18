void test_app_controller_uses_active_scheme_parameters_for_flow_meter() {
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

    TEST_ASSERT_EQUAL_UINT32(1000, app.snapshot().pulsePerLiter);
}

void test_app_snapshot_contains_water_sensor_snapshot() {
    SensorAppFixture fixture;
    fixture.setDefaultSensorReadings();

    fixture.app.tick(input({false, false, false, false}, 1000, 1000000, 1714502400));

    const AppSnapshot snapshot = fixture.app.snapshot();
    TEST_ASSERT_TRUE(snapshot.sensors.inputVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(12001, snapshot.sensors.inputVoltageMv.value);
    TEST_ASSERT_TRUE(snapshot.sensors.temperatureCentiC.valid);
    TEST_ASSERT_TRUE(snapshot.sensors.tdsPpm.valid);
    TEST_ASSERT_TRUE(snapshot.temperatureSensorEnabled);
    TEST_ASSERT_TRUE(snapshot.tdsSensorEnabled);
}

void test_temperature_reference_calibration_sets_offset_from_raw_temperature() {
    SystemConfig config = enabledWaterSensorConfig();
    config.temperatureOffsetCentiC = 120;
    config.temperatureCalibrated = true;
    SensorAppFixture fixture(config);
    fixture.setDefaultSensorReadings();

    fixture.app.tick(input({false, false, false, false}, 1000, 1000000, 1714502400));

    const AppSnapshot before = fixture.app.snapshot();
    TEST_ASSERT_TRUE(before.sensors.temperatureRawCentiC.valid);
    TEST_ASSERT_TRUE(fixture.app.saveTemperatureCalibrationForWeb(
        static_cast<std::int16_t>(before.sensors.temperatureRawCentiC.value + 60)));
    TEST_ASSERT_TRUE(fixture.app.config().temperatureCalibrated);
    TEST_ASSERT_EQUAL_INT16(60, fixture.app.config().temperatureOffsetCentiC);
}

void test_temperature_reference_calibration_rejects_disabled_temperature_sensor() {
    SensorAppFixture fixture(makeDefaultConfig());
    fixture.adc.values[1] = okMv(1650);

    fixture.app.tick(input({false, false, false, false}, 1000, 1000000, 1714502400));

    TEST_ASSERT_FALSE(fixture.app.saveTemperatureCalibrationForWeb(2500));
    TEST_ASSERT_FALSE(fixture.app.config().temperatureCalibrated);
}

void test_app_records_sensor_summary_on_completed_run() {
    SensorAppFixture fixture;
    fixture.setDefaultSensorReadings();
    applyTestMeteringScheme(fixture.app);

    fixture.app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(fixture.app, 100);
    pressAndReleaseOk(fixture.app, 300);
    fixture.app.tick(input({false, false, false, false}, 1500, 1500000, 1714502397));
    for (std::uint32_t i = 0; i < 1500; ++i) {
        fixture.app.onFlowPulse(1000000UL + i * 2000UL);
    }
    fixture.app.tick(input({false, false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_size_t(1, fixture.records.records.size());
    TEST_ASSERT_GREATER_THAN_UINT16(0, fixture.records.records[0].sensorSampleCount);
    TEST_ASSERT_INT_WITHIN(50, 2500, fixture.records.records[0].temperatureCentiC);
    TEST_ASSERT_EQUAL_UINT16(17, fixture.records.records[0].tdsPpm);
}

void test_app_rejects_tds_calibration_when_running() {
    SensorAppFixture fixture;
    applyTestMeteringScheme(fixture.app);
    fixture.app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(fixture.app, 100);
    pressAndReleaseOk(fixture.app, 300);

    TEST_ASSERT_FALSE(fixture.app.startTdsCalibrationSessionForWeb(1714502401));
    TEST_ASSERT_FALSE(fixture.app.startTdsCalibrationPointForWeb(160, 1714502401));
}

void test_app_tds_point_calibration_apply_persists_to_config() {
    SensorAppFixture fixture;
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);

    TEST_ASSERT_TRUE(fixture.app.startTdsCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_TRUE(fixture.app.startTdsCalibrationPointForWeb(160, 1714502401));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(420);
        nowMs += 1000;
        fixture.app.tick(input({false, false, false, false}, nowMs, nowMs * 1000UL, 1714502401));
    }
    TEST_ASSERT_TRUE(fixture.app.saveTdsCalibrationPointForWeb(1714502420));
    TEST_ASSERT_TRUE(fixture.app.applyTdsCalibrationForWeb());

    const SystemConfig updated = fixture.app.config();
    TEST_ASSERT_TRUE(updated.tdsCalibrated);
    TEST_ASSERT_TRUE(updated.tdsScale > 0.0f);
}

void test_app_expires_idle_tds_calibration_session_from_tick() {
    SensorAppFixture fixture;

    TEST_ASSERT_TRUE(fixture.app.startTdsCalibrationSessionForWeb(1714502400));

    fixture.app.tick(input({false, false, false, false}, 1000, 1000000UL, 1714502400 + 30UL * 60UL));

    TEST_ASSERT_FALSE(fixture.app.tdsCalibrationSnapshot().sessionActive);
}
