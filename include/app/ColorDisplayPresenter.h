#pragma once

#include "app/AppController.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kColorDisplayTextLength = 32;
constexpr std::size_t kColorDisplayMetricCount = 4;
constexpr std::size_t kColorDisplaySensorCount = 2;
constexpr std::size_t kColorDisplayHintCount = 3;
constexpr std::size_t kColorDisplayTrendColumns = 96;
constexpr std::uint8_t kColorDisplayTrendHeight = 48;
constexpr std::uint8_t kColorDisplayTrendEmpty = 0;
constexpr std::uint8_t kColorDisplayTrendGap = 0x80;

enum class ColorDisplayPage : std::uint8_t {
    StandbyVolume = 0,
    StandbyTime = 1,
    ConfirmVolume = 2,
    ConfirmTime = 3,
    RunningVolume = 4,
    RunningTime = 5,
    PausedVolume = 6,
    PausedTime = 7,
    ResultCompleted = 8,
    ResultStopped = 9,
    Alert = 10,
    CalibrationReady = 11,
    ResultAlert = 12,
    Sleep = 13,
};

struct ColorDisplayMetric {
    char label[kColorDisplayTextLength];
    char value[kColorDisplayTextLength];
    char unit[12];
};

struct ColorDisplaySensor {
    char label[12];
    char value[12];
    char unit[12];
    char low[12];
    char high[12];
    // Vertical positions are encoded as 1..height, with the high bit of
    // 'top' marking a column that may contain missing samples.
    struct Column {
        std::uint8_t top;
        std::uint8_t bottom;
    } columns[kColorDisplayTrendColumns];
    std::uint8_t latestColumn;
    std::uint8_t latestY;
};

struct ColorDisplayFrame {
    ColorDisplayPage page;
    bool on;
    char state[kColorDisplayTextLength];
    char tag[kColorDisplayTextLength];
    char title[kColorDisplayTextLength];
    char status[kColorDisplayTextLength];
    char mainValue[kColorDisplayTextLength];
    char mainUnit[12];
    char subtitle[kColorDisplayTextLength];
    char timer[12];
    char trendTime[12];
    ColorDisplayMetric metrics[kColorDisplayMetricCount];
    std::uint8_t metricCount;
    ColorDisplaySensor sensors[kColorDisplaySensorCount];
    std::uint8_t sensorCount;
    char hints[kColorDisplayHintCount][kColorDisplayTextLength];
    std::uint8_t hintCount;
    std::uint16_t progressPermille;
};

class ColorDisplayPresenter {
public:
    explicit ColorDisplayPresenter(std::uint32_t sleepTimeoutSec = kDefaultDisplaySleepSec);

    void configure(std::uint32_t sleepTimeoutSec);
    void wake(std::uint32_t nowMs);
    void observe(const AppSnapshot& snapshot);
    ColorDisplayFrame render(const AppSnapshot& snapshot, std::uint32_t nowMs, bool networkOnline) const;

private:
    static constexpr std::size_t kTrendBuckets = 300;
    struct Range {
        std::uint16_t low = UINT16_MAX;
        std::uint16_t high = 0;

        bool valid() const { return low <= high; }
        void include(const Range& other);
    };
    struct Bucket {
        Range values[kColorDisplaySensorCount];
        std::uint8_t gaps = 0;
    };

    std::uint32_t sleepTimeoutMs_;
    std::uint32_t lastWakeMs_;
    std::uint32_t nextVolumeEstimateMs_ = 0;
    std::uint32_t volumeFinishEstimateMs_ = 0;
    std::int8_t volumeEstimateDirection_ = 0;
    WaterState lastWaterState_ = WaterState::Idle;
    std::uint32_t observedSequence_ = 0;
    std::uint32_t secondsPerBucket_ = 1;
    std::uint32_t lastSampleSec_ = 0;
    std::size_t bucketCount_ = 0;
    Bucket buckets_[kTrendBuckets]{};
    Range extrema_[kColorDisplaySensorCount]{};
    SensorValue latest_[kColorDisplaySensorCount]{};

    void observeVolumeEstimate(const AppSnapshot& snapshot);
    void resetTrends();
    void mergeTrendBuckets();
    void appendTrend(std::uint32_t elapsedSec, const WaterSensorSnapshot& sensors);
    void renderTrends(ColorDisplayFrame& frame, std::uint32_t elapsedSec) const;
    void renderWaterPage(ColorDisplayFrame& frame, const AppSnapshot& snapshot) const;
};

}  // namespace faucet
