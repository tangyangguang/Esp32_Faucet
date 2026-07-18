#include <Arduino.h>
#include <Esp32Base.h>
#include <network/Esp32BaseWiFi.h>
#include <Wire.h>

#include "app/AppController.h"
#include "app/BeepDriver.h"
#include "app/CalibrationSessionTraceStore.h"
#include "app/ColorDisplayPresenter.h"
#include "app/CalibrationSessionStore.h"
#include "app/ConfigStore.h"
#include "app/DateTimeUtils.h"
#include "app/Esp32BaseConfigBackend.h"
#include "app/Esp32BaseWaterRecordBackend.h"
#include "app/FaucetAppConfig.h"
#include "app/FilterStore.h"
#include "app/MeteringSchemeStore.h"
#include "app/StatisticsStore.h"
#include "app/TimeUtils.h"
#include "app/WaterRecordFileStore.h"
#include "app/WaterPulseTraceStore.h"
#include "drivers/BoardPins.h"
#include "drivers/Ads1115AdcReader.h"
#include "drivers/FlowPulseReader.h"
#include "drivers/GpioButtonReader.h"
#include "drivers/PwmBeepHardware.h"
#include "drivers/PwmValveHardware.h"
#include "drivers/St7789Display.h"
#include "web/FaucetWeb.h"

#include <algorithm>
#include <new>
#include <time.h>

namespace {
constexpr const char* kFirmwareName = "esp32-faucet";
constexpr const char* kFirmwareVersion = "0.1.0-dev";
constexpr const char* kDefaultWebUser = "admin";
constexpr const char* kDefaultWebPassword = "admin";
constexpr std::size_t kWaterRecordCapacity = 15000;
constexpr std::size_t kPulseTraceCapacity = faucet::kRecentPulseTraceCount;
constexpr std::size_t kPulseTraceMaxBuckets =
    static_cast<std::size_t>(faucet::kRecentPulseTraceCount) * faucet::kPulseTraceMaxBucketsPerTrace;
constexpr std::size_t kPulseTraceMaxStartupEdges =
    static_cast<std::size_t>(faucet::kRecentPulseTraceCount) * faucet::kPulseTraceMaxStartupEdgesPerTrace;
constexpr std::uint32_t kRuntimePersistenceRetryIntervalMs = 30000UL;
constexpr std::size_t kMaxFlowPulsesPerTick = 32;
constexpr std::uint32_t kI2cTimeoutMs = 20UL;
constexpr const char* kWaterRecordPath = "/faucet_records_v3.bin";
constexpr const char* kMeteringSchemePath = "/faucet_metering_schemes_v8.bin";
constexpr const char* kCalibrationSessionPath = "/faucet_cal_session_v1.bin";
constexpr const char* kCalibrationSessionTracePath = "/faucet_cal_session_traces_v1.bin";

class FileRecordWriter : public faucet::WaterRecordWriter {
public:
    explicit FileRecordWriter(faucet::WaterRecordFileStore& fileStore) : fileStore_(fileStore) {}

