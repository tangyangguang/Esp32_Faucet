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
constexpr std::int32_t kConfigVersion = 11;
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

void meteringSlotKey(char* out, std::size_t len, std::size_t index, const char* suffix) {
    std::snprintf(out, len, "ms%u_%s", static_cast<unsigned>(index), suffix);
}

bool isKnownSystemConfigVersion(std::int32_t version) {
    return version >= 1 && version <= kConfigVersion;
}

bool isReadableRuntimeVersion(std::int32_t version) {
    return version >= 1 && version <= kRuntimeVersion;
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
    config.recentPulseTraceCount =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "trace_count", toInt(config.recentPulseTraceCount)));
    config.activeMeteringSlot =
        static_cast<std::uint8_t>(backend.getInt(kConfigNs, "active_ms", config.activeMeteringSlot));
    for (std::size_t i = 0; i < kMeteringSlotCount; ++i) {
        char key[16]{};
        meteringSlotKey(key, sizeof(key), i, "valid");
        config.meteringSlots[i].valid = backend.getBool(kConfigNs, key, config.meteringSlots[i].valid);
        meteringSlotKey(key, sizeof(key), i, "name");
        backend.getStr(kConfigNs, key, config.meteringSlots[i].name, sizeof(config.meteringSlots[i].name), config.meteringSlots[i].name);
        meteringSlotKey(key, sizeof(key), i, "sp");
        config.meteringSlots[i].params.startupPulseCount =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.meteringSlots[i].params.startupPulseCount)));
        meteringSlotKey(key, sizeof(key), i, "sv");
        config.meteringSlots[i].params.startupVolumeMl =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.meteringSlots[i].params.startupVolumeMl)));
        meteringSlotKey(key, sizeof(key), i, "pl");
        config.meteringSlots[i].params.stablePulsePerLiter =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.meteringSlots[i].params.stablePulsePerLiter)));
        meteringSlotKey(key, sizeof(key), i, "create");
        backend.getStr(kConfigNs, key, config.meteringSlots[i].creationNote, sizeof(config.meteringSlots[i].creationNote), config.meteringSlots[i].creationNote);
        meteringSlotKey(key, sizeof(key), i, "modify");
        backend.getStr(kConfigNs, key, config.meteringSlots[i].lastModifiedNote, sizeof(config.meteringSlots[i].lastModifiedNote), config.meteringSlots[i].lastModifiedNote);
        meteringSlotKey(key, sizeof(key), i, "mod_at");
        config.meteringSlots[i].modifiedAt =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.meteringSlots[i].modifiedAt)));
    }
    config.meteringCandidate.ready = backend.getBool(kConfigNs, "mc_ready", config.meteringCandidate.ready);
    config.meteringCandidate.params.startupPulseCount =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "mc_sp", toInt(config.meteringCandidate.params.startupPulseCount)));
    config.meteringCandidate.params.startupVolumeMl =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "mc_sv", toInt(config.meteringCandidate.params.startupVolumeMl)));
    config.meteringCandidate.params.stablePulsePerLiter =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "mc_pl", toInt(config.meteringCandidate.params.stablePulsePerLiter)));
    backend.getStr(kConfigNs, "mc_note", config.meteringCandidate.note, sizeof(config.meteringCandidate.note), config.meteringCandidate.note);
    config.meteringCandidate.generatedAt =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "mc_at", toInt(config.meteringCandidate.generatedAt)));
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
    filterKey(key, sizeof(key), index, "start");
    filter.startTime = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(filter.startTime)));
    filterKey(key, sizeof(key), index, "used");
    filter.usedMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(filter.usedMl)));
}

void loadLegacyFilters(ConfigBackend& backend, SystemConfig& config) {
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        loadFilterBasics(backend, config.filters[i], i);
        char key[12]{};
        filterKey(key, sizeof(key), i, "life_d");
        const std::uint32_t lifeDays =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, key, toInt(config.filters[i].recommendDays)));
        config.filters[i].recommendDays = lifeDays;
        config.filters[i].maxDays = lifeDays;
    }
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

