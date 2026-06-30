#include <unity.h>

#include "app/AppConfig.h"
#include "app/FilterStore.h"
#include "app/StatisticsStore.h"

#include <limits>

using namespace faucet;

void test_statistics_adds_water_to_all_periods() {
    StatisticsStore store;
    store.reset({20260505, 202619, 202605});

    store.addWater(1500, {20260505, 202619, 202605});
    const StatisticsRecord& record = store.record();

    TEST_ASSERT_EQUAL_UINT32(1500, record.todayMl);
    TEST_ASSERT_EQUAL_UINT32(1500, record.weekMl);
    TEST_ASSERT_EQUAL_UINT32(1500, record.monthMl);
    TEST_ASSERT_EQUAL_UINT32(1500, record.totalMl);
}

void test_statistics_rolls_day_week_and_month_independently() {
    StatisticsStore store;
    store.reset({20260505, 202619, 202605});
    store.addWater(1000, {20260505, 202619, 202605});

    store.addWater(2000, {20260506, 202619, 202605});
    TEST_ASSERT_EQUAL_UINT32(2000, store.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(3000, store.record().weekMl);
    TEST_ASSERT_EQUAL_UINT32(3000, store.record().monthMl);
    TEST_ASSERT_EQUAL_UINT32(3000, store.record().totalMl);

    store.addWater(3000, {20260511, 202620, 202605});
    TEST_ASSERT_EQUAL_UINT32(3000, store.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(3000, store.record().weekMl);
    TEST_ASSERT_EQUAL_UINT32(6000, store.record().monthMl);
    TEST_ASSERT_EQUAL_UINT32(6000, store.record().totalMl);

    store.addWater(4000, {20260601, 202623, 202606});
    TEST_ASSERT_EQUAL_UINT32(4000, store.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(4000, store.record().weekMl);
    TEST_ASSERT_EQUAL_UINT32(4000, store.record().monthMl);
    TEST_ASSERT_EQUAL_UINT32(10000, store.record().totalMl);
}

void test_statistics_ignores_period_keys_that_move_backwards() {
    StatisticsRecord record{};
    record.todayMl = 100;
    record.weekMl = 200;
    record.monthMl = 300;
    record.totalMl = 400;
    record.lastDayKey = 20260506;
    record.lastWeekKey = 202619;
    record.lastMonthKey = 202605;
    StatisticsStore store(record);

    store.rollPeriods({20260505, 202618, 202604});

    TEST_ASSERT_EQUAL_UINT32(100, store.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(200, store.record().weekMl);
    TEST_ASSERT_EQUAL_UINT32(300, store.record().monthMl);
    TEST_ASSERT_EQUAL_UINT32(20260506, store.record().lastDayKey);
    TEST_ASSERT_EQUAL_UINT32(202619, store.record().lastWeekKey);
    TEST_ASSERT_EQUAL_UINT32(202605, store.record().lastMonthKey);
}

void test_statistics_saturates_total_and_periods() {
    StatisticsRecord record{};
    record.todayMl = std::numeric_limits<std::uint32_t>::max() - 1;
    record.weekMl = std::numeric_limits<std::uint32_t>::max() - 1;
    record.monthMl = std::numeric_limits<std::uint32_t>::max() - 1;
    record.totalMl = std::numeric_limits<std::uint32_t>::max() - 1;
    record.lastDayKey = 20260505;
    record.lastWeekKey = 202619;
    record.lastMonthKey = 202605;
    StatisticsStore store(record);

    store.addWater(10, {20260505, 202619, 202605});

    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<std::uint32_t>::max(), store.record().todayMl);
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<std::uint32_t>::max(), store.record().weekMl);
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<std::uint32_t>::max(), store.record().monthMl);
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<std::uint32_t>::max(), store.record().totalMl);
}

void test_filter_adds_water_only_to_enabled_filters() {
    SystemConfig config = makeDefaultConfig();
    config.filters[1].enabled = true;
    config.filters[2].enabled = false;
    FilterStore store(config.filters);

    store.addWater(2500);

    TEST_ASSERT_EQUAL_UINT32(2500, store.record(0).usedMl);
    TEST_ASSERT_EQUAL_UINT32(2500, store.record(1).usedMl);
    TEST_ASSERT_EQUAL_UINT32(0, store.record(2).usedMl);
}

void test_filter_reset_sets_start_time_and_clears_flow() {
    SystemConfig config = makeDefaultConfig();
    FilterStore store(config.filters);
    store.addWater(2000);
    FilterRecord record = store.record(0);
    record.startTime = 1714502400;
    record.usedMl = 0;
    record.startBootId = 0;

    TEST_ASSERT_TRUE(store.updateFilter(0, record));

    TEST_ASSERT_EQUAL_UINT32(1714502400, store.record(0).startTime);
    TEST_ASSERT_EQUAL_UINT32(0, store.record(0).usedMl);
}

void test_filter_used_days_are_whole_days() {
    SystemConfig config = makeDefaultConfig();
    FilterStore store(config.filters);
    FilterRecord record = store.record(0);
    record.startTime = 1000;
    TEST_ASSERT_TRUE(store.updateFilter(0, record));

    TEST_ASSERT_EQUAL_UINT32(0, store.usedDays(0, 1000 + 86399));
    TEST_ASSERT_EQUAL_UINT32(1, store.usedDays(0, 1000 + 86400));
    TEST_ASSERT_EQUAL_UINT32(3, store.usedDays(0, 1000 + 3 * 86400 + 100));
}

void test_filter_rejects_invalid_index() {
    SystemConfig config = makeDefaultConfig();
    FilterStore store(config.filters);

    TEST_ASSERT_FALSE(store.updateFilter(kFilterCount, store.record(0)));
    TEST_ASSERT_EQUAL_UINT32(0, store.usedDays(kFilterCount, 9999));
}

void test_filter_update_replaces_config_fields() {
    SystemConfig config = makeDefaultConfig();
    FilterStore store(config.filters);
    FilterRecord record = store.record(0);
    record.enabled = false;
    record.recommendDays = 180;
    record.maxDays = 365;
    record.lifeMl = 2000000;
    record.startTime = 1714502400;
    record.usedMl = 123456;

    TEST_ASSERT_TRUE(store.updateFilter(0, record));

    TEST_ASSERT_FALSE(store.record(0).enabled);
    TEST_ASSERT_EQUAL_UINT32(180, store.record(0).recommendDays);
    TEST_ASSERT_EQUAL_UINT32(365, store.record(0).maxDays);
    TEST_ASSERT_EQUAL_UINT32(2000000, store.record(0).lifeMl);
    TEST_ASSERT_EQUAL_UINT32(1714502400, store.record(0).startTime);
    TEST_ASSERT_EQUAL_UINT32(123456, store.record(0).usedMl);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_statistics_adds_water_to_all_periods);
    RUN_TEST(test_statistics_rolls_day_week_and_month_independently);
    RUN_TEST(test_statistics_ignores_period_keys_that_move_backwards);
    RUN_TEST(test_statistics_saturates_total_and_periods);
    RUN_TEST(test_filter_adds_water_only_to_enabled_filters);
    RUN_TEST(test_filter_reset_sets_start_time_and_clears_flow);
    RUN_TEST(test_filter_used_days_are_whole_days);
    RUN_TEST(test_filter_rejects_invalid_index);
    RUN_TEST(test_filter_update_replaces_config_fields);
    return UNITY_END();
}
