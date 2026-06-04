# Calibration Session Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the overloaded calibration page model with a device-level guided calibration session, 5-slot session sample storage, 10-slot long-term sample storage, local-only water control, and a separate metering scheme management flow.

**Architecture:** Add a platform-independent calibration session core and two fixed-slot LittleFS-backed sample stores before touching Web UI. `AppController` owns the active CAL mode and delegates valve actions to the existing local `WaterController` safety path. Web can observe and edit calibration data, but never starts, pauses, resumes, or stops water.

**Tech Stack:** PlatformIO C++17 native tests, ESP32 Arduino firmware, LittleFS through existing `WaterRecordFileBackend`, existing `WaterPulseTrace` analysis, generated Web HTML in `FaucetWeb.cpp`.

---

## Preconditions

- Target deployment is a new PCB / clean filesystem test run.
- Do not migrate old pulse trace or old sample files.
- Do not delete or overwrite old files as part of normal boot.
- Existing metering scheme storage remains protected and readable.
- Keep existing `/faucet_pulse_traces_v4.bin` as the ordinary record pulse-detail store for record detail and technical viewing. It is no longer the primary calibration sample source for the guided calibration session.
- Main firmware upload for board testing should prefer `pio run -e esp32dev -t webota`; serial upload is reserved for first flash, partition changes, unreachable network, or OTA recovery.

## File Structure

- Create `include/app/CalibrationSession.h`: session statuses, attempt statuses, skip/invalid reasons, constants, serializable session record types, and pure helper functions.
- Create `src/app/CalibrationSession.cpp`: session state transitions, sample counting, capacity checks, quality classification, and generated-result eligibility.
- Create `include/app/CalibrationSessionStore.h`: abstract reader/writer and LittleFS-backed current-session file store.
- Create `src/app/CalibrationSessionStore.cpp`: fixed-size current/last session persistence with checksum and staged commit.
- Create `include/app/CalibrationSampleStore.h`: common fixed-slot sample record model plus session trace store and long-term sample store APIs.
- Create `src/app/CalibrationSampleStore.cpp`: 5-slot session trace file and 10-slot long-term sample file, both using `uint32_t elapsedUs[4096]`.
- Modify `include/app/AppController.h`: add calibration session snapshot fields and local calibration-mode entry points.
- Modify `src/app/AppController.cpp`: integrate CAL mode, local button semantics, record completion handoff, and session restore.
- Modify `src/main.cpp`: allocate RAM trace capacity for 1 recent trace, instantiate new stores, precreate fixed-slot files, wire context.
- Modify `include/web/FaucetWeb.h`: add session and sample store pointers to `FaucetWebContext`.
- Modify `src/web/FaucetWebRoutes.cpp`: adjust navigation order and add `/faucet/metering`.
- Modify `src/web/FaucetWeb.cpp`: replace calibration page with session workflow, move scheme list/edit to metering page, add sample library views.
- Modify `include/web/FaucetWebPolicy.h`: add a dedicated `Metering` write target for `/faucet/metering` busy redirects.
- Modify `src/web/FaucetWebPolicy.cpp`: keep calibration and metering write guards busy-safe.
- Modify `src/web/FaucetWebJson.cpp`: expose calibration session snapshot in status JSON.
- Modify docs `docs/03-software-architecture.md`, `docs/04-ui-interaction.md`, `docs/08-change-record.md`: reflect final routes, local CAL flow, and the split between calibration session and metering scheme management.
- Test `test/native/test_calibration_session/test_calibration_session.cpp`: pure state machine and rule tests.
- Test `test/native/test_calibration_session_store/test_calibration_session_store.cpp`: current session file persistence.
- Test `test/native/test_calibration_sample_store/test_calibration_sample_store.cpp`: 5-slot and 10-slot fixed file behavior.
- Modify `test/native/test_app_controller/test_app_controller.cpp`: local CAL flow tests.
- Modify `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`: navigation, route, and no-web-water-control tests.
- Modify `test/native/test_faucet_web_json/test_faucet_web_json.cpp`: status JSON tests.
- Modify `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`: POST action and busy guard tests.

## Tasks

### Task 1: Calibration Session Domain Model

**Files:**
- Create: `include/app/CalibrationSession.h`
- Create: `src/app/CalibrationSession.cpp`
- Test: `test/native/test_calibration_session/test_calibration_session.cpp`

- [ ] **Step 1: Add failing tests for sample limits and attempts**

