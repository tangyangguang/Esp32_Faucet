#include "app/WaterSensorManager.h"

#include <algorithm>

namespace faucet {
namespace {

constexpr std::uint32_t kSampleIntervalMs = 1000;
constexpr std::uint32_t kInputDividerHighOhm = 100000;
constexpr std::uint32_t kInputDividerLowOhm = 10000;
constexpr std::uint32_t kNtcPullupOhm = 51000;
constexpr std::uint8_t kOfflineThreshold = 3;
constexpr std::uint8_t kRecoveryThreshold = 3;
constexpr std::uint8_t kTdsDownshiftWindows = 8;
constexpr std::uint8_t kCalibrationMinSamples = 12;
constexpr std::uint32_t kTdsCalibrationSessionTimeoutSec = 30UL * 60UL;

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
    return config.temperatureKind == TemperatureKind::Ntc50kB3950;
}

bool enabledTds(const SystemConfig& config) {
    return config.tdsKind == TdsKind::AnalogTdsAo;
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
    config_ = config;
    sanitizeConfig(config_);
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
}

void WaterSensorManager::sampleRun() {
    accumulateRunSample(snapshot_);
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
    } else if (enabledTemperature(config_)) {
        summary.sensorFlags |= kWaterSensorFlagTempInvalid;
    }
    if (tdsCount > 0) {
        summary.tdsPpm = static_cast<std::uint16_t>(tdsSum / tdsCount);
    } else if (enabledTds(config_)) {
        summary.sensorFlags |= kWaterSensorFlagTdsInvalid;
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
    session.rawAvgPpm = calibrationRawAverage();
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
    point.rawPpm = calibrationRawAverage();
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

bool WaterSensorManager::applyReadyTdsCalibration(SystemConfig& config, std::uint32_t nowSeconds) {
    (void)nowSeconds;
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

void WaterSensorManager::sampleOnce() {
    WaterSensorSnapshot next{};
    std::uint8_t failures = 0;

    if (sampleInputVoltage_) {
        const AdcReadResult input = adc_.readSingleEnded(AdcChannel::A0);
        if (input.ok) {
            next.inputVoltageMv.valid = true;
            next.inputVoltageMv.value = static_cast<std::int32_t>(
                inputVoltageMvFromDivider(input.millivolts, kInputDividerHighOhm, kInputDividerLowOhm));
        } else {
            ++failures;
        }
    }

    if (enabledTemperature(config_)) {
        const AdcReadResult temp = adc_.readSingleEnded(AdcChannel::A1);
        if (temp.ok && temp.millivolts > 0 && temp.millivolts < kSensorVrefMv) {
            const std::int16_t rawCentiC =
                ntcCentiCFromDividerMv(static_cast<std::uint16_t>(temp.millivolts), kSensorVrefMv, kNtcPullupOhm);
            next.temperatureRawCentiC.valid = true;
            next.temperatureRawCentiC.value = rawCentiC;
            next.temperatureCentiC.valid = true;
            next.temperatureCentiC.value = rawCentiC + config_.temperatureOffsetCentiC;
        } else {
            next.flags |= kWaterSensorFlagTempInvalid;
            if (!temp.ok) {
                ++failures;
            }
        }
    }

    if (enabledTds(config_)) {
        const AdcReadResult tds = adc_.readSingleEnded(AdcChannel::A2);
        if (!tds.ok) {
            ++failures;
            next.flags |= kWaterSensorFlagTdsInvalid;
        } else if (tds.overflow) {
            next.flags |= kWaterSensorFlagTdsAdcOverflow;
            updateTdsRange(adcRangeFullScaleMv(tdsRange_));
        } else {
            const std::uint16_t voltageMv = static_cast<std::uint16_t>(std::max<std::int16_t>(0, tds.millivolts));
            updateTdsRange(voltageMv);
            if (discardNextTdsSample_) {
                discardNextTdsSample_ = false;
            } else {
                next.tdsVoltageMv.valid = true;
                next.tdsVoltageMv.value = voltageMv;
                TdsComputationInput input{};
                input.voltageMv = voltageMv;
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

std::uint16_t WaterSensorManager::calibrationRawAverage() const {
    if (calibrationSampleCount_ == 0) {
        return 0;
    }
    std::uint32_t sum = 0;
    for (std::uint8_t i = 0; i < calibrationSampleCount_; ++i) {
        sum += calibrationReadings_[i];
    }
    return static_cast<std::uint16_t>(sum / calibrationSampleCount_);
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
