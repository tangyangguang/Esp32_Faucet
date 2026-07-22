#include "app/WaterSensorManager.h"

#include <algorithm>

namespace faucet {
namespace {

constexpr std::uint32_t kSampleIntervalMs = 1000;
constexpr std::uint8_t kOfflineThreshold = 3;
constexpr std::uint8_t kRecoveryThreshold = 3;
constexpr std::uint8_t kTdsDownshiftWindows = 8;
constexpr std::uint8_t kCalibrationMinSamples = 12;
constexpr std::uint32_t kTdsCalibrationSessionTimeoutSec = 30UL * 60UL;
constexpr std::uint16_t kTemperatureShortThresholdMv = 50;
// The divider is powered by the nominal 3.3 V rail. Keep enough margin for
// regulator tolerance while remaining far above the valid liquid-water range.
constexpr std::uint16_t kTemperatureOpenThresholdMv = 3000;
constexpr std::uint32_t kInputVoltageCalibrationMinMv = 1000;
constexpr std::uint32_t kInputVoltageCalibrationMaxMv = 60000;
constexpr std::uint32_t kInputVoltageCalibrationMinSpanMv = 1000;
constexpr std::uint32_t kInputVoltageStableMinimumToleranceMv = 10;

std::uint16_t toU16(std::int32_t value) {
    if (value <= 0) {
        return 0;
    }
    return value > 65535 ? 65535 : static_cast<std::uint16_t>(value);
}

std::int16_t toI16(std::int32_t value) {
    if (value < -32768) {
        return -32768;
    }
    if (value > 32767) {
        return 32767;
    }
    return static_cast<std::int16_t>(value);
}

bool enabledTemperature(const SystemConfig& config) {
    return config.temperatureKind == TemperatureKind::NtcBeta;
}

bool enabledTds(const SystemConfig& config) {
    return config.tdsKind == TdsKind::AnalogTdsAo;
}

std::int32_t applyInputVoltageCalibration(std::int32_t theoreticalMv,
                                          const InputVoltageCalibration& calibration) {
    const std::int64_t scaled =
        (static_cast<std::int64_t>(theoreticalMv) * calibration.gainPpm + 500000LL) / 1000000LL;
    const std::int64_t calibrated = scaled + calibration.offsetMillivolts;
    return static_cast<std::int32_t>(
        std::max<std::int64_t>(0, std::min<std::int64_t>(INT32_MAX, calibrated)));
}

}  // namespace

WaterSensorManager::WaterSensorManager(AdcReader& adc, bool sampleInputVoltage)
    : adc_(adc),
      sampleInputVoltage_(sampleInputVoltage),
      config_(makeDefaultConfig()),
      snapshot_{},
      lastSampleMs_(0),
      hasSampleTime_(false),
      consecutiveFailureCycles_(0),
      consecutiveSuccessCycles_(0),
      tdsRange_(AdcRange::P256),
      tdsLowRangeWindows_(0),
      discardNextTdsSample_(false),
      run_{},
      sampleSequence_(0),
      lastRunSampleSequence_(0),
      calibrationKind_(CalibrationKind::None),
      calibrationReferencePpm_(0),
      calibrationReadings_{},
      calibrationSampleCount_(0),
      calibrationTempFallback_(false),
      calibrationFailed_(false),
      tdsCalibrationSessionActive_(false),
      tdsCalibrationUpdatedAt_(0),
      tdsCalibrationPoints_{},
      tdsCalibrationPointCount_(0),
      tdsCalibrationFit_{} {}

void WaterSensorManager::configure(const SystemConfig& config) {
    const bool dividerChanged = config_.inputVoltageDividerHighOhm != config.inputVoltageDividerHighOhm ||
                                config_.inputVoltageDividerLowOhm != config.inputVoltageDividerLowOhm;
    const bool tdsHardwareChanged = config_.tdsKind != config.tdsKind ||
                                    config_.tdsDividerHighOhm != config.tdsDividerHighOhm ||
                                    config_.tdsDividerLowOhm != config.tdsDividerLowOhm;
    config_ = config;
    sanitizeConfig(config_);
    if (dividerChanged) {
        inputVoltageWindowNext_ = 0;
        inputVoltageWindowCount_ = 0;
        snapshot_.inputVoltageMv = {};
        snapshot_.inputVoltageTheoreticalMv = {};
        snapshot_.inputVoltageAdcMv = {};
        snapshot_.inputVoltageAdcRaw = {};
        snapshot_.inputVoltageWindowSpanMv = 0;
        snapshot_.inputVoltageSampleCount = 0;
        snapshot_.inputVoltageStable = false;
    }
    if (tdsHardwareChanged) {
        resetTdsRawWindow();
        discardTdsCalibrationSession();
    }
    refreshInputVoltageSnapshotCalibration();
}

bool WaterSensorManager::begin() {
    const bool ready = adc_.begin();
    if (sampleInputVoltage_) {
        adc_.setRange(AdcChannel::A0, AdcRange::P4096);
    }
    adc_.setRange(AdcChannel::A1, AdcRange::P4096);
    adc_.setRange(AdcChannel::A2, tdsRange_);
    return ready;
}

void WaterSensorManager::tick(std::uint32_t nowMs) {
    if (hasSampleTime_ && nowMs - lastSampleMs_ < kSampleIntervalMs) {
        return;
    }
    lastSampleMs_ = nowMs;
    hasSampleTime_ = true;
    sampleOnce();
}

WaterSensorSnapshot WaterSensorManager::snapshot() const {
    return snapshot_;
}

void WaterSensorManager::beginRun() {
    run_ = {};
    lastRunSampleSequence_ = sampleSequence_;
}

void WaterSensorManager::sampleRun() {
    if (lastRunSampleSequence_ == sampleSequence_) {
        return;
    }
    accumulateRunSample(snapshot_);
    lastRunSampleSequence_ = sampleSequence_;
}

WaterSensorRunSummary WaterSensorManager::finishRun() const {
    WaterSensorRunSummary summary{};
    summary.sensorFlags = run_.flags;
    std::int32_t tempSum = 0;
    std::uint32_t tdsSum = 0;
    std::uint8_t tempCount = 0;
    std::uint8_t tdsCount = 0;
    for (std::uint8_t i = 0; i < run_.count; ++i) {
        const RunWindowSample& sample = run_.samples[i];
        if (sample.temperatureValid) {
            tempSum += sample.temperatureCentiC;
            ++tempCount;
        }
        if (sample.tdsValid) {
            tdsSum += sample.tdsPpm;
            ++tdsCount;
        }
    }
    summary.sensorSampleCount = std::max(tempCount, tdsCount);
    if (tempCount > 0) {
        summary.temperatureCentiC = toI16(tempSum / tempCount);
    } else {
        summary.sensorFlags |= kWaterSensorFlagTempUnavailable;
        if (enabledTemperature(config_)) {
            summary.sensorFlags |= kWaterSensorFlagTempInvalid;
        }
    }
    if (tdsCount > 0) {
        summary.tdsPpm = static_cast<std::uint16_t>(tdsSum / tdsCount);
    } else {
        summary.sensorFlags |= kWaterSensorFlagTdsUnavailable;
        if (enabledTds(config_)) {
            summary.sensorFlags |= kWaterSensorFlagTdsInvalid;
        }
    }
    return summary;
}

TdsCalibrationSessionSnapshot WaterSensorManager::calibrationSnapshot() const {
    TdsCalibrationSessionSnapshot session{};
    session.samplingActive = calibrationKind_ != CalibrationKind::None;
    session.failed = calibrationFailed_;
    session.tempFallback25C = calibrationTempFallback_;
    session.sampleCount = calibrationSampleCount_;
    session.referencePpm = calibrationReferencePpm_;
    session.rawAvgPpm = calibrationRawMedian();
    session.flags = snapshot_.flags;
    session.readyToSave = calibrationReady();
    session.sessionActive = tdsCalibrationSessionActive_;
    session.pointCount = tdsCalibrationPointCount_;
    session.full = tdsCalibrationPointCount_ >= kTdsCalibrationMaxPoints;
    session.candidateReady = tdsCalibrationFit_.valid;
    session.referenceSpanPpm = tdsCalibrationFit_.referenceSpanPpm;
    session.rawSpanPpm = tdsCalibrationFit_.rawSpanPpm;
    session.candidateScale = tdsCalibrationFit_.scale;
    session.candidateOffsetPpm = tdsCalibrationFit_.offsetPpm;
    for (std::uint8_t i = 0; i < tdsCalibrationPointCount_; ++i) {
        session.points[i] = tdsCalibrationPoints_[i];
    }
    return session;
}

bool WaterSensorManager::startTdsCalibrationSession(std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || tdsCalibrationSessionActive_ || calibrationKind_ != CalibrationKind::None) {
        return false;
    }
    tdsCalibrationSessionActive_ = true;
    tdsCalibrationUpdatedAt_ = nowSeconds;
    for (auto& point : tdsCalibrationPoints_) {
        point = {};
    }
    tdsCalibrationPointCount_ = 0;
    tdsCalibrationFit_ = {};
    calibrationFailed_ = false;
    calibrationTempFallback_ = false;
    return true;
}

