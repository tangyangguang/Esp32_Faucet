#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

#include "app/AppController.h"
#include "app/BeepDriver.h"
#include "app/ConfigStore.h"
#include "app/DisplayPresenter.h"
#include "app/Esp32BaseConfigBackend.h"
#include "app/Esp32BaseWaterRecordBackend.h"
#include "app/FaucetAppConfig.h"
#include "app/FilterStore.h"
#include "app/MeteringSchemeStore.h"
#include "app/StatisticsStore.h"
#include "app/TimeUtils.h"
#include "app/WaterRecordCalibrationStore.h"
#include "app/WaterRecordFileStore.h"
#include "app/WaterRecordMeteringSnapshotStore.h"
#include "app/WaterRecordStore.h"
#include "app/WaterPulseTraceStore.h"
#include "drivers/BoardPins.h"
#include "drivers/FlowPulseReader.h"
#include "drivers/GpioButtonReader.h"
#include "drivers/Lcd1602Display.h"
#include "drivers/PwmBeepHardware.h"
#include "drivers/PwmValveHardware.h"
#include "drivers/RtcClock.h"
#include "web/FaucetWeb.h"

#include <algorithm>
#include <new>
#include <time.h>

namespace {
constexpr const char* kFirmwareName = "esp32-faucet";
constexpr const char* kFirmwareVersion = "0.1.0-dev";
constexpr const char* kDefaultWebUser = "admin";
constexpr const char* kDefaultWebPassword = "admin";
constexpr std::size_t kRamRecordCapacity = 128;
constexpr std::size_t kRamRecordCalibrationCapacity = 32;
constexpr std::size_t kRamRecordMeteringSnapshotCapacity = 32;
constexpr std::size_t kWaterRecordCapacity = 20000;
constexpr std::size_t kWaterRecordCalibrationCapacity = 512;
constexpr std::size_t kWaterRecordMeteringSnapshotCapacity = kWaterRecordCapacity;
constexpr std::size_t kPulseTraceCapacity = faucet::kMaxRecentPulseTraceCount;
constexpr std::size_t kSavedPulseTraceMaxCount = faucet::kSavedPulseTraceMaxCount;
constexpr std::size_t kSavedPulseTraceSamplesPerTrace = faucet::kPulseTraceMaxRawEdgesPerTrace;
constexpr std::size_t kPulseTraceMaxSamples =
    static_cast<std::size_t>(faucet::kMaxRecentPulseTraceCount) * faucet::kPulseTraceMaxRawEdgesPerTrace;
constexpr const char* kWaterRecordPath = "/faucet_records_v1.bin";
constexpr const char* kWaterRecordCalibrationPath = "/faucet_record_cal_v1.bin";
constexpr const char* kWaterRecordMeteringSnapshotPath = "/faucet_record_metering_v1.bin";
constexpr const char* kMeteringSchemePath = "/faucet_metering_schemes_v1.bin";
constexpr const char* kSavedPulseTracePath = "/faucet_pulse_traces_v4.bin";

class PersistentRecordWriter : public faucet::WaterRecordWriter, public faucet::WaterRecordReader {
public:
    PersistentRecordWriter() : fileStore_(nullptr), ramStore_(records_, kRamRecordCapacity) {}

    void setFileStore(faucet::WaterRecordFileStore* store) {
        fileStore_ = store;
    }

    bool append(const faucet::WaterRecord& record) override {
        if (fileStore_ && fileStore_->ready() && fileStore_->append(record)) {
            return true;
        }
        return ramStore_.append(record);
    }

    std::size_t rewriteBootRelativeTimes(std::uint32_t bootId, std::uint32_t bootStartRealSec) {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->rewriteBootRelativeTimes(bootId, bootStartRealSec);
        }
        return ramStore_.rewriteBootRelativeTimes(bootId, bootStartRealSec);
    }

    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         faucet::WaterRecord* output,
                         std::size_t outputCapacity) const override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->readPage(pageIndex, pageSize, output, outputCapacity);
        }
        return ramStore_.readPage(pageIndex, pageSize, output, outputCapacity);
    }

    std::size_t count() const override {
        return fileStore_ && fileStore_->ready() ? fileStore_->count() : ramStore_.count();
    }

    bool ready() const override {
        return (fileStore_ && fileStore_->ready()) || ramStore_.ready();
    }

    const char* storageName() const override {
        return fileStore_ && fileStore_->ready() ? fileStore_->storageName() : ramStore_.storageName();
    }

