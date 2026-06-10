# Calibration Metering Lightweight Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor calibration sample storage and metering scheme persistence to match the lightweight design: 3 session samples, 5 long-term samples, no persisted generated candidate, no heavy scheme metadata, and no high-frequency scheme usage writes.

**Architecture:** Keep the existing app/web/store boundaries, but shrink the persisted `MeteringSchemeRecord` and sample store capacities. Generated results become transient values created from current saved samples and are only written when converted into a formal scheme. Calibration pending attempts keep only session state until the user records a valid actual volume, then write the full trace once.

**Tech Stack:** C++17, PlatformIO native tests, existing `WaterRecordFileBackend` storage abstraction, existing ESP32 Web routes.

---

### Task 1: Shrink Core Metering Scheme Model

**Files:**
- Modify: `include/app/MeteringScheme.h`
- Modify: `src/app/MeteringScheme.cpp`
- Test: `test/native/test_metering_scheme/test_metering_scheme.cpp`

- [x] **Step 1: Write failing tests for the lightweight model**

Update `test/native/test_metering_scheme/test_metering_scheme.cpp` so generated schemes assert only minimal metadata:

```cpp
TEST_ASSERT_TRUE(saved->recordUsed);
TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(MeteringSchemeState::Available),
                        static_cast<unsigned>(saved->state));
TEST_ASSERT_EQUAL_UINT16(3, saved->sampleCount);
TEST_ASSERT_EQUAL_UINT32(1000, saved->minActualMl);
TEST_ASSERT_EQUAL_UINT32(7000, saved->maxActualMl);
TEST_ASSERT_EQUAL_UINT32(28, saved->maxErrorMl);
TEST_ASSERT_EQUAL_UINT16(42, saved->maxErrorTenthPercent);
TEST_ASSERT_FALSE(saved->usedEver);
```

Remove assertions for `meterLabel`, `installationLabel`, `conditionLabel`, `userNote`, `sampleTraceIds`, startup duration aggregate fields, `creationSummary`, `lastModifiedSummary`, `useCount`, `lastUsedAt`, and `usageStatsDirty`.

- [x] **Step 2: Run test to verify it fails**

Run:

```bash
pio test -e native -f test_metering_scheme
```

Expected: compile fails because `MeteringSchemeState`, `recordUsed`, and `maxErrorTenthPercent` are not implemented yet, or assertions fail while old fields still exist.

- [x] **Step 3: Implement minimal model changes**

In `include/app/MeteringScheme.h`:

```cpp
enum class MeteringSchemeSource : std::uint8_t {
    Default = 0,
    CalibrationSession = 1,
    Manual = 2,
    Migrated = 3,
    LongTermSamples = 4,
};

enum class MeteringSchemeState : std::uint8_t {
    Available = 0,
    Disabled = 1,
};

struct MeteringSchemeRecord {
    std::uint32_t id = 0;
    bool recordUsed = false;
    MeteringSchemeState state = MeteringSchemeState::Available;
    char name[kMeteringSchemeNameLength]{};
    MeteringParameters params{};
    MeteringSchemeSource sourceType = MeteringSchemeSource::Default;
    std::uint32_t revision = 0;
    std::uint32_t createdAt = 0;
    std::uint32_t updatedAt = 0;
    bool usedEver = false;
    std::uint16_t sampleCount = 0;
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    std::uint16_t maxErrorTenthPercent = 0;
};

struct MeteringSchemeCandidate {
    bool ready = false;
    MeteringSchemeSource sourceType = MeteringSchemeSource::CalibrationSession;
    MeteringParameters params{};
    std::uint32_t generatedAt = 0;
    std::uint16_t sampleCount = 0;
    std::uint32_t minActualMl = 0;
    std::uint32_t maxActualMl = 0;
    std::uint32_t maxErrorMl = 0;
    std::uint16_t maxErrorTenthPercent = 0;
};
```

In `src/app/MeteringScheme.cpp`, replace `valid` checks with `recordUsed`, `enabled` checks with `state == MeteringSchemeState::Available`, and copy only the candidate minimal summary into generated schemes.

- [x] **Step 4: Run test to verify it passes**

Run:

```bash
pio test -e native -f test_metering_scheme
```

Expected: `test_metering_scheme` passes.

- [x] **Step 5: Commit**

```bash
git add include/app/MeteringScheme.h src/app/MeteringScheme.cpp test/native/test_metering_scheme/test_metering_scheme.cpp
git commit -m "refactor: shrink metering scheme model"
```

