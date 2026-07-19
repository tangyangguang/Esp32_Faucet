#include "app/ConfigStore.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace faucet {
namespace {

constexpr const char* kConfigNs = "faucet_cfg";
constexpr const char* kStatNs = "faucet_stat";
constexpr const char* kRunNs = "faucet_run";
constexpr std::int32_t kConfigVersion = 22;
constexpr std::int32_t kRuntimeVersion = 1;
constexpr const char* kSensorNone = "none";
constexpr const char* kTemperatureSensorNtcBeta = "ntc_beta";
constexpr const char* kTdsSensorAnalogAo = "tds_board_v1";

std::int32_t toInt(std::uint32_t value) {
    return value > static_cast<std::uint32_t>(INT32_MAX) ? INT32_MAX : static_cast<std::int32_t>(value);
}

bool okAll(bool current, bool next) {
    return current && next;
}

bool setU32(ConfigBackend& backend, const char* ns, const char* key, std::uint32_t value) {
    char text[11]{};
    std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    return backend.setStr(ns, key, text);
}

bool readStrKey(ConfigBackend& backend, const char* ns, const char* key, char* out, std::size_t len) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';
    return backend.getStr(ns, key, out, len, "");
}

std::uint32_t getU32(ConfigBackend& backend, const char* ns, const char* key, std::uint32_t def) {
    char defText[11]{};
    char text[11]{};
    std::snprintf(defText, sizeof(defText), "%lu", static_cast<unsigned long>(def));
    if (!backend.getStr(ns, key, text, sizeof(text), defText)) {
        return def;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (!end || *end != '\0' || value > UINT32_MAX) {
        return def;
    }
    return static_cast<std::uint32_t>(value);
}

void presetKey(char* out, std::size_t len, std::size_t index, const char* suffix) {
    std::snprintf(out, len, "p%u_%s", static_cast<unsigned>(index), suffix);
}

void filterKey(char* out, std::size_t len, std::size_t index, const char* suffix) {
    std::snprintf(out, len, "f%u_%s", static_cast<unsigned>(index), suffix);
}

void voltagePointKey(char* out, std::size_t len, std::size_t index, const char* suffix) {
    std::snprintf(out, len, "v%u_%s", static_cast<unsigned>(index), suffix);
}

bool isCurrentRuntimeVersion(std::int32_t version) {
    return version == kRuntimeVersion;
}

const char* temperatureSensorConfigValue(const SystemConfig& config) {
    return config.temperatureKind == TemperatureKind::NtcBeta ? kTemperatureSensorNtcBeta : kSensorNone;
}

const char* tdsSensorConfigValue(const SystemConfig& config) {
    return config.tdsKind == TdsKind::AnalogTdsAo ? kTdsSensorAnalogAo : kSensorNone;
}

void applyTemperatureSensorConfigValue(SystemConfig& config, const char* value) {
    if (std::strcmp(value ? value : "", kTemperatureSensorNtcBeta) == 0) {
        config.temperatureKind = TemperatureKind::NtcBeta;
        return;
    }
    config.temperatureKind = TemperatureKind::None;
}

void applyTdsSensorConfigValue(SystemConfig& config, const char* value) {
    if (std::strcmp(value ? value : "", kTdsSensorAnalogAo) == 0) {
        config.tdsKind = TdsKind::AnalogTdsAo;
        return;
    }
    config.tdsKind = TdsKind::None;
}

void loadCommonSystemConfig(ConfigBackend& backend, SystemConfig& config) {
    config.confirmTimeoutSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "confirm_s", toInt(config.confirmTimeoutSec)));
    config.maxOutTimeSec = static_cast<std::uint32_t>(backend.getInt(kConfigNs, "max_time", toInt(config.maxOutTimeSec)));
    config.maxOutVolumeMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, "max_ml", toInt(config.maxOutVolumeMl)));
    config.overflowPercent = static_cast<std::uint8_t>(backend.getInt(kConfigNs, "overflow", config.overflowPercent));
    config.noFlowTimeoutSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "noflow_s", toInt(config.noFlowTimeoutSec)));
    config.highFlowMlPerMin =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "high_flow", toInt(config.highFlowMlPerMin)));
    config.highFlowDurationSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "high_s", toInt(config.highFlowDurationSec)));
    config.pauseTimeoutSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "pause_s", toInt(config.pauseTimeoutSec)));
    config.volumeAdjustStepMl =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "vol_step", toInt(config.volumeAdjustStepMl)));
    config.timeAdjustStepSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "time_step", toInt(config.timeAdjustStepSec)));
    config.pulseMinIntervalUs =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "pulse_min_us", toInt(config.pulseMinIntervalUs)));
    config.valveFullPowerSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "valve_s", toInt(config.valveFullPowerSec)));
    config.valveHoldDutyPercent = static_cast<std::uint8_t>(backend.getInt(kConfigNs, "hold_pct", config.valveHoldDutyPercent));
    config.valvePwmFrequencyHz =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "valve_hz", toInt(config.valvePwmFrequencyHz)));
    config.displaySleepSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "disp_s", toInt(config.displaySleepSec)));
    config.resultDisplaySec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "result_s", toInt(config.resultDisplaySec)));
    config.beepEnabled = backend.getBool(kConfigNs, "beep", config.beepEnabled);
    char sensorText[32]{};
    if (readStrKey(backend, kConfigNs, "temp_sensor", sensorText, sizeof(sensorText))) {
        applyTemperatureSensorConfigValue(config, sensorText);
    }
    config.temperatureNominalOhm = getU32(backend, kConfigNs, "temp_r25", config.temperatureNominalOhm);
    config.temperatureBeta = getU32(backend, kConfigNs, "temp_beta", config.temperatureBeta);
    config.temperaturePullupOhm = getU32(backend, kConfigNs, "temp_pull", config.temperaturePullupOhm);
    config.temperatureOffsetCentiC =
        static_cast<std::int16_t>(backend.getInt(kConfigNs, "temp_off_c", config.temperatureOffsetCentiC));
    config.temperatureCalibrated = backend.getBool(kConfigNs, "temp_cal", config.temperatureCalibrated);
    if (readStrKey(backend, kConfigNs, "tds_sensor", sensorText, sizeof(sensorText))) {
        applyTdsSensorConfigValue(config, sensorText);
    }
    config.tdsDividerHighOhm = getU32(backend, kConfigNs, "tds_rh", config.tdsDividerHighOhm);
    config.tdsDividerLowOhm = getU32(backend, kConfigNs, "tds_rl", config.tdsDividerLowOhm);
    config.tdsScale =
        static_cast<float>(backend.getInt(kConfigNs,
                                          "tds_scale_milli",
                                          static_cast<std::int32_t>(std::lround(config.tdsScale * 1000.0f)))) /
        1000.0f;
    config.tdsOffsetPpm = static_cast<std::int16_t>(backend.getInt(kConfigNs, "tds_off_ppm", config.tdsOffsetPpm));
    config.tdsCalibrated = backend.getBool(kConfigNs, "tds_cal", config.tdsCalibrated);
    config.tdsTemperatureCompensationEnabled =
        backend.getBool(kConfigNs, "tds_temp_comp", config.tdsTemperatureCompensationEnabled);
    config.inputVoltageDividerHighOhm = getU32(backend, kConfigNs, "vin_rh", config.inputVoltageDividerHighOhm);
    config.inputVoltageDividerLowOhm = getU32(backend, kConfigNs, "vin_rl", config.inputVoltageDividerLowOhm);
    InputVoltageCalibration& voltage = config.inputVoltageCalibration;
    voltage.pointCount =
        static_cast<std::uint8_t>(backend.getInt(kConfigNs, "vcal_count", voltage.pointCount));
    voltage.calibrated = backend.getBool(kConfigNs, "vcal_ok", voltage.calibrated);
    voltage.gainPpm = backend.getInt(kConfigNs, "vcal_gain", voltage.gainPpm);
    voltage.offsetMillivolts = backend.getInt(kConfigNs, "vcal_off", voltage.offsetMillivolts);
    for (std::size_t i = 0; i < kInputVoltageCalibrationMaxPoints; ++i) {
        char key[16]{};
        InputVoltageCalibrationPoint& point = voltage.points[i];
        voltagePointKey(key, sizeof(key), i, "raw");
        point.adcRaw = static_cast<std::int16_t>(backend.getInt(kConfigNs, key, point.adcRaw));
        voltagePointKey(key, sizeof(key), i, "min");
        point.adcRawMin = static_cast<std::int16_t>(backend.getInt(kConfigNs, key, point.adcRawMin));
        voltagePointKey(key, sizeof(key), i, "max");
        point.adcRawMax = static_cast<std::int16_t>(backend.getInt(kConfigNs, key, point.adcRawMax));
        voltagePointKey(key, sizeof(key), i, "rng");
        point.adcRange = static_cast<std::uint8_t>(backend.getInt(kConfigNs, key, point.adcRange));
        voltagePointKey(key, sizeof(key), i, "adc");
        point.adcMillivolts = getU32(backend, kConfigNs, key, point.adcMillivolts);
        voltagePointKey(key, sizeof(key), i, "theory");
        point.theoreticalInputMillivolts = getU32(backend, kConfigNs, key, point.theoreticalInputMillivolts);
        voltagePointKey(key, sizeof(key), i, "actual");
        point.actualInputMillivolts = getU32(backend, kConfigNs, key, point.actualInputMillivolts);
        voltagePointKey(key, sizeof(key), i, "at");
        point.capturedAt = getU32(backend, kConfigNs, key, point.capturedAt);
    }
}

