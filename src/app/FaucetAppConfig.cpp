#ifndef NATIVE_BUILD

#include "app/FaucetAppConfig.h"

#include "app/AppController.h"

#include <Esp32Base.h>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

constexpr const char* kConfigNs = "faucet_cfg";
constexpr const char* kVersionKey = "ver";
constexpr std::int32_t kConfigVersion = 4;

FaucetAppConfigContext g_context{};

const char kGroupSafety[] = "safety";
const char kGroupFlow[] = "flow";
const char kGroupValve[] = "valve";
const char kGroupLocal[] = "local";
const char kGroupCalibration[] = "calibration";
const char kGroupPresets[] = "presets";
const char kGroupFilters[] = "filters";

const char kKeyConfirmTimeout[] = "confirm_s";
const char kKeyMaxTime[] = "max_time";
const char kKeyMaxMl[] = "max_ml";
const char kKeyOverflow[] = "overflow";
const char kKeyNoFlow[] = "noflow_s";
const char kKeyHighFlow[] = "high_flow";
const char kKeyHighDuration[] = "high_s";
const char kKeyPauseTimeout[] = "pause_s";
const char kKeyPulseMilli[] = "pulse_m";
const char kKeyValveFullPower[] = "valve_s";
const char kKeyValveHoldDuty[] = "hold_pct";
const char kKeyDisplaySleep[] = "disp_s";
const char kKeyResultDisplay[] = "result_s";
const char kKeyLcdAddress[] = "lcd_addr";
const char kKeyBeep[] = "beep";

const char kCalKeys[kCalibrationTargetCount][8] = {"cal0_ml", "cal1_ml", "cal2_ml", "cal3_ml"};
const char kPresetEnabledKeys[kPresetCount][8] = {
    "p0_en", "p1_en", "p2_en", "p3_en", "p4_en", "p5_en", "p6_en", "p7_en", "p8_en"};
const char kPresetNameKeys[kPresetCount][8] = {
    "p0_name", "p1_name", "p2_name", "p3_name", "p4_name", "p5_name", "p6_name", "p7_name", "p8_name"};
const char kFilterEnabledKeys[kFilterCount][8] = {"f0_en", "f1_en", "f2_en", "f3_en", "f4_en", "f5_en"};
const char kFilterNameKeys[kFilterCount][8] = {"f0_name", "f1_name", "f2_name", "f3_name", "f4_name", "f5_name"};
const char kFilterRecommendKeys[kFilterCount][12] = {
    "f0_life_min", "f1_life_min", "f2_life_min", "f3_life_min", "f4_life_min", "f5_life_min"};
const char kFilterMaxKeys[kFilterCount][12] = {
    "f0_life_max", "f1_life_max", "f2_life_max", "f3_life_max", "f4_life_max", "f5_life_max"};
const char kFilterLifeMlKeys[kFilterCount][11] = {
    "f0_life_ml", "f1_life_ml", "f2_life_ml", "f3_life_ml", "f4_life_ml", "f5_life_ml"};

const char kCalLabels[kCalibrationTargetCount][16] = {"校准容量1", "校准容量2", "校准容量3", "校准容量4"};
const char kPresetEnabledLabels[kPresetCount][16] = {
    "预设1启用", "预设2启用", "预设3启用", "预设4启用", "预设5启用",
    "预设6启用", "预设7启用", "预设8启用", "预设9启用"};
const char kPresetNameLabels[kPresetCount][16] = {
    "预设1名称", "预设2名称", "预设3名称", "预设4名称", "预设5名称",
    "预设6名称", "预设7名称", "预设8名称", "预设9名称"};
const char kFilterEnabledLabels[kFilterCount][16] = {
    "滤芯1启用", "滤芯2启用", "滤芯3启用", "滤芯4启用", "滤芯5启用", "滤芯6启用"};
const char kFilterNameLabels[kFilterCount][16] = {
    "滤芯1名称", "滤芯2名称", "滤芯3名称", "滤芯4名称", "滤芯5名称", "滤芯6名称"};
