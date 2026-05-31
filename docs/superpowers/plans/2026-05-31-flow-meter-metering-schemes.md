# Flow Meter Metering Schemes Implementation Plan

> **Status:** Superseded. Do not execute this plan. The scheme design changed after this plan was written: fixed max-10 scheme storage is no longer the product model; schemes now use可用/停用状态, used schemes cannot be physically deleted, unused non-current schemes can be deleted, and water records must save a metering snapshot. Rewrite the implementation plan after the revised design is approved.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current fixed metering-slot implementation with flow-meter metering schemes: up to 10 saved schemes, one active scheme, one generated candidate, save-as-new only, explicit enable, edit, delete non-active schemes, and legacy migration.

**Architecture:** Introduce a small scheme-domain module independent of `SystemConfig`, then persist it through a new `MeteringSchemeStore` using a separate NVS namespace. Runtime code reads only the active scheme's 3 core parameters. Web calibration actions operate on the scheme config, while ordinary system config remains responsible for safety, display, presets, filters, and valve settings.

**Tech Stack:** C++17, PlatformIO native tests, ESP32 NVS via existing `ConfigBackend`, existing lightweight Web renderer in `src/web/FaucetWeb.cpp`.

---

## File Structure

- Create `include/app/MeteringScheme.h`: scheme constants, data structures, source enum, and pure helper API.
- Create `src/app/MeteringScheme.cpp`: validation, default config, sanitize, add candidate, manual create, update, enable, delete.
- Create `include/app/MeteringSchemeStore.h`: persistent store API backed by `ConfigBackend`.
- Create `src/app/MeteringSchemeStore.cpp`: load/save/reset scheme namespace and migrate old metering keys from `faucet_cfg`.
- Modify `include/app/AppConfig.h` and `src/app/AppConfig.cpp`: remove fixed slot/candidate fields from `SystemConfig`; keep only general system config helpers.
- Modify `include/app/ConfigStore.h` and `src/app/ConfigStore.cpp`: stop loading/saving metering slots in `faucet_cfg`; leave old key reads only for migration support in `MeteringSchemeStore`.
- Modify `include/web/FaucetWeb.h`, `src/main.cpp`, and `src/web/FaucetWeb.cpp`: pass `MeteringSchemeStore` and live `MeteringSchemeConfig` into Web; persist scheme changes separately from system config.
- Modify `src/web/FaucetWebJson.cpp`: replace slot JSON with active scheme fields and candidate state.
- Modify tests under `test/native`: add focused scheme/store tests and update Web/config expectations.

---

### Task 1: Add Pure Metering Scheme Model

**Files:**
- Create: `include/app/MeteringScheme.h`
- Create: `src/app/MeteringScheme.cpp`
- Test: `test/native/test_metering_scheme/test_metering_scheme.cpp`

- [ ] **Step 1: Write failing tests for default scheme config and candidate save-as-new**

Create `test/native/test_metering_scheme/test_metering_scheme.cpp` with tests equivalent to:

