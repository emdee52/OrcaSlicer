# PF-10-auto - automatic support painting

- Source: preFlight v1.3.0 (`github.com/oozebot/preFlight`), "Automatic painting" (fork sha
  `f74dc69`), **reimplemented Orca-native** - preFlight's version paints every support spot as a
  generic enforcer and its button is disabled. Our version classifies overhang regions into the
  registered support styles.
- Branch: `port/PF-10-auto-fix`, based on `port/integration` (PF-10-auto already merged).
- Category: B/D. Status: **reimplemented (mesh-driven), build clean; runtime pending.**

## Why the first port did not work

The first PF-10-auto classifier was support-preview-driven (`PrintObject::generate_support_preview`
on a background thread, then `SupportLayer::support_islands` + upward raycasts). It never painted:

- `support_islands` is written only by the classic engine (`SupportCommon.cpp:2023`); tree/organic
  supports leave it empty, so `classify_support_paint` returned `{}` and the button did nothing.
- Support layers were walked bottom-up with a first-hit dedupe, so regions were measured at the
  support **base**, not the contact patch; `support_height_mm` was ~one layer, so the height gates
  never fired.
- `get_facet_state` returns `NONE` for split triangles and `set_facet` calls `undivide_triangle`, so
  an auto-paint could overwrite a painted blocker.
- The gizmo highlight angle and "on highlighted overhangs only" were ignored.
- The button was wired to the BBS preview thread; a skipped/cancelled preview left it looking dead.
- No per-type control, no undo snapshot.

## New design (mesh-driven, synchronous)

`classify_support_paint(ModelObject, instance_trafo, painted_masks, params)`:

1. For each model-part volume, a facet is a **candidate** when its normal is within
   `overhang_angle_deg` of straight down - the exact predicate of
   `GLGizmoFdmSupports::select_facets_by_angle`, so candidates equal the highlighted overhangs.
   When `overhangs_only` is false the angle is 90 deg (every downward facet). Facets already painted
   (any state, including blockers) are excluded using `TriangleSelector::painted_facet_mask()`,
   which is split-safe (each triangle keeps its `source_triangle`).
2. Candidates are clustered into regions by facet adjacency (`its_face_neighbors`, union-find).
3. Each region is measured: `area_mm2`, `span_mm` (max XY extent), `height_mm` (lowest point above
   the plate), `wall_angle_deg` (0 = vertical wall, 90 = horizontal ceiling, from the max
   facet severity), `curvature` (1 - |area-weighted mean world normal|), and `gap_below_mm` (one
   downward ray to the next model surface).
4. The feature vector is scored against the registry's per-type rules (below); the winning state is
   painted on every facet of the region. No rule matches => left unpainted (object's own style).

Runs synchronously on the UI thread; no slicing, no support preview, no thread.

## Extensible scoring registry (`SupportPaintTypes`)

The single extension point is the registry. A **new support type is one row** (`SupportPaintType`),
including its `rules`; the classifier, gizmo and slicer dispatcher all iterate the registry and need
no changes. Adding a new *feature* is one `SupportFeature` value plus its computation in
`SupportAutoPaint`, after which every type can weigh against it.

- `SupportPaintRule::Term` = a ranged preference over one feature: full credit inside
  `[min_val,max_val]`, linear decay to `[soft_min,soft_max]`, `weight`, and `hard` (outside the
  window rejects the clause).
- A rule is a conjunction of terms; a type may carry several rules (OR) and keeps its best score.
- `support_type_score = best_rule_score * priority`; a type wins when `score >= min_score`, highest
  score first. `priority` is the hook for future strength/material weighting UI.

Default rules (all thresholds live in this one table):

| Type | Rules | min_score | priority | auto default |
|------|-------|-----------|----------|--------------|
| Snug | `Area >= 50` | 0.5 | 1 | on |
| Grid | `Area >= 400 && Span >= 40 && Height <= 50` | 0.9 | 2 | on |
| Organic | (`Area <= 30 && Height >= 4`) OR (`Curvature >= 0.5 && Height >= 10`) | 0.6 | 3 | on |
| NeoWave | `WallAngle >= 70 && GapBelow ~<= 4` OR `WallAngle >= 78` | 0.5 | 3 | **off** |

`Default` (the legacy generic `ENFORCER`) has no rules and is never auto-selected; it is chosen by
the left brush when a region should keep the object's own style.

## Gizmo

- "Automatic painting" runs the analysis synchronously and paints; `TakeSnapshot` is taken first.
- Plus a per-type checkbox row ("Automatic types") seeded from `auto_enabled_by_default`; NeoWave is
  off until checked. The enabled set is passed to the classifier (`enabled_types`).
- Uses the existing "Highlight overhangs" angle and "On highlighted overhangs only" checkbox.
- The old preview/thread hooks (`m_auto_paint_pending`) were removed; `update_support_volumes`
  returns to its stock form.

## Behavior-neutral at defaults

Nothing runs until the button is pressed. No config keys, no change to normal painting or slicing.
Unclassified regions are not painted; a re-run is additive (painted facets are excluded), so
"Erase all" gives a clean run.

## Deviations / gaps

- Classification granularity is the original mesh facet; connected components are not split by
  normal, so a strongly curved region is one patch (tune later if over-merging shows up).
- `overhangs_only == false` uses every downward facet (90 deg); documented interpretation.
- The thresholds are fixed defaults (weighting UI deferred by user decision).
- `test_orca_support_paint.cpp` covers scoring; the mesh region extraction is exercised manually.

## Verification

- Build: `build_win.bat -s --no-configure -j 8` -> 0 errors; `OrcaSlicer.dll` linked 2026-09-16.
- Tests: `build_win.bat -s --run-tests` -> **100% of 807 tests passed**, including the eight
  `[OrcaSupportPaint]` cases in `tests/libslic3r/test_orca_support_paint.cpp` (Grid/Snug/Organic/
  NeoWave selection, NeoWave opt-in gating, leave-unpainted, hard/soft term membership).
- Manual (pending): press Automatic painting -> overhangs are painted immediately with a mix of
  Snug/Grid/Organic (and NeoWave when checked); painted/blocker regions untouched; re-run is stable;
  "Erase all" clears; undo restores.

## Files

- Modified: `src/libslic3r/OrcaExt/SupportAutoPaint.{hpp,cpp}` (rewritten),
  `src/libslic3r/OrcaExt/SupportPaintTypes.{hpp,cpp}` (scoring registry),
  `src/libslic3r/TriangleSelector.{hpp,cpp}` (`painted_facet_mask`),
  `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.{hpp,cpp}`,
  `tests/libslic3r/CMakeLists.txt`, `tests/libslic3r/test_orca_support_paint.cpp` (new).
- Patch: `porting/patches/20_PF-10-auto.patch` (regenerated).
