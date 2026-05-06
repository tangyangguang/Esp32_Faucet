#include <Arduino.h>
#include <Esp32Base.h>

#include "app/AppController.h"
#include "app/BeepDriver.h"
#include "app/ConfigStore.h"
#include "app/DisplayPresenter.h"
#include "app/Esp32BaseConfigBackend.h"
#include "app/Esp32BaseWaterLogBackend.h"
#include "app/FilterStore.h"
#include "app/StatisticsStore.h"
#include "app/WaterLogFileStore.h"
#include "app/WaterLogStore.h"
#include "drivers/BoardPins.h"
#include "drivers/FlowPulseReader.h"
#include "drivers/GpioButtonReader.h"
#include "drivers/OledDisplay.h"
#include "drivers/PwmBeepHardware.h"
#include "drivers/PwmValveHardware.h"
#include "drivers/RtcClock.h"
#include "web/FaucetWeb.h"

namespace {
constexpr const char* kFirmwareName = "esp32-faucet";
constexpr const char* kFirmwareVersion = "0.1.0-dev";
constexpr const char* kDefaultWebUser = "admin";
constexpr const char* kDefaultWebPassword = "admin";
constexpr std::size_t kRamLogCapacity = 128;
constexpr std::size_t kWaterLogCapacity = 20000;
constexpr const char* kWaterLogPath = "/faucet_water.bin";
constexpr std::uint32_t kUnixSecondsAt2000 = 946684800UL;

char g_hostname[17] = "water-0000";

class PersistentLogWriter : public faucet::AppLogWriter {
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
faucet::GpioButtonReader g_buttons(faucet::kPinButtonStop, faucet::kPinButtonOk, faucet::kPinButtonNext);
faucet::FlowPulseReader g_flowPulses(faucet::kPinFlow);
faucet::PwmValveHardware g_valveHardware(faucet::kPinValve, faucet::kLedcChannelValve);
faucet::BeepDriver g_beep;
faucet::PwmBeepHardware g_beepHardware(faucet::kPinBeep, faucet::kLedcChannelBeep);
faucet::RtcClock g_rtc(faucet::kPinI2cSda, faucet::kPinI2cScl);
faucet::OledDisplay g_oled(faucet::kPinI2cSda, faucet::kPinI2cScl);
faucet::DisplayPresenter* g_display = nullptr;
bool g_persistenceFailureLogged = false;
std::uint32_t g_lastDisplayMs = 0;

void buildDefaultHostname() {
    const uint64_t mac = ESP.getEfuseMac();
    const uint16_t suffix = static_cast<uint16_t>(mac & 0xFFFF);
    snprintf(g_hostname, sizeof(g_hostname), "water-%04x", suffix);
}

void configureBase() {
    buildDefaultHostname();

    Esp32Base::setFirmwareInfo(kFirmwareName, kFirmwareVersion, __DATE__ " " __TIME__);
    Esp32Base::setHostname(g_hostname);

#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setAuth(kDefaultWebUser, kDefaultWebPassword);
    Esp32BaseWeb::setDeviceName("智能出水");
    Esp32BaseWeb::setHomePath("/faucet");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "系统");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "网络");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_OTA, "OTA");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_LOGS, "系统日志");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_REBOOT, "重启");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_SYSTEM, "系统工具");
    if (!faucet::registerFaucetWeb()) {
        ESP32BASE_LOG_E("app", "faucet web route registration failed");
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

faucet::PeriodKeys makeFallbackPeriodKeys(std::uint32_t nowSeconds) {
    const std::uint32_t day = nowSeconds / 86400UL;
    return faucet::PeriodKeys{day, day / 7UL, day / 31UL};
}

std::uint32_t currentSeconds() {
    const faucet::RtcDateTime now = g_rtc.readNow();
    if (!now.valid) {
#if ESP32BASE_ENABLE_NTP
        if (Esp32BaseNtp::isTimeSynced()) {
            const std::uint32_t timestamp = Esp32BaseNtp::timestamp();
            if (timestamp >= kUnixSecondsAt2000) {
                return timestamp - kUnixSecondsAt2000;
            }
        }
#endif
        return millis() / 1000UL;
    }

    const std::uint32_t days = daysSince2000(now.year, now.month, now.day);
    return days * 86400UL + static_cast<std::uint32_t>(now.hour) * 3600UL +
           static_cast<std::uint32_t>(now.minute) * 60UL + now.second;
}

faucet::PeriodKeys currentPeriodKeys(std::uint32_t nowSeconds) {
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
    g_config = g_configStore.loadSystemConfig();
    g_logs.setFileStore(g_waterLogFile.begin() ? &g_waterLogFile : nullptr);
    const std::uint32_t nowSec = currentSeconds();
    g_statistics = faucet::StatisticsStore(g_configStore.loadStatistics(currentPeriodKeys(nowSec)));
    g_configStore.loadFilterRuntime(g_config.filters);
    g_filters = new faucet::FilterStore(g_config.filters);
    g_app = new faucet::AppController(g_config, g_statistics, *g_filters, g_logs);
    g_display = new faucet::DisplayPresenter(g_config.oledSleepSec);
    faucet::setFaucetWebContext(
        faucet::FaucetWebContext{&g_config, &g_configStore, g_app, g_filters, &g_waterLogFile, currentSeconds});

    g_buttons.begin();
    g_flowPulses.begin();
    g_valveHardware.begin();
    g_beep.setEnabled(g_config.beepEnabled);
    g_beepHardware.begin();
    g_oled.begin();

    g_app->resetInputs(g_buttons.read(), millis());
    if (g_display) {
        g_display->wake(millis());
    }

    ESP32BASE_LOG_I("app",
                    "application initialized rtc=%s oled=%s log=%s",
                    g_rtc.present() ? "present" : "absent",
                    g_oled.present() ? "present" : "absent",
                    g_waterLogFile.ready() ? "file" : "ram");
}

void runApplicationTick() {
    if (!g_app) {
        return;
    }

    std::uint32_t pulseUs = 0;
    while (g_flowPulses.pop(pulseUs)) {
        g_app->onFlowPulse(pulseUs);
    }

    const std::uint32_t nowMs = millis();
    const std::uint32_t nowUs = micros();
    const std::uint32_t nowSec = currentSeconds();
    const faucet::ButtonLevels levels = g_buttons.read();
    const faucet::AppTickInput input{
        levels,
        nowMs,
        nowUs,
        nowSec,
        currentPeriodKeys(nowSec),
    };
    g_app->tick(input);

    const faucet::AppSnapshot snapshot = g_app->snapshot();
    g_valveHardware.apply(snapshot.valve);

    if (g_display) {
        if (levels.stopPressed || levels.okPressed || levels.nextPressed || snapshot.water.state != faucet::WaterState::Idle) {
            g_display->wake(nowMs);
        }
        if (nowMs - g_lastDisplayMs >= 200UL) {
            g_oled.apply(g_display->render(snapshot, nowMs));
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
        g_configStore.resetStatistics(currentPeriodKeys(nowSec));
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
    Serial.println("[faucet] setup: base begin done");
    initializeApplication();
    Serial.println("[faucet] setup: app initialized");
}

void loop() {
    runApplicationTick();
    Esp32Base::handle();
    delay(1);
}
