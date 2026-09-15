# PQ-3b - NeoArachne full hybrid routing

- Source: `NEOTKOCM_RELEASE_2_38.md`.
- Fork: `OrcaFS-NeotkoCM` @ `2c90c65bea`, current form `9299cb368a`.
- Category: D, very large (~3300 new lines / 23 files).

## Scope
- Add `wall_generator = neoarachne`; per-feature routing (outer / inner / gap fill each Classic / Arachne / NeotkoEdge); validator auto-aligning invalid combos; Edge Closure; wall-count hysteresis; preview panel.

## New files
- `src/libslic3r/NeoArachne/*` (21 files, ~1816 lines; includes `Preview/` 9 files)
- `src/slic3r/GUI/NeoArachnePreviewPanel.cpp/.hpp` (1482 lines)

## Upstream anchors (fork lines)
- Enum: `PrintConfig.hpp:382-385`, `PrintConfig.cpp:512,8531`
- Dispatch: `LayerRegion.cpp:339-352`; `NeoArachnePlan.cpp:59-360`; `NeoArachneEngine.cpp`; `NeoArachneInterior.cpp`
- Edge Closure: `NeoArachneInterior.cpp:36-68`; `WallToolPaths.cpp:510-524,564-567`; Plan `:266-292`
- Hysteresis: `NeoArachneConfig.hpp:48-51`, `NeoArachneBeadingStrategy.cpp:30-48`
- Validator: `ConfigManipulation.cpp:562-627`
- Preview: `NeoArachnePreviewPanel.cpp:818-821` (self-gates on `wall_generator==NeoArachne`); `Tab.cpp:7582-7595`
- Plumbing: `GCode.cpp:363,8521,10085`; `ExtrusionEntity.hpp:148-283`; `GCode.hpp:343,687`

## Keys
- `wall_generator` += `neoarachne`
- `neoarachne_outer_wall` (enum, Classic), `neoarachne_inner_walls` (enum, ArachneStock), `neoarachne_gap_fill` (enum, Off)
- `neoarachne_allowed_overlap_pct` (0), `neoarachne_min_bead_width_pct` (40), `neoarachne_max_bead_width_pct` (200), `neoarachne_min_feature_size_pct` (10), `neoarachne_keep_short_tails` (true), `neoarachne_bead_count_hysteresis_pct` (20)
- Note: `PrintConfig.hpp:1383-1402` region keys were split into a derived config due to the BOOST_PP 256 limit - must be reconciled with target's structure.

## Gates to remove
- `LayerRegion.cpp:345-350` - the `if (neotko_libre_mode)` around `NeoArachne::run`.
- `Tab.cpp:8238` - hides the enum value unless LibreMode.
- `ConfigManipulation.cpp:1006-1009`.

## Coupling / blockers
- Depends on `NeoDebug`.
- Target already inserted `smipSpiralInset` etc.; **append enum values last** or serialized profile indices break.
- `SupportMaterial`/`PerimeterGenerator`/`WallToolPaths` may have moved; expect category D re-implementation.

## Verification
- Classic and Arachne unchanged; NeoArachne hybrid routing correct; invalid combos auto-align; the fork's print tests.

## Open questions
- Whether to port PQ-3a and PQ-3b together (PQ-3a cannot stand alone).
- The `ExtrusionEntity`/`GCode` spiral-lift plumbing may or may not be required for basic routing.

## Port progress

### Status: ported (branch `port/PQ-3`, base `port/integration` @ `df0800a0a9`).

Decisions taken during the port (all in `PORTING_PLAN.md` decisions 4/7):
- **PQ-3a + PQ-3b are one port.** The two "low-risk" knobs are reachable only on the hybrid
  path, so they ship with the routing.
- **Preview panel excluded.** `NeoArachnePreviewPanel.{hpp,cpp}` and the `Preview/` subdir are
  not ported; the routing works without them. (User decision.)
- **Naming neutralised.** The fork's `NeoArachneWallSource::NeotkoEdge` becomes
  `HybridWallSource::ArachneHybrid` (serialised `"arachne_hybrid"`, label "Arachne (hybrid)").
  The `neoarachne_*` keys are `hybrid_*` (mapping below). The fork's `NEOTKO_NEOARACHNE_TAG`
  comment tags are neutralised to `[ORCAPORT:PQ-3]` / `NEOARACHNE`; new files carry the
  `[ORCAPORT FILE]` stamp.
- `wall_generator` gains the value string `"hybrid"` (label "NeoArachne"), appended last so
  serialised indices do not shift.

### New files
`src/libslic3r/NeoArachne/` (12 files, namespace `Slic3r::NeoArachne`):
`NeoArachneConfig.hpp`, `NeoArachneRuntime.hpp`, `NeoArachneDebug.{hpp,cpp}`,
`NeoArachneEngine.{hpp,cpp}`, `NeoArachnePlan.{hpp,cpp}`, `NeoArachneInterior.{hpp,cpp}`,
`NeoArachneBeadingStrategy.{hpp,cpp}`. Registered in `src/libslic3r/CMakeLists.txt`.

