# Metering Scheme Edit Form Design

## Goal

Optimize the flow-meter metering scheme edit page at `/faucet/calibration?scheme=<id>` so it is easy to scan and edit on desktop and mobile. The page keeps the existing route and post actions, and changes only the presentation and user-facing guidance of the form.

## Context

The current page renders all fields in one compact horizontal form. On wide screens this makes the name, three core metering parameters, environment fields, and action buttons compete in a single row. On narrower screens the same structure is hard to read and the save/cancel actions are visually disconnected from the fields.

The form edits these fields:

- Scheme name.
- Startup pulse count.
- Startup volume in ml.
- Stable P/L.
- Meter label.
- Installation label.
- Condition label.
- User note.

Creating a manual scheme uses the same page style but does not show existing scheme metadata.

## Recommended Approach

Use a single-page grouped form. Keep all editable fields on one page, but organize them into clear sections:

1. Scheme identity.
2. Core metering parameters.
3. Applicability and notes.
4. Save/cancel actions.

This keeps the workflow lightweight while fixing the current visual compression.

## Page Structure

The page title remains `新建计量方案` or `修改计量方案`.

The return action remains near the top as `返回校准`.

The form body becomes an unframed section inside the existing panel:

- `方案信息`
  - Scheme name spans the full width.
  - Existing scheme pages show compact read-only metadata when useful: scheme id, revision, and current enabled status if available.

- `容量估算计量参数`
  - Startup pulse count, startup volume, and stable P/L are presented as three equal fields on desktop.
  - These fields are used for actual volume, filter usage, and statistics.

- `时间估算计量参数`
  - Startup duration and estimated stable flow are presented as numeric fields.
  - These fields are used only for estimated display, not actual volume, filter usage, or statistics.
  - Each field has a short hint for unit and valid range.
  - Values remain numeric inputs with the existing min/max validation.

- `适用条件`
  - Meter label, installation label, and condition label use a responsive grid.
  - User note spans the full width.

- `操作`
  - Primary save button and cancel link sit together at the bottom.
  - The primary label remains `保存为新方案` for creation and `保存修改` for editing.

## Current Scheme Warning

When editing the current active scheme, show a warning near the top of the form:

`当前启用方案：保存后会立即影响后续出水估算。`

This warning is informational only. It does not block saving and does not add a confirmation dialog.

## Behavior

No route, action, parameter name, or persistence behavior changes.

Existing form actions stay unchanged:

- `create_metering_scheme`
- `edit_metering_scheme`

Existing request fields stay unchanged:

- `name`
- `startupPulseCount`
- `startupVolumeMl`
- `stablePulsePerLiter`
- `meterLabel`
- `installationLabel`
- `conditionLabel`
- `userNote`

No remote water control is added.

## Styling

Add targeted CSS for the scheme edit form instead of changing global form behavior.

Desktop layout:

- Use a 12-column responsive grid for edit sections.
- Name and note fields span all columns.
- Capacity estimate metering fields each span four columns.
- Time estimate metering fields each span four columns.
- Applicability fields each span four columns.

Mobile layout:

- At narrow widths, all fields stack to one column.
- Buttons wrap cleanly and remain large enough to tap.

The page must not use nested cards. The existing panel remains the page container; section grouping uses headings, spacing, and subtle dividers.

## Testing

Native route tests should assert that the page source contains:

- Dedicated scheme edit section classes.
- The `容量估算计量参数` and `时间估算计量参数` section headings.
- The active-scheme warning string.
- Existing action names and submit labels.

Visual verification should open the edit page or a local equivalent and confirm:

- Fields no longer squeeze into one row.
- Labels and input values do not overlap.
- Save and cancel actions are visually grouped.
- The layout is usable at desktop and mobile widths.

## Out of Scope

- Changing metering formulas.
- Changing storage format or migration behavior.
- Adding new validation rules.
- Adding confirmation dialogs.
- Adding any Web action that starts, pauses, resumes, or stops water output.
