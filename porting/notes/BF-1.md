# BF-1 - First layer flow (print-time bottom-surface flow reduction)

Status: implemented on `port/BF-1` (branched from `port/integration` `01f754c26b`), build + tests
clean, awaiting the user's runtime test. Original feature - no Neotko/preFlight source.

**Superseded in part by PA-1**: the plate-driven default is no longer hardcoded to `-20 %` for the
cool plates. The reduction is configured per plate type in the **Plate automation** popup and saved
locally in AppConfig; the row follows it while `initial_layer_bottom_surface_flow_auto` is on, and any
click makes the value the project's own. The emission, key names and no-re-slice behaviour described
below are unchanged.

## What it does

The "Send G-code to printer host" dialog (`PrintHostSendDialog`) gains a **First layer flow** row
under BT-1's bed-temperature row: four mutually exclusive buttons `-20% -15% -10% -5%` plus a status
line (`First layer flow: 1.00 -> 0.80 (-20%)`, or `... (profile)` when the reduction is off). There
is **no `0%` button**: clicking the selected value clears the reduction, exactly like the
bed-temperature offsets. On a **cool plate** (Smooth Cool Plate, Textured Cool Plate, Cool Plate
(SuperTack)) the row starts with `-20%` selected; on any other plate it starts with nothing selected.
The percentage is applied to the **bottom surface flow ratio of the first layer only**, at G-code
export. It is for plates that over-extrude layer 1 even with a correct Z offset, without keeping a
second process profile per plate.

Only `erBottomSurface` paths on the first layer are scaled. On layer 1 those are the solid-infill
beads that sit on the plate; the outer/inner walls of layer 1 are `erExternalPerimeter`/
`erPerimeter` and keep their own flow. The dialog caption stays the user's wording ("First layer
flow"); the row tooltip states the wall caveat and the cool-plate default.

### Default selection and "cleared stays cleared"

Spelled out, the row's state comes from two project-level keys:

| State | `..._auto` | `..._delta` | Row on open |
|---|---|---|---|
| never chose | `true` | `0` | cool plate: `-20%` selected, other plates: nothing |
| chose a reduction | `false` | `-5`/`-10`/`-15`/`-20` | that value |
| clicked the selected value (stock) | `false` | `0` | nothing selected |

`auto` is why the plate default and "the user asked for stock" can both exist: a single
`delta == 0` cannot tell them apart, so clearing a cool-plate choice would come back on the next
open. Any click in the row sets `auto = false`. The flag is dialog-only - the G-code generator reads
`delta` alone - so exporting from the Prepare tab never changes flow and a profile/user that never
opens the dialog is untouched (the fff_print test pins this: toggling `auto` alone moves no E value).

## Why no re-slice

Same mechanism as BT-1, and the reason the feature is a project-level key rather than a preset
edit:

- `initial_layer_bottom_surface_flow_delta` is a **print-level** key, so it is in `PrintConfig` and
  therefore in `FullPrintConfig`, which is what the G-code generator holds per region.
- It is in the `steps_gcode` set of `Print::invalidate_state_by_config_options`
  (`Print.cpp:176+`). Without that entry an unknown print key falls through to
  `invalidate_all_steps()` (`Print.cpp:536`) and the change would re-slice. With it, the diff maps to
  `invalidate_step(psGCodeExport)` alone.
- `project_config` is applied **last** in `PresetBundle::construct_full_config`
  (`PresetBundle.cpp:213`), so the project value overrides the preset and the preset is never
  dirtied or saved.

Path: dialog OK -> `Plater::priv::send_gcode_legacy` writes `project_config` (`Plater.cpp:21387`) ->
`Plater::priv::export_gcode(..., upload_job)` -> `update_background_process` -> `Print::apply` ->
`print_diff` contains the key -> `psGCodeExport` -> `background_process.start()` runs the G-code
export step only.

Note the difference from the *existing* process option: `bottom_solid_infill_flow_ratio` is a
`PrintRegionConfig` (region) key, so editing it in the process settings also only re-exports (via
`PrintObject::invalidate_state_by_config_options`, `PrintObject.cpp:1851`). The new key is
print-level because the override is per print, not per region/object.

## Config keys

`initial_layer_bottom_surface_flow_delta` - `coFloat`, percent, default `0`, mode `comAdvanced`,
range `[-50, 0]` (`OrcaExt::kFirstLayerFlowDeltaMin/Max`). This is the value the G-code generator
applies; `0` is stock flow.

`initial_layer_bottom_surface_flow_auto` - `coBool`, default `true`, mode `comAdvanced`. Dialog
state only: it says the row still follows the plate type (cool plates preselect `-20%`) and has not
been chosen or cleared yet. Nothing in the G-code path reads it.

- Both are members of the **`PrintConfig` struct** (`PrintConfig.hpp`, next to
  `bed_temperature_offset`). Required: `Print::apply` diffs `m_config.keys()`
  (`print_config_diffs`) and only applies keys the struct knows.
