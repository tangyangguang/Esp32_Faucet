# Flash Storage Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the confirmed Flash storage consolidation: compact 20,000-record history with `meteringSchemeId`, 5 long-term samples, 3 session samples, 100 metering schemes with overwrite rules, and clear incompatible/corrupt file states.

**Architecture:** Keep the current small fixed-record stores and native-testable app logic. Replace the per-record metering snapshot file with a compact `WaterRecord` field, keep statistics/filter NVS behavior, and use fixed engineering capacities instead of open-ended dynamic storage. Old files are not migrated; new stores report readable status for Web/API messaging.

**Tech Stack:** PlatformIO, C++17, ESP32 Arduino, LittleFS through Esp32Base `WaterRecordFileBackend`, NVS through `ConfigStore`, PlatformIO native tests.

---

## File Map

- `include/app/AppTypes.h`: change `WaterRecord` compact field from `pulsePerMlAtRun` to `meteringSchemeId` while preserving 36-byte size.
- `include/app/WaterRecordFileStore.h`, `src/app/WaterRecordFileStore.cpp`: add file status classification for missing, incompatible, corrupt, and skipped records if the existing store does not expose enough state.
- `include/app/WaterRecordStore.h`, `src/app/WaterRecordStore.cpp`: update aggregation and record identity to use the compact record model.
- `include/app/WaterRecordMeteringSnapshotStore.h`, `src/app/WaterRecordMeteringSnapshotStore.cpp`: delete after dependent Web/App code no longer uses it.
- `include/app/AppConfig.h`, `include/app/CalibrationSampleStore.h`, `src/main.cpp`: change long-term sample count to 5, session sample count to 3, record capacity to 20,000, and stop constructing the saved trace store.
- `include/app/AppController.h`, `src/app/AppController.cpp`: stop writing metering snapshots; write `meteringSchemeId` into `WaterRecord`; preserve statistics/filter NVS dirty flow.
- `include/app/MeteringSchemeStore.h`, `src/app/MeteringSchemeStore.cpp`: enforce 100-slot engineering capacity and overwrite rules.
- `src/app/MeteringScheme.cpp`: adjust edit/update rules so used schemes do not get metering-affecting in-place updates when the caller asks for changed parameters.
- `include/web/FaucetWeb.h`, `src/web/FaucetWeb.cpp`, `src/web/FaucetWebJson.cpp`: remove snapshot dependency; render scheme lookup by `meteringSchemeId`; show covered/incompatible text.
- Native tests under `test/native/*`: update and add focused tests for each behavior.
- Docs touched only if implementation reveals a mismatch with `docs/superpowers/specs/2026-06-11-flash-storage-consolidation-design.md`.

---

### Task 1: Compact Record Model

**Files:**
- Modify: `include/app/AppTypes.h`
- Modify: `src/app/WaterRecordStore.cpp`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Test: `test/native/test_water_record_store/test_water_record_store.cpp`

- [ ] **Step 1: Write failing tests for record size and scheme ID**

Add tests to `test/native/test_water_record_store/test_water_record_store.cpp`:

```cpp
void test_water_record_keeps_compact_size_and_scheme_id() {
    WaterRecord record{};
    record.meteringSchemeId = 42;
    TEST_ASSERT_EQUAL_UINT32(36, sizeof(WaterRecord));
    TEST_ASSERT_EQUAL_UINT32(42, record.meteringSchemeId);
}

void test_water_record_aggregation_ignores_scheme_id() {
    WaterRecord records[2]{};
    records[0].startTime = 832032000UL;
    records[0].volumeMl = 1000;
    records[0].durationSec = 10;
    records[0].meteringSchemeId = 7;
    records[1].startTime = 832032060UL;
    records[1].volumeMl = 2000;
    records[1].durationSec = 20;
    records[1].meteringSchemeId = 8;
    WaterRecordStore store(records, 2);
    TEST_ASSERT_TRUE(store.append(records[0]));
    TEST_ASSERT_TRUE(store.append(records[1]));
    const WaterUsageSummary summary = aggregateWaterRecords(store, 832032060UL, 30);
    TEST_ASSERT_EQUAL_UINT32(3000, summary.todayMl);
    TEST_ASSERT_EQUAL_UINT32(3000, summary.totalMl);
}
```