void loadPresets(ConfigBackend& backend, SystemConfig& config) {
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        char key[12]{};
        presetKey(key, sizeof(key), i, "en");
        config.presets[i].enabled = backend.getBool(kConfigNs, key, config.presets[i].enabled);
        presetKey(key, sizeof(key), i, "type");
        config.presets[i].type =
            backend.getInt(kConfigNs, key, static_cast<std::int32_t>(config.presets[i].type)) == 1
                ? PresetType::Time
                : PresetType::Volume;
        presetKey(key, sizeof(key), i, "val");
        config.presets[i].value = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.presets[i].value)));
        presetKey(key, sizeof(key), i, "name");
        backend.getStr(kConfigNs, key, config.presets[i].name, sizeof(config.presets[i].name), config.presets[i].name);
    }
}

void loadFilterBasics(ConfigBackend& backend, FilterConfig& filter, std::size_t index) {
    char key[12]{};
    filterKey(key, sizeof(key), index, "en");
    filter.enabled = backend.getBool(kConfigNs, key, filter.enabled);
    filterKey(key, sizeof(key), index, "name");
    backend.getStr(kConfigNs, key, filter.name, sizeof(filter.name), filter.name);
    filterKey(key, sizeof(key), index, "life_ml");
    filter.lifeMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(filter.lifeMl)));
}