private:
    faucet::WaterRecordFileStore* fileStore_;
    faucet::WaterRecord records_[kRamRecordCapacity]{};
    faucet::WaterRecordStore ramStore_;
};

class PersistentRecordCalibrationStore : public faucet::WaterRecordCalibrationReader,
                                         public faucet::WaterRecordCalibrationWriter {
public:
    PersistentRecordCalibrationStore()
        : fileStore_(nullptr), ramStore_(calibrations_, kRamRecordCalibrationCapacity) {}

    void setFileStore(faucet::WaterRecordCalibrationFileStore* store) {
        fileStore_ = store;
    }

    bool upsert(const faucet::WaterRecordCalibration& calibration) override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->upsert(calibration);
        }
        return ramStore_.upsert(calibration);
    }

    bool find(const faucet::WaterRecord& record, faucet::WaterRecordCalibration& output) const override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->find(record, output);
        }
        return ramStore_.find(record, output);
    }

    std::size_t findAny(const faucet::WaterRecord* records,
                        std::size_t recordCount,
                        faucet::WaterRecordCalibration* output,
                        bool* found) const override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->findAny(records, recordCount, output, found);
        }
        return ramStore_.findAny(records, recordCount, output, found);
    }

    std::size_t count() const override {
        return fileStore_ && fileStore_->ready() ? fileStore_->count() : ramStore_.count();
    }

    bool ready() const override {
        return (fileStore_ && fileStore_->ready()) || ramStore_.ready();
    }

    const char* storageName() const override {
        return fileStore_ && fileStore_->ready() ? fileStore_->storageName() : ramStore_.storageName();
    }

private:
    faucet::WaterRecordCalibrationFileStore* fileStore_;
    faucet::WaterRecordCalibration calibrations_[kRamRecordCalibrationCapacity]{};
    faucet::WaterRecordCalibrationStore ramStore_;
};

class PersistentRecordMeteringSnapshotStore : public faucet::WaterRecordMeteringSnapshotReader,
                                             public faucet::WaterRecordMeteringSnapshotWriter {
public:
    PersistentRecordMeteringSnapshotStore()
        : fileStore_(nullptr), ramStore_(snapshots_, kRamRecordMeteringSnapshotCapacity) {}

    void setFileStore(faucet::WaterRecordMeteringSnapshotFileStore* store) {
        fileStore_ = store;
    }

    bool upsert(const faucet::WaterRecordMeteringSnapshot& snapshot) override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->upsert(snapshot);
        }
        return ramStore_.upsert(snapshot);
    }

    bool find(const faucet::WaterRecord& record, faucet::WaterRecordMeteringSnapshot& output) const override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->find(record, output);
        }
        return ramStore_.find(record, output);
    }

    std::size_t findAny(const faucet::WaterRecord* records,
                        std::size_t recordCount,
                        faucet::WaterRecordMeteringSnapshot* output,
                        bool* found) const override {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->findAny(records, recordCount, output, found);
        }
        return ramStore_.findAny(records, recordCount, output, found);
    }

    std::size_t count() const override {
        return fileStore_ && fileStore_->ready() ? fileStore_->count() : ramStore_.count();
    }

    bool ready() const override {
        return (fileStore_ && fileStore_->ready()) || ramStore_.ready();
    }

    const char* storageName() const override {
        return fileStore_ && fileStore_->ready() ? fileStore_->storageName() : ramStore_.storageName();
    }

private:
    faucet::WaterRecordMeteringSnapshotFileStore* fileStore_;
    faucet::WaterRecordMeteringSnapshot snapshots_[kRamRecordMeteringSnapshotCapacity]{};
    faucet::WaterRecordMeteringSnapshotStore ramStore_;
};

faucet::Esp32BaseConfigBackend g_configBackend;
faucet::ConfigStore g_configStore(g_configBackend);
faucet::SystemConfig g_config{};
faucet::StatisticsStore g_statistics;
faucet::FilterStore* g_filters = nullptr;
faucet::Esp32BaseWaterRecordBackend g_waterRecordBackend;
faucet::WaterRecordFileStore g_waterRecordFile(g_waterRecordBackend, kWaterRecordPath, kWaterRecordCapacity);
faucet::WaterRecordCalibrationFileStore g_recordCalibrationFile(
    g_waterRecordBackend,
    kWaterRecordCalibrationPath,
    kWaterRecordCalibrationCapacity);
