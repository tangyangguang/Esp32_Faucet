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
    bool timeSynced;
    std::uint16_t bootId;
};

enum class LocalUiMode : std::uint8_t {
    Normal = 0,
    Result = 1,
};

struct AppSnapshot {
    WaterSnapshot water;
    ValveOutput valve;
    StatisticsRecord statistics;
    CalibrationSnapshot calibration;
    LocalUiMode localMode = LocalUiMode::Normal;
    std::uint32_t adjustmentStepMl = 500;
    std::uint32_t flowDroppedPulses = 0;
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
    void setFlowDroppedPulses(std::uint32_t droppedPulses);
    bool canApplyConfig() const;
    bool applyConfig(const SystemConfig& config);
    const SystemConfig& config() const;

private:
    void handleButtonEvent(ButtonEvent event,
                           std::uint32_t nowMs,
                           std::uint32_t nowSeconds,
                           bool timeSynced,
                           std::uint16_t bootId);
    void startSelectedPreset(std::uint32_t nowMs, std::uint32_t nowSeconds, bool timeSynced, std::uint16_t bootId);
    void exitResultDisplay(std::uint32_t nowMs);
    void toggleAdjustmentStep();
    void syncFlow(std::uint32_t nowUs);
    void syncValve(std::uint32_t nowMs);
    void processResult(std::uint32_t startTime, const PeriodKeys& periodKeys, bool startTimeSynced, std::uint16_t bootId);

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
    bool activeStartTimeSynced_;
    std::uint16_t activeStartBootId_;
    bool lastValveDesiredOpen_;
    bool calibrationValveOpen_;
    bool lastLogWriteOk_;
    bool persistenceDirty_;
    bool configDirty_;
    bool factoryResetRequested_;
    BeepPattern pendingBeep_;
    std::uint32_t flowDroppedPulses_;
    std::uint32_t resultDisplayStartMs_;
    std::uint32_t adjustmentStepMl_;
};

}  // namespace faucet
