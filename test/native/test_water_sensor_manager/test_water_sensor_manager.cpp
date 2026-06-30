#include <unity.h>

#include "app/WaterSensorManager.h"
#include "../support/FakeAdcReader.h"

using namespace faucet;
using faucet_test::FakeAdcReader;
using faucet_test::okMv;

namespace {

SystemConfig enabledSensorConfig() {
    SystemConfig config = makeDefaultConfig();
    config.temperatureEnabled = true;
    config.temperatureKind = TemperatureKind::Ntc50kB3950;
    config.tdsEnabled = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.tdsCalibrated = true;
    config.tdsCalibrationMode = TdsCalibrationMode::TwoPoint;
    return config;
}

void advanceSample(WaterSensorManager& manager, std::uint32_t& nowMs) {
    nowMs += 1000;
    manager.tick(nowMs);
}

struct SensorManagerFixture {
    FakeAdcReader adc;
    SystemConfig config;
    WaterSensorManager manager;

    explicit SensorManagerFixture(const SystemConfig& initialConfig = enabledSensorConfig(),
                                  bool sampleInputVoltage = true)
        : config(initialConfig),
          manager(adc, sampleInputVoltage) {
        manager.configure(config);
        TEST_ASSERT_TRUE(manager.begin());
    }

    void setDefaultReadings() {
        adc.values[0] = okMv(1091);
        adc.values[1] = okMv(1650);
        adc.values[2] = okMv(24);
    }
};

}  // namespace

void test_manager_samples_a0_input_voltage_a1_temp_a2_tds() {
    SensorManagerFixture fixture;
    fixture.setDefaultReadings();
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);

    const WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_TRUE(snapshot.inputVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(12001, snapshot.inputVoltageMv.value);
    TEST_ASSERT_TRUE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_INT_WITHIN(50, 2500, snapshot.temperatureCentiC.value);
    TEST_ASSERT_TRUE(snapshot.tdsPpm.valid);
    TEST_ASSERT_EQUAL_INT32(10, snapshot.tdsPpm.value);
    TEST_ASSERT_TRUE(snapshot.tdsVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(24, snapshot.tdsVoltageMv.value);
    TEST_ASSERT_EQUAL_size_t(1, fixture.adc.readCount[0]);
    TEST_ASSERT_EQUAL_size_t(1, fixture.adc.readCount[1]);
    TEST_ASSERT_EQUAL_size_t(1, fixture.adc.readCount[2]);
}

void test_manager_can_skip_input_voltage_when_only_water_sensors_are_wired() {
    SensorManagerFixture fixture(enabledSensorConfig(), false);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.values[2] = okMv(24);
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);

    const WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_FALSE(snapshot.inputVoltageMv.valid);
    TEST_ASSERT_TRUE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_TRUE(snapshot.tdsPpm.valid);
    TEST_ASSERT_TRUE((snapshot.flags & kWaterSensorFlagAdcOffline) == 0);
    TEST_ASSERT_EQUAL_size_t(0, fixture.adc.readCount[0]);
    TEST_ASSERT_EQUAL_size_t(3, fixture.adc.readCount[1]);
    TEST_ASSERT_EQUAL_size_t(3, fixture.adc.readCount[2]);
}

void test_manager_marks_adc_offline_after_three_failures() {
    SensorManagerFixture fixture;
    fixture.adc.failAll = true;
    std::uint32_t nowMs = 0;

    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);

    TEST_ASSERT_TRUE((fixture.manager.snapshot().flags & kWaterSensorFlagAdcOffline) != 0);
}

void test_manager_recovers_after_three_successes() {
    SensorManagerFixture fixture;
    fixture.adc.failAll = true;
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);
    TEST_ASSERT_TRUE((fixture.manager.snapshot().flags & kWaterSensorFlagAdcOffline) != 0);

    fixture.adc.failAll = false;
    fixture.setDefaultReadings();
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);
    advanceSample(fixture.manager, nowMs);

    TEST_ASSERT_TRUE((fixture.manager.snapshot().flags & kWaterSensorFlagAdcOffline) == 0);
}

void test_tds_range_switches_up_at_85_percent() {
    SensorManagerFixture fixture;
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.values[2] = okMv(230);

    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(fixture.adc.ranges[2]));
    TEST_ASSERT_FALSE(fixture.manager.snapshot().tdsPpm.valid);
}

void test_tds_range_switches_down_after_eight_low_windows() {
    SensorManagerFixture fixture;
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.values[2] = okMv(230);
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(fixture.adc.ranges[2]));

    fixture.adc.values[2] = okMv(100);
    for (std::uint8_t i = 0; i < 9; ++i) {
        advanceSample(fixture.manager, nowMs);
    }

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P256),
                            static_cast<std::uint8_t>(fixture.adc.ranges[2]));
}

