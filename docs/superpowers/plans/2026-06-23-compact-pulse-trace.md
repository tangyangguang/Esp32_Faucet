# Compact Pulse Trace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace full raw-edge pulse trace storage with compact software-only pulse trajectories: full-run 500ms effective-pulse buckets, first-15s effective-pulse edges, minimal summaries, and fixed 6-slot session persistence.

**Architecture:** Keep calibration session state and trace detail storage separate. `WaterPulseTraceStore` owns in-RAM trajectory capture during a run; `CalibrationSessionTraceStore` persists compact per-attempt trace slots; `CalibrationSessionRecord` keeps only attempt state and summary data used for quick parameter generation. Web pages read compact trace slots only when the user opens detail charts.

**Tech Stack:** C++17-style embedded code under PlatformIO/Arduino ESP32, native Unity tests, LittleFS-like `WaterRecordFileBackend`, no hardware changes in this plan.

---

## File Structure

- Modify `include/app/AppConfig.h`: add compact trace capacity constants.
- Modify `include/app/WaterPulseTraceStore.h`: replace raw sample-only trace model with bucket/startup-edge model while keeping a small compatibility surface during migration.
- Modify `src/app/WaterPulseTraceStore.cpp`: capture effective pulses into 500ms buckets and first-15s edge list; compute analysis from compact trajectory.
- Modify `include/app/CalibrationSampleStore.h`: update persisted trace payload API from raw samples to compact trace details.
- Modify `src/app/CalibrationSampleStore.cpp`: create a v2 fixed 6-slot compact trace file, rebuilding incompatible v1 files.
- Modify `include/app/CalibrationSession.h`: rename summary `rejectedPulses` semantics to minimum-interval filtered count if storage-facing names are touched.
- Modify `src/app/AppController.cpp`: stop copying raw edge arrays; persist compact trace and extract minimal summaries.
- Modify `src/web/FaucetWeb.cpp`: render compact trace charts, startup detail, and min-interval filtered diagnostics.
- Modify tests:
  - `test/native/test_app_config/test_app_config.cpp`
  - `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`
  - `test/native/test_calibration_sample_store/test_calibration_sample_store.cpp`
  - `test/native/test_app_controller/test_app_controller.cpp`
  - `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`
  - `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

## Task 1: Constants And Data Shape

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `include/app/WaterPulseTraceStore.h`
- Test: `test/native/test_app_config/test_app_config.cpp`

- [ ] **Step 1: Write failing capacity tests**

Add to `test/native/test_app_config/test_app_config.cpp` near existing pulse trace constant assertions:

```cpp
void test_compact_pulse_trace_capacity_constants() {
    TEST_ASSERT_EQUAL_UINT32(500, kPulseTraceBucketMs);
    TEST_ASSERT_EQUAL_UINT32(15000, kPulseTraceStartupDetailMs);
    TEST_ASSERT_EQUAL_size_t(1200, kPulseTraceMaxBucketsPerTrace);
    TEST_ASSERT_EQUAL_size_t(4096, kPulseTraceMaxStartupEdgesPerTrace);
    TEST_ASSERT_EQUAL_size_t(6, kCalibrationSessionTraceSlots);
}
```

Add `RUN_TEST(test_compact_pulse_trace_capacity_constants);` in `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
pio test -e native -f native/test_app_config
```

Expected: compile failure for undefined `kPulseTraceBucketMs`, `kPulseTraceStartupDetailMs`, `kPulseTraceMaxBucketsPerTrace`, and `kPulseTraceMaxStartupEdgesPerTrace`.

- [ ] **Step 3: Add compact trace constants**

Add to `include/app/AppConfig.h` near existing pulse trace constants:

```cpp
constexpr std::uint32_t kPulseTraceBucketMs = 500;
constexpr std::uint32_t kPulseTraceStartupDetailMs = 15000;
constexpr std::size_t kPulseTraceMaxBucketsPerTrace = 1200;
constexpr std::size_t kPulseTraceMaxStartupEdgesPerTrace = 4096;
```

Keep `kPulseTraceMaxRawEdgesPerTrace` during migration only if still referenced by tests or old helper names. New code should use the compact constants.

- [ ] **Step 4: Define compact trace structs**

Add to `include/app/WaterPulseTraceStore.h` after `WaterPulseTracePauseWindow`:

```cpp
enum PulseTraceFlags : std::uint8_t {
    kPulseTraceFlagBucketOverflow = 1U << 0U,
    kPulseTraceFlagStartupOverflow = 1U << 1U,
    kPulseTraceFlagDroppedPulseOverflow = 1U << 2U,
};

