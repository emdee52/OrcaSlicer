# Upstream sync 2026-10-15

Merging mainline `OrcaSlicer/OrcaSlicer` main into the port. This file records what the merge
had to decide, so a later session can review it without re-reading every hunk.

## Facts

| item | value |
|------|-------|
| merge base | `c168d0c8db` (2026-09-19, the previous sync already inside `port/integration`) |
| upstream tip | `109d09e0b8` (2026-10-15) - `upstream/main` and `origin/main` are the same commit (verified) |
| commits merged | 1352 |
| branch | `sync/upstream-20261015` (from `port/integration` `00f4969058`) |
| backup ref | `backup/port-integration-20261016` |
| conflicts | 44 files, all resolved; no conflict markers left in `src/` or `tests/` |

Local `main` was fast-forwarded to `109d09e0b8` with `git fetch origin main:main` (no checkout).

Conflict style was `zdiff3`, so each hunk also showed the merge base. In the working file
`HEAD` was the port and `origin/main` was upstream.

Upstream removed in this range: all of Clipper1 (`deps_src/clipper`,
`src/libslic3r/clipper.{cpp,hpp}`, `Clipper2Utils.*`), `deps/GLEW`, `deps/OpenCSG`, and the CAD
gizmos `GLGizmoPrimitive` / `GLGizmoSketch` / `GLGizmoHollow` / `GLGizmoText` /
`GLGizmoAdvancedCut` / `GLGizmoFaceDetector` / `GLGizmoSlaSupports`. It added belt-printer and
IMEX support, `PreciseSeam`, `MeshRepair`, `TextureBake`, and its own section view (a
clipping plane in `libvgcode`, exposed as `Viewer::set_clipping_plane`).

## Resolutions worth knowing

Dots/build files - `.gitattributes`, `.gitignore`, the three `CMakeLists.txt` - are unions;
upstream's block kept verbatim and the port's files appended. The commented-out
`#GLGizmoHollow.*` entries were dropped (upstream deleted that gizmo).

**Traps where one common line had to close two different scopes** (a naive union breaks the
brace balance):
- `Support/TreeModelVolumes.cpp` - the port's SU-1 neighbour block and upstream's belt-floor
  block; emitted both with their own closing brace.
- `PrintObject.cpp` - same shape for the port's `if (painted.empty())` early-out and upstream's
  belt unshift/reshift around the classic generator.
- `PrintObjectSlice.cpp` - the port's two new functions were left unclosed.
- `Support/SupportMaterial.cpp` - the port's grid-bite loop closes; upstream's
  `has_belt_floor` block closes; both pairs are needed.

**Signature changes kept in step:**
- `GCode/ExtrusionProcessor.hpp` + `GCode.cpp`: `estimate_extrusion_quality(..., bool
  slowdown_for_curled_edges, float overhang_shadow_ratio, float overhang_shadow_reach, float
  fan_overlap_threshold)`; both call sites pass `inner_wall_shadow_ratio,
  inner_wall_shadow_reach, fan_overlap_threshold`.
