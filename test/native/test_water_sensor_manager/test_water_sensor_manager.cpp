#include <unity.h>

#include "app/WaterSensorManager.h"
#include "../support/FakeAdcReader.h"

using namespace faucet;
using faucet_test::FakeAdcReader;
using faucet_test::okMv;

namespace {

SystemConfig enabledSensorConfig() {
    SystemConfig config = makeDefaultConfig();
    config.temperatureKind = TemperatureKind::NtcBeta;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.tdsCalibrated = true;
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
        adc.setAnalogMillivolts(AdcChannel::A2, 24);
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
    TEST_ASSERT_EQUAL_INT32(17, snapshot.tdsPpm.value);
    TEST_ASSERT_TRUE(snapshot.tdsVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(40, snapshot.tdsVoltageMv.value);
    TEST_ASSERT_EQUAL_size_t(1, fixture.adc.readCount[0]);
    TEST_ASSERT_EQUAL_size_t(1, fixture.adc.readCount[1]);
    TEST_ASSERT_EQUAL_size_t(1, fixture.adc.readCount[2]);
}

void test_manager_can_skip_input_voltage_when_only_water_sensors_are_wired() {
    SensorManagerFixture fixture(enabledSensorConfig(), false);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 24);
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

void test_temperature_open_and_short_display_as_invalid_without_changing_tds_zero() {
    SensorManagerFixture fixture;
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(3290);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 0);
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);

    WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_FALSE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_TRUE((snapshot.flags & kWaterSensorFlagTempOpen) != 0);
    TEST_ASSERT_TRUE(snapshot.tdsPpm.valid);
    TEST_ASSERT_EQUAL_INT32(0, snapshot.tdsPpm.value);

    fixture.adc.values[1] = okMv(10);
    advanceSample(fixture.manager, nowMs);
    snapshot = fixture.manager.snapshot();
    TEST_ASSERT_FALSE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_TRUE((snapshot.flags & kWaterSensorFlagTempShort) != 0);
}

void test_temperature_open_detection_allows_3v3_supply_tolerance_but_keeps_zero_c_valid() {
    SensorManagerFixture fixture;
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(3150);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 0);
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);

    WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_FALSE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_TRUE((snapshot.flags & kWaterSensorFlagTempOpen) != 0);

    fixture.adc.values[1] = okMv(2535);
    advanceSample(fixture.manager, nowMs);
    snapshot = fixture.manager.snapshot();
    TEST_ASSERT_TRUE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_INT_WITHIN(150, 0, snapshot.temperatureCentiC.value);
}

void test_input_voltage_calibration_saves_raw_points_and_recomputes_fit() {
    SensorManagerFixture fixture;
    fixture.setDefaultReadings();
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 5; ++i) {
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveInputVoltageCalibrationPoint(fixture.config, 12000, 1720000000));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.config.inputVoltageCalibration.pointCount);
    TEST_ASSERT_TRUE(fixture.config.inputVoltageCalibration.calibrated);
    TEST_ASSERT_INT_WITHIN(5, 8728, fixture.config.inputVoltageCalibration.points[0].adcRaw);
    TEST_ASSERT_EQUAL_UINT32(12001, fixture.config.inputVoltageCalibration.points[0].theoreticalInputMillivolts);
    TEST_ASSERT_EQUAL_INT32(12000, fixture.manager.snapshot().inputVoltageMv.value);

    fixture.adc.values[0] = okMv(1500);
    for (std::uint8_t i = 0; i < 5; ++i) {
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveInputVoltageCalibrationPoint(fixture.config, 16400, 1720000010));
    TEST_ASSERT_EQUAL_UINT8(2, fixture.config.inputVoltageCalibration.pointCount);
    TEST_ASSERT_TRUE(fixture.config.inputVoltageCalibration.gainPpm < 1000000);
    TEST_ASSERT_TRUE(fixture.manager.removeInputVoltageCalibrationPoint(fixture.config, 0));
    TEST_ASSERT_EQUAL_UINT8(1, fixture.config.inputVoltageCalibration.pointCount);
    TEST_ASSERT_TRUE(fixture.manager.clearInputVoltageCalibration(fixture.config));
    TEST_ASSERT_EQUAL_UINT8(0, fixture.config.inputVoltageCalibration.pointCount);
    TEST_ASSERT_FALSE(fixture.config.inputVoltageCalibration.calibrated);
}