struct WaterPulseTraceBucketSample {
    std::uint16_t pulseCount;
};
```

Update `WaterPulseTrace` fields to track compact arrays:

```cpp
std::size_t bucketStart;
std::size_t bucketCount;
std::size_t startupEdgeStart;
std::size_t startupEdgeCount;
std::uint32_t minIntervalFilteredCount;
std::uint32_t droppedPulseCount;
std::uint32_t lastEffectiveElapsedUs;
std::uint8_t flags;
```

Keep `sampleStart/sampleCount/truncated` through Task 5 so existing tests can be migrated incrementally. Remove them in Task 6 after all callers use compact APIs.

- [ ] **Step 5: Run app config tests**

Run:

```bash
pio test -e native -f native/test_app_config
```

Expected: all tests in `native/test_app_config` pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/AppConfig.h include/app/WaterPulseTraceStore.h test/native/test_app_config/test_app_config.cpp
git commit -m "Define compact pulse trace shape"
```

## Task 2: In-RAM Compact Capture

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [ ] **Step 1: Write failing tests for bucket capture**

Add tests to `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`:

```cpp
void test_trace_store_records_effective_pulses_into_500ms_buckets() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[kPulseTraceMaxBucketsPerTrace]{};
    WaterPulseTraceSample startupEdges[kPulseTraceMaxStartupEdgesPerTrace]{};
    WaterPulseTraceStore store(traces, 1, buckets, kPulseTraceMaxBucketsPerTrace, startupEdges, kPulseTraceMaxStartupEdgesPerTrace, 1);

    const std::uint32_t id = store.beginTrace(1000, 1000);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 0));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 100000));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 510000));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 900000));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_size_t(2, trace->bucketCount);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 0)->pulseCount);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 1)->pulseCount);
    TEST_ASSERT_EQUAL_size_t(4, trace->startupEdgeCount);
    TEST_ASSERT_EQUAL_UINT32(900000, store.startupEdgeAt(*trace, 3)->elapsedUs);
}
```

- [ ] **Step 2: Write failing tests for min interval filtering**

Add:

```cpp
void test_trace_store_filters_too_close_edges_without_bucket_counting() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[8]{};
    WaterPulseTraceSample startupEdges[8]{};
    WaterPulseTraceStore store(traces, 1, buckets, 8, startupEdges, 8, 1);

    const std::uint32_t id = store.beginTrace(1000, 1000);
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 0));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 500));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 1500));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT32(2, trace->totalPulses);
    TEST_ASSERT_EQUAL_UINT32(1, trace->minIntervalFilteredCount);
    TEST_ASSERT_EQUAL_UINT16(2, store.bucketAt(*trace, 0)->pulseCount);
    TEST_ASSERT_EQUAL_size_t(2, trace->startupEdgeCount);
}
```

- [ ] **Step 3: Write failing tests for overflow semantics**

Add:

```cpp
void test_trace_store_bucket_overflow_keeps_counting_totals() {
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[1]{};
    WaterPulseTraceSample startupEdges[4]{};
    WaterPulseTraceStore store(traces, 1, buckets, 1, startupEdges, 4, 1);

    const std::uint32_t id = store.beginTrace(1000, 1000);
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 0));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 600000));
    TEST_ASSERT_TRUE(store.appendPulseEdge(id, 1100000));

    const WaterPulseTrace* trace = store.findById(id);
    TEST_ASSERT_NOT_NULL(trace);
    TEST_ASSERT_EQUAL_UINT32(3, trace->totalPulses);
    TEST_ASSERT_EQUAL_size_t(1, trace->bucketCount);
    TEST_ASSERT_TRUE((trace->flags & kPulseTraceFlagBucketOverflow) != 0);
}
```