faucet::WaterRecordMeteringSnapshotFileStore g_recordMeteringSnapshotFile(
    g_waterRecordBackend,
    kWaterRecordMeteringSnapshotPath,
    kWaterRecordMeteringSnapshotCapacity);
faucet::MeteringSchemeStore g_meteringSchemes(g_waterRecordBackend, kMeteringSchemePath);
faucet::WaterPulseTraceFileStore g_savedPulseTraceFile(
    g_waterRecordBackend,
    kSavedPulseTracePath,
    kSavedPulseTraceSamplesPerTrace,
    kSavedPulseTraceMaxCount);
PersistentRecordWriter g_records;
PersistentRecordCalibrationStore g_recordCalibrations;
PersistentRecordMeteringSnapshotStore g_recordMeteringSnapshots;
faucet::WaterPulseTrace* g_pulseTraceRecords = nullptr;
faucet::WaterPulseTraceSample* g_pulseTraceSamples = nullptr;
faucet::WaterPulseTraceStore* g_pulseTraces = nullptr;
faucet::AppController* g_app = nullptr;
faucet::GpioButtonReader g_buttons(faucet::kPinButtonCancel,
                                   faucet::kPinButtonOk,
                                   faucet::kPinButtonPlus,
                                   faucet::kPinButtonMinus);
faucet::FlowPulseReader g_flowPulses(faucet::kPinFlow);
faucet::PwmValveHardware g_valveHardware(faucet::kPinValve, faucet::kLedcChannelValve);
faucet::BeepDriver g_beep;
faucet::PwmBeepHardware g_beepHardware(faucet::kPinBeep, faucet::kLedcChannelBeep);
faucet::RtcClock g_rtc(faucet::kPinI2cSda, faucet::kPinI2cScl);
faucet::Lcd1602Display g_lcd(faucet::kPinI2cSda, faucet::kPinI2cScl);
faucet::DisplayPresenter* g_display = nullptr;
faucet::DisplayFrame g_lastDisplayFrame{faucet::DisplayPage::Sleep, false, {}, {}};
bool g_persistenceFailureLogged = false;
std::uint32_t g_lastDisplayMs = 0;
std::uint32_t g_lastDroppedPulsesLogged = 0;
std::uint32_t g_lastDroppedPulsesLogMs = 0;
bool g_bootRelativeTimesRewritten = false;
std::uint32_t g_lastLoopStartUs = 0;
std::uint32_t g_maxLoopIntervalUs = 0;
std::uint32_t g_maxAppTickUs = 0;
std::uint32_t g_maxBaseHandleUs = 0;

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
}

const char* configLoadStatusName(faucet::ConfigStore::LoadStatus status) {
    switch (status) {
        case faucet::ConfigStore::LoadStatus::DefaultsNoVersion:
            return "defaults_no_version";
        case faucet::ConfigStore::LoadStatus::LoadedCurrent:
            return "loaded_current";
        case faucet::ConfigStore::LoadStatus::MigratedLegacy:
            return "migrated_legacy";
        case faucet::ConfigStore::LoadStatus::LoadedFutureVersionReadOnly:
            return "future_version_read_only";
        case faucet::ConfigStore::LoadStatus::UnsupportedVersionDefault:
            return "unsupported_version_default";
    }
    return "unknown";
}

void logSystemConfigStatus() {
    const faucet::ConfigStore::LoadStatus status = g_configStore.lastSystemConfigLoadStatus();
    ESP32BASE_LOG_I("app",
                    "system config load status=%s raw_version=%ld target_version=%ld migrated_writeback=%s read_only=%s",
                    configLoadStatusName(status),
                    static_cast<long>(g_configStore.lastSystemConfigRawVersion()),
                    static_cast<long>(g_configStore.currentSystemConfigVersion()),
                    g_configStore.lastSystemConfigMigrationWriteBack() ? "yes" : "no",
                    g_configStore.systemConfigReadOnly() ? "yes" : "no");
    if (status == faucet::ConfigStore::LoadStatus::LoadedFutureVersionReadOnly) {
        ESP32BASE_LOG_W("app", "system config loaded from future version in read-only mode");
    } else if (status == faucet::ConfigStore::LoadStatus::UnsupportedVersionDefault) {
        ESP32BASE_LOG_W("app", "system config version unsupported, defaults used");
    }
}