    bool append(const faucet::WaterRecord& record) override {
        return fileStore_.append(record);
    }

private:
    faucet::WaterRecordFileStore& fileStore_;
};

faucet::Esp32BaseConfigBackend g_configBackend;
faucet::ConfigStore g_configStore(g_configBackend);
faucet::SystemConfig g_config{};
faucet::StatisticsStore g_statistics;
faucet::FilterStore* g_filters = nullptr;
faucet::Esp32BaseWaterRecordBackend g_waterRecordBackend;
faucet::WaterRecordFileStore g_waterRecordFile(g_waterRecordBackend, kWaterRecordPath, kWaterRecordCapacity);
faucet::MeteringSchemeStore g_meteringSchemes(g_waterRecordBackend, kMeteringSchemePath);
faucet::CalibrationSessionFileStore g_calibrationSession(g_waterRecordBackend, kCalibrationSessionPath);
faucet::CalibrationSessionTraceStore g_calibrationSessionTraces(g_waterRecordBackend, kCalibrationSessionTracePath);
FileRecordWriter g_recordWriter(g_waterRecordFile);
faucet::WaterPulseTrace* g_pulseTraceRecords = nullptr;
faucet::WaterPulseTraceBucketSample* g_pulseTraceBuckets = nullptr;
faucet::WaterPulseTraceSample* g_pulseTraceStartupEdges = nullptr;
faucet::WaterPulseTraceStore* g_pulseTraces = nullptr;
faucet::AppController* g_app = nullptr;
faucet::GpioButtonReader g_buttons(faucet::kPinButtonCancel,
                                   faucet::kPinButtonOk,
                                   faucet::kPinButtonPlus,
                                   faucet::kPinButtonMinus);
faucet::FlowPulseReader g_flowPulses(faucet::kPinFlowPrimary);
faucet::Ads1115AdcReader g_waterSensorAdc;
faucet::WaterSensorManager g_waterSensors(g_waterSensorAdc, false);
faucet::PwmValveHardware g_valveHardware(
    faucet::kPinValvePwm, faucet::kPinValveShutdown, faucet::kLedcChannelValve);
faucet::BeepDriver g_beep;
faucet::PwmBeepHardware g_beepHardware(faucet::kPinBeep, faucet::kLedcChannelBeep);
faucet::St7789Display g_st7789(faucet::kPinSt7789Backlight);
faucet::ColorDisplayPresenter* g_colorDisplay = nullptr;
faucet::ColorDisplayFrame g_lastColorDisplayFrame{};
bool g_persistenceFailureLogged = false;
bool g_runtimePersistenceRetryActive = false;
bool g_rebuildRecordStoreAfterFormatFs = false;
std::uint32_t g_lastRuntimePersistenceFailureMs = 0;
std::uint32_t g_lastColorDisplayMs = 0;
std::uint32_t g_lastDroppedPulsesLogged = 0;
std::uint32_t g_lastDroppedPulsesLogMs = 0;
bool g_bootRelativeTimesRewritten = false;
std::uint32_t g_lastLoopStartUs = 0;
std::uint32_t g_maxLoopIntervalUs = 0;
std::uint32_t g_maxAppTickUs = 0;
std::uint32_t g_maxBaseHandleUs = 0;
bool g_baseBeginComplete = false;
bool g_littleFsInitComplete = false;
bool g_configInitComplete = false;
bool g_recordStoreInitComplete = false;
bool g_traceStoreInitComplete = false;
bool g_runtimeStateInitComplete = false;
bool g_appControllerInitComplete = false;
bool g_valveClosedApplied = false;
bool g_otaMarkValidAttempted = false;

void configureBase() {
    Esp32Base::setFirmwareInfo(kFirmwareName, kFirmwareVersion, __DATE__ " " __TIME__);

#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth(kDefaultWebUser, kDefaultWebPassword);
    Esp32BaseWeb::setDeviceName("首页");
    Esp32BaseWeb::setHomePath("/index");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    if (!faucet::registerFaucetAppConfig()) {
        ESP32BASE_LOG_E("app", "app config registration failed");
    }
    if (!faucet::registerFaucetWeb()) {
        ESP32BASE_LOG_E("app", "faucet web route registration failed");
    }
#endif
}

void feedStartupWatchdog() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
}

void logStartupPhase(const char* phase) {
    ESP32BASE_LOG_D("app", "startup_phase=%s", phase ? phase : "-");
    feedStartupWatchdog();
}

void initializeI2cBus() {
    Wire.begin(faucet::kPinI2cSda, faucet::kPinI2cScl);
    Wire.setTimeOut(kI2cTimeoutMs);
}

const char* configLoadStatusName(faucet::ConfigStore::LoadStatus status) {
    switch (status) {
        case faucet::ConfigStore::LoadStatus::Defaults:
            return "defaults";
        case faucet::ConfigStore::LoadStatus::LoadedCurrent:
            return "loaded_current";
    }
    return "unknown";
}

void logSystemConfigStatus() {
    const faucet::ConfigStore::LoadStatus status = g_configStore.lastSystemConfigLoadStatus();
    ESP32BASE_LOG_I("app",
                    "system config load status=%s raw_version=%ld target_version=%ld",
                    configLoadStatusName(status),
                    static_cast<long>(g_configStore.lastSystemConfigRawVersion()),
                    static_cast<long>(g_configStore.currentSystemConfigVersion()));
    if (status == faucet::ConfigStore::LoadStatus::Defaults &&
        g_configStore.lastSystemConfigRawVersion() != g_configStore.currentSystemConfigVersion()) {
        ESP32BASE_LOG_W("app", "system config version mismatch, defaults used until next save");
    }
}

