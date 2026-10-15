# BT-1 - Bed temperature readout + print-time offset

Status: merged (user-verified). `b9ba044107` on `port/integration` for the feature, `ae832a04aa` for
the fix below. Original feature - no fork source.

Branch `port/BT-1`, based on `port/integration` `3d2099554d`. No Neotko/preFlight
counterpart: `bed_temperature_formula` (the "hottest filament" behaviour, Neotko
`MT-3` / `NEOTKOCM_RELEASE_2_44.md`) is already upstream and stays untouched.

## What it does

Two GUI additions around the plate temperature:

1. **Plate readout.** The first-layer plate temperature the slicer will command is
   drawn under the plate number in the 3D scene (e.g. `07` with `60°C` below it).
2. **Print-time offset.** The "Send G-code to printer host" dialog
   (`PrintHostSendDialog`) gains a row showing the selected plate type and the current
   plate temperature plus six mutually exclusive buttons (-15 / -10 / -5 / +5 / +10 /
   +15). The chosen delta is added to the plate temperature at G-code export.

## Why no post-processing script

The request was to change the bed temperature right before a print "without
re-slicing" via a post-process script. A script is unnecessary and strictly more work:

- Every bed-temperature option is already in the **G-code-export-only** invalidation
  bucket (`Print::invalidate`, `Print.cpp`):
  - `*_plate_temp_initial_layer` -> `steps_gcode`
  - `curr_bed_type`, `bed_temperature_formula` -> `steps_gcode`
  - `cool/hot/textured/textured_cool/eng/supertack_plate_temp` -> the wipe-tower group
- Changing a project-config key therefore re-runs `psGCodeExport` and nothing else: the
  slice result is preserved. Measured path: dialog OK -> `project_config` write ->
  `Plater::priv::export_gcode` -> `update_background_process` -> `Print::apply` ->
  `print_diff` contains the key -> `invalidate_state_by_config_options` -> `psGCodeExport`.

The offset is a config key rather than a G-code rewrite, so the wipe tower, the
placeholder parser and the "highest filament" formula all see it for free.

## Config key

`bed_temperature_offset` - `coInt`, default `0`, mode `comAdvanced`.

- Member of the **`PrintConfig` struct** (`PrintConfig.hpp`), next to `curr_bed_type`.
  Required: `Print::apply` diffs `m_config.keys()` (`print_config_diffs`,
  `PrintApply.cpp:237`) and only applies keys the struct knows.
- Registered in `PrintConfig.cpp` (no preset tab: it is not in `s_Preset_print_options`,
  exactly like `curr_bed_type`).
- Listed in `PresetBundle::s_project_options` so a saved 3mf round-trips it.
  Deliberately **not** in `s_project_options_published`: a published project must not
  impose the author's bed tweak (same reasoning as `curr_bed_type`).
- Added to `steps_gcode` in `Print.cpp` - without that it falls through to
  `invalidate_all_steps()` and re-slices.

## Emission

`GCode::get_bed_temperature()` is the single choke point: it feeds the first layer
(`_print_first_layer_bed_temperature`), the later layers and
`get_highest_bed_temperature()`. It now returns
`OrcaExt::apply_bed_temp_offset(base, m_config.bed_temperature_offset.value)`.

Start G-code reads the temperature through the placeholder parser, and the bundled
profiles do use it (`M190 S[bed_temperature_initial_layer]`, `[first_layer_bed_temperature]`,
`[bed_temperature_initial_layer_single]`). The two per-filament **vectors** are offset too,
otherwise every profile that emits its own `M190`/`M140` would silently ignore the dialog.
`bed_temperature_initial_layer_single` and the scalar placeholders already pick the offset
up through `get_bed_temperature()`.

