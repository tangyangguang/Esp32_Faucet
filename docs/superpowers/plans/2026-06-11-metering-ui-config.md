# Metering UI And Pulse Window Config Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move pulse observation window into system config and compact the metering scheme UI.

**Architecture:** Extend `SystemConfig` and `ConfigStore` with a persisted `pulseObservationWindowSec` field. Keep rendering server-side in `FaucetWeb.cpp`, using a new active-scheme card and compact scheme list while preserving existing routes and actions.

**Tech Stack:** PlatformIO, C++17, ESP32 Arduino, Esp32Base App Config, native Unity tests.

---

## File Map

- `include/app/AppConfig.h`: add pulse observation constants and field.
- `src/app/AppConfig.cpp`: default and sanitize the new field.
- `src/app/ConfigStore.cpp`: bump config version, load/save `pulse_win_s`.
- `src/app/FaucetAppConfig.cpp`: add App Config field under "计量".
- `src/web/FaucetWeb.cpp`: remove calibration-page local pulse window form; use config value; redesign metering scheme and generated result markup/CSS.
- `test/native/test_app_config/test_app_config.cpp`: config default/sanitize/source tests.
- `test/native/test_config_store/test_config_store.cpp`: save/load and migration tests.
- `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`: source-level UI structure tests.

## Tasks

### Task 1: Persist Pulse Observation Window

- [ ] Add failing tests for default 10s, clamp 1-60s, and save/load round trip.
- [ ] Add `kDefaultPulseObservationWindowSec`, min/max constants, and `SystemConfig::pulseObservationWindowSec`.
- [ ] Initialize and sanitize the field.
- [ ] Bump config version and load/save `pulse_win_s`.
- [ ] Register App Config field named `脉冲观察窗口`.
- [ ] Run `pio test -e native -f native/test_app_config -f native/test_config_store`.

### Task 2: Move Calibration Pulse Window Control

- [ ] Add source test proving the calibration page no longer renders `前几秒脉冲总数` and does not post `samplePulseWindowSec`.
- [ ] Read the configured value in `sendCalibrationPage()` and pass it to existing pulse summary renderers.
- [ ] Remove the pulse detail form and explanatory text from the calibration page.
- [ ] Run `pio test -e native -f native/test_faucet_web_routes`.

### Task 3: Redesign Metering Scheme Page

- [ ] Add source tests for `active-metering-card`, `metering-scheme-list`, and generated result summary classes.
- [ ] Add helper rendering for full active scheme summary.
- [ ] Replace nested scheme table markup with compact list rows.
- [ ] Replace generated result wide table with summary cards plus residual table.
- [ ] Update CSS for active card, compact rows, and generated result cards.
- [ ] Run `pio test -e native -f native/test_faucet_web_routes`.

### Task 4: Verify

- [ ] Run `pio test -e native`.
- [ ] Run `pio run -e esp32dev`.
- [ ] Review `git diff` to ensure only requested files plus prior accepted storage fix are changed.
