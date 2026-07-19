#include <unity.h>

#include "app/WaterSensors.h"

using namespace faucet;

void test_ntc_51k_pullup_50k_b3950_key_points() {
    TEST_ASSERT_INT16_WITHIN(50, 2500, ntcCentiCFromDividerMv(1634, 3300, 51000, 50000, 3950));
    TEST_ASSERT_INT16_WITHIN(120, 1000, ntcCentiCFromDividerMv(2192, 3300, 51000, 50000, 3950));
    TEST_ASSERT_INT16_WITHIN(120, 4000, ntcCentiCFromDividerMv(1129, 3300, 51000, 50000, 3950));
}

void test_input_voltage_11_to_1_divider() {
    TEST_ASSERT_EQUAL_UINT32(12001, inputVoltageMvFromDivider(1091, 100000, 10000));
    TEST_ASSERT_EQUAL_UINT32(24002, inputVoltageMvFromDivider(2182, 100000, 10000));
}

void test_tds_board_voltage_restores_10k_15k_input_divider() {
    TEST_ASSERT_EQUAL_UINT32(1000, inputVoltageMvFromDivider(600, 10000, 15000));
    TEST_ASSERT_EQUAL_UINT32(40, inputVoltageMvFromDivider(24, 10000, 15000));
}

void test_tds_formula_uses_25c_when_temperature_invalid() {
    TdsComputationInput input{};
    input.voltageMv = 24;
    input.temperatureValid = false;

    const TdsComputationResult result = computeTdsPpm(input);

    TEST_ASSERT_TRUE((result.flags & kWaterSensorFlagTdsTempFallback25C) != 0);
    TEST_ASSERT_TRUE((result.flags & kWaterSensorFlagTdsUncalibrated) != 0);
    TEST_ASSERT_EQUAL_UINT16(10, result.rawPpm);
    TEST_ASSERT_EQUAL_UINT16(10, result.ppm);
}

void test_tds_temperature_compensation_sets_fallback_flag() {
    TdsComputationInput at25{};
    at25.voltageMv = 1000;
    at25.temperatureValid = true;
    at25.temperatureCentiC = 2500;
    TdsComputationInput at35 = at25;
    at35.temperatureCentiC = 3500;

    const TdsComputationResult base = computeTdsPpm(at25);
    const TdsComputationResult warm = computeTdsPpm(at35);

    TEST_ASSERT_TRUE(warm.rawPpm < base.rawPpm);
    TEST_ASSERT_TRUE((warm.flags & kWaterSensorFlagTdsTempFallback25C) == 0);
}

void test_tds_fit_one_point_uses_scale_with_zero_offset() {
    TdsCalibrationPointInput points[1]{};
    points[0] = TdsCalibrationPointInput{160, 150};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 1, fit));

    TEST_ASSERT_TRUE(fit.valid);
    TEST_ASSERT_EQUAL_UINT8(1, fit.pointCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0667f, fit.scale);
    TEST_ASSERT_EQUAL_INT16(0, fit.offsetPpm);
}

void test_tds_fit_linear_regression_matches_reference_line() {
    TdsCalibrationPointInput points[3]{};
    points[0] = TdsCalibrationPointInput{20, 30};
    points[1] = TdsCalibrationPointInput{120, 130};
    points[2] = TdsCalibrationPointInput{220, 230};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 3, fit));
    TEST_ASSERT_TRUE(fit.valid);
    TEST_ASSERT_EQUAL_UINT8(3, fit.pointCount);
    TEST_ASSERT_EQUAL_UINT16(200, fit.referenceSpanPpm);
    TEST_ASSERT_EQUAL_UINT16(200, fit.rawSpanPpm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, fit.scale);
    TEST_ASSERT_EQUAL_INT16(-10, fit.offsetPpm);
}

void test_tds_fit_one_reference_point_with_different_scale() {
    TdsCalibrationPointInput points[1]{};
    points[0] = TdsCalibrationPointInput{160, 200};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 1, fit));
    TEST_ASSERT_TRUE(fit.valid);
    TEST_ASSERT_EQUAL_UINT8(1, fit.pointCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, fit.scale);
    TEST_ASSERT_EQUAL_INT16(0, fit.offsetPpm);
}

void test_tds_fit_rejects_low_span_for_multiple_points() {
    TdsCalibrationPointInput points[2]{};
    points[0] = TdsCalibrationPointInput{100, 100};
    points[1] = TdsCalibrationPointInput{120, 150};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));

    points[1] = TdsCalibrationPointInput{170, 120};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));
}

void test_tds_fit_rejects_duplicate_conflicts() {
    TdsCalibrationPointInput points[2]{};
    points[0] = TdsCalibrationPointInput{100, 100};
    points[1] = TdsCalibrationPointInput{100, 131};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));

    points[1] = TdsCalibrationPointInput{151, 100};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));
}

void test_tds_fit_two_saved_points_is_order_independent() {
    TdsCalibrationPointInput points[2]{};
    points[0] = TdsCalibrationPointInput{160, 150};
    points[1] = TdsCalibrationPointInput{0, 5};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 2, fit));
    TEST_ASSERT_TRUE(fit.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.10f, fit.scale);
    TEST_ASSERT_INT_WITHIN(1, -6, fit.offsetPpm);
}

void test_tds_stability_windows() {
    const std::uint16_t ordinaryStable[] = {158, 159, 160, 161, 160, 159, 160, 161, 160, 159, 160, 161};
    const std::uint16_t ordinaryUnstable[] = {140, 160, 175, 151, 166, 170, 145, 160, 172, 155, 169, 150};
    const std::uint16_t lowStable[] = {0, 1, 1, 0, 2, 1, 1, 0, 1, 2, 1, 0};

    TEST_ASSERT_TRUE(tdsReadingsStable(ordinaryStable, 12, 160, false));
    TEST_ASSERT_FALSE(tdsReadingsStable(ordinaryUnstable, 12, 160, false));
    TEST_ASSERT_TRUE(tdsReadingsStable(lowStable, 12, 1, true));
    TEST_ASSERT_FALSE(tdsReadingsStable(ordinaryStable, 11, 160, false));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_ntc_51k_pullup_50k_b3950_key_points);
    RUN_TEST(test_input_voltage_11_to_1_divider);
    RUN_TEST(test_tds_board_voltage_restores_10k_15k_input_divider);
    RUN_TEST(test_tds_formula_uses_25c_when_temperature_invalid);
    RUN_TEST(test_tds_temperature_compensation_sets_fallback_flag);
    RUN_TEST(test_tds_fit_one_point_uses_scale_with_zero_offset);
    RUN_TEST(test_tds_fit_linear_regression_matches_reference_line);
    RUN_TEST(test_tds_fit_one_reference_point_with_different_scale);
    RUN_TEST(test_tds_fit_rejects_low_span_for_multiple_points);
    RUN_TEST(test_tds_fit_rejects_duplicate_conflicts);
    RUN_TEST(test_tds_fit_two_saved_points_is_order_independent);
    RUN_TEST(test_tds_stability_windows);
    return UNITY_END();
}
