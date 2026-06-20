# Calibration Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the calibration redesign so the calibration center is a status overview, flow-meter calibration uses current/history parameters, temperature calibration is a simple entered workflow, and TDS calibration uses a unified 1-5 calibration-point model.

**Architecture:** Keep calculation and session rules in native-testable app code first, then expose them through `AppController`, then render simple Web pages. Flow-meter calibration keeps the existing current-parameter/history-parameter direction. TDS calibration replaces the visible single/two-point choice with one in-memory calibration-point session and an automatically refreshed pending result.

**Tech Stack:** C++17, PlatformIO, Unity native tests, existing `Esp32BaseWeb` helpers, existing app configuration and LittleFS-backed stores.

---

## File Structure

- Modify `include/app/WaterSensors.h` and `src/app/WaterSensors.cpp`: add shared TDS multi-point fitting helpers with explicit span and range validation.
- Modify `include/app/WaterSensorManager.h` and `src/app/WaterSensorManager.cpp`: replace the public single/two-point Web-facing session with a 1-5 point in-memory session.
- Modify `include/app/AppConfig.h`, `src/app/AppConfig.cpp`, `src/app/ConfigStore.cpp`: add `TdsCalibrationMode::MultiPoint` and keep config sanitization/storage bounded.
- Modify `include/app/AppController.h` and `src/app/AppController.cpp`: expose simple TDS calibration-point session APIs and keep busy-state gating.
- Do not add Web routes. The route table is already at the Esp32Base default capacity of 24; temperature and TDS detail views must reuse `/faucet/calibration` with `view=temperature` and `view=tds`.
- Modify `src/web/FaucetWeb.cpp`: make calibration center a status overview, move temperature/TDS forms to detail views under the existing calibration route, render TDS calibration-point session.
- Modify tests:
  - `test/native/test_water_sensors/test_water_sensors.cpp`
  - `test/native/test_water_sensor_manager/test_water_sensor_manager.cpp`
  - `test/native/test_app_controller/test_app_controller.cpp`
  - `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
  - `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`
  - `test/native/test_config_store/test_config_store.cpp`
  - `test/native/test_app_config/test_app_config.cpp`
- Modify docs after implementation:
  - `docs/01-product-requirements.md`
  - `docs/03-software-architecture.md`
  - `docs/06-implementation-plan.md`
  - `docs/10-flow-meter-metering-schemes.md`
  - `docs/脉冲分段计量参数说明.md`

## Task 1: TDS Calibration Math

**Files:**
- Modify: `include/app/WaterSensors.h`
- Modify: `src/app/WaterSensors.cpp`
- Test: `test/native/test_water_sensors/test_water_sensors.cpp`

- [ ] **Step 1: Write failing tests for multi-point fitting**

Add these tests to `test/native/test_water_sensors/test_water_sensors.cpp`:

```cpp
void test_tds_multi_point_linear_fit_matches_two_point_line() {
    TdsCalibrationPointInput points[3]{};
    points[0] = TdsCalibrationPointInput{20, 30};
    points[1] = TdsCalibrationPointInput{120, 130};
    points[2] = TdsCalibrationPointInput{220, 230};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 3, fit));
    TEST_ASSERT_EQUAL_UINT8(3, fit.pointCount);
    TEST_ASSERT_EQUAL_UINT16(200, fit.referenceSpanPpm);
    TEST_ASSERT_EQUAL_UINT16(200, fit.rawSpanPpm);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, fit.scale);
    TEST_ASSERT_EQUAL_INT16(-10, fit.offsetPpm);
}

void test_tds_fit_uses_single_point_scale_with_zero_offset() {
    TdsCalibrationPointInput points[1]{};
    points[0] = TdsCalibrationPointInput{160, 200};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 1, fit));
    TEST_ASSERT_EQUAL_UINT8(1, fit.pointCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, fit.scale);
    TEST_ASSERT_EQUAL_INT16(0, fit.offsetPpm);
}

void test_tds_fit_rejects_low_span_for_multiple_points() {
    TdsCalibrationPointInput points[2]{};
    points[0] = TdsCalibrationPointInput{100, 100};
    points[1] = TdsCalibrationPointInput{120, 150};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));

    points[1] = TdsCalibrationPointInput{170, 120};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));
}

void test_tds_fit_rejects_duplicate_conflicts() {
    TdsCalibrationPointInput points[2]{};
    points[0] = TdsCalibrationPointInput{100, 100};
    points[1] = TdsCalibrationPointInput{100, 131};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));

    points[1] = TdsCalibrationPointInput{151, 100};
    TEST_ASSERT_FALSE(computeTdsCalibrationFit(points, 2, fit));
}

void test_tds_two_point_fit_is_order_independent() {
    TdsCalibrationPointInput points[2]{};
    points[0] = TdsCalibrationPointInput{160, 150};
    points[1] = TdsCalibrationPointInput{0, 5};

    TdsCalibrationFitResult fit{};
    TEST_ASSERT_TRUE(computeTdsCalibrationFit(points, 2, fit));
    TEST_ASSERT_TRUE(fit.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.10f, fit.scale);
    TEST_ASSERT_INT_WITHIN(1, -6, fit.offsetPpm);
}
```

Register them in `main()`:

```cpp
RUN_TEST(test_tds_multi_point_linear_fit_matches_two_point_line);
RUN_TEST(test_tds_fit_uses_single_point_scale_with_zero_offset);
RUN_TEST(test_tds_fit_rejects_low_span_for_multiple_points);
RUN_TEST(test_tds_fit_rejects_duplicate_conflicts);
RUN_TEST(test_tds_two_point_fit_is_order_independent);
```

- [ ] **Step 2: Run the failing test**

Run:

```bash
pio test -e native -f test_water_sensors
```

Expected: compile fails because `TdsCalibrationPointInput`, `TdsCalibrationFitResult`, and `computeTdsCalibrationFit()` do not exist.

- [ ] **Step 3: Add public TDS fit types**

In `include/app/WaterSensors.h`, add after `TdsComputationResult`:

```cpp
constexpr std::uint8_t kTdsCalibrationMaxPoints = 5;
constexpr std::uint16_t kTdsCalibrationMinReferenceSpanPpm = 50;
constexpr std::uint16_t kTdsCalibrationMinRawSpanPpm = 30;
constexpr float kTdsCalibrationMinScale = 0.05f;
constexpr float kTdsCalibrationMaxScale = 20.0f;
constexpr std::int16_t kTdsCalibrationMinOffsetPpm = -2000;
constexpr std::int16_t kTdsCalibrationMaxOffsetPpm = 2000;

