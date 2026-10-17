# PP-5 — the teardrop detector and Mesh boolean after a boolean

Both bugs reported after PP-4, both now fixed on `port/PP-5-fixes`. The stale-index fix from
`9cf2a9def0` is kept; neither bug was that. `docs/handoff.md` section 37 has the narrative.

## (1) A teardrop tunnel was not detected

The patch around a teardrop tunnel is its whole side wall — arc and roof meet tangentially, so they
are one smooth 132-face patch (the two caps are separate patches that the cylinder test drops). The
gate that rejected it was the circle residual, and the band meant to exclude the roof was the
problem.

The band selected **faces by centroid**, and a teardrop roof quad is two triangles: the one that
shares the arc's tangent vertex has its centroid ON the circle while it still carries the apex
vertex far off it. Instrumented (temporary `std::printf` before each `continue` in `detect_holes`,
one label per gate):

```
[PP5] seed pts=132 r=2.543 residual=0.902          # the roof pulls the first fit out
[PP5] refit inliers=130/132 r=2.516 residual=0.986 # the apex is back in the refit; 0.986 > 0.201
```

The 130 inliers are the arc (128) plus the two roof triangles whose centroid lands on the circle;
`inlier_pts` then took **all three vertices** of every inlier face, so the apex re-entered the fit,
and `fit_circle()` returns a *maximum* residual, so one apex vertex killed the patch. The earlier
note in the test ("a roof face's centroid sits ~0.35 r off") was wrong for this triangulation —
half the roof triangles have a centroid on the circle.

**Fix** (`HoleDetector.cpp`): band the **vertices** of the seed circle, refit on those, and gate the
residual on those. The roof faces stay out of the convexity average by centroid, and the angular
coverage is measured on the same vertices the fit used. After:

```
[PP5] circle pts=130/132 r=2.500 residual=0.000 tol=0.200
[PP5] gap max=90.0 deg
[PP5] convexity avg=-0.999 inliers=130
```

The test case is un-tagged (`[NotWorking]` removed). The full `libslic3r` suite is green: 103170
assertions in 1548 cases.

## (2) Mesh boolean did nothing once the object had already had a boolean

Reported with a perforation, an edge fillet and a place-hole-on-face cutter; the trigger was "an
earlier boolean". Ruled out by measurement (not by reading):

- **The stale index** — already fixed in `9cf2a9def0`, and the symptom survived it.
- **The raycaster array** — the tool reports `raycasters_count` next to `volume_count`; they matched
  at every step of the repro.
- **The activation gate** — `on_is_activable()` wants a full instance with more than one volume
  selected. A perforation and an edge-dress commit both leave the whole object selected, so the
  gizmo opens with the natural selection; a direct pick then runs the second boolean fine.
- **The boolean itself on clean input** — a second Difference with two counters on a fresh box
  worked (12 → 900 → 2140 triangles).

**Root cause**: `extrude_perforation_pattern()` (`PerforationPattern.cpp`) built the cutter by
pushing **three fresh vertices per cap triangle** and its own vertices per wall strip. The prism is
geometrically closed but topologically an open soup — measured 360 open edges and 90 patches on a
honeycomb cutter (a proper one has 12 patches, 0 open edges). `MeshBoolean::mcut` is handed that as
a *solid*: it fails or crashes, and a result it produced from one is not watertight either, so the
**next** boolean fails. That is "does nothing once the object has already had a boolean", and the
crash is real: an `ACCESS_VIOLATION` inside mcut's `hmesh_t::source` (crash log from
`16:15:08`), on the honeycomb-cutter result fed to a circles-cutter Difference. The edge-fillet and
place-hole cutters are watertight, which is why every *second* operation failed regardless of which
of the three the user tried: the first boolean's result was the broken input.

**Fix**: weld the prism with `its_merge_vertices()` before returning. The cap and wall vertices come
from the same `Point` coordinates through the same scaling, so the weld is exact. After: the cutter
has 0 open edges and the honeycomb-then-circles sequence runs clean (904 → 7604 triangles, 0 open
edges). New `[PerforationPattern]` case pins the prism closed (0 open edges, positive volume) for
Lines, Grid, Honeycomb and Circles.

## Reproducing without a human

`mesh_boolean_gizmo` (MCP-3) drives the gizmo: open, pick by index or by screen ray, `run`. Service
the flow with a perforation cutter on a box, one Difference, a second cutter, a second Difference.
Before the weld the second one crashes mcut or produces a non-watertight result; after it, both
results have 0 open edges.

## Files

- `src/libslic3r/HoleDetector.cpp` — vertex band, vertex refit, vertex residual gate.
- `src/libslic3r/PerforationPattern.cpp` — `its_merge_vertices()` at the end of
  `extrude_perforation_pattern()`.
- `tests/libslic3r/test_holedetector.cpp` — teardrop case un-hidden.
- `tests/libslic3r/test_perforationpattern.cpp` — the closed-solid case.

Patch: `porting/patches/73_PP-5-fix.patch` (MCP-3's tool is `72_MCP-3.patch`).
