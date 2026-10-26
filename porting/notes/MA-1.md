# MA-1 - Material automation: a local table of per-material bridge, top-surface and speed recipes

Status: implemented on `port/MA-1` (branched from `port/integration` `0213931f9f`), build clean,
libslic3r 1582/1582, fff_print 462/462, awaiting the user's test of the popups and the badge.
Original feature - no Neotko/preFlight source.

## What it does

One local table keyed by **filament_type** lets a material carry its own bridge and top-surface
numbers instead of a separate process profile. A plate turns on any of three recipes:

| Recipe | Fields | Consumed at |
|---|---|---|
| **Free bridging** | `bridge_density`, `bridge_flow`, `bridge_speed` | density and flow at slice, speed at export |
| **Quality Top** | `top_solid_infill_flow_ratio`, `top_surface_line_width`, `top_surface_speed` | width at slice, flow and speed at export |
| **Outer Speed** | `outer_wall_speed` | export only |
| _(no toggle)_ | first-layer bottom-surface flow (`fl`, percent delta) | export only, applies as soon as a material sets it |

The bridging recipe touches **external** bridges only: `internal_bridge_density`,
`internal_bridge_flow` and `internal_bridge_speed` are never written, and `bridge_speed` is
overridden for `erBridgeInfill` only, so overhang walls keep the profile's value (upstream couples
them through one option; the recipe deliberately does not).

Where a material's field is empty, the profile's value is used, so an empty table is a no-op and
turning a recipe off restores the profile exactly.

## Keys

Only local, plate-level state - nothing is written into a preset.

- `[app] orca_ext_material_automation` (AppConfig) - the table:
  `"PLA:bd=120;bf=1.05;bs=30;tf=0.96;tw=110%;ts=80;os=120;fl=-10|PETG:bd=95"`. Materials separated
  by `|`, fields by `;`, `key=value`; `tw` carries `%` for a percentage of the nozzle, otherwise mm.
  Parsing is tolerant (unknown fields, unknown materials and malformed numbers are dropped) and every
  number is clamped to the option's own range, so a hand-edited value cannot crash the dialog or the
  slicer.
- Plate config keys (serialized with the plate in the 3mf):
  - `material_automation_recipes` - `coInt`, bit mask of `OrcaExt::MaterialRecipe`.
  - `material_automation` - `coString`, the table resolved to the **toolpath** fields of the enabled
    recipes.
  - `material_automation_gcode` - `coString`, the table resolved to the **E/F** fields. The
    first-layer flow is always in this half, which is what "no toggle" means for it.

The plate keeps the *resolved* copy, so a project slices the same on a machine whose local table
differs, while the local table can still be re-applied to any plate.

## Why two keys, and why no re-slice for the speed/flow half

`material_automation` (bridge density, bridge flow, top-surface width) is a toolpath input, so it is
mapped in `Print::invalidate_state_by_config_options` to `posPerimeters` + `posSupportMaterial` -
perimeters, infill and supports re-run, the meshes are not re-sliced. `material_automation_gcode` is
in the `steps_gcode` bucket, so a speed/flow-only change re-runs `psGCodeExport` alone.
`material_automation_recipes` has an explicit no-op branch: the two derived keys travel with it and
are what the slicer reads, and without the branch an unknown print key would fall through to
`invalidate_all_steps()`.

## Resolution and emission

- `BackgroundSlicingProcess::apply` calls `PartPlate::resolve_material_automation()` **before** the
  plate config is merged into the full config, so a table edit, a filament swap, a project load and a
  plate switch all converge on the next apply. The write is skipped when the value is unchanged.
- `Print::refresh_material_automation()` parses the two strings and builds a per-filament pointer
  table keyed by `filament_type`; it runs at the end of `Print::apply`, at the start of
  `Print::process` and at the start of `Print::export_gcode`. The accessors are one array index, which
  matters because `PrintRegion::flow` is hot.
- Export: `GCode::_extrude` reads `m_print->material_gcode_values(active filament)` and replaces the
  role's flow/speed. `erTopSolidInfill` flow and speed, `erExternalPerimeter` speed,
  `erBridgeInfill` speed, and the first-layer bottom-surface reduction (multiplied **with** BF-1's
  plate delta, not instead of it).
- Slice: `PrintRegion::flow(frTopSolidInfill)` (width), `LayerRegion::bridging_flow()` (flow ratio -
  the same function the profile's value goes through, so spacing and width adjust exactly as they do
  for a profile), and `Fill.cpp`'s external-bridge density, resolved against the region's solid-infill
  filament.

## UI

- **Send dialog**: a "Material automation" row with the three checkboxes, a **Material automation...**
  button and a hint listing the plate's material rows (or `(none)`). On OK the mask is written to the
  plate and the values are resolved; `export_gcode` then re-slices or re-exports as needed.
- **Plate Settings** ("Customize current plate"): the same three checkboxes, so a toolpath recipe can
  be turned on, sliced and previewed before sending.
- **Table dialog**: one column per material on the plate, one row per field, empty = not set. Validates
  and applies without closing, so a group of values can be entered in one go.
- **Bed badge**: the plate name texture gains a second texture to its right reading
  `Free bridging · Quality Top · Outer Speed` for the recipes that are on (same `generate_from_text_string`
  path as BT-1's bed-temperature readout). Nothing is drawn when no recipe is on.

## Verification

- Build: `build_win.bat -s --tests -j 8`, clean.
- `tests/libslic3r/test_orca_material_automation.cpp` - 3 cases / 60 assertions: round trip, tolerant
  and bounded parsing (mm vs `%` width kept apart), and the per-recipe filtering of the two resolved
  keys (including "nothing enabled still emits the first-layer flow, nothing in the toolpath half").
- `tests/fff_print/test_orca_material_automation.cpp` - 2 cases / 9772 assertions:
  - speeds of 33/44 mm/s reach `;TYPE:Top surface` / `;TYPE:Outer wall`, the first-layer bottom surface
    E scales by 0.9 and nothing else moves, a key for a material the plate does not use changes
    nothing, and the apply invalidates export only (`posSlice` and `posInfill` still done);
  - a `tw=120%` recipe changes the region's resolved top-surface width, invalidates `posPerimeters`
    but not `posSlice`, and clearing the keys restores the baseline G-code byte for byte (E/F).
  - The fixture disables cooling and the volumetric cap, otherwise F is capped by
    `filament_max_volumetric_speed` (2 mm3/s in the defaults) and a speed assertion measures the cap
    rather than the recipe.
- Suites: libslic3r **1582/1582**, fff_print **462/462**.
- Runtime: not yet checked in the app - the modal dialogs and the bed badge cannot be driven over MCP;
  the user's test covers the rows, the table dialog and the badge.

## Known limitations / decisions

1. `bridge_flow` and `top_surface_line_width` are toolpath inputs: changing them re-runs perimeters and
   supports. That was accepted (`-50..0` percent is not needed; the table is not a live slider).
2. The table is keyed by `filament_type` only, so all PLA shares a row. A per-spool override is out of
   scope; a mixed plate still resolves each filament's own material.
3. `.gcode.3mf` printers (`use_3mf`) get the three checkboxes disabled, like BF-1's flow row: the
   bundle ships the already-exported G-code.
4. `FlashforgePrintHostSendDialog` overrides `init()` and has none of these rows.
5. While a recipe is on it wins over the profile, and the settings pages keep showing the profile's
   value - the badge and the table are the record of what is in effect. Marking the overridden fields
   in the settings pages is not done.
6. `PrintRegion::bridging_height_avg` (support interface) still reads the profile's `bridge_flow`; the
   recipe is about external bridges.
