# Segmented Calibration Samples Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make latest-record capacity calibration automatically feed the segmented calibration sample library and show useful segmented metering parameters on the records page.

**Architecture:** Keep the existing saved pulse trace v2 file format and use its `actualMl` field as the persistent sample marker. RAM pulse traces remain the capture buffer; saved pulse traces become the persistent sample library used for automatic fitting. The record calibration API stores the user-entered actual volume, syncs the matching trace into the sample library, and recomputes segmented diagnostic parameters without changing the active shutoff control coefficient.

**Tech Stack:** PlatformIO native C++17 tests, ESP32 Arduino firmware, existing `WaterPulseTraceStore`, `WaterPulseTraceFileStore`, and `FaucetWeb` rendering.

---

### Task 1: Pulse Trace Sample Library Interfaces

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [x] Add failing tests proving RAM traces and saved traces can update `actualMl` by record identity.
- [x] Add failing test proving saved traces can be listed for sample-library fitting without reading every page record.
- [x] Implement `setActualMlByRecord` for RAM and saved stores, plus `list`.
- [x] Run `pio test -e native -f native/test_water_pulse_trace_store`.

### Task 2: Automatic Fitting From Samples

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Test: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`

- [x] Add a failing test where three noisy samples fit stable P/L from all samples, not only min and max.
- [x] Replace the two-point segmented calibration calculation with a small linear fit over valid samples.
- [x] Preserve existing output fields and add sample-count/range/error fields for diagnostics.
- [x] Run `pio test -e native -f native/test_water_pulse_trace_store`.

### Task 3: Latest Record Calibration Sync

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [x] Add source-level regression checks that record calibration calls segmented sample sync after storing actual volume.
- [x] Change unchanged-actual handling so an uncalibrated record whose actual equals firmware estimate can still become a sample.
- [x] On record calibration, update matching RAM and saved traces, auto-save the RAM trace when possible, then recompute segmented diagnostic config.
- [x] Run `pio test -e native -f native/test_faucet_web_routes`.

### Task 4: Records Page Diagnostics

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [x] Replace the old three metric labels with control P/L, stable P/L, startup equivalent, and sample range.
- [x] Show candidate status and useful footnote values such as suggested startup compensation and fitting error.
- [x] Replace the pulse detail form with a sample status card so the actual volume input is centralized on the latest record calibration page.
- [x] Run `pio test -e native -f native/test_faucet_web_routes`.

### Task 5: Verification

**Files:**
- No new production files.

- [x] Run `pio test -e native`.
- [x] Run `pio run -e native` and record that this project currently fails native linking because the native environment has no `main`.
- [x] Run `pio run -e esp32dev`; use `pio run -e esp32dev -t webota` only when burning/testing on the board.
