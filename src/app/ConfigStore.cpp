#include "app/ConfigStore.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace faucet {
namespace {

constexpr const char* kConfigNs = "faucet_cfg";
constexpr const char* kStatNs = "faucet_stat";
constexpr const char* kRunNs = "faucet_run";
constexpr std::int32_t kConfigVersion = 14;
constexpr std::int32_t kRuntimeVersion = 1;

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

bool hasIntKey(ConfigBackend& backend, const char* ns, const char* key) {
    constexpr std::int32_t kMissingA = INT32_MIN;
    constexpr std::int32_t kMissingB = INT32_MAX;
    return backend.getInt(ns, key, kMissingA) != kMissingA || backend.getInt(ns, key, kMissingB) != kMissingB;
}

bool hasStrKey(ConfigBackend& backend, const char* ns, const char* key) {
    char text[2]{};
    return backend.getStr(ns, key, text, sizeof(text), "");
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

bool isKnownSystemConfigVersion(std::int32_t version) {
    return version >= 1 && version <= kConfigVersion;
}

bool isReadableRuntimeVersion(std::int32_t version) {
    return version >= 1 && version <= kRuntimeVersion;
}

bool hasRecognizedLegacySystemConfig(ConfigBackend& backend) {
    const char* scalarKeys[] = {
        "confirm_s",
        "max_time",
        "max_ml",
        "overflow",
        "noflow_s",
        "high_flow",
        "high_s",
        "pause_s",
        "vol_step",
        "time_step",
        "pulse_min_us",
        "trace_count",
        "cal_an_us",
        "cal_win_s",
        "cal_tol",
        "cal_span",
        "cal_err",
        "cal_rel",
        "active_ms",
        "pulse_m",
        "seg_stable_p",
        "seg_start_p",
        "seg_start_ml",
        "oled_s",
        "valve_s",
        "hold_pct",
        "disp_s",
        "result_s",
        "lcd_addr",
        "mc_sp",
        "mc_sv",
        "mc_pl",
        "cand_start_p",
        "cand_start_ml",
        "cand_stable",
    };
    for (const char* key : scalarKeys) {
        if (hasIntKey(backend, kConfigNs, key)) {
            return true;
        }
    }

    const char* stringKeys[] = {"mc_note"};
    for (const char* key : stringKeys) {
        if (hasStrKey(backend, kConfigNs, key)) {
            return true;
        }
    }

    for (std::size_t i = 0; i < kPresetCount; ++i) {
        char key[16]{};
        presetKey(key, sizeof(key), i, "type");
        if (hasIntKey(backend, kConfigNs, key)) {
            return true;
        }
        presetKey(key, sizeof(key), i, "val");
        if (hasIntKey(backend, kConfigNs, key)) {
            return true;
        }
        presetKey(key, sizeof(key), i, "name");
        if (hasStrKey(backend, kConfigNs, key)) {
            return true;
        }
    }

    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[16]{};
        const char* intSuffixes[] = {"life_d", "life_min", "life_max", "life_ml", "start", "used"};
        for (const char* suffix : intSuffixes) {
            filterKey(key, sizeof(key), i, suffix);
            if (hasIntKey(backend, kConfigNs, key)) {
                return true;
            }
        }
        filterKey(key, sizeof(key), i, "name");
        if (hasStrKey(backend, kConfigNs, key)) {
            return true;
        }
    }

    return false;
}

bool hasLegacyLifeDays(ConfigBackend& backend) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[16]{};
        filterKey(key, sizeof(key), i, "life_d");
        if (hasIntKey(backend, kConfigNs, key)) {
            return true;
        }
    }
    return false;
}

std::int32_t inferLegacyVersionWithoutVersion(ConfigBackend& backend) {
    if (!hasRecognizedLegacySystemConfig(backend)) {
        return 0;
    }
    return hasLegacyLifeDays(backend) || hasIntKey(backend, kConfigNs, "pulse_m") ||
                   hasIntKey(backend, kConfigNs, "oled_s")
               ? 1
               : 2;
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
    config.recentPulseTraceCount =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "trace_count", toInt(config.recentPulseTraceCount)));
    config.calibrationAnalysisPulseMinIntervalUs = static_cast<std::uint32_t>(
        backend.getInt(kConfigNs, "cal_an_us", toInt(config.calibrationAnalysisPulseMinIntervalUs)));
    config.calibrationStableWindowSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cal_win_s", toInt(config.calibrationStableWindowSec)));
    config.calibrationStableTolerancePercent =
        static_cast<std::uint8_t>(backend.getInt(kConfigNs, "cal_tol", config.calibrationStableTolerancePercent));
    config.calibrationMinVolumeSpanMl =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cal_span", toInt(config.calibrationMinVolumeSpanMl)));
    config.calibrationMaxErrorMl =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cal_err", toInt(config.calibrationMaxErrorMl)));
    config.calibrationMaxRelativeErrorTenthPercent = static_cast<std::uint16_t>(
        backend.getInt(kConfigNs, "cal_rel", config.calibrationMaxRelativeErrorTenthPercent));
    config.valveFullPowerSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "valve_s", toInt(config.valveFullPowerSec)));
    config.valveHoldDutyPercent = static_cast<std::uint8_t>(backend.getInt(kConfigNs, "hold_pct", config.valveHoldDutyPercent));
    config.displaySleepSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "disp_s", toInt(config.displaySleepSec)));
    config.resultDisplaySec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "result_s", toInt(config.resultDisplaySec)));
    config.lcdI2cAddress =
        static_cast<std::uint8_t>(backend.getInt(kConfigNs, "lcd_addr", config.lcdI2cAddress));
    config.beepEnabled = backend.getBool(kConfigNs, "beep", config.beepEnabled);
}

