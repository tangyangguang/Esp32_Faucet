# Flow Meter Metering Schemes Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fixed “参数槽” implementation with dynamic flow-meter metering schemes that support generated/manual schemes, explicit enable, enabled/disabled state, conservative usage tracking, and per-record metering snapshots.

**Architecture:** Move metering schemes out of `SystemConfig` into a separate file-backed scheme store. Runtime code keeps only the active scheme snapshot and 3 core metering parameters; Web actions operate through the scheme store. Water records keep their existing binary format, while a separate metering-snapshot metadata store records the scheme and parameter snapshot for each successfully written water record.

**Tech Stack:** C++17, PlatformIO native tests, existing `WaterRecordFileBackend` LittleFS adapter, current ESP32Base config/Web integration, existing `FaucetWeb` rendering style.

---

## Design Verdict

The completed design is approved for implementation with these guardrails:

- There is no product-level fixed scheme count. UI must not show fixed empty slots or “最多 10 套” as a user rule.
- Bottom storage may have engineering capacity limits, but it must grow by records and reuse physically deleted unused slots with new IDs.
- Used schemes are never physically deleted. A scheme is treated as used when `useCount > 0` or `usageStatsDirty == true`.
- `useCount` is cumulative and is not rebuilt by scanning water records.
- Every successful water record must be associated with a metering snapshot containing scheme ID, revision, and the 3 core parameters.
- Candidate generation and scheme save are separate; save creates a new scheme and does not enable it.
- Current active scheme must always be enabled and cannot be disabled or deleted.

## File Structure

- Create `include/app/MeteringScheme.h`: scheme structs, source/status enums, constants, validation, operation result types, pure helper functions.
- Create `src/app/MeteringScheme.cpp`: validation, create/update/enable/disable/delete rules, revision logic, candidate metadata conversion.
- Create `include/app/MeteringSchemeStore.h`: file-backed scheme persistence API and legacy migration API.
- Create `src/app/MeteringSchemeStore.cpp`: header/candidate/record-slot persistence over `WaterRecordFileBackend`.
- Create `include/app/WaterRecordMeteringSnapshotStore.h`: metering snapshot type and reader/writer interfaces.
- Create `src/app/WaterRecordMeteringSnapshotStore.cpp`: RAM and file-backed snapshot stores, modeled after `WaterRecordCalibrationStore`.
- Modify `include/app/AppConfig.h` and `src/app/AppConfig.cpp`: remove fixed metering slot/candidate fields from `SystemConfig`; keep metering limits only in the new metering module.
- Modify `include/app/ConfigStore.h` and `src/app/ConfigStore.cpp`: stop saving metering slot keys; expose legacy metering key reads for one-time migration.
- Modify `include/app/AppController.h` and `src/app/AppController.cpp`: accept an active metering scheme snapshot and write metering snapshots with records.
- Modify `src/main.cpp`: instantiate scheme store and snapshot store, run migration, load active scheme, wire Web context.
- Modify `include/web/FaucetWeb.h`, `src/web/FaucetWeb.cpp`, and `src/web/FaucetWebJson.cpp`: replace slot UI/API with dynamic scheme operations and snapshot display.
- Modify docs after implementation: `docs/10-flow-meter-metering-schemes.md`, `docs/03-software-architecture.md`, `docs/06-implementation-plan.md`, `docs/config-persistence-migration.md` if implementation details differ from this plan.

## Storage Layout

`MeteringSchemeStore` uses one file, for example `/faucet_metering_schemes_v1.bin`.

```cpp
struct MeteringSchemeFileHeader {
    std::uint32_t magic;          // 'FMS1'
    std::uint16_t version;        // 1
    std::uint16_t headerSize;
    std::uint16_t recordSize;
    std::uint16_t reserved0;
    std::uint32_t activeSchemeId;
    std::uint32_t nextSchemeId;
    std::uint32_t slotCount;
    std::uint32_t headerChecksum;
};
```

After the header, store one fixed-size `MeteringSchemeCandidate` area, followed by fixed-size `MeteringSchemeRecord` slots. New schemes first reuse `valid == false` slots, otherwise append one slot and update `slotCount`. IDs always come from `nextSchemeId` and are never reused.

