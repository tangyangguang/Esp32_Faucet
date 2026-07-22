#include <unity.h>

#include "app/ColorDisplayPresenter.h"

#include <cstring>

using namespace faucet;

namespace {

AppSnapshot makeSnapshot(WaterState state, WaterMode mode, std::uint32_t target, std::uint32_t volumeMl = 0) {
    AppSnapshot snapshot{
        WaterSnapshot{
            state,
            0,
            0,
            state == WaterState::Running,
            volumeMl,
            0,
            WaterResult::Completed,
            mode,
            target,
        },
        ValveOutput{ValveState::Closed, false, 0},
        StatisticsRecord{3200, 0, 0, 9000, 20260506, 202619, 202605},
    };
    snapshot.pulsePerLiter = 248;
    snapshot.meteringParams = MeteringParameters{8, 130, 248, 5000, 1950};
    snapshot.targetEstimatedDurationSec = 46;
    snapshot.selectedPresetEstimatedDurationSec = 46;
    snapshot.targetEstimatedVolumeMl = 11700;
    snapshot.selectedPresetEstimatedVolumeMl = 1950;
    snapshot.adjustmentStepMl = 100;
    snapshot.timeAdjustmentStepSec = 10;
    snapshot.temperatureSensorEnabled = true;
    snapshot.tdsSensorEnabled = true;
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2460};
    snapshot.sensors.tdsPpm = SensorValue{true, 36};
    snapshot.sensors.tdsCalibrated = true;
    return snapshot;
}

WaterRecord makeLastRecord(std::uint32_t volumeMl,
                           std::uint32_t target,
                           std::uint16_t durationSec,
                           WaterResult result) {
    WaterRecord record{};
    record.volumeMl = volumeMl;
    record.targetValue = target;
    record.durationSec = durationSec;
    record.result = result;
    record.mode = WaterMode::Volume;
    record.temperatureCentiC = 2470;
    record.tdsPpm = 386;
    record.sensorSampleCount = 3;
    return record;
}

void assertTextEquals(const char* expected, const char* actual) {
    TEST_ASSERT_EQUAL_STRING(expected, actual);
}

}  // namespace

