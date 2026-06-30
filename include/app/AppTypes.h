#pragma once

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kPresetCount = 9;
constexpr std::size_t kFilterCount = 6;
constexpr std::size_t kNameLength = 16;
constexpr std::size_t kPresetNameLength = kNameLength;
constexpr std::size_t kFilterNameMaxChars = 30;
constexpr std::size_t kFilterNameLength = kFilterNameMaxChars * 3 + 1;
constexpr std::uint32_t kMinRealDateSeconds = 631152000UL;  // 2020-01-01 in seconds since 2000-01-01.

enum class PresetType : std::uint8_t {
    Volume = 0,
    Time = 1,
};

enum class WaterMode : std::uint8_t {
    Volume = 0,
    Time = 1,
};

enum class WaterResult : std::uint8_t {
    Completed = 0,
    StoppedByUser = 1,
    SafetyStopped = 2,
    FlowError = 3,
    PauseTimeout = 4,
};

inline bool waterResultAllowsCalibration(WaterResult result) {
    return result == WaterResult::Completed || result == WaterResult::StoppedByUser ||
           result == WaterResult::PauseTimeout;
}

struct PresetConfig {
    bool enabled;
    PresetType type;
    std::uint32_t value;
    char name[kPresetNameLength];
};

struct FilterRecord {
    bool enabled;
    char name[kFilterNameLength];
    std::uint32_t recommendDays;
    std::uint32_t maxDays;
    std::uint32_t lifeMl;
    std::uint32_t startTime;
    std::uint32_t usedMl;
    std::uint32_t startBootId;
};

struct MeteringParameters {
    constexpr MeteringParameters()
        : startupPulseCount(0),
          startupVolumeMl(0),
          stablePulsePerLiter(0),
          startupDurationMs(5000),
          stableFlowMlPerMin(1950) {}

    constexpr MeteringParameters(std::uint32_t startupPulseCountValue,
                                 std::uint32_t startupVolumeMlValue,
                                 std::uint32_t stablePulsePerLiterValue,
                                 std::uint32_t startupDurationMsValue = 5000,
                                 std::uint32_t stableFlowMlPerMinValue = 1950)
        : startupPulseCount(startupPulseCountValue),
          startupVolumeMl(startupVolumeMlValue),
          stablePulsePerLiter(stablePulsePerLiterValue),
          startupDurationMs(startupDurationMsValue),
          stableFlowMlPerMin(stableFlowMlPerMinValue) {}

    std::uint32_t startupPulseCount;
    std::uint32_t startupVolumeMl;
    std::uint32_t stablePulsePerLiter;
    std::uint32_t startupDurationMs;
    std::uint32_t stableFlowMlPerMin;
};

struct WaterRecord {
    std::uint32_t startTime;
    std::uint32_t startBootId;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint32_t pulseCount;
    std::uint32_t filteredPulseCount;
    std::uint32_t meteringSchemeId;
    std::uint16_t durationSec;
    std::int16_t temperatureCentiC;
    std::uint16_t tdsPpm;
    std::uint16_t sensorFlags;
    WaterMode mode;
    WaterResult result;
    std::uint8_t selectedPreset;
    std::uint8_t sensorSampleCount;
};

static_assert(sizeof(WaterRecord) == 40, "WaterRecord persistent layout changed; bump record file format.");

inline std::uint32_t waterRecordBootId(const WaterRecord& record) {
    return record.startBootId;
}

inline void markWaterRecordBootId(WaterRecord& record, std::uint32_t bootId) {
    record.startBootId = bootId;
}

inline void clearWaterRecordBootId(WaterRecord& record) {
    markWaterRecordBootId(record, 0);
}

inline bool waterRecordHasRealTime(const WaterRecord& record) {
    return record.startTime >= kMinRealDateSeconds && waterRecordBootId(record) == 0;
}

inline bool waterRecordHasBootRelativeTime(const WaterRecord& record) {
    return record.startTime > 0 && record.startTime < kMinRealDateSeconds && waterRecordBootId(record) != 0;
}

struct StatisticsRecord {
    std::uint32_t todayMl;
    std::uint32_t weekMl;
    std::uint32_t monthMl;
    std::uint32_t totalMl;
    std::uint32_t lastDayKey;
    std::uint32_t lastWeekKey;
    std::uint32_t lastMonthKey;
};

}  // namespace faucet