void checkFileSystemCapacity() {
#if ESP32BASE_ENABLE_FS
    if (!Esp32BaseFs::isReady()) {
        return;
    }
    constexpr std::size_t kLowFreeBytes = 100UL * 1024UL;
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

bool isLeapYear(std::uint16_t year) {
    return (year % 4U == 0 && year % 100U != 0) || year % 400U == 0;
}

std::uint16_t dayOfYear(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    static constexpr std::uint16_t kDaysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    std::uint16_t value = kDaysBeforeMonth[month > 0 ? month - 1 : 0] + day - 1U;
    if (month > 2 && isLeapYear(year)) {
        ++value;
    }
    return value;
}

std::uint32_t daysSince2000(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
    std::uint32_t days = 0;
    for (std::uint16_t y = 2000; y < year; ++y) {
        days += isLeapYear(y) ? 366UL : 365UL;
    }
    return days + dayOfYear(year, month, day);
}

std::uint32_t localSecondsFromUnix(std::uint32_t timestamp) {
    time_t value = static_cast<time_t>(timestamp);
    struct tm localTime {};
    localtime_r(&value, &localTime);
    const std::uint16_t year = static_cast<std::uint16_t>(localTime.tm_year + 1900);
    const std::uint8_t month = static_cast<std::uint8_t>(localTime.tm_mon + 1);
    const std::uint8_t day = static_cast<std::uint8_t>(localTime.tm_mday);
    return daysSince2000(year, month, day) * 86400UL + static_cast<std::uint32_t>(localTime.tm_hour) * 3600UL +
           static_cast<std::uint32_t>(localTime.tm_min) * 60UL + static_cast<std::uint32_t>(localTime.tm_sec);
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
    faucet::DisplayFrame physicalFrame = g_lastDisplayFrame;
    faucet::DisplayFrame logicalFrame = g_lastDisplayFrame;
    if (g_display && g_app) {
        const std::uint32_t nowMs = millis();
        const faucet::AppSnapshot snapshot = g_app->snapshot();
        physicalFrame = g_display->render(snapshot, nowMs);
        g_lastDisplayFrame = physicalFrame;
        faucet::DisplayPresenter awakePresenter(0);
        awakePresenter.wake(nowMs);
        logicalFrame = awakePresenter.render(snapshot, nowMs);
    }
    return faucet::FaucetDisplayStatus{logicalFrame, physicalFrame.on};
}

faucet::FaucetRuntimeDiagnostics currentRuntimeDiagnostics() {
    return faucet::FaucetRuntimeDiagnostics{g_maxLoopIntervalUs, g_maxAppTickUs, g_maxBaseHandleUs};
}

void applyRuntimeSettings(const faucet::SystemConfig& config) {
    g_beep.setEnabled(config.beepEnabled);
    if (g_display) {
        g_display->configure(config.displaySleepSec);
        g_display->wake(millis());
    }
    g_lcd.begin(config.lcdI2cAddress);
}

bool currentPeriodKeys(std::uint32_t nowSeconds, faucet::PeriodKeys& keys) {
    if (nowSeconds >= faucet::kMinRealDateSeconds) {
        std::uint32_t day = nowSeconds / 86400UL;
        std::uint16_t year = 2000;
        while (true) {
            const std::uint16_t yearDays = isLeapYear(year) ? 366 : 365;
            if (day < yearDays) {
                break;
            }
            day -= yearDays;
            ++year;
        }
        std::uint8_t month = 1;
        static constexpr std::uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        while (month <= 12) {
            std::uint8_t monthDays = kMonthDays[month - 1];
            if (month == 2 && isLeapYear(year)) {
                monthDays = 29;
            }
            if (day < monthDays) {
                break;
            }
            day -= monthDays;
            ++month;
        }
        const std::uint8_t monthDay = static_cast<std::uint8_t>(day + 1);
        const std::uint32_t dayKey =
            static_cast<std::uint32_t>(year) * 10000UL + static_cast<std::uint32_t>(month) * 100UL + monthDay;
        const std::uint32_t weekKey = nowSeconds / 86400UL / 7UL;
        const std::uint32_t monthKey = static_cast<std::uint32_t>(year) * 100UL + month;
        keys = faucet::PeriodKeys{dayKey, weekKey, monthKey};
        return true;
    }
    const faucet::RtcDateTime now = g_rtc.readNow();
    if (!now.valid) {
        keys = faucet::PeriodKeys{0, 0, 0};
        return false;
    }
    const std::uint32_t rtcSeconds =
        daysSince2000(now.year, now.month, now.day) * 86400UL + static_cast<std::uint32_t>(now.hour) * 3600UL +
        static_cast<std::uint32_t>(now.minute) * 60UL + static_cast<std::uint32_t>(now.second);
    if (rtcSeconds < faucet::kMinRealDateSeconds) {
        keys = faucet::PeriodKeys{0, 0, 0};
        return false;
    }

    const std::uint32_t dayKey = static_cast<std::uint32_t>(now.year) * 10000UL +
                                 static_cast<std::uint32_t>(now.month) * 100UL + now.day;
    const std::uint32_t weekKey = daysSince2000(now.year, now.month, now.day) / 7UL;
    const std::uint32_t monthKey =
        static_cast<std::uint32_t>(now.year) * 100UL + static_cast<std::uint32_t>(now.month);
    keys = faucet::PeriodKeys{dayKey, weekKey, monthKey};
    return true;
}

void handleTimeSynced(const Esp32BaseNtp::TimeSnapshot& time);

void initializeApplication() {
    logStartupPhase("app_init_start");
    initializeI2cBus();
    logStartupPhase("i2c_ready");
    g_rtc.begin();
    logStartupPhase("rtc_ready");
    g_config = g_configStore.loadSystemConfig();
    logSystemConfigStatus();
    logStartupPhase("config_ready");
    const std::uint32_t nowSeconds = currentSeconds();
    g_records.setFileStore(g_waterRecordFile.begin() ? &g_waterRecordFile : nullptr);
    g_recordCalibrations.setFileStore(g_recordCalibrationFile.begin() ? &g_recordCalibrationFile : nullptr);
    g_recordMeteringSnapshots.setFileStore(
        g_recordMeteringSnapshotFile.begin() ? &g_recordMeteringSnapshotFile : nullptr);
    const bool meteringSchemesReady = g_meteringSchemes.begin();
    if (!meteringSchemesReady) {
        ESP32BASE_LOG_W("app", "metering scheme store unavailable, using config fallback");
    } else if (!g_meteringSchemes.migrateLegacyFromConfig(g_configBackend, nowSeconds)) {
        ESP32BASE_LOG_W("app", "legacy metering scheme migration failed");
    }
    logStartupPhase("record_store_ready");
    g_pulseTraceRecords = new (std::nothrow) faucet::WaterPulseTrace[kPulseTraceCapacity]{};
    g_pulseTraceSamples = new (std::nothrow) faucet::WaterPulseTraceSample[kPulseTraceMaxSamples]{};
    if (g_pulseTraceRecords && g_pulseTraceSamples) {
        g_pulseTraces = new (std::nothrow) faucet::WaterPulseTraceStore(
            g_pulseTraceRecords,
            kPulseTraceCapacity,
            g_pulseTraceSamples,
            kPulseTraceMaxSamples,
            g_config.recentPulseTraceCount);
    }
    if (!g_pulseTraces) {
        ESP32BASE_LOG_W("app", "pulse trace store allocation failed, trace diagnostics disabled");
    }
    logStartupPhase("trace_store_ready");
    checkFileSystemCapacity();
    faucet::PeriodKeys periodKeys{};
    currentPeriodKeys(nowSeconds, periodKeys);
    g_statistics = faucet::StatisticsStore(g_configStore.loadStatistics(periodKeys));
    g_configStore.loadFilterRuntime(g_config.filters);
    logStartupPhase("runtime_state_ready");
    g_filters = new (std::nothrow) faucet::FilterStore(g_config.filters);
    if (!g_filters) {
        ESP32BASE_LOG_E("app", "filter store allocation failed");
        return;
    }
    if (g_pulseTraces) {
        g_pulseTraces->setRecentTraceLimit(g_config.recentPulseTraceCount);
    }
    faucet::MeteringSchemeRecord activeScheme{};
    if (meteringSchemesReady && g_meteringSchemes.activeScheme(activeScheme)) {
        g_app = new (std::nothrow) faucet::AppController(
            g_config, activeScheme,
            g_statistics,
            *g_filters,
            g_records,
            g_recordMeteringSnapshots,
            g_meteringSchemes,
            g_pulseTraces,
            &g_recordCalibrations);
    } else {
        g_app = new (std::nothrow) faucet::AppController(
            g_config, g_statistics, *g_filters, g_records, g_pulseTraces, &g_recordCalibrations);
    }
    if (!g_app) {
        ESP32BASE_LOG_E("app", "app controller allocation failed");
        return;
    }
    g_display = new (std::nothrow) faucet::DisplayPresenter(g_config.displaySleepSec);
    if (!g_display) {
        ESP32BASE_LOG_W("app", "display presenter allocation failed, lcd disabled");
    }
    faucet::setFaucetWebContext(
        faucet::FaucetWebContext{&g_config,
                                 &g_configStore,
                                 g_app,
                                 g_filters,
                                 &g_records,
                                 &g_recordCalibrations,
                                 &g_recordCalibrations,
                                 &g_recordMeteringSnapshots,
                                 &g_recordMeteringSnapshots,
                                 &g_meteringSchemes,
                                 g_pulseTraces,
                                 &g_savedPulseTraceFile,
                                 currentSeconds,
                                 currentBootId,
                                 applyRuntimeSettings,
                                 currentDisplayStatus,
                                 currentRuntimeDiagnostics});
    faucet::setFaucetAppConfigContext(
        faucet::FaucetAppConfigContext{&g_config, &g_configStore, g_app, applyRuntimeSettings});
#if ESP32BASE_ENABLE_NTP
    Esp32BaseNtp::onTimeSynced(handleTimeSynced);
#endif
    logStartupPhase("context_ready");

    g_buttons.begin();
    g_flowPulses.begin();
    g_valveHardware.begin();
    g_beep.setEnabled(g_config.beepEnabled);
    g_beepHardware.begin();
    g_lcd.begin(g_config.lcdI2cAddress);
    logStartupPhase("hardware_ready");

    g_app->resetInputs(g_buttons.read(), millis());
    if (g_display) {
        g_display->wake(millis());
        g_lastDisplayFrame = g_display->render(g_app->snapshot(), millis());
    }
    logStartupPhase("display_ready");

    ESP32BASE_LOG_I("app",
                    "application initialized rtc=%s lcd=%s records=%s",
                    g_rtc.present() ? "present" : "absent",
                    g_lcd.present() ? "present" : "absent",
                    g_waterRecordFile.ready() ? "file" : "ram");
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
    const std::size_t recordCount = g_records.rewriteBootRelativeTimes(time.bootId, bootStartRealSec);
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
    if (filtersChanged && !g_configStore.saveFilterRuntime(g_filters->records())) {
        ESP32BASE_LOG_E("app", "filter time correction persistence failed");
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

    const std::uint32_t nowMs = millis();
    const std::uint32_t nowUs = micros();
    const faucet::ButtonLevels levels = g_buttons.read();
    if (g_buttons.consumeCancelInterrupt() || levels.cancelPressed) {
        g_app->emergencyStop(nowMs);
        g_valveHardware.apply(g_app->snapshot().valve);
    }

    std::uint32_t pulseUs = 0;
    while (g_flowPulses.pop(pulseUs)) {
        g_app->onFlowPulse(pulseUs);
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
    const faucet::BeepPattern beep = g_app->consumeBeepPattern();
    if (beep != faucet::BeepPattern::None) {
        g_beep.play(beep, nowMs);
    }

    const faucet::AppSnapshot snapshot = g_app->snapshot();
    g_valveHardware.apply(snapshot.valve);

    if (g_display) {
        if (levels.cancelPressed || levels.okPressed || levels.plusPressed || levels.minusPressed ||
            snapshot.water.state != faucet::WaterState::Idle || snapshot.localMode != faucet::LocalUiMode::Normal) {
            g_display->wake(nowMs);
        }
        if (faucet::elapsedAtLeast(nowMs, g_lastDisplayMs, 200UL)) {
            g_lastDisplayFrame = g_display->render(snapshot, nowMs);
            g_lcd.apply(g_lastDisplayFrame);
            g_lastDisplayMs = nowMs;
        }
    }

    if (g_app->consumePersistenceDirty() && g_filters) {
        const bool ok = g_configStore.saveStatistics(g_statistics.record()) &&
                        g_configStore.saveFilterRuntime(g_filters->records());
        if (!ok && !g_persistenceFailureLogged) {
            ESP32BASE_LOG_E("app", "runtime persistence failed");
            g_persistenceFailureLogged = true;
        } else if (ok) {
            g_persistenceFailureLogged = false;
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
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[faucet] setup: start");
    configureBase();
    Serial.println("[faucet] setup: base configured");

    if (!Esp32Base::begin()) {
        ESP32BASE_LOG_E("app", "Esp32Base begin failed: %s", Esp32Base::lastError());
    }
    Serial.println("[faucet] setup: base begin done");
    initializeApplication();
    Serial.println("[faucet] setup: app initialized");
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
    delay(1);
}
