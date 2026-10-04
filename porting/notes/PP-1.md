# PP-1 — Perforations gizmo (planar face)

Status: **implemented, build clean, verified** (unit + headless slice). Not merged/pushed.

## What it is

A new mesh gizmo that stamps a 2D pattern into a picked **planar** face and adds it as a single
`NEGATIVE_VOLUME`. Full through perforations, not surface grooves: the pattern is subtracted from
the wall. This is the (A) reading of the original request — real holes for airflow / weight, not
the "expose infill with top/bottom layers = 0" trick.

## Why negative volumes and not a mesh boolean

Orca does not mesh-boolean negative volumes. `PrintObjectSlice` slices them like any other volume
(`model_volume_needs_slicing`, line ~145) and subtracts them in 2D per layer
(`applyNegtiveVolumes`, `PrintObjectSlice.cpp:620`). Consequences that shaped the design:

- No CGAL/mcut 3D boolean, no 3D mesh-validity fight, UI stays responsive.
- Non-destructive: the part mesh is untouched; delete the volume to undo.
- `NegativeVolume` is already understood by `.3mf` (`Format/3mf.cpp:368`) — **no new config keys,
  no persistence format, no migration.**
- The prism only has to *slice* cleanly, not survive a boolean.

## Patterns (v1, updated)

Four kinds, chosen so `angle` does the rest:

- **Lines** — one parallel family.
- **Grid** — two orthogonal families at `angle` / `angle+90` (`+` at 0, `x` at 45). Replaces the
  former Cross/X/DualDiagonal, which were the same family set under different angle conventions.
- **Honeycomb** — staggered hexagons (isotropic; the airflow default).
- **Circles** — round holes on a staggered grid (isotropic, conventional vent look).

Angle slider is **0–45°**, which covers every orientation once a family is crossed.

### Minimum wall - warning only, no auto-shrink

The builder uses `width` **exactly as set**; it never shrinks it. `PERFORATION_MIN_WALL` (1.0 mm)
is an advisory threshold only: when `spacing - width` drops below it the panel shows a warning
("Hole width is too large for the spacing: the holes may merge..."). If the user ignores it the
holes do merge into a continuous void - applying is allowed, and the warning explains it. No value
is ever changed behind the user's back.

