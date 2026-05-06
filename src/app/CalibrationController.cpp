#include "app/CalibrationController.h"

#include <algorithm>
#include <cmath>

namespace faucet {
namespace {

bool isFinitePositive(float value) {
    return std::isfinite(value) && value > 0.0f;
}

}  // namespace

CalibrationController::CalibrationController(float oldPulsePerMl) {
    reset(oldPulsePerMl);
}

void CalibrationController::reset(float oldPulsePerMl) {
    reset(oldPulsePerMl, kDefaultCalibrationTargetsMl);
}

void CalibrationController::reset(float oldPulsePerMl, const std::uint32_t (&targets)[kCalibrationTargetCount]) {
    state_ = CalibrationState::Ready;
    std::copy(targets, targets + kCalibrationTargetCount, targetsMl_);
    targetMl_ = firstEnabledCalibrationTarget(targetsMl_);
    sampleCount_ = 0;
    oldPulsePerMl_ = isFinitePositive(oldPulsePerMl) ? oldPulsePerMl : kDefaultPulsePerMl;
    proposedPulsePerMl_ = oldPulsePerMl_;
    lastSamplePulsePerMl_ = 0.0f;
    std::fill(samples_, samples_ + kMaxCalibrationSamples, 0.0f);
}

bool CalibrationController::setTargetMl(std::uint32_t targetMl) {
    if (!isEnabledCalibrationTarget(targetsMl_, targetMl) || state_ == CalibrationState::Sampling) {
        return false;
    }
    targetMl_ = targetMl;
    sampleCount_ = 0;
    proposedPulsePerMl_ = oldPulsePerMl_;
    lastSamplePulsePerMl_ = 0.0f;
    state_ = CalibrationState::Ready;
    std::fill(samples_, samples_ + kMaxCalibrationSamples, 0.0f);
    return true;
}

bool CalibrationController::beginSampling() {
    if (!isEnabledCalibrationTarget(targetsMl_, targetMl_) || sampleCount_ >= kMaxCalibrationSamples) {
        return false;
    }
    state_ = CalibrationState::Sampling;
    return true;
}

CalibrationSampleResult CalibrationController::finishSample(std::uint32_t pulseCount) {
    if (state_ != CalibrationState::Sampling) {
        return CalibrationSampleResult::InvalidPulseCount;
    }
    if (!isEnabledCalibrationTarget(targetsMl_, targetMl_)) {
        state_ = CalibrationState::Rejected;
        return CalibrationSampleResult::InvalidTarget;
    }
    if (pulseCount == 0) {
        state_ = CalibrationState::Rejected;
        return CalibrationSampleResult::InvalidPulseCount;
    }
    if (sampleCount_ >= kMaxCalibrationSamples) {
        state_ = CalibrationState::Rejected;
        return CalibrationSampleResult::TooManySamples;
    }

    const float candidate = static_cast<float>(pulseCount) / static_cast<float>(targetMl_);
    lastSamplePulsePerMl_ = candidate;

    if (!isFinitePositive(candidate) || candidate < kMinPulsePerMl || candidate > kMaxPulsePerMl) {
        state_ = CalibrationState::Rejected;
        return CalibrationSampleResult::InvalidPulseCount;
    }

    if (!samplesConsistent(candidate)) {
        state_ = CalibrationState::Rejected;
        return CalibrationSampleResult::Inconsistent;
    }

    samples_[sampleCount_] = candidate;
    ++sampleCount_;
    proposedPulsePerMl_ = averageWith(0.0f);
    state_ = sampleCount_ >= kMinCalibrationSamples ? CalibrationState::ReadyToSave : CalibrationState::NeedsMoreSamples;
    return CalibrationSampleResult::Accepted;
}

bool CalibrationController::canSave() const {
    return state_ == CalibrationState::ReadyToSave && sampleCount_ >= kMinCalibrationSamples;
}

float CalibrationController::proposedPulsePerMl() const {
    return proposedPulsePerMl_;
}

CalibrationSnapshot CalibrationController::snapshot() const {
    return CalibrationSnapshot{
        state_,
        targetMl_,
        sampleCount_,
        oldPulsePerMl_,
        proposedPulsePerMl_,
        lastSamplePulsePerMl_,
    };
}

bool CalibrationController::samplesConsistent(float candidate) const {
    if (sampleCount_ == 0) {
        return true;
    }
    const float average = averageWith(0.0f);
    const float allowed = average * (static_cast<float>(kCalibrationTolerancePercent) / 100.0f);
    return std::fabs(candidate - average) <= allowed;
}

float CalibrationController::averageWith(float candidate) const {
    float sum = 0.0f;
    std::uint8_t count = sampleCount_;
    for (std::uint8_t i = 0; i < sampleCount_; ++i) {
        sum += samples_[i];
    }
    if (candidate > 0.0f) {
        sum += candidate;
        ++count;
    }
    return count == 0 ? oldPulsePerMl_ : sum / static_cast<float>(count);
}

}  // namespace faucet