const char kFilterRecommendLabels[kFilterCount][20] = {
    "滤芯1建议天数", "滤芯2建议天数", "滤芯3建议天数",
    "滤芯4建议天数", "滤芯5建议天数", "滤芯6建议天数"};
const char kFilterMaxLabels[kFilterCount][20] = {
    "滤芯1最长天数", "滤芯2最长天数", "滤芯3最长天数",
    "滤芯4最长天数", "滤芯5最长天数", "滤芯6最长天数"};
const char kFilterLifeMlLabels[kFilterCount][20] = {
    "滤芯1寿命流量", "滤芯2寿命流量", "滤芯3寿命流量",
    "滤芯4寿命流量", "滤芯5寿命流量", "滤芯6寿命流量"};

void copyError(char* error, std::size_t len, const char* text) {
    if (error && len > 0) {
        std::snprintf(error, len, "%s", text ? text : "");
    }
}

bool validateTextName(const Esp32BaseAppConfig::FieldRef&,
                      const Esp32BaseAppConfig::FieldValue& value,
                      char* error,
                      std::size_t errorLen) {
    const char* text = value.text ? value.text : "";
    if (std::strchr(text, '<') || std::strchr(text, '>') || std::strchr(text, '&')) {
        copyError(error, errorLen, "名称不能包含 <、> 或 &。");
        return false;
    }
    return true;
}

bool submittedInt(const char* key, std::int32_t& out) {
    return Esp32BaseAppConfig::submittedInt(kConfigNs, key, out);
}

bool validateAppConfigPage(char* error, std::size_t errorLen) {
    if (g_context.app && !g_context.app->canApplyConfig()) {
        copyError(error, errorLen, "设备正在出水或显示结果，请回到待机后再保存配置。");
        return false;
    }

    std::int32_t maxTime = 0;
    std::int32_t noFlow = 0;
    std::int32_t highDuration = 0;
    if (!submittedInt(kKeyMaxTime, maxTime) || !submittedInt(kKeyNoFlow, noFlow) ||
        !submittedInt(kKeyHighDuration, highDuration)) {
        copyError(error, errorLen, "提交的流量保护参数不可用。");
        return false;
    }
    if (noFlow > maxTime || highDuration > maxTime) {
        copyError(error, errorLen, "无流量超时和高流量持续时间不能大于最长出水时间。");
        return false;
    }

    bool hasCalibrationTarget = false;
    std::int32_t targets[kCalibrationTargetCount]{};
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        if (!submittedInt(kCalKeys[i], targets[i])) {
            copyError(error, errorLen, "提交的校准容量不可用。");
            return false;
        }
        if (targets[i] != 0) {
            hasCalibrationTarget = true;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (targets[i] != 0 && targets[i] == targets[j]) {
                copyError(error, errorLen, "启用的校准容量不能重复。");
                return false;
            }
        }
    }
    if (!hasCalibrationTarget) {
        copyError(error, errorLen, "至少需要保留一个启用的校准容量。");
        return false;
    }

    for (std::size_t i = 0; i < kFilterCount; ++i) {
        std::int32_t recommendDays = 0;
        std::int32_t maxDays = 0;
        if (!submittedInt(kFilterRecommendKeys[i], recommendDays) || !submittedInt(kFilterMaxKeys[i], maxDays)) {
            copyError(error, errorLen, "提交的滤芯寿命参数不可用。");
            return false;
        }
        if (recommendDays == 0 && maxDays != 0) {
            copyError(error, errorLen, "滤芯建议天数为 0 时，最长天数也必须为 0。");
            return false;
        }
        if (recommendDays > 0 && maxDays > 0 && maxDays < recommendDays) {
            copyError(error, errorLen, "滤芯最长天数不能小于建议天数。");
            return false;
        }
    }

    return true;
}