`OrcaExt::apply_bed_temp_offset` leaves a base of `0` alone (0 = "this filament is not
printable on this plate") and never returns 0 for a heated base (M140 S0 turns the bed off).

## Anchors

| File | Enclosing function | What is inserted |
|------|--------------------|------------------|
| `src/libslic3r/PrintConfig.hpp` | `PrintConfig` struct list | `bed_temperature_offset` member |
| `src/libslic3r/PrintConfig.cpp` | `PrintConfigDef::init_fff_params` (next to `curr_bed_type`) | option definition + OrcaExt include |
| `src/libslic3r/Print.cpp` | `Print::invalidate_state_by_config_options` | `steps_gcode` entry |
| `src/libslic3r/GCode.cpp` | `GCode::get_bed_temperature` | offset applied at the choke point |
| `src/libslic3r/GCode.cpp` | `GCode::process` | placeholder vectors get the offset |
| `src/libslic3r/PresetBundle.cpp` | `s_project_options` | project-level key |
| `src/slic3r/GUI/PrintHostDialogs.hpp/.cpp` | `PrintHostSendDialog` | the offset row + helpers |
| `src/slic3r/GUI/PartPlate.hpp/.cpp` | `PartPlate::render_icons` / `render_only_numbers` | plate temperature label |
| `src/slic3r/GUI/Plater.cpp` | `Plater::send_gcode_legacy` | persist the offset into `project_config` |
| `src/slic3r/GUI/Plater.cpp` | `Plater::on_config_change` | invalidate the cached plate labels |

## New file

`src/libslic3r/OrcaExt/BedTempOverride.hpp` - header-only. Bounds the offset, applies it,
computes the effective first-layer temperature and converts 1-based plate filament ids to
0-based config indices. Header-only on purpose: the functions are a few lines each and a
`.cpp` would add a translation unit plus a `CMakeLists.txt` entry for no benefit.

## Plate readout details

- `PartPlate::generate_bed_temp_texture()` mirrors `generate_plate_name_texture()`; the geometry
  mirrors `calc_vertex_for_number()`, placing the label on the plate immediately left of the plate
  number on the same baseline (`bed_ext[1]` corner, factor = plate depth / 200).
- Regenerated only when invalidated, never per frame: `preset_bundle->full_config()` is
  far too expensive for a render path.
- Invalidation: `Plater::priv::update` (every model/config mutation - see the fix below),
  `Plater::on_config_change` (config edits that never reach `update()`), `PartPlate::set_bed_type`
  (per-plate bed type) and the dialog OK handler (the project-config write).
- `effective_first_layer_bed_temp` uses the plate's **used** filaments
  (`PartPlate::get_extruders()`, 1-based), so an unused filament slot with a high bed
  temperature cannot inflate the number. Empty list (nothing sliced) falls back to every
  configured filament.

## Fix (2026-10-14) - the readout ignored a per-object filament change

Reported by the user after testing: changing **the object's filament** (PLA -> PETG) left the plate
readout on the old temperature, while the print dialog showed the new one. The dialog recomputes on
open, so the two can only differ by *time* - it was a stale cached texture.

Root cause, from the code: the readout is a cached `GLTexture` invalidated only from
`Plater::on_config_change` (plus the plate bed-type setter and the dialog OK). A per-object filament
change does not go through any of them:

- every entry point (object list, part context menu, right-click filament menu in
  `GUI_Factories.cpp:1104`, the canvas number keys `GLCanvas3D.cpp:3635/4193`, `Selection.cpp:2202`)
  funnels into `ObjectList::set_extruder_for_selected_items()` (`GUI_ObjectList.cpp:6600`), which
  mutates the object/volume `extruder` config and then only calls `plater()->update()` + `Refresh()`;
- `Plater::on_config_change` is never called, because the object's `extruder` is model state, not
  the print config that function diffs;
- `Plater::on_filament_change` (`Plater.cpp:20267`) is an upstream **stub** - it computes
  `filament_type` and throws it away.

The scene *did* repaint (`plater()->update()` -> `reload_scene()` -> `set_as_dirty()`, and
`GLCanvas3D::on_idle` renders when dirty), which is exactly why the stale texture kept being drawn
instead of the plate appearing frozen.

Fix: invalidate the cached labels in `Plater::priv::update()`. That is the funnel every model/config
mutation passes through (per-object/volume filament assignment, the MMU segmentation gizmo, the
filament map dialog, selection edits, plate settings, config changes), so no entry point can be
missed again. The existing invalidation in `Plater::on_config_change` stays for config edits that do
not reach `update()`.

Second defect found while verifying: the label was drawn **below and to the right of the plate
number**, i.e. off the front-right corner of the plate and past the viewport edge in the default
"fit plate" view (and under the plate icon column). The user's camera happened to be zoomed out,
which is why they could see it at all. It is now drawn **on the plate, immediately left of the
number on the same baseline**, which is inside the default view and clear of the icon column.

## Fix verification (2026-10-14)

All of it on the running build, over MCP, with the window captured via `PrintWindow` (the app's 3D
canvas, so the readout is read from pixels) and the plate's sliced G-code as ground truth. Setup:
Elegoo CC, Textured PEI plate, per-filament `textured_plate_temp_initial_layer = 80,80,60,80`
(slots 1/2/4 PETG 80, slot 3 PLA 60); one 20 mm box, `tests/data/test_stl/ASCII/20mmbox-LF.stl`.

| step | plate G-code | plate readout (capture) |
|------|--------------|-------------------------|
| object on filament 1 (PETG) | `M190 S80` / `M140 S80` | `80°C` |
| object switched to filament 3 (PLA) | `M190 S60` / `M140 S60` | `60°C` |
| second object added on filament 1 (first stays on 3) | - (see below) | `80°C` |

- Row 2 is the reported bug: the readout followed the **per-object** filament change, which it did not
  before the fix, and matches the emitted G-code exactly.
- Row 3 checks the multi-material expectation: with filament 3 (60) and filament 1 (80) both used, the
  readout shows 80, the highest of the plate's used filaments, i.e. the `by_highest_temp` formula the
  slicer defaults to. This row has no G-code: slicing PLA + PETG on this printer is refused by the
  nozzle-temperature compatibility validation (`process_completed_with_error`), so only the readout is
  observed there; the max-over-filaments logic itself is unit-tested.
- The captures also confirm the placement fix: the readout renders on the plate, left of the plate
  number, inside the default "fit plate" view (before the fix it was off the front-right corner and
  clipped by the viewport edge). Captures:
  `build-logs/bt1fix-{before,after,multi}.png` (full window) and `.crop.png` (bottom-right zoom).

## Verification

- Build: `build_win.bat -s -j 8` clean; then `-s --tests --slicer-target libslic3r_tests`.
- Unit test: `tests/libslic3r/test_orca_bed_temp.cpp` (`[OrcaBedTemp]`) - `All tests passed
  (17 assertions in 3 test cases)`. Covers the offset clamp (including the 0-base and
  never-zero rules), the by-highest / by-first-filament formulas and the 1-based -> 0-based
  conversion.
- **Runtime A/B over MCP** (app started with `ORCA_EXT_MCP=1`; 20 mm box,
  `tests/data/test_stl/ASCII/20mmbox-LF.stl`, sliced, then exported twice):
  - `apply_config {settings:[{type:"project", key:"bed_temperature_offset", value:-10}]}`
    -> `status: success`, `applied_keys: ["bed_temperature_offset"]`, `invalid_keys: []`.
  - Offset 0 -> `M140 S65` / `M190 S65` / `; first_layer_bed_temperature = 65` /
    `; bed_temperature_offset = 0`.
  - Offset -10 -> `M140 S55` / `M190 S55` / `; first_layer_bed_temperature = 55` /
    `; bed_temperature_offset = -10`.
  - Whole-file compare: **12 changed lines**, every one of them a bed-temperature line, the
    `bed_temperature_offset` config dump, or the generation timestamp. Every toolpath line is
    byte-identical, and the profile's own start G-code (`M140 S55 ; set bed temperature`,
    a `[bed_temperature_initial_layer]`-style placeholder) moved with it - which is what the
    placeholder anchors exist for.