struct TdsCalibrationPointInput {
    std::uint16_t referencePpm = 0;
    std::uint16_t rawPpm = 0;
};

struct TdsCalibrationFitResult {
    bool valid = false;
    std::uint8_t pointCount = 0;
    float scale = 1.0f;
    std::int16_t offsetPpm = 0;
    std::uint16_t referenceSpanPpm = 0;
    std::uint16_t rawSpanPpm = 0;
};
```

Declare:

```cpp
bool computeTdsCalibrationFit(const TdsCalibrationPointInput* points,
                              std::size_t count,
                              TdsCalibrationFitResult& result);
```

- [ ] **Step 4: Implement the fitting helper**

In `src/app/WaterSensors.cpp`, add:

```cpp
bool tdsFitOutputAllowed(float scale, std::int16_t offset) {
    return isfinite(scale) &&
           scale >= kTdsCalibrationMinScale &&
           scale <= kTdsCalibrationMaxScale &&
           offset >= kTdsCalibrationMinOffsetPpm &&
           offset <= kTdsCalibrationMaxOffsetPpm;
}
```

Then implement:

```cpp
bool computeTdsCalibrationFit(const TdsCalibrationPointInput* points,
                              std::size_t count,
                              TdsCalibrationFitResult& result) {
    result = TdsCalibrationFitResult{};
    if (!points || count == 0 || count > kTdsCalibrationMaxPoints) {
        return false;
    }

    std::uint16_t minReference = points[0].referencePpm;
    std::uint16_t maxReference = points[0].referencePpm;
    std::uint16_t minRaw = points[0].rawPpm;
    std::uint16_t maxRaw = points[0].rawPpm;
    for (std::size_t i = 0; i < count; ++i) {
        if (points[i].rawPpm == 0 || points[i].referencePpm > 2000 || points[i].rawPpm > 2000) {
            return false;
        }
        minReference = std::min(minReference, points[i].referencePpm);
        maxReference = std::max(maxReference, points[i].referencePpm);
        minRaw = std::min(minRaw, points[i].rawPpm);
        maxRaw = std::max(maxRaw, points[i].rawPpm);
        for (std::size_t j = i + 1; j < count; ++j) {
            if (points[i].referencePpm == points[j].referencePpm &&
                std::abs(static_cast<int>(points[i].rawPpm) - static_cast<int>(points[j].rawPpm)) > 30) {
                return false;
            }
            if (points[i].rawPpm == points[j].rawPpm &&
                std::abs(static_cast<int>(points[i].referencePpm) - static_cast<int>(points[j].referencePpm)) > 50) {
                return false;
            }
        }
    }

    result.pointCount = static_cast<std::uint8_t>(count);
    result.referenceSpanPpm = maxReference - minReference;
    result.rawSpanPpm = maxRaw - minRaw;

    if (count == 1) {
        float scale = 0.0f;
        if (!computeSinglePointTdsCalibration(points[0].referencePpm, points[0].rawPpm, scale)) {
            return false;
        }
        result.scale = scale;
        result.offsetPpm = 0;
        result.valid = tdsFitOutputAllowed(result.scale, result.offsetPpm);
        return result.valid;
    }

    if (result.referenceSpanPpm < kTdsCalibrationMinReferenceSpanPpm ||
        result.rawSpanPpm < kTdsCalibrationMinRawSpanPpm) {
        return false;
    }

    if (count == 2) {
        const TdsCalibrationPointInput& low = points[0].referencePpm <= points[1].referencePpm ? points[0] : points[1];
        const TdsCalibrationPointInput& high = points[0].referencePpm <= points[1].referencePpm ? points[1] : points[0];
        if (!computeTwoPointTdsCalibration(low.referencePpm,
                                           low.rawPpm,
                                           high.referencePpm,
                                           high.rawPpm,
                                           result.scale,
                                           result.offsetPpm)) {
            return false;
        }
        result.valid = tdsFitOutputAllowed(result.scale, result.offsetPpm);
        return result.valid;
    }

    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double x = static_cast<double>(points[i].rawPpm);
        const double y = static_cast<double>(points[i].referencePpm);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double n = static_cast<double>(count);
    const double denominator = n * sumXX - sumX * sumX;
    if (denominator <= 0.0 || !isfinite(denominator)) {
        return false;
    }
    result.scale = static_cast<float>((n * sumXY - sumX * sumY) / denominator);
    result.offsetPpm = roundToI16((sumY - static_cast<double>(result.scale) * sumX) / n);
    result.valid = tdsFitOutputAllowed(result.scale, result.offsetPpm);
    return result.valid;
}
```

- [ ] **Step 5: Run TDS math tests**

Run:

```bash
pio test -e native -f test_water_sensors
```

Expected: all `test_water_sensors` tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/WaterSensors.h src/app/WaterSensors.cpp test/native/test_water_sensors/test_water_sensors.cpp
git commit -m "feat: add tds calibration point fitting"
```

## Task 2: TDS Calibration Session Model

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `src/app/AppConfig.cpp`
- Modify: `src/app/ConfigStore.cpp`
- Modify: `include/app/WaterSensorManager.h`
- Modify: `src/app/WaterSensorManager.cpp`
- Test: `test/native/test_water_sensor_manager/test_water_sensor_manager.cpp`
- Test: `test/native/test_app_config/test_app_config.cpp`
- Test: `test/native/test_config_store/test_config_store.cpp`

- [ ] **Step 1: Write failing manager tests**

Add to `test/native/test_water_sensor_manager/test_water_sensor_manager.cpp`:

