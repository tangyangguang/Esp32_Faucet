#pragma once

#include "app/AdcReader.h"
#include "app/AppConfig.h"
#include "app/WaterSensors.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

struct WaterSensorRunSummary {
    std::int16_t temperatureAvgCentiC = 0;
    std::int16_t temperatureMinCentiC = 0;
    std::int16_t temperatureMaxCentiC = 0;
    std::uint16_t tdsAvgPpm = 0;
    std::uint16_t tdsMinPpm = 0;
    std::uint16_t tdsMaxPpm = 0;
    std::uint16_t tdsVoltageAvgMv = 0;
    std::uint16_t sensorSampleCount = 0;
    std::uint16_t sensorFlags = 0;
    std::uint16_t tdsCalibrationRevisionAtRun = 0;
    std::uint8_t tdsCalibrationModeAtRun = 0;
    std::uint8_t tdsCalibratedAtRun = 0;
    std::uint8_t tdsTemperatureCompensatedAtRun = 0;
    std::uint8_t tdsTempFallback25CAtRun = 0;
};

struct TdsCalibrationSessionSnapshot {
    bool active = false;
    bool readyToSave = false;
    bool failed = false;
    bool tempFallback25C = false;
    bool highReferenceLowWarning = false;
    bool hasPendingLowPoint = false;
    std::uint8_t sampleCount = 0;
    std::uint16_t referencePpm = 0;
    std::uint16_t rawAvgPpm = 0;
    std::uint16_t flags = 0;
};

class WaterSensorManager {
public:
    explicit WaterSensorManager(AdcReader& adc);

    void configure(const SystemConfig& config);
    bool begin();
    void tick(std::uint32_t nowMs);
    WaterSensorSnapshot snapshot() const;

    void beginRun();
    void sampleRun();
    WaterSensorRunSummary finishRun() const;

    bool startTdsSinglePointCalibration(std::uint16_t referencePpm,
                                        std::uint32_t nowSeconds);
    bool startTdsTwoPointLow(std::uint16_t lowReferencePpm,
                             std::uint32_t nowSeconds);
    bool startTdsTwoPointHigh(std::uint16_t highReferencePpm,
                              std::uint32_t nowSeconds);
    bool cancelTdsCalibration();
    TdsCalibrationSessionSnapshot calibrationSnapshot() const;
    bool saveReadyTdsCalibration(SystemConfig& config, std::uint32_t nowSeconds);

private:
    static constexpr std::size_t kCalibrationMaxSamples = 32;

    struct Accumulator {
        std::int32_t tempSum = 0;
        std::int16_t tempMin = 0;
        std::int16_t tempMax = 0;
        std::uint32_t tdsSum = 0;
        std::uint16_t tdsMin = 0;
        std::uint16_t tdsMax = 0;
        std::uint32_t voltageSum = 0;
        std::uint16_t count = 0;
        std::uint16_t flags = 0;
        bool fallback = false;
    };

    enum class CalibrationKind : std::uint8_t {
        None = 0,
        Single = 1,
        Low = 2,
        High = 3,
    };

    AdcReader& adc_;
    SystemConfig config_;
    WaterSensorSnapshot snapshot_;
    std::uint32_t lastSampleMs_;
    bool hasSampleTime_;
    std::uint8_t consecutiveFailureCycles_;
    std::uint8_t consecutiveSuccessCycles_;
    AdcRange tdsRange_;
    std::uint8_t tdsLowRangeWindows_;
    bool discardNextTdsSample_;
    Accumulator run_;

    CalibrationKind calibrationKind_;
    std::uint32_t calibrationStartedSeconds_;
    std::uint16_t calibrationReferencePpm_;
    std::uint16_t calibrationReadings_[kCalibrationMaxSamples];
    std::uint8_t calibrationSampleCount_;
    bool calibrationTempFallback_;
    bool calibrationFailed_;
    bool hasPendingLowPoint_;
    std::uint16_t pendingLowReferencePpm_;
    std::uint16_t pendingLowRawPpm_;
    float pendingScale_;
    std::int16_t pendingOffsetPpm_;

    void sampleOnce();
    void updateOfflineState(std::uint8_t failureCount);
    void updateTdsRange(std::uint16_t tdsVoltageMv);
    void accumulateCalibration(const TdsComputationResult& result);
    bool calibrationReady() const;
    std::uint16_t calibrationRawAverage() const;
    void accumulateRunSample(const WaterSensorSnapshot& current);
};

}  // namespace faucet
