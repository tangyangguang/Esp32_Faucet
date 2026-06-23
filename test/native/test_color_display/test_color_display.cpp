#include <unity.h>

#include "app/ColorDisplayPresenter.h"

#include <cstdio>
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
    record.temperatureAvgCentiC = 2470;
    record.temperatureMinCentiC = 2460;
    record.temperatureMaxCentiC = 2480;
    record.tdsAvgPpm = 386;
    record.tdsMinPpm = 380;
    record.tdsMaxPpm = 390;
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::StandbyOffline),
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
    assertTextEquals("12.8", frame.metrics[1].value);
    assertTextEquals("L/min", frame.metrics[1].unit);
    assertTextEquals("TDS · 近30秒", frame.sensors[0].label);
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

    AppSnapshot calReady = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500, 0);
    calReady.localMode = LocalUiMode::Calibration;
    calReady.calibrationStatus = CalibrationSessionStatus::WaitingLocalRun;
    calReady.calibrationValidSampleCount = 2;
    frame = presenter.render(calReady, 2400, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::CalibrationReady),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("等待本地出水", frame.title);

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
    assertTextEquals("停止", frame.hints[0]);

    AppSnapshot calEntry = makeSnapshot(WaterState::Idle, WaterMode::Volume, 1500, 920);
    calEntry.localMode = LocalUiMode::RecordCalibration;
    calEntry.calibrationActualMl = 900;
    calEntry.calibrationStepMl = 100;
    frame = presenter.render(calEntry, 2500, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ColorDisplayPage::CalibrationEntry),
                            static_cast<std::uint8_t>(frame.page));
    assertTextEquals("量杯实测", frame.title);
    assertTextEquals("0.90", frame.mainValue);

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
    assertTextEquals("均TDS", frame.metrics[2].label);
    assertTextEquals("386", frame.metrics[2].value);
    assertTextEquals("均水温", frame.metrics[3].label);
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

void test_st7789_driver_avoids_per_pixel_text_rendering_in_loop() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char buffer[24000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "drawGlyphBlock"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "SPI.writePattern"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "SPI.writeBytes"));
    TEST_ASSERT_NULL(std::strstr(buffer, "x + gx * scale"));
}

void test_st7789_driver_uses_tft_espi_verified_no_cs_init_path_and_spi_mode() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char buffer[76000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kSt7789SpiHz = 27000000UL"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kSt7789SpiMode = SPI_MODE3"));
    TEST_ASSERT_NULL(std::strstr(buffer, "kSt7789SpiMode = SPI_MODE2"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kCmdRamCtrl = 0xB0"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "kCmdInvOn"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "tftInit"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "renderFrame(frame, true)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "beginBufferedFrame(fullRedraw)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "if (fullRedraw && !buffered)"));
    TEST_ASSERT_NULL(std::strstr(buffer, "data(buffer, sizeof(buffer));\n    data(buffer, sizeof(buffer));"));
}

void test_st7789_driver_has_compile_time_boot_self_test_path() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char buffer[36000]{};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "FAUCET_ST7789_BOOT_TEST"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "drawBootTestPattern"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "fillRect(0, 0, 80, kHeight, kRed)"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "drawCenteredText(108, \"ST7789 BOOT\""));
}

void test_st7789_formal_build_uses_tft_espi_backend() {
    FILE* sourceFile = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(sourceFile);
    char source[42000]{};
    const std::size_t sourceRead = std::fread(source, 1, sizeof(source) - 1, sourceFile);
    std::fclose(sourceFile);
    TEST_ASSERT_GREATER_THAN_size_t(0, sourceRead);

    FILE* platformFile = std::fopen("platformio.ini", "r");
    TEST_ASSERT_NOT_NULL(platformFile);
    char platform[12000]{};
    const std::size_t platformRead = std::fread(platform, 1, sizeof(platform) - 1, platformFile);
    std::fclose(platformFile);
    TEST_ASSERT_GREATER_THAN_size_t(0, platformRead);

    TEST_ASSERT_NOT_NULL(std::strstr(source, "FAUCET_ST7789_USE_TFT_ESPI"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "#include <TFT_eSPI.h>"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "g_tft.init()"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "g_tft.fillRect"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "g_tft.pushColors"));
    TEST_ASSERT_NOT_NULL(std::strstr(platform, "-D FAUCET_ST7789_USE_TFT_ESPI=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(platform, "bodmer/TFT_eSPI @ 2.5.43"));
    TEST_ASSERT_NOT_NULL(std::strstr(platform, "-D LOAD_FONT2=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(platform, "-D LOAD_FONT4=1"));
    TEST_ASSERT_NOT_NULL(std::strstr(platform, "-D LOAD_FONT7=1"));
}

void test_st7789_main_numbers_do_not_use_scaled_16x16_bitmap_text() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char source[52000]{};
    const std::size_t read = std::fread(source, 1, sizeof(source) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawMainValue"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawAsciiText"));
    TEST_ASSERT_NULL(std::strstr(source, "drawCenteredText(69, frame.mainValue"));
    TEST_ASSERT_NULL(std::strstr(source, "drawCenteredText(91, frame.mainValue"));
    TEST_ASSERT_NULL(std::strstr(source, "drawText(32, 82, frame.mainValue"));
    TEST_ASSERT_NULL(std::strstr(source, "drawCenteredText(74, frame.mainValue"));
}

void test_st7789_formal_build_disables_temporary_boot_test() {
    FILE* platformFile = std::fopen("platformio.ini", "r");
    TEST_ASSERT_NOT_NULL(platformFile);

    char platform[12000]{};
    const std::size_t read = std::fread(platform, 1, sizeof(platform) - 1, platformFile);
    std::fclose(platformFile);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NULL(std::strstr(platform, "-D FAUCET_ST7789_BOOT_TEST=1"));
}

void test_st7789_standby_cards_use_black_background_and_fit_text() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char source[62000]{};
    const std::size_t read = std::fread(source, 1, sizeof(source) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(source, "constexpr std::uint16_t kBg = kBlack"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawMetricLabel"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawTextFit"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawAsciiTrackedText"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawMetricCard(14, 184, 78"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawMetricCard(99, 184, 66"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawMetricCard(173, 184, 53"));
    TEST_ASSERT_NULL(std::strstr(source, "14 + i * 75"));
}

void test_st7789_layout_uses_padded_badges_and_non_overlapping_cards() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char source[70000]{};
    const std::size_t read = std::fread(source, 1, sizeof(source) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawTagPill"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "constexpr std::int16_t kTagHeight = 22"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawStatusPill"));
    TEST_ASSERT_NULL(std::strstr(source, "fillRoundRect(tagX, 13, tagW, 16"));
    TEST_ASSERT_NULL(std::strstr(source, "drawText(static_cast<std::int16_t>(tagX + 5), 13, frame.tag"));
    TEST_ASSERT_NOT_NULL(
        std::strstr(source, "drawMetricCard(132, static_cast<std::int16_t>(48 + i * 38), 94, frame.metrics[i], 36)"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "static_cast<std::int16_t>(142 + (i / 2) * 39)"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "static_cast<std::int16_t>(140 + (i / 2) * 39)"));
    TEST_ASSERT_NULL(std::strstr(source, "145 + (i / 2) * 38"));
    TEST_ASSERT_NULL(std::strstr(source, "142 + (i / 2) * 38"));
}