Register both tests in that file's `main()` near the other `RUN_TEST(...)` calls.

- [ ] **Step 2: Run the focused test to verify it fails**

Run:

```bash
pio test -e native -f test_water_record_store
```

Expected: compilation fails because `WaterRecord::meteringSchemeId` does not exist.

- [ ] **Step 3: Replace the compact field**

In `include/app/AppTypes.h`, replace:

```cpp
float pulsePerMlAtRun;
std::uint8_t reserved[4];
```

with:

```cpp
std::uint32_t meteringSchemeId;
std::uint8_t reserved[4];
```

Add a static assertion after `struct WaterRecord`:

```cpp
static_assert(sizeof(WaterRecord) == 36, "WaterRecord must remain compact for 20,000-record LittleFS budget");
```

In `src/app/WaterPulseTraceStore.cpp`, keep record hashing behavior stable by replacing any hashing of the removed field with:

```cpp
hashRecordField(hash, record.meteringSchemeId);
```

Do not include `meteringSchemeId` in `sameRecordIdentity(...)`; records remain identified by start time, volume, target, pulse count, duration, preset, and result.

- [ ] **Step 4: Run focused test**

Run:

```bash
pio test -e native -f test_water_record_store
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/app/AppTypes.h src/app/WaterRecordStore.cpp src/app/WaterPulseTraceStore.cpp test/native/test_water_record_store/test_water_record_store.cpp
git commit -m "refactor: store metering scheme id in water records"
```

---

### Task 2: Record File Capacity And Status

**Files:**
- Modify: `include/app/WaterRecordStore.h`
- Modify: `include/app/WaterRecordFileStore.h`
- Modify: `src/app/WaterRecordStore.cpp`
- Modify: `src/app/WaterRecordFileStore.cpp`
- Modify: `src/main.cpp`
- Test: `test/native/test_water_record_file_store/test_water_record_file_store.cpp`

- [ ] **Step 1: Add failing tests for incompatible and corrupt status**

Add status assertions to the existing bad-header tests in `test_water_record_file_store.cpp`. Use this expected API:

```cpp
TEST_ASSERT_EQUAL(WaterRecordFileStatus::Incompatible, store.status());
```

Add a corrupt-size test:

```cpp
void test_water_record_file_store_reports_corrupt_short_file() {
    FakeWaterRecordFileBackend backend;
    const std::uint8_t bad[] = {0x44, 0x52, 0x57, 0x46};
    TEST_ASSERT_TRUE(backend.writeAt("/water.bin", 0, bad, sizeof(bad)));
    WaterRecordFileStore store(backend, "/water.bin", 20000);
    TEST_ASSERT_FALSE(store.begin());
    TEST_ASSERT_EQUAL(WaterRecordFileStatus::Corrupt, store.status());
}
```

- [ ] **Step 2: Run focused test to verify it fails**

Run:

```bash
pio test -e native -f test_water_record_file_store
```

Expected: compilation fails because `WaterRecordFileStatus` and `status()` do not exist.

- [ ] **Step 3: Implement status classification**

In `include/app/WaterRecordStore.h`, add:

```cpp
enum class WaterRecordFileStatus : std::uint8_t {
    NotStarted = 0,
    Ready = 1,
    Empty = 2,
    Missing = 3,
    Incompatible = 4,
    Corrupt = 5,
    IoError = 6,
};
```

Add to `WaterRecordReader` public API:

```cpp
virtual WaterRecordFileStatus status() const = 0;
```

Add to `WaterRecordStore` and `WaterRecordFileStore` public API:

```cpp
WaterRecordFileStatus status() const override;
```

Add private member:

```cpp
WaterRecordFileStatus status_;
```

