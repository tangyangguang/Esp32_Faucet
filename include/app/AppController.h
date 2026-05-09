#pragma once

#include "app/ButtonInput.h"
#include "app/BeepDriver.h"
#include "app/CalibrationController.h"
#include "app/FilterStore.h"
#include "app/FlowMeter.h"
#include "app/StatisticsStore.h"
#include "app/ValveDriver.h"
#include "app/WaterController.h"

#include <cstdint>

namespace faucet {

class AppLogWriter {
public:
    virtual ~AppLogWriter() = default;
    virtual bool append(const WaterLogRecord& record) = 0;
};

struct AppTickInput {
    ButtonLevels buttons;
    std::uint32_t nowMs;
    std::uint32_t nowUs;
    std::uint32_t nowSeconds;
    PeriodKeys periodKeys;
};

enum class LocalUiMode : std::uint8_t {
    Normal = 0,
    CalibrationSelect = 1,
    CalibrationConfirm = 2,
    CalibrationSampling = 3,
    CalibrationReview = 4,
    CalibrationRejected = 5,
    FactoryResetConfirm = 6,
};

struct AppSnapshot {
    WaterSnapshot water;
    ValveOutput valve;
    StatisticsRecord statistics;
    CalibrationSnapshot calibration;
    LocalUiMode localMode = LocalUiMode::Normal;
};

class AppController {
public:
    AppController(const SystemConfig& config,
                  StatisticsStore& statistics,
                  FilterStore& filters,
                  AppLogWriter& logs);

    void resetInputs(ButtonLevels levels, std::uint32_t nowMs);
    void onFlowPulse(std::uint32_t nowUs);
    void tick(const AppTickInput& input);

    AppSnapshot snapshot() const;
    bool lastLogWriteOk() const;
    bool consumePersistenceDirty();
    bool consumeConfigDirty();
    bool consumeFactoryResetRequest();
    BeepPattern consumeBeepPattern();
    bool emergencyStop(std::uint32_t nowMs);
    bool canApplyConfig() const;
    bool applyConfig(const SystemConfig& config);
    const SystemConfig& config() const;

private:
    void handleButtonEvent(ButtonEvent event, std::uint32_t nowMs, std::uint32_t nowSeconds);
    void handleCalibrationEvent(ButtonEvent event, std::uint32_t nowMs, std::uint32_t nowUs);
    void enterCalibration();
    void cycleCalibrationTarget();
    void finishCalibrationSample(std::uint32_t nowUs);
    void saveCalibration();
    void syncFlow(std::uint32_t nowUs);
    void syncValve(std::uint32_t nowMs);
    void processResult(std::uint32_t startTime, const PeriodKeys& periodKeys);

    SystemConfig config_;
    WaterController water_;
    CalibrationController calibration_;
    LocalUiMode localMode_;
    ButtonInput buttons_;
    FlowMeter flow_;
    ValveDriver valve_;
    StatisticsStore& statistics_;
    FilterStore& filters_;
    AppLogWriter& logs_;
    std::uint32_t lastFlowVolumeMl_;
    std::uint32_t activeStartTimeSec_;
    bool lastValveDesiredOpen_;
    bool calibrationValveOpen_;
    std::uint32_t calibrationStartMs_;
    bool lastLogWriteOk_;
    bool persistenceDirty_;
    bool configDirty_;
    bool factoryResetRequested_;
    std::uint32_t factoryConfirmArmedMs_;
    BeepPattern pendingBeep_;
};

}  // namespace faucet