```cpp
#include <unity.h>

#include "app/MeteringScheme.h"

#include <cstring>

using namespace faucet;

void test_default_scheme_config_has_one_active_default_scheme() {
    const MeteringSchemeConfig config = makeDefaultMeteringSchemeConfig();

    TEST_ASSERT_EQUAL_UINT32(1, config.activeSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, config.nextSchemeId);
    TEST_ASSERT_EQUAL_UINT8(1, meteringSchemeCount(config));
    const MeteringScheme* active = activeMeteringScheme(config);
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_UINT32(1, active->id);
    TEST_ASSERT_TRUE(active->valid);
    TEST_ASSERT_EQUAL_STRING("YF-S201 默认计量方案", active->name);
    TEST_ASSERT_EQUAL_UINT32(0, active->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, active->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(kDefaultStablePulsePerLiter, active->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(MeteringSchemeSource::Default),
                            static_cast<std::uint8_t>(active->source));
    TEST_ASSERT_FALSE(config.candidate.ready);
}

void test_candidate_saves_as_new_scheme_without_enabling() {
    MeteringSchemeConfig config = makeDefaultMeteringSchemeConfig();
    config.candidate.ready = true;
    config.candidate.params = MeteringParameters{40, 553, 222};
    config.candidate.sampleCount = 3;
    config.candidate.minActualMl = 1500;
    config.candidate.maxActualMl = 7500;
    config.candidate.maxErrorMl = 28;
    config.candidate.startupDurationMinSec = 4;
    config.candidate.startupDurationMaxSec = 6;
    config.candidate.startupDurationMedianSec = 5;
    config.candidate.startupDurationAvgSec = 5;
    std::strncpy(config.candidate.creationSummary,
                 "样本数量 3，容量范围 1500ml-7500ml，最大误差 28ml。",
                 sizeof(config.candidate.creationSummary) - 1);

    std::uint32_t newId = 0;
    TEST_ASSERT_TRUE(saveCandidateAsNewMeteringScheme(config, "低压实验", 1770000000, newId));

    TEST_ASSERT_EQUAL_UINT32(1, config.activeSchemeId);
    TEST_ASSERT_EQUAL_UINT32(2, newId);
    TEST_ASSERT_EQUAL_UINT32(3, config.nextSchemeId);
    TEST_ASSERT_FALSE(config.candidate.ready);
    const MeteringScheme* saved = findMeteringSchemeById(config, newId);
    TEST_ASSERT_NOT_NULL(saved);
    TEST_ASSERT_EQUAL_STRING("低压实验", saved->name);
    TEST_ASSERT_EQUAL_UINT32(40, saved->params.startupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(553, saved->params.startupVolumeMl);
    TEST_ASSERT_EQUAL_UINT32(222, saved->params.stablePulsePerLiter);
    TEST_ASSERT_EQUAL_UINT32(3, saved->sampleCount);
    TEST_ASSERT_EQUAL_UINT32(5, saved->startupDurationAvgSec);
    TEST_ASSERT_NOT_NULL(std::strstr(saved->creationSummary, "样本数量 3"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_default_scheme_config_has_one_active_default_scheme);
    RUN_TEST(test_candidate_saves_as_new_scheme_without_enabling);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test and verify RED**

Run: `pio test -e native -f test_metering_scheme`

Expected: build fails because `app/MeteringScheme.h` does not exist.

- [ ] **Step 3: Implement the minimal scheme model**

Create `include/app/MeteringScheme.h` with these public pieces:

```cpp
#pragma once

#include "app/AppConfig.h"

#include <cstddef>
#include <cstdint>

