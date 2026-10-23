# BF-1 - First layer flow (print-time bottom-surface flow reduction)

Status: implemented on `port/BF-1` (branched from `port/integration` `01f754c26b`), build + tests
clean, awaiting the user's runtime test. Original feature - no Neotko/preFlight source.

## What it does

The "Send G-code to printer host" dialog (`PrintHostSendDialog`) gains a **First layer flow** row
under BT-1's bed-temperature row: five mutually exclusive buttons `-20% -15% -10% -5% 0%` plus a
status line (`First layer flow: 1.00 -> 0.90 (-10%)`, or `... (profile)` at 0). The chosen
percentage is applied to the **bottom surface flow ratio of the first layer only**, at G-code
export. It is for plates that over-extrude layer 1 even with a correct Z offset, without keeping a
second process profile per plate.

Only `erBottomSurface` paths on the first layer are scaled. On layer 1 those are the solid-infill
beads that sit on the plate; the outer/inner walls of layer 1 are `erExternalPerimeter`/
`erPerimeter` and keep their own flow. The dialog caption stays the user's wording ("First layer
flow"); the row tooltip states the wall caveat.

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

## Config key

`initial_layer_bottom_surface_flow_delta` - `coFloat`, percent, default `0`, mode `comAdvanced`,
range `[-50, 0]` (`OrcaExt::kFirstLayerFlowDeltaMin/Max`).

- Member of the **`PrintConfig` struct** (`PrintConfig.hpp:2117`), next to `bed_temperature_offset`.
  Required: `Print::apply` diffs `m_config.keys()` (`print_config_diffs`) and only applies keys the
  struct knows.
- Registered in `PrintConfig.cpp` (`init_fff_params`, next to `bed_temperature_offset`). Not in any
  `s_Preset_*` list: it is not a process preset key and never appears in a settings tab.
- Listed in `PresetBundle::s_project_options` so a saved 3mf round-trips it; deliberately **not** in
  `s_project_options_published` (a published project must not impose the author's first-layer tweak,
  same reasoning as `curr_bed_type` / `bed_temperature_offset`). Its neutral default is `0`, so a
  reset project cannot clobber a profile's authored `bottom_solid_infill_flow_ratio` (the trap
  documented at `PresetBundle.cpp:145`).
- Added to `Print::invalidate_state_by_config_options`'s `steps_gcode` set.
- Added to the MCP `project_keys` list (`OrcaMCPConfigKeys.hpp`) so `apply_config` /
  `get_project_config` can drive it - BT-1 left its key out of that list; this one is in from the
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
| `src/slic3r/GUI/Plater.cpp` | `Plater::send_gcode_legacy` | persist the delta into `project_config` |
| `src/slic3r/GUI/OrcaMCP/OrcaMCPConfigKeys.hpp` | `project_keys` | MCP discovery/apply |

New files: `src/libslic3r/OrcaExt/FirstLayerFlow.hpp` (header-only: bounds + scale + effective
flow), `tests/libslic3r/test_orca_first_layer_flow.cpp`.

## GUI details

- The row reuses BT-1's construction: `RadioBox` per value bound to `wxEVT_LEFT_DOWN`, a body-font
  caption and a status `wxStaticText` on the following row. `0%` is an explicit button (unlike
  BT-1's toggle-to-clear) because "off" is one of the five choices.
- The reference value is `preset_bundle->full_config().opt_float("bottom_solid_infill_flow_ratio")`,
  read once on dialog open - the same cost profile as BT-1's `full_config()` call.
- Gated on the printer preset's `use_3mf` exactly like BT-1: a `.gcode.3mf` upload is bundled from
  the plate's already-exported G-code, so the buttons are disabled with a tooltip instead of
  silently doing nothing.
- `FlashforgePrintHostSendDialog::init()` overrides `PrintHostSendDialog::init()` and does not call
  it, so the row (like BT-1's) is not present for Flashforge. Elegoo and Creality call the base
  `init()` and get it.

## Verification

- Build: `build_win.bat -s --tests --no-gettext -j 8` (configure needed: new test file), clean.
- Unit tests:
  - `tests/libslic3r/test_orca_first_layer_flow.cpp` (`[OrcaFirstLayerFlow]`) - 11 assertions: scale
    table (`0 -> 1`, `-5 -> 0.95`, `-20 -> 0.8`), a non-1.0 profile ratio keeps its proportion
    (`0.95, -10 -> 0.855`), reduction never amplifies, the floor is `0.5`, and the effective flow is
    never zero.
  - `tests/fff_print/test_orca_first_layer_flow.cpp` (`[OrcaFirstLayerFlow]`) - 118 assertions:
    slicing the same cube with delta 0 and -10 yields the same number of bottom-surface moves in the
    same order; every first-layer move's E scales by 0.9 (5-decimal rounding) and every move on
    layers 2+ keeps its exact E. The invalidation case asserts `Print::apply` returns
    `APPLY_STATUS_INVALIDATED`, `psGCodeExport` is no longer done, and `posSlice`, `posPerimeters`
    and `posInfill` are still done - i.e. the change cannot re-slice.
- Suites: libslic3r **1576/1576**, fff_print **460/460**.
- **Runtime A/B over MCP** (app started with `ORCA_EXT_MCP=1`; `tests/data/test_stl/ASCII/20mmbox-LF.stl`,
  Snapmaker U1 0.4 nozzle, profile `bottom_solid_infill_flow_ratio = 1`, 100 layers):
  - slice once, `export_gcode` -> A; `apply_config {type: project, key:
    initial_layer_bottom_surface_flow_delta, value: -10}` (`applied_keys: [...]`, `invalid_keys: []`)
    -> `export_gcode` -> B.
  - Whole-file compare of A and B (8051 lines each, 3887 `G1` each): **116 differing lines**, of
    which **109 are E-only** changes, all on **layer 1**, all inside `;TYPE:Bottom surface`,
    **every one at ratio 0.9** (0.899896-0.900209, only the 5-decimal E rounding). No X/Y, no
    `;TYPE` change, no layer count change.
  - The other 7: the generation timestamp, `; initial_layer_bottom_surface_flow_delta = 0 -> -10`,
    the three `; filament used` totals (1007.69 -> 1004.39 mm, 3.08 -> 3.07 g), and one `M73`
    line moved by a single `G1` at 35% (same 107 M73 lines in both files, identical sequence and
    values - the progress marker is placed between two moves, so one move crossing the emit point
    shifts it).
  - The delta was also checked at -15 (`; initial_layer_bottom_surface_flow_delta = -15`), same
    restriction to layer 1.
- Still to eyeball in the GUI (no automated route: the dialog is a modal wx window and the harness
  cannot click it): the five buttons select exclusively, the status line reads
  `First layer flow: 1.00 -> 0.90 (-10%)` and `... (profile)` at 0, and the value survives reopening
  the dialog (project_config round-trip).

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