bool WaterSensorManager::startTdsCalibrationPoint(std::uint16_t referencePpm, std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || !tdsCalibrationSessionActive_ || calibrationKind_ != CalibrationKind::None ||
        tdsCalibrationPointCount_ >= kTdsCalibrationMaxPoints || referencePpm > 2000) {
        return false;
    }
    calibrationKind_ = CalibrationKind::TdsPoint;
    calibrationReferencePpm_ = referencePpm;
    calibrationSampleCount_ = 0;
    calibrationTempFallback_ = false;
    calibrationFailed_ = false;
    tdsCalibrationUpdatedAt_ = nowSeconds;
    return true;
}

bool WaterSensorManager::saveStableTdsCalibrationPoint(std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || !calibrationReady() || tdsCalibrationPointCount_ >= kTdsCalibrationMaxPoints) {
        return false;
    }

    TdsCalibrationPointSnapshot point{};
    point.referencePpm = calibrationReferencePpm_;
    point.rawPpm = calibrationRawMedian();
    point.voltageMv = snapshot_.tdsVoltageMv.valid ? toU16(snapshot_.tdsVoltageMv.value) : 0;
    tdsCalibrationPoints_[tdsCalibrationPointCount_++] = point;

    calibrationKind_ = CalibrationKind::None;
    calibrationSampleCount_ = 0;
    calibrationReferencePpm_ = 0;
    calibrationFailed_ = false;
    calibrationTempFallback_ = false;
    tdsCalibrationUpdatedAt_ = nowSeconds;
    refreshTdsCalibrationCandidate();
    return true;
}

