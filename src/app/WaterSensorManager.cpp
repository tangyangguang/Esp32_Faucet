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
constexpr std::uint16_t kTdsHighReferenceWarningPpm = 100;

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
    return config.temperatureEnabled && config.temperatureKind == TemperatureKind::Ntc50kB3950;
}

bool enabledTds(const SystemConfig& config) {
    return config.tdsEnabled && config.tdsKind == TdsKind::AnalogTdsAo;
}

}  // namespace

WaterSensorManager::WaterSensorManager(AdcReader& adc)
    : adc_(adc),
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
      calibrationStartedSeconds_(0),
      calibrationReferencePpm_(0),
      calibrationReadings_{},
      calibrationSampleCount_(0),
      calibrationTempFallback_(false),
      calibrationFailed_(false),
      hasPendingLowPoint_(false),
      pendingLowReferencePpm_(0),
      pendingLowRawPpm_(0),
      pendingScale_(1.0f),
      pendingOffsetPpm_(0) {}

void WaterSensorManager::configure(const SystemConfig& config) {
    config_ = config;
    sanitizeConfig(config_);
}

bool WaterSensorManager::begin() {
    const bool ready = adc_.begin();
    adc_.setRange(AdcChannel::A0, AdcRange::P4096);
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
    summary.sensorSampleCount = run_.count;
    if (run_.count > 0) {
        summary.temperatureAvgCentiC = toI16(run_.tempSum / run_.count);
        summary.temperatureMinCentiC = run_.tempMin;
        summary.temperatureMaxCentiC = run_.tempMax;
        summary.tdsAvgPpm = static_cast<std::uint16_t>(run_.tdsSum / run_.count);
        summary.tdsMinPpm = run_.tdsMin;
        summary.tdsMaxPpm = run_.tdsMax;
        summary.tdsVoltageAvgMv = static_cast<std::uint16_t>(run_.voltageSum / run_.count);
    }
    summary.tdsCalibrationRevisionAtRun = config_.tdsCalibrationRevision;
    summary.tdsCalibrationModeAtRun = static_cast<std::uint8_t>(config_.tdsCalibrationMode);
    summary.tdsCalibratedAtRun = config_.tdsCalibrated ? 1 : 0;
    summary.tdsTemperatureCompensatedAtRun = config_.tdsTemperatureCompensationEnabled ? 1 : 0;
    summary.tdsTempFallback25CAtRun = run_.fallback ? 1 : 0;
    return summary;
}

bool WaterSensorManager::startTdsSinglePointCalibration(std::uint16_t referencePpm,
                                                        std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || referencePpm == 0 || calibrationKind_ != CalibrationKind::None) {
        return false;
    }
    calibrationKind_ = CalibrationKind::Single;
    calibrationStartedSeconds_ = nowSeconds;
    calibrationReferencePpm_ = referencePpm;
    calibrationSampleCount_ = 0;
    calibrationTempFallback_ = false;
    calibrationFailed_ = false;
    pendingScale_ = 1.0f;
    pendingOffsetPpm_ = 0;
    return true;
}

bool WaterSensorManager::startTdsTwoPointLow(std::uint16_t lowReferencePpm,
                                             std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || calibrationKind_ != CalibrationKind::None) {
        return false;
    }
    calibrationKind_ = CalibrationKind::Low;
    calibrationStartedSeconds_ = nowSeconds;
    calibrationReferencePpm_ = lowReferencePpm;
    calibrationSampleCount_ = 0;
    calibrationTempFallback_ = false;
    calibrationFailed_ = false;
    return true;
}

bool WaterSensorManager::startTdsTwoPointHigh(std::uint16_t highReferencePpm,
                                              std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || highReferencePpm == 0 || !hasPendingLowPoint_ ||
        calibrationKind_ != CalibrationKind::None) {
        return false;
    }
    calibrationKind_ = CalibrationKind::High;
    calibrationStartedSeconds_ = nowSeconds;
    calibrationReferencePpm_ = highReferencePpm;
    calibrationSampleCount_ = 0;
    calibrationTempFallback_ = false;
    calibrationFailed_ = false;
    return true;
}

bool WaterSensorManager::cancelTdsCalibration() {
    const bool wasActive = calibrationKind_ != CalibrationKind::None;
    calibrationKind_ = CalibrationKind::None;
    calibrationSampleCount_ = 0;
    calibrationFailed_ = false;
    return wasActive;
}

TdsCalibrationSessionSnapshot WaterSensorManager::calibrationSnapshot() const {
    TdsCalibrationSessionSnapshot session{};
    session.active = calibrationKind_ != CalibrationKind::None;
    session.failed = calibrationFailed_;
    session.tempFallback25C = calibrationTempFallback_;
    session.sampleCount = calibrationSampleCount_;
    session.referencePpm = calibrationReferencePpm_;
    session.rawAvgPpm = calibrationRawAverage();
    session.flags = snapshot_.flags;
    session.readyToSave = calibrationReady();
    session.hasPendingLowPoint = hasPendingLowPoint_;
    session.highReferenceLowWarning =
        calibrationKind_ == CalibrationKind::High && calibrationReferencePpm_ < kTdsHighReferenceWarningPpm;
    return session;
}