- `Print.hpp` / `PrintObject.cpp`: upstream's `FillAdaptive::RegionOctrees
  prepare_adaptive_infill_data(...)` (definition and call site already match).
- `Print.cpp`: upstream's new `generate_support_material(bool with_belt_brim)` - the port's
  serial `xobj_serial_support` loop also passes `false`, or those objects would get a belt
  brim twice.

**Upstream mechanism adopted, port duplicate dropped:**
- `Gizmos/GLGizmosManager.{cpp,hpp}` - the CAD `Primitive`/`Sketch` enum entries and their
  registration are gone (upstream deleted those gizmos; `SLIC3R_CAD` is ON, so keeping them
  would not compile).
- `Gizmos/GLGizmosCommon.{cpp,hpp}` - upstream deleted `AssembleViewDataPool`,
  `ModelObjectsInfo` and `ModelObjectsClipper` (nothing referenced them afterwards in either
  tree). The port's face-picking helpers (`FaceRegionCache`, `coplanar_region`,
  `region_has_facet`, `build_coplanar_patch`, `raycast_object_face`, `build_face_snap_points`,
  `world_to_screen`) stay.
- `libvgcode` (PF-6) - upstream's **section view** implements the same toolpath clipping
  (`uniform vec4 clipping_plane`, cut faces via `find_cut`, driven by
  `GCodeViewer::set_clipping_plane(ClippingPlane)` <- `Viewer::set_clipping_plane(array)`).
  So the port's parallel shader path was dropped: the cap-triangle geometry in
  `SegmentTemplate.cpp` (which existed only because the port's shader merely discarded
  fragments) plus its `clip plane cap projection` block, the `is_cap`/`eff_id` vertex-id
  remap, the duplicate `clipping_plane` uniform declarations and the `clipping_dist` varying.
  `Shaders.hpp` and `SegmentTemplate.cpp` are now byte-identical to upstream.
  The port's plumbing is kept and still works: `Viewer/ViewerImpl::set_clipping_plane(nx, ny,
  nz, offset)` and `reset_clipping_plane()` write upstream's `m_clipping_plane`, which
  upstream's uniforms upload, and the port's shell clipping (`gouraud_light_clip` shaders,
  `GCodeViewer::m_preview_clipping_plane`) is untouched. PF-6's controller keeps working with
  no source change.
- `PartPlate.{cpp,hpp}` - the port's plate-column icons and upstream's IMEX icon wanted the
  same slots. Upstream's IMEX sub-id 9 and layout slot 6 (single) / 7 (dual) are kept as-is;
  the port's free-Z toggle and Snap & Drag magnet moved up one slot
  (`SNAP_DRAG_HOVER_ID = 10`, `FREE_Z_HOVER_ID = 11`, `GRABBER_COUNT = 12`; layout slots
  7/8 and 8/9) and `bed_icon_count` went 8 -> 9. All uses are symbolic; the picking-id budget
  is 36 * 12 = 432 < `SceneRaycaster::EIdBase::Volume` (1000).

**Decision taken in `CAD/CadDocument.cpp` (resolved after review):** the port replaced the CAD
Hole feature's local `HoleStdEntry` table with the shared `HoleStandards` module ("the CAD Hole
feature and the mesh tool cannot diverge"), while upstream *improved* that local table in this
range (ISO 10642 countersink angles, ASME `#10-24`, `UNC` designations plus the bare form,
`hole_std_lookup` returning a pointer). Rather than drop either side, upstream's values were
adopted into `src/libslic3r/HoleStandards.{hpp,cpp}` (commit "port(ME-2): adopt upstream's hole
standard seat dimensions"):

- `HoleStandard` gained seat columns `cbore_d` / `cbore_depth` / `csink_pocket_d`, kept row for
  row with upstream's table next to the port's head dimensions (`socket_d/k`, `button_d/k`,
  `csink_d`), so a future upstream table edit is a straight copy into this table.
- `CadDocument::add_hole_standard()` now passes those seat columns (and the canonical
  designation) to `add_hole_styled()`, so the CAD feature behaves as upstream's did.
- Inch rows carry upstream's clearances, the UNC series names, the added `#10-24 UNC` size and
  82 deg seats; `find_hole_standard()` accepts the bare size ("1/4-20") as well, so documents
  written with the old bare designations still resolve.
- `tests/libslic3r/test_holestandards.cpp` covers the seat columns, the UNC/bare lookup and the
  82 deg inch angle.

The port's mesh bore/pocket tool is unchanged in shape (it sizes its seat as head dimension +
its own `sink` setting); the inch sizes it offers now use upstream's ASME numbers.

**Other notable resolutions:** `src/OrcaSlicer.cpp` keeps upstream's refactored CLI preset
resolvers and re-applies the three `[BUILDFIX:VS17.8]` `config_substitution_rule` captures
(still required: the installed toolchain is VS 17.8); `Layer.hpp` takes upstream's
`lslices_separated_component_ids` replacement plus the port's `counterbore_bridge_regions`;
`GCode.cpp`'s support loop keeps upstream's const element while keeping the port's index loop;
`Plater.cpp` keeps upstream's `Sidebar::ensure_mixed_filament` implemented on top of the port's
`Sidebar::apply_mixed_filament`, and `ensure_mixed_filament` now uses it instead of the removed
`create_mixed_filament_from_result`.

**Clipper1 fallout fixed:** `src/slic3r/GUI/Gizmos/GLGizmoSupportZones.cpp` (SU-5) used
`ClipperLib::jtRound` in three `offset_ex` calls; Clipper1 is gone, so they now use the
`jtRound` from `ClipperUtils.hpp`.

## Verification still to do

Committed on `sync/upstream-20261015` and **being built**:
`build_win.bat -ds -j 8` (deps + slicer). Upstream changed `deps/` and removed
GLEW/OpenCSG/Clipper1, so the existing `build/` tree needs a reconfigure - `-s --no-configure`
is not enough. Then:

1. Expect a compile-fix pass anyway. Watch: the `SegmentsUniforms` / `LayerColorsUniforms`
   refactor in `libvgcode`, `do_move(snapshot_type, force_volume_move)` callers in
   `GLCanvas3D`, and the belt/IMEX helpers.
2. `ctest --test-dir build/tests -C Release` (the suites are off by default, so the configure
   has to include them).
3. Off-by-default equivalence: with every port option at its default, the sliced output must
   match stock 2.5. Then the MCP runtime pass per feature.
4. `scripts/run_gettext.bat --full` (the port adds `_L()` strings; upstream churned the
   catalogs).
5. Update `manifest.tsv` for anything upstream has adopted (PF-6's toolpath clipping is now
   upstream's section view; its object-scoped shell clipping is still the port's).
6. Only then merge into `port/integration`, after the user has tested the build.

## Gaps found while resolving

- `porting/scripts/` contains only `.gitkeep`: `verify-markers.ps1`, `apply.ps1` and
  `scripts/excluded/` are referenced by `PORTING_GUIDELINES.md` sections 1 and 8 but do not
  exist, so the marker gate cannot run and the documented reapply route is not runnable.
- Marker hygiene: `Gizmos/GizmoObjectManipulation.cpp` and `Gizmos/GLGizmoMove.*` carry
  `// ORCAPORT: ...` comments without the mandated `[ORCAPORT:<id>]` form; `GLCanvas3D.cpp`
  has an `MV-3` marker with no `MV-3` row in `manifest.tsv` or the handoff. Left as found;
  fix in a follow-up so the merge commit stays a merge.