void checkFileSystemCapacity() {
#if ESP32BASE_ENABLE_FS
    if (!Esp32BaseFs::isReady()) {
        return;
    }
    constexpr std::size_t kLowFreeBytes = 350UL * 1024UL;
    const std::size_t freeBytes = Esp32BaseFs::freeBytes();
    if (freeBytes < kLowFreeBytes) {
        ESP32BASE_LOG_W("app",
                        "LittleFS low free space: free=%lu total=%lu used=%lu",
                        static_cast<unsigned long>(freeBytes),
                        static_cast<unsigned long>(Esp32BaseFs::totalBytes()),
                        static_cast<unsigned long>(Esp32BaseFs::usedBytes()));
    }
#endif
}

std::uint32_t localSecondsFromUnix(std::uint32_t timestamp) {
    time_t value = static_cast<time_t>(timestamp);
    struct tm localTime {};
    localtime_r(&value, &localTime);
    const std::uint16_t year = static_cast<std::uint16_t>(localTime.tm_year + 1900);
    const std::uint8_t month = static_cast<std::uint8_t>(localTime.tm_mon + 1);
    const std::uint8_t day = static_cast<std::uint8_t>(localTime.tm_mday);
    return faucet::secondsSince2000(year,
                                    month,
                                    day,
                                    static_cast<std::uint8_t>(localTime.tm_hour),
                                    static_cast<std::uint8_t>(localTime.tm_min),
                                    static_cast<std::uint8_t>(localTime.tm_sec));
}

std::uint32_t currentSeconds() {
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot time = Esp32BaseNtp::snapshot();
    if (time.synced) {
        return localSecondsFromUnix(time.epochSec);
    }
    return time.uptimeSec;
#else
    return millis() / 1000UL;
#endif
}

std::uint32_t currentBootId() {
#if ESP32BASE_ENABLE_NTP
    return Esp32BaseNtp::snapshot().bootId;
#else
    return 0;
#endif
}

faucet::FaucetDisplayStatus currentDisplayStatus() {
    return faucet::FaucetDisplayStatus{g_lastColorDisplayFrame.on};
}

faucet::FaucetRuntimeDiagnostics currentRuntimeDiagnostics() {
    return faucet::FaucetRuntimeDiagnostics{g_maxLoopIntervalUs, g_maxAppTickUs, g_maxBaseHandleUs};
}

void applyValveOutput(faucet::ValveOutput output) {
    g_valveHardware.apply(output);
}

void requestRecordStoreRebuildAfterFormatFs() {
    g_rebuildRecordStoreAfterFormatFs = true;
}

void applyImmediateRuntimeSettings(const faucet::SystemConfig& config) {
    g_beep.setEnabled(config.beepEnabled);
    if (!g_valveHardware.configureFrequency(config.valvePwmFrequencyHz)) {
        ESP32BASE_LOG_E("app", "valve PWM frequency apply failed hz=%lu",
                        static_cast<unsigned long>(config.valvePwmFrequencyHz));
    }
    if (g_app) {
        g_valveHardware.apply(g_app->snapshot().valve);
    }
    if (g_colorDisplay) {
        g_colorDisplay->configure(config.displaySleepSec);
        g_colorDisplay->wake(millis());
    }
}

void applyRuntimeSettings(const faucet::SystemConfig& config) {
    applyImmediateRuntimeSettings(config);
    g_waterSensors.configure(config);
    g_st7789.begin();
}

bool currentPeriodKeys(std::uint32_t nowSeconds, faucet::PeriodKeys& keys) {
    if (nowSeconds >= faucet::kMinRealDateSeconds) {
        const std::uint32_t day = nowSeconds / 86400UL;
        std::uint16_t year = 0;
        std::uint8_t month = 0;
        std::uint8_t monthDay = 0;
        faucet::dateFromDayIndex(day, year, month, monthDay);
        const std::uint32_t dayKey =
            static_cast<std::uint32_t>(year) * 10000UL + static_cast<std::uint32_t>(month) * 100UL + monthDay;
        const std::uint32_t weekKey = nowSeconds / 86400UL / 7UL;
        const std::uint32_t monthKey = static_cast<std::uint32_t>(year) * 100UL + month;
        keys = faucet::PeriodKeys{dayKey, weekKey, monthKey};
        return true;
    }
    keys = faucet::PeriodKeys{0, 0, 0};
    return false;
}