void loadFilterRanges(ConfigBackend& backend, SystemConfig& config) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        loadFilterBasics(backend, config.filters[i], i);
        char key[12]{};
        filterKey(key, sizeof(key), i, "life_min");
        config.filters[i].recommendDays =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.filters[i].recommendDays)));
        filterKey(key, sizeof(key), i, "life_max");
        config.filters[i].maxDays =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.filters[i].maxDays)));
    }
}

}  // namespace

ConfigStore::ConfigStore(ConfigBackend& backend)
    : backend_(backend),
      lastSystemStatus_(LoadStatus::Defaults),
      lastSystemRawVersion_(0) {}

SystemConfig ConfigStore::loadSystemConfig() {
    SystemConfig config = makeDefaultConfig();
    const std::int32_t storedVersion = backend_.getInt(kConfigNs, "ver", 0);
    lastSystemRawVersion_ = storedVersion;
    if (storedVersion != kConfigVersion) {
        lastSystemStatus_ = LoadStatus::Defaults;
        return config;
    }
    lastSystemStatus_ = LoadStatus::LoadedCurrent;

    loadCommonSystemConfig(backend_, config);
    loadPresets(backend_, config);
    loadFilterRanges(backend_, config);

    sanitizeConfig(config);
    return config;
}

SystemConfig ConfigStore::loadSystemConfigForExplicitSave(const SystemConfig& base) {
    SystemConfig config = base;
    loadCommonSystemConfig(backend_, config);
    sanitizeConfig(config);
    return config;
}

