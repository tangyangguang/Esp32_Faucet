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

struct WaterRecord {
    std::uint32_t startTime;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint32_t pulseCount;
    std::uint32_t rejectedPulseCount;
    std::uint16_t durationSec;
    WaterMode mode;
    WaterResult result;
    std::uint8_t selectedPreset;
    std::uint8_t reserved0;
    float pulsePerMlAtRun;
    std::uint8_t reserved[4];
};

inline std::uint32_t waterRecordBootId(const WaterRecord& record) {
    return static_cast<std::uint32_t>(record.reserved[0]) |
           (static_cast<std::uint32_t>(record.reserved[1]) << 8U) |
           (static_cast<std::uint32_t>(record.reserved[2]) << 16U) |
           (static_cast<std::uint32_t>(record.reserved[3]) << 24U);
}

inline void markWaterRecordBootId(WaterRecord& record, std::uint32_t bootId) {
    record.reserved[0] = static_cast<std::uint8_t>(bootId & 0xFFU);
    record.reserved[1] = static_cast<std::uint8_t>((bootId >> 8U) & 0xFFU);
    record.reserved[2] = static_cast<std::uint8_t>((bootId >> 16U) & 0xFFU);
    record.reserved[3] = static_cast<std::uint8_t>((bootId >> 24U) & 0xFFU);
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