void handleTimeSynced(const Esp32BaseNtp::TimeSnapshot& time);

void initializeApplication() {
    logStartupPhase("app_init_start");
    initializeI2cBus();
    logStartupPhase("i2c_ready");
    g_config = g_configStore.loadSystemConfig();
    g_waterSensors.configure(g_config);
    const bool waterSensorAdcReady = g_waterSensors.begin();
    ESP32BASE_LOG_I("app",
                    "water_sensor_adc=%s type=ads1115 address=0x%02x temp_channel=1 tds_channel=2",
                    waterSensorAdcReady ? "ready" : "absent",
                    static_cast<unsigned>(faucet::kAds1115Address));
    logSystemConfigStatus();
    logStartupPhase("config_ready");
    g_configInitComplete = true;
    const std::uint32_t nowSeconds = currentSeconds();
    const bool waterRecordReady = g_waterRecordFile.begin();
    const bool meteringSchemesReady = g_meteringSchemes.begin();
    if (!meteringSchemesReady) {
        ESP32BASE_LOG_W("app", "metering scheme store unavailable, using config fallback");
    }
    const bool calibrationSessionReady = g_calibrationSession.begin();
    const bool calibrationSessionTracesReady = g_calibrationSessionTraces.begin();
    if (!calibrationSessionReady || !calibrationSessionTracesReady) {
        ESP32BASE_LOG_W("app", "guided calibration session storage unavailable");
    }
    g_recordStoreInitComplete = waterRecordReady && calibrationSessionReady;
    logStartupPhase("record_store_ready");
    g_pulseTraceRecords = new (std::nothrow) faucet::WaterPulseTrace[kPulseTraceCapacity]{};
    g_pulseTraceBuckets = new (std::nothrow) faucet::WaterPulseTraceBucketSample[kPulseTraceMaxBuckets]{};
    g_pulseTraceStartupEdges = new (std::nothrow) faucet::WaterPulseTraceSample[kPulseTraceMaxStartupEdges]{};
    if (g_pulseTraceRecords && g_pulseTraceBuckets && g_pulseTraceStartupEdges) {
        g_pulseTraces = new (std::nothrow) faucet::WaterPulseTraceStore(
            g_pulseTraceRecords,
            kPulseTraceCapacity,
            g_pulseTraceBuckets,
            kPulseTraceMaxBuckets,
            g_pulseTraceStartupEdges,
            kPulseTraceMaxStartupEdges,
            faucet::kRecentPulseTraceCount);
    }
    if (!g_pulseTraces) {
        ESP32BASE_LOG_W("app", "pulse trace store allocation failed, trace diagnostics disabled");
    }
    g_traceStoreInitComplete = g_pulseTraces && calibrationSessionTracesReady;
    logStartupPhase("trace_store_ready");
    checkFileSystemCapacity();
    faucet::PeriodKeys periodKeys{};
    currentPeriodKeys(nowSeconds, periodKeys);
    g_statistics = faucet::StatisticsStore(g_configStore.loadStatistics(periodKeys));
    faucet::FilterRuntime filterRuntime[faucet::kFilterCount]{};
    g_configStore.loadFilterRuntime(filterRuntime);
    g_runtimeStateInitComplete = true;
    logStartupPhase("runtime_state_ready");
    g_filters = new (std::nothrow) faucet::FilterStore(g_config.filters);
    if (!g_filters) {
        ESP32BASE_LOG_E("app", "filter store allocation failed");
        return;
    }
    g_filters->applyRuntime(filterRuntime);
    if (g_pulseTraces) {
        g_pulseTraces->setRecentTraceLimit(faucet::kRecentPulseTraceCount);
    }
    faucet::MeteringSchemeRecord activeScheme{};
    if (meteringSchemesReady && g_meteringSchemes.activeScheme(activeScheme)) {
        g_app = new (std::nothrow) faucet::AppController(
            g_config, activeScheme,
            g_statistics,
            *g_filters,
            g_recordWriter,
            g_meteringSchemes,
            g_pulseTraces,
            &g_calibrationSession,
            &g_calibrationSessionTraces,
            &g_waterSensors);
    } else {
        g_app = new (std::nothrow) faucet::AppController(
            g_config,
            g_statistics,
            *g_filters,
            g_recordWriter,
            g_pulseTraces,
            &g_calibrationSession,
            &g_calibrationSessionTraces,
            &g_waterSensors);
    }
    if (!g_app) {
        ESP32BASE_LOG_E("app", "app controller allocation failed");
        return;
    }
    g_appControllerInitComplete = true;
    g_colorDisplay = new (std::nothrow) faucet::ColorDisplayPresenter(g_config.displaySleepSec);
    if (!g_colorDisplay) {
        ESP32BASE_LOG_W("app", "color display presenter allocation failed, st7789 disabled");
    }
    faucet::setFaucetWebContext(
        faucet::FaucetWebContext{&g_config,
                                 &g_configStore,
                                 &g_statistics,
                                 g_app,
                                 g_filters,
                                 &g_waterRecordFile,
                                 &g_meteringSchemes,
                                 &g_calibrationSession,
                                 &g_calibrationSessionTraces,
                                 g_pulseTraces,
                                 currentSeconds,
                                 currentBootId,
                                 applyRuntimeSettings,
                                 requestRecordStoreRebuildAfterFormatFs,
                                 currentDisplayStatus,
                                 currentRuntimeDiagnostics});
    faucet::setFaucetAppConfigContext(
        faucet::FaucetAppConfigContext{
            &g_config, &g_configStore, g_app, applyImmediateRuntimeSettings, applyRuntimeSettings});
#if ESP32BASE_ENABLE_NTP
    Esp32BaseNtp::onTimeSynced(handleTimeSynced);
#endif
    logStartupPhase("context_ready");

    g_buttons.begin();
    g_flowPulses.begin();
    if (!g_valveHardware.begin(g_config.valvePwmFrequencyHz)) {
        ESP32BASE_LOG_E("app", "valve PWM initialization failed hz=%lu",
                        static_cast<unsigned long>(g_config.valvePwmFrequencyHz));
    }
    g_app->setValveOutputSink(applyValveOutput);
    const faucet::ValveOutput startupValve = g_app->snapshot().valve;
    g_valveHardware.apply(startupValve);
    g_valveClosedApplied =
        startupValve.state == faucet::ValveState::Closed && !startupValve.enabled && startupValve.dutyPercent == 0;
    g_beep.setEnabled(g_config.beepEnabled);
    g_beepHardware.begin();
    g_st7789.begin();
    logStartupPhase("hardware_ready");

    g_app->resetInputs(g_buttons.read(), millis());
    if (g_colorDisplay) {
        g_colorDisplay->wake(millis());
        g_lastColorDisplayFrame = g_colorDisplay->render(g_app->snapshot(), millis(), Esp32BaseWiFi::isConnected());
        g_st7789.apply(g_lastColorDisplayFrame);
    }
    logStartupPhase("display_ready");

    ESP32BASE_LOG_I("app",
                    "application initialized st7789=%s records=%s",
                    g_st7789.present() ? "present" : "absent",
                    g_waterRecordFile.ready() ? "file" : "unavailable");
}