void loadLegacyDisplayConfig(ConfigBackend& backend, SystemConfig& config) {
    config.displaySleepSec =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "oled_s", toInt(config.displaySleepSec)));
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

void loadFilterBasics(ConfigBackend& backend, FilterRecord& filter, std::size_t index) {
    char key[12]{};
    filterKey(key, sizeof(key), index, "en");
    filter.enabled = backend.getBool(kConfigNs, key, filter.enabled);
    filterKey(key, sizeof(key), index, "name");
    backend.getStr(kConfigNs, key, filter.name, sizeof(filter.name), filter.name);
    filterKey(key, sizeof(key), index, "life_ml");
    filter.lifeMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(filter.lifeMl)));
}

void loadLegacyFilterRuntime(ConfigBackend& backend, FilterRecord& filter, std::size_t index) {
    char key[12]{};
    filterKey(key, sizeof(key), index, "start");
    filter.startTime = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(filter.startTime)));
    filterKey(key, sizeof(key), index, "used");
    filter.usedMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(filter.usedMl)));
}

void loadLegacyFilters(ConfigBackend& backend, SystemConfig& config) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        loadFilterBasics(backend, config.filters[i], i);
        loadLegacyFilterRuntime(backend, config.filters[i], i);
        char key[12]{};
        filterKey(key, sizeof(key), i, "life_d");
        const std::uint32_t lifeDays =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.filters[i].recommendDays)));
        config.filters[i].recommendDays = lifeDays;
        config.filters[i].maxDays = lifeDays;
    }
}

void loadFilterRanges(ConfigBackend& backend, SystemConfig& config, std::int32_t version) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        loadFilterBasics(backend, config.filters[i], i);
        if (version < kConfigVersion) {
            loadLegacyFilterRuntime(backend, config.filters[i], i);
        }
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
      lastSystemStatus_(LoadStatus::DefaultsNoVersion),
      systemConfigReadOnly_(false),
      lastSystemRawVersion_(0),
      lastSystemMigrationWriteBack_(false) {}

SystemConfig ConfigStore::loadSystemConfig() {
    SystemConfig config = makeDefaultConfig();
    const std::int32_t storedVersion = backend_.getInt(kConfigNs, "ver", 0);
    std::int32_t version = storedVersion;
    systemConfigReadOnly_ = false;
    lastSystemRawVersion_ = storedVersion;
    lastSystemMigrationWriteBack_ = false;
    if (version == 0) {
        version = inferLegacyVersionWithoutVersion(backend_);
        if (version == 0) {
            lastSystemStatus_ = LoadStatus::DefaultsNoVersion;
            return config;
        }
    }
    if (version < 0) {
        lastSystemStatus_ = LoadStatus::UnsupportedVersionDefault;
        return config;
    }
    if (version > kConfigVersion) {
        systemConfigReadOnly_ = true;
        lastSystemStatus_ = LoadStatus::LoadedFutureVersionReadOnly;
    } else if (!isKnownSystemConfigVersion(version)) {
        lastSystemStatus_ = LoadStatus::UnsupportedVersionDefault;
        return config;
    } else {
        lastSystemStatus_ = version == kConfigVersion ? LoadStatus::LoadedCurrent : LoadStatus::MigratedLegacy;
    }

    loadCommonSystemConfig(backend_, config);
    if (version < 4) {
        loadLegacyDisplayConfig(backend_, config);
    }
    loadPresets(backend_, config);
    if (version == 1) {
        loadLegacyFilters(backend_, config);
    } else {
        loadFilterRanges(backend_, config, version);
    }

    sanitizeConfig(config);
    if (version != kConfigVersion && !systemConfigReadOnly_) {
        lastSystemMigrationWriteBack_ = saveSystemConfig(config);
        if (lastSystemMigrationWriteBack_) {
            saveFilterRuntime(config.filters);
        }
    }
    return config;
}

