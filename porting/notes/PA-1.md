# PA-1 - Local plate automation (first-layer flow and bed temperature)

Status: implemented on `port/BF-1` (stacked on BF-1/UI-3; branch from `port/integration`
`29a5f92560`), build clean, libslic3r 1582/1582, fff_print 460/460, awaiting the user's test of the
popup. Original feature - no Neotko/preFlight source.

## What it does

One **Plate automation...** button in the "Send G-code to printer host" dialog opens a popup that
edits two preferences **per plate type**:

1. **First layer flow** - the BF-1 reduction (percent on the first layer's bottom surface infill),
   now chosen per plate type instead of BF-1's hardcoded `-20 %` for the cool plates. The popup
   offers the four steps (`-20/-15/-10/-5`), `None`, and a free-form percentage (`-50..-1`).
2. **Bed temperature offset by part size** - a `degC` offset per size bucket, applied to the plate
   temperature through BT-1's existing `bed_temperature_offset`, chosen from **the largest single
   instance on the plate**.

Both are saved on this computer only (AppConfig), so a preset, a printer profile, a saved project or
a 3mf downloaded from anywhere can neither read nor reset them. A value chosen in the send dialog's
own rows still applies to that project alone.

## Where the preferences live (and why not a printer profile)

`AppConfig` `[app]` section, two strings:

```
first_layer_flow_plates     = "0:-20;2:-12"                  (BedType:percent)
first_layer_bed_temp_buckets = "0:-15,-10,-5,0,0,5;5:..."    (BedType:six offsets, bucket order)
```

- `BedType` integers keep the encoding stable; unknown types are ignored, values are clamped on read
  so a hand-edited `OrcaSlicer.conf` can never crash the dialog.
- A **configured 0** is kept in the string: it is what makes a plate type's automation override an
  older per-project value instead of falling back to it.
- A printer profile was rejected: presets are inherited, exported, compared against vendor presets
  and replaced on profile updates - a personal plate preference there travels with any profile you
  share and can be silently reset. It is also per-printer, while the need is per plate type.
- Nothing new is written into a project: the per-project keys are the ones BF-1/BT-1 already had,
  and both are excluded from `s_project_options_published`.

## How the automation meets the two dialog rows

Each row keeps its own project-level `auto` flag:

- `initial_layer_bottom_surface_flow_auto` (BF-1) - the flow row follows the local map.
- `bed_temperature_offset_auto` (new, PA-1) - the offset row follows the bucket map.

Resolution when the dialog opens (`PrintHostSendDialog::init`):

```
if (auto && the plate type has a map entry)  value = map[bed_type][bucket]  (flow: map[bed_type])
else                                         value = the project's stored value
```

- The "has an entry" test is what keeps existing projects intact: a project saved before this
  feature keeps its stored offset/reduction, because its plate type has no entry yet.
- Any click in a row sets its `auto` flag to false and stores the clicked value, so a per-project
  choice sticks and a cleared choice does not come back on reopen.
- Both flags are in `Print::invalidate_state_by_config_options`' `steps_gcode` set, so writing them
  (and the values) is a G-code re-export, never a re-slice.

## Part size buckets

Measured on the plate the dialog is uploading (`PartPlate::obj_to_instance_set`):

- footprint = **2D convex hull area** of the instance (`ModelObject::convex_hull_2d`), in mm2. The
  hull, not the bounding box: a part turned 45 degrees has up to twice the bounding box for the same
  material, which would move it a band (unit-tested: 20x20 part at 45 deg -> 400 mm2).
- height = instance bounding box Z, in mm.
- **the riskiest part decides**: the plate takes the highest bucket any of its parts lands in, because
  the bed temperature serves the whole plate. A first-layer test sharing the plate with a real part
  takes the real part's bucket, and a tall tower outvotes a large flat patch.

Warping is a **height** problem first and an area problem second - a thin part has no lever arm and no
temperature gradient across its thickness, so it never needs bed heat. The classifier is therefore a
height x footprint matrix, with footprint as a share of the plate's own area (so it travels to another
bed size):

| height \ footprint | under 3 % | 3-15 % | over 15 % |
|---|---|---|---|
| **under 3 mm** | Small | Small | Small |
| **3-20 mm** | Small | Small-medium | Small-medium |
| **20-50 mm** | Medium | Medium | Medium-large |
| **50-100 mm** | Medium-large | Large | Large |
| **100 mm and up** | Tall | Tall | Tall |

- The **under-3 mm gate** is what keeps a single-layer test or a full-bed 1 mm patch at `Small`; before
  it, the footprint alone put them in the top bucket.
- 50-100 mm with a small footprint is `Medium-large` rather than `Large`: a slender part lifts sooner,
  but a broad one has more area pulling.
- The boundaries (`kThinPartHeightMm`, `kMidPartHeightMm`, `kHighPartHeightMm`, `kTallPartHeightMm`,
  `kPartSizeAreaPctCuts`) are constants, not UI, by decision; they are one edit away if a band feels
  wrong. The bucket names (`Small ... Large, Tall`) replaced the old "Large flat": with the matrix,
  the top band is "large and at least 50 mm tall", not only flat.