- Both are registered in `PrintConfig.cpp` (`init_fff_params`, next to `bed_temperature_offset`). Not
  in any `s_Preset_*` list: they are not process preset keys and never appear in a settings tab.
- Both are listed in `PresetBundle::s_project_options` so a saved 3mf round-trips them; deliberately
  **not** in `s_project_options_published` (a published project must not impose the author's
  first-layer tweak, same reasoning as `curr_bed_type` / `bed_temperature_offset`). Their neutral
  defaults (`0` / `true`) let a reset project fall back to the plate default instead of clobbering a
  profile's authored `bottom_solid_infill_flow_ratio` (the trap documented at
  `PresetBundle.cpp:145`).
- Both are in `Print::invalidate_state_by_config_options`'s `steps_gcode` set.
- Both are in the MCP `project_keys` list (`OrcaMCPConfigKeys.hpp`) so `apply_config` /
  `get_project_config` can drive them - BT-1 left its key out of that list; these are in from the
  start.

## Emission

`GCode.cpp:9372-9389` computes the effective flow per path. The bottom-surface arm becomes:

```cpp
} else if (path.role() == erBottomSurface) {
    _mm3_per_mm *= m_config.bottom_solid_infill_flow_ratio;
    if (path_on_first_layer)
        _mm3_per_mm *= OrcaExt::first_layer_bottom_flow_scale(m_config.initial_layer_bottom_surface_flow_delta.value);
}
```

`path_on_first_layer` is the flag `_extrude` already computes once per path at `GCode.cpp:9246`
(`GCode::on_first_layer(point)`), i.e. exactly the one that drives first-layer acceleration, jerk and
speed. For a flat printer it reduces to `m_layer->id() == 0 && |m_layer->bottom_z()| < EPSILON`; on a
belt printer it is the height above the belt.

Net E per mm:

```
E/mm = filament_flow_ratio x print_flow_ratio x bottom_solid_infill_flow_ratio
       x first_layer_bottom_flow_scale x geometric_mm3_per_mm / filament_area
```

It scales the deposited E only - never line spacing or width. Too large a reduction shows as gaps
between the first layer's beads, not as a thinner bead.

`first_layer_bottom_flow_scale` (`OrcaExt/FirstLayerFlow.hpp`, header-only): `0 -> 1.0`,
`-20 -> 0.8`, clamped so the multiplier never exceeds `1.0` (reduction only) and never drops below
`kFirstLayerFlowScaleMin = 0.5` (a hand-edited project cannot ask for zero flow). The dialog readout
uses the same two helpers, so it cannot disagree with the emitted value.

## Anchors

| File | Enclosing function | What is inserted |
|------|--------------------|------------------|
| `src/libslic3r/PrintConfig.hpp` | `PrintConfig` struct list | `initial_layer_bottom_surface_flow_delta` member |
| `src/libslic3r/PrintConfig.cpp` | `PrintConfigDef::init_fff_params` (next to `bed_temperature_offset`) | option definition + OrcaExt include |
| `src/libslic3r/Print.cpp` | `Print::invalidate_state_by_config_options` | `steps_gcode` entry |
| `src/libslic3r/GCode.cpp` | `GCode::_extrude` (bottom-surface flow arm) | scale applied when `path_on_first_layer` |
| `src/libslic3r/PresetBundle.cpp` | `s_project_options` | project-level key |
| `src/slic3r/GUI/PrintHostDialogs.hpp/.cpp` | `PrintHostSendDialog` | the row, its status line and helpers |
| `src/slic3r/GUI/Plater.cpp` | `Plater::send_gcode_legacy` | persist both keys into `project_config` |
| `src/slic3r/GUI/OrcaMCP/OrcaMCPConfigKeys.hpp` | `project_keys` | MCP discovery/apply |

New files: `src/libslic3r/OrcaExt/FirstLayerFlow.hpp` (header-only: bounds + scale + effective
flow), `tests/libslic3r/test_orca_first_layer_flow.cpp`.

## GUI details

- The row reuses BT-1's construction: `RadioBox` per value bound to `wxEVT_LEFT_DOWN`, a body-font
  caption and a status `wxStaticText` on the following row. Four buttons (`-20/-15/-10/-5`) and no
  `0%`: `set_first_layer_flow_delta` clears when the clicked value is the active one, matching the
  bed-temperature row.
- On open, `auto` decides what is selected: `true` -> `OrcaExt::first_layer_flow_plate_default(
  plate->get_bed_type(true))`, i.e. `-20` for `btPC` / `btPCT` / `btSuperTack` and nothing
  otherwise; `false` -> the stored delta. Any click sets `auto = false`, so a cleared choice stays
  cleared.