bool WaterSensorManager::removeTdsCalibrationPoint(std::uint8_t index, std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || calibrationKind_ != CalibrationKind::None || index >= tdsCalibrationPointCount_) {
        return false;
    }
    for (std::uint8_t i = index; i + 1U < tdsCalibrationPointCount_; ++i) {
        tdsCalibrationPoints_[i] = tdsCalibrationPoints_[i + 1U];
    }
    --tdsCalibrationPointCount_;
    tdsCalibrationPoints_[tdsCalibrationPointCount_] = {};
    tdsCalibrationUpdatedAt_ = nowSeconds;
    refreshTdsCalibrationCandidate();
    return true;
}

bool WaterSensorManager::updateTdsCalibrationPoint(std::uint8_t index,
                                                   std::uint16_t referencePpm,
                                                   std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || calibrationKind_ != CalibrationKind::None ||
        index >= tdsCalibrationPointCount_ || referencePpm > 2000) {
        return false;
    }
    const std::uint16_t previous = tdsCalibrationPoints_[index].referencePpm;
    tdsCalibrationPoints_[index].referencePpm = referencePpm;
    if (!refreshTdsCalibrationCandidate()) {
        tdsCalibrationPoints_[index].referencePpm = previous;
        refreshTdsCalibrationCandidate();
        return false;
    }
    tdsCalibrationUpdatedAt_ = nowSeconds;
    return true;
}

bool WaterSensorManager::discardTdsCalibrationSession() {
    const bool wasActive = tdsCalibrationSessionActive_ || calibrationKind_ != CalibrationKind::None;
    tdsCalibrationSessionActive_ = false;
    tdsCalibrationUpdatedAt_ = 0;
    for (auto& point : tdsCalibrationPoints_) {
        point = {};
    }
    tdsCalibrationPointCount_ = 0;
    tdsCalibrationFit_ = {};
    calibrationKind_ = CalibrationKind::None;
    calibrationSampleCount_ = 0;
    calibrationFailed_ = false;
    calibrationTempFallback_ = false;
    return wasActive;
}

bool WaterSensorManager::expireTdsCalibrationSession(std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || nowSeconds < tdsCalibrationUpdatedAt_ ||
        nowSeconds - tdsCalibrationUpdatedAt_ < kTdsCalibrationSessionTimeoutSec) {
        return false;
    }
    return discardTdsCalibrationSession();
}

bool WaterSensorManager::applyReadyTdsCalibration(SystemConfig& config) {
    if (!tdsCalibrationSessionActive_ || !tdsCalibrationFit_.valid || tdsCalibrationPointCount_ == 0) {
        return false;
    }
    config.tdsScale = tdsCalibrationFit_.scale;
    config.tdsOffsetPpm = tdsCalibrationFit_.offsetPpm;
    config.tdsCalibrated = true;
    sanitizeConfig(config);
    configure(config);
    return discardTdsCalibrationSession();
}