namespace faucet {

constexpr std::size_t kMeteringSchemeMaxCount = 10;
constexpr std::size_t kMeteringSchemeNameLength = 32;
constexpr std::size_t kMeteringSchemeSummaryLength = 192;

enum class MeteringSchemeSource : std::uint8_t {
    Default = 0,
    Generated = 1,
    Manual = 2,
    Migrated = 3,
};

struct MeteringScheme {
    std::uint32_t id;
    bool valid;
    char name[kMeteringSchemeNameLength];
    MeteringParameters params;
    MeteringSchemeSource source;
    std::uint32_t createdAt;
    std::uint32_t updatedAt;
    std::uint32_t lastActivatedAt;
    std::uint16_t sampleCount;
    std::uint32_t minActualMl;
    std::uint32_t maxActualMl;
    std::uint32_t maxErrorMl;
    std::uint32_t startupDurationMinSec;
    std::uint32_t startupDurationMaxSec;
    std::uint32_t startupDurationMedianSec;
    std::uint32_t startupDurationAvgSec;
    char creationSummary[kMeteringSchemeSummaryLength];
    char lastModifiedSummary[kMeteringSchemeSummaryLength];
};

struct MeteringSchemeCandidate {
    bool ready;
    MeteringParameters params;
    std::uint32_t generatedAt;
    std::uint16_t sampleCount;
    std::uint32_t minActualMl;
    std::uint32_t maxActualMl;
    std::uint32_t maxErrorMl;
    std::uint32_t startupDurationMinSec;
    std::uint32_t startupDurationMaxSec;
    std::uint32_t startupDurationMedianSec;
    std::uint32_t startupDurationAvgSec;
    char creationSummary[kMeteringSchemeSummaryLength];
};

struct MeteringSchemeConfig {
    std::uint32_t activeSchemeId;
    std::uint32_t nextSchemeId;
    MeteringScheme schemes[kMeteringSchemeMaxCount];
    MeteringSchemeCandidate candidate;
};

MeteringSchemeConfig makeDefaultMeteringSchemeConfig();
void sanitizeMeteringSchemeConfig(MeteringSchemeConfig& config);
std::size_t meteringSchemeCount(const MeteringSchemeConfig& config);
MeteringScheme* findMeteringSchemeById(MeteringSchemeConfig& config, std::uint32_t id);
const MeteringScheme* findMeteringSchemeById(const MeteringSchemeConfig& config, std::uint32_t id);
MeteringScheme* activeMeteringScheme(MeteringSchemeConfig& config);
const MeteringScheme* activeMeteringScheme(const MeteringSchemeConfig& config);
MeteringParameters activeMeteringSchemeParameters(const MeteringSchemeConfig& config);
bool saveCandidateAsNewMeteringScheme(MeteringSchemeConfig& config,
                                      const char* name,
                                      std::uint32_t nowSeconds,
                                      std::uint32_t& newSchemeId);
bool createManualMeteringScheme(MeteringSchemeConfig& config,
                                const char* name,
                                const MeteringParameters& params,
                                std::uint32_t nowSeconds,
                                std::uint32_t& newSchemeId);
bool updateMeteringScheme(MeteringSchemeConfig& config,
                          std::uint32_t schemeId,
                          const char* name,
                          const MeteringParameters& params,
                          std::uint32_t nowSeconds);
bool enableMeteringScheme(MeteringSchemeConfig& config, std::uint32_t schemeId, std::uint32_t nowSeconds);
bool deleteMeteringScheme(MeteringSchemeConfig& config, std::uint32_t schemeId);

}  // namespace faucet
```

Create `src/app/MeteringScheme.cpp` implementing:

- default config with one valid scheme named `YF-S201 默认计量方案`, id `1`, `nextSchemeId = 2`.
- `sanitizeMeteringSchemeConfig()` clamps strings, clears invalid candidates, keeps at least one valid default scheme, and ensures `activeSchemeId` points to a valid scheme.
- `saveCandidateAsNewMeteringScheme()` finds a free slot, copies candidate metadata, sets source `Generated`, clears candidate, does not change `activeSchemeId`.
- `createManualMeteringScheme()` creates source `Manual` with sample metadata zeroed and creation summary `手工创建。`.
- `updateMeteringScheme()` changes name and 3 core params only, preserves creation metadata, updates `updatedAt` and `lastModifiedSummary`.
- `enableMeteringScheme()` only updates `activeSchemeId` and `lastActivatedAt`.
- `deleteMeteringScheme()` rejects active scheme, rejects deleting the last valid scheme, and clears non-active schemes.

- [ ] **Step 4: Run test and verify GREEN**

Run: `pio test -e native -f test_metering_scheme`

Expected: all tests in `test_metering_scheme` pass.

---

### Task 2: Expand Scheme Operation Tests

**Files:**
- Modify: `test/native/test_metering_scheme/test_metering_scheme.cpp`
- Modify: `src/app/MeteringScheme.cpp`

- [ ] **Step 1: Add failing tests for manual create, update, enable, delete, and full capacity**

Append tests that assert:

- manual create does not auto-enable.
- update of active scheme preserves `creationSummary` and changes `lastModifiedSummary`.
- enable changes active scheme and `lastActivatedAt`.
- delete rejects active scheme and last remaining scheme.
- saving candidate fails when 10 schemes are already valid.

Use concrete assertions:

```cpp
TEST_ASSERT_FALSE(deleteMeteringScheme(config, config.activeSchemeId));
TEST_ASSERT_EQUAL_UINT32(oldActiveId, config.activeSchemeId);
TEST_ASSERT_TRUE(enableMeteringScheme(config, manualId, 1770000300));
TEST_ASSERT_EQUAL_UINT32(manualId, config.activeSchemeId);
TEST_ASSERT_EQUAL_UINT32(1770000300, activeMeteringScheme(config)->lastActivatedAt);
```

- [ ] **Step 2: Run test and verify RED**

Run: `pio test -e native -f test_metering_scheme`

Expected: at least one new behavior fails or is unimplemented.

- [ ] **Step 3: Complete scheme helpers**

Fill out any missing helper behavior in `src/app/MeteringScheme.cpp` until all operation tests pass.

- [ ] **Step 4: Run test and verify GREEN**

Run: `pio test -e native -f test_metering_scheme`

Expected: all tests in `test_metering_scheme` pass.

---

### Task 3: Add Independent MeteringSchemeStore

**Files:**
- Create: `include/app/MeteringSchemeStore.h`
- Create: `src/app/MeteringSchemeStore.cpp`
- Test: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [ ] **Step 1: Write failing persistence tests**

Create a test with a local `FakeConfigBackend` like `test/native/test_config_store/test_config_store.cpp`. Cover:

- default load when namespace has no `ver`.
- save/load round trip for active id, next id, two schemes, and candidate stats including `startupDurationAvgSec`.
- future version returns defaults and refuses save while preserving storage.
- reset clears scheme namespace and saves defaults.

Use namespace assertions against `faucet_meter`, not `faucet_cfg`:

```cpp
TEST_ASSERT_EQUAL_INT32(1, backend.getInt("faucet_meter", "ver", 0));
TEST_ASSERT_EQUAL_INT32(2, backend.getInt("faucet_meter", "active", 0));
TEST_ASSERT_EQUAL_INT32(5, backend.getInt("faucet_meter", "cand_avg_s", 0));
```

- [ ] **Step 2: Run test and verify RED**

Run: `pio test -e native -f test_metering_scheme_store`

Expected: build fails because `MeteringSchemeStore` does not exist.

- [ ] **Step 3: Implement store API**

Create `include/app/MeteringSchemeStore.h`:

```cpp
#pragma once