Create `test/native/test_calibration_session/test_calibration_session.cpp` with tests named:

```cpp
void test_new_session_starts_preparing();
void test_two_valid_samples_allow_quick_generation();
void test_three_valid_samples_are_recommended();
void test_five_valid_samples_stop_new_runs();
void test_ten_attempts_stop_session_when_not_ready();
void test_skipped_attempt_does_not_count_as_valid();
void test_paused_resume_attempt_is_invalid_for_generation();
void test_attempt_keeps_full_water_record_identity();
```

Use these expected constants in the assertions:

```cpp
TEST_ASSERT_EQUAL_UINT8(2, kCalibrationMinQuickSamples);
TEST_ASSERT_EQUAL_UINT8(3, kCalibrationRecommendedSamples);
TEST_ASSERT_EQUAL_UINT8(5, kCalibrationMaxValidSamples);
TEST_ASSERT_EQUAL_UINT8(10, kCalibrationMaxAttempts);
```

- [ ] **Step 2: Run the failing test**

Run:

```bash
pio test -e native -f native/test_calibration_session
```

Expected: FAIL because `app/CalibrationSession.h` does not exist.

- [ ] **Step 3: Define the domain types**

Add `include/app/CalibrationSession.h` with these names and constants:

```cpp
namespace faucet {

constexpr std::uint8_t kCalibrationMinQuickSamples = 2;
constexpr std::uint8_t kCalibrationRecommendedSamples = 3;
constexpr std::uint8_t kCalibrationMaxValidSamples = 5;
constexpr std::uint8_t kCalibrationMaxAttempts = 10;
constexpr std::uint32_t kCalibrationMinVolumeSpanMl = 500;
constexpr std::uint32_t kCalibrationRecommendedVolumeSpanMl = 1000;

enum class CalibrationSessionStatus : std::uint8_t {
    Idle,
    Preparing,
    WaitingLocalRun,
    Running,
    AwaitingActual,
    ReadyToGenerate,
    Generated,
    Applied,
    Discarded,
    Failed,
};

enum class CalibrationAttemptStatus : std::uint8_t {
    Empty,
    PendingActual,
    Valid,
    Skipped,
    Invalid,
};

enum class CalibrationSkipReason : std::uint8_t {
    None,
    OverflowOrUnclearReading,
    ContainerMissed,
    WaterPathClosed,
    Mistake,
    Other,
};

enum class CalibrationInvalidReason : std::uint8_t {
    None,
    ResumedAfterPause,
    TruncatedTrace,
    MissingActualMl,
    NoEffectivePulse,
    AnalysisFailed,
    ErrorResult,
    StorageFailed,
};

enum class CalibrationCoverageQuality : std::uint8_t {
    Insufficient,
    NarrowQuick,
    Recommended,
};

struct CalibrationAttempt {
    std::uint8_t attemptIndex = 0;
    std::uint8_t sessionTraceSlot = 255;
    WaterRecord record{};
    std::uint32_t targetHintMl = 0;
    std::uint32_t actualMl = 0;
    CalibrationAttemptStatus status = CalibrationAttemptStatus::Empty;
    CalibrationSkipReason skipReason = CalibrationSkipReason::None;
    CalibrationInvalidReason invalidReason = CalibrationInvalidReason::None;
    bool resumedAfterPause = false;
    bool truncated = false;
};

struct CalibrationSessionRecord {
    std::uint32_t sessionId = 0;
    CalibrationSessionStatus status = CalibrationSessionStatus::Idle;
    std::uint32_t startedAt = 0;
    std::uint32_t updatedAt = 0;
    std::uint32_t appliedSchemeId = 0;
    std::uint8_t attemptCount = 0;
    std::uint8_t validSampleCount = 0;
    CalibrationAttempt attempts[kCalibrationMaxAttempts]{};
};

CalibrationSessionRecord makeCalibrationSession(std::uint32_t sessionId, std::uint32_t nowSeconds);
std::uint8_t countValidCalibrationSamples(const CalibrationSessionRecord& session);
std::uint8_t countCalibrationAttempts(const CalibrationSessionRecord& session);
bool calibrationCanStartAttempt(const CalibrationSessionRecord& session);
bool calibrationCanQuickGenerate(const CalibrationSessionRecord& session);
bool calibrationIsRecommended(const CalibrationSessionRecord& session);
CalibrationCoverageQuality calibrationCoverageQuality(const CalibrationSessionRecord& session);
bool appendCalibrationAttempt(CalibrationSessionRecord& session, const CalibrationAttempt& attempt);

}  // namespace faucet
```