void test_st7789_layout_buffers_full_redraws_and_fits_confirm_and_hints() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char source[76000]{};
    const std::size_t read = std::fread(source, 1, sizeof(source) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NOT_NULL(std::strstr(source, "TFT_eSprite g_frameSprite"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "beginBufferedFrame"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "endBufferedFrame"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "pushSprite(0, 0)"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "if (fullRedraw && !buffered)"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawCenteredTextFit"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawHintSlot"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawTextFit"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "drawBoxCenteredText"));
    TEST_ASSERT_NULL(std::strstr(source, "fillRoundRect(15, 55, 210, 105"));
    TEST_ASSERT_NULL(std::strstr(source, "drawCenteredText(143, \"确认后开始出水\""));
    TEST_ASSERT_NULL(std::strstr(source, "drawCenteredText(64, frame.title"));
    TEST_ASSERT_NULL(std::strstr(source, "drawText(x, 221, frame.hints[i]"));
    TEST_ASSERT_NULL(std::strstr(
        source, "drawText(static_cast<std::int16_t>(x + 6), static_cast<std::int16_t>(y + 4), sensor.label"));
}

void test_st7789_updates_use_buffered_frames_to_avoid_visible_erase() {
    FILE* file = std::fopen("src/drivers/St7789Display.cpp", "r");
    TEST_ASSERT_NOT_NULL(file);

    char source[76000]{};
    const std::size_t read = std::fread(source, 1, sizeof(source) - 1, file);
    std::fclose(file);
    TEST_ASSERT_GREATER_THAN_size_t(0, read);

    TEST_ASSERT_NULL(std::strstr(source, "const bool runningPage"));
    TEST_ASSERT_NULL(std::strstr(source, "!runningPage"));
    TEST_ASSERT_NOT_NULL(std::strstr(source, "renderFrame(frame, true)"));
    TEST_ASSERT_NULL(std::strstr(source, "renderPartialFrame(frame, lastFrame_)"));
    TEST_ASSERT_NULL(std::strstr(source, "renderStandbyPartialFrame"));
    TEST_ASSERT_NULL(std::strstr(source, "renderConfirmPartialFrame"));

    FILE* platformFile = std::fopen("platformio.ini", "r");
    TEST_ASSERT_NOT_NULL(platformFile);
    char platform[12000]{};
    const std::size_t platformRead = std::fread(platform, 1, sizeof(platform) - 1, platformFile);
    std::fclose(platformFile);
    TEST_ASSERT_GREATER_THAN_size_t(0, platformRead);
    TEST_ASSERT_NOT_NULL(std::strstr(platform, "-D SPI_FREQUENCY=27000000"));
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
    RUN_TEST(test_st7789_driver_avoids_per_pixel_text_rendering_in_loop);
    RUN_TEST(test_st7789_driver_uses_tft_espi_verified_no_cs_init_path_and_spi_mode);
    RUN_TEST(test_st7789_driver_has_compile_time_boot_self_test_path);
    RUN_TEST(test_st7789_formal_build_uses_tft_espi_backend);
    RUN_TEST(test_st7789_main_numbers_do_not_use_scaled_16x16_bitmap_text);
    RUN_TEST(test_st7789_formal_build_disables_temporary_boot_test);
    RUN_TEST(test_st7789_standby_cards_use_black_background_and_fit_text);
    RUN_TEST(test_st7789_layout_uses_padded_badges_and_non_overlapping_cards);
    RUN_TEST(test_st7789_layout_buffers_full_redraws_and_fits_confirm_and_hints);
    RUN_TEST(test_st7789_updates_use_buffered_frames_to_avoid_visible_erase);
    return UNITY_END();
}
