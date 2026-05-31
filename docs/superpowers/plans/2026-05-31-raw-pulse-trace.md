# Raw Pulse Trace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace second-bucket pulse traces with objective raw GPIO edge traces using `uint32_t elapsedUs[]`, add record detail pages, and derive effective pulse views from raw data.

**Architecture:** `FlowPulseReader` continues to capture ISR edge timestamps. `AppController` records every raw edge into `WaterPulseTraceStore`, while `FlowMeter` independently applies `pulseMinIntervalUs` for real-time effective metering. `WaterPulseTraceStore` stores fixed-capacity raw edge arrays in RAM and LittleFS v3 slots, and Web renders user-facing record details separately from professional pulse detail diagnostics.

**Tech Stack:** PlatformIO C++17 native tests, ESP32 Arduino firmware, LittleFS via existing `WaterRecordFileBackend`, existing Esp32Base Web/AppConfig.

---

## File Map

- Modify `include/app/AppConfig.h`: add pulse interval constants, config field, and new trace capacity constants.
- Modify `src/app/AppConfig.cpp`: defaults and sanitization.
- Modify `src/app/ConfigStore.cpp`: bump system config version, load/save `pulse_min_us`, keep no old trace-data compatibility.
- Modify `src/app/FaucetAppConfig.cpp`: expose `pulseMinIntervalUs`, set `recentPulseTraceCount` range to 1-3, show max effective frequency in help text.
- Modify `include/app/FlowMeter.h`, `src/app/FlowMeter.cpp`: use configured pulse interval and expose it where needed.
- Replace `include/app/WaterPulseTraceStore.h`, `src/app/WaterPulseTraceStore.cpp` second-bucket trace model with raw-edge trace model.
- Modify `include/app/AppController.h`, `src/app/AppController.cpp`: remove second sampler state; append raw edges from `onFlowPulse`; track pause-resume segments.
- Modify `src/main.cpp`: allocate raw edge buffers, switch saved path to v3, set saved capacity to 12.
- Modify `src/web/FaucetWeb.cpp`: add record detail page, update pulse detail page to derive effective/raw views, update save/delete/calibration flows.
- Modify `src/web/FaucetWebJson.cpp` if config JSON exposes trace settings.
- Update tests under `test/native/`.
- Update `docs/03-software-architecture.md`, `docs/05-test-plan.md`, `docs/08-change-record.md` after implementation.

---

## Task 1: Configuration Model

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `src/app/AppConfig.cpp`
- Modify: `src/app/ConfigStore.cpp`
- Modify: `src/app/FaucetAppConfig.cpp`
- Test: `test/native/test_app_config/test_app_config.cpp`
- Test: `test/native/test_config_store/test_config_store.cpp`

- [ ] **Step 1: Write failing config tests**

Add expectations:

```cpp
TEST_ASSERT_EQUAL_UINT32(1000, kDefaultPulseMinIntervalUs);
TEST_ASSERT_EQUAL_UINT32(100, kMinPulseMinIntervalUs);
TEST_ASSERT_EQUAL_UINT32(100000, kMaxPulseMinIntervalUs);
TEST_ASSERT_EQUAL_UINT32(3, kDefaultRecentPulseTraceCount);
TEST_ASSERT_EQUAL_UINT32(1, kMinRecentPulseTraceCount);
TEST_ASSERT_EQUAL_UINT32(3, kMaxRecentPulseTraceCount);
TEST_ASSERT_EQUAL_UINT32(kDefaultPulseMinIntervalUs, config.pulseMinIntervalUs);
```

Add sanitization checks:

```cpp
config.pulseMinIntervalUs = 1;
sanitizeConfig(config);
TEST_ASSERT_EQUAL_UINT32(kMinPulseMinIntervalUs, config.pulseMinIntervalUs);
config.pulseMinIntervalUs = 999999;
sanitizeConfig(config);
TEST_ASSERT_EQUAL_UINT32(kMaxPulseMinIntervalUs, config.pulseMinIntervalUs);
```

Add config store round-trip:

```cpp
config.pulseMinIntervalUs = 2500;
config.recentPulseTraceCount = 2;
TEST_ASSERT_TRUE(store.saveSystemConfig(config));
const SystemConfig loaded = store.loadSystemConfig();
TEST_ASSERT_EQUAL_UINT32(2500, loaded.pulseMinIntervalUs);
TEST_ASSERT_EQUAL_UINT32(2, loaded.recentPulseTraceCount);
```

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_app_config -f native/test_config_store
```

Expected: compile or assertion failures because `pulseMinIntervalUs` and new constants are missing.

- [ ] **Step 3: Implement config fields**

Add constants:

```cpp
constexpr std::uint32_t kDefaultPulseMinIntervalUs = 1000;
constexpr std::uint32_t kMinPulseMinIntervalUs = 100;
constexpr std::uint32_t kMaxPulseMinIntervalUs = 100000;
constexpr std::uint32_t kDefaultRecentPulseTraceCount = 3;
constexpr std::uint32_t kMinRecentPulseTraceCount = 1;
constexpr std::uint32_t kMaxRecentPulseTraceCount = 3;
constexpr std::uint32_t kPulseTraceMaxRawEdgesPerTrace = 4096;
constexpr std::uint32_t kSavedPulseTraceMaxCount = 12;
```

Add `std::uint32_t pulseMinIntervalUs;` to `SystemConfig`, set default, clamp it, load/save key `pulse_min_us`, bump config version by 1.

- [ ] **Step 4: Update AppConfig page**

Add a flow group field:

```cpp
Esp32BaseAppConfig::addInt({kGroupFlow, kConfigNs, kKeyPulseMinIntervalUs, "最小有效脉冲间隔", 1000, 100, 100000, 100, "us", "小于该间隔的原始边沿保留在明细中，但不计入有效脉冲。1000us 等效最大有效频率 1000Hz。", false, nullptr})
```

Update recent trace field label/help to “RAM 最近原始脉冲明细条数”, default 3, range 1-3.

- [ ] **Step 5: Verify config tests**

Run:

```bash
pio test -e native -f native/test_app_config -f native/test_config_store
```

Expected: all selected tests pass.

---

## Task 2: Raw Trace Store Model

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [ ] **Step 1: Write failing raw-edge RAM tests**

Add tests for:

```cpp
WaterPulseTraceRawEdge edges[16]{};
WaterPulseTrace traces[2]{};
WaterPulseTraceStore store(traces, 2, edges, 16, 2);
const auto id = store.beginTrace(1000, 1000, MeteringParameters{4, 80, 225});
TEST_ASSERT_TRUE(store.appendRawEdge(id, 120000));
TEST_ASSERT_TRUE(store.appendRawEdge(id, 121000));
TEST_ASSERT_TRUE(store.appendRawEdge(id, 250000));
```

Expected behavior:

- raw edge count is 3.
- effective pulse count at `pulseMinIntervalUs=1000` is 3 when intervals are exactly at boundary.
- filtered count is 0.
- `edgeAt(trace, i)->elapsedUs` returns stored values.

Add test for filtering:

```cpp
append 100000, 100400, 200000 with pulseMinIntervalUs 1000
effective count == 2
filtered count == 1
```

Add truncation test for more than `kPulseTraceMaxRawEdgesPerTrace`.

- [ ] **Step 2: Run failing test**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store
```

Expected: compile failures because raw-edge API/types do not exist.

- [ ] **Step 3: Implement raw-edge structs**

Replace second sample with:

```cpp
struct WaterPulseTraceRawEdge {
    std::uint32_t elapsedUs;
};

struct WaterPulseTrace {
    std::uint32_t traceId;
    std::uint32_t startTime;
    WaterRecord record;
    MeteringParameters meteringParams;
    std::uint32_t pulseMinIntervalUs;
    std::uint32_t rawEdgeStart;
    std::uint32_t rawEdgeCount;
    std::uint32_t rawEdgeTotal;
    std::uint32_t effectivePulseCount;
    std::uint32_t filteredEdgeCount;
    std::uint32_t actualMl;
    std::uint8_t flowSegmentCount;
    bool resumedAfterPause;
    bool finished;
    bool truncated;
};
```

Add `beginTrace(startTime, pulseMinIntervalUs, meteringParams)`, `appendRawEdge(traceId, elapsedUs)`, `edgeAt()`, and analysis helpers to derive effective pulses by comparing each raw edge against the last effective edge.

- [ ] **Step 4: Remove second-bucket append API**

Remove `appendSecond()` and old `WaterPulseTraceSample` dependencies from RAM store. Keep bucket aggregation as a derived function over raw edges:

```cpp
std::size_t aggregateWaterPulseTrace(const WaterPulseTrace&, const WaterPulseTraceRawEdge*, std::size_t, std::uint32_t bucketSeconds, WaterPulseTraceBucket*, std::size_t);
```

The bucket function must compute both raw and effective counts per bucket if the UI needs both.

- [ ] **Step 5: Verify raw trace store tests**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store
```

Expected: trace store tests pass.

---

## Task 3: Saved Trace v3 File Store

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Modify: `src/main.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [ ] **Step 1: Write failing v3 save/read tests**

Add tests that save a raw trace with 3 raw edges, read it back, and verify:

- `pulseMinIntervalUs`
- `meteringParams`
- `rawEdgeCount`
- `effectivePulseCount`
- `filteredEdgeCount`
- raw edge values
- `actualMl`
- `resumedAfterPause`

Add a capacity test that creates a file store with max count 1, saves first trace, rejects second trace with `LimitReached`.

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store
```

Expected: failures because saved store still uses v2 sample slots.

- [ ] **Step 3: Implement v3 fixed slots**

Use new magic/version and fixed slots:

```cpp
constexpr std::uint32_t kSavedTraceFileMagic = 0x46575245UL; // FWRE
constexpr std::uint16_t kSavedTraceFileVersion = 3;
```

Each slot has one index entry and `4096 * sizeof(WaterPulseTraceRawEdge)` bytes. File path in `main.cpp` becomes `/faucet_pulse_traces_v3.bin`. Saved max count becomes `kSavedPulseTraceMaxCount`.

- [ ] **Step 4: Verify saved trace tests**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store
```

