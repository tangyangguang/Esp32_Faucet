#include "app/WaterSensors.h"

#include <algorithm>
#include <cmath>
#include <math.h>

namespace faucet {
namespace {

constexpr double kNtcNominalKelvin = 298.15;
constexpr std::uint16_t kTdsMaxVoltageMv = 2300;
constexpr std::size_t kTdsMaxStabilitySamples = 64;
constexpr std::size_t kTdsMinStableSamples = 12;

std::uint16_t roundToU16(double value) {
    if (!isfinite(value) || value <= 0.0) {
        return 0;
    }
    if (value >= 65535.0) {
        return 65535;
    }
    return static_cast<std::uint16_t>(std::lround(value));
}

std::int16_t roundToI16(double value) {
    if (!isfinite(value)) {
        return 0;
    }
    if (value <= -32768.0) {
        return -32768;
    }
    if (value >= 32767.0) {
        return 32767;
    }
    return static_cast<std::int16_t>(std::lround(value));
}

std::uint16_t tdsRawPpmFromCompensatedVoltage(double voltageV) {
    const double raw = (133.42 * voltageV * voltageV * voltageV - 255.86 * voltageV * voltageV + 857.39 * voltageV) * 0.5;
    return roundToU16(raw);
}

bool tdsFitOutputAllowed(float scale, std::int16_t offset) {
    return isfinite(scale) &&
           scale >= kTdsCalibrationMinScale &&
           scale <= kTdsCalibrationMaxScale &&
           offset >= kTdsCalibrationMinOffsetPpm &&
           offset <= kTdsCalibrationMaxOffsetPpm;
}

bool computeOnePointScale(std::uint16_t referencePpm, std::uint16_t rawPpm, float& scaleOut) {
    if (referencePpm == 0 || rawPpm == 0) {
        return false;
    }
    scaleOut = static_cast<float>(referencePpm) / static_cast<float>(rawPpm);
    return isfinite(scaleOut) && scaleOut > 0.0f;
}

}  // namespace

std::int16_t ntcCentiCFromDividerMv(std::uint16_t adcMv,
                                    std::uint16_t vrefMv,
                                    std::uint32_t pullupOhm,
                                    std::uint32_t nominalOhm,
                                    std::uint32_t beta) {
    if (adcMv == 0 || vrefMv == 0 || adcMv >= vrefMv || pullupOhm == 0 || nominalOhm == 0 || beta == 0) {
        return 0;
    }
    const double adc = static_cast<double>(adcMv);
    const double vref = static_cast<double>(vrefMv);
    const double resistance = static_cast<double>(pullupOhm) * adc / (vref - adc);
    if (resistance <= 0.0 || !isfinite(resistance)) {
        return 0;
    }
    const double kelvin =
        1.0 / ((1.0 / kNtcNominalKelvin) +
               (std::log(resistance / static_cast<double>(nominalOhm)) / static_cast<double>(beta)));
    return roundToI16((kelvin - 273.15) * 100.0);
}

std::uint32_t inputVoltageMvFromDivider(std::uint16_t adcMv, std::uint32_t highOhm, std::uint32_t lowOhm) {
    if (lowOhm == 0) {
        return 0;
    }
    const std::uint64_t numerator = static_cast<std::uint64_t>(adcMv) * (highOhm + lowOhm);
    return static_cast<std::uint32_t>(numerator / lowOhm);
}

std::uint32_t inputVoltageMvFromAdcRaw(std::int16_t raw,
                                      std::uint16_t fullScaleMv,
                                      std::uint32_t highOhm,
                                      std::uint32_t lowOhm) {
    if (raw < 0 || fullScaleMv == 0 || lowOhm == 0) {
        return 0;
    }
    const std::uint64_t numerator = static_cast<std::uint64_t>(raw) * fullScaleMv * (highOhm + lowOhm);
    const std::uint64_t denominator = 32768ULL * lowOhm;
    return static_cast<std::uint32_t>((numerator + denominator / 2ULL) / denominator);
}

TdsComputationResult computeTdsPpm(const TdsComputationInput& input) {
    TdsComputationResult result{};
    if (input.voltageMv > kTdsMaxVoltageMv) {
        result.flags |= kWaterSensorFlagTdsInvalid;
        return result;
    }

    double compensatedVoltage = static_cast<double>(input.voltageMv) / 1000.0;
    if (input.temperatureCompensationEnabled) {
        if (input.temperatureValid) {
            const double temperatureC = static_cast<double>(input.temperatureCentiC) / 100.0;
            const double compensation = 1.0 + 0.02 * (temperatureC - 25.0);
            if (compensation > 0.0) {
                compensatedVoltage /= compensation;
            }
        } else {
            result.flags |= kWaterSensorFlagTdsTempFallback25C;
        }
    }

    result.rawPpm = tdsRawPpmFromCompensatedVoltage(compensatedVoltage);
    double calibratedPpm = static_cast<double>(result.rawPpm) * input.scale + input.offsetPpm;
    if (!input.calibrated) {
        result.flags |= kWaterSensorFlagTdsUncalibrated;
    }
    result.ppm = roundToU16(calibratedPpm);
    return result;
}

bool computeTdsCalibrationFit(const TdsCalibrationPointInput* points,
                              std::size_t count,
                              TdsCalibrationFitResult& result) {
    result = TdsCalibrationFitResult{};
    if (!points || count == 0 || count > kTdsCalibrationMaxPoints) {
        return false;
    }

    std::uint16_t minReference = points[0].referencePpm;
    std::uint16_t maxReference = points[0].referencePpm;
    std::uint16_t minRaw = points[0].rawPpm;
    std::uint16_t maxRaw = points[0].rawPpm;
    double rawSum = 0.0;
    double referenceSum = 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        if (points[i].rawPpm == 0 || points[i].referencePpm > 2000 || points[i].rawPpm > 2000) {
            return false;
        }
        minReference = std::min(minReference, points[i].referencePpm);
        maxReference = std::max(maxReference, points[i].referencePpm);
        minRaw = std::min(minRaw, points[i].rawPpm);
        maxRaw = std::max(maxRaw, points[i].rawPpm);
        rawSum += points[i].rawPpm;
        referenceSum += points[i].referencePpm;

        for (std::size_t j = i + 1; j < count; ++j) {
            if (points[i].referencePpm == points[j].referencePpm &&
                std::abs(static_cast<int>(points[i].rawPpm) - static_cast<int>(points[j].rawPpm)) > 30) {
                return false;
            }
            if (points[i].rawPpm == points[j].rawPpm &&
                std::abs(static_cast<int>(points[i].referencePpm) - static_cast<int>(points[j].referencePpm)) > 50) {
                return false;
            }
        }
    }