- [ ] **Step 4: Implement the minimal pure helpers**

Implement the helpers in `src/app/CalibrationSession.cpp` so the tests pass. `appendCalibrationAttempt()` must reject attempts after 10 total attempts and must reject new valid samples after 5 valid samples.

- [ ] **Step 5: Re-run the session tests**

Run:

```bash
pio test -e native -f native/test_calibration_session
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/app/CalibrationSession.h src/app/CalibrationSession.cpp test/native/test_calibration_session/test_calibration_session.cpp
git commit -m "feat: add calibration session domain model"
```

### Task 2: Fixed-Slot Session and Sample Stores

**Files:**
- Create: `include/app/CalibrationSessionStore.h`
- Create: `src/app/CalibrationSessionStore.cpp`
- Create: `include/app/CalibrationSampleStore.h`
- Create: `src/app/CalibrationSampleStore.cpp`
- Test: `test/native/test_calibration_session_store/test_calibration_session_store.cpp`
- Test: `test/native/test_calibration_sample_store/test_calibration_sample_store.cpp`

- [ ] **Step 1: Add failing persistence tests**

Create tests that prove:

```text
session store writes and reads one current session
session store marks corrupt checksum as not ready
session trace store has exactly 5 slots
starting a new session clears the 5 session slots
long-term sample store has exactly 10 slots
long-term sample store refuses the 11th sample
long-term sample remove clears only the fixed slot index entry and frees that slot
```

- [ ] **Step 2: Run the failing tests**

Run:

```bash
pio test -e native -f native/test_calibration_session_store -f native/test_calibration_sample_store
```

Expected: FAIL because the stores do not exist.

- [ ] **Step 3: Define fixed store APIs**

`CalibrationSessionStore.h` must expose:

```cpp
class CalibrationSessionFileStore {
public:
    CalibrationSessionFileStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool ready() const;
    bool load(CalibrationSessionRecord& output) const;
    bool save(const CalibrationSessionRecord& session);
    bool clear();
    const char* storageName() const;
};
```

`WaterRecord record{}` is required so restored `AwaitingActual` attempts can later write `WaterRecordCalibrationStore` metadata using the same identity fields as `sameWaterRecordCalibrationIdentity()`: `startTime`, `volumeMl`, `targetValue`, `pulseCount`, `rejectedPulseCount`, `durationSec`, `mode`, `result`, and `selectedPreset`.

`CalibrationSampleStore.h` must expose:

```cpp
constexpr std::size_t kCalibrationSessionTraceSlots = 5;
constexpr std::size_t kCalibrationLongTermSampleSlots = 10;

struct CalibrationStoredTrace {
    bool valid = false;
    bool pendingActual = false;
    std::uint32_t sampleId = 0;
    std::uint32_t sessionId = 0;
    std::uint8_t attemptIndex = 0;
    std::uint32_t actualMl = 0;
    std::uint32_t savedAt = 0;
    WaterPulseTrace trace{};
};

class CalibrationSessionTraceStore {
public:
    CalibrationSessionTraceStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool clear();
    bool clearForNewSession(std::uint32_t sessionId);
    bool savePending(std::uint8_t slot, const CalibrationStoredTrace& trace, const WaterPulseTraceSample* samples, std::size_t sampleCount);
    bool commitValid(std::uint8_t slot, std::uint32_t actualMl, std::uint32_t savedAt);
    bool invalidate(std::uint8_t slot);
    bool load(std::uint8_t slot, CalibrationStoredTrace& trace) const;
    std::size_t readSamples(std::uint8_t slot, WaterPulseTraceSample* output, std::size_t outputCapacity) const;
    std::size_t capacity() const;
    bool ready() const;
    const char* storageName() const;
};

class CalibrationLongTermSampleStore {
public:
    CalibrationLongTermSampleStore(WaterRecordFileBackend& backend, const char* path);

    bool begin();
    bool clear();
    bool save(const CalibrationStoredTrace& trace, const WaterPulseTraceSample* samples, std::size_t sampleCount, std::uint32_t& sampleId);
    bool remove(std::uint32_t sampleId);
    bool load(std::uint32_t sampleId, CalibrationStoredTrace& trace) const;
    std::size_t list(CalibrationStoredTrace* output, std::size_t outputCapacity) const;
    std::size_t capacity() const;
    bool ready() const;
    const char* storageName() const;
};
```