void handleTimeSynced(const Esp32BaseNtp::TimeSnapshot& time) {
#if ESP32BASE_ENABLE_NTP
    if (!time.synced || g_bootRelativeTimesRewritten || time.bootId == 0) {
        return;
    }
    std::uint32_t bootStartEpochSec = 0;
    if (!Esp32BaseNtp::resolveCurrentBootEvent(time.bootId, 0, &bootStartEpochSec)) {
        return;
    }
    const std::uint32_t bootStartRealSec = localSecondsFromUnix(bootStartEpochSec);
    const std::size_t recordCount = g_waterRecordFile.rewriteBootRelativeTimes(time.bootId, bootStartRealSec);
    bool filtersChanged = false;
    if (g_filters) {
        for (std::size_t i = 0; i < faucet::kFilterCount; ++i) {
            faucet::FilterRecord record = g_filters->record(i);
            if (record.startBootId == time.bootId && record.startTime > 0 &&
                record.startTime < faucet::kMinRealDateSeconds) {
                std::uint32_t eventEpochSec = 0;
                if (!Esp32BaseNtp::resolveCurrentBootEvent(record.startBootId, record.startTime, &eventEpochSec)) {
                    continue;
                }
                record.startTime = localSecondsFromUnix(eventEpochSec);
                record.startBootId = 0;
                g_filters->updateFilter(i, record);
                filtersChanged = true;
            }
        }
    }
    if (filtersChanged) {
        faucet::FilterRuntime filterRuntime[faucet::kFilterCount]{};
        g_filters->copyRuntime(filterRuntime);
        if (!g_configStore.saveFilterRuntime(filterRuntime)) {
            ESP32BASE_LOG_E("app", "filter time correction persistence failed");
        }
    }
    if (recordCount > 0 || filtersChanged) {
        ESP32BASE_LOG_I("app",
                        "boot_relative_times_rewritten boot_id=%lu records=%lu filters=%s",
                        static_cast<unsigned long>(time.bootId),
                        static_cast<unsigned long>(recordCount),
                        filtersChanged ? "yes" : "no");
    }
    g_bootRelativeTimesRewritten = true;
#else
    (void)time;
#endif
}

