#include <unity.h>

#include "app/AppController.h"

#include <vector>

using namespace faucet;

namespace {

class MemoryLogWriter : public AppLogWriter {
public:
    bool ok = true;
    std::vector<WaterLogRecord> records;

    bool append(const WaterLogRecord& record) override {
        if (!ok) {
            return false;
        }
        records.push_back(record);
        return true;
    }
};

AppTickInput input(ButtonLevels levels, std::uint32_t nowMs, std::uint32_t nowUs, std::uint32_t nowSeconds) {
    return AppTickInput{
        levels,
        nowMs,
        nowUs,
        nowSeconds,
        {20260506, 202619, 202605},
    };
}

void pressAndReleaseOk(AppController& app, std::uint32_t baseMs) {
    app.tick(input({false, true, false}, baseMs, baseMs * 1000UL, 1000 + baseMs / 1000));
    app.tick(input({false, true, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({false, false, false}, baseMs + 60, (baseMs + 60) * 1000UL, 1000));
    app.tick(input({false, false, false}, baseMs + 60 + kButtonDebounceMs, (baseMs + 60 + kButtonDebounceMs) * 1000UL, 1000));
}

void longPressOk(AppController& app, std::uint32_t baseMs) {
    app.tick(input({false, true, false}, baseMs, baseMs * 1000UL, 1000));
    app.tick(input({false, true, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({false, false, false}, baseMs + kButtonDebounceMs + kButtonLongPressMs + 20,
                   (baseMs + kButtonDebounceMs + kButtonLongPressMs + 20) * 1000UL,
                   1001));
    app.tick(input({false, false, false}, baseMs + 2 * kButtonDebounceMs + kButtonLongPressMs + 20,
                   (baseMs + 2 * kButtonDebounceMs + kButtonLongPressMs + 20) * 1000UL,
                   1001));
}

void holdFactoryCombo(AppController& app, std::uint32_t baseMs) {
    app.tick(input({true, true, false}, baseMs, baseMs * 1000UL, 1000));
    app.tick(input({true, true, false}, baseMs + kButtonDebounceMs, (baseMs + kButtonDebounceMs) * 1000UL, 1000));
    app.tick(input({true, true, false}, baseMs + kFactoryResetComboMs + kButtonDebounceMs + 10,
                   (baseMs + kFactoryResetComboMs + kButtonDebounceMs + 10) * 1000UL,
                   1005));
}

}  // namespace

void test_app_controller_starts_after_double_ok_and_opens_valve() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryLogWriter logs;
    AppController app(config, statistics, filters, logs);

    app.resetInputs({false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Confirm),
                            static_cast<std::uint8_t>(app.snapshot().water.state));

    pressAndReleaseOk(app, 300);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Running),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_UINT8(100, app.snapshot().valve.dutyPercent);
}

void test_app_controller_completion_writes_log_statistics_and_filters() {
    SystemConfig config = makeDefaultConfig();
    config.pulsePerMl = 1.0f;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryLogWriter logs;
    AppController app(config, statistics, filters, logs);

    app.resetInputs({false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);

    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_size_t(1, logs.records.size());
    TEST_ASSERT_EQUAL_UINT32(1500, logs.records[0].volumeMl);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::Completed),
                            static_cast<std::uint8_t>(logs.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
    TEST_ASSERT_TRUE(app.consumePersistenceDirty());
    TEST_ASSERT_FALSE(app.consumePersistenceDirty());
}

void test_app_controller_stop_down_closes_valve_and_records_user_stop() {
    SystemConfig config = makeDefaultConfig();
    config.pulsePerMl = 1.0f;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryLogWriter logs;
    AppController app(config, statistics, filters, logs);

    app.resetInputs({false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 300; ++i) {
        app.onFlowPulse(1000000UL + i * 3000UL);
    }
    app.tick(input({false, false, false}, 1500, 1500000, 1714502400));

    app.tick(input({true, false, false}, 1600, 1600000, 1714502401));
    app.tick(input({true, false, false}, 1600 + kButtonDebounceMs, (1600 + kButtonDebounceMs) * 1000UL, 1714502401));

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterState::Idle),
                            static_cast<std::uint8_t>(app.snapshot().water.state));
    TEST_ASSERT_FALSE(app.snapshot().valve.enabled);
    TEST_ASSERT_EQUAL_size_t(1, logs.records.size());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(WaterResult::StoppedByUser),
                            static_cast<std::uint8_t>(logs.records[0].result));
    TEST_ASSERT_EQUAL_UINT32(300, logs.records[0].volumeMl);
}

void test_app_controller_reports_log_write_failure_without_losing_statistics() {
    SystemConfig config = makeDefaultConfig();
    config.pulsePerMl = 1.0f;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryLogWriter logs;
    logs.ok = false;
    AppController app(config, statistics, filters, logs);

    app.resetInputs({false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    for (std::uint32_t i = 0; i < 1500; ++i) {
        app.onFlowPulse(1000000UL + i * 2000UL);
    }
    app.tick(input({false, false, false}, 5000, 5000000, 1714502400));

    TEST_ASSERT_FALSE(app.lastLogWriteOk());
    TEST_ASSERT_EQUAL_UINT32(1500, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, filters.record(0).usedMl);
}

void test_app_controller_local_calibration_saves_without_water_log_or_stats() {
    SystemConfig config = makeDefaultConfig();
    config.pulsePerMl = 0.45f;
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryLogWriter logs;
    AppController app(config, statistics, filters, logs);

    app.resetInputs({false, false, false}, 0);
    longPressOk(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::CalibrationSelect),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    pressAndReleaseOk(app, 1300);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::CalibrationConfirm),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    pressAndReleaseOk(app, 1500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::CalibrationSampling),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_TRUE(app.snapshot().valve.enabled);
    for (std::uint32_t i = 0; i < 750; ++i) {
        app.onFlowPulse(2000000UL + i * 2000UL);
    }
    pressAndReleaseOk(app, 3000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::CalibrationSelect),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    pressAndReleaseOk(app, 3300);
    pressAndReleaseOk(app, 3500);
    for (std::uint32_t i = 0; i < 750; ++i) {
        app.onFlowPulse(5000000UL + i * 2000UL);
    }
    pressAndReleaseOk(app, 6200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::CalibrationReview),
                            static_cast<std::uint8_t>(app.snapshot().localMode));

    pressAndReleaseOk(app, 6500);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, app.config().pulsePerMl);
    TEST_ASSERT_TRUE(app.consumeConfigDirty());
    TEST_ASSERT_EQUAL_size_t(0, logs.records.size());
    TEST_ASSERT_EQUAL_UINT32(0, statistics.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(0, filters.record(0).usedMl);
}

void test_app_controller_factory_reset_requires_second_ok() {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryLogWriter logs;
    AppController app(config, statistics, filters, logs);

    app.resetInputs({false, false, false}, 0);
    holdFactoryCombo(app, 100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::FactoryResetConfirm),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
    TEST_ASSERT_FALSE(app.consumeFactoryResetRequest());

    app.tick(input({false, false, false}, 5300, 5300000, 1005));
    app.tick(input({false, false, false}, 5330, 5330000, 1005));
    pressAndReleaseOk(app, 5600);
    TEST_ASSERT_TRUE(app.consumeFactoryResetRequest());
    TEST_ASSERT_FALSE(app.consumeFactoryResetRequest());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_app_controller_starts_after_double_ok_and_opens_valve);
    RUN_TEST(test_app_controller_completion_writes_log_statistics_and_filters);
    RUN_TEST(test_app_controller_stop_down_closes_valve_and_records_user_stop);
    RUN_TEST(test_app_controller_reports_log_write_failure_without_losing_statistics);
    RUN_TEST(test_app_controller_local_calibration_saves_without_water_log_or_stats);
    RUN_TEST(test_app_controller_factory_reset_requires_second_ok);
    return UNITY_END();
}
