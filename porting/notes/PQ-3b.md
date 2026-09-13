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