#include "app/ConfigStore.h"
#include "app/MeteringScheme.h"

#include <cstdint>

namespace faucet {

class MeteringSchemeStore {
public:
    enum class LoadStatus : std::uint8_t {
        DefaultsNoVersion = 0,
        LoadedCurrent = 1,
        MigratedLegacy = 2,
        LoadedFutureVersionReadOnly = 3,
        UnsupportedVersionDefault = 4,
    };

    explicit MeteringSchemeStore(ConfigBackend& backend);

    MeteringSchemeConfig load();
    bool save(const MeteringSchemeConfig& config);
    bool reset();
    LoadStatus lastLoadStatus() const;
    bool readOnly() const;
    std::int32_t lastRawVersion() const;
    std::int32_t currentVersion() const;

private:
    ConfigBackend& backend_;
    LoadStatus lastStatus_;
    bool readOnly_;
    std::int32_t lastRawVersion_;
};

}  // namespace faucet
```

Implement `src/app/MeteringSchemeStore.cpp` using namespace `faucet_meter`, current version `1`, and key prefixes:

- global: `ver`, `active`, `next`
- scheme key: `s%u_valid`, `s%u_id`, `s%u_name`, `s%u_src`, `s%u_sp`, `s%u_sv`, `s%u_pl`, `s%u_create`, `s%u_update`, `s%u_activate`, `s%u_samples`, `s%u_min_ml`, `s%u_max_ml`, `s%u_err_ml`, `s%u_min_s`, `s%u_max_s`, `s%u_med_s`, `s%u_avg_s`, `s%u_create_note`, `s%u_modify_note`
- candidate: `cand_ready`, `cand_sp`, `cand_sv`, `cand_pl`, `cand_at`, `cand_samples`, `cand_min_ml`, `cand_max_ml`, `cand_err_ml`, `cand_min_s`, `cand_max_s`, `cand_med_s`, `cand_avg_s`, `cand_note`

Use `sanitizeMeteringSchemeConfig()` before saving.

- [ ] **Step 4: Run test and verify GREEN**

Run: `pio test -e native -f test_metering_scheme_store`

Expected: all store tests pass.

---

### Task 4: Migrate Legacy Metering Keys into Scheme Store

**Files:**
- Modify: `src/app/MeteringSchemeStore.cpp`
- Modify: `test/native/test_metering_scheme_store/test_metering_scheme_store.cpp`

- [ ] **Step 1: Add failing legacy migration tests**

Add tests with old keys in `faucet_cfg`:

- v1-style keys: `pulse_m`, `seg_start_p`, `seg_start_ml`, `cand_ready`, `cand_start_p`, `cand_start_ml`, `cand_stable`, `cand_samples`, `cand_min_ml`, `cand_max_ml`, `cand_err_ml`, `cand_start_s`.
- v12-style slot keys: `active_ms`, `ms0_valid`, `ms0_name`, `ms0_sp`, `ms0_sv`, `ms0_pl`, `ms0_create`, `ms1_valid`, `ms1_name`, `ms1_sp`, `ms1_sv`, `ms1_pl`, `mc_ready`, `mc_sp`, `mc_sv`, `mc_pl`, `mc_note`, `mc_at`.

Assert:

```cpp
TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(MeteringSchemeStore::LoadStatus::MigratedLegacy),
                        static_cast<std::uint8_t>(store.lastLoadStatus()));