`WaterRecordMeteringSnapshotFileStore` uses a separate file, for example `/faucet_record_metering_v1.bin`, with ring/upsert semantics keyed by the same water-record identity used by `WaterRecordCalibrationStore`.

---

### Task 1: Pure Metering Scheme Model

**Files:**
- Create: `include/app/MeteringScheme.h`
- Create: `src/app/MeteringScheme.cpp`
- Test: `test/native/test_metering_scheme/test_metering_scheme.cpp`

- [ ] **Step 1: Write failing tests for default, candidate save, manual create, and operation rules**

Use these test names in `test/native/test_metering_scheme/test_metering_scheme.cpp`:

```cpp
RUN_TEST(test_default_store_has_one_enabled_active_default_scheme);
RUN_TEST(test_candidate_saves_as_new_scheme_without_enabling);
RUN_TEST(test_manual_create_uses_source_manual_and_revision_one);
RUN_TEST(test_core_or_environment_edit_increments_revision);
RUN_TEST(test_name_only_edit_does_not_increment_revision);
RUN_TEST(test_current_scheme_cannot_be_disabled_or_deleted);
RUN_TEST(test_used_or_dirty_scheme_cannot_be_physically_deleted);
RUN_TEST(test_unused_non_current_scheme_can_be_physically_deleted);
```

The assertions must cover:

- default active scheme id is `1`;
- default scheme is `valid == true`, `enabled == true`, `revision == 1`;
- generated candidate save creates a new scheme, clears candidate, and leaves `activeSchemeId` unchanged;
- manual create sets `sourceType == MeteringSchemeSource::Manual` and sample stats to zero;
- editing `startupPulseCount`, `startupVolumeMl`, `stablePulsePerLiter`, `meterLabel`, `installationLabel`, `conditionLabel`, or `userNote` increments `revision`;
- editing only `name` does not increment `revision`;
- active scheme cannot be disabled or deleted;
- `useCount > 0` or `usageStatsDirty == true` blocks physical delete.

- [ ] **Step 2: Run the tests and verify RED**

Run:

```bash
pio test -e native -f test_metering_scheme
```

Expected result: build fails because `app/MeteringScheme.h` is not implemented yet.

- [ ] **Step 3: Implement the pure model**

Define these core types in `include/app/MeteringScheme.h`:

```cpp
enum class MeteringSchemeSource : std::uint8_t {
    Default = 0,
    Generated = 1,
    Manual = 2,
    Migrated = 3,
};

enum class MeteringSchemeEditKind : std::uint8_t {
    NameOnly = 0,
    MeteringOrApplicability = 1,
};

struct MeteringSchemeRecord {
    std::uint32_t id;
    bool valid;
    bool enabled;
    char name[32];
    char meterLabel[32];
    char installationLabel[32];
    char conditionLabel[48];
    char userNote[128];
    MeteringParameters params;
    MeteringSchemeSource sourceType;
    std::uint32_t revision;
    std::uint32_t createdAt;
    std::uint32_t updatedAt;
    std::uint32_t lastActivatedAt;
    std::uint32_t useCount;
    std::uint32_t lastUsedAt;
    bool usageStatsDirty;
    std::uint16_t sampleCount;
    std::uint32_t sampleTraceIds[12];
    std::uint32_t minActualMl;
    std::uint32_t maxActualMl;
    std::uint32_t maxErrorMl;
    float maxErrorPercent;
    std::uint32_t startupDurationMinSec;
    std::uint32_t startupDurationMaxSec;
    std::uint32_t startupDurationMedianSec;
    std::uint32_t startupDurationAvgSec;
    char creationSummary[192];
    char lastModifiedSummary[192];
};
```

Move metering-specific constants and validation into this module:

```cpp
constexpr std::uint32_t kDefaultStablePulsePerLiter = 450;
constexpr std::uint32_t kMinSegmentedPulsePerLiter = 50;
constexpr std::uint32_t kMaxSegmentedPulsePerLiter = 5000;
constexpr std::uint32_t kMaxSegmentedStartupPulseCount = 100000;
constexpr std::uint32_t kMaxSegmentedStartupVolumeMl = 20000;

bool validMeteringParameters(const MeteringParameters& params);
MeteringParameters defaultMeteringParameters();
bool canDisableMeteringScheme(const MeteringSchemeRecord& scheme, std::uint32_t activeSchemeId);
bool canPhysicallyDeleteMeteringScheme(const MeteringSchemeRecord& scheme, std::uint32_t activeSchemeId);
```