void test_color_display_covers_idle_confirm_running_pause_result_alert_calibration_offline_and_sleep_pages() {
    ColorDisplayPresenter presenter(60);
    presenter.wake(1000);

    ColorDisplayFrame frame = presenter.render(makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500), 1200, true);
    TEST_ASSERT_TRUE(frame.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::StandbyVolume),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("待机", frame.state);
    assertTextEquals("P1", frame.tag);
    assertTextEquals("1.50", frame.mainValue);
    assertTextEquals("L", frame.mainUnit);

    AppSnapshot timeIdle = makeSnapshot(WaterState::Idle, WaterMode::Time, 60);
    frame = presenter.render(timeIdle, 1300, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::StandbyTime),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("60", frame.mainValue);
    assertTextEquals("秒", frame.mainUnit);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.subtitle, "预计"));

    frame = presenter.render(makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500), 1400, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::StandbyVolume),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("离线", frame.tag);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.subtitle, "本地可用"));

    frame = presenter.render(makeSnapshot(WaterState::Confirm, WaterMode::Volume, 1500), 1500, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::ConfirmVolume),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("确认出水", frame.state);
    assertTextEquals("本次目标", frame.title);
    assertTextEquals("1.50", frame.mainValue);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.subtitle, "约46秒"));
    assertTextEquals("加减", frame.hints[1]);

    AppSnapshot confirmTime = makeSnapshot(WaterState::Confirm, WaterMode::Time, 360);
    frame = presenter.render(confirmTime, 1600, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::ConfirmTime),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("本次时长", frame.title);
    assertTextEquals("06:00", frame.mainValue);
    assertTextEquals("加减", frame.hints[1]);

    AppSnapshot runningVolume = makeSnapshot(WaterState::Running, WaterMode::Volume, 1500, 920);
    runningVolume.water.elapsedSec = 222;
    runningVolume.displayFlowMlPerMin = 12800;
    runningVolume.instantFlowMlPerMin = 99999;
    runningVolume.sensors.temperatureCentiC = SensorValue{true, 2470};
    runningVolume.sensors.tdsPpm = SensorValue{true, 386};
    frame = presenter.render(runningVolume, 1700, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::RunningVolume),
                            static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_UINT16(613, frame.progressPermille);
    assertTextEquals("已出水", frame.title);
    assertTextEquals("0.92", frame.mainValue);
    assertTextEquals("实时流速", frame.metrics[1].label);
    assertTextEquals("12.80", frame.metrics[1].value);
    assertTextEquals("L/min", frame.metrics[1].unit);
    assertTextEquals("TDS", frame.sensors[0].label);
    assertTextEquals("386", frame.sensors[0].value);
    assertTextEquals("水温", frame.sensors[1].label);
    assertTextEquals("24.7", frame.sensors[1].value);

    AppSnapshot runningTime = makeSnapshot(WaterState::Running, WaterMode::Time, 360, 2360);
    runningTime.water.elapsedSec = 138;
    runningTime.displayFlowMlPerMin = 12800;
    frame = presenter.render(runningTime, 1800, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::RunningTime),
                            static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_UINT16(383, frame.progressPermille);
    assertTextEquals("剩余时间", frame.title);
    assertTextEquals("03:42", frame.mainValue);

    AppSnapshot pausedVolume = runningVolume;
    pausedVolume.water.state = WaterState::Paused;
    pausedVolume.valve = ValveOutput{ValveState::Closed, false, 0};
    frame = presenter.render(pausedVolume, 1900, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::PausedVolume),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("阀关", frame.tag);
    assertTextEquals("已暂停", frame.title);
    assertTextEquals("阀门已关闭", frame.status);
    assertTextEquals("剩余", frame.metrics[1].label);
    TEST_ASSERT_EQUAL_UINT8(2, frame.hintCount);
    assertTextEquals("确认 继续", frame.hints[0]);
    assertTextEquals("取消 结束", frame.hints[1]);

    AppSnapshot pausedTime = runningTime;
    pausedTime.water.state = WaterState::Paused;
    frame = presenter.render(pausedTime, 2000, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::PausedTime),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("剩余时间", frame.metrics[0].label);

    AppSnapshot completed = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500, 1500);
    completed.localMode = LocalUiMode::Result;
    completed.water.elapsedSec = 46;
    completed.water.lastResult = WaterResult::Completed;
    completed.runAverageFlowMlPerMin = 1960;
    completed.sensors.temperatureCentiC = SensorValue{true, 2470};
    completed.sensors.tdsPpm = SensorValue{true, 386};
    frame = presenter.render(completed, 2100, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::ResultCompleted),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("已完成", frame.state);
    assertTextEquals("本次出水", frame.title);
    assertTextEquals("1.50", frame.mainValue);
    TEST_ASSERT_EQUAL_UINT8(1, frame.hintCount);
    assertTextEquals("取消 返回", frame.hints[0]);

    completed.water.lastResult = WaterResult::StoppedByUser;
    completed.water.volumeMl = 920;
    frame = presenter.render(completed, 2200, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::ResultStopped),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("已停止", frame.state);
    assertTextEquals("目标", frame.metrics[0].label);

    AppSnapshot error = makeSnapshot(WaterState::Error, WaterMode::Volume, 1500, 120);
    error.water.lastResult = WaterResult::FlowError;
    frame = presenter.render(error, 2300, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::Alert),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("已关阀", frame.title);
    assertTextEquals("流量异常", frame.status);
    assertTextEquals("阀关", frame.tag);
    TEST_ASSERT_EQUAL_UINT8(1, frame.hintCount);
    assertTextEquals("取消 返回", frame.hints[0]);

    AppSnapshot calReady = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500, 0);
    calReady.localMode = LocalUiMode::Calibration;
    calReady.calibrationStatus = CalibrationSessionStatus::WaitingLocalRun;
    calReady.calibrationValidSampleCount = 2;
    frame = presenter.render(calReady, 2400, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::CalibrationReady),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("等待本地出水", frame.title);
    assertTextEquals("TDS", frame.metrics[2].label);
    TEST_ASSERT_EQUAL_UINT8(2, frame.hintCount);
    assertTextEquals("确认 开始", frame.hints[0]);
    assertTextEquals("取消 退出", frame.hints[1]);

    AppSnapshot calRunning = makeSnapshot(WaterState::Running, WaterMode::Volume, 1500, 920);
    calRunning.localMode = LocalUiMode::Calibration;
    calRunning.calibrationStatus = CalibrationSessionStatus::Running;
    calRunning.water.targetValue = 0;
    calRunning.water.elapsedSec = 222;
    calRunning.displayFlowMlPerMin = 12800;
    frame = presenter.render(calRunning, 2450, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::CalibrationReady),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("校准出水中", frame.title);
    assertTextEquals("0.92", frame.mainValue);
    assertTextEquals("已用", frame.metrics[0].label);
    assertTextEquals("实时流速", frame.metrics[2].label);
    TEST_ASSERT_EQUAL_UINT8(1, frame.hintCount);
    assertTextEquals("取消 停止", frame.hints[0]);

    AppSnapshot calAwaiting = calRunning;
    calAwaiting.water.state = WaterState::Idle;
    calAwaiting.calibrationStatus = CalibrationSessionStatus::AwaitingActual;
    frame = presenter.render(calAwaiting, 2500, true);
    assertTextEquals("等待网页录入", frame.title);
    TEST_ASSERT_EQUAL_UINT8(1, frame.hintCount);
    assertTextEquals("取消 放弃", frame.hints[0]);

    ColorDisplayPresenter sleepy(1);
    sleepy.wake(1000);
    frame = sleepy.render(makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500), 2100, true);
    TEST_ASSERT_FALSE(frame.on);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::Sleep),
                            static_cast<std::uint8_t>(frame.page));
}

