#include "app/WaterSensors.h"

#include <algorithm>
#include <cmath>
#include <math.h>

namespace faucet {
namespace {

constexpr double kNtcNominalOhm = 50000.0;
constexpr double kNtcNominalKelvin = 298.15;
constexpr double kNtcBeta = 3950.0;
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

}  // namespace

std::int16_t ntcCentiCFromDividerMv(std::uint16_t adcMv, std::uint16_t vrefMv, std::uint32_t pullupOhm) {
    if (adcMv == 0 || vrefMv == 0 || adcMv >= vrefMv || pullupOhm == 0) {
        return 0;
    }
    const double adc = static_cast<double>(adcMv);
    const double vref = static_cast<double>(vrefMv);
    const double resistance = static_cast<double>(pullupOhm) * adc / (vref - adc);
    if (resistance <= 0.0 || !isfinite(resistance)) {
        return 0;
    }
    const double kelvin = 1.0 / ((1.0 / kNtcNominalKelvin) + (std::log(resistance / kNtcNominalOhm) / kNtcBeta));
    return roundToI16((kelvin - 273.15) * 100.0);
}

std::uint32_t inputVoltageMvFromDivider(std::uint16_t adcMv, std::uint32_t highOhm, std::uint32_t lowOhm) {
    if (lowOhm == 0) {
        return 0;
    }
    const std::uint64_t numerator = static_cast<std::uint64_t>(adcMv) * (highOhm + lowOhm);
    return static_cast<std::uint32_t>(numerator / lowOhm);
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

bool computeSinglePointTdsCalibration(std::uint16_t referencePpm, std::uint16_t rawPpm, float& scaleOut) {
    if (referencePpm == 0 || rawPpm == 0) {
        return false;
    }
    scaleOut = static_cast<float>(referencePpm) / static_cast<float>(rawPpm);
    return isfinite(scaleOut) && scaleOut > 0.0f;
}

bool computeTwoPointTdsCalibration(std::uint16_t lowReferencePpm,
                                   std::uint16_t lowRawPpm,
                                   std::uint16_t highReferencePpm,
                                   std::uint16_t highRawPpm,
                                   float& scaleOut,
                                   std::int16_t& offsetOut) {
    if (highReferencePpm <= lowReferencePpm || highRawPpm <= lowRawPpm) {
        return false;
    }
    const std::uint16_t referenceSpan = highReferencePpm - lowReferencePpm;
    const std::uint16_t rawSpan = highRawPpm - lowRawPpm;
    if (referenceSpan < 50 || rawSpan < 30) {
        return false;
    }
    scaleOut = static_cast<float>(referenceSpan) / static_cast<float>(rawSpan);
    if (!isfinite(scaleOut) || scaleOut <= 0.0f) {
        return false;
    }
    offsetOut = roundToI16(static_cast<double>(lowReferencePpm) - static_cast<double>(scaleOut) * lowRawPpm);
    return true;
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
