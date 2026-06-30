#include <unity.h>

#include "app/FlowMeter.h"

using namespace faucet;

void test_counts_valid_pulses_and_converts_to_volume() {
    FlowMeter meter(MeteringParameters{0, 0, 500});

    for (std::uint32_t i = 0; i < 10; ++i) {
        TEST_ASSERT_TRUE(meter.onPulse(1000 + i * 2000));
    }

    FlowSnapshot snapshot = meter.snapshot(1000 + 9 * 2000);
    TEST_ASSERT_EQUAL_UINT32(10, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(20, snapshot.volumeMl);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.filteredPulses);
}

void test_default_constructor_uses_builtin_yfs201_startup_parameters() {
    FlowMeter meter;

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_EQUAL_UINT32(16, meter.snapshot(1000).volumeMl);

    for (std::uint32_t i = 1; i < 4; ++i) {
        TEST_ASSERT_TRUE(meter.onPulse(1000 + i * 2000));
    }
    TEST_ASSERT_EQUAL_UINT32(65, meter.snapshot(9000).volumeMl);

    for (std::uint32_t i = 4; i < 8; ++i) {
        TEST_ASSERT_TRUE(meter.onPulse(1000 + i * 2000));
    }
    TEST_ASSERT_EQUAL_UINT32(130, meter.snapshot(15000).volumeMl);
}

void test_segmented_startup_volume_is_spread_across_startup_pulses() {
    FlowMeter meter(MeteringParameters{4, 80, 200}, kDefaultPulseFilterUs);

    TEST_ASSERT_EQUAL_UINT32(0, meter.snapshot(1000).volumeMl);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_EQUAL_UINT32(20, meter.snapshot(1000).volumeMl);
    TEST_ASSERT_TRUE(meter.onPulse(2000));
    TEST_ASSERT_TRUE(meter.onPulse(3000));
    TEST_ASSERT_TRUE(meter.onPulse(4000));
    TEST_ASSERT_EQUAL_UINT32(80, meter.snapshot(4000).volumeMl);
}

void test_segmented_stable_stage_uses_stable_pulse_per_liter_after_startup() {
    FlowMeter meter(MeteringParameters{4, 80, 200}, kDefaultPulseFilterUs);

    for (std::uint32_t i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(meter.onPulse(1000 + i * 1000));
    }

    TEST_ASSERT_EQUAL_UINT32(90, meter.snapshot(6000).volumeMl);
}

void test_segmented_startup_pulse_offset_can_have_zero_startup_volume() {
    FlowMeter meter(MeteringParameters{4, 0, 200}, kDefaultPulseFilterUs);

    for (std::uint32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(meter.onPulse(1000 + i * 1000));
        TEST_ASSERT_EQUAL_UINT32(0, meter.snapshot(1000 + i * 1000).volumeMl);
    }
    TEST_ASSERT_TRUE(meter.onPulse(5000));
    TEST_ASSERT_EQUAL_UINT32(5, meter.snapshot(5000).volumeMl);
}

void test_filters_pulses_inside_filter_window() {
    FlowMeter meter(MeteringParameters{0, 0, 500}, 1000);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_FALSE(meter.onPulse(1500));
    TEST_ASSERT_TRUE(meter.onPulse(2000));

    FlowSnapshot snapshot = meter.snapshot(2000);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.filteredPulses);
}

void test_pulse_exactly_at_filter_boundary_is_accepted() {
    FlowMeter meter(MeteringParameters{0, 0, 500}, 1000);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_TRUE(meter.onPulse(2000));

    TEST_ASSERT_EQUAL_UINT32(2, meter.snapshot(2000).pulseCount);
}

void test_current_flow_uses_recent_pulse_interval() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));

    FlowSnapshot snapshot = meter.snapshot(2000000);
    TEST_ASSERT_EQUAL_UINT32(60, snapshot.instantFlowMlPerMin);
}

void test_current_flow_survives_micros_wrap() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});
    const std::uint32_t firstPulseUs = 0xFFFFF000UL;
    const std::uint32_t secondPulseUs = firstPulseUs + static_cast<std::uint32_t>(1000000);

    TEST_ASSERT_TRUE(meter.onPulse(firstPulseUs));
    TEST_ASSERT_TRUE(meter.onPulse(secondPulseUs));

    FlowSnapshot snapshot = meter.snapshot(secondPulseUs);
    TEST_ASSERT_EQUAL_UINT32(60, snapshot.instantFlowMlPerMin);
}

void test_current_flow_expires_when_no_recent_pulse() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));

    FlowSnapshot snapshot = meter.snapshot(5000001);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.instantFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.displayFlowMlPerMin);
}

void test_window_flow_uses_recent_two_second_pulse_count() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(1500000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));
    TEST_ASSERT_TRUE(meter.onPulse(2500000));

    FlowSnapshot snapshot = meter.snapshot(2500000);
    TEST_ASSERT_EQUAL_UINT32(120, snapshot.windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(120, snapshot.currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(120, snapshot.displayFlowMlPerMin);
}

void test_window_flow_slides_out_old_pulses() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});

    TEST_ASSERT_TRUE(meter.onPulse(0));
    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));
    TEST_ASSERT_TRUE(meter.onPulse(3000000));

    FlowSnapshot snapshot = meter.snapshot(3000000);
    TEST_ASSERT_EQUAL_UINT32(90, snapshot.windowFlowMlPerMin);
}