void test_input_voltage_calibration_point_edit_recomputes_without_changing_raw_capture() {
    SensorManagerFixture fixture;
    fixture.setDefaultReadings();
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 5; ++i) {
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveInputVoltageCalibrationPoint(
        fixture.config, 12000, 1720000000));
    const InputVoltageCalibrationPoint captured =
        fixture.config.inputVoltageCalibration.points[0];

    TEST_ASSERT_TRUE(fixture.manager.updateInputVoltageCalibrationPoint(
        fixture.config, 0, 12100));

    const InputVoltageCalibrationPoint& updated =
        fixture.config.inputVoltageCalibration.points[0];
    TEST_ASSERT_EQUAL_INT16(captured.adcRaw, updated.adcRaw);
    TEST_ASSERT_EQUAL_UINT32(captured.theoreticalInputMillivolts,
                             updated.theoreticalInputMillivolts);
    TEST_ASSERT_EQUAL_UINT32(12100, updated.actualInputMillivolts);
    TEST_ASSERT_EQUAL_INT32(12100, fixture.manager.snapshot().inputVoltageMv.value);
}

void test_input_voltage_uses_recent_raw_median_and_rejects_unstable_capture() {
    SensorManagerFixture fixture;
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 24);
    const std::int16_t readings[5] = {247, 248, 280, 249, 250};
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 5; ++i) {
        fixture.adc.values[0] = okMv(readings[i]);
        advanceSample(fixture.manager, nowMs);
    }

    const WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_EQUAL_INT32(okMv(249).raw, snapshot.inputVoltageAdcRaw.value);
    TEST_ASSERT_FALSE(snapshot.inputVoltageStable);
    TEST_ASSERT_TRUE(snapshot.inputVoltageWindowSpanMv > 10);
    TEST_ASSERT_FALSE(fixture.manager.saveInputVoltageCalibrationPoint(fixture.config, 2740, 1720000000));
}

void test_configured_input_voltage_divider_is_used_by_live_sampling() {
    SystemConfig config = enabledSensorConfig();
    config.inputVoltageDividerHighOhm = 50000;
    config.inputVoltageDividerLowOhm = 10000;
    SensorManagerFixture fixture(config);
    fixture.setDefaultReadings();
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);

    TEST_ASSERT_EQUAL_INT32(6546, fixture.manager.snapshot().inputVoltageMv.value);
}

void test_tds_hardware_change_discards_in_progress_calibration_points() {
    SensorManagerFixture fixture;
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001));

    fixture.config.tdsDividerHighOhm = 12000;
    fixture.manager.configure(fixture.config);

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_FALSE(session.sessionActive);
    TEST_ASSERT_FALSE(session.samplingActive);
    TEST_ASSERT_EQUAL_UINT8(0, session.pointCount);
}

void test_configured_temperature_and_tds_hardware_parameters_are_used() {
    SystemConfig config = enabledSensorConfig();
    config.temperatureNominalOhm = 10000;
    config.temperatureBeta = 3435;
    config.temperaturePullupOhm = 10000;
    config.tdsDividerHighOhm = 15000;
    config.tdsDividerLowOhm = 15000;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 24);
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);

    const WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_INT_WITHIN(2, 2500, snapshot.temperatureRawCentiC.value);
    TEST_ASSERT_EQUAL_INT32(48, snapshot.tdsVoltageMv.value);
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
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 230);

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
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 230);
    std::uint32_t nowMs = 0;
    advanceSample(fixture.manager, nowMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(fixture.adc.ranges[2]));

    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 100);
    for (std::uint8_t i = 0; i < 9; ++i) {
        advanceSample(fixture.manager, nowMs);
    }

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P256),
                            static_cast<std::uint8_t>(fixture.adc.ranges[2]));
}