For RAM `WaterRecordStore`, return `WaterRecordFileStatus::Ready` when `capacity_ > 0`, otherwise `WaterRecordFileStatus::Empty`.

Initialize file-store `status_(WaterRecordFileStatus::NotStarted)` in the constructor. In `begin()`:

- invalid path/capacity: `IoError`
- missing file and successful initialize: `Empty`
- missing file and failed initialize: `IoError`
- header magic/version/recordSize mismatch: `Incompatible`
- short file, impossible size, or read failure: `Corrupt` or `IoError` based on operation failure
- successful load with `count == 0`: `Empty`
- successful load with records: `Ready`

In `src/main.cpp`, change:

```cpp
constexpr std::size_t kWaterRecordCapacity = 20000;
constexpr const char* kWaterRecordPath = "/faucet_records_v1.bin";
```

to:

```cpp
constexpr std::size_t kWaterRecordCapacity = 20000;
constexpr const char* kWaterRecordPath = "/faucet_records_v2.bin";
```

- [ ] **Step 4: Run focused test**

Run:

```bash
pio test -e native -f test_water_record_file_store
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/app/WaterRecordStore.h include/app/WaterRecordFileStore.h src/app/WaterRecordStore.cpp src/app/WaterRecordFileStore.cpp src/main.cpp test/native/test_water_record_file_store/test_water_record_file_store.cpp
git commit -m "feat: classify water record file status"
```

---

### Task 3: Remove Per-Record Metering Snapshot Writes

**Files:**
- Modify: `include/app/AppController.h`
- Modify: `src/app/AppController.cpp`
- Modify: `src/main.cpp`
- Modify: `include/web/FaucetWeb.h`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Write failing controller test**

In `test_app_controller.cpp`, update the completion test to assert the record carries the active scheme ID:

```cpp
TEST_ASSERT_EQUAL_UINT32(activeScheme.id, records.newest(0)->meteringSchemeId);
```

Add an assertion that completion no longer depends on a metering snapshot writer. Construct `AppController` in one test with `nullptr` for the snapshot writer after the constructor signature is simplified.

- [ ] **Step 2: Run focused test to verify it fails**

Run:

```bash
pio test -e native -f test_app_controller
```

Expected: compilation fails while `WaterRecord` construction still uses `pulsePerMlAtRun` or controller still requires snapshot store wiring.

- [ ] **Step 3: Simplify AppController wiring**

In `include/app/AppController.h` and `src/app/AppController.cpp`:

- Remove `WaterRecordMeteringSnapshotWriter* meteringSnapshots_`.
- Remove constructor parameter `WaterRecordMeteringSnapshotWriter* meteringSnapshots`.
- Remove include of `WaterRecordMeteringSnapshotStore.h` if it becomes unused.
- Construct records with:

```cpp
activeMeteringScheme_.id,
{0, 0, 0, 0},
```

in place of the old `pulsePerMlAtRun` field.

Replace the snapshot write block with:

```cpp
if (lastRecordWriteOk_ && meteringSchemes_ && !activeMeteringScheme_.usedEver) {
    if (meteringSchemes_->markUsedAfterRecordWrite(activeMeteringScheme_.id)) {
        activeMeteringScheme_.usedEver = true;
    }
}
```

This preserves the confirmed rule: if the record write fails, `usedEver` is not marked.

In `src/main.cpp`:

- Remove `kRamRecordMeteringSnapshotCapacity`.
- Remove `kWaterRecordMeteringSnapshotCapacity`.
- Remove `kWaterRecordMeteringSnapshotPath`.
- Remove `PersistentRecordMeteringSnapshotStore` globals and file store wiring.
- Stop passing snapshot store into `AppController` and Web context.

- [ ] **Step 4: Run focused test**

Run:

```bash
pio test -e native -f test_app_controller
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp src/main.cpp include/web/FaucetWeb.h test/native/test_app_controller/test_app_controller.cpp
git commit -m "refactor: remove record metering snapshot writes"
```

---

### Task 4: Web Record Details Use Scheme ID