bool ConfigStore::saveSystemConfig(const SystemConfig& config) {
    if (systemConfigReadOnly_) {
        return false;
    }

    SystemConfig* safeStorage = new (std::nothrow) SystemConfig(config);
    if (!safeStorage) {
        return false;
    }
    sanitizeConfig(*safeStorage);
    const SystemConfig& safe = *safeStorage;

    bool ok = true;
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
    ok = okAll(ok, backend_.setInt(kConfigNs, "trace_count", toInt(safe.recentPulseTraceCount)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "cal_an_us", toInt(safe.calibrationAnalysisPulseMinIntervalUs)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "cal_win_s", toInt(safe.calibrationStableWindowSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "cal_tol", safe.calibrationStableTolerancePercent));
    ok = okAll(ok, backend_.setInt(kConfigNs, "cal_span", toInt(safe.calibrationMinVolumeSpanMl)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "cal_err", toInt(safe.calibrationMaxErrorMl)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "cal_rel", safe.calibrationMaxRelativeErrorTenthPercent));
    ok = okAll(ok, backend_.setInt(kConfigNs, "valve_s", toInt(safe.valveFullPowerSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "hold_pct", safe.valveHoldDutyPercent));
    ok = okAll(ok, backend_.setInt(kConfigNs, "disp_s", toInt(safe.displaySleepSec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "result_s", toInt(safe.resultDisplaySec)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "lcd_addr", safe.lcdI2cAddress));
    ok = okAll(ok, backend_.setBool(kConfigNs, "beep", safe.beepEnabled));

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
    ok = okAll(ok, backend_.setInt(kConfigNs, "ver", kConfigVersion));

    delete safeStorage;
    return ok;
}

bool ConfigStore::resetSystemConfig() {
    systemConfigReadOnly_ = false;
    const bool ok = backend_.clearNamespace(kConfigNs) && saveSystemConfig(makeDefaultConfig());
    if (ok) {
        lastSystemStatus_ = LoadStatus::LoadedCurrent;
    }
    return ok;
}

ConfigStore::LoadStatus ConfigStore::lastSystemConfigLoadStatus() const {
    return lastSystemStatus_;
}

bool ConfigStore::systemConfigReadOnly() const {
    return systemConfigReadOnly_;
}

std::int32_t ConfigStore::lastSystemConfigRawVersion() const {
    return lastSystemRawVersion_;
}

std::int32_t ConfigStore::currentSystemConfigVersion() const {
    return kConfigVersion;
}

bool ConfigStore::lastSystemConfigMigrationWriteBack() const {
    return lastSystemMigrationWriteBack_;
}

StatisticsRecord ConfigStore::loadStatistics(const PeriodKeys& defaultKeys) {
    StatisticsRecord record{};
    record.lastDayKey = defaultKeys.dayKey;
    record.lastWeekKey = defaultKeys.weekKey;
    record.lastMonthKey = defaultKeys.monthKey;
    const std::int32_t version = backend_.getInt(kStatNs, "ver", 0);
    if (!isReadableRuntimeVersion(version)) {
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
    if (version != kRuntimeVersion) {
        saveStatistics(record);
    }
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

void ConfigStore::loadFilterRuntime(FilterRecord (&records)[kFilterCount]) {
    const std::int32_t version = backend_.getInt(kRunNs, "ver", 0);
    if (!isReadableRuntimeVersion(version)) {
        return;
    }

    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[12]{};
        filterKey(key, sizeof(key), i, "start");
        records[i].startTime = getU32(backend_, kRunNs, key, records[i].startTime);
        filterKey(key, sizeof(key), i, "used");
        records[i].usedMl = getU32(backend_, kRunNs, key, records[i].usedMl);
        filterKey(key, sizeof(key), i, "boot");
        records[i].startBootId = getU32(backend_, kRunNs, key, records[i].startBootId);
    }
    if (version != kRuntimeVersion) {
        saveFilterRuntime(records);
    }
}

bool ConfigStore::saveFilterRuntime(const FilterRecord (&records)[kFilterCount]) {
    bool ok = backend_.setInt(kRunNs, "ver", kRuntimeVersion);
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        char key[12]{};
        filterKey(key, sizeof(key), i, "start");
        ok = okAll(ok, setU32(backend_, kRunNs, key, records[i].startTime));
        filterKey(key, sizeof(key), i, "used");
        ok = okAll(ok, setU32(backend_, kRunNs, key, records[i].usedMl));
        filterKey(key, sizeof(key), i, "boot");
        ok = okAll(ok, setU32(backend_, kRunNs, key, records[i].startBootId));
    }
    return ok;
}

bool ConfigStore::resetFilterRuntime() {
    return backend_.clearNamespace(kRunNs);
}

}  // namespace faucet
