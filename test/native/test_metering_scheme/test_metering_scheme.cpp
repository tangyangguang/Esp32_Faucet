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

void test_default_store_has_one_current_not_deleted_default_scheme() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);

    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));

    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, schemes.nextSchemeId);
    TEST_ASSERT_EQUAL_size_t(1, meteringSchemeCount(schemes, false));
    const MeteringSchemeRecord* active = activeMeteringScheme(schemes);
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_UINT32(1, active->id);
    TEST_ASSERT_TRUE(active->recordUsed);
    TEST_ASSERT_FALSE(active->deleted);
    TEST_ASSERT_FALSE(active->usedEver);
    TEST_ASSERT_EQUAL_UINT32(1, active->revision);
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active->name);
    TEST_ASSERT_EQUAL_UINT32(8, active->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(130, active->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(248, active->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, active->params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(1950, active->params.stableFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Default),
                            static_cast<unsigned>(active->sourceType));
}

void test_candidate_saves_as_new_not_deleted_scheme_without_switching_current() {
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
    TEST_ASSERT_FALSE(saved->deleted);
    TEST_ASSERT_FALSE(saved->usedEver);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::CalibrationSession),
                            static_cast<unsigned>(saved->sourceType));
    TEST_ASSERT_EQUAL_STRING("低压实验", saved->name);
    TEST_ASSERT_EQUAL_UINT32(40, saved->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, saved->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, saved->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(5000, saved->params.startupDurationMs);
    TEST_ASSERT_EQUAL_UINT32(1950, saved->params.stableFlowMlPerMin);
}

void test_create_manual_uses_free_slot_before_deleted_slot() {
    MeteringSchemeRecord records[4]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));
    initializeManualMeteringScheme(records[1], 99, "已删除", MeteringParameters{12, 180, 360}, 1770000001);
    records[1].deleted = true;

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(createManualMeteringScheme(
        schemes, "手工低压", MeteringParameters{12, 180, 360, 7000, 900}, 1770000200, newId));

    TEST_ASSERT_EQUAL_UINT32(2, newId);
    TEST_ASSERT_TRUE(records[1].deleted);
    TEST_ASSERT_EQUAL_UINT32(2, records[2].id);
    TEST_ASSERT_FALSE(records[2].deleted);
}

void test_create_manual_reuses_deleted_slot_when_no_free_slot_exists() {
    MeteringSchemeRecord records[2]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));
    initializeManualMeteringScheme(records[1], 2, "已删除", MeteringParameters{12, 180, 360}, 1770000001);
    records[1].deleted = true;
    schemes.nextSchemeId = 3;

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(createManualMeteringScheme(
        schemes, "复用槽位", MeteringParameters{14, 200, 380}, 1770000200, newId));

    TEST_ASSERT_EQUAL_UINT32(3, newId);
    TEST_ASSERT_EQUAL_UINT32(3, records[1].id);
    TEST_ASSERT_EQUAL_STRING("复用槽位", records[1].name);
    TEST_ASSERT_FALSE(records[1].deleted);
}

void test_create_manual_fails_when_all_slots_are_not_deleted() {
    MeteringSchemeRecord records[2]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));
    initializeManualMeteringScheme(records[1], 2, "占用", MeteringParameters{12, 180, 360}, 1770000001);
    schemes.nextSchemeId = 3;

    std::uint32_t newId = 0;
    TEST_ASSERT_FALSE(createManualMeteringScheme(
        schemes, "没有槽位", MeteringParameters{14, 200, 380}, 1770000200, newId));
    TEST_ASSERT_EQUAL_UINT32(0, newId);
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

void test_used_scheme_rejects_metering_parameter_edit() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "已使用", MeteringParameters{12, 180, 360}, 1770000000);
    scheme.usedEver = true;

    MeteringSchemeEdit edit = makeMeteringSchemeEdit(scheme);
    edit.params.stablePulsePerLiter = 400;

    TEST_ASSERT_FALSE(updateMeteringSchemeRecord(scheme, edit, 1770000300));
    TEST_ASSERT_EQUAL_UINT32(1, scheme.revision);
    TEST_ASSERT_EQUAL_UINT32(360, scheme.params.stablePulsePerLiter);
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

void test_used_scheme_rejects_name_only_edit() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "旧名称", MeteringParameters{12, 180, 360}, 1770000000);
    scheme.usedEver = true;

    MeteringSchemeEdit edit = makeMeteringSchemeEdit(scheme);
    std::strncpy(edit.name, "新名称", sizeof(edit.name) - 1);

    TEST_ASSERT_FALSE(updateMeteringSchemeRecord(scheme, edit, 1770000500));
    TEST_ASSERT_EQUAL_STRING("旧名称", scheme.name);
    TEST_ASSERT_TRUE(scheme.usedEver);
    TEST_ASSERT_EQUAL_UINT32(1, scheme.revision);
}

void test_current_scheme_cannot_be_logically_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "当前", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_FALSE(canDeleteMeteringScheme(scheme, 2));
}

void test_non_current_scheme_can_be_logically_deleted_even_if_referenced() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "历史引用", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_TRUE(canDeleteMeteringScheme(scheme, 1));
}

void test_deleted_scheme_is_not_counted_in_default_list_count() {
    MeteringSchemeRecord records[2]{};
    MeteringSchemeCollection schemes = collectionFor(records);
    TEST_ASSERT_TRUE(initializeDefaultMeteringSchemes(schemes, 1770000000));
    initializeManualMeteringScheme(records[1], 2, "已删除", MeteringParameters{12, 180, 360}, 1770000001);
    records[1].deleted = true;

    TEST_ASSERT_EQUAL_size_t(1, meteringSchemeCount(schemes, false));
    TEST_ASSERT_EQUAL_size_t(2, meteringSchemeCount(schemes, true));
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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_default_store_has_one_current_not_deleted_default_scheme);
    RUN_TEST(test_candidate_saves_as_new_not_deleted_scheme_without_switching_current);
    RUN_TEST(test_create_manual_uses_free_slot_before_deleted_slot);
    RUN_TEST(test_create_manual_reuses_deleted_slot_when_no_free_slot_exists);
    RUN_TEST(test_create_manual_fails_when_all_slots_are_not_deleted);
    RUN_TEST(test_core_edit_increments_revision);
    RUN_TEST(test_used_scheme_rejects_metering_parameter_edit);
    RUN_TEST(test_name_only_edit_does_not_increment_revision);
    RUN_TEST(test_used_scheme_rejects_name_only_edit);
    RUN_TEST(test_current_scheme_cannot_be_logically_deleted);
    RUN_TEST(test_non_current_scheme_can_be_logically_deleted_even_if_referenced);
    RUN_TEST(test_deleted_scheme_is_not_counted_in_default_list_count);
    RUN_TEST(test_metering_estimate_uses_segmented_parameters_for_target_volume);
    RUN_TEST(test_metering_estimate_handles_no_startup_segment);
    return UNITY_END();
}