- Off-by-default: the default is 0, `apply_bed_temp_offset(x, 0) == x` (unit test), and the
  offset-0 run above is the untouched G-code.
- Still to eyeball in the GUI (no automated route: `render_plate_view` draws objects only, so
  it cannot see the bed labels, and there is no camera/zoom tool): the `NN°C` readout under
  the plate number, and the dialog row updating as the buttons are clicked. A PrintWindow
  capture of the running app confirmed the plate number itself renders at the plate's
  front-right corner, which is the same GL path the readout uses.

## Known limitations

1. **`.gcode.3mf` uploads cannot take the offset.** `Plater::send_gcode_legacy` builds the
   bundle from the plate's already-exported G-code (`send_gcode` -> `export_3mf` with
   `SaveStrategy::WithGcode`) *before* the export that would apply the offset, so a delta
   chosen in the dialog cannot reach the bundle. Printers whose preset sets `use_3mf`
   (Flashforge, Qidi, ...) get the controls disabled with a tooltip instead of a silent
   no-op. The plain upload path re-exports through `export_gcode` and works. Making the
   3mf path work needs a synchronous G-code re-export before the bundle is built; there is
   no such API in `BackgroundSlicingProcess` today.
2. The recommendation button (suggesting a plate temperature from the part's volume /
   footprint / count / height) is **dropped by user decision** - do not build it. The geometry
   is easy to compute from the plate's objects, but the mapping from geometry to degrees
   Celsius is a thermal judgement, and the user does not want the suggestion.
3. The offset applies to the whole print (both first-layer and later-layer bed
   temperatures), matching `bed_temperature_formula`'s whole-print behaviour. Per-layer
   offsets are not supported.
4. No deterministic test asserts that the key leaves the slice result valid. `Print::apply`
   would return `APPLY_STATUS_INVALIDATED` rather than `APPLY_STATUS_CHANGED` if a slice step
   were invalidated; a `fff_print` test could pin that, but building that suite was not worth
   the cycle here. The mechanism is a direct code fact: `bed_temperature_offset` is in
   `steps_gcode`, and `Print::invalidate_state_by_config_options` maps `steps_gcode` to
   `psGCodeExport` alone.
5. `bed_temperature_offset` is not in the MCP `project_keys` list
   (`OrcaMCPConfigKeys.hpp`), so `get_valid_config_keys`/the project dump do not list it.
   `curr_bed_type` is absent there too, and `apply_config` writes `project_config` directly,
   so nothing is blocked - only discovery is. One line to add if wanted.