void WaterSensorManager::addInputVoltageSample(const AdcReadResult& input) {
    inputVoltageRawWindow_[inputVoltageWindowNext_] = input.raw;
    inputVoltageAdcMvWindow_[inputVoltageWindowNext_] =
        static_cast<std::uint16_t>(std::max<std::int16_t>(0, input.millivolts));
    inputVoltageWindowNext_ = static_cast<std::uint8_t>((inputVoltageWindowNext_ + 1U) % kInputVoltageWindowSamples);
    if (inputVoltageWindowCount_ < kInputVoltageWindowSamples) {
        ++inputVoltageWindowCount_;
    }
}

bool WaterSensorManager::summarizeInputVoltageWindow(std::uint32_t dividerHighOhm,
                                                     std::uint32_t dividerLowOhm,
                                                     std::int16_t& rawMedian,
                                                     std::uint16_t& adcMvMedian,
                                                     std::uint32_t& theoreticalMedianMv,
                                                     std::uint32_t& theoreticalSpanMv) const {
    if (inputVoltageWindowCount_ == 0) {
        return false;
    }
    std::int16_t raw[kInputVoltageWindowSamples]{};
    std::uint16_t adcMv[kInputVoltageWindowSamples]{};
    std::copy(inputVoltageRawWindow_, inputVoltageRawWindow_ + inputVoltageWindowCount_, raw);
    std::copy(inputVoltageAdcMvWindow_, inputVoltageAdcMvWindow_ + inputVoltageWindowCount_, adcMv);
    std::sort(raw, raw + inputVoltageWindowCount_);
    std::sort(adcMv, adcMv + inputVoltageWindowCount_);
    const std::uint8_t middle = inputVoltageWindowCount_ / 2U;
    rawMedian = raw[middle];
    adcMvMedian = adcMv[middle];
    theoreticalMedianMv =
        inputVoltageMvFromAdcRaw(rawMedian,
                                 adcRangeFullScaleMv(AdcRange::P4096),
                                 dividerHighOhm,
                                 dividerLowOhm);
    const std::uint32_t low =
        inputVoltageMvFromAdcRaw(raw[0],
                                 adcRangeFullScaleMv(AdcRange::P4096),
                                 dividerHighOhm,
                                 dividerLowOhm);
    const std::uint32_t high =
        inputVoltageMvFromAdcRaw(raw[inputVoltageWindowCount_ - 1U],
                                 adcRangeFullScaleMv(AdcRange::P4096),
                                 dividerHighOhm,
                                 dividerLowOhm);
    theoreticalSpanMv = high >= low ? high - low : 0;
    return true;
}

void WaterSensorManager::refreshInputVoltageSnapshotCalibration() {
    if (!snapshot_.inputVoltageTheoreticalMv.valid) {
        return;
    }
    snapshot_.inputVoltageMv.valid = true;
    snapshot_.inputVoltageMv.value =
        applyInputVoltageCalibration(snapshot_.inputVoltageTheoreticalMv.value,
                                     config_.inputVoltageCalibration);
    snapshot_.inputVoltageCalibrated = config_.inputVoltageCalibration.calibrated;
}

