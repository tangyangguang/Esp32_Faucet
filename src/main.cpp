#include <Arduino.h>
#include <Esp32Base.h>

#include "app/AppController.h"
#include "app/BeepDriver.h"
#include "app/ConfigStore.h"
#include "app/DisplayPresenter.h"
#include "app/Esp32BaseConfigBackend.h"
#include "app/Esp32BaseWaterLogBackend.h"
#include "app/FaucetAppConfig.h"
#include "app/FilterStore.h"
#include "app/StatisticsStore.h"
#include "app/TimeUtils.h"
#include "app/WaterLogFileStore.h"
#include "app/WaterLogStore.h"
#include "drivers/BoardPins.h"
#include "drivers/FlowPulseReader.h"
#include "drivers/GpioButtonReader.h"
#include "drivers/Lcd1602Display.h"
#include "drivers/PwmBeepHardware.h"
#include "drivers/PwmValveHardware.h"
#include "drivers/RtcClock.h"
#include "web/FaucetWeb.h"

#include <new>
#include <time.h>

namespace {
constexpr const char* kFirmwareName = "esp32-faucet";
constexpr const char* kFirmwareVersion = "0.1.0-dev";
constexpr const char* kDefaultWebUser = "admin";
constexpr const char* kDefaultWebPassword = "admin";
constexpr std::size_t kRamLogCapacity = 128;
constexpr std::size_t kWaterLogCapacity = 20000;
constexpr const char* kWaterLogPath = "/faucet_water.bin";
constexpr std::uint32_t kUnixSecondsAt2000 = 946684800UL;

class PersistentLogWriter : public faucet::AppLogWriter, public faucet::WaterLogReader {
public:
    PersistentLogWriter() : fileStore_(nullptr), ramStore_(records_, kRamLogCapacity) {}

    void setFileStore(faucet::WaterLogFileStore* store) {
        fileStore_ = store;
    }

    bool append(const faucet::WaterLogRecord& record) override {
        if (fileStore_ && fileStore_->ready() && fileStore_->append(record)) {
            return true;
        }
        return ramStore_.append(record);
    }

    std::size_t rewriteBootRelativeTimes(std::uint16_t bootId, std::uint32_t bootStartRealSec) {
        if (fileStore_ && fileStore_->ready()) {
            return fileStore_->rewriteBootRelativeTimes(bootId, bootStartRealSec);
        }
        return ramStore_.rewriteBootRelativeTimes(bootId, bootStartRealSec);
    }