    result.pointCount = static_cast<std::uint8_t>(count);
    result.referenceSpanPpm = maxReference - minReference;
    result.rawSpanPpm = maxRaw - minRaw;

    if (count == 1) {
        if (!computeOnePointScale(points[0].referencePpm, points[0].rawPpm, result.scale)) {
            result = TdsCalibrationFitResult{};
            return false;
        }
        result.offsetPpm = 0;
        result.valid = tdsFitOutputAllowed(result.scale, result.offsetPpm);
        if (!result.valid) {
            result = TdsCalibrationFitResult{};
        }
        return result.valid;
    }

    if (result.referenceSpanPpm < kTdsCalibrationMinReferenceSpanPpm ||
        result.rawSpanPpm < kTdsCalibrationMinRawSpanPpm) {
        result = TdsCalibrationFitResult{};
        return false;
    }

    const double rawMean = rawSum / static_cast<double>(count);
    const double referenceMean = referenceSum / static_cast<double>(count);
    double covariance = 0.0;
    double rawVariance = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double rawDelta = static_cast<double>(points[i].rawPpm) - rawMean;
        const double referenceDelta = static_cast<double>(points[i].referencePpm) - referenceMean;
        covariance += rawDelta * referenceDelta;
        rawVariance += rawDelta * rawDelta;
    }
    if (rawVariance <= 0.0) {
        result = TdsCalibrationFitResult{};
        return false;
    }

    result.scale = static_cast<float>(covariance / rawVariance);
    result.offsetPpm = roundToI16(referenceMean - static_cast<double>(result.scale) * rawMean);
    result.valid = tdsFitOutputAllowed(result.scale, result.offsetPpm);
    if (!result.valid) {
        result = TdsCalibrationFitResult{};
    }
    return result.valid;
}

bool tdsReadingsStable(const std::uint16_t* readings, std::size_t count, std::uint16_t referencePpm, bool lowPoint) {
    if (!readings || count < kTdsMinStableSamples || count > kTdsMaxStabilitySamples) {
        return false;
    }
    std::uint16_t sorted[kTdsMaxStabilitySamples]{};
    for (std::size_t i = 0; i < count; ++i) {
        sorted[i] = readings[i];
    }
    std::sort(sorted, sorted + count);
    const std::size_t p10Index = (count - 1U) / 10U;
    const std::size_t p90Index = ((count - 1U) * 9U) / 10U;
    const std::uint16_t span = sorted[p90Index] - sorted[p10Index];
    const std::uint16_t threshold = lowPoint
                                        ? std::max<std::uint16_t>(2, static_cast<std::uint16_t>(referencePpm / 5U))
                                        : std::max<std::uint16_t>(5, static_cast<std::uint16_t>(referencePpm / 20U));
    return span <= threshold;
}

}  // namespace faucet