**Files:**
- Modify: `include/web/FaucetWeb.h`
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `src/web/FaucetWebJson.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Test: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`

- [ ] **Step 1: Write failing Web source tests**

In `test_faucet_web_routes.cpp`, add source-level assertions:

```cpp
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "meteringSchemeId"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "计量方案已被覆盖"));
TEST_ASSERT_NULL(std::strstr(buffer, "WaterRecordMeteringSnapshot"));
TEST_ASSERT_NULL(std::strstr(buffer, "recordMeteringSnapshots"));
```

In `test_faucet_web_json.cpp`, update record JSON expectations so a record exposes `meteringSchemeId` and no longer exposes snapshot parameter fields.

- [ ] **Step 2: Run Web tests to verify failure**

Run:

```bash
pio test -e native -f test_faucet_web_routes -f test_faucet_web_json
```

Expected: FAIL while Web code still references `WaterRecordMeteringSnapshot`.

- [ ] **Step 3: Replace snapshot lookup helpers**

In `src/web/FaucetWeb.cpp`, delete helpers:

```cpp
findRecordMeteringSnapshot(...)
meteringSchemeForSnapshot(...)
```

Add:

```cpp
bool meteringSchemeForRecord(const WaterRecord& record, MeteringSchemeRecord& output) {
    return record.meteringSchemeId != 0 && ensureMeteringSchemesReady() &&
           g_context.meteringSchemes->findById(record.meteringSchemeId, output);
}
```

For record details, render:

- found: scheme name, ID, state, and 5 parameters from `MeteringSchemeRecord`
- missing: `计量方案已被覆盖，历史参数不可查看`
- zero ID: `计量方案不可查看`

In `include/web/FaucetWeb.h`, remove:

```cpp
const WaterRecordMeteringSnapshotReader* recordMeteringSnapshots;
WaterRecordMeteringSnapshotWriter* recordMeteringSnapshotWriter;
```

- [ ] **Step 4: Run Web tests**

Run:

```bash
pio test -e native -f test_faucet_web_routes -f test_faucet_web_json
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/web/FaucetWeb.h src/web/FaucetWeb.cpp src/web/FaucetWebJson.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp test/native/test_faucet_web_json/test_faucet_web_json.cpp
git commit -m "feat: show metering schemes from record ids"
```

---

### Task 5: Sample Store Capacity Consolidation

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `include/app/CalibrationSampleStore.h`
- Modify: `src/main.cpp`
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_calibration_sample_store/test_calibration_sample_store.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [ ] **Step 1: Write failing capacity tests**

In `test_calibration_sample_store.cpp`, assert:

```cpp
TEST_ASSERT_EQUAL_UINT32(3, kCalibrationSessionTraceSlots);
TEST_ASSERT_EQUAL_UINT32(5, kCalibrationLongTermSampleSlots);
```

In `test_water_pulse_trace_store.cpp`, update tests that assume 12 saved traces so the new production path no longer depends on `kSavedPulseTraceMaxCount`.

- [ ] **Step 2: Run focused tests to verify failure**

Run:

```bash
pio test -e native -f test_calibration_sample_store -f test_water_pulse_trace_store
```

Expected: FAIL because the current code still exposes the old saved trace count and session/long-term constants do not match the final design.

- [ ] **Step 3: Change capacities and remove old saved trace production wiring**

In `include/app/CalibrationSampleStore.h`:

```cpp
constexpr std::size_t kCalibrationSessionTraceSlots = 3;
constexpr std::size_t kCalibrationLongTermSampleSlots = 5;
```

In `include/app/AppConfig.h`, remove:

```cpp
constexpr std::uint32_t kSavedPulseTraceMaxCount = 12;
```

In `src/main.cpp`, stop constructing `g_savedPulseTraceFile` as an independent long-term pool. Existing Web routes that save long-term samples should write to `CalibrationLongTermSampleStore`.

- [ ] **Step 4: Run focused tests**

Run:

```bash
pio test -e native -f test_calibration_sample_store -f test_water_pulse_trace_store
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/app/AppConfig.h include/app/CalibrationSampleStore.h src/main.cpp src/web/FaucetWeb.cpp test/native/test_calibration_sample_store/test_calibration_sample_store.cpp test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp
git commit -m "refactor: consolidate saved traces into sample stores"
```

---

### Task 6: Metering Scheme 100-Slot Capacity And Overwrite

**Files:**
- Modify: `include/app/MeteringSchemeStore.h`
- Modify: `src/app/MeteringSchemeStore.cpp`
- Modify: `src/app/MeteringScheme.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`
- Test: `test/native/test_metering_scheme/test_metering_scheme.cpp`

- [ ] **Step 1: Write failing overwrite tests**

In `test_metering_scheme_store.cpp`, add tests for:

```cpp
void test_metering_scheme_store_uses_100_slot_capacity();
void test_metering_scheme_store_overwrites_oldest_disabled_non_active_when_full();
void test_metering_scheme_store_overwrites_oldest_non_active_when_no_disabled_slots();
void test_metering_scheme_store_never_overwrites_active_scheme();
```

Each test should create schemes until capacity is full. Use stable IDs to assert the overwritten scheme is the smallest eligible ID and the new scheme receives a fresh ID greater than every previous ID.

- [ ] **Step 2: Run focused tests to verify failure**

Run:

```bash
pio test -e native -f test_metering_scheme_store -f test_metering_scheme
```

Expected: FAIL because the store currently grows dynamically without a 100-slot overwrite policy.

- [ ] **Step 3: Add fixed capacity and victim selection**

In `include/app/MeteringSchemeStore.h`, add:

```cpp
static constexpr std::uint32_t kMeteringSchemeStoreCapacity = 100;
```

In `src/app/MeteringSchemeStore.cpp`, when adding a scheme:

1. Try `findFreeSlot(slot)`.
2. If no free slot and `header_.slotCount < kMeteringSchemeStoreCapacity`, append at `header_.slotCount`.
3. If full, find victim with:

```cpp
bool MeteringSchemeStore::findOverwriteSlot(std::size_t& slot) const {
    std::uint32_t bestDisabledId = UINT32_MAX;
    std::size_t bestDisabledSlot = kMeteringSchemeStoreCapacity;
    std::uint32_t bestAnyId = UINT32_MAX;
    std::size_t bestAnySlot = kMeteringSchemeStoreCapacity;
    for (std::size_t i = 0; i < header_.slotCount; ++i) {
        MeteringSchemeRecord record{};
        if (!readRecord(i, record) || !record.recordUsed || record.id == header_.activeSchemeId) {
            continue;
        }
        if (record.state == MeteringSchemeState::Disabled && record.id < bestDisabledId) {
            bestDisabledId = record.id;
            bestDisabledSlot = i;
        }
        if (record.id < bestAnyId) {
            bestAnyId = record.id;
            bestAnySlot = i;
        }
    }
    if (bestDisabledSlot < kMeteringSchemeStoreCapacity) {
        slot = bestDisabledSlot;
        return true;
    }
    if (bestAnySlot < kMeteringSchemeStoreCapacity) {
        slot = bestAnySlot;
        return true;
    }
    return false;
}
```

When overwriting a slot, write the new record into the victim slot and do not reuse the victim ID. `nextSchemeId` continues to advance.

- [ ] **Step 4: Enforce used-scheme edit rule**

In `src/app/MeteringScheme.cpp`, make `updateMeteringSchemeRecord(...)` reject metering-affecting edits when `scheme.usedEver` is true. Name/state-only edits remain allowed. Web edit handling must reject metering-affecting edits of used schemes with the message `已使用方案不能直接覆盖计量参数，请另存为新方案。`

- [ ] **Step 5: Run focused tests**

Run:

```bash
pio test -e native -f test_metering_scheme_store -f test_metering_scheme
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/app/MeteringSchemeStore.h src/app/MeteringSchemeStore.cpp src/app/MeteringScheme.cpp test/native/test_metering_scheme_store/test_metering_scheme_store.cpp test/native/test_metering_scheme/test_metering_scheme.cpp
git commit -m "feat: cap metering schemes with overwrite policy"
```