- [ ] **Step 4: Run tests to verify they fail**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store
```

Expected: compile failures for the new constructor, `appendPulseEdge`, `bucketAt`, and `startupEdgeAt`.

- [ ] **Step 5: Implement compact constructor and accessors**

Change `WaterPulseTraceStore` constructor in `include/app/WaterPulseTraceStore.h` and `src/app/WaterPulseTraceStore.cpp`:

```cpp
WaterPulseTraceStore(WaterPulseTrace* traces,
                     std::size_t traceCapacity,
                     WaterPulseTraceBucketSample* buckets,
                     std::size_t bucketCapacity,
                     WaterPulseTraceSample* startupEdges,
                     std::size_t startupEdgeCapacity,
                     std::size_t recentTraceLimit);

bool appendPulseEdge(std::uint32_t traceId, std::uint32_t elapsedUs);
const WaterPulseTraceBucketSample* bucketAt(const WaterPulseTrace& trace, std::size_t index) const;
const WaterPulseTraceSample* startupEdgeAt(const WaterPulseTrace& trace, std::size_t index) const;
```

Keep `appendRawEdge` as a wrapper only while callers migrate:

```cpp
bool WaterPulseTraceStore::appendRawEdge(std::uint32_t traceId, std::uint32_t elapsedUs) {
    return appendPulseEdge(traceId, elapsedUs);
}
```

- [ ] **Step 6: Implement 500ms bucket append**

Implement helpers in `src/app/WaterPulseTraceStore.cpp`:

```cpp
std::size_t bucketIndexForElapsedUs(std::uint32_t elapsedUs) {
    return elapsedUs / (kPulseTraceBucketMs * 1000UL);
}

bool ensureBucket(WaterPulseTraceStore& store, WaterPulseTrace& trace, std::size_t bucketIndex);
```

`appendPulseEdge` behavior:

```cpp
if (trace.hasEffectivePulse && elapsedUs - trace.lastEffectiveElapsedUs < trace.pulseMinIntervalUs) {
    ++trace.minIntervalFilteredCount;
    return true;
}

++trace.totalPulses;
trace.lastEffectiveElapsedUs = elapsedUs;
trace.hasEffectivePulse = true;

if (elapsedUs < kPulseTraceStartupDetailMs * 1000UL) {
    append to startupEdges unless full; otherwise set kPulseTraceFlagStartupOverflow
}

append/increment bucket unless bucket index >= kPulseTraceMaxBucketsPerTrace or backing capacity is full;
if full, set kPulseTraceFlagBucketOverflow but keep totalPulses updated
```

Add this field to `WaterPulseTrace`:

```cpp
bool hasEffectivePulse;
```

- [ ] **Step 7: Update finish logic**

In `finishTrace`, stop recomputing total pulses from raw samples. Use the running `trace.totalPulses` and `record.pulseCount` only as a consistency fallback:

```cpp
trace->record = record;
if (record.pulseCount > trace->totalPulses) {
    trace->totalPulses = record.pulseCount;
}
trace->finalState = finalState;
trace->finished = true;
```

- [ ] **Step 8: Run trace store tests**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store
```

Expected: all tests in `native/test_water_pulse_trace_store` pass after updating older tests to use buckets/startup edges instead of raw sample arrays.

- [ ] **Step 9: Commit**

```bash
git add include/app/WaterPulseTraceStore.h src/app/WaterPulseTraceStore.cpp test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp
git commit -m "Capture compact pulse trajectories"
```

## Task 3: Fixed 6-Slot Compact Trace Persistence

**Files:**
- Modify: `include/app/CalibrationSampleStore.h`
- Modify: `src/app/CalibrationSampleStore.cpp`
- Test: `test/native/test_calibration_sample_store/test_calibration_sample_store.cpp`

- [ ] **Step 1: Write failing file size and round-trip tests**

Replace old raw sample round-trip expectations with compact trace expectations:

```cpp
void test_session_trace_store_uses_fixed_compact_slot_file() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");

    TEST_ASSERT_TRUE(store.begin());
    const std::size_t expectedMin =
        24 + kCalibrationSessionTraceSlots * sizeof(CalibrationStoredTrace) +
        kCalibrationSessionTraceSlots * kPulseTraceMaxBucketsPerTrace * sizeof(WaterPulseTraceBucketSample) +
        kCalibrationSessionTraceSlots * kPulseTraceMaxStartupEdgesPerTrace * sizeof(WaterPulseTraceSample);
    TEST_ASSERT_TRUE(static_cast<std::size_t>(backend.fileSize("/session-traces.bin")) >= expectedMin);
}
```

