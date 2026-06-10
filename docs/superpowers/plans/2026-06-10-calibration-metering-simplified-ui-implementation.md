# Calibration Metering Simplified UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Simplify calibration session behavior and redesign the Calibration and Metering pages around the new lightweight flow.

**Architecture:** Keep the existing controller, store, and route structure, but narrow the user-facing state model. `AppController` owns timeout and reboot recovery. `FaucetWeb.cpp` owns the page copy and layout, while native tests lock behavioral and page-source regressions.

**Tech Stack:** PlatformIO, ESP32 Arduino, native Unity tests, existing ESP32Base Web helpers.

---

## File Map

- Modify `include/app/CalibrationSession.h`: set the simple sample/attempt limits and add the idle timeout constant.
- Modify `src/app/AppController.cpp`: expire stale interactive calibration sessions and invalidate `AwaitingActual` after reboot if the RAM trace is missing.
- Modify `test/native/test_app_controller/test_app_controller.cpp`: add controller tests for timeout and reboot recovery.
- Modify `src/web/FaucetWeb.cpp`: rewrite the calibration page first panel and metering page ordering/copy.
- Modify `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`: update source-level UI assertions.

## Task 1: Simplify Session Limits And Idle Behavior

**Files:**
- Modify: `include/app/CalibrationSession.h`
- Modify: `src/app/AppController.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [x] **Step 1: Write failing tests**

Add tests that assert:

```cpp
void test_app_controller_calibration_preparing_times_out_to_discarded() {
    AppController app = makeAppWithCalibrationStores();
    TEST_ASSERT_TRUE(app.startCalibrationSessionForWeb(1714502400));
    app.tick(input({false, false, false, false}, 1801000, 1801000000, 1714504201));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Discarded),
                            static_cast<unsigned>(app.snapshot().calibrationStatus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LocalUiMode::Normal),
                            static_cast<std::uint8_t>(app.snapshot().localMode));
}

void test_app_controller_reboot_drops_awaiting_actual_when_ram_trace_missing() {
    CalibrationSessionRecord session = makeCalibrationSession(77, 1714502400);
    session.status = CalibrationSessionStatus::AwaitingActual;
    session.attemptCount = 1;
    session.attempts[0].status = CalibrationAttemptStatus::PendingActual;
    TEST_ASSERT_TRUE(sessionStore.save(session));
    AppController rebooted(config, statistics, filters, records, &pulseTraces, &calibrations,
                           &sessionStore, &traceStore, &sampleStore);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(rebooted.snapshot().calibrationStatus));
}
```

- [x] **Step 2: Run test and verify failure**

Run: `pio test -e native -f native/test_app_controller -v`

Expected before implementation: at least one new test fails because sessions do not time out and `AwaitingActual` is restored unchanged.

- [x] **Step 3: Implement minimal behavior**

Set:

```cpp
constexpr std::uint8_t kCalibrationMaxValidSamples = 3;
constexpr std::uint8_t kCalibrationMaxAttempts = 6;
constexpr std::uint32_t kCalibrationIdleTimeoutSec = 30 * 60;
```

Add an `AppController` helper that discards `Preparing`, `WaitingLocalRun`, and `AwaitingActual` when `nowSeconds - updatedAt >= kCalibrationIdleTimeoutSec`.

During `restoreCalibrationSession()`, if status is `AwaitingActual` and the pending attempt has no RAM trace in `pulseTraces_`, mark the pending attempt invalid and move to `WaitingLocalRun` or `Failed`.

- [x] **Step 4: Run tests**

Run:

```bash
pio test -e native -f native/test_app_controller -v
pio test -e native -f native/test_calibration_session -v
```

Expected: all selected tests pass.

## Task 2: Redesign Calibration Page First Screen

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [x] **Step 1: Write failing source assertions**

Assert that the calibration page source contains:

```cpp
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "当前步骤"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "本次样本"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "推荐 3 条"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "最多 6 次"));
TEST_ASSERT_NULL(std::strstr(buffer, "最多 5 次"));
```

- [x] **Step 2: Run test and verify failure**

Run: `pio test -e native -f native/test_faucet_web_routes -v`

Expected before implementation: fails because the current page still says `最多 5 次` and does not use the simplified step copy.

- [x] **Step 3: Rewrite the calibration session panel**

In `handleCalibrationPage()`, replace the old generic session panel copy with:

- step title from `CalibrationSessionStatus`
- one next-action sentence
- compact counters: `本次样本 X/3`, `尝试 X/6`, `容量范围`
- only relevant forms for start, discard, save actual, skip, generate, apply

- [x] **Step 4: Run route and handler tests**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -v
pio test -e native -f native/test_faucet_web_handler -v
```

Expected: tests pass and no remote water-control routes are introduced.

## Task 3: Reorder Metering Page Around Current Scheme And Samples

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [x] **Step 1: Write source assertions**

Assert within `handleMeteringPage()` that:

```cpp
TEST_ASSERT_NOT_NULL(std::strstr(meteringPageSource, "sendSegmentedMeteringPanel"));
TEST_ASSERT_NOT_NULL(std::strstr(meteringPageSource, "长期样本库"));
TEST_ASSERT_NOT_NULL(std::strstr(meteringPageSource, "sendCalibrationGenerationPanel"));
TEST_ASSERT_NOT_NULL(std::strstr(meteringPageSource, "sendCalibrationParameterPanels"));
TEST_ASSERT_NULL(std::strstr(meteringPageSource, "sendPulseTraceCachePanel"));
```

- [x] **Step 2: Run test and verify failure**

Run: `pio test -e native -f native/test_faucet_web_routes -v`

Expected before implementation: fails because the metering page does not yet expose a long-term sample library section.

- [x] **Step 3: Add the page section**

Add a lightweight `sendLongTermSampleLibraryPanel()` placeholder backed by the existing long-term sample store when practical. It must show fixed capacity `5`, current count, and a table frame or empty state. Do not reintroduce old saved pulse trace cards.

- [x] **Step 4: Run verification**

Run:

```bash
pio test -e native -f native/test_faucet_web_routes -v
pio run -e esp32dev
```

Expected: native route tests and ESP32 build pass.

## Final Verification And Delivery

- [x] Run `git diff --check`.
- [x] Upload to the connected ESP32 serial board:

```bash
pio device list
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-57460296581
```

- [x] Commit and push:

```bash
git add include/app/CalibrationSession.h src/app/AppController.cpp src/web/FaucetWeb.cpp test/native/test_app_controller/test_app_controller.cpp test/native/test_faucet_web_routes/test_faucet_web_routes.cpp
git commit -m "feat: simplify calibration metering flow"
git push origin main
```

## Self-Review

- Spec coverage: covers simplified session timeout, reboot behavior, calibration page, metering page ordering, and legacy trace card removal.
- Placeholder scan: no `TBD` or open implementation placeholders.
- Scope note: this plan intentionally does not complete the full old-data removal or full long-term sample workflow migration; it creates the visible simplified structure and behavior first.