---

### Task 7: Remove Snapshot Store From Build

**Files:**
- Delete: `include/app/WaterRecordMeteringSnapshotStore.h`
- Delete: `src/app/WaterRecordMeteringSnapshotStore.cpp`
- Delete: `test/native/test_water_record_metering_snapshot_store/test_water_record_metering_snapshot_store.cpp`

- [ ] **Step 1: Search for remaining production references**

Run:

```bash
rg -n "WaterRecordMeteringSnapshot|recordMeteringSnapshots|faucet_record_metering" include src test/native
```

Expected: only the snapshot store files and their legacy tests remain.

- [ ] **Step 2: Delete obsolete snapshot files**

Remove the obsolete files:

```bash
rm include/app/WaterRecordMeteringSnapshotStore.h
rm src/app/WaterRecordMeteringSnapshotStore.cpp
rm -r test/native/test_water_record_metering_snapshot_store
```

Do not edit `platformio.ini`; deleting the source file removes it from native and ESP32 builds.

- [ ] **Step 3: Verify deleted files are not referenced by test filter**

Run:

```bash
find test/native -maxdepth 1 -type d -name 'test_water_record_metering_snapshot_store'
```

Expected: no output.

- [ ] **Step 4: Verify no references**

Run:

```bash
rg -n "WaterRecordMeteringSnapshot|recordMeteringSnapshots|faucet_record_metering" include src test/native
```

Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add include/app src/app test/native
git commit -m "refactor: remove record metering snapshot store"
```

---

### Task 8: Web And API Error Messages

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `src/web/FaucetWebJson.cpp`
- Modify: `include/app/WaterRecordStore.h`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Test: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`

- [ ] **Step 1: Write failing source tests for messages**

Assert the Web source contains these Chinese messages:

```cpp
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "无历史数据"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "历史记录格式不兼容"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "历史记录数据可能损坏"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "部分记录已跳过"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "计量方案已被覆盖，历史参数不可查看"));
```

- [ ] **Step 2: Run Web tests to verify failure**

Run:

```bash
pio test -e native -f test_faucet_web_routes -f test_faucet_web_json
```

Expected: FAIL until Web renders the new messages.

- [ ] **Step 3: Implement status-to-message mapping**

In `src/web/FaucetWeb.cpp`, add a local helper:

```cpp
const char* waterRecordStatusText(WaterRecordFileStatus status) {
    switch (status) {
        case WaterRecordFileStatus::Missing:
        case WaterRecordFileStatus::Empty:
            return "无历史数据";
        case WaterRecordFileStatus::Incompatible:
            return "历史记录格式不兼容";
        case WaterRecordFileStatus::Corrupt:
        case WaterRecordFileStatus::IoError:
            return "历史记录数据可能损坏";
        case WaterRecordFileStatus::Ready:
        case WaterRecordFileStatus::NotStarted:
        default:
            return "";
    }
}
```

Use `g_context.records->status()` to select the message; this is available because Task 2 adds `status()` to `WaterRecordReader`.

- [ ] **Step 4: Run Web tests**

Run:

```bash
pio test -e native -f test_faucet_web_routes -f test_faucet_web_json
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/web/FaucetWeb.cpp src/web/FaucetWebJson.cpp include/app/WaterRecordStore.h test/native/test_faucet_web_routes/test_faucet_web_routes.cpp test/native/test_faucet_web_json/test_faucet_web_json.cpp
git commit -m "feat: report record storage status in web"
```

---

### Task 9: Main Wiring And Capacity Budget Verification

**Files:**
- Modify: `src/main.cpp`
- Modify: `docs/superpowers/specs/2026-06-11-flash-storage-consolidation-design.md` only if measured sizes differ
- Test: create `test/native/test_flash_storage_budget/test_flash_storage_budget.cpp`

- [ ] **Step 1: Add size budget test**

