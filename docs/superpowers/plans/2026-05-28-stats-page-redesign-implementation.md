# Stats Page Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Implement the frozen water usage statistics page design and preserve the preview artifact for future UI iteration.

**Architecture:** Keep aggregation in `WaterRecordStore` and rendering in `FaucetWeb.cpp`. Do not change persisted records or configuration; add only derived fields to `WaterUsageSummary` and render percentages at page generation time.

**Tech Stack:** PlatformIO native tests, Unity, C++17, server-rendered HTML/SVG in `src/web/FaucetWeb.cpp`.

---

### Task 1: Persist Preview Artifact

**Files:**
- Create: `docs/ui/stats-redesign-preview.html`
- Create: `docs/ui/stats-redesign-preview.md`

- [x] **Step 1: Copy the approved preview**

Copy `.superpowers/brainstorm/88735-1779930692/content/stats-redesign-preview.html` to `docs/ui/stats-redesign-preview.html`.

- [x] **Step 2: Write the usage note**

Create `docs/ui/stats-redesign-preview.md` explaining that future UI iterations should update this preview first, then sync the firmware implementation.

### Task 2: Add Usage Count Aggregates

**Files:**
- Modify: `include/app/WaterRecordStore.h`
- Modify: `src/app/WaterRecordStore.cpp`
- Test: `test/native/test_water_record_store/test_water_record_store.cpp`

- [x] **Step 1: Write failing aggregate assertions**

In `test_record_aggregate_uses_calendar_month_and_real_daily_buckets`, assert `todayCount`, `monthCount`, `last30DaysCount`, and `totalCount`.

- [x] **Step 2: Run RED**

Run: `pio test -e native -f test_water_record_store`

Expected: compile failure because the new fields do not exist.

- [x] **Step 3: Add derived fields**

Add `std::uint32_t todayCount`, `monthCount`, `last30DaysCount`, and `totalCount` to `WaterUsageSummary`.

- [x] **Step 4: Populate fields while aggregating**

Increment `totalCount` for each real-time record, `todayCount` for today, `monthCount` for records since `monthStartDay`, and `last30DaysCount` for records in the 30-day window.

- [x] **Step 5: Run GREEN**

Run: `pio test -e native -f test_water_record_store`

Expected: all `test_water_record_store` tests pass.

### Task 3: Render Frozen Stats Page

**Files:**
- Modify: `src/web/FaucetWeb.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`

- [x] **Step 1: Write failing source checks**

Add checks for:

- `最近 30 天分布`
- `以下分布均按最近 30 天真实时间记录统计`
- `按容量段分布`
- `按完成结果分布`
- `0.5 L 以下`
- `10 L 以上`
- five Y-axis tick labels/classes
- rotated X-axis labels
- reduced-radius chart bars and distribution bars
- no old `duration-bar` chart dependency

- [x] **Step 2: Run RED**

Run: `pio test -e native -f test_faucet_web_routes`

Expected: test failure because the new source strings are missing.

- [x] **Step 3: Update styles**

Sync the stats-specific color, weight, border, radius, chart, and distribution styles from `docs/ui/stats-redesign-preview.html` into `sendAppStyles()`. Keep unrelated current Web changes intact.

- [x] **Step 4: Update stat card rendering**

Render secondary text under all four cards:

- 今日: `今日 N 次`
- 本月: `日均 N 次 · 总共 M 次`
- 过去 30 天日均: `日均 N 次 · 总共 M 次`
- 总累计: `累计 N 次`

- [x] **Step 5: Update chart rendering**

Render one 30-day volume chart with five Y-axis ticks, tilted daily date labels, per-bar volume labels, low-emphasis daily-count line labels, SVG `title` hover details, and no separate duration bar chart.

- [x] **Step 6: Update distribution rendering**

Render the "最近 30 天分布" heading and three panels: preset, capacity range, completion result. Percentages are calculated from group totals at render time.

- [x] **Step 7: Run GREEN**

Run: `pio test -e native -f test_faucet_web_routes`

Expected: all route/source tests pass.

### Task 4: Verify Relevant Native Tests

**Files:**
- Test: `test/native/test_water_record_store/test_water_record_store.cpp`
- Test: `test/native/test_faucet_web_routes/test_faucet_web_routes.cpp`
- Test: `test/native/test_faucet_web_json/test_faucet_web_json.cpp`

- [x] **Step 1: Run focused tests**

Run: `pio test -e native -f test_water_record_store -f test_faucet_web_routes -f test_faucet_web_json`

Expected: all focused tests pass.

- [x] **Step 2: Inspect final diff**

Run: `git diff --stat` and `git diff --check`.

Expected: only intended docs, aggregation, page rendering, and tests are changed; no whitespace errors.
