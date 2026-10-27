# EX-1 - Filament -> Cooling: the Exhaust fan rows edit the wrong nozzle-variant column

Status: implemented on `port/EX-1` (branched from `port/integration` `5de2656a67`), build clean,
awaiting the owner's click-through. Local fix of an **upstream** defect (Orca 2.5) - reported here
because the owner does not want to wait for upstream.

## Symptom

A filament with **Activate air filtration** + **During print** switched on in Filament -> Cooling
slices without any `M106 P3` in the G-code, so the exhaust fan never turns on from a print. Turning
the fan on by hand from the UI still works, so the machine and the firmware command are fine.

## Cause

The five exhaust/air-filtration options are **per nozzle variant** - upstream commit `81d81da08c`
("Add more granular control over exhaust fan speed during print and on completion", #13228) put them
in `filament_options_with_variant`. The Filament -> Cooling page built their rows **without a variant
index** (`append_single_option_line(key, path)` / `get_option(key)`), and a field built on the base
key always reads and writes **column 0**; only fields created with an index are re-indexed when the
variant switch changes (`Tab.cpp:9240-9250`). That is also why these rows showed no nozzle icon
while the Auxiliary part cooling fan row right above them did - `0eb6e6814e` fixed that one row
(added `, 0`) and left the exhaust block alone.

So on a printer whose nozzles resolve to a variant other than the first, the UI displayed and edited
the **first** variant's values. Concretely, on the owner's U1 (all four nozzles High Flow):

| | Direct Drive Standard | Direct Drive High Flow |
|---|---|---|
| `activate_air_filtration` | 1 | **0** |
| `during_print_exhaust_fan_speed` | 100 | **70** |
| `complete_print_exhaust_fan_speed` | 0 | **70** |

The HF tab showed `1 / 100 / 0` (the Standard column), so the setting looked on; the slice resolved
every filament to `Direct Drive High Flow` and read `0`:

```
; activate_air_filtration = 0,0,0,0
; activate_air_filtration_during_print = 1,1,1,1
; during_print_exhaust_fan_speed = 70,70,70,70
; support_air_filtration = 1
```

The emission in `GCode.cpp` is unchanged and correct - it needs `activate_air_filtration` for the
filament in use - and `GCodeWriter::set_exhaust_fan` emits `M106 P3 S<speed/100*255>`
unconditionally, no flavor check.

Not a regression from this workspace's porting: both involved commits are ancestors of
`upstream/main` (`OrcaSlicer/OrcaSlicer` @ `109d09e0b8`). It surfaced when the plate started
resolving to High Flow, which is why the setting appeared to work until then.

## Fix (one file)

`src/slic3r/GUI/Tab.cpp`:

1. `TabFilament::build()`, the Exhaust fan group: `append_single_option_line(..., ..., 0)` and
   `get_option(key, 0)` for the four paired options - the same index treatment the auxiliary fan row
   already uses.
2. `TabFilament::toggle_options()`: `toggle_line(el, support_air_filtration, 256 + variant_index)`.
3. The enable/disable block below it: read `activate_air_filtration*` at `variant_index` instead of a
   hard-coded `0`, and pass `256 + variant_index` to the `toggle_option` calls (they previously
   targeted the index-less key, so they never matched the field).

Sweep: every call site in `Tab.cpp` for a key in `filament_options_with_variant` was checked (63 keys,
29 `append_single_option_line`/`get_option` call sites); these five were the only ones built without
an index. `activate_chamber_temp_control` is **not** in that list, so its un-indexed row is correct.

## Verification

- Build: `build_win.bat -s -j 8`, clean.
- Manual protocol (the GUI field wiring is not unit-testable):
  1. Filament -> Jayo PLA Pro -> Cooling -> **Direct Drive HF** tab: **Activate air filtration must
     now show the HF values** - unchecked, During print speed 70, Complete print speed 70 (it showed
     1 / 100 / 0 before).
  2. Tick it, set During print speed 100: the preset must store `activate_air_filtration: ["1","1"]`
     (or `["1","1"]` after editing both tabs) and `during_print_exhaust_fan_speed: ["100", ...]`.
  3. Slice the plate: the G-code must contain `M106 P3 S255` and the footer
     `activate_air_filtration = 1,1,1,1`.
- The engine half was never in doubt: the owner's exported G-code already carries
  `activate_air_filtration_during_print = 1,1,1,1`, `support_air_filtration = 1`, and only the
  filtration flag is 0.

## Limitations / follow-up

- Upstream ships the same defect; this is a local fork fix, not a port, so an upstream rebase will
  conflict here until upstream fixes it. The note stays with the manifest row.
- Per-variant storage itself is upstream's design and is left alone; anything else that is per-variant
  (flow, temperatures, PA, retraction) is already indexed correctly.
- Profiles written before this fix keep their per-variant values; the owner's Jayo preset still has
  filtration on for Standard only until they tick it on the HF tab.