- [ ] **Step 4: Implement fixed-slot files with checksum**

Use `WaterRecordFileBackend::readAt()` and `writeAt()` like existing record stores. Write a slot body first, then mark slot valid in its index/header so power loss cannot create a half-valid sample.

Session trace slots have three visible states:

```text
empty: no usable attempt trace
pendingActual: trace body is durable, actual ml has not been accepted yet
valid: trace body plus actual ml are committed and can participate in generation
```

`savePending()` writes the body and then writes an index entry with `pendingActual=true, valid=false`. `commitValid()` only flips the index entry to `valid=true, pendingActual=false` after actual ml and record calibration metadata have been saved. `invalidate()` clears both flags when the user skips the attempt or the trace cannot be used.

`CalibrationLongTermSampleStore::remove(sampleId)` must not delete the whole file and must not compact slots. It must find the fixed slot by `sampleId`, write a blank/invalid index entry, and leave the body bytes untouched. This matches the existing saved pulse trace pattern and keeps erase/write behavior bounded.

- [ ] **Step 5: Re-run persistence tests**

Run:

```bash
pio test -e native -f native/test_calibration_session_store -f native/test_calibration_sample_store
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/app/CalibrationSessionStore.h src/app/CalibrationSessionStore.cpp include/app/CalibrationSampleStore.h src/app/CalibrationSampleStore.cpp test/native/test_calibration_session_store/test_calibration_session_store.cpp test/native/test_calibration_sample_store/test_calibration_sample_store.cpp
git commit -m "feat: add calibration session storage"
```

### Task 3: Main Wiring and Space Gates

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/app/AppConfig.h`
- Modify: `src/app/AppConfig.cpp`
- Modify: `src/app/FaucetAppConfig.cpp`
- Test: `test/native/test_app_config/test_app_config.cpp`

- [ ] **Step 1: Add failing tests for trace-cache constants**

Add assertions to `test/native/test_app_config/test_app_config.cpp`:

```cpp
TEST_ASSERT_EQUAL_UINT32(1, makeDefaultConfig().recentPulseTraceCount);
TEST_ASSERT_EQUAL_UINT32(1, kMaxRecentPulseTraceCount);
```

Also add a source-level assertion that the generated app config page no longer registers an editable setting named `RAM 最近脉冲明细条数`.

- [ ] **Step 2: Run native tests**

Run:

```bash
pio test -e native -f native/test_app_config
```

Expected: FAIL until config defaults and constants are updated.

- [ ] **Step 3: Reduce RAM recent traces to one**

Change:

```cpp
constexpr std::uint32_t kDefaultRecentPulseTraceCount = 1;
constexpr std::uint32_t kMinRecentPulseTraceCount = 1;
constexpr std::uint32_t kMaxRecentPulseTraceCount = 1;
```

Keep Web/app config help text clear that this is the latest trace cache, not the long-term sample library.

Do not leave `recentPulseTraceCount` as an editable Web/app config field with min=max=1. Keep the config value internally for allocation compatibility, but remove or hide the user-facing setting from `FaucetAppConfig.cpp`. The UI may show a read-only technical note elsewhere if needed.

- [ ] **Step 4: Wire new stores in `main.cpp`**

Use these paths:

```cpp
constexpr const char* kCalibrationSessionPath = "/faucet_cal_session_v1.bin";
constexpr const char* kCalibrationSessionTracePath = "/faucet_cal_session_traces_v1.bin";
constexpr const char* kCalibrationLongTermSamplesPath = "/faucet_cal_samples_v1.bin";
```

Instantiate stores beside the existing record and metering stores. Keep old `/faucet_pulse_traces_v4.bin` available for ordinary record pulse-detail viewing and manual diagnostic saves; do not use it as the guided calibration session sample source.

- [ ] **Step 5: Add clean-device space gates**

Implement startup logging for the new PCB clean filesystem assumption:

```text
warn below 350 KiB free LittleFS
refuse to start calibration session below 120 KiB free LittleFS if fixed files are not ready
refuse to initialize long-term sample library below 200 KiB free LittleFS
```

If fixed files are already created and stores are ready, normal slot overwrite does not require free-space growth.

Session sample storage is required for guided calibration. Long-term sample storage is optional professional functionality: if `CalibrationLongTermSampleStore::begin()` fails because of space or file corruption, disable “样本库 / 存入长期样本库 / 从样本库生成” actions but still allow guided calibration if the session store and 5-slot session trace store are ready.

- [ ] **Step 6: Run tests and build**

Run:

```bash
pio test -e native -f native/test_app_config
pio run -e esp32dev
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add include/app/AppConfig.h src/app/AppConfig.cpp src/app/FaucetAppConfig.cpp src/main.cpp test/native/test_app_config/test_app_config.cpp
git commit -m "feat: wire calibration storage gates"
```

### Task 4: AppController CAL Mode

**Files:**
- Modify: `include/app/AppController.h`
- Modify: `src/app/AppController.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Add failing AppController tests**

