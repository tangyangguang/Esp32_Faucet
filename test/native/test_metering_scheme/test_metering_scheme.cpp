#include <unity.h>

#include "app/MeteringScheme.h"

#include <cstring>

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
    candidate.params = MeteringParameters{40, 553, 222};
    candidate.generatedAt = 1770000000;
    candidate.sampleCount = 3;
    candidate.minActualMl = 1500;
    candidate.maxActualMl = 7500;
    candidate.maxErrorMl = 28;
    candidate.maxErrorTenthPercent = 18;
    return candidate;
}

}  // namespace

void test_default_store_has_one_enabled_active_default_scheme() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);

    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));

    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, schemes.nextSchemeId);
    TEST_ASSERT_EQUAL_size_t(1, meteringSchemeCount(schemes, true));
    const MeteringSchemeRecord* active = activeMeteringScheme(schemes);
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_UINT32(1, active->id);
    TEST_ASSERT_TRUE(active->recordUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(active->state));
    TEST_ASSERT_EQUAL_UINT32(1, active->revision);
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active->name);
    TEST_ASSERT_EQUAL_UINT32(8, active->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(36, active->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(225, active->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active->params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, active->params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Default),
                            static_cast<unsigned>(active->sourceType));
    TEST_ASSERT_FALSE(active->usedEver);
}

void test_candidate_saves_as_new_scheme_without_enabling() {
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
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                            static_cast<unsigned>(saved->state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::CalibrationSession),
                            static_cast<unsigned>(saved->sourceType));
    TEST_ASSERT_EQUAL_STRING("低压实验", saved->name);
    TEST_ASSERT_EQUAL_UINT32(40, saved->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, saved->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, saved->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, saved->params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(480, saved->params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(1, saved->revision);
    TEST_ASSERT_EQUAL_UINT16(3, saved->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(1500, saved->minActualMl);
    TEST_ASSERT_EQUAL_UINT32(7500, saved->maxActualMl);
    TEST_ASSERT_EQUAL_UINT32(28, saved->maxErrorMl);
    TEST_ASSERT_EQUAL_UINT16(18, saved->maxErrorTenthPercent);
    TEST_ASSERT_FALSE(saved->usedEver);
}

void test_long_term_candidate_saves_generated_kind_without_activation() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));
    MeteringSchemeCandidate candidate = sampleCandidate();
    candidate.sourceType = MeteringSchemeSource::LongTermSamples;

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(saveCandidateAsNewMeteringScheme(schemes, candidate, "长期样本生成", 1770000100, newId));

    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
    const MeteringSchemeRecord* saved = findMeteringSchemeById(schemes, newId);
    TEST_ASSERT_NOT_NULL(saved);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::LongTermSamples),
                            static_cast<unsigned>(saved->sourceType));
    TEST_ASSERT_EQUAL_UINT16(3, saved->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(1500, saved->minActualMl);
    TEST_ASSERT_EQUAL_UINT32(7500, saved->maxActualMl);
}

void test_manual_create_uses_source_manual_and_revision_one() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(createManualMeteringScheme(
        schemes, "手工低压", MeteringParameters{12, 180, 360}, 1770000200, newId));

    const MeteringSchemeRecord* saved = findMeteringSchemeById(schemes, newId);
    TEST_ASSERT_NOT_NULL(saved);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Manual),
                            static_cast<unsigned>(saved->sourceType));
    TEST_ASSERT_EQUAL_UINT32(1, saved->revision);
    TEST_ASSERT_EQUAL_UINT16(0, saved->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(0, saved->minActualMl);
    TEST_ASSERT_EQUAL_UINT32(0, saved->maxActualMl);
    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
}

void test_core_edit_increments_revision() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "待修改", MeteringParameters{12, 180, 360}, 1770000000);

    MeteringSchemeEdit edit = makeMeteringSchemeEdit(scheme);
    edit.params.stablePulsePerLiter = 400;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeEditKind::MeteringOrApplicability),
                            static_cast<unsigned>(classifyMeteringSchemeEdit(scheme, edit)));
    TEST_ASSERT_TRUE(updateMeteringSchemeRecord(scheme, edit, 1770000300));
    TEST_ASSERT_EQUAL_UINT32(2, scheme.revision);
    TEST_ASSERT_EQUAL_UINT32(400, scheme.params.stablePulsePerLiter);
}

void test_name_only_edit_does_not_increment_revision() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "旧名称", MeteringParameters{12, 180, 360}, 1770000000);

    MeteringSchemeEdit edit = makeMeteringSchemeEdit(scheme);
    std::strncpy(edit.name, "新名称", sizeof(edit.name) - 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeEditKind::NameOnly),
                            static_cast<unsigned>(classifyMeteringSchemeEdit(scheme, edit)));
    TEST_ASSERT_TRUE(updateMeteringSchemeRecord(scheme, edit, 1770000500));

    TEST_ASSERT_EQUAL_STRING("新名称", scheme.name);
    TEST_ASSERT_EQUAL_UINT32(1, scheme.revision);
    TEST_ASSERT_EQUAL_UINT32(1770000500, scheme.updatedAt);
}

