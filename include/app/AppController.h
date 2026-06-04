#pragma once

#include "app/ButtonInput.h"
#include "app/BeepDriver.h"
#include "app/FilterStore.h"
#include "app/FlowMeter.h"
#include "app/MeteringSchemeStore.h"
#include "app/StatisticsStore.h"
#include "app/ValveDriver.h"
#include "app/WaterController.h"
#include "app/WaterRecordCalibrationStore.h"
#include "app/WaterRecordMeteringSnapshotStore.h"
#include "app/WaterPulseTraceStore.h"

#include <cstdint>

namespace faucet {

class WaterRecordWriter {
public:
    virtual ~WaterRecordWriter() = default;
    virtual bool append(const WaterRecord& record) = 0;
};

struct AppTickInput {
    ButtonLevels buttons;
    std::uint32_t nowMs;
    std::uint32_t nowUs;
    std::uint32_t nowSeconds;
    PeriodKeys periodKeys;
    bool periodKeysValid;
    bool timeSynced;
    std::uint32_t bootId;
};

enum class LocalUiMode : std::uint8_t {
    Normal = 0,
    Result = 1,
    Calibration = 2,
};

struct AppSnapshot {
    WaterSnapshot water;
    ValveOutput valve;
    StatisticsRecord statistics;
    LocalUiMode localMode = LocalUiMode::Normal;
    std::uint32_t adjustmentStepMl = kDefaultVolumeAdjustStepMl;
    std::uint32_t timeAdjustmentStepSec = kDefaultTimeAdjustStepSec;
    std::uint32_t calibrationActualMl = 0;
    std::uint32_t calibrationStepMl = 100;
    bool calibrationReady = false;
    std::uint32_t pulsePerLiter = 0;
    MeteringParameters meteringParams{};
    std::uint32_t targetEstimatedDurationSec = 0;
    std::uint32_t selectedPresetEstimatedDurationSec = 0;
    std::uint32_t targetEstimatedVolumeMl = 0;
    std::uint32_t targetEstimatedPulseCount = 0;
    float targetStablePulsePerSec = 0.0f;
    const char* targetEstimateReason = nullptr;
    std::uint32_t selectedPresetEstimatedVolumeMl = 0;
    std::uint32_t selectedPresetEstimatedPulseCount = 0;
    float selectedPresetStablePulsePerSec = 0.0f;
    const char* selectedPresetEstimateReason = nullptr;
    std::uint32_t currentFlowMlPerMin = 0;
    std::uint32_t instantFlowMlPerMin = 0;
    std::uint32_t windowFlowMlPerMin = 0;
    std::uint32_t displayFlowMlPerMin = 0;
    std::uint32_t runAverageFlowMlPerMin = 0;
    std::uint32_t recentAverageFlowMlPerMin = 0;
    std::uint32_t flowDroppedPulses = 0;
    std::uint32_t maxLoopIntervalUs = 0;
    std::uint32_t maxAppTickUs = 0;
    std::uint32_t maxBaseHandleUs = 0;
};

enum class CalibrationApplyResult : std::uint8_t {
    Saved = 0,
    NotAvailable = 1,
    InvalidActual = 2,
    InvalidRecord = 3,
    InvalidFactor = 4,
    TooMuchDrift = 5,
};

class AppController {
public:
    AppController(const SystemConfig& config,
                  StatisticsStore& statistics,
                  FilterStore& filters,
                  WaterRecordWriter& records,
                  WaterPulseTraceStore* pulseTraces = nullptr,
                  WaterRecordCalibrationWriter* recordCalibrations = nullptr);
    AppController(const SystemConfig& config,
                  const MeteringSchemeRecord& activeScheme,
                  StatisticsStore& statistics,
                  FilterStore& filters,
                  WaterRecordWriter& records,
                  WaterRecordMeteringSnapshotWriter& meteringSnapshots,
                  MeteringSchemeStore& meteringSchemes,
                  WaterPulseTraceStore* pulseTraces = nullptr,
                  WaterRecordCalibrationWriter* recordCalibrations = nullptr);

    void resetInputs(ButtonLevels levels, std::uint32_t nowMs);
    void onFlowPulse(std::uint32_t nowUs);
    void tick(const AppTickInput& input);

