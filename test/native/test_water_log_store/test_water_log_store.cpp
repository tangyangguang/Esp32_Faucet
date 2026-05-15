#include <unity.h>

#include "app/WaterLogStore.h"

using namespace faucet;

namespace {

WaterLogRecord makeRecord(std::uint32_t startTime, std::uint32_t volumeMl) {
    return WaterLogRecord{
        startTime,
        volumeMl,
        12,
        WaterMode::Volume,
        WaterResult::Completed,
        {0, 0},
    };
}

}  // namespace

void test_log_append_keeps_newest_first() {
    WaterLogRecord records[4]{};
    WaterLogStore store(records, 4);

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));

    TEST_ASSERT_EQUAL_UINT32(300, store.newest(0)->startTime);
    TEST_ASSERT_EQUAL_UINT32(200, store.newest(1)->startTime);
    TEST_ASSERT_EQUAL_UINT32(100, store.newest(2)->startTime);
    TEST_ASSERT_NULL(store.newest(3));
}

void test_log_rolls_when_capacity_is_full() {
    WaterLogRecord records[3]{};
    WaterLogStore store(records, 3);

    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(300, 3000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(400, 4000)));

    TEST_ASSERT_TRUE(store.full());
    TEST_ASSERT_EQUAL_size_t(3, store.count());
    TEST_ASSERT_EQUAL_UINT32(400, store.newest(0)->startTime);
    TEST_ASSERT_EQUAL_UINT32(300, store.newest(1)->startTime);
    TEST_ASSERT_EQUAL_UINT32(200, store.newest(2)->startTime);
}

void test_log_reads_pages_newest_first() {
    WaterLogRecord records[6]{};
    WaterLogStore store(records, 6);
    for (std::uint32_t i = 1; i <= 6; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i * 100, i * 1000)));
    }

    WaterLogRecord page[2]{};
    TEST_ASSERT_EQUAL_size_t(2, store.readPage(0, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(600, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(500, page[1].startTime);

    TEST_ASSERT_EQUAL_size_t(2, store.readPage(2, 2, page, 2));
    TEST_ASSERT_EQUAL_UINT32(200, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(100, page[1].startTime);
}

void test_log_page_size_is_sanitized_and_limited_by_output() {
    WaterLogRecord records[5]{};
    WaterLogStore store(records, 5);
    for (std::uint32_t i = 1; i <= 5; ++i) {
        TEST_ASSERT_TRUE(store.append(makeRecord(i, i)));
    }

    WaterLogRecord page[3]{};
    TEST_ASSERT_EQUAL_size_t(3, store.readPage(0, 0, page, 3));
    TEST_ASSERT_EQUAL_UINT32(5, page[0].startTime);
    TEST_ASSERT_EQUAL_UINT32(3, page[2].startTime);
}

void test_log_rejects_missing_storage() {
    WaterLogStore store(nullptr, 0);
    WaterLogRecord page[1]{};

    TEST_ASSERT_FALSE(store.append(makeRecord(1, 1)));
    TEST_ASSERT_EQUAL_size_t(0, store.readPage(0, 1, page, 1));
    TEST_ASSERT_EQUAL_size_t(0, store.capacity());
}

void test_log_clear_resets_count_and_order() {
    WaterLogRecord records[2]{};
    WaterLogStore store(records, 2);
    TEST_ASSERT_TRUE(store.append(makeRecord(100, 1000)));
    TEST_ASSERT_TRUE(store.append(makeRecord(200, 2000)));

    store.clear();

    TEST_ASSERT_EQUAL_size_t(0, store.count());
    TEST_ASSERT_FALSE(store.full());
    TEST_ASSERT_NULL(store.newest(0));
}

void test_log_rewrites_current_boot_relative_times() {
    WaterLogRecord records[4]{};
    WaterLogStore store(records, 4);
    WaterLogRecord current = makeRecord(21, 1500);
    markWaterLogBootId(current, 9);
    WaterLogRecord previous = makeRecord(30, 500);
    markWaterLogBootId(previous, 8);

    TEST_ASSERT_TRUE(store.append(current));
    TEST_ASSERT_TRUE(store.append(previous));

    TEST_ASSERT_EQUAL_size_t(1, store.rewriteBootRelativeTimes(9, 815500000));
    TEST_ASSERT_EQUAL_UINT32(30, store.newest(0)->startTime);
    TEST_ASSERT_EQUAL_UINT16(8, waterLogBootId(*store.newest(0)));
    TEST_ASSERT_EQUAL_UINT32(815500021, store.newest(1)->startTime);
    TEST_ASSERT_EQUAL_UINT16(0, waterLogBootId(*store.newest(1)));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_log_append_keeps_newest_first);
    RUN_TEST(test_log_rolls_when_capacity_is_full);
    RUN_TEST(test_log_reads_pages_newest_first);
    RUN_TEST(test_log_page_size_is_sanitized_and_limited_by_output);
    RUN_TEST(test_log_rejects_missing_storage);
    RUN_TEST(test_log_clear_resets_count_and_order);
    RUN_TEST(test_log_rewrites_current_boot_relative_times);
    return UNITY_END();
}