```cpp
void test_tds_calibration_point_session_generates_after_one_point() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(380);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_TRUE(session.sessionActive);
    TEST_ASSERT_EQUAL_UINT8(1, session.pointCount);
    TEST_ASSERT_TRUE(session.candidateReady);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::SinglePoint),
                            static_cast<std::uint8_t>(session.candidateMode));
}

void test_tds_calibration_point_session_multi_point_fit_and_apply() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    std::uint32_t nowMs = 0;

    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(20, 1720000001UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(160);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));

    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000030UL));
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000040UL));

    TEST_ASSERT_TRUE(manager.applyReadyTdsCalibration(config, 1720000050UL));
    TEST_ASSERT_TRUE(config.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::TwoPoint),
                            static_cast<std::uint8_t>(config.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(5, config.tdsCalibrationRevision);
}

void test_tds_calibration_point_removal_recomputes_candidate() {
    FakeAdcReader adc;
    SystemConfig config = enabledSensorConfig();
    WaterSensorManager manager(adc);
    manager.configure(config);
    TEST_ASSERT_TRUE(manager.begin());

    TEST_ASSERT_TRUE(manager.startTdsCalibrationSession(1720000000UL));
    TEST_ASSERT_TRUE(manager.startTdsCalibrationPoint(160, 1720000001UL));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        advanceSample(manager, nowMs);
    }
    TEST_ASSERT_TRUE(manager.saveStableTdsCalibrationPoint(1720000020UL));
    TEST_ASSERT_TRUE(manager.removeTdsCalibrationPoint(0, 1720000030UL));

    const TdsCalibrationSessionSnapshot session = manager.calibrationSnapshot();
    TEST_ASSERT_EQUAL_UINT8(0, session.pointCount);
    TEST_ASSERT_FALSE(session.candidateReady);
}
```

Register them in `main()`.

- [ ] **Step 2: Run failing manager tests**

Run:

```bash
pio test -e native -f test_water_sensor_manager
```

Expected: compile fails because the point-session API and snapshot fields do not exist.

- [ ] **Step 3: Add `MultiPoint` calibration mode**

In `include/app/AppConfig.h`, extend the enum:

```cpp
enum class TdsCalibrationMode : std::uint8_t {
    None,
    SinglePoint,
    TwoPoint,
    MultiPoint,
};
```

In `src/app/AppConfig.cpp`, update enum sanitization:

```cpp
if (!enumInRange(config.tdsCalibrationMode, TdsCalibrationMode::None, TdsCalibrationMode::MultiPoint)) {
    config.tdsCalibrationMode = TdsCalibrationMode::None;
}
```

Update `test/native/test_app_config/test_app_config.cpp` so enum range tests expect `MultiPoint` as the maximum valid TDS mode.

In `test/native/test_config_store/test_config_store.cpp`, update the sensor config round-trip test to use and verify `MultiPoint`:

```cpp
config.tdsCalibrationMode = TdsCalibrationMode::MultiPoint;
```

and:

```cpp
TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::MultiPoint),
                        static_cast<std::uint8_t>(loaded.tdsCalibrationMode));
```

If `src/app/ConfigStore.cpp` stores `tds_cal_mode` as the numeric enum value, no storage-key change is needed; this test still proves the new enum value survives save/load.

- [ ] **Step 4: Add TDS point-session types**

In `include/app/WaterSensorManager.h`, replace the old single/two-point snapshot fields with:

```cpp
struct TdsCalibrationPointSnapshot {
    bool valid = false;
    bool tempFallback25C = false;
    std::uint16_t referencePpm = 0;
    std::uint16_t rawPpm = 0;
    std::uint16_t voltageMv = 0;
    std::int16_t temperatureCentiC = 0;
    std::uint32_t sampledAt = 0;
};

struct TdsCalibrationSessionSnapshot {
    bool sessionActive = false;
    bool samplingActive = false;
    bool failed = false;
    bool tempFallback25C = false;
    bool candidateReady = false;
    bool full = false;
    std::uint8_t sampleCount = 0;
    std::uint8_t pointCount = 0;
    std::uint16_t referencePpm = 0;
    std::uint16_t rawAveragePpm = 0;
    std::uint16_t referenceSpanPpm = 0;
    std::uint16_t rawSpanPpm = 0;
    float candidateScale = 1.0f;
    std::int16_t candidateOffsetPpm = 0;
    TdsCalibrationMode candidateMode = TdsCalibrationMode::None;
    TdsCalibrationPointSnapshot points[kTdsCalibrationMaxPoints]{};
};
```

Add public methods:

```cpp
bool startTdsCalibrationSession(std::uint32_t nowSeconds);
bool startTdsCalibrationPoint(std::uint16_t referencePpm, std::uint32_t nowSeconds);
bool saveStableTdsCalibrationPoint(std::uint32_t nowSeconds);
bool removeTdsCalibrationPoint(std::uint8_t index, std::uint32_t nowSeconds);
bool discardTdsCalibrationSession();
bool expireTdsCalibrationSession(std::uint32_t nowSeconds);
bool applyReadyTdsCalibration(SystemConfig& config, std::uint32_t nowSeconds);
```

Keep existing single/two-point public methods as temporary wrappers during Task 2 so the branch still compiles before Web handlers are migrated. They must be deleted in Task 6 after the Web POST actions no longer call them.

- [ ] **Step 5: Implement in-memory point session**

In `src/app/WaterSensorManager.cpp`, replace the old calibration kind state with fields:

```cpp
bool tdsCalibrationSessionActive_ = false;
bool tdsCalibrationSamplingActive_ = false;
bool tdsCalibrationFailed_ = false;
std::uint32_t tdsCalibrationUpdatedAt_ = 0;
std::uint16_t tdsCalibrationReferencePpm_ = 0;
TdsCalibrationPointSnapshot tdsCalibrationPoints_[kTdsCalibrationMaxPoints]{};
std::uint8_t tdsCalibrationPointCount_ = 0;
TdsCalibrationFitResult tdsCalibrationFit_{};
```

Add a helper:

```cpp
bool WaterSensorManager::refreshTdsCalibrationCandidate() {
    TdsCalibrationPointInput points[kTdsCalibrationMaxPoints]{};
    for (std::uint8_t i = 0; i < tdsCalibrationPointCount_; ++i) {
        points[i].referencePpm = tdsCalibrationPoints_[i].referencePpm;
        points[i].rawPpm = tdsCalibrationPoints_[i].rawPpm;
    }
    return computeTdsCalibrationFit(points, tdsCalibrationPointCount_, tdsCalibrationFit_);
}
```

Implement:

```cpp
bool WaterSensorManager::startTdsCalibrationSession(std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || tdsCalibrationSamplingActive_) {
        return false;
    }
    tdsCalibrationSessionActive_ = true;
    tdsCalibrationSamplingActive_ = false;
    tdsCalibrationFailed_ = false;
    tdsCalibrationUpdatedAt_ = nowSeconds;
    tdsCalibrationPointCount_ = 0;
    tdsCalibrationFit_ = TdsCalibrationFitResult{};
    for (auto& point : tdsCalibrationPoints_) {
        point = TdsCalibrationPointSnapshot{};
    }
    return true;
}

bool WaterSensorManager::startTdsCalibrationPoint(std::uint16_t referencePpm, std::uint32_t nowSeconds) {
    if (!enabledTds(config_) || !tdsCalibrationSessionActive_ || tdsCalibrationSamplingActive_ ||
        tdsCalibrationPointCount_ >= kTdsCalibrationMaxPoints || referencePpm > 2000) {
        return false;
    }
    tdsCalibrationReferencePpm_ = referencePpm;
    calibrationSampleCount_ = 0;
    calibrationTempFallback_ = false;
    tdsCalibrationSamplingActive_ = true;
    tdsCalibrationFailed_ = false;
    tdsCalibrationUpdatedAt_ = nowSeconds;
    return true;
}
```

In `accumulateCalibration()`, append readings only when `tdsCalibrationSamplingActive_` is true:

```cpp
void WaterSensorManager::accumulateCalibration(const TdsComputationResult& result) {
    if (!tdsCalibrationSamplingActive_ || tdsCalibrationFailed_) {
        return;
    }
    if ((result.flags & (kWaterSensorFlagTdsInvalid | kWaterSensorFlagTdsAdcOverflow)) != 0) {
        tdsCalibrationFailed_ = true;
        return;
    }
    if ((result.flags & kWaterSensorFlagTdsTempFallback25C) != 0) {
        calibrationTempFallback_ = true;
    }
    if (calibrationSampleCount_ < kCalibrationMaxSamples) {
        calibrationReadings_[calibrationSampleCount_++] = result.rawPpm;
    }
}
```

Update `calibrationReady()` so it no longer depends on the old `CalibrationKind`:

```cpp
bool WaterSensorManager::calibrationReady() const {
    if (!tdsCalibrationSamplingActive_ || tdsCalibrationFailed_ || calibrationSampleCount_ < kCalibrationMinSamples) {
        return false;
    }
    return tdsReadingsStable(calibrationReadings_, calibrationSampleCount_, tdsCalibrationReferencePpm_, false);
}
```

Then implement `saveStableTdsCalibrationPoint()`:

```cpp
bool WaterSensorManager::saveStableTdsCalibrationPoint(std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || !tdsCalibrationSamplingActive_ ||
        tdsCalibrationPointCount_ >= kTdsCalibrationMaxPoints || !calibrationReady()) {
        return false;
    }
    TdsCalibrationPointSnapshot& point = tdsCalibrationPoints_[tdsCalibrationPointCount_++];
    point.valid = true;
    point.tempFallback25C = calibrationTempFallback_;
    point.referencePpm = tdsCalibrationReferencePpm_;
    point.rawPpm = calibrationRawAverage();
    point.voltageMv = snapshot_.tdsVoltageMv.valid ? static_cast<std::uint16_t>(snapshot_.tdsVoltageMv.value) : 0;
    point.temperatureCentiC =
        snapshot_.temperatureCentiC.valid ? static_cast<std::int16_t>(snapshot_.temperatureCentiC.value) : 2500;
    point.sampledAt = nowSeconds;
    tdsCalibrationSamplingActive_ = false;
    tdsCalibrationUpdatedAt_ = nowSeconds;
    refreshTdsCalibrationCandidate();
    return true;
}
```

Implement remove/discard/expire/apply with these rules:

```cpp
bool WaterSensorManager::removeTdsCalibrationPoint(std::uint8_t index, std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || tdsCalibrationSamplingActive_ || index >= tdsCalibrationPointCount_) {
        return false;
    }
    for (std::uint8_t i = index; i + 1 < tdsCalibrationPointCount_; ++i) {
        tdsCalibrationPoints_[i] = tdsCalibrationPoints_[i + 1];
    }
    --tdsCalibrationPointCount_;
    tdsCalibrationPoints_[tdsCalibrationPointCount_] = TdsCalibrationPointSnapshot{};
    tdsCalibrationUpdatedAt_ = nowSeconds;
    refreshTdsCalibrationCandidate();
    return true;
}

bool WaterSensorManager::discardTdsCalibrationSession() {
    tdsCalibrationSessionActive_ = false;
    tdsCalibrationSamplingActive_ = false;
    tdsCalibrationFailed_ = false;
    tdsCalibrationPointCount_ = 0;
    tdsCalibrationFit_ = TdsCalibrationFitResult{};
    return true;
}

bool WaterSensorManager::expireTdsCalibrationSession(std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || nowSeconds <= tdsCalibrationUpdatedAt_ ||
        nowSeconds - tdsCalibrationUpdatedAt_ < 30U * 60U) {
        return false;
    }
    return discardTdsCalibrationSession();
}

bool WaterSensorManager::applyReadyTdsCalibration(SystemConfig& config, std::uint32_t nowSeconds) {
    if (!tdsCalibrationSessionActive_ || !tdsCalibrationFit_.valid) {
        return false;
    }
    config.tdsScale = tdsCalibrationFit_.scale;
    config.tdsOffsetPpm = tdsCalibrationFit_.offsetPpm;
    config.tdsCalibrationMode = tdsCalibrationPointCount_ == 1
                                    ? TdsCalibrationMode::SinglePoint
                                    : (tdsCalibrationPointCount_ == 2 ? TdsCalibrationMode::TwoPoint
                                                                      : TdsCalibrationMode::MultiPoint);
    config.tdsCalibrationRevision = static_cast<std::uint16_t>(config.tdsCalibrationRevision + 1U);
    config.tdsCalibrationTime = nowSeconds;
    config.tdsCalibrated = true;
    discardTdsCalibrationSession();
    return true;
}
```

- [ ] **Step 6: Update snapshot**

Make `calibrationSnapshot()` populate:

