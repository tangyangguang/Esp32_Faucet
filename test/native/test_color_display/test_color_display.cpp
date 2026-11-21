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

void assertTextEquals(const char* expected, const char* actual) {
    TEST_ASSERT_EQUAL_STRING(expected, actual);
}

ColorDisplayFrame showVolumeEstimate(ColorDisplayPresenter& presenter, AppSnapshot& snapshot,
                                    std::uint32_t elapsedMs, std::uint32_t volumeMl, std::uint32_t flowMlPerMin) {
    snapshot.water.elapsedMs = elapsedMs;
    snapshot.water.elapsedSec = elapsedMs / 1000U;
    snapshot.water.volumeMl = volumeMl;
    snapshot.displayFlowMlPerMin = flowMlPerMin;
    presenter.observe(snapshot);
    return presenter.render(snapshot, elapsedMs, true);
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
    runningVolume.water.elapsedSec = 220;
    runningVolume.water.elapsedMs = 220000;
    runningVolume.displayFlowMlPerMin = 12800;
    runningVolume.instantFlowMlPerMin = 99999;
    runningVolume.sensors.temperatureCentiC = SensorValue{true, 2470};
    runningVolume.sensors.tdsPpm = SensorValue{true, 386};
    presenter.observe(runningVolume);
    runningVolume.water.elapsedSec = 222;
    runningVolume.water.elapsedMs = 222000;
    runningVolume.sensors.sampleSequence = 1;
    presenter.observe(runningVolume);
    frame = presenter.render(runningVolume, 1700, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::RunningVolume),
                            static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_UINT16(613, frame.progressPermille);
    assertTextEquals("已出水", frame.title);
    assertTextEquals("0.92", frame.mainValue);
    assertTextEquals("定量 1.50 L", frame.tag);
    assertTextEquals("00:03", frame.timer);
    assertTextEquals("总计约 03:45", frame.subtitle);
    assertTextEquals("水温", frame.sensors[0].label);
    assertTextEquals("24.7", frame.sensors[0].value);
    assertTextEquals("TDS", frame.sensors[1].label);
    assertTextEquals("386", frame.sensors[1].value);

    AppSnapshot runningTime = makeSnapshot(WaterState::Running, WaterMode::Time, 360, 2360);
    runningTime.water.elapsedSec = 138;
    runningTime.displayFlowMlPerMin = 12800;
    frame = presenter.render(runningTime, 1800, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::RunningTime),
                            static_cast<std::uint8_t>(frame.page));
    TEST_ASSERT_EQUAL_UINT16(383, frame.progressPermille);
    assertTextEquals("已出水", frame.title);
    assertTextEquals("2.36", frame.mainValue);
    assertTextEquals("定时 06:00", frame.tag);
    assertTextEquals("03:42", frame.timer);
    assertTextEquals("", frame.subtitle);

    AppSnapshot pausedVolume = runningVolume;
    pausedVolume.water.state = WaterState::Paused;
    pausedVolume.valve = ValveOutput{ValveState::Closed, false, 0};
    frame = presenter.render(pausedVolume, 1900, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::PausedVolume),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("定量 1.50 L", frame.tag);
    assertTextEquals("已暂停", frame.state);
    assertTextEquals("阀关", frame.status);
    assertTextEquals("0.92", frame.mainValue);
    TEST_ASSERT_EQUAL_UINT8(2, frame.hintCount);
    assertTextEquals("确认 继续", frame.hints[0]);
    assertTextEquals("取消 停止", frame.hints[1]);

    AppSnapshot pausedTime = runningTime;
    pausedTime.water.state = WaterState::Paused;
    frame = presenter.render(pausedTime, 2000, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::PausedTime),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("定时 06:00", frame.tag);

    AppSnapshot completed = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500, 1500);
    completed.localMode = LocalUiMode::Result;
    completed.lastDisplayResult = {true, WaterMode::Volume, WaterResult::Completed, 1500, 1500, 46, 0};
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

    completed.lastDisplayResult.result = WaterResult::StoppedByUser;
    completed.lastDisplayResult.volumeMl = 920;
    frame = presenter.render(completed, 2200, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::ResultStopped),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("已停止", frame.state);
    assertTextEquals("定量 1.50 L", frame.tag);
    TEST_ASSERT_EQUAL_UINT16(613, frame.progressPermille);

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

