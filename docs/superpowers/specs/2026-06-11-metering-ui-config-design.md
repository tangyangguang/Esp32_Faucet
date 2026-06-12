# Metering UI And Pulse Window Config Design

## Goal

Move the calibration page pulse-window control into persistent system settings, and redesign the metering scheme page so the active scheme and scheme list are easier to scan.

## Current Problems

- The calibration page shows "前几秒脉冲总数" inside the pulse detail panel even though it is an observation preference and rarely changes.
- The value is currently a page-local input with a hard-coded default of 10 seconds, so it does not belong to the calibration workflow.
- The metering page shows only partial active-scheme information and then repeats dense nested tables in the scheme list.
- The generated-parameter panel uses wide tables for a small number of important values, making the result harder to compare with saved schemes.

## Requirements

- Add a persistent `pulseObservationWindowSec` system config field.
- Default the field to 10 seconds, clamp it to 1-60 seconds, and migrate old config versions without losing existing user config.
- Register the field in App Config under the existing "计量" group.
- Remove the pulse-window form and explanation from the calibration page pulse detail section.
- Continue using the configured window value when rendering pulse detail/sample summaries.
- On the metering page, render the active scheme in a dedicated summary card with all core fields:
  - name, id, revision, source, state, use status
  - startup pulse count, startup volume, stable P/L
  - startup duration, stable flow
  - sample count, capacity range, max fit error
- Replace the tall nested scheme table with compact rows showing identity, capacity summary, time summary, sample summary, and actions.
- Keep detailed inspection available through the existing detail modal.
- Redesign generated-parameter display around summary cards and a residual/error table instead of one very wide result table.
- Do not add any remote water-control capability.

## Approach

The backend configuration follows the existing `SystemConfig` and `ConfigStore` patterns. The new field is stored in NVS under `pulse_win_s`, loaded with defaults for old versions, saved with the current version, and surfaced through Esp32Base App Config.

The calibration page keeps the existing sample analysis functions but reads the observation window from `g_context.config`. The local form and copy are removed.

The metering page remains server-rendered HTML/CSS in `FaucetWeb.cpp` to match current project style. The active scheme becomes a standalone summary section, while the full scheme collection becomes a compact list. Existing POST routes and scheme management logic are unchanged.

## Verification

- Native tests cover default/sanitize/save-load behavior for the new config field.
- Source-level web tests cover App Config registration and removal of the calibration-page local pulse-window form.
- Source-level web tests cover the new metering page classes and active-scheme summary rendering.
- Run full native test suite and ESP32 build.