void onAppConfigChange(const Esp32BaseAppConfig::Change& change) {
    ESP32BASE_LOG_I("appcfg",
                    "changed ns=%s key=%s type=%u restart=%s old=%s new=%s oldRaw=%ld newRaw=%ld oldBool=%s newBool=%s",
                    change.field.ns ? change.field.ns : "",
                    change.field.key ? change.field.key : "",
                    static_cast<unsigned>(change.field.type),
                    change.field.restartRequired ? "yes" : "no",
                    change.oldValue.text ? change.oldValue.text : "",
                    change.newValue.text ? change.newValue.text : "",
                    static_cast<long>(change.oldValue.raw),
                    static_cast<long>(change.newValue.raw),
                    change.oldValue.boolean ? "true" : "false",
                    change.newValue.boolean ? "true" : "false");
}

void onAppConfigSave(const Esp32BaseAppConfig::SaveSummary& summary) {
    ESP32BASE_LOG_I("appcfg",
                    "save changed=%u saved=%u failed=%u restart=%s",
                    summary.changedCount,
                    summary.savedCount,
                    summary.failedCount,
                    summary.restartRequired ? "yes" : "no");
    if (summary.savedCount == 0 || summary.failedCount > 0 || !g_context.configStore || !g_context.config ||
        !g_context.app) {
        return;
    }

    const std::int32_t version = Esp32BaseConfig::getInt(kConfigNs, kVersionKey, 0);
    if (version != kConfigVersion) {
        Esp32BaseConfig::setInt(kConfigNs, kVersionKey, kConfigVersion);
    }

    if (summary.restartRequired) {
        ESP32BASE_LOG_I("appcfg", "runtime apply skipped because restart-required field changed");
        return;
    }

    SystemConfig loaded = g_context.configStore->loadSystemConfig();
    if (!g_context.app->applyConfig(loaded)) {
        ESP32BASE_LOG_W("appcfg", "runtime apply failed after app config save");
        return;
    }
    *g_context.config = loaded;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
}