bool ConfigStore::saveSystemConfig(const SystemConfig& config) {
    std::unique_ptr<SystemConfig> storage(new (std::nothrow) SystemConfig(config));
    if (!storage) {
        return false;
    }
    sanitizeConfig(*storage);
    const SystemConfig& safe = *storage;

    bool ok = backend_.setInt(kConfigNs, "ver", 0);
    ok = okAll(ok, backend_.setInt(kConfigNs, "confirm_s", toInt(safe.confirmTimeoutSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "max_time", toInt(safe.maxOutTimeSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "max_ml", toInt(safe.maxOutVolumeMl)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "overflow", safe.overflowPercent));
    ok = okAll(ok, backend_.setInt(kConfigNs, "noflow_s", toInt(safe.noFlowTimeoutSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "high_flow", toInt(safe.highFlowMlPerMin)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "high_s", toInt(safe.highFlowDurationSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "pause_s", toInt(safe.pauseTimeoutSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "vol_step", toInt(safe.volumeAdjustStepMl)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "time_step", toInt(safe.timeAdjustStepSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "pulse_min_us", toInt(safe.pulseMinIntervalUs)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "valve_s", toInt(safe.valveFullPowerSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "hold_pct", safe.valveHoldDutyPercent));
    ok = okAll(ok, backend_.setInt(kConfigNs, "valve_hz", toInt(safe.valvePwmFrequencyHz)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "disp_s", toInt(safe.displaySleepSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "result_s", toInt(safe.resultDisplaySec)));
    ok = okAll(ok, backend_.setBool(kConfigNs, "beep", safe.beepEnabled));
    ok = okAll(ok, backend_.setStr(kConfigNs, "temp_sensor", temperatureSensorConfigValue(safe)));
    ok = okAll(ok, setU32(backend_, kConfigNs, "temp_r25", safe.temperatureNominalOhm));
    ok = okAll(ok, setU32(backend_, kConfigNs, "temp_beta", safe.temperatureBeta));
    ok = okAll(ok, setU32(backend_, kConfigNs, "temp_pull", safe.temperaturePullupOhm));
    ok = okAll(ok, backend_.setInt(kConfigNs, "temp_off_c", safe.temperatureOffsetCentiC));
    ok = okAll(ok, backend_.setBool(kConfigNs, "temp_cal", safe.temperatureCalibrated));
    ok = okAll(ok, backend_.setStr(kConfigNs, "tds_sensor", tdsSensorConfigValue(safe)));
    ok = okAll(ok, setU32(backend_, kConfigNs, "tds_rh", safe.tdsDividerHighOhm));
    ok = okAll(ok, setU32(backend_, kConfigNs, "tds_rl", safe.tdsDividerLowOhm));
    ok = okAll(ok,
               backend_.setInt(kConfigNs,
                               "tds_scale_milli",
                               static_cast<std::int32_t>(std::lround(safe.tdsScale * 1000.0f))));
    ok = okAll(ok, backend_.setInt(kConfigNs, "tds_off_ppm", safe.tdsOffsetPpm));
    ok = okAll(ok, backend_.setBool(kConfigNs, "tds_cal", safe.tdsCalibrated));
    ok = okAll(ok, backend_.setBool(kConfigNs, "tds_temp_comp", safe.tdsTemperatureCompensationEnabled));
    ok = okAll(ok, setU32(backend_, kConfigNs, "vin_rh", safe.inputVoltageDividerHighOhm));
    ok = okAll(ok, setU32(backend_, kConfigNs, "vin_rl", safe.inputVoltageDividerLowOhm));
    const InputVoltageCalibration& voltage = safe.inputVoltageCalibration;
    ok = okAll(ok, backend_.setInt(kConfigNs, "vcal_count", voltage.pointCount));
    ok = okAll(ok, backend_.setBool(kConfigNs, "vcal_ok", voltage.calibrated));
    ok = okAll(ok, backend_.setInt(kConfigNs, "vcal_gain", voltage.gainPpm));
    ok = okAll(ok, backend_.setInt(kConfigNs, "vcal_off", voltage.offsetMillivolts));
    for (std::size_t i = 0; i < kInputVoltageCalibrationMaxPoints; ++i) {
        char key[16]{};
        const InputVoltageCalibrationPoint& point = voltage.points[i];
        voltagePointKey(key, sizeof(key), i, "raw");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, point.adcRaw));
        voltagePointKey(key, sizeof(key), i, "min");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, point.adcRawMin));
        voltagePointKey(key, sizeof(key), i, "max");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, point.adcRawMax));
        voltagePointKey(key, sizeof(key), i, "rng");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, point.adcRange));
        voltagePointKey(key, sizeof(key), i, "adc");
        ok = okAll(ok, setU32(backend_, kConfigNs, key, point.adcMillivolts));
        voltagePointKey(key, sizeof(key), i, "theory");
        ok = okAll(ok, setU32(backend_, kConfigNs, key, point.theoreticalInputMillivolts));
        voltagePointKey(key, sizeof(key), i, "actual");
        ok = okAll(ok, setU32(backend_, kConfigNs, key, point.actualInputMillivolts));
        voltagePointKey(key, sizeof(key), i, "at");
        ok = okAll(ok, setU32(backend_, kConfigNs, key, point.capturedAt));
    }

    for (std::size_t i = 0; i < kPresetCount; ++i) {
        char key[12]{};
        presetKey(key, sizeof(key), i, "en");
        ok = okAll(ok, backend_.setBool(kConfigNs, key, safe.presets[i].enabled));
        presetKey(key, sizeof(key), i, "type");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, static_cast<std::int32_t>(safe.presets[i].type)));
        presetKey(key, sizeof(key), i, "val");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.presets[i].value)));
        presetKey(key, sizeof(key), i, "name");
        ok = okAll(ok, backend_.setStr(kConfigNs, key, safe.presets[i].name));
    }

    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[12]{};
        filterKey(key, sizeof(key), i, "en");
        ok = okAll(ok, backend_.setBool(kConfigNs, key, safe.filters[i].enabled));
        filterKey(key, sizeof(key), i, "name");
        ok = okAll(ok, backend_.setStr(kConfigNs, key, safe.filters[i].name));
        filterKey(key, sizeof(key), i, "life_min");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.filters[i].recommendDays)));
        filterKey(key, sizeof(key), i, "life_max");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.filters[i].maxDays)));
        filterKey(key, sizeof(key), i, "life_ml");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.filters[i].lifeMl)));
    }
    if (ok) {
        ok = backend_.setInt(kConfigNs, "ver", kConfigVersion);
    }

    return ok;
}

