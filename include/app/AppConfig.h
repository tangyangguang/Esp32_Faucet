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

enum class FilterLifeStatus : std::uint8_t {
    Normal = 0,
    RecommendReplace = 1,
    Expired = 2,
};

enum class TemperatureKind : std::uint8_t {
    None = 0,
    Ntc50kB3950 = 1,
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
    std::uint32_t displaySleepSec;
    std::uint32_t resultDisplaySec;
    bool beepEnabled;
    TemperatureKind temperatureKind;
    std::int16_t temperatureOffsetCentiC;
    bool temperatureCalibrated;
    TdsKind tdsKind;
    float tdsScale;
    std::int16_t tdsOffsetPpm;
    bool tdsCalibrated;
    bool tdsTemperatureCompensationEnabled;
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