```cpp
session.sessionActive = tdsCalibrationSessionActive_;
session.samplingActive = tdsCalibrationSamplingActive_;
session.failed = tdsCalibrationFailed_;
session.tempFallback25C = calibrationTempFallback_;
session.candidateReady = tdsCalibrationFit_.valid;
session.full = tdsCalibrationPointCount_ >= kTdsCalibrationMaxPoints;
session.sampleCount = calibrationSampleCount_;
session.pointCount = tdsCalibrationPointCount_;
session.referencePpm = tdsCalibrationReferencePpm_;
session.rawAveragePpm = calibrationSampleCount_ > 0 ? calibrationRawAverage() : 0;
session.referenceSpanPpm = tdsCalibrationFit_.referenceSpanPpm;
session.rawSpanPpm = tdsCalibrationFit_.rawSpanPpm;
session.candidateScale = tdsCalibrationFit_.scale;
session.candidateOffsetPpm = tdsCalibrationFit_.offsetPpm;
session.candidateMode = tdsCalibrationPointCount_ == 1
                            ? TdsCalibrationMode::SinglePoint
                            : (tdsCalibrationPointCount_ == 2 ? TdsCalibrationMode::TwoPoint
                                                              : (tdsCalibrationPointCount_ >= 3
                                                                     ? TdsCalibrationMode::MultiPoint
                                                                     : TdsCalibrationMode::None));
for (std::uint8_t i = 0; i < tdsCalibrationPointCount_; ++i) {
    session.points[i] = tdsCalibrationPoints_[i];
}
```

- [ ] **Step 7: Run manager and config tests**

Run:

```bash
pio test -e native -f test_water_sensor_manager
pio test -e native -f test_app_config
pio test -e native -f test_config_store
```

Expected: both suites pass.

- [ ] **Step 8: Commit**

```bash
git add include/app/AppConfig.h src/app/AppConfig.cpp src/app/ConfigStore.cpp include/app/WaterSensorManager.h src/app/WaterSensorManager.cpp test/native/test_water_sensor_manager/test_water_sensor_manager.cpp test/native/test_app_config/test_app_config.cpp test/native/test_config_store/test_config_store.cpp
git commit -m "feat: model tds calibration points"
```

## Task 3: AppController TDS APIs

**Files:**
- Modify: `include/app/AppController.h`
- Modify: `src/app/AppController.cpp`
- Test: `test/native/test_app_controller/test_app_controller.cpp`

- [ ] **Step 1: Write failing AppController tests**

Add:

```cpp
void test_app_tds_point_calibration_rejects_when_running() {
    SystemConfig config = enabledWaterSensorConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    TEST_ASSERT_TRUE(sensors.begin());
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);
    applyTestMeteringScheme(app);

    app.resetInputs({false, false, false, false}, 0);
    pressAndReleaseOk(app, 100);
    pressAndReleaseOk(app, 300);
    TEST_ASSERT_FALSE(app.startTdsCalibrationSessionForWeb(1714502401));
    TEST_ASSERT_FALSE(app.startTdsCalibrationPointForWeb(160, 1714502401));
}

void test_app_tds_point_calibration_apply_persists_to_config() {
    SystemConfig config = enabledWaterSensorConfig();
    StatisticsStore statistics;
    statistics.reset({20260506, 202619, 202605});
    FilterStore filters(config.filters);
    MemoryRecordWriter records;
    FakeAdcReader adc;
    WaterSensorManager sensors(adc);
    sensors.configure(config);
    TEST_ASSERT_TRUE(sensors.begin());
    AppController app(config, statistics, filters, records, nullptr, nullptr, nullptr, nullptr, nullptr, &sensors);

    TEST_ASSERT_TRUE(app.startTdsCalibrationSessionForWeb(1714502400));
    TEST_ASSERT_TRUE(app.startTdsCalibrationPointForWeb(160, 1714502401));
    std::uint32_t nowMs = 0;
    for (std::uint8_t i = 0; i < 16; ++i) {
        adc.values[2] = okMv(420);
        app.tick(input({false, false, false, false}, nowMs += 1000, nowMs * 1000UL, 1714502401));
    }
    TEST_ASSERT_TRUE(app.saveTdsCalibrationPointForWeb(1714502420));
    TEST_ASSERT_TRUE(app.applyTdsCalibrationForWeb(1714502430));

    const SystemConfig updated = app.config();
    TEST_ASSERT_TRUE(updated.tdsCalibrated);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(TdsCalibrationMode::SinglePoint),
                            static_cast<std::uint8_t>(updated.tdsCalibrationMode));
    TEST_ASSERT_EQUAL_UINT16(4, updated.tdsCalibrationRevision);
}
```

Register both in `main()`.

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f test_app_controller
```

Expected: compile fails because the new controller methods do not exist.

- [ ] **Step 3: Add AppController methods**

In `include/app/AppController.h`, add the new TDS Web methods while keeping the old single/two-point methods temporarily so existing Web code still compiles until Task 6:

```cpp
bool startTdsCalibrationSessionForWeb(std::uint32_t nowSeconds);
bool startTdsCalibrationPointForWeb(std::uint16_t referencePpm, std::uint32_t nowSeconds);
bool saveTdsCalibrationPointForWeb(std::uint32_t nowSeconds);
bool removeTdsCalibrationPointForWeb(std::uint8_t index, std::uint32_t nowSeconds);
bool discardTdsCalibrationForWeb();
bool applyTdsCalibrationForWeb(std::uint32_t nowSeconds);
TdsCalibrationSessionSnapshot tdsCalibrationSnapshot() const;
```

- [ ] **Step 4: Implement AppController methods**

In `src/app/AppController.cpp`, implement:

```cpp
bool AppController::startTdsCalibrationSessionForWeb(std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->startTdsCalibrationSession(nowSeconds);
}

bool AppController::startTdsCalibrationPointForWeb(std::uint16_t referencePpm, std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->startTdsCalibrationPoint(referencePpm, nowSeconds);
}

bool AppController::saveTdsCalibrationPointForWeb(std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->saveStableTdsCalibrationPoint(nowSeconds);
}

bool AppController::removeTdsCalibrationPointForWeb(std::uint8_t index, std::uint32_t nowSeconds) {
    return canUseTdsCalibration() && waterSensors_->removeTdsCalibrationPoint(index, nowSeconds);
}

bool AppController::discardTdsCalibrationForWeb() {
    return canUseTdsCalibration() && waterSensors_->discardTdsCalibrationSession();
}

bool AppController::applyTdsCalibrationForWeb(std::uint32_t nowSeconds) {
    if (!canUseTdsCalibration()) {
        return false;
    }
    SystemConfig updated = config_;
    if (!waterSensors_->applyReadyTdsCalibration(updated, nowSeconds)) {
        return false;
    }
    sanitizeConfig(updated);
    config_ = updated;
    waterSensors_->configure(config_);
    return true;
}
```

Keep `canUseTdsCalibration()` unchanged so outflow busy states block all TDS calibration writes.

- [ ] **Step 5: Run AppController tests**

Run:

```bash
pio test -e native -f test_app_controller
```

Expected: app-controller tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "feat: expose tds calibration point controls"
```