void test_used_scheme_rejects_metering_parameter_edit_but_allows_name_edit() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "已使用", MeteringParameters{12, 180, 360}, 1770000000);
    scheme.usedEver = true;

    MeteringSchemeEdit meteringEdit = makeMeteringSchemeEdit(scheme);
    meteringEdit.params.stablePulsePerLiter = 400;
    TEST_ASSERT_FALSE(updateMeteringSchemeRecord(scheme, meteringEdit, 1770000300));
    TEST_ASSERT_EQUAL_UINT32(360, scheme.params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(1, scheme.revision);

    MeteringSchemeEdit nameEdit = makeMeteringSchemeEdit(scheme);
    std::strncpy(nameEdit.name, "只改名称", sizeof(nameEdit.name) - 1);
    TEST_ASSERT_TRUE(updateMeteringSchemeRecord(scheme, nameEdit, 1770000400));
    TEST_ASSERT_EQUAL_STRING("只改名称", scheme.name);
    TEST_ASSERT_EQUAL_UINT32(1, scheme.revision);
}

void test_current_scheme_cannot_be_disabled_or_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "当前", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_FALSE(canDisableMeteringScheme(scheme, 2, 2));
    TEST_ASSERT_FALSE(canPhysicallyDeleteMeteringScheme(scheme, 2, 2));
}

void test_last_valid_scheme_cannot_be_deleted_even_if_not_active() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "最后一套", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_FALSE(canPhysicallyDeleteMeteringScheme(scheme, 1, 1));
}

void test_used_scheme_cannot_be_physically_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "已使用", MeteringParameters{12, 180, 360}, 1770000000);

    scheme.usedEver = true;
    TEST_ASSERT_FALSE(canPhysicallyDeleteMeteringScheme(scheme, 1, 2));
}

void test_unused_non_current_scheme_can_be_physically_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "未使用", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_TRUE(canPhysicallyDeleteMeteringScheme(scheme, 1, 2));
}

void test_metering_estimate_uses_segmented_parameters_for_target_volume() {
    const MeteringParameters params{8, 36, 225};

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
    const MeteringParameters params{0, 0, 450};

    TEST_ASSERT_EQUAL_UINT32(675, estimatePulsesForVolumeMl(params, 1500));

    const MeteringTargetEstimate estimate = meteringEstimateForTarget(params, 1500);
    TEST_ASSERT_TRUE(estimate.valid);
    TEST_ASSERT_EQUAL_UINT32(675, estimate.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(450, estimate.fullRunPulsePerLiter);
}

void test_time_estimate_uses_segmented_startup_and_stable_flow() {
    const MeteringParameters params{8, 36, 225, 5000, 480};

    TEST_ASSERT_EQUAL_UINT32(2500, estimateDurationMsForVolumeMl(params, 18));
    TEST_ASSERT_EQUAL_UINT32(5000, estimateDurationMsForVolumeMl(params, 36));
    TEST_ASSERT_EQUAL_UINT32(130000, estimateDurationMsForVolumeMl(params, 1036));

    TEST_ASSERT_EQUAL_UINT32(18, estimateVolumeMlForDurationMs(params, 2500));
    TEST_ASSERT_EQUAL_UINT32(36, estimateVolumeMlForDurationMs(params, 5000));
    TEST_ASSERT_EQUAL_UINT32(1036, estimateVolumeMlForDurationMs(params, 130000));
}

void test_time_estimate_handles_no_startup_time_segment() {
    const MeteringParameters params{0, 0, 450, 0, 600};

    TEST_ASSERT_EQUAL_UINT32(100000, estimateDurationMsForVolumeMl(params, 1000));
    TEST_ASSERT_EQUAL_UINT32(1000, estimateVolumeMlForDurationMs(params, 100000));
}

void test_time_estimate_parameter_validation() {
    TEST_ASSERT_FALSE(validMeteringSchemeParameters(MeteringParameters{0, 0, 450, 0, 0}));
    TEST_ASSERT_FALSE(validMeteringSchemeParameters(MeteringParameters{0, 0, 450, 0, 30001}));
    TEST_ASSERT_FALSE(validMeteringSchemeParameters(MeteringParameters{0, 0, 450, 60001, 600}));
    TEST_ASSERT_TRUE(validMeteringSchemeParameters(MeteringParameters{0, 0, 450, 0, 600}));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_default_store_has_one_enabled_active_default_scheme);
    RUN_TEST(test_candidate_saves_as_new_scheme_without_enabling);
    RUN_TEST(test_long_term_candidate_saves_generated_kind_without_activation);
    RUN_TEST(test_manual_create_uses_source_manual_and_revision_one);
    RUN_TEST(test_core_edit_increments_revision);
    RUN_TEST(test_name_only_edit_does_not_increment_revision);
    RUN_TEST(test_used_scheme_rejects_metering_parameter_edit_but_allows_name_edit);
    RUN_TEST(test_current_scheme_cannot_be_disabled_or_deleted);
    RUN_TEST(test_last_valid_scheme_cannot_be_deleted_even_if_not_active);
    RUN_TEST(test_used_scheme_cannot_be_physically_deleted);
    RUN_TEST(test_unused_non_current_scheme_can_be_physically_deleted);
    RUN_TEST(test_metering_estimate_uses_segmented_parameters_for_target_volume);
    RUN_TEST(test_metering_estimate_handles_no_startup_segment);
    RUN_TEST(test_time_estimate_uses_segmented_startup_and_stable_flow);
    RUN_TEST(test_time_estimate_handles_no_startup_time_segment);
    RUN_TEST(test_time_estimate_parameter_validation);
    return UNITY_END();
}
