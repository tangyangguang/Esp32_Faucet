#include <unity.h>

#include "app/MeteringScheme.h"

using namespace faucet;

namespace {

template <std::size_t N>
MeteringSchemeCollection collectionFor(MeteringSchemeRecord (&records)[N]) {
    return MeteringSchemeCollection{records, N, 0, 0};
}

MeteringSchemeCandidate sampleCandidate() {
    MeteringSchemeCandidate candidate{};
    candidate.ready = true;
    candidate.sourceType = MeteringSchemeSource::CalibrationSession;
    candidate.params = MeteringParameters{40, 553, 222, 5000, 1950};
    candidate.generatedAt = 1770000000;
    candidate.sampleCount = 3;
    candidate.minActualMl = 1500;
    candidate.maxActualMl = 7500;
    candidate.maxErrorMl = 28;
    candidate.maxErrorTenthPercent = 18;
    return candidate;
}

}  // namespace

void test_metering_history_capacity_is_twenty() {
    TEST_ASSERT_EQUAL_size_t(20, kMeteringSchemeStoreSlotCount);
}

void test_default_store_has_one_current_default_scheme() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);

    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));

    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, schemes.nextSchemeId);
    TEST_ASSERT_EQUAL_size_t(1, meteringSchemeCount(schemes));
    const MeteringSchemeRecord* active = activeMeteringScheme(schemes);
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_UINT32(1, active->id);
    TEST_ASSERT_TRUE(active->recordUsed);
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active->name);
    TEST_ASSERT_EQUAL_UINT32(8, active->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(130, active->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(248, active->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active->params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(1950, active->params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Default),
                            static_cast<unsigned>(active->sourceType));
}

void test_candidate_saves_as_new_without_switching_current() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));
    MeteringSchemeCandidate candidate = sampleCandidate();

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(saveCandidateAsNewMeteringScheme(schemes, candidate, "低压实验", 1770000100, newId));

    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, newId);
    TEST_ASSERT_EQUAL_UINT32(3, schemes.nextSchemeId);
    TEST_ASSERT_FALSE(candidate.ready);
    const MeteringSchemeRecord* saved = findMeteringSchemeById(schemes, newId);
    TEST_ASSERT_NOT_NULL(saved);
    TEST_ASSERT_TRUE(saved->recordUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::CalibrationSession),
                            static_cast<unsigned>(saved->sourceType));
    TEST_ASSERT_EQUAL_STRING("低压实验", saved->name);
    TEST_ASSERT_EQUAL_UINT32(40, saved->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, saved->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, saved->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(3, saved->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(7500, saved->maxActualMl);
}

void test_create_manual_uses_free_slot_and_fails_when_collection_is_full() {
    MeteringSchemeRecord records[2]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(createManualMeteringScheme(
        schemes, "手工低压", MeteringParameters{12, 180, 360, 7000, 900}, 1770000200, newId));
    TEST_ASSERT_EQUAL_UINT32(2, newId);
    TEST_ASSERT_EQUAL_STRING("手工低压", records[1].name);

    std::uint32_t overflowId = 0;
    TEST_ASSERT_FALSE(createManualMeteringScheme(
        schemes, "没有槽位", MeteringParameters{14, 200, 380, 7000, 900}, 1770000300, overflowId));
    TEST_ASSERT_EQUAL_UINT32(0, overflowId);
}

void test_metering_estimate_uses_segmented_parameters_for_target_volume() {
    const MeteringParameters params{8, 36, 225, 5000, 1950};

    TEST_ASSERT_EQUAL_UINT32(4, estimatePulsesForVolumeMl(params, 18));
    TEST_ASSERT_EQUAL_UINT32(8, estimatePulsesForVolumeMl(params, 36));
    TEST_ASSERT_EQUAL_UINT32(338, estimatePulsesForVolumeMl(params, 1500));
    TEST_ASSERT_EQUAL_UINT32(225, fullRunPulsePerLiter(338, 1500));

    const MeteringTargetEstimate estimate = meteringEstimateForTarget(params, 1500);
    TEST_ASSERT_TRUE(estimate.valid);
    TEST_ASSERT_EQUAL_UINT32(1500, estimate.targetMl);
    TEST_ASSERT_EQUAL_UINT32(338, estimate.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(225, estimate.fullRunPulsePerLiter);
}

void test_metering_estimate_handles_no_startup_segment() {
    const MeteringParameters params{0, 0, 450, 5000, 1950};

    TEST_ASSERT_EQUAL_UINT32(675, estimatePulsesForVolumeMl(params, 1500));

    const MeteringTargetEstimate estimate = meteringEstimateForTarget(params, 1500);
    TEST_ASSERT_TRUE(estimate.valid);
    TEST_ASSERT_EQUAL_UINT32(675, estimate.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(450, estimate.fullRunPulsePerLiter);
}

void test_metering_estimate_allows_startup_pulse_offset_without_startup_volume() {
    const MeteringParameters params{120, 0, 1900, 5000, 1900};

    TEST_ASSERT_TRUE(validMeteringSchemeParameters(params));
    TEST_ASSERT_EQUAL_UINT32(2020, estimatePulsesForVolumeMl(params, 1000));
    TEST_ASSERT_EQUAL_UINT32(36579, estimateDurationMsForVolumeMl(params, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, estimateVolumeMlForDurationMs(params, 5000));
    TEST_ASSERT_EQUAL_UINT32(1000, estimateVolumeMlForDurationMs(params, 36579));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_metering_history_capacity_is_twenty);
    RUN_TEST(test_default_store_has_one_current_default_scheme);
    RUN_TEST(test_candidate_saves_as_new_without_switching_current);
    RUN_TEST(test_create_manual_uses_free_slot_and_fails_when_collection_is_full);
    RUN_TEST(test_metering_estimate_uses_segmented_parameters_for_target_volume);
    RUN_TEST(test_metering_estimate_handles_no_startup_segment);
    RUN_TEST(test_metering_estimate_allows_startup_pulse_offset_without_startup_volume);
    return UNITY_END();
}