## Task 4: Calibration Center Overview

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing center-page tests**

Add:

```cpp
void test_calibration_center_is_status_overview() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前计量参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("启动脉冲"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("启动水量"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("稳态 P/L"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("进入流量计校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("进入温度校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("进入水质校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("参考温度</span>"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存参考温度"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("参考 ppm"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("保存为校准点"));
}
```

Register in `main()`.

- [ ] **Step 2: Run failing Web handler tests**

Run:

```bash
pio test -e native -f test_faucet_web_handler
```

Expected: fails because the center page still includes temperature/TDS forms or does not show full metering details.

- [ ] **Step 3: Refactor center page into cards**

In `src/web/FaucetWeb.cpp`, make `handleCalibrationPage()` render only overview cards:

```cpp
void sendCalibrationCenterFlowCard(const AppSnapshot& snapshot);
void sendCalibrationCenterTemperatureCard(const AppSnapshot& snapshot, const SystemConfig& config);
void sendCalibrationCenterTdsCard(const AppSnapshot& snapshot, const SystemConfig& config);
```

`sendCalibrationCenterFlowCard()` must show:

```cpp
"当前计量参数"
"启动脉冲"
"启动水量"
"启动时长"
"稳态 P/L"
"稳态流速"
"进入流量计校准"
"手工修改参数"
"历史参数"
```

`sendCalibrationCenterTemperatureCard()` must show sensor enabled/calibrated state, current water temperature, and current offset, with only:

```html
<a class='btn-link primary' href='/faucet/calibration?view=temperature'>进入温度校准</a>
```

`sendCalibrationCenterTdsCard()` must show sensor enabled/calibrated state, current TDS, voltage, temperature compensation, `scale`, and `offset`, with only:

```html
<a class='btn-link primary' href='/faucet/calibration?view=tds'>进入水质校准</a>
```

- [ ] **Step 4: Run center-page test**

Run:

```bash
pio test -e native -f test_faucet_web_handler
```

Expected: Web handler tests pass for the center page.