TEST_ASSERT_EQUAL_UINT32(1, loaded.activeSchemeId);
TEST_ASSERT_EQUAL_UINT32(620, activeMeteringSchemeParameters(loaded).stablePulsePerLiter);
TEST_ASSERT_TRUE(loaded.candidate.ready);
TEST_ASSERT_NOT_NULL(std::strstr(loaded.candidate.creationSummary, "旧生成结果"));
```

- [ ] **Step 2: Run migration tests and verify RED**

Run: `pio test -e native -f test_metering_scheme_store`

Expected: migration tests fail because legacy keys are not read yet.

- [ ] **Step 3: Implement legacy migration**

In `MeteringSchemeStore::load()`:

- If `faucet_meter/ver` is missing, inspect `faucet_cfg` for legacy metering keys.
- If no legacy keys exist, return default scheme config.
- If old single metering fields exist, create one active migrated scheme from `seg_*` or `pulse_m`.
- If fixed-slot keys exist, create one migrated scheme for the active slot and one migrated scheme for each non-default valid inactive slot.
- Convert old candidate fields into `MeteringSchemeConfig::candidate`.
- Save migrated config into `faucet_meter` only when not read-only and all writes succeed.
- Do not clear legacy keys during this task.

- [ ] **Step 4: Run migration tests and verify GREEN**

Run: `pio test -e native -f test_metering_scheme_store`

Expected: all scheme store tests pass.

---

### Task 5: Remove Metering Slots from SystemConfig Persistence

**Files:**
- Modify: `include/app/AppConfig.h`
- Modify: `src/app/AppConfig.cpp`
- Modify: `src/app/ConfigStore.cpp`
- Modify: `test/native/test_app_config/test_app_config.cpp`
- Modify: `test/native/test_config_store/test_config_store.cpp`

- [ ] **Step 1: Write failing config tests**

Update `test_app_config` to no longer assert `activeMeteringSlot`, `meteringSlots`, or `meteringCandidate` in `SystemConfig`.

Update `test_config_store`:

- `saveSystemConfig()` should no longer write `active_ms`, `ms0_*`, or `mc_*`.
- v1/v2 migration should still preserve non-metering fields.
- current config version should increment to the next version, because `SystemConfig` shape changes.

Concrete assertion:

```cpp
TEST_ASSERT_EQUAL_INT32(13, backend.getInt("faucet_cfg", "ver", 0));
TEST_ASSERT_EQUAL_INT32(-1, backend.getInt("faucet_cfg", "active_ms", -1));
```

- [ ] **Step 2: Run config tests and verify RED**

Run: `pio test -e native -f test_app_config -f test_config_store`

Expected: tests fail because `SystemConfig` still contains and saves metering slots.

- [ ] **Step 3: Remove old metering fields from system config**

In `include/app/AppConfig.h` remove:

- `kMeteringSlotCount`
- `kMeteringSlotNameLength`
- `kMeteringNoteLength`
- `SystemConfig::MeteringSlot`
- `SystemConfig::MeteringCandidate`
- `activeMeteringSlot`
- slot operation helper declarations

Keep metering parameter bounds and `validMeteringParameters()` because `FlowMeter` and schemes still use them.

In `src/app/AppConfig.cpp` remove default/sanitize/operation logic for slots and candidates.

In `src/app/ConfigStore.cpp`:

- bump `kConfigVersion` from `12` to `13`.
- remove metering slot and candidate reads from `loadCommonSystemConfig()`.
- remove `migrateLegacyMetering()`.
- remove metering writes from `saveSystemConfig()`.
- keep legacy-key detection for `pulse_m`, `seg_*`, `active_ms`, `ms*`, and `mc_*` only if still needed to classify a missing-version config as recognizable.

- [ ] **Step 4: Run config tests and verify GREEN**

Run: `pio test -e native -f test_app_config -f test_config_store`

Expected: both test suites pass.

---

### Task 6: Wire Active Scheme into Runtime

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/web/FaucetWeb.h`
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `test/native/test_app_controller/test_app_controller.cpp` if constructor/application assumptions change.