## Anchors

| File | What is inserted |
|------|------------------|
| `src/libslic3r/OrcaExt/PlateAutomation.hpp` | buckets, classification, both maps + AppConfig encoding, `instance_footprint` |
| `src/libslic3r/OrcaExt/FirstLayerFlow.hpp` | the hardcoded cool-plate default is removed |
| `src/libslic3r/PrintConfig.hpp/.cpp` | `bed_temperature_offset_auto` option |
| `src/libslic3r/Print.cpp` | the new key in `steps_gcode` |
| `src/libslic3r/PresetBundle.cpp` | the new key in `s_project_options` |
| `src/slic3r/GUI/PlateAutomationStore.hpp` | load/save the two AppConfig keys |
| `src/slic3r/GUI/PlateAutomationDialog.hpp/.cpp` | the popup (tiles, flow controls, six bucket inputs, Apply) |
| `src/slic3r/GUI/PartPlate.hpp/.cpp` | `get_instance_footprints()` |
| `src/slic3r/GUI/PrintHostDialogs.hpp/.cpp` | row resolution, the **Plate automation...** button, refresh after Apply |
| `src/slic3r/GUI/Plater.cpp` | writes `bed_temperature_offset_auto` next to the offset (guarded for dialogs that never built the row) |
| `src/slic3r/GUI/OrcaMCP/OrcaMCPConfigKeys.hpp` | the new key in `project_keys` |

## Popup behaviour

- Plate tiles (the six plate pictures) are a **multi-select**; the current plate is preselected and
  supplies the values shown initially. Clicking a tile loads *that* tile's values into the controls.
- **Apply** writes the controls (flow value + all six offsets) to **every selected tile**, saves both
  AppConfig keys, and refreshes the send dialog's rows that still follow the automation; the popup
  stays open so another group can be edited. Nothing selected -> "Select at least one plate first."
- A custom flow value is validated on Apply (`-50..0`); an out-of-range or non-numeric entry is
  reported inline and nothing is written.
- Tile badges show each type's configured flow (`--` when unset).
- **Reset to auto** (next to `Plate automation...` in the send dialog) puts both rows back to the
  automatic value where the plate type is configured, and to the stock value where it is not
  (flow 0, offset 0). It also turns both `auto` flags back on, so a value picked by hand stops
  overriding the automation for this project.

The bed-temperature status line names the bucket whenever the automation picked the offset, e.g.
`Textured Cool Plate: 55°C (-10, medium-large)`; a hand-picked offset keeps the plain `45°C (-15)`,
and an automatic offset of 0 reads `(medium-large)`.

## Verification

- Build: `build_win.bat -s --tests --no-gettext -j 8`, clean.
- Unit tests `tests/libslic3r/test_orca_plate_automation.cpp` (`[OrcaPlateAutomation]`, 49
  assertions in 6 cases): every matrix cell boundary (including the under-3 mm gate at 0.2/1/2.99 mm
  and 3 mm at both footprint columns), the 100 mm tall override, a plate with no area, the
  riskiest-part rule (a full-bed patch plus a first-layer test stays Small; a tower outvotes a
  patch), the hull-vs-bbox footprint on a 45-degree part, AppConfig round-trip (including configured
  zeros), malformed/unknown/out-of-range input, and the per-type/per-bucket lookups.
- `[OrcaFirstLayerFlow]` updated: the old "cool plates default to -20" case is replaced by the map
  lookup including "configured 0 counts as configured".
- Suites: libslic3r **1583/1583**, fff_print **460/460**.
- Runtime smoke over MCP: app starts, model slices, export carries
  `; bed_temperature_offset_auto = 1` next to the existing keys, and a `-10` delta still scales only
  the first layer's bottom surface (BF-1 path untouched).
- The popup and the bed-temp row's resolved offset are visual-only: a modal wx dialog cannot be
  opened over MCP. Check by hand: open Plate automation, select two plates, set flow `-10` and a
  couple of offsets, Apply, reopen and confirm both plates kept the values; then open the send
  dialog on one of those plates and confirm both rows show them.

## Known limitations

1. **Heuristic**: the matrix is tuned by hand (thin gate 3 mm, 3/15 % footprint columns, 20/50/100 mm
   height rows). It is deliberately simple and explainable; the thresholds are constants, and the
   send dialog names the bucket it picked before Upload, so a surprising value is visible.
2. The riskiest part decides, but **plate coverage and part count are not considered**: twenty small
   parts are still classified "small" even though the whole bed is used.
3. Supports and brims are not in the footprint (they do not exist before slicing).
4. Multi-plate projects classify **the plate being uploaded**; "All plates" uses the current plate
   for both rows, as BT-1 and BF-1 already do.
5. The automation is applied when the **send dialog opens**; exporting G-code from the Prepare tab
   does not run it (same scope as BF-1's flow reduction).
6. The bed-temp row's buttons cover `+-5/10/15`; an automated offset outside that grid (e.g. `-20`)
   applies but leaves no button selected - the status line still shows the offset and the bucket.
7. `FlashforgePrintHostSendDialog` overrides `init()` and gets none of these rows (existing
   behaviour).