    std::size_t readPage(std::size_t pageIndex,
                         std::uint16_t pageSize,
                         faucet::WaterLogRecord* output,
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
    faucet::WaterLogFileStore* fileStore_;
    faucet::WaterLogRecord records_[kRamLogCapacity]{};
    faucet::WaterLogStore ramStore_;
};

faucet::Esp32BaseConfigBackend g_configBackend;
faucet::ConfigStore g_configStore(g_configBackend);
faucet::SystemConfig g_config{};
faucet::StatisticsStore g_statistics;
faucet::FilterStore* g_filters = nullptr;
faucet::Esp32BaseWaterLogBackend g_waterLogBackend;
faucet::WaterLogFileStore g_waterLogFile(g_waterLogBackend, kWaterLogPath, kWaterLogCapacity);
PersistentLogWriter g_logs;
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
bool g_persistenceFailureLogged = false;
std::uint32_t g_lastDisplayMs = 0;
std::uint32_t g_lastDroppedPulsesLogged = 0;
std::uint32_t g_lastDroppedPulsesLogMs = 0;
std::uint16_t g_bootId = 0;
bool g_bootRelativeTimesRewritten = false;

struct TimeSnapshot {
    bool synced;
    std::uint32_t seconds;
    std::uint32_t uptimeSeconds;
    std::uint16_t bootId;
};

void configureBase() {
    Esp32Base::setFirmwareInfo(kFirmwareName, kFirmwareVersion, __DATE__ " " __TIME__);

#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth(kDefaultWebUser, kDefaultWebPassword);
    Esp32BaseWeb::setDeviceName("智能出水");
    Esp32BaseWeb::setHomePath("/faucet");
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

void applyFileLogPolicy() {
#if ESP32BASE_ENABLE_FILELOG
    if (!Esp32BaseFileLog::setMode(Esp32BaseFileLog::INFO)) {
        ESP32BASE_LOG_W("app", "file log INFO policy apply failed");
    }
#endif
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
    if (status == faucet::ConfigStore::LoadStatus::LoadedCurrent) {
        return;
    }
    if (status == faucet::ConfigStore::LoadStatus::LoadedFutureVersionReadOnly) {
        ESP32BASE_LOG_W("app", "system config loaded from future version in read-only mode");
    } else if (status == faucet::ConfigStore::LoadStatus::UnsupportedVersionDefault) {
        ESP32BASE_LOG_W("app", "system config version unsupported, defaults used");
    } else {
        ESP32BASE_LOG_I("app", "system config status=%s", configLoadStatusName(status));
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

faucet::PeriodKeys makeFallbackPeriodKeys(std::uint32_t nowSeconds) {
    const std::uint32_t day = nowSeconds / 86400UL;
    return faucet::PeriodKeys{day, day / 7UL, day / 31UL};
}

TimeSnapshot currentTimeSnapshot() {
    const std::uint32_t uptimeSeconds = millis() / 1000UL;
    const faucet::RtcDateTime now = g_rtc.readNow();
    if (!now.valid) {
#if ESP32BASE_ENABLE_NTP
        if (Esp32BaseNtp::isTimeSynced()) {
            const std::uint32_t timestamp = Esp32BaseNtp::timestamp();
            if (timestamp >= kUnixSecondsAt2000) {
                return TimeSnapshot{true, localSecondsFromUnix(timestamp), uptimeSeconds, g_bootId};
            }
        }
#endif
        return TimeSnapshot{false, uptimeSeconds, uptimeSeconds, g_bootId};
    }

    const std::uint32_t days = daysSince2000(now.year, now.month, now.day);
    const std::uint32_t seconds = days * 86400UL + static_cast<std::uint32_t>(now.hour) * 3600UL +
                                  static_cast<std::uint32_t>(now.minute) * 60UL + now.second;
    return TimeSnapshot{true, seconds, uptimeSeconds, g_bootId};
}

std::uint32_t currentSeconds() {
    return currentTimeSnapshot().seconds;
}

std::uint16_t currentBootId() {
    return g_bootId;
}

void applyRuntimeSettings(const faucet::SystemConfig& config) {
    g_beep.setEnabled(config.beepEnabled);
    if (g_display) {
        g_display->configure(config.displaySleepSec);
        g_display->wake(millis());
    }
    g_lcd.begin(config.lcdI2cAddress);
}

faucet::PeriodKeys currentPeriodKeys(std::uint32_t nowSeconds) {
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
        return faucet::PeriodKeys{dayKey, weekKey, monthKey};
    }
    const faucet::RtcDateTime now = g_rtc.readNow();
    if (!now.valid) {
        return makeFallbackPeriodKeys(nowSeconds);
    }

    const std::uint32_t dayKey = static_cast<std::uint32_t>(now.year) * 10000UL +
                                 static_cast<std::uint32_t>(now.month) * 100UL + now.day;
    const std::uint32_t weekKey = daysSince2000(now.year, now.month, now.day) / 7UL;
    const std::uint32_t monthKey =
        static_cast<std::uint32_t>(now.year) * 100UL + static_cast<std::uint32_t>(now.month);
    return faucet::PeriodKeys{dayKey, weekKey, monthKey};
}

void initializeApplication() {
    g_rtc.begin();
    g_bootId = g_configStore.allocateBootId();
    g_config = g_configStore.loadSystemConfig();
    logSystemConfigStatus();
    g_logs.setFileStore(g_waterLogFile.begin() ? &g_waterLogFile : nullptr);
    checkFileSystemCapacity();
    const TimeSnapshot time = currentTimeSnapshot();
    g_statistics = faucet::StatisticsStore(g_configStore.loadStatistics(currentPeriodKeys(time.seconds)));
    g_configStore.loadFilterRuntime(g_config.filters);
    g_filters = new (std::nothrow) faucet::FilterStore(g_config.filters);
    if (!g_filters) {
        ESP32BASE_LOG_E("app", "filter store allocation failed");
        return;
    }
    g_app = new (std::nothrow) faucet::AppController(g_config, g_statistics, *g_filters, g_logs);
    if (!g_app) {
        ESP32BASE_LOG_E("app", "app controller allocation failed");
        return;
    }
    g_display = new (std::nothrow) faucet::DisplayPresenter(g_config.displaySleepSec);
    if (!g_display) {
        ESP32BASE_LOG_W("app", "display presenter allocation failed, lcd disabled");
    }
    faucet::setFaucetWebContext(
        faucet::FaucetWebContext{&g_config, &g_configStore, g_app, g_filters, &g_logs, currentSeconds, currentBootId, applyRuntimeSettings});
    faucet::setFaucetAppConfigContext(
        faucet::FaucetAppConfigContext{&g_config, &g_configStore, g_app, applyRuntimeSettings});

    g_buttons.begin();
    g_flowPulses.begin();
    g_valveHardware.begin();
    g_beep.setEnabled(g_config.beepEnabled);
    g_beepHardware.begin();
    g_lcd.begin(g_config.lcdI2cAddress);

    g_app->resetInputs(g_buttons.read(), millis());
    if (g_display) {
        g_display->wake(millis());
    }

    ESP32BASE_LOG_I("app",
                    "application initialized rtc=%s lcd=%s log=%s",
                    g_rtc.present() ? "present" : "absent",
                    g_lcd.present() ? "present" : "absent",
                    g_waterLogFile.ready() ? "file" : "ram");
}

void rewriteCurrentBootRelativeTimes(const TimeSnapshot& time) {
    if (!time.synced || g_bootRelativeTimesRewritten || time.bootId == 0 || time.seconds < time.uptimeSeconds) {
        return;
    }
    const std::uint32_t bootStartRealSec = time.seconds - time.uptimeSeconds;
    const std::size_t logCount = g_logs.rewriteBootRelativeTimes(time.bootId, bootStartRealSec);
    bool filtersChanged = false;
    if (g_filters) {
        for (std::size_t i = 0; i < faucet::kFilterCount; ++i) {
            faucet::FilterRecord record = g_filters->record(i);
            if (record.startBootId == time.bootId && record.startTime > 0 &&
                record.startTime < faucet::kMinRealDateSeconds) {
                record.startTime = bootStartRealSec + record.startTime;
                record.startBootId = 0;
                g_filters->updateFilter(i, record);
                filtersChanged = true;
            }
        }
    }
    if (filtersChanged && !g_configStore.saveFilterRuntime(g_filters->records())) {
        ESP32BASE_LOG_E("app", "filter time correction persistence failed");
    }
    if (logCount > 0 || filtersChanged) {
        ESP32BASE_LOG_I("app",
                        "boot_relative_times_rewritten boot_id=%u logs=%lu filters=%s",
                        static_cast<unsigned>(time.bootId),
                        static_cast<unsigned long>(logCount),
                        filtersChanged ? "yes" : "no");
    }
    g_bootRelativeTimesRewritten = true;
}

void runApplicationTick() {
    if (!g_app) {
        return;
    }

    const std::uint32_t nowMs = millis();
    const std::uint32_t nowUs = micros();
    const TimeSnapshot time = currentTimeSnapshot();
    rewriteCurrentBootRelativeTimes(time);
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

    const faucet::AppTickInput input{
        levels,
        nowMs,
        nowUs,
        time.seconds,
        currentPeriodKeys(time.seconds),
        time.synced,
        time.bootId,
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
            g_lcd.apply(g_display->render(snapshot, nowMs));
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

    if (g_app->consumeFactoryResetRequest()) {
        ESP32BASE_LOG_W("app", "factory reset requested from local buttons");
        g_valveHardware.apply(faucet::ValveOutput{faucet::ValveState::Closed, false, 0});
        g_configStore.resetSystemConfig();
        g_configStore.resetStatistics(currentPeriodKeys(time.seconds));
        g_configStore.resetFilterRuntime();
        g_waterLogFile.clear();
        delay(200);
        ESP.restart();
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
    applyFileLogPolicy();
    Serial.println("[faucet] setup: base begin done");
    initializeApplication();
    Serial.println("[faucet] setup: app initialized");
}

void loop() {
    runApplicationTick();
    Esp32Base::handle();
    delay(1);
}
