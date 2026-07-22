#pragma once

#include "app/AppController.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kColorDisplayTextLength = 32;
constexpr std::size_t kColorDisplayMetricCount = 4;
constexpr std::size_t kColorDisplaySensorCount = 2;
constexpr std::size_t kColorDisplayHintCount = 3;
constexpr std::size_t kColorDisplayTrendSamples = 8;

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
    StandbyOffline = 13,
    Sleep = 14,
};

struct ColorDisplayMetric {
    char label[kColorDisplayTextLength];
    char value[kColorDisplayTextLength];
    char unit[12];
};

struct ColorDisplaySensor {
    char label[kColorDisplayTextLength];
    char value[kColorDisplayTextLength];
    char unit[12];
    std::uint16_t samples[kColorDisplayTrendSamples];
    std::uint8_t sampleCount;
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
    ColorDisplayFrame render(const AppSnapshot& snapshot, std::uint32_t nowMs, bool networkOnline);

private:
    std::uint32_t sleepTimeoutMs_;
    std::uint32_t lastWakeMs_;
    WaterState lastWaterState_;
    std::uint32_t lastTdsTrendSampleMs_;
    std::uint32_t lastTempTrendSampleMs_;
    std::uint16_t tdsTrend_[kColorDisplayTrendSamples];
    std::uint16_t tempTrend_[kColorDisplayTrendSamples];
    std::uint8_t tdsTrendCount_;
    std::uint8_t tempTrendCount_;

    void resetTrends();
    void sampleTrends(const AppSnapshot& snapshot, std::uint32_t nowMs);
};

}  // namespace faucet
