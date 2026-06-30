#pragma once

#include "app/AdcReader.h"
#include "app/AppConfig.h"
#include "app/WaterSensors.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

struct WaterSensorRunSummary {
    std::int16_t temperatureCentiC = 0;
    std::uint16_t tdsPpm = 0;
    std::uint8_t sensorSampleCount = 0;
    std::uint16_t sensorFlags = 0;
};

struct TdsCalibrationPointSnapshot {
    bool valid = false;
    bool tempFallback25C = false;
    std::uint16_t referencePpm = 0;
    std::uint16_t rawPpm = 0;
    std::uint16_t voltageMv = 0;
    std::int16_t temperatureCentiC = 0;
    std::uint32_t sampledAt = 0;
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
    bool sessionActive = false;
    bool samplingActive = false;
    bool candidateReady = false;
    bool full = false;
    std::uint8_t pointCount = 0;
    std::uint16_t rawAveragePpm = 0;
    std::uint16_t referenceSpanPpm = 0;
    std::uint16_t rawSpanPpm = 0;
    float candidateScale = 1.0f;
    std::int16_t candidateOffsetPpm = 0;
    TdsCalibrationPointSnapshot points[kTdsCalibrationMaxPoints]{};
};

class WaterSensorManager {
public:
    explicit WaterSensorManager(AdcReader& adc, bool sampleInputVoltage = true);

    void configure(const SystemConfig& config);
    bool begin();
    void tick(std::uint32_t nowMs);
    WaterSensorSnapshot snapshot() const;

    void beginRun();
    void sampleRun();
    WaterSensorRunSummary finishRun() const;

    TdsCalibrationSessionSnapshot calibrationSnapshot() const;
    bool startTdsCalibrationSession(std::uint32_t nowSeconds);
    bool startTdsCalibrationPoint(std::uint16_t referencePpm, std::uint32_t nowSeconds);
    bool saveStableTdsCalibrationPoint(std::uint32_t nowSeconds);
    bool removeTdsCalibrationPoint(std::uint8_t index, std::uint32_t nowSeconds);
    bool discardTdsCalibrationSession();
    bool expireTdsCalibrationSession(std::uint32_t nowSeconds);
    bool applyReadyTdsCalibration(SystemConfig& config, std::uint32_t nowSeconds);

private:
    static constexpr std::size_t kCalibrationMaxSamples = 32;
    static constexpr std::size_t kRunWindowSamples = 5;

    struct RunWindowSample {
        bool temperatureValid = false;
        bool tdsValid = false;
        std::int16_t temperatureCentiC = 0;
        std::uint16_t tdsPpm = 0;
    };

    struct RunWindow {
        RunWindowSample samples[kRunWindowSamples]{};
        std::uint8_t next = 0;
        std::uint8_t count = 0;
        std::uint16_t flags = 0;
    };

    enum class CalibrationKind : std::uint8_t {
        None = 0,
        TdsPoint = 1,
    };

    AdcReader& adc_;
    bool sampleInputVoltage_;
    SystemConfig config_;
    WaterSensorSnapshot snapshot_;
    std::uint32_t lastSampleMs_;
    bool hasSampleTime_;
    std::uint8_t consecutiveFailureCycles_;
    std::uint8_t consecutiveSuccessCycles_;
    AdcRange tdsRange_;
    std::uint8_t tdsLowRangeWindows_;
    bool discardNextTdsSample_;
    RunWindow run_;

    CalibrationKind calibrationKind_;
    std::uint16_t calibrationReferencePpm_;
    std::uint16_t calibrationReadings_[kCalibrationMaxSamples];
    std::uint8_t calibrationSampleCount_;
    bool calibrationTempFallback_;
    bool calibrationFailed_;
    bool tdsCalibrationSessionActive_;
    std::uint32_t tdsCalibrationUpdatedAt_;
    TdsCalibrationPointSnapshot tdsCalibrationPoints_[kTdsCalibrationMaxPoints];
    std::uint8_t tdsCalibrationPointCount_;
    TdsCalibrationFitResult tdsCalibrationFit_;

    void sampleOnce();
    void updateOfflineState(std::uint8_t failureCount);
    void updateTdsRange(std::uint16_t tdsVoltageMv);
    void accumulateCalibration(const TdsComputationResult& result);
    bool calibrationReady() const;
    std::uint16_t calibrationRawAverage() const;
    bool refreshTdsCalibrationCandidate();
    void accumulateRunSample(const WaterSensorSnapshot& current);
};

}  // namespace faucet
