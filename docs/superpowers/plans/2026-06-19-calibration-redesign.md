# Calibration Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rework calibration into a current-parameter workflow with automatic flow-meter result generation, step-based temperature/TDS calibration, and a downgraded advanced sample library.

**Architecture:** Keep the existing ESP32 native-testable core first. Reuse existing metering scheme and sample stores in the first implementation, but change user-facing concepts to current parameters, history, current session samples, and advanced sample diagnostics. Web pages remain read/configuration-only and must not expose remote water control.

**Tech Stack:** C++17, PlatformIO, Unity native tests, existing Esp32Base web helpers, LittleFS-backed project stores.

---

## File Structure

- Modify `include/app/CalibrationSession.h`: raise session sample limits and add removed-sample semantics.
- Modify `src/app/CalibrationSession.cpp`: count only valid non-removed samples; allow up to 10 valid samples.
- Modify `include/app/AppController.h`: add methods for automatic candidate refresh and current-session sample removal.
- Modify `src/app/AppController.cpp`: automatically compute pending calibration candidate after sample changes; apply candidate as current parameter; keep Web control boundary unchanged.
- Modify `src/web/FaucetWeb.cpp`: redesign calibration pages and handlers around current parameters, automatic results, sample removal, history copy, temperature preview, and step-based TDS.
- Modify `src/web/FaucetWebPolicy.cpp` only if redirect destinations need renamed anchors; do not loosen busy policy.
- Modify `test/native/test_calibration_session/test_calibration_session.cpp`: sample-count and removal rules.
- Modify `test/native/test_app_controller/test_app_controller.cpp`: automatic generation, removal, apply-current behavior.
- Modify `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`: page text, absence of generate button, sample removal, sensor calibration layout.
- Modify `test/native/test_faucet_web_policy/test_faucet_web_policy.cpp`: redirect expectations only if paths change.
- Modify `docs/04-ui-interaction.md`, `docs/10-flow-meter-metering-schemes.md`, and `docs/09-raw-pulse-trace.md`: align documentation after behavior is implemented.

## Task 1: Flow Calibration Session Model

**Files:**
- Modify: `include/app/CalibrationSession.h`
- Modify: `src/app/CalibrationSession.cpp`
- Test: `test/native/test_calibration_session/test_calibration_session.cpp`

- [ ] **Step 1: Write failing tests for 10 valid samples and removed samples**

Append tests near the existing calibration session tests:

```cpp
void test_session_allows_ten_valid_samples() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    for (std::uint8_t i = 0; i < 10; ++i) {
        TEST_ASSERT_TRUE(appendCalibrationAttempt(session, validAttempt(i, 500 + static_cast<std::uint32_t>(i) * 250)));
    }
    TEST_ASSERT_EQUAL_UINT8(10, kCalibrationMaxValidSamples);
    TEST_ASSERT_EQUAL_UINT8(10, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanStartAttempt(session));
}

void test_removed_sample_does_not_count_as_valid() {
    CalibrationSessionRecord session = makeCalibrationSession(1, 1770000000);
    CalibrationAttempt attempt = validAttempt(0, 800);
    attempt.status = CalibrationAttemptStatus::Removed;
    TEST_ASSERT_TRUE(appendCalibrationAttempt(session, attempt));
    TEST_ASSERT_EQUAL_UINT8(0, countValidCalibrationSamples(session));
    TEST_ASSERT_FALSE(calibrationCanQuickGenerate(session));
}
```

Register both in `main()`:

```cpp
RUN_TEST(test_session_allows_ten_valid_samples);
RUN_TEST(test_removed_sample_does_not_count_as_valid);
```

- [ ] **Step 2: Run failing test**

Run: `pio test -e native -f test_calibration_session`

Expected: compile fails because `CalibrationAttemptStatus::Removed` does not exist, or assertions fail because max valid samples is still 3.

- [ ] **Step 3: Update session constants and status enum**

In `include/app/CalibrationSession.h`, change:

```cpp
constexpr std::uint8_t kCalibrationMinQuickSamples = 2;
constexpr std::uint8_t kCalibrationRecommendedSamples = 3;
constexpr std::uint8_t kCalibrationMaxValidSamples = 10;
constexpr std::uint8_t kCalibrationMaxAttempts = 16;
```

