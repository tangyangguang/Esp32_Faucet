# Calibration Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an independent calibration workbench page that separates historical records from sample calibration, candidate generation, parameter application, and rollback.

**Architecture:** Reuse the existing synchronous ESP32 WebServer flow and the existing `/api/faucet/records` POST endpoint with explicit action values. Replace the hidden `/faucet/records/calibration` page with a visible `/faucet/calibration` page, move diagnostic panels there, and store candidate/previous segmented parameters in `SystemConfig` through versioned `ConfigStore` keys.

**Tech Stack:** PlatformIO, Arduino ESP32, existing `Esp32BaseWeb`, native Unity tests, `WaterPulseTraceStore`, `WaterRecordCalibrationStore`, NVS-backed `ConfigStore`.

---

### Task 1: Persist Candidate And Previous Segmented Parameters

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `src/app/AppConfig.cpp`
- Modify: `src/app/ConfigStore.cpp`
- Test: `test/native/test_app_config/test_app_config.cpp`
- Test: `test/native/test_config_store/test_config_store.cpp`

- [ ] **Step 1: Write failing config tests**

Add assertions that defaults have no candidate or previous parameters, that sanitization clears invalid candidate/previous flags, and that save/load round trips candidate plus previous fields:

```cpp
TEST_ASSERT_FALSE(config.segmentedCandidateReady);
TEST_ASSERT_FALSE(config.segmentedPreviousReady);
config.segmentedCandidateReady = true;
config.candidateStablePulsePerLiter = 222;
config.candidateStartupDurationSec = 5;
config.candidateStartupPulseCount = 40;
config.candidateStartupVolumeMl = 510;
config.candidateSampleCount = 3;
config.candidateMinActualMl = 1500;
config.candidateMaxActualMl = 7500;
config.candidateMaxErrorMl = 35;
config.segmentedPreviousReady = true;
config.previousStablePulsePerLiter = 221;
config.previousStartupDurationSec = 5;
config.previousStartupPulseCount = 39;
config.previousStartupVolumeMl = 500;
config.previousSegmentedMeteringCalibrated = true;
TEST_ASSERT_TRUE(store.saveSystemConfig(config));
const SystemConfig loaded = store.loadSystemConfig();
TEST_ASSERT_TRUE(loaded.segmentedCandidateReady);
TEST_ASSERT_EQUAL_UINT32(222, loaded.candidateStablePulsePerLiter);
TEST_ASSERT_TRUE(loaded.segmentedPreviousReady);
TEST_ASSERT_EQUAL_UINT32(221, loaded.previousStablePulsePerLiter);
```

- [ ] **Step 2: Run tests and verify they fail**

Run: `pio test -e native -f native/test_app_config -f native/test_config_store`

Expected: compile failure for missing `SystemConfig` fields.

- [ ] **Step 3: Add config fields and defaults**

Add candidate and previous fields to `SystemConfig`, initialize them in `makeDefaultConfig()`, and sanitize them. Candidate validity requires stable P/L, startup duration, startup pulse count, sample count >= 2, min/max actual ml, and max actual > min actual. Previous validity requires stable P/L, startup duration, startup pulse count, and a saved `previousSegmentedMeteringCalibrated` value.

- [ ] **Step 4: Version and persist config**

Increase `kConfigVersion` from `7` to `8`. Load absent new keys with defaults in `loadCommonSystemConfig()`. Save keys with compact names such as `cand_stpl`, `cand_sts`, `cand_stp`, `cand_stml`, `cand_scnt`, `cand_min`, `cand_max`, `cand_err`, `cand_at`, `prev_stpl`, `prev_sts`, `prev_stp`, `prev_stml`, `prev_cal`.

- [ ] **Step 5: Run tests and commit**

Run: `pio test -e native -f native/test_app_config -f native/test_config_store`

Expected: all selected tests pass.

Commit: `git commit -m "Add persisted segmented calibration candidates"`

### Task 2: Move Calibration To A Top-Level Navigation Page

**Files:**
- Modify: `src/web/FaucetWebRoutes.cpp`
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing route tests**

Update route tests to expect navigation order `首页`, `记录`, `校准`, `统计`, `预设`, `滤芯`; expect `/faucet/calibration` to be allowed; expect `/faucet/records/calibration` to be disallowed; expect record page source not to contain `records-diagnostic-strip` or `href='/faucet/records/calibration'`.

- [ ] **Step 2: Run route tests and verify they fail**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: route/navigation assertions fail against the old records calibration path.

- [ ] **Step 3: Replace route and handler mapping**

In `FaucetWebRoutes.cpp`, replace `"/faucet/records/calibration"` with `"/faucet/calibration"` and make it a visible `Page` route titled `校准`. In `FaucetWeb.cpp`, rename `handleRecordCalibrationPage()` to `handleCalibrationPage()` and map the new route to that handler.

- [ ] **Step 4: Remove diagnostics and calibration entry from records page**

Remove the records-page top diagnostic grid and remove row action links that point to the old calibration page. Keep calibrated status badges, RAM trace links, saved trace links, save/delete trace actions, pagination, and historical record display.