- [ ] **Step 1: Add failing runtime integration assertions**

Add or update native tests where possible so runtime uses `activeMeteringSchemeParameters(schemeConfig)` rather than `activeMeteringParameters(systemConfig)`.

At minimum, compile should fail after `SystemConfig` no longer contains metering fields until `main.cpp` and Web are rewired.

- [ ] **Step 2: Run compile/test and verify RED**

Run: `pio test -e native`

Expected: build fails at references to `activeMeteringParameters(config)`, `activeMeteringSlot`, `meteringSlots`, or `meteringCandidate`.

- [ ] **Step 3: Add global scheme config and store**

In `src/main.cpp`:

- include `app/MeteringSchemeStore.h`.
- add `faucet::MeteringSchemeConfig g_meteringSchemes{};`
- add `faucet::MeteringSchemeStore g_meteringSchemeStore(g_configBackend);`
- after loading `g_config`, load `g_meteringSchemes = g_meteringSchemeStore.load();`
- initialize and update `FlowMeter` from `activeMeteringSchemeParameters(g_meteringSchemes)`.
- pass `&g_meteringSchemes` and `&g_meteringSchemeStore` into `FaucetWebContext`.

In `include/web/FaucetWeb.h` add:

```cpp
class MeteringSchemeStore;
struct MeteringSchemeConfig;

struct FaucetWebContext {
    ...
    MeteringSchemeConfig* meteringSchemes;
    MeteringSchemeStore* meteringSchemeStore;
};
```

In Web persistence helpers, add a `persistMeteringSchemes(const MeteringSchemeConfig&)` function that:

- rejects while `!g_context.app->canApplyConfig()`.
- saves through `g_context.meteringSchemeStore`.
- applies active parameters to `FlowMeter` through the same runtime settings path used by config changes.