Add status:

```cpp
enum class CalibrationAttemptStatus : std::uint8_t {
    Empty,
    PendingActual,
    Valid,
    Skipped,
    Invalid,
    Removed,
};
```

- [ ] **Step 4: Update counting rules**

In `src/app/CalibrationSession.cpp`, make the valid predicate count only active valid samples:

```cpp
bool isValidSample(const CalibrationAttempt& attempt) {
    return attempt.status == CalibrationAttemptStatus::Valid &&
           attempt.actualMl >= kCalibrationMinActualMl &&
           attempt.record.pulseCount > 0;
}
```

Keep `Removed`, `Skipped`, and `Invalid` excluded from `countValidCalibrationSamples()`.

- [ ] **Step 5: Run session tests**

Run: `pio test -e native -f test_calibration_session`

Expected: all tests in `test_calibration_session` pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/CalibrationSession.h src/app/CalibrationSession.cpp test/native/test_calibration_session/test_calibration_session.cpp
git commit -m "feat: update calibration session sample rules"
```

## Task 2: Automatic Calibration Candidate Generation

**Files:**
- Modify: `include/app/AppController.h`
- Modify: `src/app/AppController.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Write failing tests for automatic candidate generation**

Add these local helpers and tests near existing calibration controller tests:

```cpp
struct CalibrationAppFixture {
    SystemConfig config = makeDefaultConfig();
    StatisticsStore statistics;
    FilterStore filters;
    MemoryRecordWriter records;
    MemoryCalibrationWriter calibrations;
    MemoryFileBackend backend;
    MeteringSchemeStore schemes;
    MeteringSchemeRecord active{};
    CalibrationSessionFileStore sessionStore;
    CalibrationSessionTraceStore traceStore;
    CalibrationLongTermSampleStore sampleStore;
    AppController* app = nullptr;

    CalibrationAppFixture()
        : filters(config.filters),
          schemes(backend, "/schemes.bin"),
          sessionStore(backend, "/cal-session.bin"),
          traceStore(backend, "/cal-traces.bin"),
          sampleStore(backend, "/cal-samples.bin") {
        statistics.reset({20260506, 202619, 202605});
        TEST_ASSERT_TRUE(prepareMeteringScheme(schemes, 225, active));
        TEST_ASSERT_TRUE(sessionStore.begin());
        TEST_ASSERT_TRUE(traceStore.begin());
        TEST_ASSERT_TRUE(sampleStore.begin());
        app = new AppController(config, active, statistics, filters, records, schemes, nullptr, &calibrations,
                                &sessionStore, &traceStore, &sampleStore);
    }

    ~CalibrationAppFixture() {
        delete app;
    }
};

void saveTwoSessionSamplesThroughController(CalibrationAppFixture& fixture, std::uint32_t nowSeconds) {
    CalibrationSessionRecord session = fixture.sessionStore.load();
    saveCalibrationSessionSample(fixture.traceStore, session, 0, nowSeconds, 1500, 40, 210, 6);
    saveCalibrationSessionSample(fixture.traceStore, session, 1, nowSeconds + 10, 7500, 40, 1540, 11);
    TEST_ASSERT_TRUE(fixture.sessionStore.save(session));
    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(1500, nowSeconds + 20));
    TEST_ASSERT_TRUE(fixture.app->submitCalibrationActualForWeb(7500, nowSeconds + 30));
}

void test_calibration_auto_generates_after_second_valid_sample() {
    CalibrationAppFixture fixture;
    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));

    saveTwoSessionSamplesThroughController(fixture, 1714502410);

    const AppSnapshot snapshot = fixture.app->snapshot();
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(snapshot.calibrationStatus));
    TEST_ASSERT_TRUE(fixture.app->applyGeneratedCalibrationForWeb(1714502500));
}

void test_calibration_removed_sample_recalculates_or_clears_candidate() {
    CalibrationAppFixture fixture;
    TEST_ASSERT_TRUE(fixture.app->startCalibrationSessionForWeb(1714502400));

    saveTwoSessionSamplesThroughController(fixture, 1714502410);
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::Generated),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));

    TEST_ASSERT_TRUE(fixture.app->removeCalibrationSessionSampleForWeb(1, 1714502500));
    TEST_ASSERT_EQUAL_UINT8(static_cast<unsigned>(CalibrationSessionStatus::WaitingLocalRun),
                            static_cast<unsigned>(fixture.app->snapshot().calibrationStatus));
    TEST_ASSERT_FALSE(fixture.app->applyGeneratedCalibrationForWeb(1714502600));
}
```