void test_tds_live_value_uses_recent_raw_median() {
    SensorManagerFixture fixture;
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);
    const std::int16_t readings[5] = {10, 12, 200, 11, 13};
    std::uint32_t nowMs = 0;
    for (std::int16_t reading : readings) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, reading);
        advanceSample(fixture.manager, nowMs);
    }

    const WaterSensorSnapshot snapshot = fixture.manager.snapshot();
    TEST_ASSERT_TRUE(snapshot.tdsVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(20, snapshot.tdsVoltageMv.value);
}

void test_run_summary_ignores_duplicate_loop_samples() {
    SensorManagerFixture fixture;
    fixture.setDefaultReadings();
    fixture.manager.beginRun();
    std::uint32_t nowMs = 0;

    advanceSample(fixture.manager, nowMs);
    for (std::uint8_t i = 0; i < 10; ++i) {
        fixture.manager.sampleRun();
    }

    const WaterSensorRunSummary summary = fixture.manager.finishRun();
    TEST_ASSERT_EQUAL_UINT8(1, summary.sensorSampleCount);
    TEST_ASSERT_EQUAL_UINT16(17, summary.tdsPpm);
}

void test_run_summary_aggregates_valid_samples_only() {
    SensorManagerFixture fixture;
    fixture.setDefaultReadings();
    fixture.manager.beginRun();
    std::uint32_t nowMs = 0;

    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();
    fixture.adc.values[1] = okMv(1840);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 48);
    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();
    fixture.adc.clearAnalogMillivolts(AdcChannel::A2);
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
    config.temperatureKind = TemperatureKind::None;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = {};
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 24);
    fixture.manager.beginRun();
    std::uint32_t nowMs = 0;

    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();
    advanceSample(fixture.manager, nowMs);
    fixture.manager.sampleRun();

    const WaterSensorRunSummary summary = fixture.manager.finishRun();
    TEST_ASSERT_EQUAL_UINT8(2, summary.sensorSampleCount);
    TEST_ASSERT_EQUAL_UINT16(17, summary.tdsPpm);
    TEST_ASSERT_TRUE((summary.sensorFlags & kWaterSensorFlagTempUnavailable) != 0);
    TEST_ASSERT_FALSE((summary.sensorFlags & kWaterSensorFlagTempInvalid) != 0);
    TEST_ASSERT_TRUE((summary.sensorFlags & kWaterSensorFlagTdsTempFallback25C) != 0);
}

void test_calibration_uses_25c_fallback_without_failing() {
    SystemConfig config = makeDefaultConfig();
    config.tdsKind = TdsKind::AnalogTdsAo;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = {};
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 24);

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
    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_saves_two_points_without_flash_progress_dependency() {
    SystemConfig config = enabledSensorConfig();
    config.tdsCalibrated = false;
    SensorManagerFixture fixture(config);
    fixture.adc.values[0] = okMv(1091);
    fixture.adc.values[1] = okMv(1650);
    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 12);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(0, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.calibrationSnapshot().readyToSave);
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_FALSE(config.tdsCalibrated);

    fixture.adc.setAnalogMillivolts(AdcChannel::A2, 380);
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(fixture.manager, nowMs);
    }

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.readyToSave);
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000050UL));
    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_point_session_generates_after_one_point() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 380);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.sessionActive);
    TEST_ASSERT_EQUAL_UINT8(1, session.pointCount);
    TEST_ASSERT_TRUE(session.candidateReady);
    TEST_ASSERT_TRUE(session.candidateScale > 0.0f);
}

void test_tds_calibration_session_rejects_duplicate_start_without_clearing_points() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 420);
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