ConfigStore::LoadStatus ConfigStore::lastSystemConfigLoadStatus() const {
    return lastSystemStatus_;
}

std::int32_t ConfigStore::lastSystemConfigRawVersion() const {
    return lastSystemRawVersion_;
}

std::int32_t ConfigStore::currentSystemConfigVersion() const {
    return kConfigVersion;
}

StatisticsRecord ConfigStore::loadStatistics(const PeriodKeys& defaultKeys) {
    StatisticsRecord record{};
    record.lastDayKey = defaultKeys.dayKey;
    record.lastWeekKey = defaultKeys.weekKey;
    record.lastMonthKey = defaultKeys.monthKey;
    const std::int32_t version = backend_.getInt(kStatNs, "ver", 0);
    if (!isCurrentRuntimeVersion(version)) {
        return record;
    }

    record.todayMl = getU32(backend_, kStatNs, "today", 0);
    record.weekMl = getU32(backend_, kStatNs, "week", 0);
    record.monthMl = getU32(backend_, kStatNs, "month", 0);
    record.totalMl = getU32(backend_, kStatNs, "total", 0);
    record.lastDayKey = getU32(backend_, kStatNs, "day_key", defaultKeys.dayKey);
    record.lastWeekKey = getU32(backend_, kStatNs, "week_key", defaultKeys.weekKey);
    record.lastMonthKey = getU32(backend_, kStatNs, "month_key", defaultKeys.monthKey);

    StatisticsStore store(record);
    store.rollPeriods(defaultKeys);
    record = store.record();
    return record;
}

