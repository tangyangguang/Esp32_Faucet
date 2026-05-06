#pragma once

#include "app/AppConfig.h"

#include <cstdint>

namespace faucet {

constexpr std::uint8_t kMaxCalibrationSamples = 4;

enum class CalibrationState : std::uint8_t {
    Idle = 0,
    Ready = 1,
    Sampling = 2,
    NeedsMoreSamples = 3,
    ReadyToSave = 4,
    Rejected = 5,
};

enum class CalibrationSampleResult : std::uint8_t {
    Accepted = 0,
    InvalidTarget = 1,
    InvalidPulseCount = 2,
    TooManySamples = 3,
    Inconsistent = 4,
};

struct CalibrationSnapshot {
    CalibrationState state;
    std::uint32_t targetMl;
    std::uint8_t sampleCount;
    float oldPulsePerMl;
    float proposedPulsePerMl;
    float lastSamplePulsePerMl;
};

class CalibrationController {
public:
    explicit CalibrationController(float oldPulsePerMl = kDefaultPulsePerMl);

    void reset(float oldPulsePerMl);
    void reset(float oldPulsePerMl, const std::uint32_t (&targets)[kCalibrationTargetCount]);
    bool setTargetMl(std::uint32_t targetMl);
    bool beginSampling();
    CalibrationSampleResult finishSample(std::uint32_t pulseCount);
    bool canSave() const;
    float proposedPulsePerMl() const;
    CalibrationSnapshot snapshot() const;

private:
    bool samplesConsistent(float candidate) const;
    float averageWith(float candidate) const;

    CalibrationState state_;
    std::uint32_t targetMl_;
    std::uint32_t targetsMl_[kCalibrationTargetCount];
    std::uint8_t sampleCount_;
    float oldPulsePerMl_;
    float proposedPulsePerMl_;
    float lastSamplePulsePerMl_;
    float samples_[kMaxCalibrationSamples];
};

}  // namespace faucet
