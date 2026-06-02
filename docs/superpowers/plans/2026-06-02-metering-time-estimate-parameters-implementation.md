# Metering Time Estimate Parameters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 5-parameter metering schemes with separate capacity-estimate and time-estimate parameters, preserving existing user metering data.

**Architecture:** Extend `MeteringParameters` with `startupDurationMs` and `stableFlowMlPerMin`, keep actual volume calculation based only on pulse/capacity parameters, and add separate time-estimate helpers. Persist the expanded parameters in scheme records, candidates, and water-record metering snapshots, with v1-to-v2 migration for existing files. Update Web JSON/forms and native tests.

**Tech Stack:** PlatformIO native C++17, Unity native tests, existing LittleFS-like record stores through `WaterRecordFileBackend`, existing Web chunk rendering.

---

### Task 1: Core Parameter Model And Estimation Helpers

**Files:**
- Modify: `include/app/AppTypes.h`
- Modify: `include/app/MeteringScheme.h`
- Modify: `src/app/MeteringScheme.cpp`
- Test: `test/native/test_metering_scheme/test_metering_scheme.cpp`

- [ ] **Step 1: Write failing tests**

Add tests that assert default parameters include `startupDurationMs = 5000` and `stableFlowMlPerMin = 480`, validation rejects `stableFlowMlPerMin = 0` and values above `30000`, capacity-target duration estimation uses the segmented startup formula, and duration-target volume estimation uses the same segmented time model.

Run: `pio test -e native -f test_metering_scheme`
Expected: FAIL because the fields and helper functions do not exist yet.

- [ ] **Step 2: Implement minimal core changes**

Add fields to `MeteringParameters`:

```cpp
std::uint32_t startupDurationMs = kDefaultStartupDurationMs;
std::uint32_t stableFlowMlPerMin = kDefaultStableFlowMlPerMin;
```

Add constants:

```cpp
kDefaultStartupDurationMs = 5000
kDefaultStableFlowMlPerMin = 480
kMaxSegmentedStartupDurationMs = 60000
kMinStableFlowMlPerMin = 50
kMaxStableFlowMlPerMin = 30000
```

Add helpers:

```cpp
std::uint32_t estimateDurationMsForVolumeMl(const MeteringParameters& params, std::uint32_t targetMl);
std::uint32_t estimateVolumeMlForDurationMs(const MeteringParameters& params, std::uint32_t durationMs);
```

Use 64-bit integer math and saturate to `UINT32_MAX`.

- [ ] **Step 3: Verify core tests pass**

Run: `pio test -e native -f test_metering_scheme`
Expected: PASS.

### Task 2: Scheme Store, Candidate, And Legacy File Migration

**Files:**
- Modify: `src/app/MeteringScheme.cpp`
- Modify: `src/app/MeteringSchemeStore.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [ ] **Step 1: Write failing tests**

Add tests that default stores and manual schemes persist the two new parameters, legacy config migration fills `5000/480`, and a handcrafted v1 scheme-store file is migrated to v2 without losing records or candidate data.

Run: `pio test -e native -f test_metering_scheme_store`
Expected: FAIL because the new fields and migration do not exist.

- [ ] **Step 2: Implement scheme-store changes**

Update manual and generated summaries to include all 5 parameters. Increment metering scheme store version to 2. Add v1 structs inside `MeteringSchemeStore.cpp` matching the old `MeteringParameters` layout, old `MeteringSchemeRecord`, and old `MeteringSchemeCandidate`. If `loadHeader()` sees version 1 with old record and candidate sizes, read all old records/candidate, convert missing time-estimate parameters to defaults, recreate the file with version 2, then continue loading.

- [ ] **Step 3: Verify scheme-store tests pass**

Run: `pio test -e native -f test_metering_scheme_store`
Expected: PASS.

### Task 3: Water-Record Metering Snapshot Migration

**Files:**
- Modify: `src/app/WaterRecordMeteringSnapshotStore.cpp`
- Test: `test/native/test_water_record_metering_snapshot_store/test_water_record_metering_snapshot_store.cpp`

- [ ] **Step 1: Write failing tests**

Add tests that snapshots store and reload 5-parameter snapshots, and an old v1 snapshot file migrates to v2 with default time-estimate parameters.

Run: `pio test -e native -f test_water_record_metering_snapshot_store`
Expected: FAIL because the migrated fields do not exist.

- [ ] **Step 2: Implement snapshot migration**

Increment snapshot version to 2. Add a v1 snapshot struct with old 3-field `MeteringParameters`. During `begin()`, if the file header is version 1 with old record size, read entries in newest-order or physical-order consistently, convert params to defaults for the new fields, recreate the file with version 2, and restore count/oldest index semantics.

- [ ] **Step 3: Verify snapshot tests pass**

Run: `pio test -e native -f test_water_record_metering_snapshot_store`
Expected: PASS.

### Task 4: Calibration Generation And Web Surfaces

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `src/web/FaucetWebJson.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`
- Test: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing tests**

Add tests that generated calibration results include startup duration and stable flow, status JSON includes 5 metering fields, and scheme edit/calibration pages render `startupDurationMs`, `stableFlowMlPerMin`, `容量估算计量参数`, and `时间估算计量参数`.

Run: `pio test -e native -f test_water_pulse_trace_store -f test_faucet_web_json -f test_faucet_web_routes`
Expected: FAIL because Web and generated result code still expose 3 fields.

- [ ] **Step 2: Implement Web and generation changes**

Populate generated candidate params from `SegmentedCalibrationResult::startupDurationSec` and stable flow derived from stable pulse rate and stable P/L. Replace recent-average-flow target duration estimates with `estimateDurationMsForVolumeMl()`. Replace time-mode display estimates with `estimateVolumeMlForDurationMs()`. Add the two new form fields and request parsing. Add new JSON fields under `metering`.

- [ ] **Step 3: Verify Web/generation tests pass**

Run: `pio test -e native -f test_water_pulse_trace_store -f test_faucet_web_json -f test_faucet_web_routes`
Expected: PASS.

### Task 5: Documentation Review Fixes And Full Verification

**Files:**
- Modify: `docs/superpowers/specs/2026-06-02-metering-time-estimate-parameters-design.md`
- Modify docs only if implementation reveals wording gaps.

- [ ] **Step 1: Update docs for migration and helper names**

Add explicit v1-to-v2 persistence migration notes for metering scheme files and water-record metering snapshot files.

- [ ] **Step 2: Run focused and full native verification**

Run: `pio test -e native -f test_metering_scheme -f test_metering_scheme_store -f test_water_record_metering_snapshot_store -f test_water_pulse_trace_store -f test_faucet_web_json -f test_faucet_web_routes`

Then run: `pio test -e native`

Expected: PASS.

- [ ] **Step 3: Check whitespace and git diff**

Run: `git diff --check`
Expected: no output.

Run: `git status --short`
Expected: only intentional implementation and doc changes.
