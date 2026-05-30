#include <unity.h>

#include "app/AppConfig.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace faucet;

void test_default_config_matches_product_defaults() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_EQUAL_UINT32(kDefaultConfirmTimeoutSec, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultMaxOutTimeSec, config.maxOutTimeSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultMaxOutVolumeMl, config.maxOutVolumeMl);
    TEST_ASSERT_EQUAL_UINT8(kDefaultOverflowPercent, config.overflowPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultNoFlowTimeoutSec, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(10, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultHighFlowMlPerMin, config.highFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(kDefaultHighFlowDurationSec, config.highFlowDurationSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultPauseTimeoutSec, config.pauseTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(90, config.pauseTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultVolumeAdjustStepMl, config.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultTimeAdjustStepSec, config.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultRecentPulseTraceCount, config.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(10, config.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(1, kMinRecentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(10, kMaxRecentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT8(0, config.activeMeteringSlot);
    TEST_ASSERT_FALSE(config.meteringCandidate.ready);
    TEST_ASSERT_EQUAL_UINT32(0, config.meteringCandidate.params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, config.meteringCandidate.params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultStablePulsePerLiter, config.meteringCandidate.params.stablePulsePerLiter);
    for (std::size_t i = 0; i < kMeteringSlotCount; ++i) {
        TEST_ASSERT_TRUE(config.meteringSlots[i].valid);
        TEST_ASSERT_EQUAL_UINT32(0, config.meteringSlots[i].params.startupPulseCount);
        TEST_ASSERT_EQUAL_UINT32(0, config.meteringSlots[i].params.startupVolumeMl);
        TEST_ASSERT_EQUAL_UINT32(kDefaultStablePulsePerLiter, config.meteringSlots[i].params.stablePulsePerLiter);
    }
    TEST_ASSERT_EQUAL_STRING("参数槽 1", config.meteringSlots[0].name);
    TEST_ASSERT_EQUAL_UINT32(kDefaultValveFullPowerSec, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT32(5, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(kDefaultDisplaySleepSec, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(kDefaultResultDisplaySec, config.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT8(kDefaultLcdI2cAddress, config.lcdI2cAddress);
    TEST_ASSERT_TRUE(config.beepEnabled);
}

void test_default_presets_use_two_enabled_volume_presets() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_TRUE(config.presets[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PresetType::Volume), static_cast<unsigned>(config.presets[0].type));
    TEST_ASSERT_EQUAL_UINT32(1500, config.presets[0].value);
    TEST_ASSERT_EQUAL_STRING("1.5L", config.presets[0].name);

    TEST_ASSERT_TRUE(config.presets[1].enabled);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PresetType::Volume), static_cast<unsigned>(config.presets[1].type));
    TEST_ASSERT_EQUAL_UINT32(7500, config.presets[1].value);
    TEST_ASSERT_EQUAL_STRING("7.5L", config.presets[1].name);

    for (std::size_t i = 2; i < kPresetCount; ++i) {
        TEST_ASSERT_FALSE(config.presets[i].enabled);
        TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(PresetType::Volume), static_cast<unsigned>(config.presets[i].type));
        TEST_ASSERT_EQUAL_UINT32(1000, config.presets[i].value);
        TEST_ASSERT_EQUAL_STRING("预设", config.presets[i].name);
    }
}

void test_default_filters_support_six_lightweight_records() {
    const SystemConfig config = makeDefaultConfig();

    TEST_ASSERT_TRUE(config.filters[0].enabled);
    TEST_ASSERT_EQUAL_STRING("第1级滤芯", config.filters[0].name);
    TEST_ASSERT_EQUAL_UINT32(180, config.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(180, config.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].lifeMl);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].usedMl);
    TEST_ASSERT_EQUAL_UINT32(0, config.filters[0].startBootId);

    for (std::size_t i = 1; i < kFilterCount; ++i) {
        char name[kFilterNameLength]{};
        std::snprintf(name, sizeof(name), "第%u级滤芯", static_cast<unsigned>(i + 1));
        TEST_ASSERT_FALSE(config.filters[i].enabled);
        TEST_ASSERT_EQUAL_STRING(name, config.filters[i].name);
        TEST_ASSERT_EQUAL_UINT32(180, config.filters[i].recommendDays);
        TEST_ASSERT_EQUAL_UINT32(180, config.filters[i].maxDays);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].lifeMl);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].startTime);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].usedMl);
        TEST_ASSERT_EQUAL_UINT32(0, config.filters[i].startBootId);
    }
}

