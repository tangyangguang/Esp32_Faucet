#pragma once

#include "app/AppTypes.h"

#include <cstdint>

namespace faucet {

constexpr std::uint32_t kMinVolumePresetMl = 100;
constexpr std::uint32_t kMaxVolumePresetMl = 20000;
constexpr std::uint32_t kMinTimePresetSec = 5;
constexpr std::uint32_t kMaxTimePresetSec = 1800;

constexpr std::uint32_t kDefaultConfirmTimeoutSec = 60;
constexpr std::uint32_t kDefaultMaxOutTimeSec = 1800;
constexpr std::uint32_t kDefaultMaxOutVolumeMl = 30000;
constexpr std::uint8_t kDefaultOverflowPercent = 10;
constexpr std::uint32_t kDefaultNoFlowTimeoutSec = 10;
constexpr std::uint32_t kDefaultHighFlowMlPerMin = 30000;
constexpr std::uint32_t kDefaultHighFlowDurationSec = 5;
constexpr std::uint32_t kDefaultPauseTimeoutSec = 90;
constexpr std::uint32_t kDefaultVolumeAdjustStepMl = 100;
constexpr std::uint32_t kMinVolumeAdjustStepMl = 10;
constexpr std::uint32_t kMaxVolumeAdjustStepMl = 1000;
constexpr std::uint32_t kDefaultTimeAdjustStepSec = 10;
constexpr std::uint32_t kMinTimeAdjustStepSec = 1;
constexpr std::uint32_t kMaxTimeAdjustStepSec = 300;
constexpr std::uint32_t kDefaultPulseMinIntervalUs = 1000;
constexpr std::uint32_t kMinPulseMinIntervalUs = 100;
constexpr std::uint32_t kMaxPulseMinIntervalUs = 100000;
constexpr std::uint32_t kRecentPulseTraceCount = 1;
constexpr std::uint32_t kDefaultPulseObservationWindowSec = 10;
constexpr std::uint32_t kMinPulseObservationWindowSec = 1;
constexpr std::uint32_t kMaxPulseObservationWindowSec = 60;
constexpr std::uint32_t kDefaultCalibrationAnalysisPulseMinIntervalUs = 0;
constexpr std::uint32_t kMinCalibrationStableWindowSec = 2;
constexpr std::uint32_t kDefaultCalibrationStableWindowSec = 4;
constexpr std::uint32_t kMaxCalibrationStableWindowSec = 10;
constexpr std::uint8_t kMinCalibrationStableTolerancePercent = 10;
constexpr std::uint8_t kDefaultCalibrationStableTolerancePercent = 25;
constexpr std::uint8_t kMaxCalibrationStableTolerancePercent = 60;
constexpr std::uint32_t kMinCalibrationMinVolumeSpanMl = 500;
constexpr std::uint32_t kDefaultCalibrationMinVolumeSpanMl = 1000;
constexpr std::uint32_t kMaxCalibrationMinVolumeSpanMl = 10000;
constexpr std::uint32_t kMinCalibrationMaxErrorMl = 20;
constexpr std::uint32_t kDefaultCalibrationMaxErrorMl = 100;
constexpr std::uint32_t kMaxCalibrationMaxErrorMl = 1000;
constexpr std::uint16_t kMinCalibrationMaxRelativeErrorTenthPercent = 10;
constexpr std::uint16_t kDefaultCalibrationMaxRelativeErrorTenthPercent = 50;
constexpr std::uint16_t kMaxCalibrationMaxRelativeErrorTenthPercent = 200;
constexpr std::uint32_t kPulseTraceBucketMs = 500;
constexpr std::uint32_t kPulseTraceStartupDetailMs = 15000;
constexpr std::size_t kPulseTraceMaxBucketsPerTrace = 1200;
constexpr std::size_t kPulseTraceMaxStartupEdgesPerTrace = 4096;

constexpr std::uint32_t kDefaultValveFullPowerSec = 5;
constexpr std::uint8_t kDefaultValveHoldDutyPercent = 70;
constexpr std::uint8_t kMinValveHoldDutyPercent = 20;
constexpr std::uint8_t kMaxValveHoldDutyPercent = 100;
constexpr std::uint32_t kDefaultValvePwmFrequencyHz = 20000;
constexpr std::uint32_t kMinValvePwmFrequencyHz = 100;
constexpr std::uint32_t kMaxValvePwmFrequencyHz = 30000;

constexpr std::uint32_t kMinDisplaySleepSec = 60;
constexpr std::uint32_t kDefaultDisplaySleepSec = 60;
constexpr std::uint32_t kMaxDisplaySleepSec = 300;
constexpr std::uint32_t kDefaultResultDisplaySec = 30;
constexpr std::uint16_t kDefaultRecordPageSize = 10;
constexpr std::uint16_t kMaxRecordPageSize = 200;
constexpr std::uint32_t kDaysPerLifeMonth = 30;
constexpr std::uint32_t kMaxFilterLifeDays = 3650;
constexpr std::uint32_t kMaxFilterLifeMl = 10000000;
constexpr std::uint16_t kSensorVrefMv = 3300;
constexpr std::uint32_t kDefaultTemperatureNominalOhm = 50000;
constexpr std::uint32_t kDefaultTemperatureBeta = 3950;
constexpr std::uint32_t kDefaultTemperaturePullupOhm = 51000;
constexpr std::uint32_t kDefaultTdsDividerHighOhm = 10000;
constexpr std::uint32_t kDefaultTdsDividerLowOhm = 15000;
constexpr std::uint32_t kDefaultInputVoltageDividerHighOhm = 100000;
constexpr std::uint32_t kDefaultInputVoltageDividerLowOhm = 10000;
constexpr std::uint32_t kMinSensorResistanceOhm = 1000;
constexpr std::uint32_t kMaxSensorResistanceOhm = 1000000;
constexpr std::uint32_t kMinTemperatureBeta = 2000;
constexpr std::uint32_t kMaxTemperatureBeta = 6000;
constexpr std::uint8_t kInputVoltageCalibrationMaxPoints = 5;
constexpr std::int32_t kInputVoltageCalibrationDefaultGainPpm = 1000000;
constexpr std::int32_t kInputVoltageCalibrationMinGainPpm = 500000;
constexpr std::int32_t kInputVoltageCalibrationMaxGainPpm = 1500000;
constexpr std::int32_t kInputVoltageCalibrationMinOffsetMv = -5000;
constexpr std::int32_t kInputVoltageCalibrationMaxOffsetMv = 5000;

struct InputVoltageCalibrationPoint {
    std::int16_t adcRaw = 0;
    std::int16_t adcRawMin = 0;
    std::int16_t adcRawMax = 0;
    std::uint8_t adcRange = 0;
    std::uint32_t adcMillivolts = 0;
    std::uint32_t theoreticalInputMillivolts = 0;
    std::uint32_t actualInputMillivolts = 0;
    std::uint32_t capturedAt = 0;
};

struct InputVoltageCalibration {
    bool calibrated = false;
    std::uint8_t pointCount = 0;
    std::int32_t gainPpm = kInputVoltageCalibrationDefaultGainPpm;
    std::int32_t offsetMillivolts = 0;
    InputVoltageCalibrationPoint points[kInputVoltageCalibrationMaxPoints]{};
};

enum class FilterLifeStatus : std::uint8_t {
    Normal = 0,
    RecommendReplace = 1,
    Expired = 2,
};

enum class TemperatureKind : std::uint8_t {
    None = 0,
    NtcBeta = 1,
};

enum class TdsKind : std::uint8_t {
    None = 0,
    AnalogTdsAo = 1,
};

struct SystemConfig {
    std::uint32_t confirmTimeoutSec;
    std::uint32_t maxOutTimeSec;
    std::uint32_t maxOutVolumeMl;
    std::uint8_t overflowPercent;
    std::uint32_t noFlowTimeoutSec;
    std::uint32_t highFlowMlPerMin;
    std::uint32_t highFlowDurationSec;
    std::uint32_t pauseTimeoutSec;
    std::uint32_t volumeAdjustStepMl;
    std::uint32_t timeAdjustStepSec;
    std::uint32_t pulseMinIntervalUs;
    std::uint32_t valveFullPowerSec;
    std::uint8_t valveHoldDutyPercent;
    std::uint32_t valvePwmFrequencyHz;
    std::uint32_t displaySleepSec;
    std::uint32_t resultDisplaySec;
    bool beepEnabled;
    TemperatureKind temperatureKind;
    std::uint32_t temperatureNominalOhm;
    std::uint32_t temperatureBeta;
    std::uint32_t temperaturePullupOhm;
    std::int16_t temperatureOffsetCentiC;
    bool temperatureCalibrated;
    TdsKind tdsKind;
    std::uint32_t tdsDividerHighOhm;
    std::uint32_t tdsDividerLowOhm;
    float tdsScale;
    std::int16_t tdsOffsetPpm;
    bool tdsCalibrated;
    bool tdsTemperatureCompensationEnabled;
    std::uint32_t inputVoltageDividerHighOhm;
    std::uint32_t inputVoltageDividerLowOhm;
    InputVoltageCalibration inputVoltageCalibration;
    PresetConfig presets[kPresetCount];
    FilterConfig filters[kFilterCount];
};

inline bool temperatureSensorEnabled(const SystemConfig& config) {
    return config.temperatureKind != TemperatureKind::None;
}

inline bool tdsSensorEnabled(const SystemConfig& config) {
    return config.tdsKind != TdsKind::None;
}

SystemConfig makeDefaultConfig();
void sanitizeConfig(SystemConfig& config);

std::uint16_t sanitizeRecordPageSize(std::uint16_t pageSize);
FilterLifeStatus filterLifeStatus(const FilterRecord& filter, std::uint32_t usedDays);

}  // namespace faucet