void test_low_flow_window_extends_to_three_seconds() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(4000000));

    FlowSnapshot snapshot = meter.snapshot(4000000);
    TEST_ASSERT_EQUAL_UINT32(40, snapshot.windowFlowMlPerMin);
}

void test_display_flow_smooths_window_changes() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_TRUE(meter.onPulse(1500000));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));
    TEST_ASSERT_TRUE(meter.onPulse(2500000));
    TEST_ASSERT_EQUAL_UINT32(120, meter.snapshot(2500000).displayFlowMlPerMin);

    TEST_ASSERT_TRUE(meter.onPulse(3000000));
    FlowSnapshot snapshot = meter.snapshot(3000000);
    TEST_ASSERT_EQUAL_UINT32(150, snapshot.windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(129, snapshot.displayFlowMlPerMin);
}

void test_filtered_pulses_do_not_enter_flow_window() {
    FlowMeter meter(MeteringParameters{0, 0, 1000}, 1000);

    TEST_ASSERT_TRUE(meter.onPulse(1000000));
    TEST_ASSERT_FALSE(meter.onPulse(1000500));
    TEST_ASSERT_TRUE(meter.onPulse(2000000));

    FlowSnapshot snapshot = meter.snapshot(2000000);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.filteredPulses);
    TEST_ASSERT_EQUAL_UINT32(60, snapshot.windowFlowMlPerMin);
}

void test_rejects_invalid_metering_parameters() {
    FlowMeter meter(MeteringParameters{0, 0, 500});

    TEST_ASSERT_FALSE(meter.setMeteringParameters(MeteringParameters{4, 80, 0}));
    TEST_ASSERT_FALSE(meter.setMeteringParameters(MeteringParameters{0, 80, 500}));
    TEST_ASSERT_TRUE(meter.setMeteringParameters(MeteringParameters{4, 0, 500}));
    TEST_ASSERT_TRUE(meter.setMeteringParameters(MeteringParameters{0, 0, 1000}));
}

void test_pulse_per_liter_boundaries_are_accepted() {
    FlowMeter meter(MeteringParameters{0, 0, kMinSegmentedPulsePerLiter});

    TEST_ASSERT_TRUE(meter.setMeteringParameters(MeteringParameters{0, 0, kMaxSegmentedPulsePerLiter}));
    TEST_ASSERT_TRUE(meter.setMeteringParameters(MeteringParameters{0, 0, kMinSegmentedPulsePerLiter}));
    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_TRUE(meter.onPulse(2000));

    TEST_ASSERT_EQUAL_UINT32(40, meter.snapshot(2000).volumeMl);
}

void test_high_frequency_flow_saturates_without_overflow() {
    FlowMeter meter(MeteringParameters{0, 0, kMinSegmentedPulsePerLiter}, 0);

    TEST_ASSERT_TRUE(meter.onPulse(1000));
    TEST_ASSERT_TRUE(meter.onPulse(1001));

    TEST_ASSERT_UINT32_WITHIN(100UL, 1200000000UL, meter.snapshot(1001).instantFlowMlPerMin);
}

void test_reset_clears_counts_and_flow() {
    FlowMeter meter(MeteringParameters{0, 0, 1000});
    meter.onPulse(1000);
    meter.onPulse(2000);

    meter.reset();

    FlowSnapshot snapshot = meter.snapshot(2000);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.volumeMl);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.currentFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.instantFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.windowFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.displayFlowMlPerMin);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_counts_valid_pulses_and_converts_to_volume);
    RUN_TEST(test_default_constructor_uses_builtin_yfs201_startup_parameters);
    RUN_TEST(test_segmented_startup_volume_is_spread_across_startup_pulses);
    RUN_TEST(test_segmented_stable_stage_uses_stable_pulse_per_liter_after_startup);
    RUN_TEST(test_segmented_startup_pulse_offset_can_have_zero_startup_volume);
    RUN_TEST(test_filters_pulses_inside_filter_window);
    RUN_TEST(test_pulse_exactly_at_filter_boundary_is_accepted);
    RUN_TEST(test_current_flow_uses_recent_pulse_interval);
    RUN_TEST(test_current_flow_survives_micros_wrap);
    RUN_TEST(test_current_flow_expires_when_no_recent_pulse);
    RUN_TEST(test_window_flow_uses_recent_two_second_pulse_count);
    RUN_TEST(test_window_flow_slides_out_old_pulses);
    RUN_TEST(test_low_flow_window_extends_to_three_seconds);
    RUN_TEST(test_display_flow_smooths_window_changes);
    RUN_TEST(test_filtered_pulses_do_not_enter_flow_window);
    RUN_TEST(test_rejects_invalid_metering_parameters);
    RUN_TEST(test_pulse_per_liter_boundaries_are_accepted);
    RUN_TEST(test_high_frequency_flow_saturates_without_overflow);
    RUN_TEST(test_reset_clears_counts_and_flow);
    return UNITY_END();
}