- [ ] **Step 2: Run failing app-controller test**

Run: `pio test -e native -f test_app_controller`

Expected: compile fails because `removeCalibrationSessionSampleForWeb()` and helper behavior do not exist, or status remains `ReadyToGenerate`.

- [ ] **Step 3: Add controller API**

In `include/app/AppController.h`, add:

```cpp
bool removeCalibrationSessionSampleForWeb(std::uint8_t attemptIndex, std::uint32_t nowSeconds);
```

In the private section, add:

```cpp
bool refreshCalibrationCandidate(std::uint32_t nowSeconds);
void clearCalibrationCandidate();
```

- [ ] **Step 4: Implement automatic refresh**

In `src/app/AppController.cpp`, implement:

```cpp
void AppController::clearCalibrationCandidate() {
    calibrationCandidate_ = MeteringSchemeCandidate{};
}

bool AppController::refreshCalibrationCandidate(std::uint32_t nowSeconds) {
    clearCalibrationCandidate();
    if (!meteringSchemes_ || !meteringSchemes_->ready() || !calibrationSessionTraces_ ||
        !calibrationSessionTraces_->ready() || !calibrationCanQuickGenerate(calibrationSession_) ||
        water_.snapshot().state != WaterState::Idle) {
        calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_)
                                         ? CalibrationSessionStatus::WaitingLocalRun
                                         : CalibrationSessionStatus::Failed;
        calibrationSession_.updatedAt = nowSeconds;
        return saveCalibrationSession();
    }

    SegmentedCalibrationSample samples[kCalibrationMaxValidSamples]{};
    std::uint32_t sourceIds[kCalibrationMaxValidSamples]{};
    std::size_t sampleCount = 0;
    const SegmentedCalibrationOptions options = segmentedCalibrationOptionsFromConfig(config_);
    for (std::uint8_t i = 0; i < calibrationSession_.attemptCount && i < kCalibrationMaxAttempts; ++i) {
        const CalibrationAttempt& attempt = calibrationSession_.attempts[i];
        if (attempt.status != CalibrationAttemptStatus::Valid ||
            attempt.sessionTraceSlot >= kCalibrationSessionTraceSlots) {
            continue;
        }
        CalibrationStoredTrace stored{};
        if (!calibrationSessionTraces_->load(attempt.sessionTraceSlot, stored)) {
            continue;
        }
        appendSessionCalibrationSample(stored,
                                       *calibrationSessionTraces_,
                                       attempt.sessionTraceSlot,
                                       options,
                                       samples,
                                       kCalibrationMaxValidSamples,
                                       sourceIds,
                                       sampleCount);
    }

    SegmentedCalibrationResult result{};
    if (!computeSegmentedCalibration(samples, sampleCount, options, result) || !result.valid) {
        calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_)
                                         ? CalibrationSessionStatus::WaitingLocalRun
                                         : CalibrationSessionStatus::Failed;
        calibrationSession_.updatedAt = nowSeconds;
        return saveCalibrationSession();
    }

    fillCandidateFromSegmentedResult(calibrationCandidate_,
                                     result,
                                     options,
                                     sourceIds,
                                     sampleCount,
                                     nowSeconds,
                                     MeteringSchemeSource::CalibrationSession);
    calibrationSession_.status = CalibrationSessionStatus::Generated;
    calibrationSession_.updatedAt = nowSeconds;
    pendingBeep_ = BeepPattern::Done;
    return saveCalibrationSession();
}
```

Then change `submitCalibrationActualForWeb()` so after appending a valid sample it calls `refreshCalibrationCandidate(nowSeconds)` instead of leaving the session in `ReadyToGenerate`.

- [ ] **Step 5: Implement sample removal**

In `src/app/AppController.cpp`, add:

```cpp
bool AppController::removeCalibrationSessionSampleForWeb(std::uint8_t attemptIndex, std::uint32_t nowSeconds) {
    if (attemptIndex >= calibrationSession_.attemptCount ||
        attemptIndex >= kCalibrationMaxAttempts ||
        water_.snapshot().state != WaterState::Idle) {
        return false;
    }
    CalibrationAttempt& attempt = calibrationSession_.attempts[attemptIndex];
    if (attempt.status != CalibrationAttemptStatus::Valid &&
        attempt.status != CalibrationAttemptStatus::PendingActual) {
        return false;
    }
    if (calibrationSessionTraces_ && attempt.sessionTraceSlot < kCalibrationSessionTraceSlots) {
        calibrationSessionTraces_->invalidate(attempt.sessionTraceSlot);
    }
    attempt.status = CalibrationAttemptStatus::Removed;
    clearCalibrationCandidate();
    calibrationSession_.validSampleCount = countValidCalibrationSamples(calibrationSession_);
    calibrationSession_.updatedAt = nowSeconds;
    return refreshCalibrationCandidate(nowSeconds);
}
```

Before the `calibrationCanQuickGenerate(calibrationSession_)` check in `refreshCalibrationCandidate()`, handle insufficient sample count explicitly:

```cpp
if (countValidCalibrationSamples(calibrationSession_) < kCalibrationMinQuickSamples) {
    calibrationSession_.status = calibrationCanStartAttempt(calibrationSession_)
                                     ? CalibrationSessionStatus::WaitingLocalRun
                                     : CalibrationSessionStatus::Failed;
    calibrationSession_.updatedAt = nowSeconds;
    return saveCalibrationSession();
}
```

- [ ] **Step 6: Keep old generate API as compatibility wrapper**

Replace `generateCalibrationForWeb()` implementation with:

```cpp
bool AppController::generateCalibrationForWeb(std::uint32_t nowSeconds) {
    return refreshCalibrationCandidate(nowSeconds);
}
```

This keeps existing tests and any advanced sample-library action working while the current session UI removes the button.

- [ ] **Step 7: Run controller tests**

Run: `pio test -e native -f test_app_controller`

Expected: app-controller tests pass.