- [ ] **Step 4: Run native tests and verify GREEN for compile**

Run: `pio test -e native`

Expected: native tests compile; behavior tests may still fail until Web expectations are updated in later tasks.

---

### Task 7: Update Candidate Generation Metadata

**Files:**
- Modify: `include/app/WaterPulseTraceStore.h`
- Modify: `src/app/WaterPulseTraceStore.cpp`
- Modify: `test/native/test_water_pulse_trace_store/test_water_pulse_trace_store.cpp`
- Modify: `src/web/FaucetWeb.cpp`

- [ ] **Step 1: Write failing tests for startup duration average/min/max/median**

Extend `SegmentedCalibrationResult` expectations:

```cpp
TEST_ASSERT_EQUAL_UINT32(5, result.startupDurationMedianSec);
TEST_ASSERT_EQUAL_UINT32(5, result.startupDurationAvgSec);
TEST_ASSERT_EQUAL_UINT32(5, result.startupDurationMinSec);
TEST_ASSERT_EQUAL_UINT32(6, result.startupDurationMaxSec);
```

- [ ] **Step 2: Run trace tests and verify RED**

Run: `pio test -e native -f test_water_pulse_trace_store`

Expected: compile or assertion failure because fields do not exist or are not computed.

- [ ] **Step 3: Implement duration stats**

Change `SegmentedCalibrationResult`:

- replace `startupDurationSec` with `startupDurationMedianSec`.
- add `startupDurationMinSec`, `startupDurationMaxSec`, `startupDurationAvgSec`.

In `computeSegmentedCalibration()`:

- collect valid sample startup durations.
- sort a small local array up to `kMaxSegmentedCandidateSamples`.
- compute min, max, median, and rounded average.

In `src/web/FaucetWeb.cpp`, update candidate population to copy all four duration stats into `MeteringSchemeCandidate`.

- [ ] **Step 4: Run trace tests and verify GREEN**

Run: `pio test -e native -f test_water_pulse_trace_store`

Expected: all trace tests pass.

---

### Task 8: Replace Web Slot UI and Actions with Scheme UI

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Modify: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Modify: `test/native/test_faucet_web_parsing/test_faucet_web_parsing.cpp` if new parsing helpers are added.

- [ ] **Step 1: Write failing Web route tests**

Update calibration route tests to assert:

- page contains `流量计计量方案`, `当前计量方案`, `计量方案生成结果`.
- page does not contain `参数槽`, `保存到槽`, or `覆盖当前启用参数`.
- candidate form action is `save_generated_scheme`.
- manual create form action is `create_metering_scheme`.
- edit form action is `update_metering_scheme`.
- enable form action is `enable_metering_scheme`.
- delete form action is `delete_metering_scheme`.

Concrete assertions:

```cpp
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='save_generated_scheme'"));
TEST_ASSERT_NOT_NULL(std::strstr(buffer, "name='action' value='create_metering_scheme'"));
TEST_ASSERT_NULL(std::strstr(buffer, "参数槽"));
TEST_ASSERT_NULL(std::strstr(buffer, "覆盖当前启用参数"));
```

- [ ] **Step 2: Run Web tests and verify RED**

Run: `pio test -e native -f test_faucet_web_routes`

Expected: assertions fail because the page still renders fixed slots.

- [ ] **Step 3: Implement scheme panel rendering**

Replace `sendCalibrationParameterPanels()` with sections:

- current scheme summary from `activeMeteringScheme(*g_context.meteringSchemes)`.
- candidate scheme table showing core params, sample count, min/max actual ml, max error, startup duration min/max/median/avg.
- dynamic scheme table iterating valid schemes only.
- manual create form.

Use these actions:

- `save_generated_scheme` with required `name`.
- `discard_candidate_scheme`.
- `create_metering_scheme`.
- `update_metering_scheme` with `schemeId`.
- `enable_metering_scheme` with `schemeId`.
- `delete_metering_scheme` with `schemeId`.