void test_color_display_uses_active_preset_and_record_summary_values() {
    ColorDisplayPresenter presenter(60);
    presenter.wake(0);

    AppSnapshot idle = makeSnapshot(WaterState::Idle, WaterMode::Volume, 7500);
    idle.water.selectedPreset = 2;
    ColorDisplayFrame frame = presenter.render(idle, 100, true);
    assertTextEquals("P3", frame.tag);
    assertTextEquals("预设3 · 定量出水", frame.title);

    AppSnapshot confirm = makeSnapshot(WaterState::Confirm, WaterMode::Time, 360);
    confirm.water.activePreset = 4;
    frame = presenter.render(confirm, 200, true);
    TEST_ASSERT_NOT_NULL(std::strstr(frame.subtitle, "预设5"));

    AppSnapshot running = makeSnapshot(WaterState::Running, WaterMode::Volume, 1500, 920);
    running.water.activePreset = 1;
    running.water.elapsedSec = 222;
    frame = presenter.render(running, 300, true);
    assertTextEquals("P2 · 03:42", frame.tag);

    AppSnapshot paused = running;
    paused.water.state = WaterState::Paused;
    paused.displayFlowMlPerMin = 0;
    paused.meteringParams.stableFlowMlPerMin = 1950;
    frame = presenter.render(paused, 400, true);
    assertTextEquals("还需约", frame.metrics[3].label);
    assertTextEquals("00:18", frame.metrics[3].value);

    AppSnapshot result = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500, 1500);
    result.localMode = LocalUiMode::Result;
    result.water.lastResult = WaterResult::Completed;
    result.water.elapsedSec = 46;
    result.runAverageFlowMlPerMin = 1960;
    result.lastResultRecordAvailable = true;
    result.lastResultRecord = makeLastRecord(1500, 1500, 46, WaterResult::Completed);
    result.sensors.tdsPpm = SensorValue{true, 12};
    result.sensors.temperatureCentiC = SensorValue{true, 3000};
    frame = presenter.render(result, 500, true);
    assertTextEquals("TDS", frame.metrics[2].label);
    assertTextEquals("386", frame.metrics[2].value);
    assertTextEquals("水温", frame.metrics[3].label);
    assertTextEquals("24.7", frame.metrics[3].value);
}

void test_color_display_confirm_page_hides_unavailable_estimates_and_uses_short_hints() {
    ColorDisplayPresenter presenter(60);
    presenter.wake(0);

    AppSnapshot confirmVolume = makeSnapshot(WaterState::Confirm, WaterMode::Volume, 7500);
    confirmVolume.water.activePreset = 1;
    confirmVolume.targetEstimatedDurationSec = 0;
    confirmVolume.targetEstimateReason = "计量参数未就绪";
    confirmVolume.meteringParams = MeteringParameters{0, 0, 0, 0, 0};
    ColorDisplayFrame frame = presenter.render(confirmVolume, 100, true);
    assertTextEquals("预设2", frame.subtitle);
    TEST_ASSERT_NULL(std::strstr(frame.subtitle, "0 秒"));
    assertTextEquals("确认", frame.hints[0]);
    assertTextEquals("加减", frame.hints[1]);
    assertTextEquals("取消", frame.hints[2]);

    AppSnapshot confirmTime = makeSnapshot(WaterState::Confirm, WaterMode::Time, 360);
    confirmTime.water.activePreset = 2;
    confirmTime.targetEstimatedVolumeMl = 0;
    confirmTime.targetEstimatedPulseCount = 0;
    frame = presenter.render(confirmTime, 200, true);
    assertTextEquals("预设3", frame.subtitle);
    TEST_ASSERT_NULL(std::strstr(frame.subtitle, "0.00L"));
}