Add:

```cpp
void test_session_trace_store_round_trips_compact_buckets_and_startup_edges() {
    MemoryFileBackend backend;
    CalibrationSessionTraceStore store(backend, "/session-traces.bin");
    TEST_ASSERT_TRUE(store.begin());

    CalibrationStoredTrace trace = traceFor(11, 0, 0);
    trace.trace.bucketCount = 2;
    trace.trace.startupEdgeCount = 2;
    WaterPulseTraceBucketSample buckets[2]{{3}, {4}};
    WaterPulseTraceSample startup[2]{{0}, {10000}};

    TEST_ASSERT_TRUE(store.savePending(0, trace, buckets, 2, startup, 2));
    TEST_ASSERT_TRUE(store.commitValid(0, 1000, 1770000100));

    CalibrationStoredTrace loaded{};
    TEST_ASSERT_TRUE(store.load(0, loaded));
    TEST_ASSERT_EQUAL_size_t(2, loaded.trace.bucketCount);
    TEST_ASSERT_EQUAL_size_t(2, loaded.trace.startupEdgeCount);

    WaterPulseTraceBucketSample copiedBuckets[2]{};
    WaterPulseTraceSample copiedStartup[2]{};
    TEST_ASSERT_EQUAL_size_t(2, store.readBuckets(0, copiedBuckets, 2));
    TEST_ASSERT_EQUAL_size_t(2, store.readStartupEdges(0, copiedStartup, 2));
    TEST_ASSERT_EQUAL_UINT16(4, copiedBuckets[1].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(10000, copiedStartup[1].elapsedUs);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
pio test -e native -f native/test_calibration_sample_store
```

Expected: compile failures for updated `savePending`, `readBuckets`, and `readStartupEdges`.

- [ ] **Step 3: Update store API**

Change `include/app/CalibrationSampleStore.h`:

```cpp
bool savePending(std::uint8_t slot,
                 const CalibrationStoredTrace& trace,
                 const WaterPulseTraceBucketSample* buckets,
                 std::size_t bucketCount,
                 const WaterPulseTraceSample* startupEdges,
                 std::size_t startupEdgeCount);

std::size_t readBuckets(std::uint8_t slot,
                        WaterPulseTraceBucketSample* output,
                        std::size_t outputCapacity) const;
std::size_t readStartupEdges(std::uint8_t slot,
                             WaterPulseTraceSample* output,
                             std::size_t outputCapacity) const;
```

- [ ] **Step 4: Implement v2 fixed file layout**

In `src/app/CalibrationSampleStore.cpp`, change:

```cpp
constexpr std::uint16_t kSampleVersion = 2;
```

Replace sample offsets with separate bucket and startup-edge offsets:

```cpp
std::size_t bucketOffset(std::size_t slots, std::uint8_t slot) {
    return sizeof(SampleHeader) + slots * sizeof(SampleIndexEntry) +
           static_cast<std::size_t>(slot) * kPulseTraceMaxBucketsPerTrace * sizeof(WaterPulseTraceBucketSample);
}

std::size_t startupOffset(std::size_t slots, std::uint8_t slot) {
    return sizeof(SampleHeader) + slots * sizeof(SampleIndexEntry) +
           slots * kPulseTraceMaxBucketsPerTrace * sizeof(WaterPulseTraceBucketSample) +
           static_cast<std::size_t>(slot) * kPulseTraceMaxStartupEdgesPerTrace * sizeof(WaterPulseTraceSample);
}
```

Update header validation:

```cpp
header.maxBuckets == kPulseTraceMaxBucketsPerTrace
header.maxStartupEdges == kPulseTraceMaxStartupEdgesPerTrace
```

Use this header shape and update tests to validate behavior instead of exact header byte count:

```cpp
struct SampleHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint8_t kind;
    std::uint8_t reserved0;
    std::uint32_t slotCount;
    std::uint32_t maxBuckets;
    std::uint32_t maxStartupEdges;
    std::uint32_t nextSampleId;
    std::uint32_t reserved1;
};
```

- [ ] **Step 5: Preserve rebuild-on-incompatible behavior**

Keep `CalibrationSessionTraceStore::begin()` behavior:

```cpp
ready_ = beginFixedStore(...);
if (!ready_ && validPath(path_)) {
    if (backend_.exists(path_) && !backend_.removeFile(path_)) {
        status_ = AppStorageStatus::BackendFailure;
        return false;
    }
    ready_ = initializeFile(...);
}
```

This project allows test-device storage rebuilds; do not add v1 migration.

- [ ] **Step 6: Run store tests**

Run:

```bash
pio test -e native -f native/test_calibration_sample_store
```

Expected: all tests in `native/test_calibration_sample_store` pass.

- [ ] **Step 7: Commit**

```bash
git add include/app/CalibrationSampleStore.h src/app/CalibrationSampleStore.cpp test/native/test_calibration_sample_store/test_calibration_sample_store.cpp
git commit -m "Persist compact calibration traces"
```

## Task 4: AppController Integration And Summary Extraction

**Files:**
- Modify: `src/app/AppController.cpp`
- Modify: `src/main.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Write failing tests for long high-pulse calibration trace**

Add to `test/native/test_app_controller/test_app_controller.cpp`:

```cpp
void test_app_controller_saves_long_high_pulse_calibration_without_raw_edge_truncation() {
    CalibrationAppFixture fixture;
    fixture.createApp();
    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));
    fixture.app->resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(*fixture.app, 300);

    for (std::uint32_t i = 0; i < 15750; ++i) {
        fixture.app->onFlowPulse(1000000UL + i * 14285UL);
    }
    fixture.app->tick(input({true, false, false, false}, 225000, 225000000UL, 1714502625));
    fixture.app->tick(input({true, false, false, false}, 225000 + kButtonDebounceMs, (225000 + kButtonDebounceMs) * 1000UL, 1714502625));

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, 1714502630));
    CalibrationStoredTrace stored{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(0, stored));
    TEST_ASSERT_TRUE(stored.valid);
    TEST_ASSERT_EQUAL_UINT32(15750, stored.trace.totalPulses);
    TEST_ASSERT_TRUE(stored.trace.bucketCount > 400);
    TEST_ASSERT_TRUE(stored.trace.startupEdgeCount > 900);
    TEST_ASSERT_FALSE((stored.trace.flags & kPulseTraceFlagBucketOverflow) != 0);
}
```

- [ ] **Step 2: Write failing test for bucket overflow not invalidating summary**

Add a second test in `test/native/test_app_controller/test_app_controller.cpp` using a fixture that allocates only four buckets and 128 startup edges, then simulates a longer run. The expected behavior is that the saved trace has `kPulseTraceFlagBucketOverflow`, but the submitted actual still succeeds when startup detail is complete and the stable summary has enough pulses:

```cpp
struct SmallTraceCalibrationFixture {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters{config.filters};
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes{backend, "/schemes.bin"};
    MeteringSchemeRecord active{};
    CalibrationSessionFileStore sessionStore{backend, "/cal-session.bin"};
    CalibrationSessionTraceStore traceStore{backend, "/cal-traces.bin"};
    WaterPulseTrace traces[1]{};
    WaterPulseTraceBucketSample buckets[4]{};
    WaterPulseTraceSample startupEdges[128]{};
    WaterPulseTraceStore pulseTraces{traces, 1, buckets, 4, startupEdges, 128, 1};
    AppController* app = nullptr;

    SmallTraceCalibrationFixture() {
        statistics.reset({20260506, 202619, 202605});
        TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
        TEST_ASSERT_TRUE(sessionStore.begin());
        TEST_ASSERT_TRUE(traceStore.begin());
    }

    ~SmallTraceCalibrationFixture() {
        delete app;
    }

    void createApp() {
        app = new AppController(config,
                                active,
                                statistics,
                                filters,
                                records,
                                schemes,
                                &pulseTraces,
                                &calibrations,
                                &sessionStore,
                                &traceStore);
    }
};