Do not render copy or rename actions.

- [ ] **Step 4: Implement Web handlers**

Replace slot handlers with scheme handlers:

- `handleSaveCandidateSchemeApi()`: validates candidate and name, calls `saveCandidateAsNewMeteringScheme()`, persists schemes, redirects `?saved=scheme_created`.
- `handleDiscardCandidateSchemeApi()`: clears candidate and persists schemes.
- `handleCreateMeteringSchemeApi()`: parses name + 3 params, calls `createManualMeteringScheme()`, persists schemes, redirects `?saved=scheme_created`.
- `handleUpdateMeteringSchemeApi()`: parses scheme id + name + 3 params, calls `updateMeteringScheme()`, persists schemes, redirects `?saved=scheme_updated` or `?saved=active_scheme_updated`.
- `handleEnableMeteringSchemeApi()`: calls `enableMeteringScheme()`, persists schemes, redirects `?saved=scheme_enabled`.
- `handleDeleteMeteringSchemeApi()`: calls `deleteMeteringScheme()`, persists schemes, redirects `?saved=scheme_deleted`.

- [ ] **Step 5: Run Web tests and verify GREEN**

Run: `pio test -e native -f test_faucet_web_routes`

Expected: Web route tests pass.

---

### Task 9: Update JSON and Status Text

**Files:**
- Modify: `src/web/FaucetWebJson.cpp`
- Modify: `include/web/FaucetWebJson.h` if signature changes
- Modify: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`
- Modify: `src/web/FaucetWeb.cpp`

- [ ] **Step 1: Write failing JSON tests**

Update config JSON test to expect:

```cpp
TEST_ASSERT_NOT_NULL(std::strstr(json, "\"activeMeteringSchemeId\":1"));
TEST_ASSERT_NOT_NULL(std::strstr(json, "\"meteringCandidateReady\":false"));
TEST_ASSERT_NULL(std::strstr(json, "activeMeteringSlot"));
```

- [ ] **Step 2: Run JSON tests and verify RED**

Run: `pio test -e native -f test_faucet_web_json`

Expected: assertion fails because old slot JSON remains.

- [ ] **Step 3: Update JSON writer**

Change `writeConfigJson()` to accept both `SystemConfig` and `MeteringSchemeConfig`, or add `writeMeteringStatusJson()` if changing the existing endpoint is too invasive.

Preferred signature:

```cpp
bool writeConfigJson(const SystemConfig& config,
                     const MeteringSchemeConfig& schemes,
                     char* out,
                     std::size_t len);
```

Update callers and tests accordingly.

- [ ] **Step 4: Run JSON tests and verify GREEN**

Run: `pio test -e native -f test_faucet_web_json`

Expected: JSON tests pass.

---

### Task 10: Full Native and Firmware Verification

**Files:**
- Potential fixes in touched source/test files only.

- [ ] **Step 1: Run full native tests**

Run: `pio test -e native`

Expected: all native tests pass.

- [ ] **Step 2: Build ESP32 firmware**

Run: `pio run -e esp32dev`

Expected: build succeeds.

- [ ] **Step 3: OTA only if the user wants device deployment**

Run only when explicitly deploying to board: `pio run -e esp32dev -t webota`

Expected: upload succeeds or reports a device/network issue without changing source.

---

## Self-Review

- Spec coverage: the plan covers multiple saved schemes, max 10 capacity, dynamic UI, candidate save-as-new only, explicit enable, edit, delete non-current, manual create, metadata including average startup duration, independent persistence, and migration.
- Placeholder scan: no placeholder markers or open-ended implementation steps remain.
- Type consistency: model names use `MeteringSchemeConfig`, `MeteringScheme`, `MeteringSchemeCandidate`, and `MeteringSchemeStore` throughout. Web action names consistently use `*_metering_scheme` or `*_candidate_scheme`.