    AppSnapshot snapshot() const;
    bool lastRecordWriteOk() const;
    bool consumePersistenceDirty();
    bool consumeConfigDirty();
    BeepPattern consumeBeepPattern();
    bool emergencyStop(std::uint32_t nowMs);
    void setFlowDroppedPulses(std::uint32_t droppedPulses);
    bool selectNextPresetForWeb();
    bool selectPreviousPresetForWeb();
    bool selectPresetForWeb(std::size_t index);
    bool canApplyConfig() const;
    bool applyConfig(const SystemConfig& config);
    bool applyActiveMeteringScheme(const MeteringSchemeRecord& activeScheme);
    CalibrationApplyResult applyCalibrationFromRecord(const WaterRecord& record, std::uint32_t actualMl);
    const SystemConfig& config() const;
    const MeteringSchemeRecord& activeMeteringScheme() const;

private:
    void handleButtonEvent(ButtonEvent event,
                           std::uint32_t nowMs,
                           std::uint32_t nowUs,
                           std::uint32_t nowSeconds,
                           bool timeSynced,
                           std::uint32_t bootId);
    void startSelectedPreset(std::uint32_t nowMs,
                             std::uint32_t nowUs,
                             std::uint32_t nowSeconds,
                             bool timeSynced,
                             std::uint32_t bootId);
    void exitResultDisplay(std::uint32_t nowMs);
    void toggleCalibrationStep();
    void enterCalibrationFromResult(std::uint32_t nowMs);
    void updateResultCalibrationHold(bool okPressed, std::uint32_t nowMs);
    bool adjustCalibrationActual(std::int32_t deltaMl);
    CalibrationApplyResult saveLocalCalibration(std::uint32_t nowSeconds);
    CalibrationApplyResult applyCalibrationFromRecordInternal(const WaterRecord& record,
                                                              std::uint32_t actualMl,
                                                              bool allowLocalCalibration,
                                                              std::uint32_t calibratedAt);
    void syncFlow(std::uint32_t nowUs);
    void finishPulseTrace(const WaterRecord& record,
                          WaterPulseTraceState finalState,
                          const FlowSnapshot& flow,
                          std::uint32_t nowUs);
    void syncValve(std::uint32_t nowMs);
    void processResult(std::uint32_t startTime,
                       const PeriodKeys& periodKeys,
                       bool periodKeysValid,
                       bool startTimeSynced,
                       std::uint32_t bootId,
                       std::uint32_t nowSeconds,
                       const FlowSnapshot& flow,
                       std::uint32_t nowUs);

    SystemConfig config_;
    MeteringSchemeRecord activeMeteringScheme_;
    WaterController water_;
    LocalUiMode localMode_;
    ButtonInput buttons_;
    FlowMeter flow_;
    ValveDriver valve_;
    StatisticsStore& statistics_;
    FilterStore& filters_;
    WaterRecordWriter& records_;
    WaterRecordCalibrationWriter* recordCalibrations_;
    WaterRecordMeteringSnapshotWriter* meteringSnapshots_;
    MeteringSchemeStore* meteringSchemes_;
    WaterPulseTraceStore* pulseTraces_;
    std::uint32_t activeTraceId_;
    std::uint32_t activeTraceStartUs_;
    std::uint32_t lastFlowVolumeMl_;
    std::uint32_t currentFlowMlPerMin_;
    std::uint32_t instantFlowMlPerMin_;
    std::uint32_t windowFlowMlPerMin_;
    std::uint32_t displayFlowMlPerMin_;
    std::uint32_t runAverageFlowMlPerMin_;
    std::uint32_t activeStartTimeSec_;
    bool activeStartTimeSynced_;
    std::uint32_t activeStartBootId_;
    bool lastValveDesiredOpen_;
    bool calibrationValveOpen_;
    bool lastRecordWriteOk_;
    bool persistenceDirty_;
    bool configDirty_;
    BeepPattern pendingBeep_;
    std::uint32_t flowDroppedPulses_;
    std::uint32_t resultDisplayStartMs_;
    std::uint32_t adjustmentStepMl_;
    std::uint32_t timeAdjustmentStepSec_;
    bool lastResultRecordValid_;
    WaterRecord lastResultRecord_;
    bool resultOkHoldTracking_;
    bool resultOkHoldTriggered_;
    std::uint32_t resultOkHoldStartMs_;
    std::uint32_t calibrationActualMl_;
    std::uint32_t calibrationStepMl_;
    bool calibrationIgnoreOkUntilReleased_;
};

}  // namespace faucet