void test_tds_calibration_point_session_combines_saved_points_and_apply() {
    SystemConfig config = enabledSensorConfig();
    SensorManagerFixture fixture(config);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(20, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 160);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_apply_is_order_independent() {
    SystemConfig config = enabledSensorConfig();
    SensorManagerFixture fixture(config);

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(20, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 160);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(fixture.manager.applyReadyTdsCalibration(config));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_TRUE(config.tdsScale > 0.0f);
}

void test_tds_calibration_point_removal_recomputes_candidate() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_TRUE(fixture.manager.removeTdsCalibrationPoint(0, 1720000030UL));

    const TdsCalibrationSessionSnapshot session = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_EQUAL_UINT8(0, session.pointCount);
    TEST_ASSERT_FALSE(session.candidateReady);
}

void test_tds_calibration_point_edit_recomputes_candidate_without_changing_capture() {
    SensorManagerFixture fixture;

    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(fixture.manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        fixture.adc.setAnalogMillivolts(AdcChannel::A2, 420);
        advanceSample(fixture.manager, nowMs);
    }
    TEST_ASSERT_TRUE(fixture.manager.saveStableTdsCalibrationPoint(1720000020UL));
    const TdsCalibrationSessionSnapshot before = fixture.manager.calibrationSnapshot();

    TEST_ASSERT_TRUE(fixture.manager.updateTdsCalibrationPoint(0, 170, 1720000030UL));

    const TdsCalibrationSessionSnapshot after = fixture.manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(after.candidateReady);
    TEST_ASSERT_EQUAL_UINT16(170, after.points[0].referencePpm);
    TEST_ASSERT_EQUAL_UINT16(before.points[0].rawPpm, after.points[0].rawPpm);
    TEST_ASSERT_EQUAL_UINT16(before.points[0].voltageMv, after.points[0].voltageMv);
    TEST_ASSERT_TRUE(after.candidateScale > before.candidateScale);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_manager_samples_a0_input_voltage_a1_temp_a2_tds);
    RUN_TEST(test_manager_can_skip_input_voltage_when_only_water_sensors_are_wired);
    RUN_TEST(test_temperature_open_and_short_display_as_invalid_without_changing_tds_zero);
    RUN_TEST(test_temperature_open_detection_allows_3v3_supply_tolerance_but_keeps_zero_c_valid);
    RUN_TEST(test_input_voltage_calibration_saves_raw_points_and_recomputes_fit);
    RUN_TEST(test_input_voltage_calibration_point_edit_recomputes_without_changing_raw_capture);
    RUN_TEST(test_input_voltage_uses_recent_raw_median_and_rejects_unstable_capture);
    RUN_TEST(test_configured_input_voltage_divider_is_used_by_live_sampling);
    RUN_TEST(test_tds_hardware_change_discards_in_progress_calibration_points);
    RUN_TEST(test_configured_temperature_and_tds_hardware_parameters_are_used);
    RUN_TEST(test_manager_marks_adc_offline_after_three_failures);
    RUN_TEST(test_manager_recovers_after_three_successes);
    RUN_TEST(test_tds_range_switches_up_at_85_percent);
    RUN_TEST(test_tds_range_switches_down_after_eight_low_windows);
    RUN_TEST(test_tds_live_value_uses_recent_raw_median);
    RUN_TEST(test_run_summary_ignores_duplicate_loop_samples);
    RUN_TEST(test_run_summary_aggregates_valid_samples_only);
    RUN_TEST(test_run_summary_records_tds_when_temperature_sensor_is_disabled);
    RUN_TEST(test_calibration_uses_25c_fallback_without_failing);
    RUN_TEST(test_tds_calibration_saves_two_points_without_flash_progress_dependency);
    RUN_TEST(test_tds_calibration_point_session_generates_after_one_point);
    RUN_TEST(test_tds_calibration_session_rejects_duplicate_start_without_clearing_points);
    RUN_TEST(test_tds_calibration_point_session_combines_saved_points_and_apply);
    RUN_TEST(test_tds_calibration_apply_is_order_independent);
    RUN_TEST(test_tds_calibration_point_removal_recomputes_candidate);
    RUN_TEST(test_tds_calibration_point_edit_recomputes_candidate_without_changing_capture);
    return UNITY_END();
}