void test_app_controller_bucket_overflow_keeps_calibration_summary_usable() {
    SmallTraceCalibrationFixture fixture;
    fixture.createApp();

    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));
    fixture.app->resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(*fixture.app, 300);
    for (std::uint32_t i = 0; i < 500; ++i) {
        fixture.app->onFlowPulse(1000000UL + i * 2000UL);
    }
    fixture.app->tick(input({true, false, false, false}, 6000, 6000000UL, 1714502406));
    fixture.app->tick(input({true, false, false, false}, 6000 + kButtonDebounceMs, (6000 + kButtonDebounceMs) * 1000UL, 1714502406));

    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(520, 1714502410));
    CalibrationStoredTrace stored{};
    TEST_ASSERT_TRUE(fixture.traceStore.load(0, stored));
    TEST_ASSERT_TRUE((stored.trace.flags & kPulseTraceFlagBucketOverflow) != 0);
    TEST_ASSERT_TRUE(stored.trace.startupEdgeCount > 0);
}
```

- [ ] **Step 3: Run tests to verify failures**

Run:

```bash
pio test -e native -f native/test_app_controller
```

Expected: compile or test failures while AppController still expects raw sample arrays.

- [ ] **Step 4: Update main allocation**

In `src/main.cpp`, replace raw sample allocation:

```cpp
constexpr std::size_t kPulseTraceMaxBuckets =
    static_cast<std::size_t>(faucet::kRecentPulseTraceCount) * faucet::kPulseTraceMaxBucketsPerTrace;
constexpr std::size_t kPulseTraceMaxStartupEdges =
    static_cast<std::size_t>(faucet::kRecentPulseTraceCount) * faucet::kPulseTraceMaxStartupEdgesPerTrace;

faucet::WaterPulseTraceBucketSample* g_pulseTraceBuckets = nullptr;
faucet::WaterPulseTraceSample* g_pulseTraceStartupEdges = nullptr;
```

Construct:

```cpp
g_pulseTraces = new (std::nothrow) faucet::WaterPulseTraceStore(
    g_pulseTraceRecords,
    kPulseTraceCapacity,
    g_pulseTraceBuckets,
    kPulseTraceMaxBuckets,
    g_pulseTraceStartupEdges,
    kPulseTraceMaxStartupEdges,
    faucet::kRecentPulseTraceCount);
```

- [ ] **Step 5: Update pulse ingestion call names**

In `AppController::onFlowPulse`, continue calling one method only:

```cpp
if (activeTraceId_ != 0 && pulseTraces_) {
    pulseTraces_->appendPulseEdge(activeTraceId_, elapsedSince(nowUs, activeTraceStartUs_));
}
```

Do not store filtered edges in trace detail; `FlowMeter::onPulse` remains the source of `rejectedPulses` for WaterRecord.

- [ ] **Step 6: Persist compact trace in submit actual**

Replace raw sample copy in `submitCalibrationActualForWeb` with:

```cpp
std::unique_ptr<WaterPulseTraceBucketSample[]> buckets(new (std::nothrow) WaterPulseTraceBucketSample[trace->bucketCount]{});
std::unique_ptr<WaterPulseTraceSample[]> startupEdges(new (std::nothrow) WaterPulseTraceSample[trace->startupEdgeCount]{});
```

Copy with `bucketAt` and `startupEdgeAt`; call:

```cpp
calibrationSessionTraces_->savePending(attempt.sessionTraceSlot,
                                       stored,
                                       buckets.get(),
                                       trace->bucketCount,
                                       startupEdges.get(),
                                       trace->startupEdgeCount)
```

- [ ] **Step 7: Extract summary from compact data**

Replace raw `analyzeWaterPulseTrace(trace, samples, sampleCount, options)` use with a compact analyzer that consumes:

```cpp
WaterPulseTraceAnalysis analyzeWaterPulseTrace(const WaterPulseTrace& trace,
                                               const WaterPulseTraceBucketSample* buckets,
                                               std::size_t bucketCount);