### Upstream anchors (target files)
- `src/libslic3r/PrintConfig.hpp` :: enum `HybridWallSource` after `PerimeterGeneratorType`;
  `Hybrid` appended to `PerimeterGeneratorType`; 11 `hybrid_*` region keys; static-map declares.
- `src/libslic3r/PrintConfig.cpp` :: `"hybrid"` in the wall-generator map/label; the
  `HybridWallSource` key map + DEFINE; the 11 field definitions.
- `src/libslic3r/LayerRegion.cpp` :: `process_ex` :: `wall_generator == Hybrid && !spiral_mode`
  -> `NeoArachne::run(g)` (ungated; the fork's `neotko_libre_mode` check is gone).
- `src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.{hpp,cpp}` :: `makeBeadingStrategy`
  :: 4 edge params, wrap the meta-chain with `NeoArachne::NeoArachneBeadingStrategy` before
  `LimitedBeadingStrategy`.
- `src/libslic3r/Arachne/WallToolPaths.{hpp,cpp}` :: `WallToolPathsParams` gains
  `keep_short_tails`, the 4 edge params, `wall_transition_filter_dist_mm`,
  `max_bead_width_pct`; ctor applies the ceiling / filter distance / short-tail gate.
- `src/libslic3r/ExtrusionEntity.hpp` :: `ExtrusionPath` base `force_no_spiral_lift`.
- `src/libslic3r/GCode.{hpp,cpp}` :: `_extrude` records `force_no_spiral_lift`; lift sites
  downgrade `SpiralLift` -> `SlopeLift`; wipe rebalance (>=50% pre-wipe).
- `src/libslic3r/Preset.cpp` :: the 11 `hybrid_*` region keys.
- `src/slic3r/GUI/ConfigManipulation.cpp` :: `update_print_fff_config` validator
  (coerce invalid combos) + `toggle_print_fff_options` visibility.
- `src/slic3r/GUI/Tab.cpp` :: Quality > Wall generator, 11 option lines.

### Keys (region config) - fork mapping
`hybrid_outer_wall` [fork `neoarachne_outer_wall`; enum; Classic];
`hybrid_inner_walls` [enum; ArachneStock]; `hybrid_gap_fill` [enum; Off];
`hybrid_allowed_overlap_pct` [0]; `hybrid_min_bead_width_pct` [40];
`hybrid_max_bead_width_pct` [200]; `hybrid_min_feature_size_pct` [10];
`hybrid_keep_short_tails` [true]; `hybrid_pin_outer_width` [fork
`neoarachne_pin_outer_width`; true]; `hybrid_bead_count_hysteresis_pct` [20];
`hybrid_transition_filter_dist_mm` [fork `neoarachne_transition_filter_dist_mm`; 100].
`wall_generator` += `"hybrid"`.

### Build evidence
`.\OrcaSlicer\build_win.bat -s --no-configure -j 8` -> clean; `OrcaSlicer.dll` relinked with
all `NeoArachne/*`, `WallToolPaths.cpp`, `BeadingStrategyFactory.cpp`, `GCode.cpp`,
`ConfigManipulation.cpp`, `Tab.cpp`. OrcaSlicer.dll rebuilt 2026-09-15 00:51.

### Verification
`wall_generator` default stays `Arachne`, so the hybrid path is off by default and stock
Classic/Arachne output is unchanged (no edits on those paths). Runtime verification of the
hybrid path is pending (manual: slice with `wall_generator = NeoArachne`, sweep the
per-feature selectors and the pin/hysteresis/transition knobs).

### Post-merge fix (runtime, found by the user)
- First runtime test with `wall_generator = NeoArachne` failed: slicing threw
  `Slic3r::InvalidArgument` from `GCode::extrude_entity`. Cause: `NeoArachne::Interior`
  appends a nested `ExtrusionEntityCollection` (the per-island bucket) to `g.loops`, but
  target 2.5's dispatcher only knew `ExtrusionPath`/`ExtrusionMultiPath`/`ExtrusionLoop`.
  Fixed by porting the fork's `Inc2e` branch: recurse into a nested collection, keeping the
  stored order when `no_sort` and chaining by proximity otherwise (`GCode.cpp`). This was the
  only Inc2e change not already present; Inc2a/b/c/d were ported originally. Rebuild clean;
  slicing now proceeds past the dispatcher.
- Known minor gap (same as the fork): `GCode::extrude_loop`'s `wipe_before_external_loop`
  neighbour scan iterates `region_perimeters` and calls `as_polyline()`, which is empty for a
  nested collection, so that heuristic under-counts when wipe-before-external-loop is enabled.
  No crash; only affects that optional wipe mode.