(Earlier revisions silently clamped the width here. That made the preview geometry a function of
two sliders at once and, combined with the auto-resizing panel, read as the controls "spazzing
out". Removed by user decision.)

### UI

- Each of spacing/width/boundary/angle is a **slider plus a typed number input** in separate
  columns. Fixed ranges (spacing 1-25, width 0.2-10, boundary 1-25, angle 0-45). The slider commits
  every edited frame so the handle tracks the drag; the typed input commits on Enter. Neither
  clamps the other, and neither is ever changed automatically.
- **The panel width is pinned.** `SetNextWindowSizeConstraints(win_w, 0)..(win_w, FLT_MAX)` before
  `Begin` fixes the width while the height still auto-fits, and all body text wraps at the content
  width. Without this, `AlwaysAutoResize` derived the width from the widest item, so a warning
  appearing mid-drag widened the window, slid the slider out from under a stationary cursor, and
  ImGui read the offset as a new value. `SetWindowSize` after `Begin` does not fix it (the next
  `Begin` recomputes from content); only a size constraint does. Same finding as SU-5.
- A **near-horizontal face warning** shows when `|normal . up| > 0.9`: holes in a top/bottom face
  bridge over air or leave first-layer gaps. The two notices share reserved rows, so toggling one
  changes neither the width nor the height.

### Icon

Teal panel `#009688` with **pure-black** holes. The toolbar's texture loader forces every drawn
pixel to the neutral tone for the un-hovered tile and recolours only pure-black pixels (to light)
for the hover tile while leaving teal pixels alone. So: neutral = solid neutral square (no teal,
like every other gizmo), hover = teal panel with light holes. Two neutral tones in one icon is not
possible — the normal pass flattens all drawn pixels to a single colour.

## Files

| Kind | Path |
|------|------|
| Core (pure, testable) | `src/libslic3r/PerforationPattern.{hpp,cpp}` |
| Gizmo | `src/slic3r/GUI/Gizmos/GLGizmoPerforation.{hpp,cpp}` |
| Icons | `resources/images/toolbar_perforation{,_dark}.svg` |
| MCP control | `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` (`perforation_gizmo`) |
| Unit test | `tests/libslic3r/test_perforationpattern.cpp` |
| Slice test | `tests/fff_print/test_perforation.cpp` |

Modified: `src/libslic3r/CMakeLists.txt`, `src/slic3r/CMakeLists.txt`,
`src/slic3r/GUI/Gizmos/GLGizmosManager.{hpp,cpp}`, `src/slic3r/GUI/GLCanvas3D.cpp`,
`src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp`, both tests' `CMakeLists.txt`.

Marker: `[ORCAPORT:PP-1]`. Shortcut: `Ctrl+I`. Enum: `GLGizmosManager::EType::Perforation`, placed
before `Primitive`/`Sketch` so the `SLIC3R_CAD`-off numbering is unaffected.

## Reuse (no new plumbing)

- Face picking / coplanar region / patch: `raycast_object_face`, `coplanar_region`,
  `build_coplanar_patch` (`GLGizmosCommon.hpp`).
- Plane axes: `face_plane_axes` (`CutUtils.hpp`).
- 2D booleans/offsets: `union_ex`, `diff_ex`, `offset_ex`, `intersection_ex` (`ClipperUtils.hpp`).
- Prism caps: `triangulate_expolygons_3d` (`Tesselate.hpp`); walls: the `wall_strip` pattern from
  `SlicesToTriangleMesh.cpp`.
- Negative-volume add: `ModelObject::add_volume(..., NEGATIVE_VOLUME, false)` + `take_snapshot`,
  mirroring `GLGizmoHoles::add_named_volume`.
- Depth probe: `AABBMesh::query_ray_hits` (`HoleShapes.cpp:354` uses the same).

## Design decisions and their reasons

- **Union in 2D before extruding.** Crossing bars (cross/x/dual-diagonal) overlap; `make_perforation_pattern`
  unions the families and intersects with the inset face, so the prism is a clean non-self-overlapping solid.
- **Domain from triangles.** The coplanar region is triangles; each is mapped to the face plane and
  unioned into a polygon. Then `offset_ex(-margin)` for the boundary.
- **Prism spans [-d, +0.5].** `z_out = +0.5` guarantees the outer skin is fully cut; `z_in` is the
  first internal surface the inward ray crosses (hollow shell) so a hole stops at the far wall and
  does not punch internal features. Solid parts fall back to the whole-object span.
- **Pattern families via axis-aligned strips.** `line_family` rotates the domain by -theta, lays
  horizontal strips, rotates back — no per-line maths, `union_ex`/`intersection_ex` do the clipping.
- **Honeycomb as equals-spacing holes.** `width` is the across-flats; hex circumradius
  `r = width/sqrt(3)`; wall between holes = `spacing - width`.

## Verification

- `libslic3r_tests [PerforationPattern]` — 5 cases: holes shrink the face; invert complements
  inside the inset (`holes + grooves == inset`); prism volume == area x height; a valid width keeps
  discrete-hole patterns discrete (Lines/Honeycomb/Circles > 1 contour) and Grid a perforated
  lattice; and width is used **as given** — an over-wide width merges the slots into one void
  (documented, not corrected).
- `fff_print_tests [Perforation]` — 1 case: a hollow 30x30x20 box sliced with and without a honeycomb
  negative volume on a side wall; the perforated mid-height layer carries **more wall loops**. This
  proves the volume reaches the slicer and is subtracted per layer. Headless, deterministic.
- Runtime (MCP): `perforation_gizmo open/set_params/hover_face/apply` on `tests/data/20mm_cube.obj`
  added the volume (bounding box unchanged) and `slice_all` reached `idle` with only the pre-existing
  unrelated wall warning. `export_gcode` / `export_3mf` open a dialog and block the MCP main thread —
  that is the known MCP-1 limitation, not this feature; the headless slice test is the real check.

## Not in v1 (deliberate)

- Curved faces (cylinders, domes) — needs a region→UV parameterization. The core is surface-agnostic;
  a developable-cylinder unwrap is the intended v1.5, a new region→plane function only.
- Multiple faces in one action, per-hole editing, fillets/chamfers on hole rims.
- Parametric re-editing: the pattern is baked into one volume; delete and re-add to change it.
- No `.3mf` recipe beyond the negative volume itself (so the shape survives, the parameters do not).

## Build

Standard Windows build. Tests are off by default; enable once with
`cmake -S . -B build -DBUILD_TESTS=ON`, then `cmake --build build --config Release --target
libslic3r_tests fff_print_tests`.