void runApplicationTick() {
    if (!g_app) {
        return;
    }

    if (g_rebuildRecordStoreAfterFormatFs) {
        g_rebuildRecordStoreAfterFormatFs = false;
        if (!g_waterRecordFile.begin()) {
            ESP32BASE_LOG_E("app", "record store rebuild after fs format failed");
        }
    }

    const bool cancelInterruptPending = g_buttons.consumeCancelInterrupt();
    const std::uint32_t nowMs = millis();
    const std::uint32_t nowUs = micros();
    if (cancelInterruptPending) {
        g_app->emergencyStop(nowMs);
    }
    const faucet::ButtonLevels levels = g_buttons.read();

    std::uint32_t pulseUs = 0;
    std::size_t processedPulses = 0;
    while (processedPulses < kMaxFlowPulsesPerTick && g_flowPulses.pop(pulseUs)) {
        g_app->onFlowPulse(pulseUs);
        ++processedPulses;
    }
    const std::uint32_t droppedPulses = g_flowPulses.droppedPulses();
    g_app->setFlowDroppedPulses(droppedPulses);
    if (droppedPulses != g_lastDroppedPulsesLogged &&
        faucet::elapsedAtLeast(nowMs, g_lastDroppedPulsesLogMs, 5000UL)) {
        ESP32BASE_LOG_W("app",
                        "flow pulse buffer dropped pulses: total=%lu delta=%lu",
                        static_cast<unsigned long>(droppedPulses),
                        static_cast<unsigned long>(droppedPulses - g_lastDroppedPulsesLogged));
        g_lastDroppedPulsesLogged = droppedPulses;
        g_lastDroppedPulsesLogMs = nowMs;
    }

    const std::uint32_t nowSeconds = currentSeconds();
    faucet::PeriodKeys periodKeys{};
    const bool periodKeysValid = currentPeriodKeys(nowSeconds, periodKeys);
    const faucet::AppTickInput input{
        levels,
        nowMs,
        nowUs,
        nowSeconds,
        periodKeys,
        periodKeysValid,
        nowSeconds >= faucet::kMinRealDateSeconds,
        currentBootId(),
    };
    g_app->tick(input);
    if (g_app->applyPendingSystemConfigIfSafe()) {
        g_config = g_app->config();
        applyRuntimeSettings(g_config);
    }
    const faucet::BeepPattern beep = g_app->consumeBeepPattern();
    if (beep != faucet::BeepPattern::None) {
        g_beep.play(beep, nowMs);
    }

    const faucet::AppSnapshot snapshot = g_app->snapshot();
    g_valveHardware.apply(snapshot.valve);

    const bool userActivity =
        levels.cancelPressed || levels.okPressed || levels.plusPressed || levels.minusPressed;
    if (g_colorDisplay && (userActivity || snapshot.water.state != faucet::WaterState::Idle ||
                           snapshot.localMode != faucet::LocalUiMode::Normal)) {
        g_colorDisplay->wake(nowMs);
    }
    if (g_colorDisplay && faucet::elapsedAtLeast(nowMs, g_lastColorDisplayMs, 500UL)) {
        g_lastColorDisplayFrame = g_colorDisplay->render(snapshot, nowMs, Esp32BaseWiFi::isConnected());
        g_st7789.apply(g_lastColorDisplayFrame);
        g_lastColorDisplayMs = nowMs;
    }

    const bool runtimePersistenceRetryDue =
        !g_runtimePersistenceRetryActive ||
        faucet::elapsedAtLeast(nowMs, g_lastRuntimePersistenceFailureMs, kRuntimePersistenceRetryIntervalMs);
    if (runtimePersistenceRetryDue && g_app->consumePersistenceDirty() && g_filters) {
        ESP32BASE_LOG_I("app", "runtime_persistence_begin");
        faucet::FilterRuntime filterRuntime[faucet::kFilterCount]{};
        g_filters->copyRuntime(filterRuntime);
        const bool ok = g_configStore.saveStatistics(g_statistics.record()) &&
                        g_configStore.saveFilterRuntime(filterRuntime);
        ESP32BASE_LOG_I("app", "runtime_persistence_done ok=%s", ok ? "yes" : "no");
        if (!ok && !g_persistenceFailureLogged) {
            ESP32BASE_LOG_E("app", "runtime persistence failed");
            g_persistenceFailureLogged = true;
        }
        if (!ok) {
            g_app->markPersistenceDirtyForRetry();
            g_runtimePersistenceRetryActive = true;
            g_lastRuntimePersistenceFailureMs = nowMs;
        } else {
            g_persistenceFailureLogged = false;
            g_runtimePersistenceRetryActive = false;
        }
    }

    if (g_app->consumeConfigDirty()) {
        g_config = g_app->config();
        if (!g_configStore.saveSystemConfig(g_config)) {
            ESP32BASE_LOG_E("app", "system config persistence failed");
        }
    }

    g_beep.tick(nowMs);
    g_beepHardware.apply(g_beep.output());
}