Add tests proving:

```text
starting calibration from idle enters Preparing
starting calibration while running is rejected
Web cannot start water in calibration mode
local OK starts calibration run from WaitingLocalRun
run completion moves to AwaitingActual
run completion writes pending session trace before AwaitingActual
CANCEL during AwaitingActual asks to skip sample
pause then resume marks the attempt invalid for generation
calibration outflow still increments statistics and filters
existing result-page actual-ml editing mode is removed or renamed so it cannot conflict with the new calibration session mode
safety-stop completion enters AwaitingActual with a warning instead of using the safety target as actual ml
restored AwaitingActual session can submit actual ml after reboot
restored Running session marks the interrupted attempt invalid
```

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_app_controller
```

Expected: FAIL until CAL mode exists.

- [ ] **Step 3: Add snapshot and commands**

Add to `AppSnapshot`:

```cpp
CalibrationSessionStatus calibrationStatus;
std::uint8_t calibrationAttemptCount;
std::uint8_t calibrationValidSampleCount;
std::uint32_t calibrationMinActualMl;
std::uint32_t calibrationMaxActualMl;
bool calibrationCanQuickGenerate;
bool calibrationRecommended;
```

Add controller methods:

```cpp
bool startCalibrationSessionForWeb(std::uint32_t nowSeconds);
bool discardCalibrationSessionForWeb(std::uint32_t nowSeconds);
bool submitCalibrationActualForWeb(std::uint32_t actualMl, std::uint32_t nowSeconds);
bool skipCalibrationAttemptForWeb(CalibrationSkipReason reason, std::uint32_t nowSeconds);
bool generateCalibrationForWeb(std::uint32_t nowSeconds);
bool applyGeneratedCalibrationForWeb(std::uint32_t nowSeconds);
```

No method may open, close, pause, resume, or stop the valve from Web.

Update `AppController` constructors and native test fixtures in the same task so the controller receives the session store, session trace store, long-term sample store, and record calibration writer through explicit dependencies. Do not make Web handlers reach around `AppController` to mutate session state.

- [ ] **Step 4: Replace the old local calibration mode with the session mode**

The current `LocalUiMode::Calibration` is used by result-page single-record actual-ml editing. Remove that old flow or rename it to a non-conflicting mode before adding the new session workflow. The new device-level calibration session must not share button handling with the old result-page edit flow.

- [ ] **Step 5: Implement local button transitions**

In CAL mode:

```text
OK on Preparing -> WaitingLocalRun
OK on WaitingLocalRun -> start a local calibration run with the current target hint
OK on AwaitingActual -> save adjusted actual ml
CANCEL short on AwaitingActual -> confirm skip current attempt
CANCEL long outside Running -> confirm discard session
CANCEL during Running -> existing stop path
OK during Running -> existing pause path
```

Calibration run target hints are advisory, not required measured volumes:

```text
small hint: 500ml target hint, 1000ml safety stop
common hint: 1500ml target hint, 3000ml safety stop
large hint: 7500ml target hint, 10000ml safety stop
custom hint: locally adjusted target hint, bounded by maxOutVolumeMl
```

The user should normally stop the calibration run locally with `CANCEL` when the container reaches a readable amount. The recorded actual ml entered after the run is the calibration truth. Safety stop values only prevent unattended overflow and do not become the actual calibration volume.

If the run ends because the safety stop target was reached, still transition to `AwaitingActual`, but surface a warning state/message such as “已触发安全停止；如量杯读数不可确认，请放弃本次”. Never prefill or save the safety stop volume as `actualMl`.

- [ ] **Step 6: Persist pending attempt before awaiting actual ml**

When a calibration run ends, before switching to `AwaitingActual`:

```text
copy latest RAM trace
copy the completed WaterRecord into CalibrationAttempt::record
reject immediately if the trace is missing or cannot be copied
write trace body to a pending session trace slot with savePending()
save the session record with status AwaitingActual, attempt status PendingActual, and sessionTraceSlot set
```

This is required because RAM only keeps one recent trace and is lost across reboot. A restored `AwaitingActual` session must still be able to accept actual ml using the pending trace slot and the full stored `WaterRecord` identity.

When boot restores a session whose status is `Running`, mark that attempt `Invalid` with `CalibrationInvalidReason::ErrorResult`, invalidate any pending trace slot for that attempt, and show that the previous run was interrupted and cannot participate in generation.

- [ ] **Step 7: Save valid samples to session trace store**

When a calibration run ends and actual ml is submitted:

```text
load the pending session trace slot
reject if missing / truncated / resumed after pause / no pulse / analysis failed
write WaterRecordCalibrationStore actualMl metadata using CalibrationAttempt::record
commitValid() the pending trace slot with actualMl
then mark attempt Valid in the session record
```

If session trace save fails or `WaterRecordCalibrationStore` write fails, mark the attempt invalid with `StorageFailed`. Do not count the sample as valid unless both the trace slot and the record calibration metadata are saved.

- [ ] **Step 8: Re-run AppController tests**

Run:

```bash
pio test -e native -f native/test_app_controller
```

Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "feat: add local calibration session mode"
```