- [ ] **Step 4: Run model tests and verify GREEN**

Run:

```bash
pio test -e native -f test_metering_scheme
```

Expected result: all tests in `test_metering_scheme` pass.

- [ ] **Step 5: Commit**

```bash
git add include/app/MeteringScheme.h src/app/MeteringScheme.cpp test/native/test_metering_scheme/test_metering_scheme.cpp
git commit -m "feat: add flow meter metering scheme model"
```

### Task 2: Dynamic File-Backed Metering Scheme Store

**Files:**
- Create: `include/app/MeteringSchemeStore.h`
- Create: `src/app/MeteringSchemeStore.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [ ] **Step 1: Write failing store tests**

Use the existing in-memory `WaterRecordFileBackend` test pattern from `test/native/test_water_record_file_store/test_water_record_file_store.cpp`. Cover these cases:

```cpp
RUN_TEST(test_begin_initializes_default_scheme_file);
RUN_TEST(test_save_generated_result_reloads_after_restart);
RUN_TEST(test_manual_create_reuses_deleted_unused_slot_with_new_id);
RUN_TEST(test_used_scheme_delete_is_rejected_and_disable_is_explicit);
RUN_TEST(test_enable_updates_active_id_and_last_activated);
RUN_TEST(test_increment_usage_marks_dirty_when_record_update_fails);
```

The key assertion for the delete/reuse case:

```cpp
TEST_ASSERT_TRUE(store.deleteScheme(unusedId));
TEST_ASSERT_FALSE(store.findById(unusedId, removed));
TEST_ASSERT_TRUE(store.createManual("new", params, 1770000100, newId));
TEST_ASSERT_NOT_EQUAL(unusedId, newId);
```

- [ ] **Step 2: Run the tests and verify RED**

```bash
pio test -e native -f test_metering_scheme_store
```

Expected result: build fails because `MeteringSchemeStore` is missing.

- [ ] **Step 3: Implement the store API**

Expose this minimal API:

```cpp
class MeteringSchemeStore {
public:
    MeteringSchemeStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool ready() const;
    bool activeScheme(MeteringSchemeRecord& output) const;
    std::uint32_t activeSchemeId() const;
    bool findById(std::uint32_t id, MeteringSchemeRecord& output) const;
    std::size_t list(MeteringSchemeRecord* output,
                     std::size_t outputCapacity,
                     bool includeDisabled) const;