bool WaterSensorManager::refreshInputVoltageCalibration(InputVoltageCalibration& calibration) const {
    if (calibration.pointCount == 0) {
        calibration = {};
        return true;
    }

    std::sort(calibration.points,
              calibration.points + calibration.pointCount,
              [](const InputVoltageCalibrationPoint& a, const InputVoltageCalibrationPoint& b) {
                  return a.theoreticalInputMillivolts < b.theoreticalInputMillivolts;
              });

    double gain = 1.0;
    double offset = 0.0;
    if (calibration.pointCount == 1) {
        const InputVoltageCalibrationPoint& point = calibration.points[0];
        if (point.theoreticalInputMillivolts == 0) {
            return false;
        }
        gain = static_cast<double>(point.actualInputMillivolts) /
               static_cast<double>(point.theoreticalInputMillivolts);
    } else {
        const std::uint32_t span =
            calibration.points[calibration.pointCount - 1U].theoreticalInputMillivolts -
            calibration.points[0].theoreticalInputMillivolts;
        if (span < kInputVoltageCalibrationMinSpanMv) {
            return false;
        }
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        for (std::uint8_t i = 0; i < calibration.pointCount; ++i) {
            const double x = calibration.points[i].theoreticalInputMillivolts;
            const double y = calibration.points[i].actualInputMillivolts;
            sumX += x;
            sumY += y;
            sumXX += x * x;
            sumXY += x * y;
        }
        const double n = calibration.pointCount;
        const double denominator = n * sumXX - sumX * sumX;
        if (denominator <= 0.0) {
            return false;
        }
        gain = (n * sumXY - sumX * sumY) / denominator;
        offset = (sumY - gain * sumX) / n;
    }

    const std::int32_t gainPpm = static_cast<std::int32_t>(gain * 1000000.0 + 0.5);
    const std::int32_t offsetMv = static_cast<std::int32_t>(offset >= 0.0 ? offset + 0.5 : offset - 0.5);
    if (gainPpm < kInputVoltageCalibrationMinGainPpm || gainPpm > kInputVoltageCalibrationMaxGainPpm ||
        offsetMv < kInputVoltageCalibrationMinOffsetMv || offsetMv > kInputVoltageCalibrationMaxOffsetMv) {
        return false;
    }
    for (std::uint8_t i = 0; i < calibration.pointCount; ++i) {
        const InputVoltageCalibrationPoint& point = calibration.points[i];
        const double predicted = gain * point.theoreticalInputMillivolts + offset;
        const double error = std::abs(predicted - point.actualInputMillivolts);
        const double allowed = std::max(50.0, point.actualInputMillivolts * 0.005);
        if (calibration.pointCount >= 3 && error > allowed) {
            return false;
        }
    }
    calibration.gainPpm = gainPpm;
    calibration.offsetMillivolts = offsetMv;
    calibration.calibrated = true;
    return true;
}

bool WaterSensorManager::saveInputVoltageCalibrationPoint(SystemConfig& config,
                                                          std::uint32_t actualMillivolts,
                                                          std::uint32_t nowSeconds) {
    if (!sampleInputVoltage_ || inputVoltageWindowCount_ < kInputVoltageWindowSamples ||
        actualMillivolts < kInputVoltageCalibrationMinMv || actualMillivolts > kInputVoltageCalibrationMaxMv ||
        config.inputVoltageCalibration.pointCount >= kInputVoltageCalibrationMaxPoints) {
        return false;
    }
    std::int16_t rawMedian = 0;
    std::uint16_t adcMvMedian = 0;
    std::uint32_t theoreticalMedianMv = 0;
    std::uint32_t theoreticalSpanMv = 0;
    if (!summarizeInputVoltageWindow(config.inputVoltageDividerHighOhm,
                                     config.inputVoltageDividerLowOhm,
                                     rawMedian,
                                     adcMvMedian,
                                     theoreticalMedianMv,
                                     theoreticalSpanMv) ||
        theoreticalSpanMv >
            std::max<std::uint32_t>(kInputVoltageStableMinimumToleranceMv, theoreticalMedianMv / 1000U)) {
        return false;
    }
    InputVoltageCalibration candidate = config.inputVoltageCalibration;
    InputVoltageCalibrationPoint& point = candidate.points[candidate.pointCount];
    std::int16_t sortedRaw[kInputVoltageWindowSamples]{};
    std::copy(inputVoltageRawWindow_, inputVoltageRawWindow_ + kInputVoltageWindowSamples, sortedRaw);
    std::sort(sortedRaw, sortedRaw + kInputVoltageWindowSamples);
    point.adcRaw = rawMedian;
    point.adcRawMin = sortedRaw[0];
    point.adcRawMax = sortedRaw[kInputVoltageWindowSamples - 1U];
    point.adcRange = static_cast<std::uint8_t>(AdcRange::P4096);
    point.adcMillivolts = adcMvMedian;
    point.theoreticalInputMillivolts = theoreticalMedianMv;
    point.actualInputMillivolts = actualMillivolts;
    point.capturedAt = nowSeconds;
    for (std::uint8_t i = 0; i < candidate.pointCount; ++i) {
        const std::uint32_t existing = candidate.points[i].theoreticalInputMillivolts;
        const std::uint32_t difference = existing > point.theoreticalInputMillivolts
                                             ? existing - point.theoreticalInputMillivolts
                                             : point.theoreticalInputMillivolts - existing;
        if (difference < kInputVoltageCalibrationMinSpanMv) {
            return false;
        }
    }
    ++candidate.pointCount;
    if (!refreshInputVoltageCalibration(candidate)) {
        return false;
    }
    config.inputVoltageCalibration = candidate;
    configure(config);
    return true;
}

