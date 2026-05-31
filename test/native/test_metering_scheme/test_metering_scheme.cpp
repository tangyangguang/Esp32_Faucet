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
    candidate.params = MeteringParameters{40, 553, 222};
    candidate.generatedAt = 1770000000;
    candidate.sampleCount = 3;
    candidate.sampleTraceIds[0] = 101;
    candidate.sampleTraceIds[1] = 102;
    candidate.sampleTraceIds[2] = 103;
    candidate.minActualMl = 1500;
    candidate.maxActualMl = 7500;
    candidate.maxErrorMl = 28;
    candidate.maxErrorPercent = 1.8f;
    candidate.startupDurationMinSec = 4;
    candidate.startupDurationMaxSec = 6;
    candidate.startupDurationMedianSec = 5;
    candidate.startupDurationAvgSec = 5;
    std::strncpy(candidate.creationSummary,
                 "样本数量 3，容量范围 1500ml-7500ml，最大误差 28ml。",
                 sizeof(candidate.creationSummary) - 1);
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
    TEST_ASSERT_TRUE(active->valid);
    TEST_ASSERT_TRUE(active->enabled);
    TEST_ASSERT_EQUAL_UINT32(1, active->revision);
    TEST_ASSERT_EQUAL_STRING("默认计量方案", active->name);
    TEST_ASSERT_EQUAL_UINT32(0, active->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, active->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultStablePulsePerLiter, active->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeSource::Default),
                            static_cast<unsigned>(active->sourceType));
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
    TEST_ASSERT_TRUE(saved->valid);
    TEST_ASSERT_TRUE(saved->enabled);
    TEST_ASSERT_EQUAL_STRING("低压实验", saved->name);
    TEST_ASSERT_EQUAL_UINT32(40, saved->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, saved->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, saved->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(1, saved->revision);
    TEST_ASSERT_EQUAL_UINT16(3, saved->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(101, saved->sampleTraceIds[0]);
    TEST_ASSERT_EQUAL_UINT32(5, saved->startupDurationAvgSec);
    TEST_ASSERT_NOT_NULL(std::strstr(saved->creationSummary, "样本数量 3"));
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
    TEST_ASSERT_EQUAL_UINT32(0, saved->sampleTraceIds[0]);
    TEST_ASSERT_NOT_NULL(std::strstr(saved->creationSummary, "手工创建"));
    TEST_ASSERT_EQUAL_UINT32(1, schemes.activeSchemeId);
}

void test_core_or_environment_edit_increments_revision() {
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

    edit = makeMeteringSchemeEdit(scheme);
    std::strncpy(edit.conditionLabel, "低水压", sizeof(edit.conditionLabel) - 1);
    TEST_ASSERT_TRUE(updateMeteringSchemeRecord(scheme, edit, 1770000400));
    TEST_ASSERT_EQUAL_UINT32(3, scheme.revision);
    TEST_ASSERT_EQUAL_STRING("低水压", scheme.conditionLabel);
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

void test_current_scheme_cannot_be_disabled_or_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "当前", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_FALSE(canDisableMeteringScheme(scheme, 2, 2));
    TEST_ASSERT_FALSE(canPhysicallyDeleteMeteringScheme(scheme, 2));
}

void test_used_or_dirty_scheme_cannot_be_physically_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "已使用", MeteringParameters{12, 180, 360}, 1770000000);

    scheme.useCount = 1;
    TEST_ASSERT_FALSE(canPhysicallyDeleteMeteringScheme(scheme, 1));

    scheme.useCount = 0;
    scheme.usageStatsDirty = true;
    TEST_ASSERT_FALSE(canPhysicallyDeleteMeteringScheme(scheme, 1));
}

void test_unused_non_current_scheme_can_be_physically_deleted() {
    MeteringSchemeRecord scheme{};
    initializeManualMeteringScheme(
        scheme, 2, "未使用", MeteringParameters{12, 180, 360}, 1770000000);

    TEST_ASSERT_TRUE(canPhysicallyDeleteMeteringScheme(scheme, 1));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_default_store_has_one_enabled_active_default_scheme);
    RUN_TEST(test_candidate_saves_as_new_scheme_without_enabling);
    RUN_TEST(test_manual_create_uses_source_manual_and_revision_one);
    RUN_TEST(test_core_or_environment_edit_increments_revision);
    RUN_TEST(test_name_only_edit_does_not_increment_revision);
    RUN_TEST(test_current_scheme_cannot_be_disabled_or_deleted);
    RUN_TEST(test_used_or_dirty_scheme_cannot_be_physically_deleted);
    RUN_TEST(test_unused_non_current_scheme_can_be_physically_deleted);
    return UNITY_END();
}