### Task 5: Generate and Apply Calibration Schemes

**Files:**
- Modify: `src/app/CalibrationSession.cpp`
- Modify: `include/app/MeteringScheme.h`
- Modify: `src/app/MeteringScheme.cpp`
- Modify: `include/app/MeteringSchemeStore.h`
- Modify: `src/app/MeteringSchemeStore.cpp`
- Test: `test/native/test_metering_scheme/test_metering_scheme.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [ ] **Step 1: Add failing tests for session-sourced schemes**

Add tests proving:

```text
2 valid samples with 500ml span can produce quick result with warning
3 valid samples with 1000ml span is recommended
session apply creates and activates a new scheme
old active scheme remains valid
session-generated scheme stores generatedKind=CalibrationSession
long-term sample generated scheme stores generatedKind=LongTermSampleLibrary
scheme stores source summary even when session trace slots are later overwritten
scheme stores aggregate evidence and source ids even when session trace slots are later overwritten
long-term sample generation still saves new scheme without activation
```

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_metering_scheme -f native/test_metering_scheme_store
```

Expected: FAIL until session source handling exists.

- [ ] **Step 3: Add structured generated source distinction**

Keep `MeteringSchemeSource::Generated` for generated schemes, and add a generated-source subtype:

```cpp
enum class MeteringSchemeGeneratedKind : std::uint8_t {
    None = 0,
    CalibrationSession = 1,
    LongTermSampleLibrary = 2,
};
```

Add this field to both `MeteringSchemeRecord` and `MeteringSchemeCandidate`:

```cpp
MeteringSchemeGeneratedKind generatedKind = MeteringSchemeGeneratedKind::None;
```

Set it as follows:

```text
Default / Manual / Migrated schemes: generatedKind=None
Generated from ordinary calibration session: sourceType=Generated, generatedKind=CalibrationSession
Generated from long-term sample library: sourceType=Generated, generatedKind=LongTermSampleLibrary
```

`creationSummary` should still contain human-readable text, but UI and tests must use `generatedKind` for the source distinction. Do not expand `MeteringSchemeRecord` to store full per-sample details in this task. Preserve structured scheme evidence in existing candidate/scheme evidence fields:

```text
sampleCount
sampleTraceIds or long-term sample ids
minActualMl
maxActualMl
maxErrorMl
maxErrorPercent
startup duration summary
```

Expose full per-sample details only while the source session slots or long-term sample entries still exist. The scheme itself must remain understandable after those details are unavailable because it stores aggregate evidence and source ids.

- [ ] **Step 4: Implement apply path**

Implement the apply path in `AppController` orchestration, using existing `MeteringSchemeStore` methods:

```text
load generated session candidate
save as new scheme
enable new scheme
apply active scheme to runtime
keep previous scheme valid
clear pending generated state only after success
```

- [ ] **Step 5: Re-run tests**

Run:

```bash
pio test -e native -f native/test_metering_scheme -f native/test_metering_scheme_store
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/app/MeteringScheme.h src/app/MeteringScheme.cpp include/app/MeteringSchemeStore.h src/app/MeteringSchemeStore.cpp src/app/CalibrationSession.cpp test/native/test_metering_scheme/test_metering_scheme.cpp test/native/test_metering_scheme_store/test_metering_scheme_store.cpp
git commit -m "feat: apply calibration session schemes"
```

### Task 6: Web Routes, Navigation, and JSON

**Files:**
- Modify: `src/web/FaucetWebRoutes.cpp`
- Modify: `include/web/FaucetWeb.h`
- Modify: `include/web/FaucetWebPolicy.h`
- Modify: `src/web/FaucetWebPolicy.cpp`
- Modify: `src/web/FaucetWebJson.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Test: `test/native/test_faucet_web_policy/test_faucet_web_policy.cpp`
- Test: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`

- [ ] **Step 1: Add failing Web route tests**

Assert navigation order:

```text
首页 | 记录 | 统计 | 预设 | 滤芯 | 校准 | 计量方案
```

Assert route allowlist includes:

```text
GET /faucet/calibration
POST /faucet/calibration
GET /faucet/metering
POST /faucet/metering
GET /faucet/calibration/samples
```

Assert no route contains:

```text
/api/faucet/start
/api/faucet/stop
/api/faucet/water
action=start_water
action=stop_water
```

Assert busy redirect policy includes:

```cpp
TEST_ASSERT_TRUE(faucetWebWriteBusyRedirect(true, FaucetWebWriteTarget::Metering, location, sizeof(location)));
TEST_ASSERT_EQUAL_STRING("/faucet/metering?error=busy", location);
```

- [ ] **Step 2: Add failing JSON tests**

Require status JSON to include:

```json
"calibration":{"status":"idle","attemptCount":0,"validSampleCount":0,"canQuickGenerate":false,"recommended":false}
```

- [ ] **Step 3: Run failing Web tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_faucet_web_policy -f native/test_faucet_web_json
```

Expected: FAIL until routes and JSON are updated.

- [ ] **Step 4: Update routes and context**

Move scheme management from calibration route to `/faucet/metering`. Add store pointers for session and samples to `FaucetWebContext`.

Add `FaucetWebWriteTarget::Metering` and route metering write failures to `/faucet/metering?error=busy`. Keep calibration writes on `/faucet/calibration?error=busy`.

- [ ] **Step 5: Serialize session status**

Add compact calibration JSON. Keep it small and bounded; do not serialize raw samples in status.

- [ ] **Step 6: Re-run Web route and JSON tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_faucet_web_policy -f native/test_faucet_web_json
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/web/FaucetWebRoutes.cpp include/web/FaucetWeb.h include/web/FaucetWebPolicy.h src/web/FaucetWebPolicy.cpp src/web/FaucetWebJson.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp test/native/test_faucet_web_policy/test_faucet_web_policy.cpp test/native/test_faucet_web_json/test_faucet_web_json.cpp
git commit -m "feat: expose calibration session routes"
```

### Task 7: Calibration and Metering Web Pages

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Add failing page source tests**

Require calibration page to include:

```text
校准会话
请在设备旁通过本地按键操作出水
有效样本
放弃本次
确认应用
样本库
```

Require calibration page to exclude:

```text
流量计计量方案
新建方案
切换使用
```

Require metering page to include:

```text
计量方案
当前启用
新建方案
从样本库生成
```

- [ ] **Step 2: Add failing handler tests**

POST `/faucet/calibration` actions:

```text
start_session
discard_session
save_actual
skip_attempt
generate_session
apply_session
```

POST `/faucet/metering` actions:

```text
create_metering_scheme
edit_metering_scheme
enable_metering_scheme
delete_metering_scheme
save_generated_scheme
discard_generated_scheme
```

Busy guard must reject write actions while water is running or confirmation/pause state is active.

Handler tests must separately prove:

```text
POST /faucet/calibration start_session is rejected while waterTaskActive
POST /faucet/metering enable_metering_scheme is rejected while waterTaskActive and redirects to /faucet/metering?error=busy
```

