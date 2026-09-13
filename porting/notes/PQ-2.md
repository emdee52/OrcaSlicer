# PQ-2 - Slow down inner walls next to overhangs

- Source: `NEOTKOCM_RELEASE_2_45.md`.
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741`.
- Category: B/D. Moderate risk; target's estimator was refactored.

## Upstream anchors (fork lines)
- `src/libslic3r/GCode.cpp` overhang-speed block (~9068)
  - compute `shadow_ratio` / `shadow_reach` (~9087-9094); pass to `estimate_extrusion_quality` (~9115, ~9136)
- `src/libslic3r/GCode/ExtrusionProcessor.hpp`
  - `estimate_extrusion_quality` gains `overhang_shadow_ratio`, `overhang_shadow_reach` (~318)
  - `overhang_shadow_shift = overhang_shadow_ratio * overhang_shadow_reach;` (~386); speed clamp (~467); fan/overlap (~472)
- `PrintConfig.cpp:1363`, `PrintConfig.hpp:1320`, `PrintObject.cpp:1464` (invalidation), `src/slic3r/GUI/Tab.cpp:7772`, `ConfigManipulation.cpp:966`

## Keys
- `overhang_shadow_inner_wall` | `coBool` | `false` | `PrintConfig.cpp:1363`
- `overhang_shadow_speed_ratio` | `coPercent` | `30` | `:1376`
- `overhang_shadow_distance` | `coPercent` | `200` | `:1390`
- All Speed / `comAdvanced`.

## Gates to remove
- `GCode.cpp:9089` - drop the `neotko_libre_mode.value &&` conjunct.
- `ConfigManipulation.cpp:966-968` - remove the `libre_active` term from `toggle_line("overhang_shadow_inner_wall", ...)`.
- Keep the UI append (`Tab.cpp:7772`).

## Coupling / blockers
- No dependency on other Neotko features.
- Requires "Slow down for overhang" (`enable_overhang_speed`); inert when walls print outer-wall-first.
- Target's `estimate_extrusion_quality` has a different, lambda-based signature (`ExtrusionProcessor.hpp:421`), and target's GCode block uses `NOZZLE_CONFIG/FILAMENT_CONFIG` macros - re-implement, category D.

## Verification
- Inner-wall speed changes only near overhangs; no change on fully supported inner walls; ratio 0 = byte-identical; fan follows.

## Open questions
- Map `path.role() == erPerimeter` to 2.5's `is_perimeter`/`is_external_perimeter`.
- The code does not itself test `wall_sequence`; the "inert outer-wall-first" claim relies on upstream ordering - verify.
