#include <unity.h>

#include "app/WaterSensorManager.h"

using namespace faucet;

namespace {

class FakeAdcReader : public AdcReader {
public:
    bool beginOk = true;
    bool failAll = false;
    AdcReadResult values[4]{};
    AdcRange ranges[4]{};
    std::size_t readCount[4]{};
    std::size_t setRangeCount[4]{};

    FakeAdcReader() {
        for (auto& range : ranges) {
            range = AdcRange::P4096;
        }
    }

    bool begin() override {
        return beginOk;
    }

    bool setRange(AdcChannel channel, AdcRange range) override {
        ranges[index(channel)] = range;
        ++setRangeCount[index(channel)];
        return true;
    }

    AdcReadResult readSingleEnded(AdcChannel channel) override {
        ++readCount[index(channel)];
        if (failAll) {
            return {};
        }
        return values[index(channel)];
    }

    static std::size_t index(AdcChannel channel) {
        return static_cast<std::size_t>(channel);
    }
};

AdcReadResult okMv(std::int16_t mv) {
    AdcReadResult result{};
    result.ok = true;
    result.millivolts = mv;
    return result;
}

SystemConfig enabledSensorConfig() {
    SystemConfig config = makeDefaultConfig();
    config.temperatureEnabled = true;
    config.temperatureKind = TemperatureKind::Ntc50kB3950;
    config.tdsEnabled = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.tdsCalibrated = true;
    config.tdsCalibrationMode = TdsCalibrationMode::TwoPoint;
    config.tdsCalibrationRevision = 4;
    return config;
}

void advanceSample(WaterSensorManager& manager, std::uint32_t& nowMs) {
    nowMs += 1000;
    manager.tick(nowMs);
}

}  // namespace

void test_manager_samples_a0_input_voltage_a1_temp_a2_tds() {
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    WaterSensorManager manager(adc);
    manager.configure(enabledSensorConfig());

    manager.begin();
    std::uint32_t nowMs = 0;
    advanceSample(manager, nowMs);

    const WaterSensorSnapshot snapshot = manager.snapshot();
    TEST_ASSERT_TRUE(snapshot.inputVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(12001, snapshot.inputVoltageMv.value);
    TEST_ASSERT_TRUE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_INT_WITHIN(50, 2500, snapshot.temperatureCentiC.value);
    TEST_ASSERT_TRUE(snapshot.tdsPpm.valid);
    TEST_ASSERT_EQUAL_INT32(10, snapshot.tdsPpm.value);
    TEST_ASSERT_TRUE(snapshot.tdsVoltageMv.valid);
    TEST_ASSERT_EQUAL_INT32(24, snapshot.tdsVoltageMv.value);
    TEST_ASSERT_EQUAL_size_t(1, adc.readCount[0]);
    TEST_ASSERT_EQUAL_size_t(1, adc.readCount[1]);
    TEST_ASSERT_EQUAL_size_t(1, adc.readCount[2]);
}

void test_manager_can_skip_input_voltage_when_only_water_sensors_are_wired() {
    FakeAdcReader adc;
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    WaterSensorManager manager(adc, false);
    manager.configure(enabledSensorConfig());

    manager.begin();
    std::uint32_t nowMs = 0;
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);

    const WaterSensorSnapshot snapshot = manager.snapshot();
    TEST_ASSERT_FALSE(snapshot.inputVoltageMv.valid);
    TEST_ASSERT_TRUE(snapshot.temperatureCentiC.valid);
    TEST_ASSERT_TRUE(snapshot.tdsPpm.valid);
    TEST_ASSERT_TRUE((snapshot.flags & kWaterSensorFlagAds1115Offline) == 0);
    TEST_ASSERT_EQUAL_size_t(0, adc.readCount[0]);
    TEST_ASSERT_EQUAL_size_t(3, adc.readCount[1]);
    TEST_ASSERT_EQUAL_size_t(3, adc.readCount[2]);
}