- [ ] **Step 3: Run failing Web handler tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_faucet_web_handler
```

Expected: FAIL until pages and handlers are split.

- [ ] **Step 4: Implement page split**

Keep `FaucetWeb.cpp` style consistent with existing generated HTML. Do not add remote water controls. Use forms/buttons only for non-valve actions.

- [ ] **Step 5: Implement sample library view**

Under calibration advanced entry, show:

```text
10-slot long-term sample list
delete sample
view sample details
generate from selected long-term samples
```

Only allow “store to long-term sample library” when the record has actual ml, complete trace, not truncated, not resumed after pause, and analysis succeeds.

- [ ] **Step 6: Re-run Web handler tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_faucet_web_handler
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: split calibration and metering pages"
```

### Task 8: Documentation Alignment

**Files:**
- Modify: `docs/03-software-architecture.md`
- Modify: `docs/04-ui-interaction.md`
- Modify: `docs/08-change-record.md`
- Modify: `docs/09-raw-pulse-trace.md`
- Modify: `docs/10-flow-meter-metering-schemes.md`

- [ ] **Step 1: Update architecture routes**

Document:

```text
/faucet/calibration: guided calibration session
/faucet/metering: metering scheme management
/faucet/calibration/samples: long-term sample library, hidden/advanced route
```

- [ ] **Step 2: Update local UI rules**

Document CAL mode LCD and button behavior:

```text
OK starts local calibration run
CANCEL during run stops water
OK during run pauses/resumes through existing safety path
CANCEL short at actual input skips current sample after confirmation
CANCEL long exits session after confirmation
```

- [ ] **Step 3: Update change record**

Add an entry that the old calibration workbench was split into guided calibration plus metering scheme management.

- [ ] **Step 4: Commit docs**

```bash
git add docs/03-software-architecture.md docs/04-ui-interaction.md docs/08-change-record.md docs/09-raw-pulse-trace.md docs/10-flow-meter-metering-schemes.md
git commit -m "docs: align calibration session implementation"
```

### Task 9: Full Verification

- [ ] **Step 1: Run native tests**

Run:

```bash
pio test -e native
```

Expected: PASS, including `test_faucet_web_policy` for `/faucet/metering?error=busy`.

- [ ] **Step 2: Build firmware**

Run:

```bash
pio run -e esp32dev
```

Expected: PASS.

- [ ] **Step 3: Review route safety**

Run:

```bash
rg -n "/api/faucet/(water|start|stop)|action=['\\\"]?(start|stop|pause|resume).*water|remote.*出水" src include test docs
```

Expected: no Web route or form action can start, pause, resume, or stop water.

- [ ] **Step 4: Review filesystem budget**

Confirm build/runtime logs show LittleFS mounted and calibration stores initialized. On new PCB test setup, use a clean filesystem image before first end-to-end calibration test.

- [ ] **Step 5: Commit verification-only fixes if needed**

If verification reveals only test/doc corrections, stage the specific corrected files from `git status --short` and commit them:

```bash
git add docs test
git commit -m "test: verify calibration session workflow"
```

### Task 10: Board Validation

- [ ] **Step 1: Deploy to board**

Prefer:

```bash
pio run -e esp32dev -t webota
```

Use serial upload only for first flash, partition changes, unreachable network, or OTA recovery.

- [ ] **Step 2: Validate local-only water control**

On the device:

```text
enter calibration from Web
confirm LCD enters CAL
verify Web has no water button
press local OK to start water
press local CANCEL to stop
verify Web only observes state
```

- [ ] **Step 3: Validate sample workflow**

Run:

```text
valid sample 1
skip sample 2
valid sample 3
pause/resume sample 4 -> invalid for generation
valid sample 5
generate and apply
```

Expected: only valid samples participate; skipped and pause/resume attempts remain visible but excluded.

- [ ] **Step 4: Validate storage behavior**

Run two calibration sessions. Expected: second session overwrites 5-slot session sample area; long-term sample library entries remain unchanged.

- [ ] **Step 5: Record results**

Update `docs/07-board-bringup.md` with date, firmware commit, upload method, calibration session result, and any hardware observations.

## Self-Review Checklist

- Spec coverage: guided session, Web no water control, local CAL mode, pending trace recovery before actual ml entry, interrupted Running recovery invalidation, 5 session slots, 10 long-term slots, clean PCB assumption, space gates, structured generated scheme source, scheme application, and documentation alignment are covered.
- Placeholder scan: this plan leaves no open-ended implementation choices where they affect behavior.
- Type consistency: constants and type names introduced in Task 1 are reused by later tasks.