void test_color_display_uses_confirmed_target_and_frozen_result() {
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

    AppSnapshot running = makeSnapshot(WaterState::Running, WaterMode::Time, 360, 920);
    running.water.activePreset = 1;
    running.water.elapsedSec = 222;
    frame = presenter.render(running, 300, true);
    assertTextEquals("定时 06:00", frame.tag);

    AppSnapshot volume = running;
    volume.water.mode = WaterMode::Volume;
    volume.water.targetValue = 1500;
    frame = presenter.render(volume, 350, true);
    assertTextEquals("--", frame.timer);
    assertTextEquals("", frame.subtitle);
    volume.displayFlowMlPerMin = 60;
    volume.maxOutTimeSec = 240;
    showVolumeEstimate(presenter, volume, 222000, 920, 60);
    frame = showVolumeEstimate(presenter, volume, 224000, 920, 60);
    assertTextEquals("--", frame.timer);
    assertTextEquals("", frame.subtitle);

    AppSnapshot result = makeSnapshot(WaterState::Idle, WaterMode::Volume, 9000);
    result.localMode = LocalUiMode::Result;
    result.lastDisplayResult = {true, WaterMode::Time, WaterResult::Completed, 1470, 360, 360, 1};
    frame = presenter.render(result, 500, true);
    assertTextEquals("定时 06:00", frame.tag);
    assertTextEquals("1.47", frame.mainValue);
    assertTextEquals("06:00", frame.timer);
    TEST_ASSERT_EQUAL_UINT16(1000, frame.progressPermille);

    result.lastDisplayResult.result = WaterResult::FlowError;
    frame = presenter.render(result, 550, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::ResultAlert),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("流量异常", frame.state);
    assertTextEquals("阀关", frame.status);
    TEST_ASSERT_EQUAL_UINT16(990, frame.progressPermille);
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

void test_color_display_trends_preserve_full_run_with_gaps_and_freeze_at_result() {
    ColorDisplayPresenter presenter(60);
    AppSnapshot running = makeSnapshot(WaterState::Running, WaterMode::Time, 600, 1500);
    presenter.observe(running);
    for (std::uint32_t sec = 0; sec < 600; ++sec) {
        running.water.elapsedSec = sec;
        running.sensors.sampleSequence = sec + 1U;
        running.sensors.temperatureCentiC = {true, sec == 2 ? -140 : sec == 18 ? 3110 : 2450};
        running.sensors.tdsPpm = {sec < 250 || sec > 290, sec == 0 ? 22 : sec == 37 ? 65000 : 47};
        presenter.observe(running);
        // Re-rendering an old sensor sequence must never invent another sample.
        AppSnapshot duplicate = running;
        duplicate.sensors.temperatureCentiC.value = 5000;
        presenter.observe(duplicate);
    }
    running.water.elapsedSec = 600;
    ColorDisplayFrame frame = presenter.render(running, 600000, true);
    assertTextEquals("10:00", frame.trendTime);
    assertTextEquals("-1.4", frame.sensors[0].low);
    assertTextEquals("31.1", frame.sensors[0].high);
    assertTextEquals("24.5", frame.sensors[0].value);
    assertTextEquals("22", frame.sensors[1].low);
    assertTextEquals("65000", frame.sensors[1].high);
    assertTextEquals("47", frame.sensors[1].value);
    TEST_ASSERT_NOT_EQUAL(0, frame.sensors[1].columns[0].bottom);
    TEST_ASSERT_NOT_EQUAL(0, frame.sensors[1].latestY);
    TEST_ASSERT_NOT_EQUAL(0, frame.sensors[1].columns[42].top & kColorDisplayTrendGap);
    TEST_ASSERT_EQUAL_UINT8(0, frame.sensors[0].columns[42].top & kColorDisplayTrendGap);

    AppSnapshot result = makeSnapshot(WaterState::Idle, WaterMode::Volume, 3000);
    result.localMode = LocalUiMode::Result;
    result.lastDisplayResult = {true, WaterMode::Time, WaterResult::Completed, 1500, 600, 600, 0};
    result.sensors.sampleSequence = 601;
    result.sensors.tdsPpm = {true, 99};
    presenter.observe(result);
    const ColorDisplayFrame frozen = presenter.render(result, 601000, true);
    assertTextEquals("10:00", frozen.trendTime);
    assertTextEquals("47", frozen.sensors[1].value);
    TEST_ASSERT_EQUAL_MEMORY(frame.sensors, frozen.sensors, sizeof(frame.sensors));
}

void test_color_display_trends_skip_idle_and_pause_samples_and_reset_at_next_run() {
    ColorDisplayPresenter presenter(60);
    AppSnapshot snapshot = makeSnapshot(WaterState::Running, WaterMode::Volume, 1500);
    snapshot.sensors.sampleSequence = 1;  // Read before the start of this run.
    presenter.observe(snapshot);
    ColorDisplayFrame frame = presenter.render(snapshot, 0, true);
    assertTextEquals("---", frame.sensors[1].value);

    snapshot.water.elapsedSec = 1;
    snapshot.sensors.sampleSequence = 2;
    snapshot.sensors.tdsPpm = {true, 30};
    presenter.observe(snapshot);
    snapshot.water.state = WaterState::Paused;
    snapshot.sensors.sampleSequence = 3;
    snapshot.sensors.tdsPpm = {true, 90};
    presenter.observe(snapshot);
    presenter.configure(60);
    frame = presenter.render(snapshot, 35000, true);
    assertTextEquals("00:01", frame.trendTime);
    assertTextEquals("30", frame.sensors[1].value);

    snapshot.water.state = WaterState::Running;
    presenter.observe(snapshot);
    snapshot.sensors.sampleSequence = 4;
    snapshot.sensors.tdsPpm = {true, 0};
    snapshot.water.elapsedSec = 2;
    presenter.observe(snapshot);
    frame = presenter.render(snapshot, 36000, true);
    assertTextEquals("0", frame.sensors[1].low);
    assertTextEquals("30", frame.sensors[1].high);
    assertTextEquals("0", frame.sensors[1].value);

    snapshot.sensors.sampleSequence = 5;
    snapshot.sensors.tdsPpm = {};
    snapshot.water.elapsedSec = 3;
    presenter.observe(snapshot);
    frame = presenter.render(snapshot, 37000, true);
    assertTextEquals("---", frame.sensors[1].value);
    assertTextEquals("30", frame.sensors[1].high);
    TEST_ASSERT_EQUAL_UINT8(0, frame.sensors[1].latestY);

    snapshot.water.state = WaterState::Idle;
    presenter.observe(snapshot);
    snapshot.water.state = WaterState::Running;
    snapshot.water.elapsedSec = 0;
    presenter.observe(snapshot);
    frame = presenter.render(snapshot, 38000, true);
    assertTextEquals("---", frame.sensors[1].high);
}

void test_volume_estimate_counts_down_and_only_corrects_sustained_changes() {
    ColorDisplayPresenter presenter;
    auto run = makeSnapshot(WaterState::Running, WaterMode::Volume, 1000);

    assertTextEquals("--", showVolumeEstimate(presenter, run, 0, 0, 0).timer);
    assertTextEquals("--", showVolumeEstimate(presenter, run, 500, 10, 3000).timer);
    assertTextEquals("--", showVolumeEstimate(presenter, run, 2499, 110, 3000).timer);
    auto frame = showVolumeEstimate(presenter, run, 2500, 125, 3000);
    assertTextEquals("00:18", frame.timer);
    assertTextEquals("总计约 00:20", frame.subtitle);

    assertTextEquals("00:17", showVolumeEstimate(presenter, run, 3500, 175, 2400).timer);
    assertTextEquals("00:16", showVolumeEstimate(presenter, run, 4500, 225, 4200).timer);
    assertTextEquals("00:15", showVolumeEstimate(presenter, run, 5500, 275, 3000).timer);
    assertTextEquals("00:14", showVolumeEstimate(presenter, run, 6500, 325, 2400).timer);
    assertTextEquals("00:14", showVolumeEstimate(presenter, run, 6999, 345, 2400).timer);
    frame = showVolumeEstimate(presenter, run, 7500, 365, 2400);
    assertTextEquals("00:16", frame.timer);
    assertTextEquals("总计约 00:24", frame.subtitle);

    assertTextEquals("00:15", showVolumeEstimate(presenter, run, 8500, 425, 3600).timer);
    frame = showVolumeEstimate(presenter, run, 9500, 485, 3600);
    assertTextEquals("00:09", frame.timer);
    assertTextEquals("总计约 00:19", frame.subtitle);
}

void test_volume_estimate_handles_expiry_pause_no_flow_and_new_runs() {
    ColorDisplayPresenter presenter;
    auto run = makeSnapshot(WaterState::Running, WaterMode::Volume, 200);

    showVolumeEstimate(presenter, run, 0, 0, 3000);
    assertTextEquals("00:02", showVolumeEstimate(presenter, run, 2000, 125, 3000).timer);
    assertTextEquals("00:01", showVolumeEstimate(presenter, run, 3499, 154, 3000).timer);
    auto frame = showVolumeEstimate(presenter, run, 3500, 155, 3000);
    assertTextEquals("00:01", frame.timer);
    assertTextEquals("总计约 00:05", frame.subtitle);

    run.water.state = WaterState::Paused;
    frame = showVolumeEstimate(presenter, run, 3500, 155, 3000);
    assertTextEquals("阀关", frame.status);
    assertTextEquals("", frame.subtitle);
    run.water.state = WaterState::Running;
    assertTextEquals("--", showVolumeEstimate(presenter, run, 3500, 155, 300).timer);
    assertTextEquals("00:07", showVolumeEstimate(presenter, run, 5500, 165, 300).timer);
    frame = showVolumeEstimate(presenter, run, 5600, 165, 0);
    assertTextEquals("--", frame.timer);
    assertTextEquals("", frame.subtitle);
    assertTextEquals("--", showVolumeEstimate(presenter, run, 5700, 170, 300).timer);

    run.water.state = WaterState::Idle;
    run.localMode = LocalUiMode::Result;
    run.lastDisplayResult = {true, WaterMode::Volume, WaterResult::Completed, 200, 200, 6, 0};
    frame = showVolumeEstimate(presenter, run, 6000, 200, 0);
    assertTextEquals("00:06", frame.timer);
    TEST_ASSERT_EQUAL_UINT16(1000, frame.progressPermille);

    run = makeSnapshot(WaterState::Running, WaterMode::Volume, 200);
    assertTextEquals("--", showVolumeEstimate(presenter, run, 0, 0, 12000).timer);
    assertTextEquals("--", showVolumeEstimate(presenter, run, 900, 180, 12000).timer);
    run.water.state = WaterState::Idle;
    run.localMode = LocalUiMode::Result;
    run.lastDisplayResult = {true, WaterMode::Volume, WaterResult::Completed, 200, 200, 1, 0};
    assertTextEquals("00:01", showVolumeEstimate(presenter, run, 1000, 200, 0).timer);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_color_display_covers_idle_confirm_running_pause_result_alert_calibration_offline_and_sleep_pages);
    RUN_TEST(test_color_display_uses_confirmed_target_and_frozen_result);
    RUN_TEST(test_color_display_confirm_page_hides_unavailable_estimates_and_uses_short_hints);
    RUN_TEST(test_color_display_confirm_volume_estimates_duration_from_metering_params_when_snapshot_omits_it);
    RUN_TEST(test_color_display_trends_preserve_full_run_with_gaps_and_freeze_at_result);
    RUN_TEST(test_color_display_trends_skip_idle_and_pause_samples_and_reset_at_next_run);
    RUN_TEST(test_volume_estimate_counts_down_and_only_corrects_sustained_changes);
    RUN_TEST(test_volume_estimate_handles_expiry_pause_no_flow_and_new_runs);
    return UNITY_END();
}