void test_manager_marks_ads_offline_after_three_failures() {
    FakeAdcReader adc;
    adc.failAll = true;
    WaterSensorManager manager(adc);
    manager.configure(enabledSensorConfig());
    manager.begin();
    std::uint32_t nowMs = 0;

    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);

    TEST_ASSERT_TRUE((manager.snapshot().flags & kWaterSensorFlagAds1115Offline) != 0);
}

void test_manager_recovers_after_three_successes() {
    FakeAdcReader adc;
    adc.failAll = true;
    WaterSensorManager manager(adc);
    manager.configure(enabledSensorConfig());
    manager.begin();
    std::uint32_t nowMs = 0;
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);
    TEST_ASSERT_TRUE((manager.snapshot().flags & kWaterSensorFlagAds1115Offline) != 0);

    adc.failAll = false;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);
    advanceSample(manager, nowMs);

    TEST_ASSERT_TRUE((manager.snapshot().flags & kWaterSensorFlagAds1115Offline) == 0);
}

void test_tds_range_switches_up_at_85_percent() {
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(230);
    WaterSensorManager manager(adc);
    manager.configure(enabledSensorConfig());
    manager.begin();

    std::uint32_t nowMs = 0;
    advanceSample(manager, nowMs);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(adc.ranges[2]));
    TEST_ASSERT_FALSE(manager.snapshot().tdsPpm.valid);
}

void test_tds_range_switches_down_after_eight_low_windows() {
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(230);
    WaterSensorManager manager(adc);
    manager.configure(enabledSensorConfig());
    manager.begin();
    std::uint32_t nowMs = 0;
    advanceSample(manager, nowMs);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P512),
                            static_cast<std::uint8_t>(adc.ranges[2]));

    adc.values[2] = okMv(100);
    for (std::uint8_t i = 0; i < 9; ++i) {
        advanceSample(manager, nowMs);
    }

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(AdcRange::P256),
                            static_cast<std::uint8_t>(adc.ranges[2]));
}

void test_run_summary_aggregates_valid_samples_only() {
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(24);
    WaterSensorManager manager(adc);
    manager.configure(enabledSensorConfig());
    manager.begin();
    manager.beginRun();
    std::uint32_t nowMs = 0;

    advanceSample(manager, nowMs);
    manager.sampleRun();
    adc.values[1] = okMv(1840);
    adc.values[2] = okMv(48);
    advanceSample(manager, nowMs);
    manager.sampleRun();
    adc.values[2] = {};
    advanceSample(manager, nowMs);
    manager.sampleRun();

    const WaterSensorRunSummary summary = manager.finishRun();
    TEST_ASSERT_EQUAL_UINT16(2, summary.sensorSampleCount);
    TEST_ASSERT_TRUE(summary.temperatureMinCentiC <= summary.temperatureAvgCentiC);
    TEST_ASSERT_TRUE(summary.temperatureAvgCentiC <= summary.temperatureMaxCentiC);
    TEST_ASSERT_TRUE(summary.tdsMinPpm <= summary.tdsAvgPpm);
    TEST_ASSERT_TRUE(summary.tdsAvgPpm <= summary.tdsMaxPpm);
    TEST_ASSERT_EQUAL_UINT16(4, summary.tdsCalibrationRevisionAtRun);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            summary.tdsCalibrationModeAtRun);
}