bool startupHealthReadyForOtaMarkValid() {
    return g_baseBeginComplete &&
           g_littleFsInitComplete &&
           g_configInitComplete &&
           g_recordStoreInitComplete &&
           g_traceStoreInitComplete &&
           g_runtimeStateInitComplete &&
           g_appControllerInitComplete &&
           g_valveClosedApplied;
}

void maybeMarkOtaValidAfterHealthCheck() {
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
    if (g_otaMarkValidAttempted || !Esp32BaseOta::waitingForMarkValid()) {
        return;
    }
    if (!startupHealthReadyForOtaMarkValid()) {
        return;
    }
    ESP32BASE_LOG_I("app", "ota_health_check begin");
    g_otaMarkValidAttempted = true;
    if (Esp32BaseOta::markCurrentValid()) {
        ESP32BASE_LOG_I("app", "ota_mark_valid ok");
    } else {
        ESP32BASE_LOG_E("app", "ota_mark_valid failed");
    }
#endif
}
}  // namespace

void setup() {
    g_valveHardware.forceSafeState();
    Serial.begin(115200);
    delay(200);
    configureBase();

    g_baseBeginComplete = Esp32Base::begin();
    if (!g_baseBeginComplete) {
        ESP32BASE_LOG_E("app", "Esp32Base begin failed: %s", Esp32Base::lastError());
    }
#if ESP32BASE_ENABLE_FS
    g_littleFsInitComplete = Esp32BaseFs::isReady();
#else
    g_littleFsInitComplete = true;
#endif
    initializeApplication();
}

void loop() {
    const std::uint32_t loopStartUs = micros();
    if (g_lastLoopStartUs != 0) {
        g_maxLoopIntervalUs = std::max(g_maxLoopIntervalUs, faucet::elapsedSince(loopStartUs, g_lastLoopStartUs));
    }
    g_lastLoopStartUs = loopStartUs;

    const std::uint32_t appTickStartUs = micros();
    runApplicationTick();
    g_maxAppTickUs = std::max(g_maxAppTickUs, faucet::elapsedSince(micros(), appTickStartUs));

    const std::uint32_t baseHandleStartUs = micros();
    Esp32Base::handle();
    g_maxBaseHandleUs = std::max(g_maxBaseHandleUs, faucet::elapsedSince(micros(), baseHandleStartUs));
    maybeMarkOtaValidAfterHealthCheck();
    delay(1);
}