bool WaterSensorManager::removeInputVoltageCalibrationPoint(SystemConfig& config, std::uint8_t index) {
    InputVoltageCalibration candidate = config.inputVoltageCalibration;
    if (index >= candidate.pointCount) {
        return false;
    }
    for (std::uint8_t i = index; i + 1U < candidate.pointCount; ++i) {
        candidate.points[i] = candidate.points[i + 1U];
    }
    --candidate.pointCount;
    candidate.points[candidate.pointCount] = {};
    if (!refreshInputVoltageCalibration(candidate)) {
        return false;
    }
    config.inputVoltageCalibration = candidate;
    configure(config);
    return true;
}

bool WaterSensorManager::updateInputVoltageCalibrationPoint(SystemConfig& config,
                                                            std::uint8_t index,
                                                            std::uint32_t actualMillivolts) {
    if (actualMillivolts < kInputVoltageCalibrationMinMv ||
        actualMillivolts > kInputVoltageCalibrationMaxMv) {
        return false;
    }
    InputVoltageCalibration candidate = config.inputVoltageCalibration;
    if (index >= candidate.pointCount) {
        return false;
    }
    candidate.points[index].actualInputMillivolts = actualMillivolts;
    if (!refreshInputVoltageCalibration(candidate)) {
        return false;
    }
    config.inputVoltageCalibration = candidate;
    configure(config);
    return true;
}

bool WaterSensorManager::clearInputVoltageCalibration(SystemConfig& config) {
    config.inputVoltageCalibration = {};
    configure(config);
    return true;
}

void WaterSensorManager::resetTdsRawWindow() {
    std::fill(tdsRawWindow_, tdsRawWindow_ + kTdsRawWindowSamples, 0);
    tdsRawWindowNext_ = 0;
    tdsRawWindowCount_ = 0;
}

void WaterSensorManager::addTdsRawSample(std::int16_t raw) {
    tdsRawWindow_[tdsRawWindowNext_] = raw;
    tdsRawWindowNext_ = static_cast<std::uint8_t>((tdsRawWindowNext_ + 1U) % kTdsRawWindowSamples);
    if (tdsRawWindowCount_ < kTdsRawWindowSamples) {
        ++tdsRawWindowCount_;
    }
}

std::int16_t WaterSensorManager::medianTdsRaw() const {
    if (tdsRawWindowCount_ == 0) {
        return 0;
    }
    std::int16_t sorted[kTdsRawWindowSamples]{};
    std::copy(tdsRawWindow_, tdsRawWindow_ + tdsRawWindowCount_, sorted);
    std::sort(sorted, sorted + tdsRawWindowCount_);
    const std::uint8_t middle = tdsRawWindowCount_ / 2U;
    if ((tdsRawWindowCount_ & 1U) != 0) {
        return sorted[middle];
    }
    return static_cast<std::int16_t>(
        (static_cast<std::int32_t>(sorted[middle - 1U]) + sorted[middle] + 1) / 2);
}