- [ ] **Step 5: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: make calibration center a status overview"
```

## Task 5: Temperature Calibration Detail View

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing page-view test**

In `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`, add:

```cpp
void test_temperature_calibration_detail_uses_page_state() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("view", "temperature");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("温度校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前偏移"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("开始校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("温度计读数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("预览偏移"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("0.01C"));
}
```

Register both tests.

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f test_faucet_web_handler
```

Expected: handler test fails because the existing calibration page does not switch to the temperature detail view.

- [ ] **Step 3: Add calibration view switch**

In `handleCalibrationPage()` in `src/web/FaucetWeb.cpp`, before rendering the center overview, read `view`. If `view=temperature`, call `handleTemperatureCalibrationPage()` and return. If `view=tds`, call `handleTdsCalibrationPage()` and return. Any other value falls back to the center overview.

- [ ] **Step 4: Implement temperature detail page**

Add:

```cpp
void handleTemperatureCalibrationPage() {
    if (!Esp32BaseWeb::checkAuth() || !requireContext()) {
        return;
    }
    const AppSnapshot snapshot = g_context.app->snapshot();
    Esp32BaseWeb::sendHeader("温度校准");
    Esp32BaseWeb::sendChunk("<h2>温度校准</h2><p><a class='btn-link' href='/faucet/calibration'>返回校准中心</a></p>");
    sendNoticeFromQuery();
    Esp32BaseWeb::sendChunk("<section class='panel temperature-calibration-panel'>");
    Esp32BaseWeb::sendChunk("<div class='panel-head'><h3>当前温度状态</h3></div>");
    // Render enabled state, current raw temperature, calibrated temperature, and current offset.
    Esp32BaseWeb::sendChunk("<button class='btn-link primary' type='button' onclick='document.body.classList.add(\"temperature-calibrating\")'>开始校准</button>");
    Esp32BaseWeb::sendChunk("<form class='temperature-calibration-form' method='post' action='/faucet/calibration' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("<input type='hidden' name='action' value='temperature_save'>");
    Esp32BaseWeb::sendChunk("<label class='compact-field'><span>温度计读数</span><span class='estimator-input-row'>"
                            "<input name='referenceC' type='number' step='0.1' min='0' max='90' required>"
                            "<span class='unit-label'>°C</span></span></label>");
    Esp32BaseWeb::sendChunk("<p class='hint'>页面预览只作辅助，保存时以后端当前原始温度重新计算偏移。</p>");
    Esp32BaseWeb::sendChunk("<div class='form-actions'><input class='primary' type='submit' value='保存温度校准'>"
                            "<a class='btn-link' href='/faucet/calibration'>取消</a></div></form></section>");
    sendCalibrationPageScript();
    sendPageEnd();
}
```

Use existing `readTemperatureCalibrationInput()` and `temperature_save` POST handling; it already accepts `referenceC`.

- [ ] **Step 5: Redirect temperature saves back to the detail view**

In the `temperature_save` branch of `handleCalibrationPost()`, replace the generic calibration redirect with a temperature-specific redirect:

```cpp
void redirectTemperatureCalibrationResult(const char* saved) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=temperature&saved=%s", saved ? saved : "temperature");
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTemperatureCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=temperature&error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}
```

Use these helpers only for temperature calibration POST outcomes.

- [ ] **Step 6: Run tests**

Run:

```bash
pio test -e native -f test_faucet_web_routes
pio test -e native -f test_faucet_web_handler
```

Expected: route count remains 24 and handler tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: add temperature calibration detail view"
```

## Task 6: TDS Calibration Detail View

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing page-view tests**

Add handler tests:

```cpp
void test_tds_calibration_detail_uses_point_model() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("view", "tds");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("水质校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前水质状态"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("开始校准"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("已保存校准点"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("使用这组参数"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("单点校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("两点校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("低值校准"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("高值校准"));
}

void test_tds_old_split_actions_are_rejected() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/faucet/calibration");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);
    Esp32BaseWeb::nativeTestSetParam("action", "tds_start_low");
    Esp32BaseWeb::nativeTestSetParam("referencePpm", "10");

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration", Esp32BaseWeb::METHOD_POST));
    TEST_ASSERT_EQUAL(303, Esp32BaseWeb::nativeTestResponse().code);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          Esp32BaseWeb::nativeTestResponse().redirectLocation.find("invalid_action"));
}
```

- [ ] **Step 2: Run failing tests**

Run:

```bash
pio test -e native -f test_faucet_web_routes
pio test -e native -f test_faucet_web_handler
```

Expected: page test fails because the current TDS panel still exposes old single/two-point concepts. Route-count test remains unchanged at 24.

- [ ] **Step 3: Add page handler**

In `src/web/FaucetWeb.cpp`, add:

```cpp
void handleTdsCalibrationPage();
```

It is called by the `view=tds` branch added in Task 5. Do not register a new route.

- [ ] **Step 4: Render TDS point session**

Implement helpers:

```cpp
void sendTdsCurrentStatusPanel(const AppSnapshot& snapshot, const SystemConfig& config);
void sendTdsCalibrationPointSessionPanel(const TdsCalibrationSessionSnapshot& session, bool canWrite);
void sendTdsCalibrationPointsTable(const TdsCalibrationSessionSnapshot& session);
void sendTdsCalibrationCandidatePanel(const TdsCalibrationSessionSnapshot& session);
```

The page must render these labels:

```cpp
"当前水质状态"
"本次校准"
"本次参考 ppm"
"保存为校准点"
"已保存校准点"
"自动生成结果"
"使用这组参数"
```

The page must not render:

```cpp
"单点校准"
"两点校准"
"低值校准"
"高值校准"
```

- [ ] **Step 5: Replace POST actions**

In `handleCalibrationPost()`, remove or reject the old actions:

```cpp
if (std::strcmp(text, "tds_start_low") == 0 ||
    std::strcmp(text, "tds_start_high") == 0 ||
    std::strcmp(text, "tds_start_single") == 0 ||
    std::strcmp(text, "tds_save") == 0 ||
    std::strcmp(text, "tds_cancel") == 0) {
    redirectTdsCalibrationFailure("invalid_action");
    return;
}
```

Add new actions:

```cpp
if (std::strcmp(text, "tds_start_session") == 0) {
    redirectTdsCalibrationResult(g_context.app &&
                                     g_context.app->startTdsCalibrationSessionForWeb(
                                         g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                 "tds_started",
                                 "busy");
    return;
}

if (std::strcmp(text, "tds_start_point") == 0) {
    std::uint16_t referencePpm = 0;
    if (!readTdsCalibrationInput(referencePpm, true)) {
        redirectTdsCalibrationFailure("invalid_value");
        return;
    }
    redirectTdsCalibrationResult(g_context.app &&
                                     g_context.app->startTdsCalibrationPointForWeb(
                                         referencePpm,
                                         g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                 "tds_point_started",
                                 "busy");
    return;
}

if (std::strcmp(text, "tds_save_point") == 0) {
    redirectTdsCalibrationResult(g_context.app &&
                                     g_context.app->saveTdsCalibrationPointForWeb(
                                         g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                 "tds_point_saved",
                                 "invalid_state");
    return;
}

if (std::strcmp(text, "tds_remove_point") == 0) {
    std::uint32_t index = 0;
    if (!getParam("index", text, sizeof(text)) || !parseU32(text, index) || index >= kTdsCalibrationMaxPoints) {
        redirectTdsCalibrationFailure("invalid_value");
        return;
    }
    redirectTdsCalibrationResult(g_context.app &&
                                     g_context.app->removeTdsCalibrationPointForWeb(
                                         static_cast<std::uint8_t>(index),
                                         g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                 "tds_point_removed",
                                 "invalid_state");
    return;
}

if (std::strcmp(text, "tds_discard_session") == 0) {
    redirectTdsCalibrationResult(g_context.app && g_context.app->discardTdsCalibrationForWeb(),
                                 "tds_discarded",
                                 "invalid_state");
    return;
}

if (std::strcmp(text, "tds_apply_session") == 0) {
    persistTdsCalibrationResult(g_context.app &&
                                    g_context.app->applyTdsCalibrationForWeb(
                                        g_context.nowSeconds ? g_context.nowSeconds() : 0),
                                "tds_saved",
                                "invalid_state");
    return;
}
```

Add these helpers near the existing calibration redirect helpers:

```cpp
void redirectTdsCalibrationResult(bool ok, const char* success, const char* failure) {
    char url[96]{};
    if (ok) {
        std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&saved=%s", success ? success : "tds_saved");
    } else {
        std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&error=%s", failure ? failure : "save_failed");
    }
    Esp32BaseWeb::redirectSeeOther(url);
}

void redirectTdsCalibrationFailure(const char* error) {
    char url[96]{};
    std::snprintf(url, sizeof(url), "/faucet/calibration?view=tds&error=%s", error ? error : "save_failed");
    Esp32BaseWeb::redirectSeeOther(url);
}
```

Update `persistTdsCalibrationResult()` so it calls `redirectTdsCalibrationFailure()` and `redirectTdsCalibrationResult()` instead of the generic calibration-center redirect helpers:

```cpp
void persistTdsCalibrationResult(bool ok, const char* success, const char* failure) {
    if (!ok || !g_context.app || !g_context.config || !g_context.configStore) {
        redirectTdsCalibrationFailure(failure);
        return;
    }
    const SystemConfig updated = g_context.app->config();
    if (!g_context.configStore->saveSystemConfig(updated)) {
        redirectTdsCalibrationFailure("save_failed");
        return;
    }
    *g_context.config = updated;
    if (g_context.applySettings) {
        g_context.applySettings(*g_context.config);
    }
    redirectTdsCalibrationResult(true, success, failure);
}
```

- [ ] **Step 6: Remove old TDS public methods**

After the Web handler no longer calls them, remove these old methods from `include/app/AppController.h`, `src/app/AppController.cpp`, `include/app/WaterSensorManager.h`, and `src/app/WaterSensorManager.cpp`:

```cpp
startTdsSinglePointCalibrationForWeb
startTdsTwoPointLowCalibrationForWeb
startTdsTwoPointHighCalibrationForWeb
cancelTdsCalibrationForWeb
saveTdsCalibrationForWeb
startTdsSinglePointCalibration
startTdsTwoPointLow
startTdsTwoPointHigh
cancelTdsCalibration
saveReadyTdsCalibration
```

Update existing native tests that still call those methods to use the new point-session APIs from Tasks 2 and 3.

- [ ] **Step 7: Run Web and TDS tests**

Run:

```bash
pio test -e native -f test_faucet_web_routes
pio test -e native -f test_faucet_web_handler
pio test -e native -f test_water_sensor_manager
pio test -e native -f test_app_controller
```

Expected: route, handler, sensor-manager, and app-controller tests pass with no old TDS calibration public methods remaining.

- [ ] **Step 8: Commit**

```bash
git add include/app/AppController.h src/app/AppController.cpp include/app/WaterSensorManager.h src/app/WaterSensorManager.cpp src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp test/native/test_water_sensor_manager/test_water_sensor_manager.cpp test/native/test_app_controller/test_app_controller.cpp
git commit -m "feat: add tds calibration point detail view"
```

## Task 7: Flow Calibration Page Alignment

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_handler/test_faucet_web_handler.cpp`

- [ ] **Step 1: Write failing flow-page wording tests**

Add:

```cpp
void test_flow_calibration_page_keeps_current_history_model() {
    WebFixture fixture;
    registerRoutes();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_GET, "/faucet/calibration/flow");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    TEST_ASSERT_TRUE(Esp32BaseWeb::nativeTestDispatch("/faucet/calibration/flow", Esp32BaseWeb::METHOD_GET));

    const std::string& body = Esp32BaseWeb::nativeTestResponse().body;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("当前计量参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("本次校准样本"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("历史参数"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, body.find("手工输入参数"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("生成推荐方案"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("计量方案列表"));
    TEST_ASSERT_EQUAL(std::string::npos, body.find("删除方案"));
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
pio test -e native -f test_faucet_web_handler
```

Expected: fails if any old primary scheme/generation wording remains.

- [ ] **Step 3: Fix user-facing wording**

In flow calibration rendering:

```cpp
"计量方案列表" -> "历史参数"
"新建计量方案" -> "手工输入参数"
"应用新方案" -> "使用这组参数"
"生成推荐方案" -> remove from current-session UI
"删除方案" -> remove from ordinary flow page
```

Keep advanced sample library available as diagnostics only and keep direct remote water-control routes absent.

- [ ] **Step 4: Run Web handler tests**

Run:

```bash
pio test -e native -f test_faucet_web_handler
```

Expected: Web handler tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/web/FaucetWeb.cpp test/native/test_faucet_web_handler/test_faucet_web_handler.cpp
git commit -m "feat: align flow calibration page language"
```

## Task 8: Documentation and Verification

**Files:**
- Modify: `docs/01-product-requirements.md`
- Modify: `docs/03-software-architecture.md`
- Modify: `docs/06-implementation-plan.md`
- Modify: `docs/10-flow-meter-metering-schemes.md`
- Modify: `docs/脉冲分段计量参数说明.md`
- Test: full native suite and ESP32 build

- [ ] **Step 1: Update product and architecture docs**

In `docs/01-product-requirements.md`, ensure the calibration section says:

```markdown
- 校准中心只展示当前状态和入口；实际校准在流量计、温度、水质详情页完成。
- 水质校准使用校准点模型：1 个点可用，2 个点推荐，最多 5 个点；单点、两点、多点只是结果说明，不作为入口选择。
```

In `docs/03-software-architecture.md`, add:

```markdown
- `WaterSensorManager` 维护运行态 TDS 校准点会话，未确认结果不影响当前 TDS 显示和记录；设备重启后丢弃未确认会话。
```

- [ ] **Step 2: Update implementation and metering docs**

In `docs/06-implementation-plan.md`, add a completed or current work item:

```markdown
- 校准页面重设计：校准中心状态总览、流量计当前/历史参数、温度详情页、水质校准点模型。
```

In `docs/10-flow-meter-metering-schemes.md`, keep:

```markdown
普通 Web 页面不再使用“计量方案列表”作为主概念。用户看到的是当前计量参数和历史参数。
```

In `docs/脉冲分段计量参数说明.md`, keep sample-library wording limited to diagnostics and auxiliary calculation.

- [ ] **Step 3: Run full native test suite**

Run:

```bash
pio test -e native
```

Expected: all native tests pass.

- [ ] **Step 4: Run ESP32 build**

Run:

```bash
pio run -e esp32dev
```

Expected: build exits 0. Record Flash/RAM usage in the final handoff.

- [ ] **Step 5: Check Web remote-control boundary**

Run:

```bash
rg -n "/api/faucet/(water|start|stop)|/api/faucet/(pause|resume)|action=['\"](start|stop|pause|resume)" src include test/native -S
```

Expected: no Web route or form action that starts, stops, pauses, or resumes water. Display text or C++ local controller functions are acceptable when they are not Web-exposed actions.

- [ ] **Step 6: Check Web route capacity**

Run:

```bash
pio test -e native -f test_faucet_web_routes
```

Expected: `faucetWebRouteCount()` remains 24 and `faucetWebRoutesFitEsp32Base()` passes.

- [ ] **Step 7: Commit docs and verification**

```bash
git add docs/01-product-requirements.md docs/03-software-architecture.md docs/06-implementation-plan.md docs/10-flow-meter-metering-schemes.md docs/脉冲分段计量参数说明.md
git commit -m "docs: align calibration redesign documentation"
```

## Self-Review Checklist

- Spec coverage:
  - Calibration center status overview: Task 4.
  - Flow-meter current/history parameter model: Task 7.
  - Temperature detail page with page-state start: Task 5.
  - TDS calibration point model, 1-5 points: Tasks 1, 2, 3, 6.
  - TDS hard thresholds and bounds: Tasks 1 and 2.
  - Route table remains at 24 routes: Tasks 5, 6, 8.
  - Busy gating and no remote water control: Tasks 3, 6, 8.
  - Documentation and full verification: Task 8.
- Placeholder scan:
  - No unresolved placeholders or deferred implementation notes are allowed.
  - Every task has exact files, test commands, expected results, and commit commands.
- Type consistency:
  - `TdsCalibrationPointInput` and `TdsCalibrationFitResult` are introduced in Task 1 and used by `WaterSensorManager` in Task 2.
  - `TdsCalibrationMode::MultiPoint` is introduced before the manager stores or reports it.
  - `startTdsCalibrationSessionForWeb()`, `startTdsCalibrationPointForWeb()`, `saveTdsCalibrationPointForWeb()`, `removeTdsCalibrationPointForWeb()`, `discardTdsCalibrationForWeb()`, and `applyTdsCalibrationForWeb()` are introduced before Web handlers call them.
  - Old `tds_start_low`, `tds_start_high`, `tds_start_single`, `tds_save`, and `tds_cancel` actions are rejected after the point-model actions exist.