    bool loadCandidate(MeteringSchemeCandidate& output) const;
    bool saveCandidate(const MeteringSchemeCandidate& candidate);
    bool discardCandidate();
    bool saveCandidateAsNew(const char* name, std::uint32_t nowSeconds, std::uint32_t& newId);
    bool createManual(const char* name,
                      const MeteringParameters& params,
                      std::uint32_t nowSeconds,
                      std::uint32_t& newId);
    bool updateScheme(const MeteringSchemeRecord& edited, std::uint32_t nowSeconds);
    bool enableScheme(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool disableScheme(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool restoreScheme(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool deleteScheme(std::uint32_t schemeId);
    bool incrementUsageAfterRecordWrite(std::uint32_t schemeId, std::uint32_t nowSeconds);
    bool markUsageStatsDirty(std::uint32_t schemeId);
};
```

Implementation rules:

- `begin()` creates the file with one default enabled active scheme if missing or invalid.
- `saveCandidateAsNew()` never enables the new scheme.
- `deleteScheme()` physically removes only unused non-current schemes by setting `valid = false`.
- `deleteScheme()` returns false for used/current schemes; Web will expose “停用” instead.
- `incrementUsageAfterRecordWrite()` updates only the touched scheme record. If the write fails, it must attempt `markUsageStatsDirty()` and return false.
- Do not scan water records to rebuild `useCount`.

- [ ] **Step 4: Run store tests and verify GREEN**

```bash
pio test -e native -f test_metering_scheme_store
```

Expected result: all store tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/app/MeteringSchemeStore.h src/app/MeteringSchemeStore.cpp test/native/test_metering_scheme_store/test_metering_scheme_store.cpp
git commit -m "feat: persist dynamic metering schemes"
```

### Task 3: Legacy Migration Out Of SystemConfig

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `src/app/AppConfig.cpp`
- Modify: `include/app/ConfigStore.h`
- Modify: `src/app/ConfigStore.cpp`
- Test: `test/native/test_config_store/test_config_store.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [ ] **Step 1: Write failing migration tests**

Add tests that seed old `faucet_cfg` keys:

```cpp
backend.setInt("faucet_cfg", "ver", 12);
backend.setInt("faucet_cfg", "active_ms", 1);
backend.setBool("faucet_cfg", "ms1_valid", true);
backend.setStr("faucet_cfg", "ms1_name", "实验参数");
backend.setInt("faucet_cfg", "ms1_sp", 40);
backend.setInt("faucet_cfg", "ms1_sv", 553);
backend.setInt("faucet_cfg", "ms1_pl", 222);
backend.setStr("faucet_cfg", "ms1_create", "旧参数槽样本说明");
backend.setBool("faucet_cfg", "mc_ready", true);
backend.setInt("faucet_cfg", "mc_sp", 41);
backend.setInt("faucet_cfg", "mc_sv", 520);
backend.setInt("faucet_cfg", "mc_pl", 224);
backend.setStr("faucet_cfg", "mc_note", "旧生成结果说明");
backend.setInt("faucet_cfg", "mc_at", 1770000000);
```

Expected:

- `MeteringSchemeStore::migrateLegacyIfNeeded()` creates migrated schemes before `SystemConfig` stops carrying metering slots.
- the active old slot becomes current scheme;
- valid non-default old slots become enabled migrated schemes;
- old candidate becomes a `MeteringSchemeCandidate`;
- migrated schemes have `useCount == 0`, `revision == 1`, and `sourceType == Migrated`.

- [ ] **Step 2: Run migration tests and verify RED**

```bash
pio test -e native -f test_metering_scheme_store
pio test -e native -f test_config_store
```

Expected result: tests fail until migration and config removal are implemented.

- [ ] **Step 3: Remove metering ownership from SystemConfig**

Remove these from `SystemConfig`:

- `meteringSlots`
- `meteringCandidate`
- `activeMeteringSlot`
- `enableMeteringSlot`
- `saveCandidateToMeteringSlot`
- `createManualMeteringSlot`
- `updateMeteringSlot`
- `activeMeteringParameters(const SystemConfig&)`

Keep these outside `SystemConfig` in the metering module:

- `MeteringParameters`
- `validMeteringParameters`
- default metering constants

Bump `kConfigVersion` from `12` to `13`, and stop writing old keys `active_ms`, `ms*_valid`, `ms*_name`, `ms*_sp`, `ms*_sv`, `ms*_pl`, `ms*_create`, `ms*_modify`, `ms*_mod_at`, `mc_ready`, `mc_sp`, `mc_sv`, `mc_pl`, `mc_note`, `mc_at`.

- [ ] **Step 4: Implement legacy reader for old metering keys**

Add a one-time migration helper near `ConfigStore` or in `MeteringSchemeStore`:

```cpp
bool migrateLegacyMeteringFromConfig(ConfigBackend& backend,
                                     MeteringSchemeStore& store,
                                     std::uint32_t nowSeconds);
```

This helper must read old keys from namespace `faucet_cfg` but must not delete them. The new scheme store file is the migration marker; if it already contains valid schemes, migration does not run again.

- [ ] **Step 5: Run migration tests and verify GREEN**

```bash
pio test -e native -f test_metering_scheme_store
pio test -e native -f test_config_store
```

Expected result: config tests pass with no metering fields in `SystemConfig`; migration tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/AppConfig.h src/app/AppConfig.cpp include/app/ConfigStore.h src/app/ConfigStore.cpp test/native/test_config_store/test_config_store.cpp test/native/test_metering_scheme_store/test_metering_scheme_store.cpp
git commit -m "refactor: migrate metering schemes out of system config"
```

### Task 4: Water Record Metering Snapshot Store

**Files:**
- Create: `include/app/WaterRecordMeteringSnapshotStore.h`
- Create: `src/app/WaterRecordMeteringSnapshotStore.cpp`
- Test: `test/native/test_water_record_metering_snapshot_store/test_water_record_metering_snapshot_store.cpp`

- [ ] **Step 1: Write failing snapshot store tests**

Use these test names:

```cpp
RUN_TEST(test_ram_snapshot_store_upserts_by_water_record_identity);
RUN_TEST(test_file_snapshot_store_reloads_after_restart);
RUN_TEST(test_find_any_matches_record_pages);
RUN_TEST(test_file_store_overwrites_oldest_when_capacity_is_full);
```

Snapshot type:

```cpp
struct WaterRecordMeteringSnapshot {
    std::uint32_t startTime;
    std::uint32_t volumeMl;
    std::uint32_t targetValue;
    std::uint32_t pulseCount;
    std::uint32_t rejectedPulseCount;
    std::uint16_t durationSec;
    WaterMode mode;
    WaterResult result;
    std::uint8_t selectedPreset;
    std::uint8_t reserved0;
    float pulsePerMlAtRun;
    std::uint32_t meteringSchemeId;
    std::uint32_t meteringSchemeRevision;
    MeteringParameters params;
};
```

- [ ] **Step 2: Run the tests and verify RED**

```bash
pio test -e native -f test_water_record_metering_snapshot_store
```

Expected result: build fails because the snapshot store is missing.

- [ ] **Step 3: Implement RAM and file stores**

Model the API after `WaterRecordCalibrationReader` and `WaterRecordCalibrationWriter`:

```cpp
class WaterRecordMeteringSnapshotReader {
public:
    virtual bool find(const WaterRecord& record, WaterRecordMeteringSnapshot& output) const = 0;
    virtual std::size_t findAny(const WaterRecord* records,
                                std::size_t recordCount,
                                WaterRecordMeteringSnapshot* output,
                                bool* found) const = 0;
    virtual std::size_t count() const = 0;
    virtual bool ready() const = 0;
    virtual const char* storageName() const = 0;
};

class WaterRecordMeteringSnapshotWriter {
public:
    virtual bool upsert(const WaterRecordMeteringSnapshot& snapshot) = 0;
};
```

Do not change the binary layout of `WaterRecord`; preserving the existing `/faucet_records_v1.bin` is part of the migration safety requirement.

- [ ] **Step 4: Run snapshot tests and verify GREEN**

```bash
pio test -e native -f test_water_record_metering_snapshot_store
```

Expected result: all snapshot store tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/app/WaterRecordMeteringSnapshotStore.h src/app/WaterRecordMeteringSnapshotStore.cpp test/native/test_water_record_metering_snapshot_store/test_water_record_metering_snapshot_store.cpp
git commit -m "feat: store metering snapshots for water records"
```

### Task 5: Runtime Wiring And Usage Counting

**Files:**
- Modify: `include/app/AppController.h`
- Modify: `src/app/AppController.cpp`
- Modify: `src/main.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Write failing runtime tests**

Cover these behaviors:

```cpp
RUN_TEST(test_app_controller_uses_active_scheme_parameters_for_flow_meter);
RUN_TEST(test_successful_record_writes_metering_snapshot);
RUN_TEST(test_usage_count_increments_after_record_and_snapshot_write);
RUN_TEST(test_record_write_failure_does_not_increment_usage_count);
RUN_TEST(test_snapshot_write_failure_marks_usage_stats_dirty);
```

Expected assertions:

- `AppSnapshot::pulsePerLiter` reflects the active scheme parameter, not `SystemConfig`.
- `WaterRecordWriter::append()` is called before snapshot upsert.
- `MeteringSchemeStore::incrementUsageAfterRecordWrite()` is called only after record append and snapshot upsert succeed.
- If record append fails, no snapshot is written and `useCount` is unchanged.
- If snapshot write fails after record append, mark `usageStatsDirty = true` for the active scheme because a use happened but cannot be reliably counted.

- [ ] **Step 2: Run runtime tests and verify RED**

```bash
pio test -e native -f test_app_controller
```

Expected result: tests fail because `AppController` still reads metering from `SystemConfig`.

- [ ] **Step 3: Update AppController boundaries**

Change construction from:

```cpp
AppController(const SystemConfig& config, ..., WaterRecordWriter& records, ...);
```

to carry the active scheme snapshot:

```cpp
AppController(const SystemConfig& config,
              const MeteringSchemeRecord& activeScheme,
              StatisticsStore& statistics,
              FilterStore& filters,
              WaterRecordWriter& records,
              WaterRecordMeteringSnapshotWriter& meteringSnapshots,
              MeteringSchemeStore& meteringSchemes,
              WaterPulseTraceStore* pulseTraces = nullptr);
```

Add:

```cpp
bool applyActiveMeteringScheme(const MeteringSchemeRecord& activeScheme);
const MeteringSchemeRecord& activeMeteringScheme() const;
```

`applyConfig()` must no longer alter flow-meter metering parameters. Only `applyActiveMeteringScheme()` does that.

- [ ] **Step 4: Wire main startup**

In `src/main.cpp`:

- instantiate `MeteringSchemeStore g_meteringSchemes(g_waterRecordBackend, "/faucet_metering_schemes_v1.bin");`
- instantiate RAM/file `WaterRecordMeteringSnapshot` stores similarly to calibration stores;
- call `g_meteringSchemes.begin()` after the file backend is available;
- call legacy migration before constructing `AppController`;
- load `activeScheme` from the store and pass it into `AppController`;
- when Web enables a scheme, call `g_app->applyActiveMeteringScheme(activeScheme)` after the store update succeeds.

- [ ] **Step 5: Run runtime tests and verify GREEN**

```bash
pio test -e native -f test_app_controller
```

Expected result: runtime tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp src/main.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "feat: apply active metering schemes at runtime"
```

### Task 6: Candidate Generation Metadata

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [ ] **Step 1: Write failing generation tests**

Add tests for:

```cpp
RUN_TEST(test_segmented_candidate_requires_three_valid_saved_samples);
RUN_TEST(test_segmented_candidate_requires_500ml_actual_volume_span);
RUN_TEST(test_segmented_candidate_rejects_error_over_threshold);
RUN_TEST(test_segmented_candidate_includes_trace_ids_and_startup_average);
```

Expected metadata fields:

- `sampleCount`
- `sampleTraceIds`
- `minActualMl`
- `maxActualMl`
- `maxErrorMl`
- `maxErrorPercent`
- `startupDurationMinSec`
- `startupDurationMaxSec`
- `startupDurationMedianSec`
- `startupDurationAvgSec`

- [ ] **Step 2: Run generation tests and verify RED**

```bash
pio test -e native -f test_water_pulse_trace_store
```

Expected result: tests fail until candidate metadata is expanded.

- [ ] **Step 3: Implement quality gate and metadata conversion**

Rules:

- only saved device samples with confirmed actual volume, no truncation, and stable analysis success are eligible;
- valid sample count must be at least 3;
- `maxActualMl - minActualMl >= 500`;
- max absolute error must be `<= 100ml`;
- max relative error must be `<= 5%`;
- near-volume samples are samples whose actual volume differs by `<= 200ml`; record repeatability diagnostics in `creationSummary`;
- quality failure returns diagnostics but does not save a candidate.

- [ ] **Step 4: Run generation tests and verify GREEN**

```bash
pio test -e native -f test_water_pulse_trace_store
```

Expected result: generation metadata tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/app/WaterPulseTraceStore.h src/app/WaterPulseTraceStore.cpp src/web/FaucetWeb.cpp test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp
git commit -m "feat: generate metering scheme candidates with sample metadata"
```

### Task 7: Web Calibration UI And API Rewrite

**Files:**
- Modify: `include/web/FaucetWeb.h`
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `src/web/FaucetWebJson.cpp`
- Test: native Web tests under `test/native`

- [ ] **Step 1: Write failing Web tests for scheme actions and JSON**

Cover:

```cpp
RUN_TEST(test_calibration_json_contains_current_candidate_and_scheme_list);
RUN_TEST(test_save_generated_result_creates_new_scheme_without_enabling);
RUN_TEST(test_enable_scheme_applies_runtime_scheme);
RUN_TEST(test_disable_used_scheme_hides_from_default_enable_list);
RUN_TEST(test_delete_unused_scheme_removes_it_from_list);
RUN_TEST(test_delete_used_scheme_is_rejected_with_disable_guidance);
```

- [ ] **Step 2: Run Web tests and verify RED**

```bash
pio test -e native -f test_faucet_web
```

Expected result: tests fail because Web still exposes fixed slots.

- [ ] **Step 3: Replace slot UI with task sections**

Calibration page sections:

1. 当前计量方案: name, current marker, 3 params, source, usage count, dirty warning.
2. 计量方案生成结果: 3 params, sample range, max error, max relative error, startup duration min/max/median/average, save/discard actions.
3. 流量计计量方案: dynamic list, default current + enabled schemes, optional disabled filter.
4. 新建计量方案: name + 3 core params.
5. 样本和明细: existing trace/sample tools.

Do not place all metadata in the core parameter area. Detailed creation basis belongs in an expanded detail panel or edit/detail page.

- [ ] **Step 4: Implement Web actions**

Supported actions:

- `generate_metering_candidate`
- `save_metering_candidate`
- `discard_metering_candidate`
- `create_metering_scheme`
- `update_metering_scheme`
- `enable_metering_scheme`
- `disable_metering_scheme`
- `restore_metering_scheme`
- `delete_metering_scheme`

Business Web actions must not add any remote water-control capability.

- [ ] **Step 5: Run Web tests and verify GREEN**

```bash
pio test -e native -f test_faucet_web
```

Expected result: Web tests pass and no fixed slot action remains.

- [ ] **Step 6: Commit**

```bash
git add include/web/FaucetWeb.h src/web/FaucetWeb.cpp src/web/FaucetWebJson.cpp test/native
git commit -m "feat: replace metering slots with scheme management UI"
```

### Task 8: End-To-End Verification And Documentation

**Files:**
- Modify: `docs/10-flow-meter-metering-schemes.md`
- Modify: `docs/03-software-architecture.md`
- Modify: `docs/06-implementation-plan.md`
- Modify: `docs/config-persistence-migration.md`

- [ ] **Step 1: Run focused native test suite**

```bash
pio test -e native -f test_metering_scheme
pio test -e native -f test_metering_scheme_store
pio test -e native -f test_water_record_metering_snapshot_store
pio test -e native -f test_water_pulse_trace_store
pio test -e native -f test_config_store
pio test -e native -f test_app_controller
pio test -e native -f test_faucet_web
```

Expected result: all focused tests pass.

- [ ] **Step 2: Run full native test suite**

```bash
pio test -e native
```

Expected result: all native tests pass.

- [ ] **Step 3: Build ESP32 firmware**

```bash
pio run -e esp32dev
```

Expected result: build succeeds.

- [ ] **Step 4: Check forbidden legacy concepts**

```bash
rg -n "meteringSlots|activeMeteringSlot|saveCandidateToMeteringSlot|enableMeteringSlot|最多10|固定 10|通过扫描.*重建 useCount|扫描.*重建 useCount" include src test/native
rg -n "最多10|最多 10|固定 10|通过扫描.*重建 useCount|扫描.*重建 useCount" docs --glob '!docs/superpowers/plans/*'
```

Expected result:

- no implementation or current docs use fixed slot concepts;
- no code scans water records to rebuild `useCount`;
- superseded plan files are excluded because they intentionally describe discarded fixed-slot work.

- [ ] **Step 5: Update docs with implementation facts**

Document:

- storage file paths;
- migration from old `faucet_cfg` metering keys;
- usage-count dirty behavior;
- snapshot metadata store;
- Web page structure.

- [ ] **Step 6: Run formatting and diff checks**

```bash
git diff --check
```

Expected result: no whitespace errors.

- [ ] **Step 7: Commit**

```bash
git add docs include src test/native
git commit -m "docs: document flow meter metering scheme rewrite"
```

## Execution Notes

- Do not OTA during implementation unless explicitly requested. If a burn test is later required, prefer `pio run -e esp32dev -t webota`.
- Preserve existing user config. Do not silently clear `faucet_cfg`.
- Preserve existing water records by keeping `/faucet_records_v1.bin` readable and adding snapshot metadata separately.
- Keep scheme operations independent from ordinary system settings saves, so editing presets or filters does not rewrite scheme records.
- Keep old plan `docs/superpowers/plans/2026-05-31-flow-meter-metering-schemes.md` marked superseded and do not execute it.
