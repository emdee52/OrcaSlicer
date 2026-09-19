# PQ-2 - Slow down inner walls next to overhangs

- Source: `NEOTKOCM_RELEASE_2_45.md`.
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741`.
- Category: D (target's estimator was refactored). Status: **ported** (branch `port/PQ-2`), build clean.

## Keys (neutral names; fork keys are a mapping)
- `inner_wall_overhang_slowdown` | `coBool` | `false` | Speed, `comAdvanced`
- `inner_wall_overhang_speed_pct` | `coPercent` | `30` | Speed
- `inner_wall_overhang_reach_pct` | `coPercent` | `200` | Speed

Fork: `overhang_shadow_inner_wall`, `overhang_shadow_speed_ratio`, `overhang_shadow_distance`.

## Implementation (target 2.5)
All inserted code is tagged `[ORCAPORT:PQ-2]`.
- `src/libslic3r/GCode/ExtrusionProcessor.hpp` - `estimate_extrusion_quality` gains
  `overhang_shadow_ratio` / `overhang_shadow_reach` (default 0). In the per-point loop,
  `overhang_shadow_shift = ratio * reach`; when > 0 the point's speed is clamped with
  `calculate_speed(distance + shift)`, and the shift joins
  `artificial_distance_to_curled_lines` for the overlap (fan) computation.
- `src/libslic3r/GCode.cpp` - in the overhang-speed block, for the **inner** perimeter only
  (`is_perimeter && !is_external`) with `enable_overhang_speed` on and the toggle set:
  `ratio = speed_pct/100`, `reach = reach_pct/100 * path.width`; both are passed to the
  estimator in the curled and non-curled branches.
- `PrintConfig.cpp/.hpp`, `Preset.cpp`, `PrintObject.cpp` (invalidation -> `posPerimeters` +
  `posSupportMaterial`), `Tab.cpp` (Speed > Overhang speed), `ConfigManipulation.cpp` (the
  toggle follows "Slow down for overhang"; the two numbers follow the toggle; no LibreMode).

## Gates
- The fork's `neotko_libre_mode` conjunct was dropped. Visibility now depends only on
  "Slow down for overhang".

## Fix history
- The visibility gating in `ConfigManipulation.cpp` was inert: the three rows were
  registered with an explicit `, 0` index (`opt_id = key#0`), but `toggle_line` looks up
  the plain key, so `OptionsGroup::get_line` never matched and the rows always showed
  (main tab and object-settings dialog). Fixed by registering the scalar options without
  the index; the existing `toggle_line` calls now hide the master unless "Slow down for
  overhang" is on, and the two numbers unless the master is on.

## Verification
- Compile/link: `build_win.bat -s --no-configure -j 8` -> 0 errors, binary produced.
- Off-by-default: ratio 0 (toggle off) -> `overhang_shadow_shift` 0 -> stock path,
  byte-identical.
- Pending runtime check: slice a shallow-slope overhang model with the toggle on; the inner
  wall next to the overhang should slow (and the overhang fan should follow), with no change
  away from overhangs. The notes' own SLOWINNER-style plate is the reference.