### Task 2: Refactor Metering Scheme Store Persistence

**Files:**
- Modify: `include/app/MeteringSchemeStore.h`
- Modify: `src/app/MeteringSchemeStore.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [x] **Step 1: Write failing store tests**

Update store tests to assert:

```cpp
TEST_ASSERT_FALSE(store.loadCandidate(loaded));
TEST_ASSERT_FALSE(store.saveCandidate(generated));
TEST_ASSERT_FALSE(store.discardCandidate());
TEST_ASSERT_TRUE(store.saveCandidateAsNew(generated, "低压实验", 1770000100, newId));
TEST_ASSERT_TRUE(store.markUsedAfterRecordWrite(id));
TEST_ASSERT_TRUE(store.markUsedAfterRecordWrite(id));
TEST_ASSERT_TRUE(store.findById(id, scheme));
TEST_ASSERT_TRUE(scheme.usedEver);
```

Remove tests that require persisted candidate round-trip, `incrementUsageAfterRecordWrite`, `markUsageStatsDirty`, `useCount`, `lastUsedAt`, `usageStatsDirty`, creation summaries, and migrated candidate persistence.

- [x] **Step 2: Run test to verify it fails**

Run:

```bash
pio test -e native -f test_metering_scheme_store
```

Expected: compile fails because store API still persists candidates and uses old usage methods.

- [x] **Step 3: Implement store changes**

Change `MeteringSchemeStoreHeader` to remove `candidateSize`, set store version to the next version, and compute layout as:

```cpp
sizeof(MeteringSchemeStoreHeader) + slotCount * sizeof(MeteringSchemeRecord)
```

Change public API:

```cpp
bool saveCandidateAsNew(const MeteringSchemeCandidate& candidate,
                        const char* name,
                        std::uint32_t nowSeconds,
                        std::uint32_t& newId);
bool markUsedAfterRecordWrite(std::uint32_t schemeId);
```

Keep `loadCandidate`, `saveCandidate`, and `discardCandidate` only if needed as compatibility stubs returning `false`; otherwise remove their callers in the same task.

Migration rules:

- V1/V2/V3 heavy records migrate to lightweight records by copying only `id`, occupied/enabled state, `name`, 5 parameters, source, revision, created/updated time, and minimal sample summary.
- `usedEver` becomes `legacy.useCount > 0`.
- Legacy generated candidate is ignored.
- Long summaries, labels, trace ID arrays, startup aggregate stats, last activation, last used, and dirty flags are discarded.

- [x] **Step 4: Run test to verify it passes**

Run:

```bash
pio test -e native -f test_metering_scheme_store
```

Expected: `test_metering_scheme_store` passes.

- [x] **Step 5: Commit**

```bash
git add include/app/MeteringSchemeStore.h src/app/MeteringSchemeStore.cpp test/native/test_metering_scheme_store/test_metering_scheme_store.cpp
git commit -m "refactor: store lightweight metering schemes"
```

### Task 3: Shrink Calibration Sample Stores and Stop Pending Trace Writes

**Files:**
- Modify: `include/app/CalibrationSampleStore.h`
- Modify: `src/app/CalibrationSampleStore.cpp`
- Modify: `src/app/AppController.cpp`
- Modify: `include/app/CalibrationSession.h`
- Test: `test/native/test_calibration_sample_store/test_calibration_sample_store.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [x] **Step 1: Write failing tests**

Update sample store tests:

```cpp
TEST_ASSERT_EQUAL_size_t(3, CalibrationSessionTraceStore(backend, path).capacity());
TEST_ASSERT_EQUAL_size_t(5, CalibrationLongTermSampleStore(backend, path).capacity());
TEST_ASSERT_FALSE(backend.exists(path));
TEST_ASSERT_TRUE(store.begin());
TEST_ASSERT_FALSE(backend.exists(path));
```

Update app controller test for pending actual:

```cpp
app.recordCompletedCalibrationAttemptForTest(record, now);
CalibrationStoredTrace stored{};
TEST_ASSERT_FALSE(traceStore.load(0, stored));
TEST_ASSERT_TRUE(app.submitCalibrationActualForWeb(actualMl, now + 1));
TEST_ASSERT_TRUE(traceStore.load(0, stored));
TEST_ASSERT_TRUE(stored.valid);
TEST_ASSERT_FALSE(stored.pendingActual);
```

- [x] **Step 2: Run tests to verify they fail**

Run:

```bash
pio test -e native -f test_calibration_sample_store -f test_app_controller
```