bool addCoreFields(const SystemConfig& defaults) {
    bool ok = true;
    ok = Esp32BaseAppConfig::addGroup({kGroupSafety, "安全限制"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupFlow, "流量保护"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupValve, "电磁阀"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupLocal, "本地交互"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupCalibration, "校准"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupPresets, "预设"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupFilters, "滤芯"}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyConfirmTimeout, "确认超时", static_cast<std::int32_t>(defaults.confirmTimeoutSec), 3, 60, 1, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyMaxTime, "最长出水时间", static_cast<std::int32_t>(defaults.maxOutTimeSec), 30, 7200, 5, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyMaxMl, "最大出水量", static_cast<std::int32_t>(defaults.maxOutVolumeMl), 1000, 100000, 100, "ml", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyOverflow, "超量保护", defaults.overflowPercent, 1, 50, 1, "%", nullptr, false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyNoFlow, "无流量超时", static_cast<std::int32_t>(defaults.noFlowTimeoutSec), 1, 30, 1, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyHighFlow, "高流量阈值", static_cast<std::int32_t>(defaults.highFlowMlPerMin), 1000, 100000, 100, "ml/min", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyHighDuration, "高流量持续", static_cast<std::int32_t>(defaults.highFlowDurationSec), 1, 30, 1, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyPauseTimeout, "暂停超时", static_cast<std::int32_t>(defaults.pauseTimeoutSec), 10, 3600, 10, "s", nullptr, false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupValve, kConfigNs, kKeyValveFullPower, "全功率时间", static_cast<std::int32_t>(defaults.valveFullPowerSec), 1, 10, 1, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupValve, kConfigNs, kKeyValveHoldDuty, "保持占空比", defaults.valveHoldDutyPercent, kMinValveHoldDutyPercent, kMaxValveHoldDutyPercent, 1, "%", nullptr, false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyDisplaySleep, "LCD熄屏时间", static_cast<std::int32_t>(defaults.displaySleepSec), 5, 300, 5, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyResultDisplay, "结果显示时间", static_cast<std::int32_t>(defaults.resultDisplaySec), 0, 60, 1, "s", nullptr, false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyLcdAddress, "LCD I2C地址", defaults.lcdI2cAddress, 0x03, 0x77, 1, nullptr, "保存后重启生效。", true, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addBool({kGroupLocal, kConfigNs, kKeyBeep, "蜂鸣器", defaults.beepEnabled, nullptr, false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addDecimal({kGroupCalibration, kConfigNs, kKeyPulseMilli, "每毫升脉冲", static_cast<std::int32_t>(defaults.pulsePerMl * 1000.0f), static_cast<std::int32_t>(kMinPulsePerMl * 1000.0f), static_cast<std::int32_t>(kMaxPulsePerMl * 1000.0f), 1, 3, "pulse/ml", nullptr, false, nullptr}) && ok;
    for (std::size_t i = 0; i < kCalibrationTargetCount; ++i) {
        ok = Esp32BaseAppConfig::addInt({kGroupCalibration, kConfigNs, kCalKeys[i], kCalLabels[i], static_cast<std::int32_t>(defaults.calibrationTargetsMl[i]), 0, kMaxCalibrationTargetMl, 100, "ml", "0 表示不启用。", false, nullptr}) && ok;
    }

    return ok;
}

bool addPresetFields(const SystemConfig& defaults) {
    bool ok = true;
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        ok = Esp32BaseAppConfig::addBool({kGroupPresets, kConfigNs, kPresetEnabledKeys[i], kPresetEnabledLabels[i], defaults.presets[i].enabled, nullptr, false, nullptr}) && ok;
        ok = Esp32BaseAppConfig::addString({kGroupPresets, kConfigNs, kPresetNameKeys[i], kPresetNameLabels[i], defaults.presets[i].name, 1, kNameLength - 1, nullptr, false, validateTextName}) && ok;
    }
    return ok;
}

bool addFilterFields(const SystemConfig& defaults) {
    bool ok = true;
    for (std::size_t i = 0; i < kFilterCount; ++i) {
        ok = Esp32BaseAppConfig::addBool({kGroupFilters, kConfigNs, kFilterEnabledKeys[i], kFilterEnabledLabels[i], defaults.filters[i].enabled, nullptr, false, nullptr}) && ok;
        ok = Esp32BaseAppConfig::addString({kGroupFilters, kConfigNs, kFilterNameKeys[i], kFilterNameLabels[i], defaults.filters[i].name, 1, kNameLength - 1, nullptr, false, validateTextName}) && ok;
        ok = Esp32BaseAppConfig::addInt({kGroupFilters, kConfigNs, kFilterRecommendKeys[i], kFilterRecommendLabels[i], static_cast<std::int32_t>(defaults.filters[i].recommendDays), 0, kMaxFilterLifeDays, 30, "day", nullptr, false, nullptr}) && ok;
        ok = Esp32BaseAppConfig::addInt({kGroupFilters, kConfigNs, kFilterMaxKeys[i], kFilterMaxLabels[i], static_cast<std::int32_t>(defaults.filters[i].maxDays), 0, kMaxFilterLifeDays, 30, "day", nullptr, false, nullptr}) && ok;
        ok = Esp32BaseAppConfig::addInt({kGroupFilters, kConfigNs, kFilterLifeMlKeys[i], kFilterLifeMlLabels[i], static_cast<std::int32_t>(defaults.filters[i].lifeMl), 0, kMaxFilterLifeMl, 1000, "ml", nullptr, false, nullptr}) && ok;
    }
    return ok;
}

}  // namespace

void setFaucetAppConfigContext(const FaucetAppConfigContext& context) {
    g_context = context;
}

bool registerFaucetAppConfig() {
    SystemConfig defaults = makeDefaultConfig();
    Esp32BaseAppConfig::setTitle("出水参数");
    Esp32BaseAppConfig::setPageValidateCallback(validateAppConfigPage);
    Esp32BaseAppConfig::setChangeCallback(onAppConfigChange);
    Esp32BaseAppConfig::setSaveCallback(onAppConfigSave);
    return addCoreFields(defaults) && addPresetFields(defaults) && addFilterFields(defaults);
}

}  // namespace faucet

#endif