void test_color_display_confirm_volume_estimates_duration_from_metering_params_when_snapshot_omits_it() {
    ColorDisplayPresenter presenter(60);
    presenter.wake(0);

    AppSnapshot confirmVolume = makeSnapshot(WaterState::Confirm, WaterMode::Volume, 7500);
    confirmVolume.water.activePreset = 1;
    confirmVolume.targetEstimatedDurationSec = 0;
    ColorDisplayFrame frame = presenter.render(confirmVolume, 100, true);

    TEST_ASSERT_NOT_NULL(std::strstr(frame.subtitle, "预设2"));
    TEST_ASSERT_NOT_NULL(std::strstr(frame.subtitle, "约03:52"));
    TEST_ASSERT_NULL(std::strstr(frame.subtitle, "0秒"));
}

void test_color_display_trends_only_use_real_running_sensor_samples() {
    ColorDisplayPresenter presenter(60);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Running, WaterMode::Volume, 1500, 100);
    snapshot.water.elapsedSec = 1;
    snapshot.sensors.tdsPpm = SensorValue{true, 380};
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2460};
    presenter.render(snapshot, 1000, true);

    snapshot.water.volumeMl = 200;
    snapshot.sensors.tdsPpm = SensorValue{true, 386};
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2470};
    presenter.render(snapshot, 2000, true);

    snapshot.water.volumeMl = 300;
    snapshot.sensors.tdsPpm = SensorValue{true, 390};
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2480};
    ColorDisplayFrame frame = presenter.render(snapshot, 3000, true);

    TEST_ASSERT_EQUAL_UINT8(3, frame.sensors[0].sampleCount);
    TEST_ASSERT_EQUAL_UINT16(380, frame.sensors[0].samples[0]);
    TEST_ASSERT_EQUAL_UINT16(386, frame.sensors[0].samples[1]);
    TEST_ASSERT_EQUAL_UINT16(390, frame.sensors[0].samples[2]);
    TEST_ASSERT_EQUAL_UINT8(3, frame.sensors[1].sampleCount);
    TEST_ASSERT_EQUAL_UINT16(246, frame.sensors[1].samples[0]);
    TEST_ASSERT_EQUAL_UINT16(247, frame.sensors[1].samples[1]);
    TEST_ASSERT_EQUAL_UINT16(248, frame.sensors[1].samples[2]);

    snapshot.water.state = WaterState::Idle;
    frame = presenter.render(snapshot, 4000, true);
    TEST_ASSERT_EQUAL_UINT8(0, frame.sensors[0].sampleCount);
    TEST_ASSERT_EQUAL_UINT8(0, frame.sensors[1].sampleCount);
}

void test_color_display_sensor_trends_are_independent_and_keep_local_tds_label_short() {
    ColorDisplayPresenter presenter(60);
    presenter.wake(0);
    AppSnapshot snapshot = makeSnapshot(WaterState::Running, WaterMode::Volume, 1500, 100);
    snapshot.sensors.tdsCalibrated = false;
    snapshot.sensors.tdsPpm = {};

    presenter.render(snapshot, 1000, true);
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2470};
    presenter.render(snapshot, 2000, true);
    snapshot.sensors.temperatureCentiC = SensorValue{true, 2480};
    ColorDisplayFrame frame = presenter.render(snapshot, 3000, true);
    TEST_ASSERT_EQUAL_UINT8(0, frame.sensors[0].sampleCount);
    TEST_ASSERT_EQUAL_UINT8(3, frame.sensors[1].sampleCount);

    snapshot.sensors.tdsPpm = SensorValue{true, 12};
    snapshot.sensors.temperatureCentiC = {};
    frame = presenter.render(snapshot, 4000, true);
    assertTextEquals("TDS", frame.sensors[0].label);
    TEST_ASSERT_EQUAL_UINT8(1, frame.sensors[0].sampleCount);
    TEST_ASSERT_EQUAL_UINT8(3, frame.sensors[1].sampleCount);

    AppSnapshot idle = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500);
    idle.sensors.tdsCalibrated = false;
    idle.sensors.tdsPpm = SensorValue{true, 12};
    frame = presenter.render(idle, 5000, true);
    assertTextEquals("TDS", frame.metrics[2].label);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_color_display_covers_idle_confirm_running_pause_result_alert_calibration_offline_and_sleep_pages);
    RUN_TEST(test_color_display_uses_active_preset_and_record_summary_values);
    RUN_TEST(test_color_display_confirm_page_hides_unavailable_estimates_and_uses_short_hints);
    RUN_TEST(test_color_display_confirm_volume_estimates_duration_from_metering_params_when_snapshot_omits_it);
    RUN_TEST(test_color_display_trends_only_use_real_running_sensor_samples);
    RUN_TEST(test_color_display_sensor_trends_are_independent_and_keep_local_tds_label_short);
    return UNITY_END();
}