Expected: tests fail because session capacity is 5, long-term capacity is 10, stores create files at `begin()`, and pending traces are written before actual volume is submitted.

- [x] **Step 3: Implement sample store and controller changes**

Set:

```cpp
constexpr std::size_t kCalibrationSessionTraceSlots = 3;
constexpr std::size_t kCalibrationLongTermSampleSlots = 5;
```

Change `begin()` so missing files mark the store ready without creating a file. Add an internal `ensureFileForWrite(kind, slots)` path used by `savePending`, the new valid-save method, and long-term `save`.

Replace pending trace flow in `AppController`:

- `persistCalibrationPendingAttempt()` only stores pending attempt identity in `CalibrationSessionRecord`.
- `submitCalibrationActualForWeb()` finds the RAM trace by record, validates it, writes the full trace directly to the session sample slot as valid, and updates session state.
- `skipCalibrationAttemptForWeb()` and invalid attempts do not write full trace data.

- [x] **Step 4: Run tests to verify they pass**

Run:

```bash
pio test -e native -f test_calibration_sample_store -f test_app_controller
```

Expected: both test suites pass.

- [x] **Step 5: Commit**

```bash
git add include/app/CalibrationSampleStore.h src/app/CalibrationSampleStore.cpp include/app/CalibrationSession.h src/app/AppController.cpp test/native/test_calibration_sample_store/test_calibration_sample_store.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "refactor: write calibration traces only after actual volume"
```

### Task 4: Update AppController Generated Scheme Flow and Usage Writes

**Files:**
- Modify: `src/app/AppController.cpp`
- Modify: `include/app/AppController.h`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [x] **Step 1: Write failing tests**

Update tests so `generateCalibrationForWeb()` stores a transient candidate in controller/session state, not in `MeteringSchemeStore`, and `confirmCalibrationForWeb()` passes that candidate directly to `saveCandidateAsNew`.

Add usage write test:

```cpp
TEST_ASSERT_TRUE(scheme.usedEver);
TEST_ASSERT_EQUAL_UINT32(0, scheme.updatedAt - scheme.createdAt);
```

and verify the second successful outflow does not rewrite the scheme record.

- [x] **Step 2: Run test to verify it fails**

Run:

```bash
pio test -e native -f test_app_controller
```

Expected: fails because current controller persists candidate through `MeteringSchemeStore` and increments usage every successful record.

- [x] **Step 3: Implement controller changes**

Add a controller member:

```cpp
MeteringSchemeCandidate calibrationCandidate_{};
```

Use it for calibration session generation/confirmation. Replace:

```cpp
meteringSchemes_->saveCandidate(candidate)
meteringSchemes_->loadCandidate(candidate)
meteringSchemes_->saveCandidateAsNew(...)
```

with:

```cpp
calibrationCandidate_ = candidate;
meteringSchemes_->saveCandidateAsNew(calibrationCandidate_, "校准生成计量方案", nowSeconds, newId)
```

Replace usage writes after record write with:

```cpp
if (!activeMeteringScheme_.usedEver && meteringSchemes_) {
    meteringSchemes_->markUsedAfterRecordWrite(activeMeteringScheme_.id);
    activeMeteringScheme_.usedEver = true;
}
```

- [x] **Step 4: Run test to verify it passes**

Run:

```bash
pio test -e native -f test_app_controller
```

Expected: `test_app_controller` passes.