bool WaterSensorManager::saveReadyTdsCalibration(SystemConfig& config, std::uint32_t nowSeconds) {
    if (!calibrationReady()) {
        return false;
    }
    const std::uint16_t rawAvg = calibrationRawAverage();
    if (calibrationKind_ == CalibrationKind::Single) {
        if (!computeSinglePointTdsCalibration(calibrationReferencePpm_, rawAvg, pendingScale_)) {
            return false;
        }
        pendingOffsetPpm_ = 0;
        config.tdsCalibrationMode = TdsCalibrationMode::SinglePoint;
        config.tdsLowReferencePpm = 0;
        config.tdsLowRawPpm = 0;
        config.tdsHighReferencePpm = calibrationReferencePpm_;
        config.tdsHighRawPpm = rawAvg;
    } else if (calibrationKind_ == CalibrationKind::Low) {
        pendingLowReferencePpm_ = calibrationReferencePpm_;
        pendingLowRawPpm_ = rawAvg;
        hasPendingLowPoint_ = true;
        calibrationKind_ = CalibrationKind::None;
        calibrationSampleCount_ = 0;
        return true;
    } else if (calibrationKind_ == CalibrationKind::High) {
        if (!hasPendingLowPoint_ ||
            !computeTwoPointTdsCalibration(pendingLowReferencePpm_,
                                           pendingLowRawPpm_,
                                           calibrationReferencePpm_,
                                           rawAvg,
                                           pendingScale_,
                                           pendingOffsetPpm_)) {
            return false;
        }
        config.tdsCalibrationMode = TdsCalibrationMode::TwoPoint;
        config.tdsLowReferencePpm = pendingLowReferencePpm_;
        config.tdsLowRawPpm = pendingLowRawPpm_;
        config.tdsHighReferencePpm = calibrationReferencePpm_;
        config.tdsHighRawPpm = rawAvg;
    } else {
        return false;
    }
    config.tdsScale = pendingScale_;
    config.tdsOffsetPpm = pendingOffsetPpm_;
    config.tdsCalibrated = true;
    config.tdsCalibrationRevision = static_cast<std::uint16_t>(config.tdsCalibrationRevision + 1U);
    config.tdsCalibrationTime = nowSeconds;
    config.tdsCalibrationTemperatureCentiC =
        snapshot_.temperatureCentiC.valid ? toI16(snapshot_.temperatureCentiC.value) : 2500;
    config.tdsCalibrationVoltageMv = snapshot_.tdsVoltageMv.valid ? toU16(snapshot_.tdsVoltageMv.value) : 0;
    sanitizeConfig(config);
    configure(config);
    hasPendingLowPoint_ = false;
    calibrationKind_ = CalibrationKind::None;
    calibrationSampleCount_ = 0;
    return true;
}

void WaterSensorManager::sampleOnce() {
    WaterSensorSnapshot next{};
    std::uint8_t failures = 0;

    const AdcReadResult input = adc_.readSingleEnded(AdcChannel::A0);
    if (input.ok) {
        next.inputVoltageMv.valid = true;
        next.inputVoltageMv.value =
            static_cast<std::int32_t>(inputVoltageMvFromDivider(input.millivolts, kInputDividerHighOhm, kInputDividerLowOhm));
    } else {
        ++failures;
    }

    if (enabledTemperature(config_)) {
        const AdcReadResult temp = adc_.readSingleEnded(AdcChannel::A1);
        if (temp.ok && temp.millivolts > 0 && temp.millivolts < config_.sensorVrefMv) {
            const std::int16_t rawCentiC =
                ntcCentiCFromDividerMv(static_cast<std::uint16_t>(temp.millivolts), config_.sensorVrefMv, kNtcPullupOhm);
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
    next.flags |= (snapshot_.flags & kWaterSensorFlagAds1115Offline);
    snapshot_ = next;
}

void WaterSensorManager::updateOfflineState(std::uint8_t failureCount) {
    if (failureCount > 0) {
        consecutiveFailureCycles_ = static_cast<std::uint8_t>(consecutiveFailureCycles_ + 1U);
        consecutiveSuccessCycles_ = 0;
        if (consecutiveFailureCycles_ >= kOfflineThreshold) {
            snapshot_.flags |= kWaterSensorFlagAds1115Offline;
        }
        return;
    }
    consecutiveFailureCycles_ = 0;
    consecutiveSuccessCycles_ = static_cast<std::uint8_t>(consecutiveSuccessCycles_ + 1U);
    if (consecutiveSuccessCycles_ >= kRecoveryThreshold) {
        snapshot_.flags &= static_cast<std::uint16_t>(~kWaterSensorFlagAds1115Offline);
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
                             calibrationKind_ == CalibrationKind::Low);
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

void WaterSensorManager::accumulateRunSample(const WaterSensorSnapshot& current) {
    if (!current.temperatureCentiC.valid || !current.tdsPpm.valid || !current.tdsVoltageMv.valid) {
        run_.flags |= current.flags;
        return;
    }
    const std::int16_t temp = toI16(current.temperatureCentiC.value);
    const std::uint16_t tds = toU16(current.tdsPpm.value);
    const std::uint16_t voltage = toU16(current.tdsVoltageMv.value);
    if (run_.count == 0) {
        run_.tempMin = temp;
        run_.tempMax = temp;
        run_.tdsMin = tds;
        run_.tdsMax = tds;
    } else {
        run_.tempMin = std::min(run_.tempMin, temp);
        run_.tempMax = std::max(run_.tempMax, temp);
        run_.tdsMin = std::min(run_.tdsMin, tds);
        run_.tdsMax = std::max(run_.tdsMax, tds);
    }
    run_.tempSum += temp;
    run_.tdsSum += tds;
    run_.voltageSum += voltage;
    ++run_.count;
    run_.flags |= current.flags;
    run_.fallback = run_.fallback || current.tdsTempFallback25C;
}

}  // namespace faucet