bool ConfigStore::saveStatistics(const StatisticsRecord& record) {
    bool ok = backend_.setInt(kStatNs, "ver", kRuntimeVersion);
    ok = okAll(ok, setU32(backend_, kStatNs, "today", record.todayMl));
    ok = okAll(ok, setU32(backend_, kStatNs, "week", record.weekMl));
    ok = okAll(ok, setU32(backend_, kStatNs, "month", record.monthMl));
    ok = okAll(ok, setU32(backend_, kStatNs, "total", record.totalMl));
    ok = okAll(ok, setU32(backend_, kStatNs, "day_key", record.lastDayKey));
    ok = okAll(ok, setU32(backend_, kStatNs, "week_key", record.lastWeekKey));
    ok = okAll(ok, setU32(backend_, kStatNs, "month_key", record.lastMonthKey));
    return ok;
}

bool ConfigStore::resetStatistics(const PeriodKeys& keys) {
    StatisticsRecord record{};
    record.lastDayKey = keys.dayKey;
    record.lastWeekKey = keys.weekKey;
    record.lastMonthKey = keys.monthKey;
    return backend_.clearNamespace(kStatNs) && saveStatistics(record);
}

void ConfigStore::loadFilterRuntime(FilterRuntime (&runtime)[kFilterCount]) {
    const std::int32_t version = backend_.getInt(kRunNs, "ver", 0);
    if (!isCurrentRuntimeVersion(version)) {
        return;
    }

    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[12]{};
        filterKey(key, sizeof(key), i, "start");
        runtime[i].startTime = getU32(backend_, kRunNs, key, runtime[i].startTime);
        filterKey(key, sizeof(key), i, "used");
        runtime[i].usedMl = getU32(backend_, kRunNs, key, runtime[i].usedMl);
        filterKey(key, sizeof(key), i, "boot");
        runtime[i].startBootId = getU32(backend_, kRunNs, key, runtime[i].startBootId);
    }
}

bool ConfigStore::saveFilterRuntime(const FilterRuntime (&runtime)[kFilterCount]) {
    bool ok = backend_.setInt(kRunNs, "ver", kRuntimeVersion);
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[12]{};
        filterKey(key, sizeof(key), i, "start");
        ok = okAll(ok, setU32(backend_, kRunNs, key, runtime[i].startTime));
        filterKey(key, sizeof(key), i, "used");
        ok = okAll(ok, setU32(backend_, kRunNs, key, runtime[i].usedMl));
        filterKey(key, sizeof(key), i, "boot");
        ok = okAll(ok, setU32(backend_, kRunNs, key, runtime[i].startBootId));
    }
    return ok;
}

}  // namespace faucet