Create `test/native/test_flash_storage_budget/test_flash_storage_budget.cpp`:

```cpp
#include <unity.h>

#include "app/AppTypes.h"
#include "app/CalibrationSampleStore.h"
#include "app/MeteringScheme.h"

using namespace faucet;

void test_flash_storage_budget_core_sizes() {
    TEST_ASSERT_EQUAL_UINT32(36, sizeof(WaterRecord));
    TEST_ASSERT_EQUAL_UINT32(3, kCalibrationSessionTraceSlots);
    TEST_ASSERT_EQUAL_UINT32(5, kCalibrationLongTermSampleSlots);
    TEST_ASSERT_EQUAL_UINT32(96, sizeof(MeteringSchemeRecord));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_flash_storage_budget_core_sizes);
    return UNITY_END();
}
```

- [ ] **Step 2: Run budget test**

Run:

```bash
pio test -e native -f test_flash_storage_budget
```

Expected: FAIL until all earlier model changes are complete.

- [ ] **Step 3: Verify main constants**

In `src/main.cpp`, final constants must include:

```cpp
constexpr std::size_t kWaterRecordCapacity = 20000;
constexpr std::size_t kWaterRecordCalibrationCapacity = 512;
constexpr const char* kWaterRecordPath = "/faucet_records_v2.bin";
constexpr const char* kMeteringSchemePath = "/faucet_metering_schemes_v1.bin";
constexpr const char* kCalibrationSessionPath = "/faucet_cal_session_v1.bin";
constexpr const char* kCalibrationSessionTracePath = "/faucet_cal_session_traces_v1.bin";
constexpr const char* kCalibrationLongTermSamplesPath = "/faucet_cal_samples_v1.bin";
```

It must not include:

```cpp
kWaterRecordMeteringSnapshotCapacity
kWaterRecordMeteringSnapshotPath
kSavedPulseTracePath
g_recordMeteringSnapshotFile
g_savedPulseTraceFile
```

- [ ] **Step 4: Run budget test**

Run:

```bash
pio test -e native -f test_flash_storage_budget
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp test/native/test_flash_storage_budget/test_flash_storage_budget.cpp docs/superpowers/specs/2026-06-11-flash-storage-consolidation-design.md
git commit -m "test: lock flash storage budget"
```

---

### Task 10: Full Verification

**Files:**
- All modified source, tests, and docs.

- [ ] **Step 1: Run all native tests**

Run:

```bash
pio test -e native
```

Expected: all native tests PASS.

- [ ] **Step 2: Build ESP32 firmware**

Run:

```bash
pio run -e esp32dev
```

Expected: build succeeds.

- [ ] **Step 3: Search for removed concepts**

Run:

```bash
rg -n "WaterRecordMeteringSnapshot|recordMeteringSnapshots|faucet_record_metering|pulsePerMlAtRun|kSavedPulseTraceMaxCount = 12|/faucet_records_v1.bin" include src test/native
```

Expected: no output.

- [ ] **Step 4: Search docs for conflicting storage rules**

Run:

```bash
rg -n "历史出水记录必须保存当时的 5 个计量参数快照|出水记录内联快照|长期样本库.*10|固定 10|目标尽量接近 20000|按 20000 条作为容量目标" docs -S --glob '!old-docs/**' --glob '!docs/superpowers/plans/**'
```

Expected: no active conflicting rule. Mentions that explicitly describe old background as obsolete are acceptable.

- [ ] **Step 5: Final status**

Run:

```bash
git status --short
```

Expected: only intentional source, test, and doc changes are present.

---

## Self-Review Notes

- Spec coverage: the plan covers record format/capacity, snapshot removal, sample capacities, scheme capacity/overwrite, statistics/filter persistence, old file status, Web messaging, tests, and build verification.
- Scope: one implementation plan is appropriate because these changes share one storage model and must be integrated atomically.
- Type consistency: all new references use `meteringSchemeId`, `WaterRecordFileStatus`, 20,000 record capacity, 3 session samples, 5 long-term samples, and 100 metering schemes.
