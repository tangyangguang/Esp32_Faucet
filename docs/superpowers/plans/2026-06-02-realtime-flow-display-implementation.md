# Realtime Flow Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the homepage flow card's single-pulse interval value with a professional realtime flow estimate based on a short pulse window and display smoothing.

**Architecture:** `FlowMeter` owns pulse-derived flow estimates and exposes diagnostic instant flow, window realtime flow, and display flow in `FlowSnapshot`. `AppController` passes display flow to user-facing status and window flow to high-flow safety. Web JSON and homepage copy expose the separate values clearly.

**Tech Stack:** PlatformIO native C++ tests, Arduino/ESP32 firmware, lightweight generated HTML/JS in `FaucetWeb.cpp`.

---

## File Structure

- `docs/11-realtime-flow-display.md`: project-level realtime flow display rules.
- `docs/03-software-architecture.md`: link the new project document from architecture.
- `include/app/FlowMeter.h`: add flow fields and fixed-size recent pulse ring state.
- `src/app/FlowMeter.cpp`: implement instant/window/display flow estimation.
- `include/app/AppController.h`: add API snapshot fields for window, instant and run-average flow.
- `src/app/AppController.cpp`: map `FlowSnapshot` fields and use window flow for high-flow safety.
- `src/web/FaucetWebJson.cpp`: serialize the new status fields.
- `src/web/FaucetWeb.cpp`: show display flow as main value and run average as flow card meta.
- `test/native/test_flow_meter/test_flow_meter.cpp`: TDD tests for window, instant, smoothing, reset and wrap behavior.
- `test/native/test_app_controller/test_app_controller.cpp`: TDD tests for high-flow safety using window flow and snapshot flow fields.
- `test/native/test_faucet_web_json/test_faucet_web_json.cpp`: JSON field tests.
- `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`: generated page string tests.

## Tasks

### Task 1: FlowMeter flow estimates

- [ ] Add failing `FlowMeter` tests for `instantFlowMlPerMin`, `windowFlowMlPerMin`, `displayFlowMlPerMin`, low-flow 3s fallback, filtered pulse exclusion, expiry, reset and `micros()` wrap.
- [ ] Run `pio test -e native -f test_flow_meter` and verify the new tests fail because the fields/behavior are missing.
- [ ] Update `FlowSnapshot` and `FlowMeter` to keep a bounded recent valid-pulse timestamp ring, compute instant flow from the last interval, compute window flow from recent pulses, and compute display flow with EMA.
- [ ] Re-run `pio test -e native -f test_flow_meter` and verify the flow-meter tests pass.

### Task 2: AppController safety and snapshot mapping

- [ ] Add failing app-controller tests proving user-facing `currentFlowMlPerMin` uses display flow and high-flow safety waits on `windowFlowMlPerMin`, not the single-pulse instant flow.
- [ ] Run `pio test -e native -f test_app_controller` and verify the new tests fail before implementation.
- [ ] Update `AppSnapshot` and `AppController::tick()` to store display, window, instant and run-average flow; pass window flow to `WaterController::tick()`.
- [ ] Re-run `pio test -e native -f test_app_controller` and verify the app-controller tests pass.

### Task 3: Web API and homepage display

- [ ] Add failing JSON tests for `windowFlowMlPerMin`, `instantFlowMlPerMin`, and `runAverageFlowMlPerMin`.
- [ ] Add failing source tests requiring homepage flow meta to use “本次平均” and no longer use “近期平均” in the flow card.
- [ ] Run `pio test -e native -f test_faucet_web_json -f test_faucet_web_routes` and verify the new tests fail before implementation.
- [ ] Update `FaucetWebJson.cpp` and `FaucetWeb.cpp` to serialize and display the separate flow values.
- [ ] Re-run `pio test -e native -f test_faucet_web_json -f test_faucet_web_routes` and verify the Web tests pass.

### Task 4: Full verification

- [ ] Run `pio test -e native`.
- [ ] Run `pio run -e esp32dev`.
- [ ] Review `git diff` to ensure only realtime-flow and project-doc changes are included, except for pre-existing user edits that remain unstaged or are explicitly reported.
