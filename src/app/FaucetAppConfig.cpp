#ifndef NATIVE_BUILD

#include "app/FaucetAppConfig.h"

#include "app/AppController.h"

#include <Esp32Base.h>
#include <cstdio>
#include <cstring>

namespace faucet {
namespace {

constexpr const char* kConfigNs = "faucet_cfg";

FaucetAppConfigContext g_context{};

const char kGroupSafety[] = "safety";
const char kGroupFlow[] = "flow";
const char kGroupValve[] = "valve";
const char kGroupLocal[] = "local";
const char kGroupMetering[] = "metering";
const char kGroupSensors[] = "sensors";

const char kKeyConfirmTimeout[] = "confirm_s";
const char kKeyMaxTime[] = "max_time";
const char kKeyMaxMl[] = "max_ml";
const char kKeyOverflow[] = "overflow";
const char kKeyNoFlow[] = "noflow_s";
const char kKeyHighFlow[] = "high_flow";
const char kKeyHighDuration[] = "high_s";
const char kKeyPauseTimeout[] = "pause_s";
const char kKeyVolumeStep[] = "vol_step";
const char kKeyTimeStep[] = "time_step";
const char kKeyPulseMinIntervalUs[] = "pulse_min_us";
const char kKeyPulseObservationWindowSec[] = "pulse_win_s";
const char kKeyCalibrationAnalysisPulseMinIntervalUs[] = "cal_an_us";
const char kKeyCalibrationStableWindowSec[] = "cal_win_s";
const char kKeyCalibrationStableTolerancePercent[] = "cal_tol";
const char kKeyCalibrationMinVolumeSpanMl[] = "cal_span";
const char kKeyCalibrationMaxErrorMl[] = "cal_err";
const char kKeyCalibrationMaxRelativeErrorTenthPercent[] = "cal_rel";
const char kKeyValveFullPower[] = "valve_s";
const char kKeyValveHoldDuty[] = "hold_pct";
const char kKeyDisplaySleep[] = "disp_s";
const char kKeyResultDisplay[] = "result_s";
const char kKeyLcdAddress[] = "lcd_addr";
const char kKeyBeep[] = "beep";
const char kKeyLcdSensorPage[] = "lcd_sensor_pg";
const char kKeyTemperatureSensor[] = "temp_sensor";
const char kKeyTdsSensor[] = "tds_sensor";
const char kKeyTdsTemperatureCompensation[] = "tds_temp_comp";

const Esp32BaseAppConfig::EnumOption kTemperatureSensorOptions[] = {
    {"none", "禁用"},
    {"ntc50k_b3950", "50K B3950 NTC"},
};

const Esp32BaseAppConfig::EnumOption kTdsSensorOptions[] = {
    {"none", "禁用"},
    {"tds_board_v1", "TDS Board V1.0"},
};

void copyError(char* error, std::size_t len, const char* text) {
    if (error && len > 0) {
        std::snprintf(error, len, "%s", text ? text : "");
    }
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

bool addCoreFields() {
    bool ok = true;
    ok = Esp32BaseAppConfig::addGroup({kGroupSafety, "安全限制"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupFlow, "流量保护"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupValve, "电磁阀"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupLocal, "本地交互"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupMetering, "计量"}) && ok;
    ok = Esp32BaseAppConfig::addGroup({kGroupSensors, "传感器"}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyConfirmTimeout, "确认页超时", static_cast<std::int32_t>(kDefaultConfirmTimeoutSec), 3, 60, 1, "s", "进入确认页后无操作自动取消。立即生效。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyMaxTime, "最大出水时长", static_cast<std::int32_t>(kDefaultMaxOutTimeSec), 30, 7200, 5, "s", "单次出水达到该时长强制关阀。立即生效。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyMaxMl, "最大出水量", static_cast<std::int32_t>(kDefaultMaxOutVolumeMl), 1000, 100000, 100, "ml", "单次出水达到该水量强制关阀。立即生效。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupSafety, kConfigNs, kKeyOverflow, "超量保护比例", kDefaultOverflowPercent, 1, 50, 1, "%", "超过目标量该比例后强制停止，防止过冲。", false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyNoFlow, "无流量判定超时", static_cast<std::int32_t>(kDefaultNoFlowTimeoutSec), 1, 30, 1, "s", "开阀后持续无脉冲超过该时间判为异常。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyHighFlow, "高流量阈值", static_cast<std::int32_t>(kDefaultHighFlowMlPerMin), 1000, 100000, 100, "ml/min", "实时流量高于该值会进入高流量观察。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyHighDuration, "高流量持续时间", static_cast<std::int32_t>(kDefaultHighFlowDurationSec), 1, 30, 1, "s", "高流量连续超过该时间才判为异常。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyPauseTimeout, "暂停保持时间", static_cast<std::int32_t>(kDefaultPauseTimeoutSec), 10, 3600, 10, "s", "暂停超过该时间自动结束本次出水。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyPulseMinIntervalUs, "有效脉冲最小间隔", static_cast<std::int32_t>(kDefaultPulseMinIntervalUs), static_cast<std::int32_t>(kMinPulseMinIntervalUs), static_cast<std::int32_t>(kMaxPulseMinIntervalUs), 100, "us", "有效脉冲间隔阈值；最大频率 = 1000000 / 当前值 Hz。", false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupValve, kConfigNs, kKeyValveFullPower, "阀门全功率时间", static_cast<std::int32_t>(kDefaultValveFullPowerSec), 1, 10, 1, "s", "开阀初段全功率吸合的持续时间。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupValve, kConfigNs, kKeyValveHoldDuty, "阀门保持占空比", kDefaultValveHoldDutyPercent, kMinValveHoldDutyPercent, kMaxValveHoldDutyPercent, 1, "%", "100% 为全压保持；低于 100% 时启用降功耗 PWM 保持。", false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyDisplaySleep, "LCD 熄屏时间", static_cast<std::int32_t>(kDefaultDisplaySleepSec), static_cast<std::int32_t>(kMinDisplaySleepSec), static_cast<std::int32_t>(kMaxDisplaySleepSec), 5, "s", "待机无操作超过该时间关闭背光。立即生效。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyResultDisplay, "结果页显示时间", static_cast<std::int32_t>(kDefaultResultDisplaySec), 0, 60, 1, "s", "出水结束后结果页停留时间，0 表示立即返回。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyVolumeStep, "容量步进", static_cast<std::int32_t>(kDefaultVolumeAdjustStepMl), static_cast<std::int32_t>(kMinVolumeAdjustStepMl), static_cast<std::int32_t>(kMaxVolumeAdjustStepMl), 10, "ml", "按键确认页容量步进；Web 表单不受影响。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyTimeStep, "时间步进", static_cast<std::int32_t>(kDefaultTimeAdjustStepSec), static_cast<std::int32_t>(kMinTimeAdjustStepSec), static_cast<std::int32_t>(kMaxTimeAdjustStepSec), 1, "s", "按键确认页时间步进；Web 表单不受影响。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupLocal, kConfigNs, kKeyLcdAddress, "LCD I2C 地址", kDefaultLcdI2cAddress, 0x03, 0x77, 1, nullptr, "保存后需重启，重启后重新探测 LCD。", true, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addBool({kGroupLocal, kConfigNs, kKeyBeep, "蜂鸣器提示音", true, "控制按键、完成和异常提示音。立即生效。", false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyCalibrationAnalysisPulseMinIntervalUs, "生成分析脉冲间隔", static_cast<std::int32_t>(kDefaultCalibrationAnalysisPulseMinIntervalUs), 0, static_cast<std::int32_t>(kMaxPulseMinIntervalUs), 100, "us", "0 使用样本记录间隔；非 0 按该间隔重新分析样本。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyPulseObservationWindowSec, "脉冲观察窗口", static_cast<std::int32_t>(kDefaultPulseObservationWindowSec), static_cast<std::int32_t>(kMinPulseObservationWindowSec), static_cast<std::int32_t>(kMaxPulseObservationWindowSec), 1, "s", "明细页观察启动阶段前几秒脉冲；不改变出水计量。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyCalibrationStableWindowSec, "生成稳态窗口", static_cast<std::int32_t>(kDefaultCalibrationStableWindowSec), static_cast<std::int32_t>(kMinCalibrationStableWindowSec), static_cast<std::int32_t>(kMaxCalibrationStableWindowSec), 1, "s", "识别稳定流量时要求连续满足条件的秒数。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyCalibrationStableTolerancePercent, "生成稳态容差", kDefaultCalibrationStableTolerancePercent, kMinCalibrationStableTolerancePercent, kMaxCalibrationStableTolerancePercent, 1, "%", "稳态窗口相对后半段稳定流量的允许偏差。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyCalibrationMinVolumeSpanMl, "生成最小容量跨度", static_cast<std::int32_t>(kDefaultCalibrationMinVolumeSpanMl), static_cast<std::int32_t>(kMinCalibrationMinVolumeSpanMl), static_cast<std::int32_t>(kMaxCalibrationMinVolumeSpanMl), 100, "ml", "参与生成的最大和最小实测容量至少需要拉开该距离。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyCalibrationMaxErrorMl, "生成最大拟合误差", static_cast<std::int32_t>(kDefaultCalibrationMaxErrorMl), static_cast<std::int32_t>(kMinCalibrationMaxErrorMl), static_cast<std::int32_t>(kMaxCalibrationMaxErrorMl), 10, "ml", "超过该绝对误差时作为样本质量提醒，仍允许生成结果。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addInt({kGroupMetering, kConfigNs, kKeyCalibrationMaxRelativeErrorTenthPercent, "生成最大相对误差", kDefaultCalibrationMaxRelativeErrorTenthPercent, kMinCalibrationMaxRelativeErrorTenthPercent, kMaxCalibrationMaxRelativeErrorTenthPercent, 1, "0.1%", "单位 0.1%；50 表示 5.0%。超过时作为样本质量提醒。", false, nullptr}) && ok;

    ok = Esp32BaseAppConfig::addBool({kGroupSensors, kConfigNs, kKeyLcdSensorPage, "LCD 待机显示水温/TDS", false, "开启后仅在待机页轮换显示传感器信息。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addEnum({kGroupSensors, kConfigNs, kKeyTemperatureSensor, "水温传感器", "none", kTemperatureSensorOptions, 2, "A1 水温探头；50K B3950 NTC，51K 上拉。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addEnum({kGroupSensors, kConfigNs, kKeyTdsSensor, "TDS 传感器", "none", kTdsSensorOptions, 2, "ADS1115 A2；TDS Board V1.0 模拟输出。", false, nullptr}) && ok;
    ok = Esp32BaseAppConfig::addBool({kGroupSensors, kConfigNs, kKeyTdsTemperatureCompensation, "TDS 温度补偿", true, "启用后使用当前水温补偿 TDS；无有效水温时按 25C 回退并记录标志。", false, nullptr}) && ok;

    return ok;
}

}  // namespace

void setFaucetAppConfigContext(const FaucetAppConfigContext& context) {
    g_context = context;
}

bool registerFaucetAppConfig() {
    Esp32BaseAppConfig::setTitle("出水系统参数");
    Esp32BaseAppConfig::setPageValidateCallback(validateAppConfigPage);
    Esp32BaseAppConfig::setChangeCallback(onAppConfigChange);
    Esp32BaseAppConfig::setSaveCallback(onAppConfigSave);
    return addCoreFields();
}

}  // namespace faucet

#endif