- [x] **Step 5: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "refactor: keep generated calibration candidate transient"
```

### Task 5: Update Web JSON and Metering Page

**Files:**
- Modify: `src/web/FaucetWebJson.cpp`
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `src/web/FaucetWebRoutes.cpp`
- Test: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [x] **Step 1: Write failing web tests**

Update tests to assert:

```cpp
TEST_ASSERT_NULL(std::strstr(buffer, "sampleTraceIds"));
TEST_ASSERT_NULL(std::strstr(buffer, "creationSummary"));
TEST_ASSERT_NULL(std::strstr(buffer, "lastActivatedAt"));
TEST_ASSERT_NULL(std::strstr(buffer, "lastUsedAt"));
TEST_ASSERT_NULL(std::strstr(buffer, "usageStatsDirty"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "使用状态"));
```

Update generation route tests so long-term sample generation renders from transient diagnostics, and saving generated scheme passes a posted/generated candidate or regenerates from current samples instead of loading a stored candidate.

- [x] **Step 2: Run tests to verify they fail**

Run:

```bash
pio test -e native -f test_faucet_web_json -f test_faucet_web_routes
```

Expected: fails because Web still emits heavy fields and uses stored candidates.

- [x] **Step 3: Implement web changes**

Remove scheme detail fields for trace IDs, startup aggregate stats, summaries, exact usage counts, last activation, and last used. Render:

- name
- state
- source
- revision
- 5 parameters
- sample count/range/max error
- used status

For `/faucet/metering` generation:

- `generate_segmented` renders diagnostics as a transient result.
- `save_generated_scheme` regenerates from current long-term samples and calls `saveCandidateAsNew(candidate, name, now, newId)`.
- `discard_generated_scheme` only redirects; it does not write Flash.

- [x] **Step 4: Run tests to verify they pass**

Run:

```bash
pio test -e native -f test_faucet_web_json -f test_faucet_web_routes
```

Expected: both web test suites pass.

- [x] **Step 5: Commit**

```bash
git add src/web/FaucetWebJson.cpp src/web/FaucetWeb.cpp src/web/FaucetWebRoutes.cpp test/native/test_faucet_web_json/test_faucet_web_json.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp
git commit -m "refactor: simplify metering scheme web surface"
```

### Task 6: Remove Boot-Time Large Calibration File Preallocation

**Files:**
- Modify: `src/main.cpp`
- Test: `test/native/test_app_config/test_app_config.cpp`

- [x] **Step 1: Write failing static/config test**

Add assertions that session and long-term sample capacities are 3 and 5:

```cpp
TEST_ASSERT_EQUAL_size_t(3, kCalibrationSessionTraceSlots);
TEST_ASSERT_EQUAL_size_t(5, kCalibrationLongTermSampleSlots);
```

Add a source scan assertion if the existing test pattern supports it, checking `src/main.cpp` no longer calls `canInitializeFixedCalibrationFile()` for `kCalibrationSessionTracePath` or `kCalibrationLongTermSamplesPath`.

- [x] **Step 2: Run test to verify it fails**

Run:

```bash
pio test -e native -f test_app_config
```

Expected: fails while constants or source scan still reflect old boot preallocation.

- [x] **Step 3: Implement boot init changes**

In `initializeApplication()`:

- Keep `CalibrationSessionFileStore` initialization for the small session state file if still required.
- Remove `canInitializeFixedCalibrationFile(..., kCalibrationSessionTracePath, ...)`.
- Remove `canInitializeFixedCalibrationFile(..., kCalibrationLongTermSamplesPath, ...)`.
- Call `g_calibrationSessionTraces.begin()` and `g_calibrationLongTermSamples.begin()` directly; missing files should now be ready and created lazily on first write.

- [x] **Step 4: Run test to verify it passes**

Run:

```bash
pio test -e native -f test_app_config
```

Expected: `test_app_config` passes.

- [x] **Step 5: Commit**

```bash
git add src/main.cpp test/native/test_app_config/test_app_config.cpp
git commit -m "refactor: lazily create calibration sample stores"
```

### Task 7: Full Verification

**Files:**
- No code changes expected.

- [x] **Step 1: Run focused native suite**

Run:

```bash
pio test -e native -f test_metering_scheme -f test_metering_scheme_store -f test_calibration_sample_store -f test_app_controller -f test_faucet_web_json -f test_faucet_web_routes -f test_app_config
```

Expected: all focused tests pass.

- [x] **Step 2: Run full native suite**

Run:

```bash
pio test -e native
```

Expected: all native tests pass.

- [x] **Step 3: Run ESP32 build**

Run:

```bash
pio run -e esp32dev
```

Expected: firmware builds successfully.

- [x] **Step 4: Commit any verification-only doc updates if needed**

If implementation notes or docs need minor alignment, commit them separately:

```bash
git add docs/10-flow-meter-metering-schemes.md docs/superpowers/specs/2026-06-10-calibration-metering-flow-design.md
git commit -m "docs: align lightweight metering implementation notes"
```

If no doc updates are needed, do not create a commit.

---

## Self-Review

- Spec coverage: Tasks cover lightweight scheme fields, transient generated candidate, `usedEver` only, 3 session samples, 5 long-term samples, lazy sample files, no pending trace write, Web surface cleanup, and verification.
- Placeholder scan: No unfinished markers or unspecified implementation steps remain.
- Type consistency: The plan consistently uses `MeteringSchemeState`, `recordUsed`, `usedEver`, `maxErrorTenthPercent`, and `saveCandidateAsNew(candidate, ...)`.