Expected: all trace store tests pass.

---

## Task 4: AppController Capture Flow

**Files:**
- Modify: `include/app/AppController.h`
- Modify: `src/app/AppController.cpp`
- Modify: `src/app/FlowMeter.cpp`
- Modify: `include/app/FlowMeter.h`
- Test: `test/native/test_app_controller/test_app_controller.cpp`
- Test: `test/native/test_flow_meter/test_flow_meter.cpp`

- [ ] **Step 1: Write failing controller tests**

Add a test where raw pulses are `1000000`, `1000400`, `1120000`, configured `pulseMinIntervalUs=1000`; verify:

- record `pulseCount == 2`
- record `rejectedPulseCount == 1`
- trace raw edge count is 3
- trace effective count is 2
- trace filtered count is 1

Add pause-resume test:

- start, add pulses, pause, resume, add pulses, finish.
- trace `resumedAfterPause == true`
- trace `flowSegmentCount == 2`
- analysis eligibility is false.

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_app_controller -f native/test_flow_meter
```

Expected: failures because AppController still samples per second and FlowMeter uses hard-coded default interval.

- [ ] **Step 3: Implement raw capture**

In `startSelectedPreset()`, call:

```cpp
activeTraceId_ = pulseTraces_->beginTrace(nowSeconds, config_.pulseMinIntervalUs, activeMeteringParameters(config_));
activeTraceStartUs_ = nowMs * 1000UL;
```

In `onFlowPulse(nowUs)`:

```cpp
const bool accepted = flow_.onPulse(nowUs);
if (pulseTraces_ && activeTraceId_ != 0) {
    pulseTraces_->appendRawEdge(activeTraceId_, nowUs - activeTraceStartUs_);
}
```

Remove `samplePulseTrace()`, `lastTraceSampleMs_`, and `lastTracePulseCount_`.

Configure `FlowMeter` with `config_.pulseMinIntervalUs` in constructor and `applyConfig()`.

- [ ] **Step 4: Track pause resume segments**

When toggling from paused to running, mark the active trace as resumed and increment `flowSegmentCount`.

- [ ] **Step 5: Verify controller tests**

Run:

```bash
pio test -e native -f native/test_app_controller -f native/test_flow_meter
```

Expected: selected tests pass.

---

## Task 5: Web Record Detail and Pulse Detail

**Files:**
- Modify: `src/web/FaucetWebRoutes.cpp`
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `include/web/FaucetWeb.h`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [ ] **Step 1: Write failing route/source tests**

Add route tests for:

- `/faucet/records/item`
- `/faucet/calibration/item` if calibration needs hidden-detail context
- record list contains a “详情” link.
- pulse detail source contains labels: `原始边沿`, `有效脉冲`, `过滤边沿`, `最小有效脉冲间隔`.

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes
```

Expected: route/source tests fail.

- [ ] **Step 3: Add record detail page**

Add a user-facing record detail page that shows business data and actions. It must not show full raw edge tables by default. It links to pulse detail when trace exists.

- [ ] **Step 4: Update pulse detail page**

Use derived effective view from raw edges. Show:

- summary: raw edge count, effective pulse count, filtered count, filter ratio, pulse interval, max effective Hz, truncation, segment count.
- trend chart: effective pulses as primary, raw edges as muted auxiliary.
- table: effective pulses by default, link to show all raw edges.

- [ ] **Step 5: Verify web route tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes
```

Expected: selected tests pass.

---

## Task 6: Calibration and Save Flow

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [ ] **Step 1: Write failing tests for sample eligibility**

Add tests that verify:

- trace with `resumedAfterPause == true` is not eligible for segmented calibration.
- trace with `truncated == true` is not eligible.
- trace without actual ml is not eligible.
- completed trace without resume and with actual ml is eligible.

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store -f native/test_faucet_web_routes
```

Expected: eligibility helpers absent or wrong.

- [ ] **Step 3: Implement eligibility helpers and save behavior**

Ensure calibration stores actual volume first, then attempts to save the trace. If saved trace store is full, return a user-visible warning and keep calibration saved.

- [ ] **Step 4: Verify calibration flow tests**

Run:

```bash
pio test -e native -f native/test_water_pulse_trace_store -f native/test_faucet_web_routes
```

Expected: selected tests pass.

---

## Task 7: Documentation and Full Verification

**Files:**
- Modify: `docs/03-software-architecture.md`
- Modify: `docs/05-test-plan.md`
- Modify: `docs/08-change-record.md`
- Verify all touched tests.

- [ ] **Step 1: Update docs**

Reference `docs/09-raw-pulse-trace.md` from architecture and test plan. Record the v3 raw-edge trace change in change record.

- [ ] **Step 2: Run full native tests**

Run:

```bash
pio test -e native
```

Expected: all native tests pass.

- [ ] **Step 3: Run ESP32 build**

Run:

```bash
pio run -e esp32dev
```

Expected: build succeeds.

- [ ] **Step 4: Report upload command**

For board testing, report:

```bash
pio run -e esp32dev -t webota
```

Use serial upload only for first flash, partition changes, network unavailable, or OTA recovery.
