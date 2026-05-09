#include <unity.h>

#include "app/FlowMeter.h"

using namespace faucet;

void test_counts_valid_pulses_and_converts_to_volume() {
    FlowMeter meter(0.5f);

    for (std::uint32_t i = 0; i < 10; ++i) {
        TEST_ASSERT_TRUE(meter.onPulse(1000 + i * 2000));
    }

    FlowSnapshot snapshot = meter.snapshot(1000 + 9 * 2000);
    TEST_ASSERT_EQUAL_UINT32(10, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(20, snapshot.volumeMl);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.rejectedPulses);
}

void test_filters_pulses_inside_filter_window() {
    FlowMeter meter(0.5f, 1000);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_FALSE(meter.onPulse(1500));
    TEST_ASSERT_TRUE(meter.onPulse(2000));

    FlowSnapshot snapshot = meter.snapshot(2000);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.rejectedPulses);
}

void test_pulse_exactly_at_filter_boundary_is_accepted() {
    FlowMeter meter(0.5f, 1000);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_TRUE(meter.onPulse(2000));

    TEST_ASSERT_EQUAL_UINT32(2, meter.snapshot(2000).pulseCount);
}

void test_current_flow_uses_recent_pulse_interval() {
    FlowMeter meter(1.0f);

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));

    FlowSnapshot snapshot = meter.snapshot(2000000);
    TEST_ASSERT_EQUAL_UINT32(60, snapshot.currentFlowMlPerMin);
}

void test_current_flow_survives_micros_wrap() {
    FlowMeter meter(1.0f);
    const std::uint32_t firstPulseUs = 0xFFFFF000UL;
    const std::uint32_t secondPulseUs = firstPulseUs + static_cast<std::uint32_t>(1000000);

    TEST_ASSERT_TRUE(meter.onPulse(firstPulseUs));
    TEST_ASSERT_TRUE(meter.onPulse(secondPulseUs));

    FlowSnapshot snapshot = meter.snapshot(secondPulseUs);
    TEST_ASSERT_EQUAL_UINT32(60, snapshot.currentFlowMlPerMin);
}

void test_current_flow_expires_when_no_recent_pulse() {
    FlowMeter meter(1.0f);

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));

    FlowSnapshot snapshot = meter.snapshot(5000001);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.currentFlowMlPerMin);
}

void test_rejects_invalid_pulse_coefficients() {
    FlowMeter meter(0.5f);

    TEST_ASSERT_FALSE(meter.setPulsePerMl(0.0f));
    TEST_ASSERT_FALSE(meter.setPulsePerMl(99.0f));
    TEST_ASSERT_TRUE(meter.setPulsePerMl(1.0f));
}

void test_pulse_coefficient_boundaries_are_accepted() {
    FlowMeter meter(kMinPulsePerMl);

    TEST_ASSERT_TRUE(meter.setPulsePerMl(kMaxPulsePerMl));
    TEST_ASSERT_TRUE(meter.setPulsePerMl(kMinPulsePerMl));
    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_TRUE(meter.onPulse(2000));

    TEST_ASSERT_EQUAL_UINT32(40, meter.snapshot(2000).volumeMl);
}

void test_high_frequency_flow_saturates_without_overflow() {
    FlowMeter meter(kMinPulsePerMl, 0);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_TRUE(meter.onPulse(1001));

    TEST_ASSERT_UINT32_WITHIN(100UL, 1200000000UL, meter.snapshot(1001).currentFlowMlPerMin);
}

void test_reset_clears_counts_and_flow() {
    FlowMeter meter(1.0f);
    meter.onPulse(1000);
    meter.onPulse(2000);

    meter.reset();

    FlowSnapshot snapshot = meter.snapshot(2000);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.volumeMl);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.currentFlowMlPerMin);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_counts_valid_pulses_and_converts_to_volume);
    RUN_TEST(test_filters_pulses_inside_filter_window);
    RUN_TEST(test_pulse_exactly_at_filter_boundary_is_accepted);
    RUN_TEST(test_current_flow_uses_recent_pulse_interval);
    RUN_TEST(test_current_flow_survives_micros_wrap);
    RUN_TEST(test_current_flow_expires_when_no_recent_pulse);
    RUN_TEST(test_rejects_invalid_pulse_coefficients);
    RUN_TEST(test_pulse_coefficient_boundaries_are_accepted);
    RUN_TEST(test_high_frequency_flow_saturates_without_overflow);
    RUN_TEST(test_reset_clears_counts_and_flow);
    return UNITY_END();
}