- The reference value is `preset_bundle->full_config().opt_float("bottom_solid_infill_flow_ratio")`,
  read once on dialog open - the same cost profile as BT-1's `full_config()` call.
- Gated on the printer preset's `use_3mf` exactly like BT-1: a `.gcode.3mf` upload is bundled from
  the plate's already-exported G-code, so the buttons are disabled with a tooltip instead of
  silently doing nothing.
- `FlashforgePrintHostSendDialog::init()` overrides `PrintHostSendDialog::init()` and does not call
  it, so the row (like BT-1's) is not present for Flashforge. Elegoo and Creality call the base
  `init()` and get it. The write-back in `send_gcode_legacy` is skipped when the row was never built
  (`has_first_layer_flow_row()`), so a Flashforge upload cannot reset a choice made from another send
  dialog back to the plate default.

## Verification

- Build: `build_win.bat -s --tests --no-gettext -j 8` (configure needed: new test file), clean.
- Unit tests:
  - `tests/libslic3r/test_orca_first_layer_flow.cpp` (`[OrcaFirstLayerFlow]`) - 20 assertions in 3
    cases: the scale table (`0 -> 1`, `-5 -> 0.95`, `-20 -> 0.8`); a non-1.0 profile ratio keeps its
    proportion (`0.95, -10 -> 0.855`); reduction never amplifies and the floor is `0.5`; and the
    plate default is `-20` for `btPC`/`btPCT`/`btSuperTack` and `0` for every other `BedType`.
  - `tests/fff_print/test_orca_first_layer_flow.cpp` (`[OrcaFirstLayerFlow]`) - 172 assertions:
    slicing the same cube with delta 0 and -10 yields the same number of bottom-surface moves in the
    same order; every first-layer move's E scales by 0.9 and every move on layers 2+ keeps its exact
    E; toggling `initial_layer_bottom_surface_flow_auto` alone moves no E value (the default bed type
    is a cool plate, so an emission-time plate default would show up here). The invalidation case
    applies both keys as the dialog does and asserts `APPLY_STATUS_INVALIDATED`, `psGCodeExport` no
    longer done, `posSlice`/`posPerimeters`/`posInfill` still done.
- Suites: libslic3r **1577/1577**, fff_print **460/460**.
- **Runtime A/B over MCP** (app started with `ORCA_EXT_MCP=1`; `tests/data/test_stl/ASCII/20mmbox-LF.stl`,
  Snapmaker U1 0.4 nozzle, Textured Cool Plate, profile `bottom_solid_infill_flow_ratio = 1`):
  - slice once, `apply_config {auto: false, delta: 0}` -> export G; `delta: -20` -> export F
    (`applied_keys` both, `invalid_keys: []`).
  - Whole-file compare (8100 lines each): **109 E-only changes, all on layer 1, all inside
    `;TYPE:Bottom surface`, every one at exactly ratio 0.8**; no coordinate, `;TYPE` or layer-count
    change. The remaining differences are the timestamp, the two config-dump lines and the filament
    totals.
  - An earlier run at -10 gave the same shape at ratio 0.9 (0.899896-0.900209, only the 5-decimal E
    rounding), and -15 was checked the same way.
- Still to eyeball in the GUI (no automated route: the dialog is a modal wx window and the harness
  cannot click it): that a cool plate opens with `-20%` selected and any other plate with nothing,
  that clicking the selected value clears it and it stays cleared on reopen, and the status line
  (`First layer flow: 1.00 -> 0.80 (-20%)` / `... (profile)`).

## Known limitations

1. **`.gcode.3mf` uploads cannot take it** - same gap as BT-1 #1, and the row is disabled for those
   printers for the same reason.
2. **Raft prints**: layer 1 sits on the raft, so `on_first_layer()` is false and the reduction does
   not apply to the object's first layer over a raft. Direct-on-plate printing is unaffected;
   `object_layer_over_raft()` would be the hook if that case is ever wanted.
3. The reduction covers **bottom surface infill only**. Layer 1's walls and any other role are
   untouched; that is deliberate (the walls are geometry the profile controls) and stated in the
   row tooltip.
4. The readout's reference value comes from the print config; a per-object/per-region
   `bottom_solid_infill_flow_ratio` override is not reflected in the `(profile)` number, though the
   scale multiplies it correctly at export.
5. `+` (amplification) is not offered: the range tops out at `0`. A plate needing more first-layer
   flow needs the process setting, not this row.
6. The plate default is applied by the **dialog**, not by the G-code generator: exporting from the
   Prepare tab on a cool plate does not get the -20%. `auto` exists so that a project which was
   exported that way is not silently changed; the reduction is a print-popup override by design.
7. An explicit choice is stored per **project**, so it follows the project rather than the plate: a
   project that has `-10` stored keeps `-10` if it is later printed on a textured PEI plate. While
   `auto` is on the value follows the plate (and a different plate resolves to nothing), which is the
   state a project is in until the row is clicked.
