# PF-10-auto - automatic support painting

- Source: preFlight v1.3.0 (`github.com/oozebot/preFlight`), "Automatic painting" (fork sha
  `f74dc69`), **reimplemented Orca-native** - preFlight's version is disabled.
- Branch: `port/PF-10-auto`, based on `port/integration` @ `a0cd6f0832` (PF-10-paint merged).
- Category: B/D. Status: **ported, build clean; runtime pending.**

## Why this is a reimplementation

preFlight's `auto_generate()` painted `ENFORCER` facets around `SupportSpotsGenerator` support
points via `TriangleSelectorWrapper::enforce_spot`, but the button is **commented out** because the
support-spots search is skipped when support is enabled. Orca has no `TriangleSelectorWrapper` and
its `generated_support_points` is never populated (`SupportPoint` is commented out); Orca's
`SupportSpotsGenerator` only fills `Layer::curled_lines`. So there is no support-points backend to
reuse.

## Data source (U1: support-preview-driven)

The auto-painter runs the existing support preview (`PrintObject::generate_support_preview()`), then
reads the generated **top-contact support islands** and projects them up onto the object mesh
(`OrcaExt/SupportAutoPaint`):

- For each support layer island, sample its contour + centroid, raycast up against each model-part
  volume's AABB tree, and take the nearest **downward-facing** facet.
- `contact_area` = island area (mm²); `support_height` = number of stacked support layers below the
  sample, in mm.
- Classify with `support_paint_classify(area, height)` (registry-driven, fixed defaults).
- A facet already painted (any non-NONE state) is skipped. Because support generation already
  respects painted blockers and blocker volumes, a blocked region produces no support contact - so
  the auto-paint inherently **follows blockers**.

## Classifier (data-driven, `SupportPaintTypes.hpp`)

Per-region, first highest-priority match wins:

| Type | Rule | Priority |
|------|------|----------|
| Organic | area <= 25 mm² and height >= 3 mm (small tall overhang; tree routes around) | 20 |
| NeoWave | area >= 100 mm² and height >= 3 mm (wide flat overhang on a tall support; wave roof eases removal) | 15 |
| Snug | area >= 50 mm² (broad flat; best interface) | 10 |
| Grid | fallback | 1 |

Baobab's rule is added with its engine in `PF-10`. Thresholds are fixed defaults in one table.

## Implementation

- New `src/libslic3r/OrcaExt/SupportAutoPaint.{hpp,cpp}` (`[ORCAPORT FILE]`):
  `classify_support_paint(const PrintObject&) -> std::vector<SupportAutoPaintHit>`.
- `SupportPaintTypes.{hpp,cpp}`: `SupportPaintClassify` per entry + `support_paint_classify()`.
- `TriangleSelector`: `get_facet_state(int)` so the painter can skip already-painted facets.
- `GLGizmoFdmSupports`: "Automatic painting" button; forces a support preview (reusing the existing
  worker thread), then paints the classified facets on the UI thread when it finishes.

## Behavior-neutral at defaults

Nothing runs until the button is pressed; no config keys, no change to normal painting/slicing.

## Deviations / gaps

- The preview is generated with the object's current support config, so the contact footprint (not
  the support style) is what drives classification - intended.
- Painting is additive: existing enforcers are left in place (a re-run adds; "Erase all" clears).
- Runtime/quality of the thresholds is unverified; the table is the tuning point.

## Verification

- Build: `build_win.bat -s -j 8` (new files) -> 0 errors; `OrcaSlicer.dll` linked 2026-09-15.
- Manual (pending): press Automatic painting -> a progress preview runs, then overhangs are painted
  with a mix of Snug/Grid/Organic/NeoWave; a painted blocker stays unassisted; re-running is stable.

## Files

- New: `src/libslic3r/OrcaExt/SupportAutoPaint.{hpp,cpp}`
- Modified: `src/libslic3r/CMakeLists.txt`, `src/libslic3r/TriangleSelector.hpp`,
  `src/libslic3r/OrcaExt/SupportPaintTypes.{hpp,cpp}`,
  `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.{hpp,cpp}`
- Patch: `porting/patches/20_PF-10-auto.patch`