void migrateLegacyMetering(ConfigBackend& backend, SystemConfig& config) {
    const std::uint32_t legacyStable =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs,
                                                  "seg_stable_p",
                                                  backend.getInt(kConfigNs, "pulse_m", kDefaultStablePulsePerLiter)));
    const std::uint32_t legacyStartupPulse =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "seg_start_p", 0));
    const std::uint32_t legacyStartupMl =
        static_cast<std::uint32_t>(backend.getInt(kConfigNs, "seg_start_ml", 0));
    config.activeMeteringSlot = 0;
    config.meteringSlots[0].valid = true;
    config.meteringSlots[0].params = MeteringParameters{legacyStartupPulse, legacyStartupMl, legacyStable};
    std::snprintf(config.meteringSlots[0].creationNote,
                  sizeof(config.meteringSlots[0].creationNote),
                  "由旧配置迁移：启动脉冲数 %luP，启动水量 %luml，稳态 P/L %lu。",
                  static_cast<unsigned long>(legacyStartupPulse),
                  static_cast<unsigned long>(legacyStartupMl),
                  static_cast<unsigned long>(legacyStable));
    if (backend.getBool(kConfigNs, "cand_ready", false)) {
        config.meteringCandidate.ready = true;
        config.meteringCandidate.params.startupPulseCount =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_start_p", 0));
        config.meteringCandidate.params.startupVolumeMl =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_start_ml", 0));
        config.meteringCandidate.params.stablePulsePerLiter =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_stable", legacyStable));
        config.meteringCandidate.generatedAt =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_at", 0));
        const std::uint32_t sampleCount =
            static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_samples", 0));
        const std::uint32_t minMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_min_ml", 0));
        const std::uint32_t maxMl = static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_max_ml", 0));
        const std::uint32_t maxError = static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_err_ml", 0));
        const std::uint32_t startupSec = static_cast<std::uint32_t>(backend.getInt(kConfigNs, "cand_start_s", 0));
        std::snprintf(config.meteringCandidate.note,
                      sizeof(config.meteringCandidate.note),
                      "由旧候选迁移：样本数量 %lu，容量范围 %lu-%luml，最大误差 %luml，启动时长典型 %lus。",
                      static_cast<unsigned long>(sampleCount),
                      static_cast<unsigned long>(minMl),
                      static_cast<unsigned long>(maxMl),
                      static_cast<unsigned long>(maxError),
                      static_cast<unsigned long>(startupSec));
    }
}

}  // namespace

ConfigStore::ConfigStore(ConfigBackend& backend)
    : backend_(backend), lastSystemStatus_(LoadStatus::DefaultsNoVersion), systemConfigReadOnly_(false) {}

SystemConfig ConfigStore::loadSystemConfig() {
    SystemConfig config = makeDefaultConfig();
    const std::int32_t version = backend_.getInt(kConfigNs, "ver", 0);
    systemConfigReadOnly_ = false;
    if (version == 0) {
        lastSystemStatus_ = LoadStatus::DefaultsNoVersion;
        return config;
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
    if (version < kConfigVersion) {
        migrateLegacyMetering(backend_, config);
    }
    if (version < 4) {
        loadLegacyDisplayConfig(backend_, config);
    }
    loadPresets(backend_, config);
    if (version == 1) {
        loadLegacyFilters(backend_, config);
    } else {
        loadFilterRanges(backend_, config);
    }

    sanitizeConfig(config);
    if (version != kConfigVersion && !systemConfigReadOnly_) {
        backend_.clearNamespace(kConfigNs);
        saveSystemConfig(config);
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

    bool ok = backend_.setInt(kConfigNs, "ver", kConfigVersion);
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
    ok = okAll(ok, backend_.setInt(kConfigNs, "trace_count", toInt(safe.recentPulseTraceCount)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "active_ms", safe.activeMeteringSlot));
    for (std::size_t i = 0; i < kMeteringSlotCount; ++i) {
        char key[16]{};
        meteringSlotKey(key, sizeof(key), i, "valid");
        ok = okAll(ok, backend_.setBool(kConfigNs, key, safe.meteringSlots[i].valid));
        meteringSlotKey(key, sizeof(key), i, "name");
        ok = okAll(ok, backend_.setStr(kConfigNs, key, safe.meteringSlots[i].name));
        meteringSlotKey(key, sizeof(key), i, "sp");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.meteringSlots[i].params.startupPulseCount)));
        meteringSlotKey(key, sizeof(key), i, "sv");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.meteringSlots[i].params.startupVolumeMl)));
        meteringSlotKey(key, sizeof(key), i, "pl");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.meteringSlots[i].params.stablePulsePerLiter)));
        meteringSlotKey(key, sizeof(key), i, "create");
        ok = okAll(ok, backend_.setStr(kConfigNs, key, safe.meteringSlots[i].creationNote));
        meteringSlotKey(key, sizeof(key), i, "modify");
        ok = okAll(ok, backend_.setStr(kConfigNs, key, safe.meteringSlots[i].lastModifiedNote));
        meteringSlotKey(key, sizeof(key), i, "mod_at");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.meteringSlots[i].modifiedAt)));
    }
    ok = okAll(ok, backend_.setBool(kConfigNs, "mc_ready", safe.meteringCandidate.ready));
    ok = okAll(ok, backend_.setInt(kConfigNs, "mc_sp", toInt(safe.meteringCandidate.params.startupPulseCount)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "mc_sv", toInt(safe.meteringCandidate.params.startupVolumeMl)));
    ok = okAll(ok, backend_.setInt(kConfigNs, "mc_pl", toInt(safe.meteringCandidate.params.stablePulsePerLiter)));
    ok = okAll(ok, backend_.setStr(kConfigNs, "mc_note", safe.meteringCandidate.note));
    ok = okAll(ok, backend_.setInt(kConfigNs, "mc_at", toInt(safe.meteringCandidate.generatedAt)));
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
        filterKey(key, sizeof(key), i, "start");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.filters[i].startTime)));
        filterKey(key, sizeof(key), i, "used");
        ok = okAll(ok, backend_.setInt(kConfigNs, key, toInt(safe.filters[i].usedMl)));
    }

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