- [ ] **Step 8: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "feat: auto-generate calibration candidates"
```

## Task 3: Flow Calibration Web Redesign

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `include/web/FaucetWebPolicy.h` only if target names need adjustment
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing Web tests for new current-session UI**

Replace or add flow-calibration render tests:

```cpp
void test_flow_calibration_page_uses_current_parameter_language() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前计量参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("本次校准样本"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("生成推荐方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("生成参数"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("计量方案列表"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("长期样本库与参数生成"));
}

void test_flow_calibration_post_remove_sample_routes_to_session() {
    WebFixture fixture;
    TEST_ASSERT_TRUE(fixture.app.startCalibrationSessionForWeb(testNowSeconds()));
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "remove_sample");
    Esp32BaseWeb::nativeTestSetParam("attemptIndex", "0");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_POST));
    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
}
```

Register both in `main()`.

- [ ] **Step 2: Run failing Web handler tests**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: tests fail because old text and `generate_session` button still render, and `remove_sample` is not handled.

- [ ] **Step 3: Update flow calibration page copy and actions**

In `handleFlowCalibrationPage()`, replace current session action rendering:

```cpp
// Remove the generate_session form from the current-session panel.
// Keep only start/discard/save actual/remove sample/apply current result actions.
```

Render these labels:

```cpp
Esp32BaseWeb::sendChunk("<h2>流量计校准</h2>");
Esp32BaseWeb::sendChunk("<h3>当前计量参数</h3>");
Esp32BaseWeb::sendChunk("<h3>本次校准样本</h3>");
Esp32BaseWeb::sendChunk("<input class='primary' type='submit' value='使用这组参数'");
```

Do not render:

```cpp
"生成推荐方案"
"应用新方案"
"计量方案列表"
"长期样本库与参数生成"
```

- [ ] **Step 4: Add remove-sample POST action**

In `handleFlowCalibrationPost()`, add before `generate_session`:

```cpp
if (std::strcmp(text, "remove_sample") == 0) {
    std::uint32_t attemptIndex = 0;
    if (!getParam("attemptIndex", text, sizeof(text)) || !parseU32(text, attemptIndex) ||
        attemptIndex >= kCalibrationMaxAttempts) {
        redirectFlowCalibrationFailure("invalid_value");
        return;
    }
    redirectFlowCalibrationResult(g_context.app &&
                                      g_context.app->removeCalibrationSessionSampleForWeb(
                                          static_cast<std::uint8_t>(attemptIndex),
                                          g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                  "sample_removed",
                                  "invalid_state");
    return;
}
```

- [ ] **Step 5: Keep advanced sample library route available**

Retain `generate_segmented`, `save_generated_scheme`, and long-term sample detail handlers for advanced sample-library work. The current flow calibration page must not show them as primary actions.

- [ ] **Step 6: Run Web handler tests**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: Web handler tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: simplify flow calibration web flow"
```

## Task 4: History Parameters and Manual Input

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing tests for history language**

Add:

```cpp
void test_flow_calibration_history_uses_parameter_language() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("历史参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("手工输入参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("复制参数"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("删除方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存为新方案"));
}
```

- [ ] **Step 2: Run failing test**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: fails because old scheme language is still present.

- [ ] **Step 3: Rename user-facing scheme sections**

In rendering functions around `sendCalibrationParameterPanels()`, change headings and buttons:

```cpp
"计量方案列表" -> "历史参数"
"新建计量方案" -> "手工输入参数"
"设为当前" -> "使用这组参数"
"编辑" -> "复制参数"
```

Do not expose delete actions in the ordinary flow page. If delete handling remains for storage maintenance, make it reachable only from an advanced maintenance route or hide it in this redesign.

- [ ] **Step 4: Add copy-to-manual behavior**

For each history row, render a link with query parameters:

```cpp
sendFmt("<a class='btn-link' href='/faucet/calibration/flow?manual=1&startupPulseCount=%lu&startupVolumeMl=%lu&stablePulsePerLiter=%lu&startupDurationMs=%lu&stableFlowMlPerMin=%lu'>复制参数</a>",
        static_cast<unsigned long>(scheme.params.startupPulseCount),
        static_cast<unsigned long>(scheme.params.startupVolumeMl),
        static_cast<unsigned long>(scheme.params.stablePulsePerLiter),
        static_cast<unsigned long>(scheme.params.startupDurationMs),
        static_cast<unsigned long>(scheme.params.stableFlowMlPerMin));
```

Use existing input validation when saving the manual parameter result.

- [ ] **Step 5: Run Web tests**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: Web handler tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: present metering schemes as parameter history"
```

## Task 5: Advanced Sample Library Downgrade

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing tests for advanced sample library**

Add:

```cpp
void test_advanced_sample_library_does_not_present_primary_apply_flow() {
    WebFixture fixture;
    saveLongTermWebSample(fixture.sampleStore, 1200, 45, 360, 12);
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow?advanced=samples");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("高级样本库"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("辅助计算"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("带入手工输入"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存为新方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("应用到当前参数"));
}
```

- [ ] **Step 2: Run failing test**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: fails because current sample library still uses long-term generation language.

- [ ] **Step 3: Rename sample library and generated result actions**

In `sendCalibrationGenerationPanel()` and related helpers:

```cpp
"长期样本库与参数生成" -> "高级样本库"
"生成参数" -> "辅助计算"
"保存为新方案" -> "带入手工输入"
"生成结果" -> "计算结果"
```

Remove any direct current-parameter application action from this advanced panel.

- [ ] **Step 4: Change generated-result action to manual prefill**

When a candidate is ready, render a GET link to the manual input route:

```cpp
sendFmt("<a class='btn-link primary' href='/faucet/calibration/flow?manual=1&startupPulseCount=%lu&startupVolumeMl=%lu&stablePulsePerLiter=%lu&startupDurationMs=%lu&stableFlowMlPerMin=%lu'>带入手工输入</a>",
        static_cast<unsigned long>(candidate.params.startupPulseCount),
        static_cast<unsigned long>(candidate.params.startupVolumeMl),
        static_cast<unsigned long>(candidate.params.stablePulsePerLiter),
        static_cast<unsigned long>(candidate.params.startupDurationMs),
        static_cast<unsigned long>(candidate.params.stableFlowMlPerMin));
```

Keep `save_generated_scheme` handler only if another advanced route still needs it; ordinary advanced sample library should not render it.

- [ ] **Step 5: Run Web tests**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: Web handler tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: downgrade sample library to diagnostics"
```

## Task 6: Temperature Calibration Card

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`
- Existing behavior tests: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Write failing Web render test**

Add:

```cpp
void test_temperature_calibration_uses_celsius_preview_language() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("温度计读数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("预览偏移"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("°C"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("0.01C"));
}
```

- [ ] **Step 2: Run failing Web test**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: fails because current page exposes `0.01C` language.

- [ ] **Step 3: Change temperature form to Celsius input**

In `handleCalibrationPage()`, replace `referenceCentiC` field with:

```cpp
Esp32BaseWeb::sendChunk("<label class='compact-field'><span>温度计读数</span><span class='estimator-input-row'>"
                        "<input name='referenceC' type='number' step='0.1' min='0' max='90' required>"
                        "<span class='unit-label'>°C</span></span></label>"
                        "<p class='hint'>保存前会按当前原始水温计算偏移。</p>");
```

- [ ] **Step 4: Parse Celsius input on POST**

In `handleCalibrationPost()`, for `temperature_save`, accept `referenceC`:

```cpp
if (std::strcmp(text, "temperature_save") == 0) {
    char tempText[32]{};
    if (!getParam("referenceC", tempText, sizeof(tempText))) {
        redirectCalibrationFailure("invalid_value");
        return;
    }
    const std::int32_t referenceCentiC = parseDecimalCelsiusToCentiC(tempText);
    if (referenceCentiC < 0 || referenceCentiC > 9000) {
        redirectCalibrationFailure("invalid_value");
        return;
    }
    const bool updated = g_context.app &&
                         g_context.app->saveTemperatureCalibrationForWeb(static_cast<std::int16_t>(referenceCentiC));
    if (!updated || !g_context.configStore->saveSystemConfig(g_context.app->config())) {
        redirectCalibrationFailure("save_failed");
        return;
    }
    *g_context.config = g_context.app->config();
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    redirectCalibrationResult(true, "temperature", "save_failed");
    return;
}
```

If no decimal parser exists, add a small local helper in `FaucetWeb.cpp`:

```cpp
std::int32_t parseDecimalCelsiusToCentiC(const char* text) {
    if (!text || !text[0]) {
        return -1;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (!end || *end != '\0' || value < 0.0 || value > 90.0) {
        return -1;
    }
    return static_cast<std::int32_t>(value * 100.0 + 0.5);
}
```

- [ ] **Step 5: Run temperature-related tests**

Run:

```bash
pio test -e native -f test_faucet_web_handler
pio test -e native -f test_app_controller
```

Expected: both suites pass.

- [ ] **Step 6: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: simplify temperature calibration"
```

## Task 7: Step-Based TDS Calibration UI

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`
- Existing behavior tests: `test/native/test_water_sensor_manager/test_water_sensor_manager.cpp`

- [ ] **Step 1: Write failing Web render tests**

Add:

```cpp
void test_tds_calibration_default_view_shows_two_point_start_not_all_forms() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("开始两点校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("高级：单点校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("1. 低值校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("2. 高值校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("开始单点校准</"));
}
```

- [ ] **Step 2: Run failing Web test**

Run: `pio test -e native -f test_faucet_web_handler`

Expected: fails because current TDS panel renders low, high, and single forms together.

- [ ] **Step 3: Render TDS by state**

Refactor `sendTdsCalibrationPanel()` into small helpers:

```cpp
void sendTdsIdlePanel(const AppSnapshot& snapshot, const SystemConfig& config, const TdsCalibrationSessionSnapshot& session);
void sendTdsLowStepPanel(const TdsCalibrationSessionSnapshot& session);
void sendTdsHighStepPanel(const TdsCalibrationSessionSnapshot& session);
void sendTdsActiveSamplingPanel(const TdsCalibrationSessionSnapshot& session);
void sendTdsReadyToSavePanel(const TdsCalibrationSessionSnapshot& session);
```

Use these rules:

```cpp
if (!tdsEnabled) {
    Esp32BaseWeb::sendChunk("<p class='hint'>先在系统设置启用 TDS 传感器。</p>");
} else if (session.active) {
    sendTdsActiveSamplingPanel(session);
} else if (session.readyToSave) {
    sendTdsReadyToSavePanel(session);
} else if (session.hasPendingLowPoint) {
    sendTdsHighStepPanel(session);
} else {
    sendTdsIdlePanel(snapshot, config, session);
}
```

- [ ] **Step 4: Keep POST actions unchanged**

Keep existing actions:

```cpp
"tds_start_low"
"tds_start_high"
"tds_start_single"
"tds_cancel"
"tds_save"
```

Only change which forms are visible in each state.

- [ ] **Step 5: Run TDS and Web tests**

Run:

```bash
pio test -e native -f test_faucet_web_handler
pio test -e native -f test_water_sensor_manager
```

Expected: both suites pass.

- [ ] **Step 6: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: make tds calibration step based"
```

## Task 8: Documentation and Full Verification

**Files:**
- Modify: `docs/04-ui-interaction.md`
- Modify: `docs/09-raw-pulse-trace.md`
- Modify: `docs/10-flow-meter-metering-schemes.md`
- Test: native suite and ESP32 build

- [ ] **Step 1: Update UI interaction docs**

In `docs/04-ui-interaction.md`, replace the Web calibration bullet with:

```markdown
- 校准：顶层保留“校准”。流量计校准围绕当前计量参数、本次校准样本、待确认结果和历史参数；本次校准达到 2 条有效样本后自动生成结果，新增、移除或修改样本后自动更新结果，不提供手动“生成参数”按钮。温度校准使用温度计读数预览偏移后保存。水质校准默认两点步骤式流程，单点校准放高级入口。高级样本库用于查看脉冲明细、稳态和启动段分析，辅助计算结果只能带入手工输入。
```

- [ ] **Step 2: Update metering scheme docs**

In `docs/10-flow-meter-metering-schemes.md`, add a section:

```markdown
## 页面命名

普通 Web 页面不再使用“计量方案列表”作为主概念。用户看到的是当前计量参数和历史参数。底层仍可复用计量方案存储结构；历史参数是以前生效过的参数快照。高级样本库的辅助计算结果不直接应用，只能带入手工输入并经过确认使用。
```

- [ ] **Step 3: Update raw pulse trace docs**

In `docs/09-raw-pulse-trace.md`, add:

```markdown
## 高级样本库定位

高级样本库用于技术观察和诊断，主要查看脉冲图、稳态开始、启动段、暂停和截断等事实。它不是普通校准主流程。普通流量计校准使用本次校准样本自动生成待确认参数；高级样本库的重新计算只是辅助功能。
```

- [ ] **Step 4: Run native tests**

Run: `pio test -e native`

Expected: all native tests pass.

- [ ] **Step 5: Run ESP32 firmware build**

Run: `pio run -e esp32dev`

Expected: build exits 0. Check Flash/RAM usage in output; if usage increases materially, note it in the final handoff.

- [ ] **Step 6: Search Web remote-control boundary**

Run:

```bash
rg -n "/api/faucet/(water|start|stop)|start water|stop water|pause|resume" src/web include/web test/native -S
```

Expected: no new remote water-control routes. Existing local-state words in comments or display text are acceptable only if they are not route handlers or Web actions.

- [ ] **Step 7: Commit docs and final verification**

```bash
git add docs/04-ui-interaction.md docs/09-raw-pulse-trace.md docs/10-flow-meter-metering-schemes.md
git commit -m "docs: align calibration redesign behavior"
```

## Self-Review Checklist

- Spec coverage:
  - Current-parameter workflow: Tasks 2, 3, 4.
  - Automatic generation, no current-session generate button: Tasks 2, 3.
  - 2-10 sample rules and removal: Tasks 1, 2, 3.
  - Advanced sample library downgrade: Task 5.
  - Temperature calibration cleanup: Task 6.
  - TDS step flow: Task 7.
  - Docs and verification: Task 8.
- Placeholder scan: no unresolved placeholders or unspecified implementation tasks are allowed in this plan.
- Type consistency:
  - `CalibrationAttemptStatus::Removed` is introduced in Task 1 and used in Task 2.
  - `removeCalibrationSessionSampleForWeb()` is declared in Task 2 and used by Web in Task 3.
  - Advanced sample-library generated results are routed to manual input, not direct apply.