void WaterSensorManager::sampleOnce() {
    WaterSensorSnapshot next{};
    std::uint8_t failures = 0;

    if (sampleInputVoltage_) {
        const AdcReadResult input = adc_.readSingleEnded(AdcChannel::A0);
        if (input.ok) {
            addInputVoltageSample(input);
            std::int16_t rawMedian = input.raw;
            std::uint16_t adcMvMedian = static_cast<std::uint16_t>(std::max<std::int16_t>(0, input.millivolts));
            std::uint32_t theoretical = 0;
            std::uint32_t theoreticalSpan = 0;
            summarizeInputVoltageWindow(config_.inputVoltageDividerHighOhm,
                                        config_.inputVoltageDividerLowOhm,
                                        rawMedian,
                                        adcMvMedian,
                                        theoretical,
                                        theoreticalSpan);
            next.inputVoltageAdcRaw.valid = true;
            next.inputVoltageAdcRaw.value = rawMedian;
            next.inputVoltageAdcMv.valid = true;
            next.inputVoltageAdcMv.value = adcMvMedian;
            next.inputVoltageTheoreticalMv.valid = true;
            next.inputVoltageTheoreticalMv.value = static_cast<std::int32_t>(theoretical);
            next.inputVoltageWindowSpanMv = theoreticalSpan;
            next.inputVoltageSampleCount = inputVoltageWindowCount_;
            next.inputVoltageStable =
                inputVoltageWindowCount_ >= kInputVoltageWindowSamples &&
                theoreticalSpan <=
                    std::max<std::uint32_t>(kInputVoltageStableMinimumToleranceMv, theoretical / 1000U);
            next.inputVoltageMv.valid = true;
            next.inputVoltageMv.value =
                applyInputVoltageCalibration(static_cast<std::int32_t>(theoretical),
                                             config_.inputVoltageCalibration);
            next.inputVoltageCalibrated = config_.inputVoltageCalibration.calibrated;
        } else {
            ++failures;
        }
    }

    if (enabledTemperature(config_)) {
        const AdcReadResult temp = adc_.readSingleEnded(AdcChannel::A1);
        if (temp.ok && temp.millivolts > kTemperatureShortThresholdMv &&
            temp.millivolts < kTemperatureOpenThresholdMv) {
            const std::int16_t rawCentiC =
                ntcCentiCFromDividerMv(static_cast<std::uint16_t>(temp.millivolts),
                                      kSensorVrefMv,
                                      config_.temperaturePullupOhm,
                                      config_.temperatureNominalOhm,
                                      config_.temperatureBeta);
            next.temperatureRawCentiC.valid = true;
            next.temperatureRawCentiC.value = rawCentiC;
            next.temperatureCentiC.valid = true;
            next.temperatureCentiC.value = rawCentiC + config_.temperatureOffsetCentiC;
        } else {
            next.flags |= kWaterSensorFlagTempInvalid;
            if (temp.ok && temp.millivolts <= kTemperatureShortThresholdMv) {
                next.flags |= kWaterSensorFlagTempShort;
            } else if (temp.ok && temp.millivolts >= kTemperatureOpenThresholdMv) {
                next.flags |= kWaterSensorFlagTempOpen;
            } else {
                ++failures;
            }
        }
    }

    if (enabledTds(config_)) {
        const AdcReadResult tds = adc_.readSingleEnded(AdcChannel::A2);
        if (!tds.ok) {
            resetTdsRawWindow();
            ++failures;
            next.flags |= kWaterSensorFlagTdsInvalid;
        } else if (tds.overflow) {
            next.flags |= kWaterSensorFlagTdsAdcOverflow;
            updateTdsRange(adcRangeFullScaleMv(tdsRange_));
        } else {
            const std::uint16_t adcVoltageMv =
                static_cast<std::uint16_t>(std::max<std::int16_t>(0, tds.millivolts));
            updateTdsRange(adcVoltageMv);
            if (discardNextTdsSample_) {
                discardNextTdsSample_ = false;
            } else {
                addTdsRawSample(tds.raw);
                const std::uint16_t moduleVoltageMv =
                    tdsModuleVoltageMvFromAdcRaw(medianTdsRaw(),
                                                adcRangeFullScaleMv(tdsRange_),
                                                config_.tdsDividerHighOhm,
                                                config_.tdsDividerLowOhm);
                next.tdsVoltageMv.valid = true;
                next.tdsVoltageMv.value = moduleVoltageMv;
                TdsComputationInput input{};
                input.voltageMv = moduleVoltageMv;
                input.temperatureValid = next.temperatureCentiC.valid;
                input.temperatureCentiC = next.temperatureCentiC.valid ? toI16(next.temperatureCentiC.value) : 2500;
                input.scale = config_.tdsScale;
                input.offsetPpm = config_.tdsOffsetPpm;
                input.calibrated = config_.tdsCalibrated;
                input.temperatureCompensationEnabled = config_.tdsTemperatureCompensationEnabled;
                const TdsComputationResult tdsResult = computeTdsPpm(input);
                next.flags |= tdsResult.flags;
                next.tdsPpm.valid = (tdsResult.flags & kWaterSensorFlagTdsInvalid) == 0;
                next.tdsPpm.value = tdsResult.ppm;
                next.tdsCalibrated = config_.tdsCalibrated;
                next.tdsTemperatureCompensated = config_.tdsTemperatureCompensationEnabled;
                next.tdsTempFallback25C = (tdsResult.flags & kWaterSensorFlagTdsTempFallback25C) != 0;
                accumulateCalibration(tdsResult);
            }
        }
    }

    updateOfflineState(failures);
    next.flags |= (snapshot_.flags & kWaterSensorFlagAdcOffline);
    snapshot_ = next;
    ++sampleSequence_;
}

void WaterSensorManager::updateOfflineState(std::uint8_t failureCount) {
    if (failureCount > 0) {
        consecutiveFailureCycles_ = static_cast<std::uint8_t>(consecutiveFailureCycles_ + 1U);
        consecutiveSuccessCycles_ = 0;
        if (consecutiveFailureCycles_ >= kOfflineThreshold) {
            snapshot_.flags |= kWaterSensorFlagAdcOffline;
        }
        return;
    }
    consecutiveFailureCycles_ = 0;
    consecutiveSuccessCycles_ = static_cast<std::uint8_t>(consecutiveSuccessCycles_ + 1U);
    if (consecutiveSuccessCycles_ >= kRecoveryThreshold) {
        snapshot_.flags &= static_cast<std::uint16_t>(~kWaterSensorFlagAdcOffline);
    }
}