```

Rules:

- Startup pulse count comes from buckets before `stableStartMs`, not from raw edges.
- Stable pulse count comes from buckets from `stableStartMs` to trace end or available bucket horizon.
- `bucketOverflow` alone does not make `usableForGeneration=false`.
- `startupOverflow` makes `usableForGeneration=false` because startup detail can be incomplete.
- `resumedAfterPause`, zero total pulses, missing actual, and error result still make unusable.

- [ ] **Step 8: Run AppController tests**

Run:

```bash
pio test -e native -f native/test_app_controller
```

Expected: all tests in `native/test_app_controller` pass.

- [ ] **Step 9: Commit**

```bash
git add src/app/AppController.cpp src/main.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "Use compact traces in calibration flow"
```

## Task 5: Web Detail Rendering From Compact Trace

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing route/source tests**

In `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`, update old raw-edge expectations:

```cpp
void test_pulse_trace_page_uses_compact_buckets_not_raw_edge_arrays() {
    std::string buffer = readFile("src/web/FaucetWeb.cpp");
    TEST_ASSERT_NOT_NULL(std::strstr(buffer.c_str(), "readBuckets("));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer.c_str(), "readStartupEdges("));
    TEST_ASSERT_NULL(std::strstr(buffer.c_str(), "new (std::nothrow) WaterPulseTraceBucket[trace->sampleCount]"));
    TEST_ASSERT_NULL(std::strstr(buffer.c_str(), "原始边沿 %lu 个，有效 %lu 个，过滤 %lu 个"));
    TEST_ASSERT_NOT_NULL(std::strstr(buffer.c_str(), "最小间隔过滤"));
}
```

- [ ] **Step 2: Write failing handler output test**

In `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`, add a compact stored trace fixture and assert:

```cpp
TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("脉冲轨迹"));
TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("500ms"));
TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("最小间隔过滤"));
TEST_ASSERT_EQUAL(std::string::npos, body.find("无效脉冲"));
TEST_ASSERT_EQUAL(std::string::npos, body.find("显示全部原始边沿"));
```

- [ ] **Step 3: Run tests to verify failures**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_faucet_web_handler
```

Expected: failures while Web still reads raw samples and old text.

- [ ] **Step 4: Update trace detail page loading**

In `src/web/FaucetWeb.cpp`, replace raw sample loading:

```cpp
samples.reset(new (std::nothrow) WaterPulseTraceSample[trace->sampleCount]{});
loadedSampleCount = g_context.calibrationSessionTraces->readSamples(...);
```

with compact arrays:

```cpp
std::unique_ptr<WaterPulseTraceBucketSample[]> buckets(new (std::nothrow) WaterPulseTraceBucketSample[trace->bucketCount]{});
std::unique_ptr<WaterPulseTraceSample[]> startupEdges(new (std::nothrow) WaterPulseTraceSample[trace->startupEdgeCount]{});
const std::size_t loadedBuckets = g_context.calibrationSessionTraces->readBuckets(slot, buckets.get(), trace->bucketCount);
const std::size_t loadedStartup = g_context.calibrationSessionTraces->readStartupEdges(slot, startupEdges.get(), trace->startupEdgeCount);
```

- [ ] **Step 5: Render 1s default chart from 500ms buckets**

Add helper:

```cpp
std::uint32_t oneSecondPulseDelta(const WaterPulseTraceBucketSample* buckets,
                                  std::size_t bucketCount,
                                  std::size_t secondIndex) {
    const std::size_t first = secondIndex * 2U;
    std::uint32_t total = 0;
    if (first < bucketCount) {
        total += buckets[first].pulseCount;
    }
    if (first + 1U < bucketCount) {
        total += buckets[first + 1U].pulseCount;
    }
    return total;
}
```

Use second-level points for the default graph and show 500ms detail only as a secondary text/table section.

- [ ] **Step 6: Update terminology**

Replace user-facing strings:

- `脉冲明细` -> `脉冲轨迹` where the page now displays compact trajectories.
- `无效脉冲` -> do not use.
- `被过滤边沿` -> `最小间隔过滤`.
- `原始边沿` -> only use for historical docs/tests, not compact trajectory UI.

- [ ] **Step 7: Run Web tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_faucet_web_handler
```

Expected: both suites pass.

- [ ] **Step 8: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "Render compact pulse trajectories"
```

## Task 6: Cleanup Old Raw Trace API And Terminology

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Modify: `docs/03-software-architecture.md`
- Modify: `docs/04-ui-interaction.md`
- Modify: `docs/05-test-plan.md`
- Modify: `docs/11-realtime-flow-display.md`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Add negative source checks**

Add a source-scanning test:

