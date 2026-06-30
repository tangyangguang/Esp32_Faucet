#pragma once

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::uint16_t kWaterSensorFlagAdcOffline = 1U << 0U;
constexpr std::uint16_t kWaterSensorFlagTdsAdcOverflow = 1U << 1U;
constexpr std::uint16_t kWaterSensorFlagTempInvalid = 1U << 2U;
constexpr std::uint16_t kWaterSensorFlagTdsInvalid = 1U << 3U;
constexpr std::uint16_t kWaterSensorFlagTdsUncalibrated = 1U << 4U;
constexpr std::uint16_t kWaterSensorFlagTdsTempFallback25C = 1U << 5U;

struct SensorValue {
    bool valid = false;
    std::int32_t value = 0;
};

struct WaterSensorSnapshot {
    SensorValue inputVoltageMv;
    SensorValue temperatureRawCentiC;
    SensorValue temperatureCentiC;
    SensorValue tdsPpm;
    SensorValue tdsVoltageMv;
    std::uint16_t flags = 0;
    bool tdsCalibrated = false;
    bool tdsTemperatureCompensated = false;
    bool tdsTempFallback25C = false;
};

struct TdsComputationInput {
    std::uint16_t voltageMv = 0;
    bool temperatureValid = false;
    std::int16_t temperatureCentiC = 2500;
    float scale = 1.0f;
    std::int16_t offsetPpm = 0;
    bool calibrated = false;
    bool temperatureCompensationEnabled = true;
};

struct TdsComputationResult {
    std::uint16_t rawPpm = 0;
    std::uint16_t ppm = 0;
    std::uint16_t flags = 0;
};

constexpr std::uint8_t kTdsCalibrationMaxPoints = 5;
constexpr std::uint16_t kTdsCalibrationMinReferenceSpanPpm = 50;
constexpr std::uint16_t kTdsCalibrationMinRawSpanPpm = 30;
constexpr float kTdsCalibrationMinScale = 0.05f;
constexpr float kTdsCalibrationMaxScale = 20.0f;
constexpr std::int16_t kTdsCalibrationMinOffsetPpm = -2000;
constexpr std::int16_t kTdsCalibrationMaxOffsetPpm = 2000;

struct TdsCalibrationPointInput {
    std::uint16_t referencePpm = 0;
    std::uint16_t rawPpm = 0;
};

struct TdsCalibrationFitResult {
    bool valid = false;
    std::uint8_t pointCount = 0;
    float scale = 1.0f;
    std::int16_t offsetPpm = 0;
    std::uint16_t referenceSpanPpm = 0;
    std::uint16_t rawSpanPpm = 0;
};

std::int16_t ntcCentiCFromDividerMv(std::uint16_t adcMv, std::uint16_t vrefMv, std::uint32_t pullupOhm);

std::uint32_t inputVoltageMvFromDivider(std::uint16_t adcMv, std::uint32_t highOhm, std::uint32_t lowOhm);

TdsComputationResult computeTdsPpm(const TdsComputationInput& input);

bool computeSinglePointTdsCalibration(std::uint16_t referencePpm, std::uint16_t rawPpm, float& scaleOut);

bool computeTdsCalibrationFit(const TdsCalibrationPointInput* points,
                              std::size_t count,
                              TdsCalibrationFitResult& result);

bool tdsReadingsStable(const std::uint16_t* readings, std::size_t count, std::uint16_t referencePpm, bool lowPoint);

}  // namespace faucet