void WaterSensorManager::updateTdsRange(std::uint16_t tdsVoltageMv) {
    const std::uint16_t fullScale = adcRangeFullScaleMv(tdsRange_);
    if (tdsVoltageMv * 100UL >= fullScale * 85UL) {
        const AdcRange next = nextLargerRange(tdsRange_);
        if (next != tdsRange_) {
            tdsRange_ = next;
            adc_.setRange(AdcChannel::A2, tdsRange_);
            resetTdsRawWindow();
            tdsLowRangeWindows_ = 0;
            discardNextTdsSample_ = true;
        }
        return;
    }
    if (tdsVoltageMv * 100UL < fullScale * 30UL) {
        if (tdsLowRangeWindows_ < 255) {
            ++tdsLowRangeWindows_;
        }
        if (tdsLowRangeWindows_ >= kTdsDownshiftWindows) {
            const AdcRange next = nextSmallerRange(tdsRange_);
            if (next != tdsRange_) {
                tdsRange_ = next;
                adc_.setRange(AdcChannel::A2, tdsRange_);
                resetTdsRawWindow();
                discardNextTdsSample_ = true;
            }
            tdsLowRangeWindows_ = 0;
        }
    } else {
        tdsLowRangeWindows_ = 0;
    }
}

void WaterSensorManager::accumulateCalibration(const TdsComputationResult& result) {
    if (calibrationKind_ == CalibrationKind::None || calibrationFailed_) {
        return;
    }
    if ((result.flags & (kWaterSensorFlagTdsInvalid | kWaterSensorFlagTdsAdcOverflow)) != 0) {
        calibrationFailed_ = true;
        return;
    }
    if ((result.flags & kWaterSensorFlagTdsTempFallback25C) != 0) {
        calibrationTempFallback_ = true;
    }
    if (calibrationSampleCount_ < kCalibrationMaxSamples) {
        calibrationReadings_[calibrationSampleCount_++] = result.rawPpm;
    }
}

bool WaterSensorManager::calibrationReady() const {
    if (calibrationKind_ == CalibrationKind::None || calibrationFailed_ || calibrationSampleCount_ < kCalibrationMinSamples) {
        return false;
    }
    return tdsReadingsStable(calibrationReadings_,
                             calibrationSampleCount_,
                             calibrationReferencePpm_,
                             false);
}

std::uint16_t WaterSensorManager::calibrationRawMedian() const {
    if (calibrationSampleCount_ == 0) {
        return 0;
    }
    std::uint16_t sorted[kCalibrationMaxSamples]{};
    for (std::uint8_t i = 0; i < calibrationSampleCount_; ++i) {
        sorted[i] = calibrationReadings_[i];
    }
    std::sort(sorted, sorted + calibrationSampleCount_);
    const std::uint8_t middle = calibrationSampleCount_ / 2U;
    if ((calibrationSampleCount_ & 1U) != 0) {
        return sorted[middle];
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(sorted[middle - 1U]) + sorted[middle]) / 2U);
}

bool WaterSensorManager::refreshTdsCalibrationCandidate() {
    TdsCalibrationPointInput points[kTdsCalibrationMaxPoints]{};
    for (std::uint8_t i = 0; i < tdsCalibrationPointCount_; ++i) {
        points[i].referencePpm = tdsCalibrationPoints_[i].referencePpm;
        points[i].rawPpm = tdsCalibrationPoints_[i].rawPpm;
    }
    return computeTdsCalibrationFit(points, tdsCalibrationPointCount_, tdsCalibrationFit_);
}

void WaterSensorManager::accumulateRunSample(const WaterSensorSnapshot& current) {
    const bool hasTemperature = current.temperatureCentiC.valid;
    const bool hasTds = current.tdsPpm.valid;
    if (!hasTemperature && !hasTds) {
        run_.flags |= current.flags;
        return;
    }

    RunWindowSample& sample = run_.samples[run_.next];
    sample = {};
    sample.temperatureValid = hasTemperature;
    sample.tdsValid = hasTds;
    sample.temperatureCentiC = hasTemperature ? toI16(current.temperatureCentiC.value) : 0;
    sample.tdsPpm = hasTds ? toU16(current.tdsPpm.value) : 0;

    run_.next = static_cast<std::uint8_t>((run_.next + 1U) % kRunWindowSamples);
    if (run_.count < kRunWindowSamples) {
        ++run_.count;
    }
    run_.flags |= current.flags;
}

}  // namespace faucet
