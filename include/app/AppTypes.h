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

enum class PresetType : std::uint8_t {
    Volume = 0,
    Time = 1,
};

enum class WaterMode : std::uint8_t {
    Volume = 0,
    Time = 1,
    Calibration = 2,
};

enum class WaterResult : std::uint8_t {
    Completed = 0,
    StoppedByUser = 1,
    SafetyStopped = 2,
    FlowError = 3,
    PauseTimeout = 4,
};

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
};

struct WaterLogRecord {
    std::uint32_t startTime;
    std::uint32_t volumeMl;
    std::uint16_t durationSec;
    WaterMode mode;
    WaterResult result;
    std::uint8_t reserved[2];
};

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
