# PA-1 - Local plate automation: first-layer flow per plate type

Status: implemented on `port/BF-1` (stacked on BF-1/UI-3), build clean, libslic3r and fff_print
suites green, awaiting the user's test of the popup. Original feature - no Neotko/preFlight source.

## What it does

One **Plate automation...** button in the "Send G-code to printer host" dialog opens a popup that
edits a **per-plate-type first-layer flow reduction**: multi-select the plate tiles, pick one of
`-20/-15/-10/-5 %`, `None`, or type a custom percentage (`-50..-1`), press **Apply**, and every
selected plate type gets it.

The reduction itself is BF-1's: the bottom surface flow ratio of layer 1 only, applied at G-code
export (`GCode::_extrude`, `erBottomSurface`, `path_on_first_layer`). PA-1 replaces BF-1's hardcoded
`-20 %` for the three cool plates with this local map, and adds the popup to edit it.

The value is saved **on this computer only** (AppConfig), so a preset, a printer profile, a saved
project or a 3mf downloaded from anywhere can neither read nor reset it. A value chosen in the send
dialog's own row still applies to that project alone.

## Where the preference lives (and why not a printer profile)

`AppConfig` `[app] first_layer_flow_plates = "0:-20;2:-12"` (BedType:percent).

- Keyed by the `BedType` integer so the encoding is stable; unknown types are ignored and values are
  clamped on read, so a hand-edited `OrcaSlicer.conf` can never crash the dialog.
- A **configured 0** is kept in the string: it is what makes a plate type's automation override an
  older per-project value instead of falling back to it.
- A printer profile was rejected: presets are inherited, exported, compared against vendor presets
  and replaced on profile updates - a personal plate preference there travels with any profile you
  share and can be silently reset. It is also per-printer, while the need is per plate type.

## How the automation meets the send dialog's row

The row keeps BF-1's project-level `initial_layer_bottom_surface_flow_auto` flag:

```
if (auto && the plate type has a map entry)  value = flow_default_for(map, bed_type)
else                                         value = the project's stored delta
```

- The "has an entry" test keeps existing projects intact: a project saved before this feature keeps
  its stored reduction, because its plate type has no entry yet.
- Any click in the row sets the flag false and stores the clicked value, so a per-project choice
  sticks and a cleared choice does not come back on reopen.
- **Reset to auto** (next to `Plate automation...`) turns the flag back on and restores the automatic
  value, or the stock value (`0`) where the plate type is not configured.
- The delta and the flag are in `Print::invalidate_state_by_config_options`' `steps_gcode` set, so
  writing them is a G-code re-export, never a re-slice.

## Anchors

| File | What is inserted |
|------|------------------|
| `src/libslic3r/OrcaExt/PlateAutomation.hpp` | `PlateFlowDefaults` map, lookup, AppConfig encoding (u + parse) |
| `src/slic3r/GUI/PlateAutomationStore.hpp` | load/save the `[app] first_layer_flow_plates` key |
| `src/slic3r/GUI/PlateAutomationDialog.hpp/.cpp` | the popup (plate tiles, flow steps + custom field, Apply) |
| `src/slic3r/GUI/PrintHostDialogs.hpp/.cpp` | the row's automatic resolution, the **Plate automation...** and **Reset to auto** buttons |
| `src/slic3r/GUI/PrintHostDialogs.cpp` (BF-1) | emission unchanged: `OrcaExt::first_layer_bottom_flow_scale(m_config.initial_layer_bottom_surface_flow_delta.value)` |
| `src/libslic3r/PrintConfig.hpp/.cpp`, `Print.cpp`, `PresetBundle.cpp`, `OrcaMCPConfigKeys.hpp` | BF-1's two project-level keys (unchanged by PA-1) |

## Popup behaviour

- The six plate tiles (the same pictures the plate picker uses) are a **multi-select**; the plate being
  uploaded is preselected and supplies the values shown initially. Clicking a tile loads *that* tile's
  value into the controls.
- **Apply** writes the control value to **every selected tile**, saves AppConfig, and refreshes the
  send dialog's row when it still follows the automation; the popup stays open for another group.
  Nothing selected -> "Select at least one plate first."
- A custom value is validated on Apply (`-50..0`); an out-of-range or non-numeric entry is reported
  inline and nothing is written.
- Tile badges show each type's configured value (`--` when unset).
- Things the popup does not do: there is no per-printer dimension and no per-filament one; the step
  buttons are `-20/-15/-10/-5`, `None`, and the custom field.

## Verification

- Build: `build_win.bat -s --tests --no-gettext -j 8`, clean.
- Unit tests `tests/libslic3r/test_orca_plate_automation.cpp` (`[OrcaPlateAutomation]`, 16
  assertions in 2 cases): the map round-trips through the AppConfig string (including a configured
  zero), malformed/unknown entries are dropped, values are clamped, and the per-type
  `has_flow_default` / `flow_default_for` lookups. `[OrcaFirstLayerFlow]` (19 assertions) keeps the
  scale, the platform clamp and BF-1's "delta 0 = profile flow" behaviour.
- Suites: libslic3r **1579/1579**, fff_print **460/460**.
- Runtime A/B over MCP for the emission is BF-1's (unchanged): a delta of -10 scales only layer 1's
  bottom surface E and nothing else.
- The popup is visual-only (modal wx dialog, cannot be opened over MCP): select two plates, set
  `-10 %`, Apply, reopen and confirm both kept it; then set a plate's value back to `None` and confirm
  the row shows the profile's flow.

## Known limitations

1. The popup covers the six plate types that have a picture; a plate type the printer does not have
   simply never applies.
2. The automation is applied when the **send dialog opens**; exporting G-code from the Prepare tab
   does not run it (same scope as BF-1).
3. `FlashforgePrintHostSendDialog` overrides `init()` and gets none of these rows (existing
   behaviour).
4. A value picked by hand in the row is stored in the project and survives reopening; it does not
   overwrite the local default.

## Removed: bed-temperature automation (by user decision)

PA-1 briefly also automated the bed temperature: a height x footprint size matrix (flat gate, five
footprint bands, `Small ... Tall`), per-plate-type offsets, an editable bands section, a hidden table
and a `bed_temperature_offset_auto` project key. The user does not want automatic bed temperature, so
all of it was removed in the same branch before merge: the matrix/bucket code in
`OrcaExt/PlateAutomation.hpp`, `PartPlate::get_instance_footprints()`, the `bed_temperature_offset_auto`
key (PrintConfig / Print.cpp / PresetBundle / MCP keys), the bucket name in the bed-temperature status
line, the bucket parameters in the popup and their tests. The send dialog's bed-temperature row is
back to BT-1's manual behaviour. Do not re-attempt it unless the user asks.