void test_sanitize_config_clamps_scalar_ranges() {
    SystemConfig config = makeDefaultConfig();
    config.confirmTimeoutSec = 0;
    config.maxOutTimeSec = 999999;
    config.maxOutVolumeMl = 1;
    config.overflowPercent = 99;
    config.noFlowTimeoutSec = 0;
    config.highFlowMlPerMin = 1;
    config.highFlowDurationSec = 99;
    config.pauseTimeoutSec = 999999;
    config.volumeAdjustStepMl = 0;
    config.timeAdjustStepSec = 0;
    config.recentPulseTraceCount = 999999;
    config.activeMeteringSlot = 99;
    config.meteringSlots[0].params = MeteringParameters{999999, 999999, 999999};
    config.meteringSlots[1].valid = false;
    config.meteringSlots[1].params = MeteringParameters{4, 80, 222};
    config.meteringCandidate.ready = true;
    config.meteringCandidate.params = MeteringParameters{0, 80, 222};
    config.valveFullPowerSec = 0;
    config.valveHoldDutyPercent = 1;
    config.displaySleepSec = 999999;
    config.resultDisplaySec = 999999;
    config.lcdI2cAddress = 0;

    sanitizeConfig(config);

    TEST_ASSERT_EQUAL_UINT32(3, config.confirmTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(7200, config.maxOutTimeSec);
    TEST_ASSERT_EQUAL_UINT32(1000, config.maxOutVolumeMl);
    TEST_ASSERT_EQUAL_UINT8(50, config.overflowPercent);
    TEST_ASSERT_EQUAL_UINT32(1, config.noFlowTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(1000, config.highFlowMlPerMin);
    TEST_ASSERT_EQUAL_UINT32(30, config.highFlowDurationSec);
    TEST_ASSERT_EQUAL_UINT32(3600, config.pauseTimeoutSec);
    TEST_ASSERT_EQUAL_UINT32(10, config.volumeAdjustStepMl);
    TEST_ASSERT_EQUAL_UINT32(1, config.timeAdjustStepSec);
    TEST_ASSERT_EQUAL_UINT32(kMaxRecentPulseTraceCount, config.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT32(10, config.recentPulseTraceCount);
    TEST_ASSERT_EQUAL_UINT8(0, config.activeMeteringSlot);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupPulseCount, config.meteringSlots[0].params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedStartupVolumeMl, config.meteringSlots[0].params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(kMaxSegmentedPulsePerLiter, config.meteringSlots[0].params.stablePulsePerLiter);
    TEST_ASSERT_TRUE(config.meteringSlots[1].valid);
    TEST_ASSERT_FALSE(config.meteringCandidate.ready);
    TEST_ASSERT_EQUAL_UINT32(1, config.valveFullPowerSec);
    TEST_ASSERT_EQUAL_UINT8(kMinValveHoldDutyPercent, config.valveHoldDutyPercent);
    TEST_ASSERT_EQUAL_UINT32(300, config.displaySleepSec);
    TEST_ASSERT_EQUAL_UINT32(60, config.resultDisplaySec);
    TEST_ASSERT_EQUAL_UINT8(0x03, config.lcdI2cAddress);

    config = makeDefaultConfig();
    config.recentPulseTraceCount = 0;
    sanitizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(kMinRecentPulseTraceCount, config.recentPulseTraceCount);
}

void test_metering_slot_operations_enforce_candidate_save_and_enable_rules() {
    SystemConfig config = makeDefaultConfig();
    config.meteringSlots[2].valid = false;

    TEST_ASSERT_FALSE(enableMeteringSlot(config, 2));

    config.meteringCandidate.ready = true;
    config.meteringCandidate.params = MeteringParameters{6, 80, 225};
    std::strncpy(config.meteringCandidate.note, "样本数量 3，容量范围 1.0L-7.5L，最大误差 20ml", sizeof(config.meteringCandidate.note) - 1);

    TEST_ASSERT_TRUE(saveCandidateToMeteringSlot(config, 1, 1770000000));
    TEST_ASSERT_EQUAL_UINT8(0, config.activeMeteringSlot);
    TEST_ASSERT_TRUE(config.meteringSlots[1].valid);
    TEST_ASSERT_EQUAL_UINT32(6, config.meteringSlots[1].params.startupPulseCount);
    TEST_ASSERT_NOT_NULL(std::strstr(config.meteringSlots[1].creationNote, "样本数量 3"));

    TEST_ASSERT_TRUE(enableMeteringSlot(config, 1));
    TEST_ASSERT_EQUAL_UINT8(1, config.activeMeteringSlot);

    config.meteringCandidate.params = MeteringParameters{7, 90, 230};
    TEST_ASSERT_TRUE(saveCandidateToMeteringSlot(config, 1, 1770000300));
    TEST_ASSERT_EQUAL_UINT8(1, config.activeMeteringSlot);
    TEST_ASSERT_EQUAL_UINT32(7, activeMeteringParameters(config).startupPulseCount);
}

void test_sanitize_config_clamps_preset_values_by_type() {
    SystemConfig config = makeDefaultConfig();
    config.presets[0].type = PresetType::Volume;
    config.presets[0].value = 1;
    config.presets[1].type = PresetType::Volume;
    config.presets[1].value = 999999;
    config.presets[2].type = PresetType::Time;
    config.presets[2].value = 1;
    config.presets[3].type = PresetType::Time;
    config.presets[3].value = 999999;
    std::memset(config.presets[4].name, 'x', sizeof(config.presets[4].name));
    std::memset(config.filters[0].name, 'y', sizeof(config.filters[0].name));
    config.filters[0].recommendDays = 999999;
    config.filters[0].maxDays = 1;
    config.filters[0].lifeMl = 99999999;

    sanitizeConfig(config);

    TEST_ASSERT_EQUAL_UINT32(kMinVolumePresetMl, config.presets[0].value);
    TEST_ASSERT_EQUAL_UINT32(kMaxVolumePresetMl, config.presets[1].value);
    TEST_ASSERT_EQUAL_UINT32(kMinTimePresetSec, config.presets[2].value);
    TEST_ASSERT_EQUAL_UINT32(kMaxTimePresetSec, config.presets[3].value);
    TEST_ASSERT_EQUAL_CHAR('\0', config.presets[4].name[kPresetNameLength - 1]);
    TEST_ASSERT_EQUAL_CHAR('\0', config.filters[0].name[kFilterNameLength - 1]);
    TEST_ASSERT_EQUAL_UINT32(kMaxFilterLifeDays, config.filters[0].recommendDays);
    TEST_ASSERT_EQUAL_UINT32(kMaxFilterLifeDays, config.filters[0].maxDays);
    TEST_ASSERT_EQUAL_UINT32(kMaxFilterLifeMl, config.filters[0].lifeMl);
}

void test_record_page_size_and_filter_life_helpers() {
    TEST_ASSERT_EQUAL_UINT16(10, kDefaultRecordPageSize);
    TEST_ASSERT_EQUAL_UINT16(kDefaultRecordPageSize, sanitizeRecordPageSize(0));
    TEST_ASSERT_EQUAL_UINT16(20, sanitizeRecordPageSize(20));
    TEST_ASSERT_EQUAL_UINT16(30, sanitizeRecordPageSize(30));
    TEST_ASSERT_EQUAL_UINT16(1, sanitizeRecordPageSize(1));
    TEST_ASSERT_EQUAL_UINT16(kMaxRecordPageSize, sanitizeRecordPageSize(999));

    FilterRecord filter = makeDefaultConfig().filters[0];
    filter.enabled = true;
    filter.recommendDays = 90;
    filter.maxDays = 180;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::Normal),
                            static_cast<unsigned>(filterLifeStatus(filter, 89)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::RecommendReplace),
                            static_cast<unsigned>(filterLifeStatus(filter, 90)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::Expired),
                            static_cast<unsigned>(filterLifeStatus(filter, 180)));
    filter.usedMl = 2000;
    filter.lifeMl = 2000;
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(FilterLifeStatus::Expired),
                            static_cast<unsigned>(filterLifeStatus(filter, 1)));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_default_config_matches_product_defaults);
    RUN_TEST(test_default_presets_use_two_enabled_volume_presets);
    RUN_TEST(test_default_filters_support_six_lightweight_records);
    RUN_TEST(test_sanitize_config_clamps_scalar_ranges);
    RUN_TEST(test_metering_slot_operations_enforce_candidate_save_and_enable_rules);
    RUN_TEST(test_sanitize_config_clamps_preset_values_by_type);
    RUN_TEST(test_record_page_size_and_filter_life_helpers);
    return UNITY_END();
}