```cpp
void test_compact_trace_removed_raw_edge_language_from_runtime_sources() {
    std::string app = readFile("src/app/WaterPulseTraceStore.cpp");
    std::string web = readFile("src/web/FaucetWeb.cpp");
    TEST_ASSERT_EQUAL(std::string::npos, app.find("appendRawEdge"));
    TEST_ASSERT_EQUAL(std::string::npos, web.find("显示全部原始边沿"));
    TEST_ASSERT_EQUAL(std::string::npos, web.find("无效脉冲"));
}
```

- [ ] **Step 2: Run source test to verify failure**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes
```

Expected: failure while compatibility wrappers or old strings remain.

- [ ] **Step 3: Remove raw wrappers and old aggregate helpers**

Remove or rename:

```cpp
appendRawEdge
sampleAt
readSamples
aggregateWaterPulseTrace overloads that require raw samples
effectivePulseCount overloads that scan raw samples
```

Keep only compact equivalents:

```cpp
appendPulseEdge
bucketAt
startupEdgeAt
readBuckets
readStartupEdges
effectivePulseCountFromBuckets
```

- [ ] **Step 4: Update documentation terminology**

In docs, replace runtime-facing language:

- `过滤脉冲` -> `最小间隔过滤`.
- `无效脉冲` -> remove.
- `原始脉冲明细` -> `脉冲轨迹` unless explicitly referring to old design history.

Do not add hardware instructions.

- [ ] **Step 5: Run focused tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -f native/test_water_pulse_trace_store -f native/test_calibration_sample_store
```

Expected: all focused tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/WaterPulseTraceStore.h src/app/WaterPulseTraceStore.cpp docs/03-software-architecture.md docs/04-ui-interaction.md docs/05-test-plan.md docs/11-realtime-flow-display.md test/native/test_faucet_web_routes/test_faucet_web_routes.cpp
git commit -m "Remove raw pulse trace leftovers"
```

## Task 7: Full Verification And Size Check

**Files:**
- No planned source edits unless verification exposes issues.

- [ ] **Step 1: Run full native test suite**

Run:

```bash
pio test -e native
```

Expected:

```text
417+ test cases: all succeeded
```

The exact number may increase after new tests; failures must be fixed before proceeding.

- [ ] **Step 2: Run ESP32 firmware build**

Run:

```bash
pio run -e esp32dev
```

Expected:

```text
[SUCCESS]
```

Record RAM and Flash percentages in the final handoff. Flash was previously about 95.9%; any increase should be explained.

- [ ] **Step 3: Run static scans**

Run:

```bash
rg -n "无效脉冲|显示全部原始边沿|appendRawEdge|readSamples\\(|sampleAt\\(" src include docs test/native -S
```

Expected: no runtime source hits. Test files may contain negative assertions only.

Run:

```bash
git diff --check
git status --short --branch
```

Expected: no whitespace errors; clean or only intended files before final commit.

- [ ] **Step 4: Final verification commit**

When verification fixes are needed, stage the full known migration surface:

```bash
git add include/app/AppConfig.h include/app/WaterPulseTraceStore.h include/app/CalibrationSampleStore.h include/app/CalibrationSession.h src/app/WaterPulseTraceStore.cpp src/app/CalibrationSampleStore.cpp src/app/AppController.cpp src/main.cpp src/web/FaucetWeb.cpp docs/03-software-architecture.md docs/04-ui-interaction.md docs/05-test-plan.md docs/11-realtime-flow-display.md test/native/test_app_config/test_app_config.cpp test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp test/native/test_calibration_sample_store/test_calibration_sample_store.cpp test/native/test_app_controller/test_app_controller.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp
git commit -m "Verify compact pulse trace migration"
```

When `git status --short` is clean after Step 3, skip this commit.

## Self-Review

- Spec coverage: The plan implements full-run 500ms buckets, first-15s effective edges, minimal min-interval filtering, fixed 6-slot compact persistence, bucket-overflow semantics, Web rendering, terminology cleanup, and verification.
- Scope: This is one cohesive subsystem migration. It touches capture, persistence, calibration integration, Web detail display, and docs because those are all consumers of the same trace format.
- Placeholders: No `TBD`, `TODO`, or unspecified “add tests” steps remain. Every task includes concrete files, commands, and expected behavior.
- Type consistency: The plan consistently uses `WaterPulseTraceBucketSample`, `appendPulseEdge`, `readBuckets`, `readStartupEdges`, `bucketOverflow`, `startupOverflow`, and `minIntervalFilteredCount`.