- [ ] **Step 5: Run route tests and commit**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: all selected tests pass.

Commit: `git commit -m "Move calibration into top-level page"`

### Task 3: Make Capacity Save A Pure Sample Operation

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing source tests**

Assert `handleRecordCalibrationApi()` no longer calls `syncSegmentedCalibrationFromActual(record, actualMl)` after saving. Assert it checks a `saveTrace` parameter and redirects to `/faucet/calibration?saved=actual`.

- [ ] **Step 2: Run route tests and verify failure**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: source assertions fail while old code still redirects to `/faucet/records`.

- [ ] **Step 3: Implement pure save behavior**

Change capacity save behavior so it:

```cpp
const bool saveTrace = checkboxParam("saveTrace");
if (!saveRecordActualMeasurement(record, actualMl)) {
    Esp32BaseWeb::redirectSeeOther("/faucet/calibration?error=calibration_mark_failed");
    return;
}
if (saveTrace) {
    autoSaveTraceAsSegmentedSample(record, actualMl);
}
Esp32BaseWeb::redirectSeeOther("/faucet/calibration?saved=actual");
```

Do not call `applySegmentedCalibrationFromAvailableSamples()` from this path.

- [ ] **Step 4: Run route tests and commit**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: selected tests pass.

Commit: `git commit -m "Separate capacity save from candidate generation"`

### Task 4: Generate, Apply, And Restore Candidate Parameters

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing source tests**

Assert `handleRecordsApi()` accepts actions `generate_candidate`, `apply_candidate`, and `restore_segmented`. Assert these actions redirect to `/faucet/calibration`. Assert generation calls a helper that scans saved samples only.

- [ ] **Step 2: Run route tests and verify failure**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: source assertions fail for missing actions/helpers.

- [ ] **Step 3: Add candidate helper functions**

Add helpers in `FaucetWeb.cpp`:

```cpp
bool generateSegmentedCandidateFromSavedSamples();
bool applySegmentedCandidate();
bool restorePreviousSegmentedParameters();
```

Generation uses saved traces from `g_context.savedPulseTraces->list()`, reads samples, analyzes stability, calls `computeSegmentedCalibration()`, and writes result into candidate fields only.

Application stores current segmented fields into previous fields, then copies candidate fields into current segmented fields and marks `segmentedMeteringCalibrated = true`.

Restore copies previous fields back into current segmented fields and preserves the candidate for comparison.

- [ ] **Step 4: Wire API actions**

In `handleRecordsApi()`, dispatch:

```cpp
if (std::strcmp(text, "generate_candidate") == 0) {
    handleCalibrationCandidateApi();
    return;
}
if (std::strcmp(text, "apply_candidate") == 0) {
    handleCalibrationApplyApi();
    return;
}
if (std::strcmp(text, "restore_segmented") == 0) {
    handleCalibrationRestoreApi();
    return;
}
```

- [ ] **Step 5: Run route tests and commit**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: selected tests pass.

Commit: `git commit -m "Add manual segmented candidate workflow"`

### Task 5: Build The Calibration Workbench UI

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing UI source tests**

Assert the calibration page includes:

- `records-diagnostic-strip`
- `sendSegmentedMeteringPanel()`
- `sendPulseTraceCachePanel()`
- `最新记录`
- `永久保存对应明细`
- `已保存样本`
- `候选参数`
- `生成候选参数`
- `应用候选参数`
- `恢复上一套参数`

- [ ] **Step 2: Run route tests and verify failure**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: UI source assertions fail.

- [ ] **Step 3: Render diagnostics and latest record**

At the top of `handleCalibrationPage()`, render `sendNoticeFromQuery()`, the three-card diagnostic grid, latest record summary, actual ml form, and `saveTrace` checkbox.

- [ ] **Step 4: Render saved sample table**

List Flash saved traces, analyze each sample, and render validity. Invalid reasons must be explicit: `缺少实测容量`, `脉冲数据不足`, `稳态识别失败`, `容量差异不足`.

- [ ] **Step 5: Render candidate and action forms**

Render current, candidate, and previous segmented parameter blocks. Disable action forms by replacing submit buttons with muted text when candidate or previous data is unavailable.

- [ ] **Step 6: Run route tests and commit**

Run: `pio test -e native -f native/test_faucet_web_routes`

Expected: selected tests pass.

Commit: `git commit -m "Build calibration workbench page"`

### Task 6: Update Documentation And Verification

**Files:**
- Modify: `docs/segmented-metering.md`
- Modify: `docs/08-change-record.md`

- [ ] **Step 1: Update docs**

Replace old “自动校准” wording with “手动校准工作流”: save actual capacity, generate candidate, apply candidate, restore previous.

- [ ] **Step 2: Run full verification**

Run:

```bash
pio test -e native
pio run -e esp32dev
git diff --check
```

Expected: native tests pass, ESP32 build succeeds, diff check is clean.

- [ ] **Step 3: Commit and push**

Commit: `git commit -m "Document calibration workbench workflow"`

Push: `git push`