void test_calibration_uses_25c_fallback_without_failing() {
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = {};
    adc.values[2] = okMv(24);
    SystemConfig config = makeDefaultConfig();
    config.tdsEnabled = true;
    config.tdsKind = TdsKind::AnalogTdsAo;
    config.temperatureEnabled = false;
    WaterSensorManager manager(adc);
    manager.configure(config);
    manager.begin();

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(10, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(manager, nowMs);
    }

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.samplingActive);
    TEST_ASSERT_TRUE(session.readyToSave);
    TEST_ASSERT_TRUE(session.tempFallback25C);
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_TRUE(manager.applyReadyTdsCalibration(config, 1720000021UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT16(1, config.tdsCalibrationRevision);
    TEST_ASSERT_EQUAL_INT16(2500, config.tdsCalibrationTemperatureCentiC);
}

void test_two_point_calibration_saves_low_then_high_without_flash_progress_dependency() {
    FakeAdcReader adc;
    adc.values[0] = okMv(1091);
    adc.values[1] = okMv(1650);
    adc.values[2] = okMv(12);
    SystemConfig config = enabledSensorConfig();
    config.tdsCalibrated = false;
    config.tdsCalibrationMode = TdsCalibrationMode::None;
    config.tdsCalibrationRevision = 0;
    WaterSensorManager manager(adc);
    manager.configure(config);
    manager.begin();

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(0, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.calibrationSnapshot().readyToSave);
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_FALSE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsCalibrationRevision);

    adc.values[2] = okMv(380);
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 20; ++i) {
        advanceSample(manager, nowMs);
    }

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.readyToSave);
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000050UL));
    TEST_ASSERT_TRUE(manager.applyReadyTdsCalibration(config, 1720000051UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(1, config.tdsCalibrationRevision);
    TEST_ASSERT_EQUAL_UINT16(0, config.tdsLowReferencePpm);
    TEST_ASSERT_TRUE(config.tdsLowRawPpm < config.tdsHighRawPpm);
    TEST_ASSERT_EQUAL_UINT16(160, config.tdsHighReferencePpm);
}

void test_tds_calibration_point_session_generates_after_one_point() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(380);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.sessionActive);
    TEST_ASSERT_EQUAL_UINT8(1, session.pointCount);
    TEST_ASSERT_TRUE(session.candidateReady);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::SinglePoint),
                            static_cast<std::uint8_t>(session.candidateMode));
}

void test_tds_calibration_session_rejects_duplicate_start_without_clearing_points() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_FALSE(manager.startTdsCalibrationSession(1720000030UL));

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.sessionActive);
    TEST_ASSERT_EQUAL_UINT8(1, session.pointCount);
    TEST_ASSERT_TRUE(session.candidateReady);
    TEST_ASSERT_EQUAL_UINT16(160, session.points[0].referencePpm);
}

void test_tds_calibration_point_session_multi_point_fit_and_apply() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(20, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(160);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(manager.applyReadyTdsCalibration(config, 1720000050UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(5, config.tdsCalibrationRevision);
}

void test_tds_calibration_apply_saves_low_and_high_by_reference_not_entry_order() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(20, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(160);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(manager.applyReadyTdsCalibration(config, 1720000050UL));
    TEST_ASSERT_EQUAL_UINT16(20, config.tdsLowReferencePpm);
    TEST_ASSERT_EQUAL_UINT16(160, config.tdsHighReferencePpm);
    TEST_ASSERT_TRUE(config.tdsLowRawPpm < config.tdsHighRawPpm);
}

void test_tds_calibration_point_removal_recomputes_candidate() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_TRUE(manager.removeTdsCalibrationPoint(0, 1720000030UL));

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_EQUAL_UINT8(0, session.pointCount);
    TEST_ASSERT_FALSE(session.candidateReady);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_manager_samples_a0_input_voltage_a1_temp_a2_tds);
    RUN_TEST(test_manager_can_skip_input_voltage_when_only_water_sensors_are_wired);
    RUN_TEST(test_manager_marks_ads_offline_after_three_failures);
    RUN_TEST(test_manager_recovers_after_three_successes);
    RUN_TEST(test_tds_range_switches_up_at_85_percent);
    RUN_TEST(test_tds_range_switches_down_after_eight_low_windows);
    RUN_TEST(test_run_summary_aggregates_valid_samples_only);
    RUN_TEST(test_calibration_uses_25c_fallback_without_failing);
    RUN_TEST(test_two_point_calibration_saves_low_then_high_without_flash_progress_dependency);
    RUN_TEST(test_tds_calibration_point_session_generates_after_one_point);
    RUN_TEST(test_tds_calibration_session_rejects_duplicate_start_without_clearing_points);
    RUN_TEST(test_tds_calibration_point_session_multi_point_fit_and_apply);
    RUN_TEST(test_tds_calibration_apply_saves_low_and_high_by_reference_not_entry_order);
    RUN_TEST(test_tds_calibration_point_removal_recomputes_candidate);
    return UNITY_END();
}