void test_run_summary_aggregates_valid_samples_only() {
    SensorManagerFixture fixture;
    fixture.setDefaultReadings();
    fixture.manager.beginRun();
    std::uint32_t nowMs = 0;

    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();
    fixture.adc.values[1] = okMv(1840);
    fixture.adc.values[2] = okMv(48);
    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();
    fixture.adc.values[2] = {};
    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();

    const WaterSensorRunSummary summary = fixture.manager.finishRun();
    TEST_ASSERT_EQUAL_UINT8(3, summary.sensorSampleCount);
    TEST_ASSERT_TRUE(summary.temperatureCentiC != 0);
    TEST_ASSERT_TRUE(summary.tdsPpm != 0);
    TEST_ASSERT_TRUE((summary.sensorFlags & kWaterSensorFlagTdsInvalid) != 0);
}

void test_run_summary_records_tds_when_temperature_sensor_is_disabled() {
    SystemConfig config = enabledSensorConfig();
    config.temperatureEnabled = false;
    config.temperatureKind = TemperatureKind::None;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = {};
    fixture.adc.values[2] = okMv(24);
    fixture.manager.beginRun();
    std::uint32_t nowMs = 0;

    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();
    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();

    const WaterSensorRunSummary summary = fixture.manager.finishRun();
    TEST_ASSERT_EQUAL_UINT8(2, summary.sensorSampleCount);
    TEST_ASSERT_EQUAL_UINT16(10, summary.tdsPpm);
    TEST_ASSERT_FALSE((summary.sensorFlags & kWaterSensorFlagTempInvalid) != 0);
    TEST_ASSERT_TRUE((summary.sensorFlags & kWaterSensorFlagTdsTempFallback25C) != 0);
}

void test_calibration_uses_25c_fallback_without_failing() {
    SystemConfig config = makeDefaultConfig();
    config.tdsEnabled = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.temperatureEnabled = false;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = {};
    fixture.adc.values[2] = okMv(24);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(10, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(fixture.manager, nowMs);
    }

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.samplingActive);
    TEST_ASSERT_TRUE(session.readyToSave);
    TEST_ASSERT_TRUE(session.tempFallback25C);
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config, 1720000021UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::SinglePoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
}

void test_two_point_calibration_saves_low_then_high_without_flash_progress_dependency() {
    SystemConfig config = enabledSensorConfig();
    config.tdsCalibrated = false;
    config.tdsCalibrationMode = TdsCalibrationMode::None;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.values[2] = okMv(12);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(0, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.calibrationSnapshot().readyToSave);
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_FALSE(config.tdsCalibrated);

    fixture.adc.values[2] = okMv(380);
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(fixture.manager, nowMs);
    }

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.readyToSave);
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000050UL));
    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config, 1720000051UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_point_session_generates_after_one_point() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(380);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.sessionActive);
    TEST_ASSERT_EQUAL_UINT8(1, session.pointCount);
    TEST_ASSERT_TRUE(session.candidateReady);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::SinglePoint),
                            static_cast<std::uint8_t>(session.candidateMode));
}

void test_tds_calibration_session_rejects_duplicate_start_without_clearing_points() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_FALSE(fixture.manager.startTdsCalibrationSession(1720000030UL));

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.sessionActive);
    TEST_ASSERT_EQUAL_UINT8(1, session.pointCount);
    TEST_ASSERT_TRUE(session.candidateReady);
    TEST_ASSERT_EQUAL_UINT16(160, session.points[0].referencePpm);
}

void test_tds_calibration_point_session_multi_point_fit_and_apply() {
    SystemConfig config = enabledSensorConfig();
    SensorManagerFixture fixture(config);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(20, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(160);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config, 1720000050UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_apply_is_order_independent() {
    SystemConfig config = enabledSensorConfig();
    SensorManagerFixture fixture(config);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(20, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(160);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config, 1720000050UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_point_removal_recomputes_candidate() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.values[2] = okMv(420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_TRUE(fixture.manager.removeTdsCalibrationPoint(0, 1720000030UL));

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_EQUAL_UINT8(0, session.pointCount);
    TEST_ASSERT_FALSE(session.candidateReady);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_manager_samples_a0_input_voltage_a1_temp_a2_tds);
    RUN_TEST(test_manager_can_skip_input_voltage_when_only_water_sensors_are_wired);
    RUN_TEST(test_manager_marks_adc_offline_after_three_failures);
    RUN_TEST(test_manager_recovers_after_three_successes);
    RUN_TEST(test_tds_range_switches_up_at_85_percent);
    RUN_TEST(test_tds_range_switches_down_after_eight_low_windows);
    RUN_TEST(test_run_summary_aggregates_valid_samples_only);
    RUN_TEST(test_run_summary_records_tds_when_temperature_sensor_is_disabled);
    RUN_TEST(test_calibration_uses_25c_fallback_without_failing);
    RUN_TEST(test_two_point_calibration_saves_low_then_high_without_flash_progress_dependency);
    RUN_TEST(test_tds_calibration_point_session_generates_after_one_point);
    RUN_TEST(test_tds_calibration_session_rejects_duplicate_start_without_clearing_points);
    RUN_TEST(test_tds_calibration_point_session_multi_point_fit_and_apply);
    RUN_TEST(test_tds_calibration_apply_is_order_independent);
    RUN_TEST(test_tds_calibration_point_removal_recomputes_candidate);
    return UNITY_END();
}
