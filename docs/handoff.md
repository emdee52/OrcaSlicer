# Handoff — mesh features: holes, cut, snap, rim and edge dress

Session summary so the next session can pick up: what was built, where it lives, and the failure
modes already understood so they are not rediscovered. Work is mesh-only (no CAD reconstruction):
Prepare-tab gizmos that add positive/negative volumes and paint-free edits to a loaded STL. Sections
2-6 are the shared ground; 7 onward is one section per feature, oldest first, ending with the hole
tooling, the rim/edge dress, and the UI-1 toolbar cleanup in section 25.

Living tracker with the full idea backlog lives in `docs/superpowers/ME-roadmap.md` (gitignored).

---

## 0. Merge policy (read first)

**A feature branch is not merged until the user has tested it.** See [`MERGE_POLICY.md`](../MERGE_POLICY.md)
for the full wording. Build it, run the tests, commit, push and report - then wait. A green build and
passing tests are not approval; merge `--no-ff` into the integration branch only after the user
confirms the feature works in the app.

---

## 1. Build / test notes (read first)

Windows, from the repo root:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\bin;$env:PATH"
$env:CMAKE_TLS_VERIFY = "0"
.\build_win.bat -s --no-configure -j 8
```

- The existing configured build is VS 2022 multi-config; direct CMake also works:
  `cmake --build build --config Release --target OrcaSlicer -- -m`.
- **Kill `orca-slicer.exe` before rebuilding** or the link fails with `LNK1104: cannot open
  OrcaSlicer.dll`. The MCP bridge auto-starts the app, so kill it between test runs and builds.
- Tests: `cmake --build build --config Release --target libslic3r_tests -- -m`, then
  `build/tests/libslic3r/Release/libslic3r_tests.exe "[HoleStandards],[HoleDetector],[HoleShapes],[CadDocument]"`.
  The CAD suite is a good regression check for the shared `HoleStandards` refactor.
- `"Failed to download uv: SSL connect error"` during the build is benign (an unrelated venv step).
- Resources (SVG icons) are read live from the repo `resources/` (no `build/resources` copy), so an
  icon change only needs an **app restart**, not a rebuild. The gizmo preloads its icons in
  `on_init`, so a restart is required to pick up a changed icon.
- MCP: the opencode tool list is fixed at session start, so a **newly added MCP tool is only
  callable via raw HTTP** until the session restarts. Pattern used:
  `curl.exe -s -X POST http://localhost:13618/mcp -H "Content-Type: application/json" --data-binary "@body.json"`
  where `body.json` is `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"<tool>","arguments":{...}}}`.
  Write the body to a file; PowerShell strips quotes from inline JSON.

---

## 2. Branches / commits

Everything below is merged into `port/integration` unless its row says otherwise. The ME, CUT, SNAP,
PF, SU and AS branches are done, as are the rim dress and edge dress tools, and the object-to-object
Alt-drag snap (SNAP-8) is the newest of them.

| Branch | State | Contents |
|---|---|---|
| `port/ME-1` | merged (`eac3472582`) | detection, teardrop, gizmo, MCP |
| `port/ME-2` | merged | shared standards, unified Holes tool, all categories |
| `port/CUT-1..3` | merged | face-aligned cut plane, single-part cut, shaped cut |
| `port/SNAP-2..7` | merged | Alt face snapping and its follow-ups |
| `port/RIM-1` | merged (`c39780e219`, `873710376f`) | rim chamfer / fillet ring solids + Holes operations |
| `port/EF-1` | merged (`b6803f53b4`) | Edge chamfer / fillet gizmo, straight edges |
| `port/EF-2` | merged | whole-loop (rim) mode, mitred loops, applied-state marks |
| `port/EF-4` | merged | irregular rims: crease-bounded regions, round-over and hidden-side picks |
| `port/SNAP-8` | merged | Alt-drag object-to-object point snap (see section 21) |
| `port/MV-1` | merged (`f6f42220e1`) | Move gizmo: axes align to a picked flat face |
| `port/MV-2` | merged (`56b5a6acba`) | Move pivot sticks to face coordinates; Alt lands it on another object (section 26) |

ME-1 commits: `3166fa8d39` (detector + shaper + tests), `72eccfdba9` (teardrop gizmo),
`9b68a49315` (MCP `find_holes`, nested-hole fix, partial-bridge pass later removed),
`cfccddb6ea` (angle/colors, superseded), `efa59827b2` (teardrop-only + preview depth-mask fix).

ME-2 commits (oldest first): `b309f93851`, `6a920ee4d1`, `afd0aa5b9e`, `db3dddc39b`, `a2c5ba9ca0`,
`f7439b423f`, `5993585a3f`, `1e9809e58a`, `9e3a47cdf3`, `4a404d4c6d`, `2cfa6412e3`, `c3faed5f2a`,
`7e5f304b78`, `0a03d1d23a`, `3085e98a73`.

RIM commits: `5ebd292061` (ring solids + tests), `dd053d811c` (Holes operations), `e7ab607390`
(rim size field width).

EF commits: `acd674d61b` (edge prism solids + tests), `df14bb9931` (gizmo), `7394150a7f` (follow
cursor, merge collinear segments), `621ecd7bed` (screen-space pick, edge cache), `75ca053842` (wider
grab); then on `port/EF-2`: `7325ad0776` (loop solids + tests), `602fffcf9a` (whole-loop mode),
`ca02ef4629` (mitred loop sweep, hidden-edge depth test, hover only on motion, size slider),
`1f8c897a62` (refuse slivers of smooth surfaces), `5bee105c1d` (applied marks, no re-dress,
right-click removal), `0f14403ee1` (reset preview models before rebuilding). Then on `port/EF-4`:
`ab4c05125e` (ring the search out to neighbouring regions, MCP `hover_at`), `2438eba6a4` (drop the
screen-radius cap when picking a loop), `a6f298b09e` (collapse micro-steps when sweeping a rim),
`656714103a` (grow a region across creases instead of by a normal tolerance), `f3fddd1cda` (40°
region join, reject a candidate loop that is mostly hidden). `fd9cd9c184` (a tolerance ladder) is on
the branch too but was superseded by `656714103a`.

---

## 3. What ships

### libslic3r (mesh-only, no CAD dependency)
- `src/libslic3r/HoleDetector.{hpp,cpp}` — axis-agnostic cylinder/hole detection on an
  `indexed_triangle_set`. Patch growth by **vertex adjacency**, PCA-of-normals axis, Kasa circle
  fit, angular coverage, cap **flood-fill** for through/blind. Returns
  `DetectedHole{axis, center, radius, depth, through, confidence, facets}`.
- `src/libslic3r/HoleShapes.{hpp,cpp}` — `its_make_teardrop`, `its_make_teardrop_for_hole`,
  `its_make_bore`, `its_make_counterbore`, `its_make_countersink`, `its_make_tube` (shrink),
  `its_make_nut_pocket` (hex prism + clearance bore), plus a shared right-handed `place_on_axis`.
  Also `its_make_rim_chamfer` / `its_make_rim_fillet` (closed ring negatives at a hole rim).
- `src/libslic3r/EdgeProfiles.{hpp,cpp}` — chamfer / fillet cross sections as 2D polygons
  (`chamfer_profile`, `fillet_profile`), `extrude_profile` (closed prism) and `make_edge_chamfer` /
  `make_edge_fillet` for one straight mesh edge. Loop mode: `LoopFrame` (a straight run `p`→`q` with
  the in-face directions `u`,`v`), `its_face_patch_loops` (boundary loops of the crease-bounded region
  around a seed face — neighbours join while their normals stay within `MAX_JOIN_DOT = cos 40°`, so a
  region boundary can only be a real rim, never a step of a round-over), `its_face_patch_loops_around`
  (rings outwards from the seed when its own region carries no loop), `sweep_loop`, `make_loop_chamfer`,
  `make_loop_fillet`.
- `src/libslic3r/HoleStandards.{hpp,cpp}` — shared screw/insert/magnet/nut table + `HoleFit`
  (Tight/Slip) and `hole_fit_diameter_delta`, `screw_nominal_diameter`. Always compiled (works with
  `SLIC3R_CAD=OFF`). `CadDocument::add_hole_standard` consumes it (socket head is the CAD counterbore).

### GUI
- `src/slic3r/GUI/Gizmos/GLGizmoHoles.{hpp,cpp}` — "Holes" tool, `EType::Holes`, shortcut `Ctrl+J`
  (icon `toolbar_holes{,_dark}.svg`). Was "Horizontal holes" until UI-1: only teardrop is horizontal,
  bore/pocket work on any axis. Operation buttons (Teardrop / Bore-pocket / Rim chamfer / Rim fillet);
  Bore reveals category image buttons
  (Screw/Nut/Magnet/Insert/Custom) then the item dropdown and parameters.
  - Teardrop: horizontal holes only, apex 45-60°.
  - Bore: screw bore with optional socket/button/countersink head, heat-set insert, magnet, nut,
    or custom diameter. Tight/Slip fit for inserts/magnets; Free/Tap for screws; editable
    diameter + tolerance; read-only True diameter; editable pocket Depth; Height buttons for
    inserts; Head-fit (Flush / -0.08 / -0.16). Shrinking adds a positive tube.
  - Preview: candidate ghosts blue, applied volumes red, hover green.
  - Icons: `resources/images/hole_cat_{screw,nut,magnet,insert,custom}.svg`.
- `src/slic3r/GUI/Gizmos/GLGizmoEdgeDress.{hpp,cpp}` — "Edge chamfer / fillet" tool,
  `EType::EdgeDress`, shortcut `Ctrl+D` (icon `toolbar_edge_dress{,_dark}.svg` since UI-1; it used to
  borrow the CAD panel's `design_filletedge.svg`, which is 24x24, stroke-only and has no dark
  variant). Chamfer / Fillet icon toggles with the name on hover (section 29), a
  0.5-3.0 mm size slider with a typed field beside it, and a "Whole loop / rim" checkbox (off by
  default). Straight edges are picked by screen-space distance; loop mode dresses the boundary loop
  of the flat face under the cursor. Applied features are drawn red, are not dressed twice, and a
  right-click removes the one under the cursor.
- MCP: `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` — `find_holes` and `holes_gizmo`
  (`open | status | set_operation | set_category | set_angle | set_standard | set_head |
  set_screw_fit | set_fit | set_diameter | set_tolerance | set_head_fit | set_through | set_depth |
  set_flip | toggle | apply_all | clear_all | refresh | close`). Registered from
  `OrcaMCPServer::register_builtin_tools`. Also `edge_dress_gizmo` (`open | status | set_mode |
  set_size | apply | apply_at | list_edges | apply_edge | remove_edge | applied_edges | set_loop |
  list_loops | apply_loop | remove_loop | applied_loops | clear_all | close`).
- `src/slic3r/GUI/OrcaExt/ObjectSnap.{hpp,cpp}` — Alt-drag object-to-object snap. Hold Alt while
  dragging a selected object in the regular preview and the grabbed point lands on the nearest snap
  feature (face corner / edge midpoint / face centre / edge quarter / face quarter) of the object under
  the cursor. Pure GUI, off unless Alt is held: no app key, no free-Z, byte-identical to stock with Alt
  up. See section 21.

### Tests
- `tests/libslic3r/test_holedetector.cpp`, `test_holeshapes.cpp`, `test_holestandards.cpp`,
  `test_cututils.cpp`, `test_edgeprofiles.cpp`.

---

## 4. Failure modes already understood (do NOT re-fix)

1. **`ImGuiWrapper::scaled(x)` multiplies by the font size** (`x * m_font_size`, ~15). Never pass
   pixel values: `scaled(22)` produced ~330 px category buttons and the gizmo window blew past the
   viewport. Icon sizes use `scaled(1.5)`. This was misdiagnosed once; don't repeat.
2. **Never upload a GL texture inside the ImGui input-window render pass** (e.g. loading an SVG in
   `on_render_input_window`). `glsafe` asserts GL errors in non-release builds, so the window
   aborted and the gizmo stopped opening. Preload icons in `on_init` (same place Orca loads toolbar
   icons).
3. **Hole detection patch growth must use vertex adjacency, not edge adjacency.** A boolean/repair
   leaves a cylinder wall split across T-junctions; edge neighbours break it into arcs that fail
   the angular-coverage gate. This hid the coaxial shaft inside a counterbore.
4. **Cap detection must flood-fill the whole cap**, not just edge-adjacent faces: a triangulated
   blind-hole floor is only ~half edge-adjacent to the wall, so the area threshold missed the cap
   and blind holes read as through.
5. **Negative volumes are 2D Clipper at slice time** (`PrintObjectSlice::slices_to_regions`), not
   3D booleans, and they only clip regions **earlier in the volume list** (order-dependent).
6. **A horizontal through-hole splits a thin plate's layer into islands** — there is no `ep.holes`
   entry, so the painted counterbore-bridging path cannot bridge it. A dedicated horizontal pass was
   written and then **removed at the user's request**; partial bridging is the existing global
   `counterbore_hole_bridging` option only. Do not add a parallel bridging implementation again.
7. **The positive shrink tube perturbs detection** (its own outer wall looks like a hole, shifting
   indices and losing applied state). `detect()` must skip volumes the tool added
   (`POCKET_NAME`-prefixed model parts).
8. **Per-hole applied state is matched by index encoded in the volume name** (`Teardrop#<i>`,
   `HolePocket#<i>`). Proximity matching is ambiguous for coaxial holes (a counterbore and its
   shaft) and deep pockets whose centroid is far along the axis. Do not go back to proximity.
9. **Handedness**: the placement/pick frame must be right-handed (`r = u.cross(a)`). A left-handed
   basis (`a.cross(u)`) inverts face normals and `SceneRaycaster` rejects every click.
10. **Preview ghosts sit inside the hole** and were depth-occluded: `glClear(GL_DEPTH_BUFFER_BIT)`
    is a no-op when the depth writemask is off. Force `glDepthMask(GL_TRUE)` before clearing, then
    draw the overlay with depth testing disabled.
11. **Per-hole parameters are captured at placement** (baked into the volumes). Changing the
    standard/fit/diameter afterwards does not re-cut applied holes; to change one, clear it and
    re-apply. Diameter relabelling is scoped to the current category (a screw is not switched to a
    magnet by typing a magnet diameter).
12. `HoleStandards` insert rows: pocket OD is the **larger** of the kit's top/bottom diameters;
    inserts are one row per size with a list of heights (buttons map to the editable depth).
13. Users' 3mf may carry `counterbore_hole_bridging = partiallybridge` globally, which bridges all
    holes regardless of this tool.
14. **`GLModel::init_from` builds a model once and then returns early** (it wants an explicit
    `reset()` first). A preview that is rebuilt without resetting keeps the geometry it was first
    built from, which looks like "only the first applied feature is highlighted" and like a hover
    highlight that sticks to an old edge. Reset every preview model at the top of the rebuild, the
    way `GLGizmoHoles::rebuild_previews` does.
15. **A patch or edge whose boundary runs along a smooth surface is a sliver, not a rim.** One quad
    of a cylinder wall is coplanar only with itself, so it looks like a face patch, and sweeping a
    section around it gives a slab or a wedge instead of a chamfer. `its_face_patch_loops` refuses a
    run whose crease with the neighbouring face is below ~15°, and `collect_edges` keeps a boundary
    out of the single-edge list when it only bends (< 20°) at both ends and has a near-coplanar
    crease. Hovering a hole wall used to produce exactly that slab.
16. **A screen-space nearest candidate needs a depth test.** Without one, an edge on the far side of
    the model wins over the edge under the cursor (the highlight jumps through the solid). Compare
    view-space depth against the ray hit and reject candidates more than ~0.5 mm behind it.
17. **A region grown by a normal tolerance steps on and off round-over facets.** A rim that crosses a
    small round-over (a chamfered jack-o'-lantern face) gets a boundary that walks a few shallow
    (23-31°) round-over facets, so the loop jogs sideways and the swept band doubles over. Growing the
    region by crease angle instead (`normal · normal >= cos 40°`) leaves those facets inside the
    region, so its boundary is only ever a real rim (measured on `lego-head-4.3mf`: mouth loop minimum
    crease 32° → 54°, shallow segments 39 → 2). A tolerance ladder cannot fix this, and neither can a
    change to the sweep.

---

## 5. Key code locations

- Detection / shapes: `src/libslic3r/HoleDetector.cpp`, `HoleShapes.cpp`.
- Edge dress geometry: `src/libslic3r/EdgeProfiles.cpp` (profiles, prism sweep, crease-bounded region
  loops).
- Shared data: `src/libslic3r/HoleStandards.cpp` (table + fit), `src/libslic3r/CAD/CadDocument.cpp`
  (`add_hole_standard` consumes it).
- Gizmo: `src/slic3r/GUI/Gizmos/GLGizmoHoles.cpp` (detect, feature_frame, bore_negative_mesh,
  bore_tube_mesh, rebuild_previews, on_render_input_window). Registration in
  `GLGizmosManager.{hpp,cpp}` (`EType::Holes`) and `src/slic3r/CMakeLists.txt`.
- Gizmo: `src/slic3r/GUI/Gizmos/GLGizmoEdgeDress.cpp` (collect_edges, extend_feature_edge,
  update_hover, loops_for_facet, mesh_for_edge/loop, rebuild_preview). Registration in
  `GLGizmosManager.{hpp,cpp}` (`EType::EdgeDress`), `src/slic3r/CMakeLists.txt`.
- MCP: `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp`.
- Icons: `resources/images/hole_cat_*.svg`; magnet reuses the `plate_snapdrag.svg` horseshoe path.

---

## 6. Open items / likely next

- EF-3 (miter corner vertices between separate straight-edge sweeps) was investigated and closed as
  redundant — see section 20. The fillet-corner case (the one unverified part) is now user-confirmed
  correct; SNAP-7 is user-tested and merged.
- Per-hole parameters are not restorable/editable after placement (only clear + re-apply).

---

## 7. CUT-1 — align the cut plane to a picked face (branch `port/CUT-1`, pushed)

Built after ME-2 was merged (`port/integration` @ `cfc22945a8`). Independent of the holes work.

Commits: `df87f01c06` (helper + test), `49431c3c6b` (gizmo + MCP), `242e670f48` (hover highlight
+ `pick_face`), `07bdf43b61` (coplanar-only highlight + `hover_face`).

- **`facet_normal_in_world`** (`src/libslic3r/CutUtils.{hpp,cpp}`) — world-space outward normal
  of a mesh facet via the inverse-transpose of the object-to-world linear part; correct under
  non-uniform scale and mirrors. Tested in `tests/libslic3r/test_cututils.cpp` (perpendicularity
  property + exact value + out-of-range fallback).
- **`GLGizmoCut3D`** — "Pick flat face" button (planar mode) toggles a pick mode; the next left
  click raycasts the selected object's volumes with `GLVolume::mesh_raycaster`/`world_matrix()`,
  takes the closest hit and its world normal, then sets the plane orientation
  (`Geometry::rotation_from_two_vectors(UnitZ, n)`) and the center to the click point. Right-click
  cancels; snapshot "Align cut plane to face". `m_cut_normal` now defaults to `UnitZ` (it was
  read before first `update_clipper()`).
- **Hover highlight** (`242e670f48`, refined by `07bdf43b61`) — while pick mode is on,
  `on_render` raycasts on each frame and draws a translucent patch over the facets **coplanar**
  with the hovered facet, grown by edge adjacency. Coplanarity is the measure tool's component-wise
  normal equality (`|Δn| < 0.001`, `Measure.cpp::is_same_normal`), so adjacent faces at even a
  shallow angle are not merged (a 20° normal cone merged a chamfer with its wall — do not loosen
  it). `render_follows_cursor()` disables frame skipping so it tracks the cursor; adjacency/normals
  are cached per volume. The raycast respects the cut clipping plane (only the visible half is
  pickable). `coplanar_region()` is the pure growth helper.
- **MCP `cut_gizmo`** (`OrcaMCPGizmoTools.cpp`) — `open | status | set_plane_normal |
  set_plane_center | shift_cut | set_mode | set_keep | flip | reset | apply | pick_face | hover_face
  | close`; public control surface on `GLGizmoCut3D`. `apply` is wrapped in
  `McpDialogSuppressionGuard` (the non-manifold repair dialog would deadlock the main-thread call).
  `pick_face` runs the same raycast-and-align path at `screen_x`/`screen_y` (default: viewport
  centre). `hover_face` reports the facet under a screen point and how many coplanar facets its
  highlight covers.
- **Verified**: `[CutUtils]` tests pass; MCP end-to-end on a 20 mm box (normal +X, center on the
  +X face, shift −10 → two 10 mm halves); on `Cut tool testing.3mf` a `hover_face` sweep shows each
  face as its own region (flat top 52/302 facets, vertical wall 2, 45° chamfer 2, a ~14° face 2) —
  no cross-face merging. Highlight rendering and the click itself are user click-tests.
- **Design note**: the measure tool's `Measure::Measuring` face grouping was studied but is not
  needed for the plane itself — the plane uses the clicked point and a single facet's plane. The
  hover highlight borrows the AlignStack patch-growth idea instead; `Measure`'s plane GL model was
  not reused.
- **Not done**: CUT-3 shaped "cookie cutter" split (cut the object into shaped pieces). Needs a
  profile→prism boolean split plus `Cut::post_process` integration. This is what the user meant
  by "keep section shapes". (CUT-2 is now the single-assembly-part cut — see section 8.)

### Shading fix (`5007ef7949`)

- **Symptom** (user): in the Cut tool, orbiting the camera made whichever side faced the camera
  look dark while other sides stayed bright.
- **Cause**: `GLGizmoCut3D::PartSelection::render` draws the split parts with the `gouraud_light`
  shader but never set `view_normal_matrix`. The real object volumes are hidden while the parts are
  drawn, so the shader kept a **stale** normal matrix from a previous camera/frame; the lighting
  appeared frozen in object space and whichever side was orbited to face the camera looked dark.
  Upstream bug (present in `main` too), not a fork regression. The same omission was in
  `GLGizmoCut3D::render_model` (connector/part markers) and `GLGizmoBrimEars` (brim-ear markers).
- **Fix**: set `view_normal_matrix = view_linear * model_part_linear^{-T}` per part/marker, the
  same inverse-transpose form `3DScene.cpp` uses for the main object.
- **Not the cause**: the main prepare-view object shader (`3DScene.cpp`) already sets the normal
  matrix; the cut cross-section (`MeshClipper::render_cut`) uses the unlit `flat` shader. The shadow
  map and outline are upstream features.
- **Caveat**: not visually confirmed end-to-end (the fix was built and code-verified; the user
  should re-check). If the symptom persists on the *main* object (not the cyan/magenta cut parts),
  the remaining suspect is the upstream shadow map.

---

## 8. CUT-2 — cut a single part of a multi-part object ✅ DONE (branch `port/CUT-2`, pushed)

Cut **one part** of a merged "Assembly" object without cutting the whole assembly. Full design +
milestone tracker: `docs/superpowers/CUT-2-single-part-cut-plan.md` (gitignored); summary in
`docs/superpowers/ME-roadmap.md`.

- **Branch**: `port/CUT-2`, stacked on `port/CUT-1`. Both are now merged `--no-ff` into
  `port/integration` (CUT-1 `08a73dd0d3`, CUT-2 `353e64f18c`, pushed).
- **UX**: select a part in the ObjectList/canvas, then open Cut; only that part is cut; the assembly
  stays **one object** with the part replaced by its two pieces; the preview shows only the
  selected part.
- **Commits**: `bc00ed12ba` (MCP startup restore-prompt auto-decline), `dda15801eb` (**M1**:
  `Cut` volume filter + `process_untouched_volume` + tests), `13acddef21` (**M2+M3**: gizmo
  activation/apply/UI/preview + MCP `set_part`).
- **Library API** (M1): `Cut(object, instance, cut_matrix, attributes, const std::vector<int>&
  cut_volume_idxs = {})`. Empty = old behaviour (cut all parts); non-empty = only those model
  parts are cut and the rest are carried into the single result object untouched. **Requires
  `KeepAsParts`.**
- **M2 (gizmo)**: `on_is_activable()` also accepts `is_single_model_part_selection()` (one
  `MODEL_PART` volume of a multi-part object — modifiers/connectors excluded). `m_cut_volume_idxs`
  is derived from the selection on activate and in `data_changed`. `perform_cut` forces
  `KeepUpper|KeepLower|KeepAsParts|KeepPaint|InvalidateCutInfo`, drops connectors/place/flip and
  invalidates the result `cut_id`. Preview: the other volumes are hidden via
  `toggle_selected_volume_visibility`, and `ObjectClipper` gained a `set_volume_filter()` so only
  the selected part's cross-section is drawn. UI shows "Cutting part: <name>" and disables the
  meaningless controls (mode combo, connectors, after-cut, cut-to-parts is shown forced on).
  Right-click contour mode is suppressed in single-part mode.
- **M3 (MCP)**: `cut_gizmo` gained `set_part` (`part` = model-volume index, `-1` = whole object)
  and an optional `part` on `open`; `status` reports `part`, `single_part` and `parts`
  (index + name). `set_part` changes the volume selection, so `on_is_activable` is what makes a
  part openable.
- **Verified**: `[CutUtils]` 8 cases / 26 assertions (added a filtered-cut paint-preservation
  case). MCP end-to-end on a hand-built two-cube assembly: `set_part 0` scopes the plane to that
  part (center x=128 vs 143 for part 1); `apply` yields one object with 3 parts
  (`_A`, `_B`, untouched `_1_2`), untouched part world bbox unchanged; whole-object apply still
  produces two objects (stock). User click-test passed (activation, plane sizing, preview,
  result).
- **Watch-outs kept**: `reset_extra_facets()` wipes paint on all volumes — `KeepPaint`+`finalize`
  remap restores the untouched part (now tested); connectors are disabled in single-part mode; a
  part is object-level so all instances of the object get it cut; the result `cut_id` is
  invalidated so it is not a restorable parametric cut.

## 9. Restore-prompt automation (`bc00ed12ba`)

- The startup "restore unsaved items" modal blocked MCP runs (it is shown before any tool call, and
  the per-tool dialog suppression defaults Yes/No to *Yes*, so it could not be used). Now
  `GUI_App::is_mcp_enabled()` (env `ORCA_EXT_MCP` or app key `orca_ext_mcp`) makes the restore
  handler skip the modal and treat it as **No**, which removes the stale backup just like clicking
  No. No need to delete `last_backup_path` by hand anymore.
- Verified: with a dead-lock backup present, a new MCP-enabled launch started normally and the
  backup dir was removed by the handler.

---

## 10. CUT-3 — shaped "cookie cutter" split ✅ DONE (branch `port/CUT-3`, pushed)

Split an object with a closed profile centered on the cut plane instead of a half-space: the
keep-upper piece is the part **inside** the cutter, the keep-lower piece is the body **outside** it
(a matching shaped hole). Profiles are built-in primitives — circle, square, hexagon.

- **Branch**: `port/CUT-3` off `port/integration` (`353e64f18c`). Merge `--no-ff` into
  `port/integration`.
- **Commits**: `c5c142d30d` (**M1** library + tests), `c17eed20ac` (**M2+M3** gizmo mode/UI/apply +
  MCP), `9157b0b926` docs, `d2832ade96` preview/drag/clipper UX, `9ca1b8a26a` crash fix (ObjectClipper
  plane left null in shape mode), `d2d74bc9db` carry over the parts the shape misses,
  `1749c97350`/`f7783fa654`/`04f14383b6` Shape follow-up (below).
- **Library** (`CutUtils.{hpp,cpp}`):
  - `perform_split(std::function<...>)` — shared driver extracted from `perform_with_plane`; a
    splitter fills the keep-upper/lower objects for one solid volume and sets `ok=false` on failure,
    which aborts the whole cut and leaves the model untouched. `perform_with_plane` now just supplies
    the plane `process_solid_part_cut` splitter (behaviour unchanged — old tests still pass).
  - `perform_with_shape(const TriangleMesh& cutter)` — splitter runs `MeshBoolean::mcut::make_boolean`
    (`INTERSECTION` -> inside, `A_NOT_B` -> outside) per model part, merges the returned pieces, and
    reuses the plane path's post-processing (KeepAsParts single object, `post_process`, paint remap,
    `finalize`). Returns **empty** if either side is empty (boolean failed / shape misses the part).
  - `make_cookie_cutter(CutShapeKind, size, half_height)` — closed origin-centred prism in cut space:
    `its_make_cylinder(r, 2H, 64)` (circle), `its_make_cube` (square), `its_make_cylinder(size/sqrt3,
    2H, 6)` (hexagon, across-flats = size). No star/polygon helper needed for this profile set.
- **Gizmo** (`GLGizmoCut`): `CutMode::cutShape` appended (index **2**, so 0=Planar / 1=Dovetail are
  unchanged); `m_shape_kind` (0 circle, 1 square, 2 hexagon), `m_shape_size`. Shape UI adds a profile
  combo + size slider, keeps *Cut position* / *Pick flat face*, and disables connectors and *Place on
  cut* (keep / cut-to-parts / flip still apply). Preview is **outline only** (`render_shape_outline`,
  a cached `LineLoop`, run only while shape mode is active) — the boolean runs on Apply. The clipper
  cross-section is disabled in shape mode. Single-part selection reuses the CUT-2 volume filter.
  `perform_cut` captures the mode before `reset_all_gizmos` and dispatches to `perform_with_shape`;
  on an empty result it warns and returns without touching the model (the undo snapshot is still
  pushed — minor).
- **MCP**: `cut_gizmo` gained `set_shape` (`shape_kind`, `shape_size`; also switches to mode 2),
  `status` reports `shape_kind`/`shape_size`, mode description is now "0=Planar, 1=Dovetail, 2=Shape".
- **Tests**: `[CutUtils]` was 12 cases / 55 assertions at M1-M3 (14 / 63 after the follow-up below). New: cutter prisms are closed/centred;
  a cylinder cutter splits a cube into an inside plug (~1568 mm³, a 64-gon) + outside body with the
  volumes tiling the cube; single-part shape cut leaves the other part untouched; a shape that misses
  the   object returns no objects. The split test also asserts the pieces are closed
  (`its_num_open_edges == 0`) and have positive volume.
- **MCP end-to-end** (two-cube assembly, `twoparts2.3mf`):
  - single-part circle D6 on part 0 -> one object with `Object_1_A` (vol 282.29 = 64-gon r3 x h10),
    `Object_1_B` (717.71 = 1000-282.29), untouched `Object_1_2` (1000).
  - whole-object hexagon D20 -> two objects, inside 750 each + outside 250 each (exactly the
    `2.5x10x10` slab cut by the hexagon flat), total 2000 = the two cubes.
- **Shape follow-up** (`1749c97350`, `f7783fa654`, `04f14383b6`):
  - the cutter preview is rotation-proof: `shape_half_height()` is the max distance from the plane to
    the bbox corners (not a projection along the normal), and `init_picking_models()` builds the
    pickable prism from the depth-aware `make_cut_shape()`, so it always pierces and matches the
    applied boolean.
  - Shape mode drops the planar cross-section and plane part-preview; instead it highlights only the
    region the shape will cut (`object ∩ cutter`). `render_shape_highlight()` runs the mcut
    `INTERSECTION` in the cut frame (no-offset instance matrix), bakes it back with
    `translation_transform(m_plane_center) * m_rotation_m`, and draws it as a translucent cyan
    overlay with polygon offset. It is cached by a signature over plane center/rotation/profile/
    size/through/depth and rebuilt on a 40 ms throttle so it tracks dragging.
  - *Through* checkbox (default on) + *Depth* slider: `make_cookie_cutter(kind, size, z_min, z_max)`
    builds a one-sided prism reaching `depth` into the object (side picked from the bbox-vs-plane
    normal), so the cut is a blind pocket whose plug is only `depth` tall. MCP `set_shape` accepts
    optional `through`/`depth`.
  - tests: `[CutUtils]` is now 14 cases / 63 assertions (adds the per-volume carry-over and the
    depth-limited pocket cases).
- **Caveats**: mcut needs closed manifold input; open meshes can still return garbage rather than an
  empty result, so the existing non-manifold repair dialog in `perform_cut` still applies. Modifiers
  reuse the plane-based `process_modifier_cut`. Paint remap is the CUT-2 simplification. `cut_id` is
  invalidated (the profile is not stored) — not a re-editable parametric cut.

## 11. ME-2c — place a bore/pocket on any flat face

Branch `port/ME-2c` off `port/integration`, merged into `port/integration` (`f4c2a25fb5`). Until now the Holes tool could only rework holes
`detect_holes()` found; ME-2c lets it author a new feature on any flat face the user clicks.

- **Shared face picking** (`refactor`, commit `edd343e289`): the Cut gizmo's
  `raycast_object_face` and `coplanar_region` (edge-adjacency flood fill with component-wise
  normal equality) moved to `src/slic3r/GUI/Gizmos/GLGizmosCommon.{hpp,cpp}` as free functions
  (`raycast_object_face`, `coplanar_region` + `FaceRegionCache`, `build_coplanar_patch`).
  `GLGizmoCut3D` now delegates to them; behaviour is unchanged (its face-pick state shrank to
  `m_face_highlight` / `m_hover_volume` / `m_hover_facet` / `m_face_cache`).
- **Through depth** (`feat`, commit `3c2d0e714f`): `hole_through_depth(its, entry, dir, margin)` in
  `HoleShapes.{hpp,cpp}` ray-marches an `AABBMesh` from the entry point to the farthest hit along the
  into-material direction, plus margin. Returns 0 when the ray leaves, so callers fall back to the
  blind depth. Tested in `tests/libslic3r/test_holeshapes.cpp`.
- **Gizmo** (`feat`, commit `649570888c`): a *Place on face* toggle in the Holes input window. While
  active the gizmo raycasts the selected object every frame and draws a translucent cyan patch over
  the whole coplanar face under the cursor, plus a hover-coloured ghost of the feature that would be
  placed. A left click places the current bore/pocket family (plain / counterbore / countersink /
  hex nut / magnet / insert, driven by the existing parameter UI) and stays in face mode so several
  can be placed; right-click exits. Teardrop is offered only on near-vertical faces (it degenerates
  on a horizontal one, where the code falls back to a plain bore).
- **Placement math**: `facet_normal_in_world` gives the outward normal; the feature grows opposite
  it. A synthetic `DetectedHole` (axis = into-material direction, `center = hit + dir*depth/2`) is fed
  to the existing mesh builders, so no new mesh code was needed. The `flip` control is neutralised for
  placed faces (the entry is fixed by the picked surface), and a through feature uses the ray-marched
  depth.
- **Storage**: placed features are negative-only `FacePocket#<id>` volumes with their own monotonic
  id, so they survive `detect()` (which wipes the detected-hole list) and keep a stable name. They
  are not tied to a detected hole and are not re-editable — the geometry is baked at placement, same
  limitation as ME-2. `clear_all` removes them too.
- **MCP**: `holes_gizmo` gained `set_place_face` (`on`), `place_face` and `hover_face`
  (`screen_x`/`screen_y`, defaulting to the viewport centre, mirroring `cut_gizmo`). `status` now
  reports `place_face_mode`, `face_pocket_volumes` and the `placed_faces` list.
- **Verification** (box STL, MCP): `hover_face` at the viewport centre returned facet 6, normal
  `[0,-1,0]`, region 2 triangles; `place_face` added one `FacePocket` volume with axis `[0,1,0]`
  (into the material) and depth 21.25 (20 mm box + 1.25 margin), and stayed in face mode; `refresh`
  kept the placed feature; `clear_all` removed it; slicing the object with the pocket completed with
  no warnings.
- **Caveats**: the negative is a 2D Clipper region at slice time, so the usual volume-order rule
  applies. A placed feature is not re-editable. The coplanar patch is rebuilt only when the hovered
  `(ModelVolume*, facet)` changes, while the ghost follows the cursor within a facet (see section 12).

## 12. ME-2d — scale-correct dimensions and a cursor-following ghost

Found while testing ME-2c, on branch `port/ME-2d` off `port/integration`; merged into `port/integration` (`d7aa104236`).

- **Symptom 1 — holes scaled with the object.** An M2 hole (tap_d 1.70 mm) measured 8.46 mm on a 5x
  instance, and every standard (head dims, nut across-flats, magnet/insert pockets) was affected.
  Cause: `bore_negative_mesh` etc. build a **1.70 mm cylinder in object space**, and the resulting
  sibling volume with an identity matrix is then scaled by the instance matrix. Fix: `object_scale()`
  returns the uniform scale of `instance_matrix().linear()` (mean of the three column norms; a circle
  cannot stay exact under a non-uniform scale), and every user/standard mm dimension is divided by it
  before building object-space geometry — bore diameter, head socket/button/countersink dims, nut
  across-flats and clearance, blind `m_depth` and `m_head_fit` sink. Detected `h.radius`/`h.depth` are
  already object space and are untouched. This applies to both the detected-hole and place-on-face
  paths. `m_diameter` stays world mm for the UI and `true_diameter`. The MCP `holes_gizmo` status now
  reports `object_scale` and, per placed face, `diameter_world` / `depth_world` (the object-space
  `diameter`/`depth` are also kept).
- **Symptom 2 — the ghost stuck to the face.** `update_face_highlight` skipped its work whenever the
  `(ModelVolume*, facet)` was unchanged, but a flat face is a few large triangles, so the ghost
  froze at the first hit of each triangle and only jumped when crossing a triangle edge. Fix: the
  coplanar patch is still rebuilt only per facet, but the ghost is rebuilt whenever the hit point
  moves more than `max(1e-4, 0.01 * diameter)` (object space). The click now uses
  `get_local_mouse_position()` so placement and hover share one coordinate space.
- **Verification**: on a 3MF whose `<item>` transform was scaled 5x (`get_object_info` reports
  scale 5.0, bbox 100 mm), placing M2 yields object-space diameter 0.34 and `true_diameter` 1.70,
  i.e. a 1.70 mm world hole. Unscaled objects are unchanged (scale 1).

## 13. SNAP-1 — Alt-snapped hole and cut placement

With Alt held, the Holes tool's place-on-face mode and the Cut tool's "pick flat face" move the
picked point onto a coordinate of the face the cursor was on when Alt was pressed — a corner, an edge
midpoint or the face centre. Without Alt the raw raycast hit is used, so the default behaviour is
unchanged.

**Geometry** lives in `libslic3r/CutUtils.{hpp,cpp}` as `face_snap_points(its, region)`, so it can be
unit tested without the GUI. It takes the coplanar facet region the GUI already computes
(`coplanar_region`), counts undirected vertex pairs over that region, keeps the edges that occur once
(boundary edges), walks them into loops and treats the largest-area loop as the outer perimeter. It
then emits every loop vertex as `Corner`, every edge midpoint as `EdgeMid`, and one `FaceCenter` at
the shoelace area centroid — the centroid, not the vertex average, which lands outside a non-convex
loop. `face_plane_axes` supplies the deterministic in-plane basis; the align tool's
`build_plane_axes` now delegates to it.

Two guards keep curved surfaces honest. A loop that leaves the plane through its first vertex by more
than `max(1e-3, 0.005 * diagonal)` rejects the whole region, because a staircase of parallel facets
can otherwise produce a meaningless loop; and a region with no boundary edges (a closed patch)
returns nothing. Both cases make the caller fall back to the raw hit.

**Picking** is `nearest_face_snap(pts, project, screen_pos, max_px, out)`: candidates are projected
to device pixels by the caller's projector and the nearest one within 8 px wins. Candidates are
emitted in priority order and only a strictly closer one replaces the current winner, so a tie goes
to `Corner` over `EdgeMid` over `FaceCenter`.

**Wiring.** The GUI side of `GLGizmosCommon` keeps `world_to_screen` and a thin
`build_face_snap_points(mv, region)` wrapper over the volume's mesh. `GLGizmoHoles` caches the
candidates per `(ModelVolume*, facet)`, snaps inside `snap_face_hit` (used by both
`update_face_highlight` and `place_face_at`), converts the winner into object space with
`mv->get_matrix()`, and stores the active kind in `m_hover_snap` so the ghost is rebuilt when the
snap target changes. `GLGizmoCut3D::pick_face_at` builds the candidates per click and passes the
snapped world point to `apply_plane_orientation` and `set_center`; the cut tool works in world space,
the holes tool in object space.

Alt is the modifier because the canvas already uses Ctrl for additive selection and Shift for
rectangle selection. The Measure tool does not snap. The candidate set started with corners, edge
midpoints and the face centre; the quarters were added afterwards, in section 15.

- **Verification**: the geometry is covered by `[CutUtils]` cases in `tests/libslic3r/test_cututils.cpp`
  (corners, edge midpoints, quarters and centre of a square face; nothing for a closed cube; nearest
  candidate within tolerance). Manually, holding Alt over a square face makes the ghost — or the cut
  plane centre — jump to the nearest of those coordinates.

## 14. SNAP-2 / SNAP-3 — adaptive stick distance, snap markers and two follow-ups

**The stick distance scales with the face.** `nearest_face_snap` no longer takes a fixed tolerance:
it projects the candidates once, finds the nearest and measures the gap to the next nearest. Half
that gap is the largest distance that can never be ambiguous — the stick region ends where the next
candidate's begins — so it is the tolerance, clamped to `[8, 48]` px. A big face therefore sticks
from further away, a small one stays tight, and zooming out tightens both without any extra code.
The chosen tolerance is reported through `tolerance_px`.

**Hysteresis** keeps the picked target stable: once a candidate is locked, the tool holds it until
the cursor moves more than `1.25 * tolerance` past it, so the ghost no longer flickers between two
neighbouring points while the cursor sits between them. The lock is dropped when Alt is released, the
pick fails, the face changes, or the cursor leaves the session face (see section 16).

**Snap markers.** While Alt is held, every candidate is drawn as a small sphere — one
`its_make_sphere(radius, PI / 12.0)` per candidate, merged per kind with `its_merge` into one
`GLModel` per `FaceSnapKind`, with the active target as a larger sphere on top. The radius is
face-relative (1.2 % of the largest candidate-to-candidate distance), corners are magenta, edge
midpoints teal and the face centre cyan, mirroring the sketch tool's inference hints. They are built
once per session face, not per frame, and drawn with depth testing off because the surface and the
lifted coplanar patch would otherwise hide them.

**The Cut tool snaps while moving.** `snap_plane_center` is the only snap implementation and it is
called from `GLGizmoCut3D::set_center`, which every way of moving the plane goes through: the
grabber drags, the position field, the arrow keys and the MCP setter. The shaped cut is built from
`m_plane_center`, so it snaps along with the plane. The Alt block that SNAP-1 had put inside
`pick_face_at` was removed in favour of this single path. The whole helper is Alt-gated, so a typed
position stays literal and a drag without Alt behaves exactly as before.

**Leaving the face.** In the Holes tool the face highlight, the ghost and the marker spheres used to
disappear the moment the cursor stepped off the face, which made coming back over an edge a race.
They now survive `FACE_LEAVE_GRACE_SEC` (0.35 s) after the raycast misses and are cleared once that
window expires; any successful hit resets the timer. In the Cut tool the markers are drawn whenever
Alt is held, not only while a face is being picked, so they also appear during a drag.

- **Verification**: `[CutUtils]` covers tolerance growth with candidate spacing, both clamps, the
  reject gate and the SNAP-1 cases. The builds producing `build/src/Release/orca-slicer.exe` are
  clean.
- **Branch state**: SNAP-2 is merged into `port/integration` (`99ecff2432`); SNAP-3 is merged into
  `port/integration` (`2e265a0d88`); SNAP-4 is on `port/SNAP-4` and SNAP-5 stacks on it, both unmerged
  until the user has tested them (see section 0).
- Still not implemented: snapping in the Measure tool.

## 15. SNAP-4 — edge quarters and face quarters

**Two more kinds.** `FaceSnapKind` gained `EdgeQuarter` and `FaceQuarter` after `FaceCenter`, so the
existing values (and the marker colours indexed by them) are unchanged. An edge quarter sits at a
quarter and three quarters along a boundary edge, that is midway between a corner and the edge
midpoint; a face quarter sits midway between the face centre and a corner. On a square face the
candidate list grows from 9 to 21: 4 corners, 4 edge midpoints, 8 edge quarters, 4 face quarters and
the centre.

**Priority is unchanged and still by emission order.** `face_snap_points` emits corners, edge
midpoints, the centre, then the edge quarters and finally the face quarters — matching the enum
order — and `nearest_face_snap` only replaces the winner with a strictly closer candidate, so a tie
resolves to the higher-priority kind. Markers are drawn from one `GLModel` per kind, so the two new
kinds simply widen `m_snap_markers[3]` to `[5]` and `KIND_COLOR[3]` to `[5]`: edge quarters are
green, face quarters pale blue.

**Effect on the stick distance.** Quarters sit between the old candidates, so the gap to the next
candidate is smaller and the adaptive tolerance tightens on the same face; the `[8, 48]` px clamp is
unchanged. This is the intended behaviour — a denser set of targets sticks from closer.

- **Verification**: `[CutUtils]` now covers the 21-point square face with the count and position of
  every kind, the emission order, a pick that lands exactly on an edge quarter and on a face quarter,
  and the tightened tolerance. 126 assertions in 19 test cases pass.
- **Branch state**: on `port/SNAP-4`, unmerged until the user has tested it (see section 0).
- Known ceiling: a curved face whose boundary is approximated by many edges (a drilled hole rim, a
  high-poly cylinder cap) emits two edge quarters and a face quarter per edge, so a 64-gon rim gives
  about 320 candidates. They are built once per session face while Alt is held, which is acceptable,
  but a spacing threshold could trim them if a heavy scene ever shows it.

## 16. SNAP-5 — one face per Alt press, no tap freeze, quiet place mode

Three follow-ups on the Alt snap, all in the Holes tool plus the same gate in the Cut tool.

**One face per Alt press.** The snap used to re-target whenever the cursor crossed onto another face
while Alt was held, which reads as "it snapped to a face I am not pointing at". The session is now
gated to the flat face under the cursor on the first Alt frame (`m_snap_mv`/`m_snap_region` hold both
the gate and the candidate cache key; the Cut tool reuses
`m_snap_points_mv`/`m_snap_points_region` for the same purpose — the gate is the coplanar region
rather than the raycast triangle, see section 17). While Alt stays down the hole keeps following the
cursor onto other faces — it simply never snaps there — and the marker spheres stay drawn on the
session face. Releasing Alt ends the session, so the next press picks whatever face the cursor is
on. The gate only applies while Alt is held, so the default paths are untouched.

**The session is dropped on the Alt release edge.** The ghost is rebuilt only inside `update_face_highlight`,
and only when the hit moved or the snapped kind changed; that made a brief Alt press able to leave
the hole pinned to a coordinate until the next mouse move. `on_render` watches the Alt edge while in
place mode and, on release, calls `clear_snap_session()`: the lock, the active marker, the session
face and the cached hit are dropped, and `m_last_face_hit` is set past any real coordinate so the
ghost is rebuilt from the raw cursor position on the very next pass. `exit_place_face_mode` uses the
same helper. A tap whose release edge never arrives is handled in section 17.

**Place mode is quiet.** The detected-hole previews — candidates, applied features and the hovered
hole — are no longer drawn while picking a face; they competed with the face highlight and the ghost
for attention. Their raycasters are unchanged and hovering is already suppressed in place mode.

- **Verification**: no unit-testable logic changed (`face_snap_points` and `nearest_face_snap` are
  untouched), so this is a manual click test: with Alt held, cross onto a second face and confirm the
  hole follows it without snapping, that the markers stay on the first face, and that entering place
  mode hides the hole previews. `[CutUtils]` still passes (126 assertions in 19 test cases) and the
  `build/src/Release/orca-slicer.exe` build is clean.
- **Branch state**: on `port/SNAP-5`, stacked on `port/SNAP-4`, both unmerged until the user has
  tested them (see section 0).

---

## 17. SNAP-6 — one flat face across its triangles, and the Alt press

Two defects found while testing SNAP-5, both on the same session gate.

**The gate is the flat face, not the raycast triangle.** A flat face is normally split into several
triangles, so keying the session — and the candidate cache — on the facet under the cursor made the
snap die the moment the cursor crossed onto a sibling triangle: every coordinate on the far side of
the face stopped responding. Both gizmos now keep the coplanar region of the session facet
(`m_snap_region` in Holes, `m_snap_points_region` in Cut) and test membership through
`region_has_facet` (new, in `GLGizmosCommon`), so one Alt press covers a whole flat face and all of
its coordinates stay live. The facet is also gone from the hysteresis lock — the locked coordinate
is mesh space and identical on every triangle of the face — and the `m_snap_lock_facet` member went
with it.

**The Alt press is swallowed while placing.** An Alt tap that nothing consumes reaches the frame and
leaves the placement pinned as though Alt were still held. The Holes gizmo now consumes the Alt
key-down in `GLGizmosManager::on_key` while place mode is on, the same handling BrimEars and Cut
already use. It is scoped to place mode, so Alt keeps its usual meaning everywhere else (rectangle
deselect included).

- **Verification**: the gate change has no unit-testable surface (`face_snap_points` and
  `nearest_face_snap` are untouched); `[CutUtils]` passes (126 assertions in 19 test cases) and the
  build is clean. Manual click test: press Alt on one side of a large flat face and drag across to
  the other side — the far-side corners, edge midpoints and quarters must all snap; then tap Alt
  once without holding it and confirm the hole still follows the cursor.
- **Branch state**: on `port/SNAP-5` (`3b1c8f6bfc`), stacked on `port/SNAP-4`, both merged into
  `port/integration` (`2b323c1aca`) once the user confirmed them (see section 0).

---

## 18. SNAP-7 — one snap target per distinct cut in planar mode

A flat face carries many coordinates — corners, edge midpoints, quarters and the centre — and in
planar mode every one of them cuts the identical plane: the plane is `normal . x = offset`, so moving
its centre in-plane does not move the plane. The Cut tool was showing a marker for each of those
coordinates, so a face flush with the plane became a grid of spheres that all did the same thing.

`snap_points_distinct_cuts` (in `libslic3r/CutUtils`, beside `face_snap_points`) reduces the
coordinates of a face to one per distinct cut. It measures each coordinate's signed distance to the
plane through `to_world` and keeps the first coordinate of every group whose distances are within
`tol` (1e-3 mm) of each other. Input order is preserved, so the existing pick priority (Corner >
EdgeMid > FaceCenter > EdgeQuarter > FaceQuarter) still chooses which coordinate represents a group,
and the representative does not change as the cursor moves across the group.

Planar mode runs the reduced list through the ordinary magnetic snap: `snap_plane_center` caches the
full candidate list as before, then, while the mode is planar, recomputes the reduced list whenever
the plane pose it was derived from (normal and centre) changes, and snaps and builds markers from
that. The spheres that remain are the ones that visibly move the plane; snapping to a dropped
coordinate was a no-op. Shape and tongue-and-groove keep the whole coordinate set: their profile is
built around the plane centre, so there the position does matter. The candidate cache is keyed on the
cut mode as well as on the flat face, so switching modes with Alt held rebuilds the markers.

- **Verification**: `[CutUtils]` passes (143 assertions in 20 test cases), including the new
  "Snap points that cut the same plane collapse to one": a plane through the face and a parallel
  plane 1 mm off both keep a single coordinate, a tilted plane keeps one per depth with the kept
  points a subsequence of the input, and depths closer than the tolerance collapse. Build is clean.
  Manual click test: in planar mode hold Alt over a face flush with the plane — one sphere, and it
  visibly moves the plane; tilt the plane off that face and several distinct spheres must appear.
- **Branch state**: on `port/SNAP-7`, branched from `port/integration` after SNAP-4, SNAP-5 and
  SNAP-6 were merged (`2b323c1aca`). Merged into `port/integration`; the user tested it and confirmed
  it works (see section 0).

---

## 19. RIM-1 / RIM-2 — chamfer and fillet a hole rim ✅ merged

A rim chamfer is only a cone ring, and a rim fillet the same shape with a circular-arc section, so
both are negatives that need no clearance bore and no 3D boolean. `its_make_rim_chamfer` and
`its_make_rim_fillet` (in `libslic3r/HoleShapes`, beside the other shape solids) build them from a
local `revolve_profile`: the 2D section is copied once per angular step, in **profile-major** vertex
order so the triangle indexing lines up, and the winding is fixed against `its_volume`. The section
keeps its first point on the crease, so the ring spans the hole radius outward by `size` on the face
and down by `size` along the wall.

RIM-2 added them to the Holes gizmo as `HoleOperation::RimChamfer` / `RimFillet`: its own operation
buttons, a size field, a preview ghost and applied volumes named `RimChamfer#<id>` / `RimFillet#<id>`
(a chamfer and a fillet cannot both sit on one rim). MCP: `holes_gizmo set_operation rim_chamfer |
rim_fillet`, `set_rim_size`.

- **Two bugs found and fixed, do not regress**: (a) the ring's vertices must be emitted
  profile-major — angle-major scrambled the mesh, which still had zero open edges but a garbage
  signed volume; (b) the fillet arc sweeps 90°, not 180°, or the ring reaches 2 × size.
- **Verification**: `[HoleShapes]` asserts closed ring, positive volume, bounding box and, for the
  chamfer, volume within 2 % of `PI * size^2 * (r + size/3)` (Pappus, triangle centroid), plus that
  the fillet is smaller. Headless A/B on `hole_box.stl`: rim on → 360,935 B, rim off → 346,965 B.
- One follow-up fix: the rim size **field** had to be a plain typed field sized like the other rows;
  a slider plus a narrow `InputFloat` on the same line squeezed the value to one digit.

---

## 20. EF-1 / EF-2 — the Edge chamfer / fillet gizmo ✅ merged

`GLGizmoEdgeDress` (`EType::EdgeDress`, `Ctrl+D`) dresses mesh edges with the same kind of negatives,
so it works on dirty STLs that the CAD dress-up cannot repair. `libslic3r/EdgeProfiles` holds the
geometry: `chamfer_profile` (right triangle), `fillet_profile` (tangent quarter-arc lune, the same
shape as the rim fillet came from), `extrude_profile` (closed prism, winding fixed against
`its_volume`), then `make_edge_chamfer` / `make_edge_fillet` for one straight edge and
`make_loop_chamfer` / `make_loop_fillet` for a whole boundary.

**EF-1 — straight edges.** A clicked edge is grown into the whole geometric edge: `extend_feature_edge`
walks outward through mesh segments that are collinear and carry the same pair of face normals, so a
40 mm box edge that is tessellated into 64 segments is one feature. The 2D section is framed by the
two facing normals (`u` in the bordered face, `v` in the other), swept from `-margin` to
`length + margin` so neighbouring sweeps overlap at corners. Picking is screen-space nearest over a
cached edge list with a generous grab radius.

**EF-2 — whole loop / rim.** With "Whole loop / rim" on, the gizmo takes the boundary loops of the
coplanar face patch under the cursor (`its_face_patch_loops`, growing the patch by face-normal match)
and dresses the loop nearest the cursor, so a round hole
rim is one closed torus-topology feature instead of its 64 tessellation segments. This is how the
original request to "chamfer and maybe even fillet holes" is answered without hole detection.

The loop sweep is framed **per straight run**, not per vertex: each run gets its own `(u, v)` from its
own two faces, consecutive runs are bridged by a miter ring at the shared vertex, and collinear runs
are merged into one frame. A per-vertex bisector frame (the first version) rotated across a corner and
made the prism twist, which showed as a taper at a square rim's corner.

- **Refusals** (section 4.15): a run whose crease with the neighbour is below ~15°, or an edge that
  only bends below 20° at both ends, is a piece of a smooth surface, not a rim or an edge; sweeping it
  produced a slab (a "fin" on a hole) or a wedge. On `hole_box.stl` the single-edge list went from
  140 to 12 (the box edges only) and a workaround loop on a hole wall became impossible.
- **Applied state**: applied features are drawn red like the Holes gizmo does, the source is stored as
  a short hash of the source geometry in the volume name (`EdgeChamfer#<id>@<tag>`, which survives
  save/reopen), an applied edge is not dressed again, and a right-click removes it. MCP:
  `remove_edge` / `remove_loop` / `applied_edges` / `applied_loops`.
- **UI**: size slider 0.5-3.0 mm with a typed field beside it.
- **Verification**: `[EdgeProfiles]` — 251 assertions in 9 cases, including "A loop chamfer rings the
  whole hole or boss rim" (volume within 2 % of the Pappus value), "…follows a square face outline"
  (mitred volume near `0.5 * size^2 * perimeter`), "A piece of a curved surface is not a rim", and the
  edge-prism cases (closed, positive volume, over-extension by the margin, degenerate input refused).
  Headless checks exported the applied negatives and confirmed watertight, correctly sized rings
  (square 79.33 vs 80 analytic, rim 16.75 vs 16.76, fillet 22.01 vs 22.0), and G-code A/B: edge
  chamfer 343,554 B and rim chamfer 358,481 B against a 346,965 B baseline.
- **Known limits**: concave edges return an empty section and are refused. Nothing here touches the
  CAD fillet/chamfer in the Design tab.
- **EF-3 closed as redundant** (investigated after EF-2): two equal-size 45° chamfers on a 90° corner
  are cut by the planes `z = y + 1 - s` and `z = x + 1 - s`, which meet along a single straight edge
  (`x = y, z = x + 1 - s`); the miter plane `z = x + y + 1 - s` is redundant given both. So the union
  of the two independently swept prisms is already a clean mitre on a cube corner — no corner filler
  or chain sweep is needed for chamfers. **The fillet corner is verified too**: the user checked two
  90° fillet sweeps unioning into a cube corner and confirmed the result is correct (no crease where
  the two quarter-cylinders meet) — so fillet corners need no spherical corner patch.
**EF-4 — irregular rims.** Whole-loop mode used to find nothing on a face whose facets are each their
own patch, which is what a hole cut into a round-over looks like. The region is now grown by CREASE
ANGLE (`MAX_JOIN_DOT = cos 40°`), not by a normal tolerance, so the region stops only at real rims,
and `its_face_patch_loops_around` rings outwards from the seed when its own region carries no loop.
Measured on `lego-head-4.3mf` (eyes, nose and mouth cut into the head's rounded front): the 40° join
gives a mouth rim of 451 segments whose smallest crease is 54°, while the eyes, nose and flat faces
keep their own regions. Two further defects were fixed with it: the sweep no longer fans a run of
sub-0.1 mm steps where a rim crosses a round-over (`sweep_loop` collapses such a step onto the previous
frame once that frame has turned more than ~10°), and a candidate loop that is mostly hidden from the
camera is rejected (`update_hover` counts visible segment midpoints and skips a loop with
`n_visible * 2 < n_segments`), so hovering the front of a cut cannot pick the loop that runs along the
back of the wall. The loop picker has no screen-radius cap, and MCP gained a `hover_at` action so a
hover can be driven deterministically. `[EdgeProfiles]` is 256 assertions in 10 cases (the cylinder
case is now "A curved surface is one region bounded by its rims"); the wider
`[EdgeProfiles],[HoleShapes],[HoleDetector],[HoleStandards],[CutUtils]` set is 546 in 61. The
geometry was worked out offline against the lego mesh with python replicas of the region/loop
algorithm (scratch, not in the repo), then confirmed in the app.

- **Chamfer-crease skin check done**: the user checked the top face beside a 45° chamfer crease and
  found no skin artifact, so there is nothing left to fix there.

---

## 21. SNAP-8 — snap one object onto another while dragging with Alt (branch `port/SNAP-8`)

The Snap & Drag line (AS-3) only rests an object on a surface (Z). This is the first *object-to-object*
snap in the regular preview: hold Alt while dragging a selected object and the dragged object's own
nearest snap feature (face corner, edge midpoint, face centre, edge quarter, face quarter) lands on the
nearest snap feature of the object under the cursor, so separately printed parts drop together in an
assembly.

- **Gesture / defaults.** Pure GUI, off unless Alt is held — no app key, no free-Z, so at defaults
  nothing changes and the G-code is untouched. Alt-snap and Snap & Drag both want the drag translation,
  so the GravitySnap block now also requires `!alt_snap`; Alt wins while it is held.
- **Where.** `GLCanvas3D::on_mouse`'s volume-drag branch (`GLCanvas3D.cpp:4786`). The snap correction is
  folded into the single `m_selection.translate(...)` call, because `Selection::translate` writes instance
  offsets ABSOLUTELY from the drag-start cache — a second call in the same frame would overwrite the first
  instead of compounding. No new gizmo; hosting it in the Move gizmo is a follow-up — **done in
  section 26 (MV-3)**, which snaps the placed pivot while a grabber is dragged.
- **Anchoring.** The anchor is a coordinate of the dragged object itself, not the raw cursor: grabbing
  a corner a few pixels off always left that corner short of the target. `mover_hit_under_cursor`
  raycasts only the moving volumes (same loop as `hit_under_cursor`, shared through a `want_moving`
  predicate), and the anchor is **latched on the first Alt frame of the drag** rather than re-picked
  every frame: once the snap moves the object the cursor sits on a different coordinate of it, so
  re-picking let the anchor hop between neighbours, and each hop commanded a different landing position
  — the side-to-side shimmy. An interior coordinate winning the pick also pushed the real corner past
  the target, which is the tip intersection. Latched with the displacement in force and the instance
  rotation at that moment, the landing is `anchor_disp + R⁻¹ * (target − anchor_world)`, which depends
  on nothing from the previous frame and so cannot feed back into itself; it relatches when Alt is
  pressed again or the drag restarts. `R⁻¹` matters because `Selection::translate` moves an instance by
  `R * displacement` (`R` is orthonormal, so its transpose is the inverse) — without it a rotated
  object settles short of the target. With no latched anchor (Alt pressed while the cursor was off the
  dragged object) there is no anchor feature to trust and the drag-start grab point is put on the
  target instead.
- **Core.** `src/slic3r/GUI/OrcaExt/ObjectSnap.{hpp,cpp}` raycasts every non-moving visible volume and
  keeps the one nearest the camera, then reuses `CutUtils::face_snap_points` (through the gizmos'
  `coplanar_region` + `build_face_snap_points`). A non-planar face under the cursor falls back to the
  raw hit point, so curved surfaces snap too. The per-facet candidate list is cached and rebuilt only
  when the volume/facet changes.
- **Picking.** The candidate is chosen by `pick_by_sphere`, not by `CutUtils::nearest_face_snap`: a pure
  screen-distance test hands the pick to whichever snap coordinate projects nearest, which near a shared
  edge is often a feature of the *neighbouring* face, and the object then lands offset from the flush
  face the user was aiming at. Instead the drawn disc is the hit target — every candidate's disc is
  measured in screen space (its centre and its rim, using the same `candidate_radius` the spheres are
  drawn with, so the picture and the pick agree), the cursor's distance to that rim is the score, and an
  8 px grab margin lets a near miss still grab. Strictly-closest wins, so candidates emitted first keep
  the `FaceSnapKind` priority on ties. `nearest_face_snap` itself is untouched. Picking does **not** gate
  the hit: the face's coordinates are reported whenever the ray lands on it, and only `SnapHit::active`
  is left at `no_candidate` when the cursor is not on a sphere — otherwise nothing would show until the
  cursor happened to sit exactly on one.
- **Markers.** While Alt is held both objects show their candidate points as spheres, the active one
  larger — white on the object the drag lands on, amber on the object being dragged, so the feature to
  grab is visible before the press and the two sets are tellable apart. Hovering with Alt over a
  selected object's face is enough to show them: `GLCanvas3D::_objsnap_update` is shared by the drag
  path and by the `evt.Moving()` hover branch, which only defers to the two gizmos that own Alt for
  their own snap (Holes, Cut) — gating on "no gizmo open" instead would never fire, because the Move
  gizmo is up whenever an object is selected. `ObjectSnap::Markers` builds one
  merged mesh per `FaceSnapKind` with `its_make_sphere`/`its_merge`, radius `0.012 * diag` clamped to
  `[0.3, 1.5]` mm, and the same per-kind colours the Holes tool uses (`GLGizmoHoles::build_snap_markers`
  is the reference). `Markers::render` runs with the depth test off so the spheres read through the
  object they belong to. `GLCanvas3D` clears both sets on every mouse event, sets them in
  `_objsnap_update`, and clears them again in `mouse_up_cleanup` (mouse-up does not re-run the drag
  branch, so without that they would linger) and in `_render_objsnap_markers` when Alt is released
  without any mouse event. The draw call sits next to `_render_snapdrag_indicator`.
- **Three lessons, do not regress.** (a) `ObjectSnap.hpp` forward-declares `GUI::Camera` as `struct`, to
  match `Camera.hpp`; MSVC mangles a `class` forward declaration differently (`AEBVCamera` vs
  `AEBUCamera`) and the mismatch is a permanent LNK2001/LNK2019 that survives every clean rebuild.
  (b) The screen cursor is `Point` (`Vec2<coord_t>`), so the `Vec2d` argument needs `pos.cast<double>()`.
  (c) `Slic3r::GUI` is a *sibling* of `Slic3r::OrcaExt::Gui`, not an enclosing namespace, so OrcaExt code
  must qualify everything from it: `GUI::GLModel`, `GUI::wxGetApp()`. But `GLShaderProgram` lives in
  `Slic3r` itself and `ColorRGBA` too, so those stay unqualified. Getting this wrong is only a compile
  error, not a linker one.
- **Verification.** Regression suites `[EdgeProfiles],[HoleShapes],[HoleDetector],[HoleStandards],
  [CutUtils]` = 546 assertions in 61 cases, all passing (this feature adds no libslic3r geometry, so
  those suites are unchanged). The live gesture had to be tested by hand, since MCP cannot hold Alt; the
  user did, and confirmed the Alt-hover preview, the sphere picking and the corner-to-corner landing.
  Manual protocol is in `docs/superpowers/SNAP-8-object-snap.md` (scratch, gitignored, not committed).
- **Branch state**: on `port/SNAP-8`, branched from `port/integration` at `fa2d06d014`. Merged. See
  section 0 for why a branch only merges once the user has tested it in the app.

## 22. RF-1 — strengthen vertical holes from the Vertical holes gizmo (branch `port/RF-1`)

A screw driven into a thin-walled hole splits the part. This feature thickens the wall around a
vertical hole by dropping a co-axial ring of material over it and giving that ring a *higher wall
count* than the rest of the object, so the hole the screw cuts into is solid plastic. It is the
first feature in this line that writes a `PARAMETER_MODIFIER` volume (every earlier hole feature
writes a negative volume).

- **Where.** The strengthening lives in the counterbore-bridge gizmo, renamed **Hole walls**
  (`GLGizmoCounterboreBridge::on_get_name`, `Ctrl+K`), which now has two top-level modes: **Bridging**
  (the pre-existing painted bridge, with its smart-stepped / partial sub-options) and **Strengthen
  hole**. The holes gizmo keeps its original identity — name **Holes** (was **Horizontal holes** until
  UI-1), icon `toolbar_holes(_dark).svg` (`Ctrl+J`, `GLGizmoHoles`); the RF-1 rename and icon that were
  briefly attached to it are gone. The counterbore gizmo took
  `toolbar_hole_walls(_dark).svg` instead, so painters and strengtheners share one tool as requested.
  Both names and both icons were revisited in UI-1 (section 25).
- **Defaults.** Two parameters: **Wall thickness** (the radial thickness of the added ring, default
  `0.8` mm) and **Extra loops** (wall loops added on top of the object's own count, default `1`). Off
  by default: the mode is one of two and a fresh object starts in Bridging, so the G-code is untouched.
  Vertical holes only — a reinforcement ring around a horizontal hole would sit in mid-air.
- **Ring geometry.** `GLGizmoCounterboreBridge::reinforce_mesh` builds a positive co-axial shell with
  `its_make_tube(outer_d, inner_d, depth, axis, entry)`: `inner_d = 2·radius`,
  `outer_d = inner_d + 2·thickness` with `thickness = max(0.05, m_reinforce_thickness / object_scale)`,
  and the axial span is the detected wall depth plus a `margin = through ? max(0.5, 0.25·radius) : 0`
  at each end so a through hole is reinforced past both openings. `entry` is
  `h.center − axis·(0.5·depth + margin)`; `DetectedHole::center` is the **absolute axial midpoint**
  of the wall (`HoleDetector.cpp:228` sets `center = cu·e1 + cv·e2 + t_mid·axis`), not an entry point,
  so the ring straddles the hole symmetrically. This mirrors the `teardrop_depth` margin convention.
- **Detection.** The ring only makes sense on a real hole, and the detector used to report the
  washer's *outer* cylinder wall as one too (index 0, r≈10, `through:false`). That false positive also
  produced an enclosing pick cylinder, which is why the real hole could not be clicked. `HoleDetector`
  now rejects a convex surface after the circle fit: it averages `face_normal · radial_unit` over the
  patch and drops the patch when the mean exceeds `0.5`. A hole is a cavity, so its outward normals
  point toward the axis (negative dot); a solid or outer cylinder is convex (positive). The
  solid-cylinder detector test was rewritten to expect an empty result.
- **The modifier.** `GLGizmoCounterboreBridge::toggle_reinforce` adds the ring as a
  `ModelVolumeType::PARAMETER_MODIFIER` named
  `HoleReinforce#<idx>` (feature-name convention shared with `Teardrop#i`, `HolePocket#i`), then sets
  on that volume's config
  `wall_loops = effective_wall_loops() + m_reinforce_loops`. `effective_wall_loops` reads the
  `wall_loops` override on the `ModelObject` config first, then the selected print preset
  (`wxGetApp().preset_bundle->prints.get_edited_preset().config`), and falls back to `2`; the `.has()`
  guard is required because `opt_int` asserts on a missing key. The counterbore gizmo's
  `add_named_volume` returns the created `ModelVolume*` so the caller can set the key in the same
  snapshot.
- **Why a modifier works.** `wall_loops` is declared on `PrintRegionConfig`
  (`PrintConfig.hpp:1461`), so it is a valid modifier key. `Print::apply` builds a child `PrintRegion`
  per modifier as parent region config + volume config (`region_config_from_model_volume`,
  `PrintObject.cpp:3940` → `apply_to_print_region_config`, `:3868`), creates a distinct region only
  when the merged config differs from the parent, and `slices_to_regions` assigns the
  modifier ∩ parent polygon to it while diffing the parent. `LayerRegion::make_perimeters`
  (`LayerRegion.cpp:528`) then reads `region().config().wall_loops`. Object- and print-scope keys
  (`layer_height`, `raft_layers`, `support_*`, `spiral_mode`, …) are silently dropped by
  `apply_to_print_region_config` and have no effect in a modifier, which is why this feature is
  `wall_loops`-only. Unlike a negative volume, a modifier stays editable after placement.
- **Applied state.** The strengthen mode keeps its own object-space hole list. `refresh_applied`
  seeds `m_reinforce` and scans `v->is_modifier()` volumes for the `HoleReinforce` prefix. The applied
  sleeves are drawn by the ordinary volume render path, so the painter stays out of the way:
  `render_painter_gizmo` early-returns in strengthen mode. `gizmo_toggle_hole`, `gizmo_reinforce_all`
  and `gizmo_clear_reinforce` restrict to vertical holes, and the pick raycasters are registered only
  in strengthen mode (`register_pickers`), so Bridging keeps its brush behaviour.
- **Icon.** The stock counterbore icon made no sense for a tool that now also strengthens holes, so it
  took monochrome 40×40 SVGs (teal `#009688` cylinder with a bore, accent `#2b3436` light / `#b6b6b6`
  dark), wired in the `GLGizmosManager` ctor and the dark-mode filename switch. Redrawn again in UI-1
  as `resources/images/toolbar_hole_walls{,_dark}.svg` (section 25); `get_name_from_gizmo_etype`, the
  dead lookup that carried the old tooltip name, was deleted at the same time.
- **MCP.** Not exposed. The strengthen mode belongs to the counterbore gizmo, which has no MCP tool,
  so the feature is UI-driven. The `holes_gizmo` `reinforce` operation and its two parameters were
  reverted when the feature moved.
- **Verification.** Build green and `libslic3r_tests` 904/904. A/B slice on a 64-segment washer
  (inner r 3 mm, outer r 10 mm, height 10 mm; `cube_with_hole.obj` is useless here — its hole is
  square, so the cylindrical detector correctly finds nothing): with `HoleReinforce#1` applied the
  G-code grew 501,016 → 691,601 bytes, `;TYPE:Inner wall` runs 98 → 147, `;TYPE:Outer wall` runs
  99 → 99 (unchanged, so the extra walls stay local to the hole), 50 layers, 0 warnings both legs.
  Layer 1 shows the baseline hole boundary at r≈3.40/3.88 and the reinforced part adding co-axial
  runs at r≈3.82, 4.75 and 5.23 — the hole wall is genuinely thicker. (A `;TYPE:` marker counts a
  run, not a loop, so the byte delta is the signal to trust.) Measured before the feature moved into
  the counterbore gizmo; the ring geometry and the modifier are unchanged. After the detector fix
  `find_holes` returns only the real hole (index 1, r≈3, `through:true`) and no longer the outer-wall
  false positive.
- **Known limitations, do not re-fix.** The loop count is baked into the modifier when it is applied,
  so changing the object/preset `wall_loops` afterwards does not re-bake it — clear and re-apply, same
  as the other per-hole features. A hole lying at the edge of a part produces a ring that hangs
  outside the part; it is left to the user rather than auto-clipped.
- **Branch state**: on `port/RF-1`, branched from `port/integration` at `4e0c9f881d`. Not merged.
  See section 0 for why a branch only merges once the user has tested it in the app.


## 23. HF-1 — Cavity fill (branch port/HF-1)

A Prepare-tab brush that plugs a recessed mark — an engraving or a watermark — with a positive
volume, so it is filled at slice time without a boolean and without any CAD reconstruction. It is the
first attempt at the "fill a depression" problem; section 24 is the second.

- **Tool.** `GLGizmoHoleFill` (display name "Cavity fill", `EType::HoleFill`, registered directly after
  `EdgeDress`, icons `resources/images/toolbar_cavity_fill{,_dark}.svg`, shortcut `Ctrl+G`). The
  geometry lives in `src/libslic3r/HoleShapes.{hpp,cpp}`; the MCP tool is `hole_fill_gizmo`
  (`src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp`); tests are the `[HoleShapes]` cases in
  `tests/libslic3r/test_holeshapes.cpp` using `tests/data/watermark.obj`.
- **Interaction.** Left-drag paints a stroke over the mark; one fill is committed on mouse-up from the
  whole stroke; right-click removes the fill under the cursor. "Brush width" is a world-space slider
  (default 2.5 mm). The fill is a positive `MODEL_PART` volume named `HoleFill#<n>`, added with a
  snapshot and the usual `ObjectList` refresh. Applied plugs are skipped by the hover raycast so they
  do not occlude the recesses next to them.
- **Geometry.** `cavity_fill_local(its, seed_points, seed_facets, radius)`: the brush reach selects a
  patch of facets around the stroke seeds; every patch vertex is raised along the patch normal to the
  highest surface plane around it (a candidate facet plane, gated to within ~26° of the patch normal,
  and searched for a margin beyond the brush so the brush width does not decide detection); one
  independent prism per raised facet is emitted and the whole plug is flipped if its signed volume is
  negative. A flat or convex wall has no plane above its own vertices, so painting it emits nothing.
- **Why it does not work well enough.** The estimator closes a cavity only up to a plane. On the flat
  front wall of the test part it is exact — the recessed letters come out flush per layer — but on the
  coarsely faceted wall under the curved top rim it keeps a residual recess (user: "it smoothed out
  the flat wall flush but the curved walls still were not flush when sliced as you can see letters in
  the image"). The front wall is prismatic (its `+X` face is exactly at `x ∈ {87.367, 87.5}` at every
  `z` over the letter band), which is why a *lowered copy of the wall* reproduces the un-engraved
  surface exactly while a fitted plane cannot. That observation is the origin of section 24.
- **History.** The branch carries several rejected designs: a fitted reference plane painted by the
  user (fixes 3–5), a per-facet ring-plane classifier (fix 6), Taubin fairing of the patch (fix 7),
  and plane-by-plane closing of each cavity (fix 8, the user's "best so far"). Fix 9 widened the wall
  search beyond the brush and was rejected as a regression; it was reverted, leaving the branch at the
  fix-8 tree plus the revert commit.
- **Branch state**: on `port/HF-1`, branched from `port/integration`. Head is the revert of fix 9
  (tree equal to fix 8). Not merged — the user has not accepted it, and work has moved to section 24.


## 24. HF-2 — Fill from above (branch port/HF-2)

Reproduces, inside the Cut gizmo, the workflow the user performs by hand when an engraved watermark
has to be filled flush on a curved or coarsely faceted wall: cut the part above the mark, duplicate
the top piece, lower the duplicate so it is a positive that follows the wall shape, and let it fill
the recess. The key property is that the engraved wall is *prismatic*: a copy of the wall band from
above the mark, dropped by the depth of the mark, is the un-engraved wall surface exactly. Nothing is
fitted to the surface, so the fill is flush by construction rather than by approximation.

- **Why it lives in the Cut gizmo.** The Cut gizmo already owns a plane in world space and already
  produces the two halves of the user's manual process. The feature is an addition, not a replacement:
  the existing cut is untouched, and the fill is a separate button.
- **Mechanism.** With the cut plane at `point` with normal `n` (both world space), and an offset `d`
  (the "Lower by" value):
  1. frame a transform whose `z` axis is `n` and whose origin is `point`;
  2. `cut_mesh(local, 0, &upper, nullptr)` — keep the half above the plane;
  3. `cut_mesh(upper, d, nullptr, &band)` — keep the slab from `0` to `d` above the plane;
  4. invert the frame and translate the slab by `-n * d`.
  The result is the band of the object between the plane and plane + `d`, moved down by `d`. It is
  added to the object as a positive `MODEL_PART` volume named `FillFromAbove#<n>`; because positive
  model parts are combined per layer by 2D Clipper at slice time, it fills the recess with no boolean.
- **User-visible parameters.** Exactly two, so the geometry stays understandable: the existing cut
  plane (position and orientation) and `Lower by` (band thickness, `m_fill_offset`, default 6 mm,
  slider 0.2–50 mm). No bounding box and no border points — the user's manual side cuts only clip the
  band footprint and are redundant where the wall is prismatic.
- **Ceilings, stated deliberately.** The band is the full cross-section of the object at that height,
  not just the wall strip, so where that cross-section contains geometry which lands in empty space
  when lowered — the inner cavity, a ledge, geometry under the curved top rim — extra material
  appears and the user trims it. `Lower by` must be at least the mark depth so the lowered band covers
  the whole mark. On a part that is not prismatic the lowered geometry can protrude. These are the
  same compromises the manual workflow makes.
- **Deferred.** Trimming the band footprint with further cut planes (the user's extra side cuts) is
  deliberately deferred: v1 has a single plane and the user trims any spill-over themselves. Add it
  only if the leftover geometry becomes a problem in practice.
- **Branch state**: on `port/HF-2`, branched from `port/HF-1`. The user tested it in the app and
  confirmed it works, so it is merged `--no-ff` into `port/integration`.


## 25. UI-1 — toolbar divider, tool names and icon set (branch `port/UI-1`)

Three QOL fixes for the gizmo toolbar at the top of the 3D view: a divider between the stock gizmos
and this fork's, names that describe what the tools do, and one consistent icon set.

- **Where the toolbar lives.** `GLGizmosManager` draws the row itself (`do_render_overlay`), unlike
  the main toolbar, which is a `GLToolbar`. It had no notion of a separator at all; the only divider
  on screen was `GLCanvas3D::m_separator_toolbar`, a one-item toolbar rendering
  `resources/images/seperator.svg` (`GLCanvas3D.cpp` `_render_separator_toolbar_{left,right}`).
- **The divider.** `is_orcaport_gizmo()` marks the fork's gizmos, and `divider_selectable_pos()`
  returns the drawn position of the first one, so the divider lands between `BrimEars` and
  `AlignStack` — no gizmo had to be reordered. `get_scaled_total_width()` adds one `separator_size()
  + gap_x` (half an icon, like `GLToolbar`'s separators, which replace an icon's cell), and
  `get_gizmo_from_mouse()` skips that cell so the divider is not clickable and every icon after it
  still maps to the right tooltip. `get_scaled_total_width()` feeds `GLCanvas3D`'s auto-fit, which
  treats the extra width as non-item width, so the icons keep their size.
- **The line** is `seperator.svg` appended to the icon atlas as one extra sprite row
  (`generate_icons_texture`; `m_icon_rows` is now rows+1 and `m_separator_row` the new row). It is
  drawn in the atlas's *Activable* state, which is the state the pipeline tints with the toolbar's
  normal colour — that is what makes it follow light/dark for free. Loading it directly would not:
  the asset is `#2b3436` at 50% opacity and is invisible on the dark bar. The tile is square while
  the line inside it sits a quarter of the way in, so the tile is offset to centre the line in the
  divider cell.
- **Names.** `GLGizmoHoles` "Horizontal holes" → **"Holes"** (only teardrop is horizontal; bore,
  pocket, rim chamfer and rim fillet are not), and `GLGizmoCounterboreBridge` "Vertical holes" →
  **"Hole walls"** (it bridges counterbores *and* thickens hole walls, which is what its panel already
  says: Wall thickness / Extra loops). The lookups that carried the old strings —
  `get_name_from_gizmo_etype()`, dead code, and the manager's `switch_gizmos_icon_filename` — were
  updated or deleted. Only `on_get_name()` is user visible; it is what the toolbar tooltip shows.
- **Icons.** All seven were redrawn to the stock language: 40×40, two colours (`#2b3436` light /
  `#b6b6b6` dark plus the `#009688` accent), line art with negative space, no pure black. The three
  real defects were: EdgeDress borrowed the CAD panel's `design_filletedge.svg` (24×24, stroke-only,
  hardcoded `#b6b6b6`, no dark variant, so it was a near-invisible pale line on the light bar);
  `toolbar_perforation_dark.svg` was byte-identical to the light icon and its holes were pure black;
  and `toolbar_counterbore_bridge{,_dark}.svg` was referenced from nowhere. The two hole icons were
  renamed with their tools: `toolbar_horizontal_holes` → `toolbar_holes`, `toolbar_vertical_holes` →
  `toolbar_hole_walls`; the dead counterbore pair was deleted. Nothing else referenced these files
  (the MCP tools open gizmos by `EType`, not by name).
- **Icon grammar (final).** The stock idiom for a part-face tool is a **rounded square you do
  something to** — `toolbar_cut` splits it, `toolbar_seam` / `toolbar_fuzzy_skin_paint` draw inside it
  and hang a brush off the corner. Five of ours now share that container exactly, rounded square
  (4,4)–(35,35) r1.5, so they no longer read as bigger or smaller than the stock squares:
  `holes` (bore + teardrop inside), `hole_walls` (bore with a thick concentric wall ring),
  `edge_dress` (three sharp corners, one filleted — the square is drawn *without* the sharp corner),
  `cavity_fill` (notch plugged flush from the top edge), `perforation` (the 3×3 grid on the shared
  container). Support Zones stays a scene (overhang slab + leaning pillar + bed) but is tightened to
  the same 4–35 footprint, and Align & Stack is two squares with a descending arrow. An isometric-cube
  pass was tried in between and discarded: it was not what "square" meant here.
- **Why the shapes are line art and not two-tone fills.** `load_from_svg_files_as_sprites_array`
  (`GLTexture.cpp`) gives the *rest* state (`Activable`, sprite column 0) a single flat ink colour —
  every non-black pixel is overwritten with `#2b3436` (light) or `#b6b6b6` (dark). The accent is only
  visible in the hover / selected / highlighted columns, which keep the file's own colours. So an
  icon must read as a **silhouette** with negative space, and two adjacent fills of different colours
  merge into one blob at rest. Checking a new icon means checking it both ways.
- **Not done.** None of these strings are in `localization/i18n/OrcaSlicer.pot` — the catalogs were
  never regenerated for them, so they show English in every language. Renaming loses no translation
  that existed; running `scripts/run_gettext.bat --full` to pick the new msgids up is still pending.
- **Branch state**: on `port/UI-1`, branched from `port/integration`. Merged `--no-ff` as `eec7a7b0fd`
  (user-verified in the app 2026-10-16: divider in place, both renames showing as tooltips, all seven
  icons reading as one set). Open, unrelated: the icon bar's auto-scale (`toolkit_size`) never
  shrinks, so at a narrow window the row reaches the window edge and the tail of the fork's gizmos
  can be pushed off it. Pre-existing and driven by upstream's `_check_and_update_toolbar_icon_scale`,
  not by this change — but it is the reason seven extra gizmos are worth watching.


## 26. MV-1 / MV-2 — a Plasticity-style Move pivot (branches `port/MV-1`, `port/MV-2`)

Give the stock Move gizmo a placed pivot that sticks to a model's features and, with Alt held,
lands on another object's features. This mirrors Plasticity's Move: `V` sets the pivot (snapping it
to ends and midpoints), and `Ctrl` while moving snaps the geometry onto another body. It replaces
nothing — the axis grabbers, the coordinate-system combo and the XYZ fields are unchanged.

- **MV-1 (merged, `f6f42220e1`) — the frame.** `GLGizmoMove.cpp`: a **Pick face** button on the Move
  window raycasts a flat face on the selected object, then `set_face_frame(hit, normal, volume)`
  re-centres the gizmo at the hit and points its Z axis along the face normal towards the camera.
  The frame is stored as `m_face_frame_local = vol->world_matrix().inverse() * world_frame`, so it
  follows the object. `change_cs_by_selection` disables the coordinate combo while a frame is
  active, and `on_dragging` translates along `face_frame_world().linear()`. `update_face_highlight`
  paints a translucent patch over the hovered coplanar region (`coplanar_region` +
  `build_coplanar_patch`). This is the pivot half; it is not documented anywhere else.
- **MV-2 pivot snapping (this branch).** The pivot no longer lands on the raw raycast hit: while
  picking, `update_face_highlight` builds the face's snap coordinates
  (`build_face_snap_points`, world space) and `nearest_face_snap` picks the one under the cursor
  with the shared 8–48 px adaptive tolerance; `pick_face_at` then puts the frame origin on it and
  remembers the `FaceSnapKind`. `ObjectSnap::Markers` draws the coordinates as per-kind spheres —
  the same palette the Holes and Cut tools use — and the window shows `Pivot: Corner` / `Edge mid` /
  `Face centre` / `Edge 1/4` / `Face 1/4` / `Free`. A click that is not near a coordinate keeps the
  raw hit, so curved faces still place a pivot. `Markers` gained `set_active`, which rebuilds only
  the highlighted sphere: a moving cursor would otherwise re-merge the whole per-kind mesh set every
  frame.
- **The arrows follow the frame, not box.max.** The frame origin is the picked coordinate, not the box
  centre, so `on_render` sizes the grabbers from the box **half-extent** (floored at a tenth of the
  longest side) whenever a frame is active. The stock `box.max + margin` length collapses to a stub on
  the axis the pivot already sits at the end of — the face normal, so the blue arrow degenerated to a
  dot. Half-extent is the same value as `box.max` when the frame is centred.
- **The placing click belongs to the gizmo, but only the click.** `m_pick_click_pending` is set on the
  placing LeftDown and swallows the release (and the Leaving that can end it) instead of letting it
  reach the canvas. Released on its own, that event ran `GLCanvas3D`'s "left-up on empty space ->
  `deselect_all()`" path and closed the tool roughly half the time, because the snap coordinates sit
  on silhouette edges where the hover raycast that gates the path is a coin flip. **Motion must still
  fall through**: the canvas's own move branch is what marks the overlay dirty, and `on_idle` renders
  nothing when neither dirty flag is set — consuming the move stopped the scene re-rendering and the
  hover highlight and snap markers went with it.
- **MV-3 Alt pivot snap (this branch).** Dragging a grabber with Alt held runs
  `ObjectSnap::hit_under_cursor` over the non-moving volumes and overrides the axis displacement so
  the pivot reaches the target coordinate. The gizmo is a **one-axis** drag, so the snap keeps the
  constraint: it slides the object along the dragged axis until the pivot's projection reaches the
  target's. The geometry is a scalar projection — `Selection::translate` moves the selection by
  `base.linear() * m_displacement`, so column `m_hover_id` of the gizmo's base matrix is the axis's
  world direction and its length is one displacement unit; `m_displacement[k] = dist / |col|` with
  `dist = (target - pivot_start)·axis`. `pivot_start` and the frame are latched in `on_start_dragging`
  (the drag is measured from the selection cache, so this holds), and the markers are dropped whenever
  Alt is up. Starting the drag also clears the SNAP-8 Alt-hover markers
  (`GLCanvas3D::clear_objsnap_markers`): that preview does not run during a gizmo drag, so its spheres
  would otherwise sit on top of the gizmo's own.
- **Alt, not Ctrl.** Plasticity uses Ctrl, but in OrcaSlicer Ctrl is additive-select in the preview
  and Alt is already `Part selection` in the Move shortcut list; SNAP-8 (object-to-object Alt snap in
  the preview) set the precedent, so MV-3 uses Alt too. The two gestures are independent: SNAP-8
  needs a raw preview drag, MV-3 a gizmo grabber drag, and the gizmo consumes the event first.
- **Off by default.** No app key, no config key: with no pivot picked and no Alt held the gizmo is
  stock (MV-1 already made the frame opt-in through the button). G-code is untouched.
- **Reuses, does not duplicate.** `CutUtils::face_snap_points` / `nearest_face_snap` / `FaceSnapKind`,
  `GLGizmosCommon::{coplanar_region, build_coplanar_patch, build_face_snap_points, world_to_screen,
  raycast_object_face, facet_normal_in_world}` and `OrcaExt::Gui::ObjectSnap::{hit_under_cursor,
  Markers}`. No new geometry.
- **Ceilings, stated deliberately.** (a) The Alt snap keeps the axis constraint, so the pivot lands on
  the *projection* of the target onto the dragged axis, not on the target itself; a freestyle (no
  axis) move is not in v1. (b) A multiple-instance selection rotates each instance by its own R, so
  the one-axis landing is only exact for a single instance; the pivot then follows the reference
  frame. (c) Local coordinates depend on the volume transform, so the projection is approximate
  there. (d) The window label is text, not the in-scene tip help Plasticity draws beside the cursor.
- **Align & Stack stays.** AS-2 also does face-to-face mate (depth / rotate / flip / mirror) and
  touch/flush/center stacking, which a point snap does not cover. Do not delete it on the strength
  of MV-2 alone.
- **Verification.** `[CutUtils]` covers the snap math and is untouched. The gesture has to be tested
  by hand (MCP cannot hold a modifier); the manual protocol is: pick a pivot near a corner / edge
  midpoint / face centre and check the marker and the label; then drag a grabber with Alt onto
  another object and check the pivot lands on its coordinate; release Alt and the object follows the
  cursor again.
- **Branch state**: `port/MV-2`, from `port/integration` at `556e074830`. Merged `--no-ff` as
  `56b5a6acba` (user-verified in the app 2026-10-15: arrows full length on a placed pivot, hover
  markers back, and the placing click no longer closes the tool). See section 0 for why a branch only
  merges once the user has tested it.

## 27. Gizmo panel chrome — anchored panels and trimmed rows (branch `port/gizmo-ui-panels`)

Six small defects in the fork's own gizmo panels, one of them shared by four of them. No slicing
behaviour, no config keys, no new files; every panel is anchored or trimmed in place.

- **The shared defect: the panel anchor.** `GLGizmosManager::do_render_overlay` calls
  `render_input_window(x, ...)` with `x` = the **left edge** of the gizmo's own icon cell (`top_x`
  before it is advanced by `stride_x`). A panel must therefore be anchored with pivot `(0,0)`.
  `Holes`, `EdgeDress`, `HoleFill` and `Perforation` used the side-toolbar branch's pivot `(1,0)`
  unconditionally, which pins the window's *right* edge to the icon's left edge — every one of them
  opened to the left of its own icon. `GLGizmoCounterboreBridge`, `FuzzySkin`, `Seam` and
  `FdmSupports` already choose by layout (`#if BBS_TOOLBAR_ON_TOP`, defined in `GLGizmosManager.hpp`
  as 1); the four now do the same, so the side layout keeps `(1,0)`. This is also why
  `GizmoImguiSetNextWIndowPos`'s overflow clamp (`x = canvas_width - w`) only makes sense with
  pivot 0: it treats `x` as a left edge.
- **Hole walls, the width.** Not an anchor bug: an `AlwaysAutoResize` window is as wide as its widest
  row, and the bridge-type row was `left_width + [Smart (stepped)] + [Partial] + "(smart: stepped
  ramp)"`, which is what made the panel fill most of the canvas. `ImGui::Separator()` is *not* a
  contributor — `SeparatorEx` deliberately does not report its width to the layout ("so that it
  doesn't get feed back into AutoFit") — and `GLGizmoUtils::begin_right_aligned_buttons` settles at
  the current width instead of growing it. Removing the trailing text and filling the active button
  with the `sel_button` helper the hole tool already uses drops the panel to the Feature row's width.
  The Feature row got the same treatment, so the active mode is visible without reading the controls
  below it.
- **Edge chamfer / cavity fill, the typed fields.** `m_imgui->scaled(x) = x * font_size`, and an
  `InputFloat` with a step carves its two buttons *out of* the item width, so `scaled(4.5)` clipped
  the value — the same ceiling `GLGizmoHoles` documents at `scaled(4)`. Both fields now use the hole
  tool's `scaled(5.5)`.
- **Redundant buttons, removed.** EdgeDress's "Apply to hovered edge" and HoleFill's "Fill under
  cursor" duplicated the viewport gesture: `on_mouse` left-click already applies (`gizmo_apply_hovered`
  for the edge; a stroke that `commit_stroke`s on release for the cavity). Only the panel buttons go;
  `gizmo_apply_hovered` stays for the mouse path and the MCP tools (`edge_dress_gizmo`,
  `hole_fill_gizmo`). The hover hints lost the wording that referred to the buttons, which also
  narrowed the cavity panel.
- **Perforations, the reserved rows.** The two advisory lines (width too large for the spacing /
  near-horizontal face) were emitted every frame with an empty string when unset — an empty `text()`
  still consumes a line — leaving a blank block under the last hint. They are now drawn only when set.
  The rows were reserved to stop a notice from moving a slider, and that is already guaranteed by the
  panel's `SetNextWindowSizeConstraints` width pin; only the height changes now.
- **MV-1: a Pick pivot shortcut, unbound.** `Shortcut::GizmoMovePickPivot` (`gizmo_move_pick_pivot`,
  `"Gizmo move - Pick pivot"`, Plater context) is registered with no default chord — `KeyChord{}` is
  `WXK_NONE`, which the registry already treats as unbound (`display()` empty, `with_key()` returns
  the plain text, `UNBOUND = "none"` in the AppConfig) — so Preferences > Keyboard shortcuts lists it
  under **Gizmos** with an empty key and an Edit button. Nothing persists the numeric enum value
  (`load`/`save` go through `ShortcutInfo::key`), so it is inserted next to `GizmoMove` in both
  `Shortcuts.hpp` and the table, which the `table_is_in_enum_order()` static_assert enforces.
- **Dispatch: a gizmo-local action is not a gizmo opener.** `GLCanvas3D::handle_shortcut`'s `default`
  branch only knew `open_gizmo_by_shortcut`, which matches a gizmo's *own* shortcut. It now tries
  `GLGizmosManager::handle_gizmo_shortcut` first, which routes to the **current** gizmo
  (`dynamic_cast<GLGizmoMove3D *>` -> `start_pick_pivot()`). Nothing else consumes a shortcut this way
  yet, so it stays one hardcoded action rather than a virtual on the gizmo base.
- **MV-1: pick and reset are no longer exclusive.** `render_extra_move_ui` was an if/else chain, so
  once a pivot was set "Pick pivot" disappeared and only "Reset pivot" was left. Both are now shown
  together; re-picking overwrites the frame (`set_face_frame`), and cancelling a re-pick keeps the
  pivot that was set — `on_mouse`'s right-click and the panel's Cancel pivot only clear
  `m_pick_face_mode`. The `"Pivot: <kind>"` line is read after the reset branch, so a reset cannot
  print a stale "Free" for one frame.
- **Verification.** GUI only, so it is a manual checklist: each of the five panels opens under its own
  icon (Perforations is the right-most fork icon, so it also exercises the overflow clamp); "1.00" and
  "2.5" are fully readable with both step buttons; there is no "Apply to hovered edge"; Perforations
  has no blank rows but still shows the orange notice when Hole width exceeds Spacing, without the
  panel moving; hole walls is narrower with the active Bridge type and Feature filled teal; and the
  shortcut dialog lists "Gizmo move - Pick pivot" under Gizmos with an empty key, which can be bound
  and then pressed with the Move tool open. `porting/scripts/verify-markers.ps1` gains no new
  unmanifested marker.
- **Rows that were missing.** EF-1, HF-1 and RF-1 had markers in the tree but no `manifest.tsv` row
  (the verifier reports exactly that). They now have one, covering the anchor fix and the panel trims
  above. The `MV-2`/`MV-3` markers in `GLGizmoMove.*` still use the unbracketed `// ORCAPORT:` form and
  are still reported; not touched here.
- **Branch state**: `port/gizmo-ui-panels`, from `port/integration` at `9e5ce7fb2b`. Seven single-ID
  commits (UI-1, PF-2b, RF-1, EF-1, HF-1, PP-1, MV-1) plus a `port:` docs commit, patches `38`-`44`.
  Not merged and not pushed: the merge is the user's gate after testing.

**Follow-up on the same branch (patches 45-46).** Two things the panels still got wrong:

- **The overflow flip.** `GizmoImguiSetNextWIndowPos`'s clamp treats `x` as a left edge
  (`x = canvas_width - w`), so on the four right-most fork icons the left-anchored panel was slid
  along the canvas right edge: butted against it, with its left side under the button. When the panel
  would run past that edge, `Holes` / `EdgeDress` / `HoleFill` / `Perforation` are now right-aligned
  instead — pivot `(1,0)` with width `0` to skip the clamp (the idiom the texture-displacement panel
  already uses), anchored at the icon's **right** edge, so the panel hangs to the left with its right
  edge still under its own icon. While the panel fits to the right of the icon nothing changes, and the
  other fork panels keep the plain left anchor. The icon width the anchor needs is
  `GLGizmosManager::get_scaled_icons_size()` (new); `m_parent.get_scale()` is the camera zoom, not the
  toolbar layout scale. Ceiling: the right-aligned branch has no clamp of its own, so a panel wider than
  the icon's distance from the canvas left edge would lose its left part — that needs an already broken
  narrow layout (the `toolkit_size` gap above).
- **Button spacing.** The move window pushes `ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 6.0)` so its XYZ
  fields butt together, which left the bare `SameLine()` between Pick pivot and Reset pivot with no
  gap. They are now spaced by the window's own column unit (`get_style_scaling() * 8`).


## 28. Panel style standard for the custom gizmos (branch `feat/extrude-fill`, HF-3)

**Owner's decision:** every gizmo this fork adds should be visually distinguishable from stock Orca,
so it is obvious at a glance which panels are ours. HF-3 (the Extrude gizmo) is the **first built
against the standard and is the reference implementation to copy**; the rest migrate next.

### The standard

**Palette** - `GizmoStyle.hpp`, `enum class GizmoCol`. Rules, in the header's own words: teal is the
accent and means "this is yours / this is your selection"; the amber (`Warn`) is reserved
*exclusively* for "something is off" and is never decorative; every grey is a step on one ramp so a
panel reads as one surface. Light and dark are both defined and `gizmo_col()` picks.

**Layout rules**, all visible in the Extrude panel:

- **Fixed narrow width, wrapped hints.** `ImGui::SetNextWindowSize(ImVec2(gizmo_panel_width(), 0.f),
  ImGuiCond_Always)` plus wrapped text (`gizmo_hint()`, or `ImGui::PushTextWrapPos(0.f)`).
  `SetNextWindowSize` overrides `AlwaysAutoResize` on the axes it sets, so the height still follows
  the content. An auto-resizing window sizes to its longest line: the Extrude panel used to come out
  ~830 px for content that needs ~360.
- **A label column**, so a stack of rows reads as a table rather than as sentences
  (`gizmo_label_w(widest_caption)` - pass the longest caption in the panel).
- **Slider + typed field, and NO `[-][+]` step buttons** (`gizmo_row_slider`). The step buttons cost
  about a third of the width; the field still accepts a typed value and clamps it to the range.
- **A switch, not a checkbox, when the caption sits on the left** (`gizmo_row_switch`).
- **Segmented buttons for a small exclusive choice** (`gizmo_segment`) - not a dropdown, not radios.
- **A right-aligned primary action** in the accent colour.

**Widgets** live in `GizmoStyle.hpp`: `gizmo_label_w`, `gizmo_row_slider`, `gizmo_row_switch`,
`gizmo_segment`, `gizmo_hint`, `gizmo_panel_width`. New panels use those.

### What still has to migrate

**Done.** Branch **`port/gizmo-style`** (merged as `a41a0c7bd8`) migrated Perforations, Hole walls,
Edge chamfer / fillet, Align & Stack and Holes; Support Zones followed on **`port/SU-5-widgets`**.

| Gizmo | ID | State |
|---|---|---|
| **Extrude** (bare `Extrude` / `Cavity fill`) | HF-3 | **reference** - built on the shared widgets |
| Perforations | PP-1 | migrated; pattern row is 4 icon buttons (see the icon button standard) |
| Hole walls (Bridging / Strengthen) | PF-2b / RF-1 | migrated (one commit: both modes are one function) |
| Edge chamfer / fillet | EF-1 | migrated |
| Align & Stack | AS-2 | migrated; its `SetWindowSize`-after-`Begin` width never stuck and is now set before `Begin` |
| Holes | ME-2 | migrated; category image buttons and the catalogue dropdown kept as they were |
| Support Zones | SU-5 / SU-6 | migrated; its four private widget copies are gone (see below) |

Both original follow-ups are done:

1. **Support Zones' widgets are consolidated** (`port/SU-5-widgets`). Deleted and replaced by the shared
   implementations: `gizmo_row_slider` (which had a *different signature* - no column, no typed field,
   so its nine call sites go through a thin `zone_row_slider` that supplies the panel's one column),
   `gizmo_row_toggle` (a `Checkbox` -> `gizmo_row_switch`), `gizmo_glyph_toggle` (chrome ->
   `gizmo_icon_toggle_sized`) and `gizmo_label_col()` (-> `zone_label_w()` over the shared
   `gizmo_label_w`). `gizmo_u()` moved into `GizmoStyle.hpp` as the fork's one font unit, and
   `gizmo_icon_toggle` became a template so a caller can pass a capturing lambda. What stays local is
   what is genuinely this panel's: `draw_glyph`, `fit_text`, the zone cards, `gizmo_section`,
   `gizmo_stat_tile`, `gizmo_status_chip`, `gizmo_warn_row`, and `gizmo_glyph_button` (a button with a
   danger tint, not a toggle - no second user yet).
2. **`GizmoStyle.hpp`'s header comment** now names its real users in this tree.

### The icon button standard (PP-1)

An icon toggle whose whole meaning is its drawing, with the name on hover. It came out of the
Perforations pattern row and is **the** standard for this kind of button — Support Zones still draws
its own copy (`gizmo_glyph_toggle` in `GLGizmoSupportZones.cpp`) and moves onto this one when its
widgets are consolidated.

| | |
|---|---|
| button | `gizmo_icon_size()` = **28 px** at the reference font, scaled like `gizmo_panel_width()` so it grows with the font/DPI |
| glyph | **0.78** of the button (`kGizmoIconGlyph`) — the chrome is a frame, not the thing the artwork sits in |
| corner | `size * 0.15` |
| off / hover | `Surface` / `SurfaceHi`, glyph `TextDim` at 0.85 |
| on | `Accent` fill at 0.85 (1.0 hovered) + a `AccentBright` hairline, glyph `Ink` |

`gizmo_icon_toggle(id, active, tip, draw)` takes a **function pointer** to the glyph drawer
(`void(ImDrawList *, const ImVec2 &, float, ImU32)`), so the chrome is shared and each gizmo keeps its
own iconography — no `std::function`.

**The plate-and-pattern language.** Both icon rows that exist so far (Perforations' four patterns,
Holes' three operations) are a square plate with something cut into it, and the plate plus its margin
are shared: `gizmo_glyph_plate()` and `kGizmoGlyphInset` / `kGizmoGlyphStroke` in `GizmoStyle.hpp`.

**The one margin rule.** The pattern box is `kGizmoGlyphInset..1-kGizmoGlyphInset` (0.21–0.79) of the
glyph box for **every** variant; the plate's own rect is `0.12..0.88`, so each pattern keeps the same
wall to the border and no variant reads as crammed against it. Perforations learned this the hard way —
Grid's bars and the Honeycomb clip each had their own margin and looked wrong next to Lines, which was
the reference. The one deliberate exception is the mouth of a hole, which has to reach the face.

**Two size traps, both of which have already bitten:**

- **`ImGuiWrapper::push_toolbar_style()` must come BEFORE `gizmo_push_window_style()`.** It pushes an
  **opaque** `ImGuiCol_WindowBg` (`COL_WINDOW_BG_DARK` is `(45,45,49)` at alpha 1.0), and the last push
  wins when `Begin` resolves the window colour. Called after ours it silently removes the panel's
  alpha, so those panels looked solid while Align & Stack and Support Zones - which never call it -
  kept `PanelBg`'s transparency. Measured on the panel background: the five opaque panels read
  `(45,45,49)`, the two correct ones `(38,38,39)` (= `PanelBg` at 0.8 over the canvas). Patch
  `60_panel-alpha.patch`. Same class of bug as the label column: two widgets setting one value, and the
  order decides which wins.

- **Clamp any gap or stroke to a minimum pixel width.** A gap that is a fraction of the glyph
  (`0.019·s` for the honeycomb cell wall) is 0.4 px on a 28 px button, and ImGui does not antialias two
  adjacent filled polygons against each other — the gap simply vanishes and the comb stops reading as
  cells. `draw_glyph` in Support Zones protects its strokes the same way: `max(1.2, s·0.075)`. This is
  why the honeycomb looked different at 28 px from how it looked at 40 px even though not a single
  coordinate had changed.
- **Check the glyph at the real size, not in the mockup.** The mockups are SVG, which antialiases; the
  panels are drawn with `ImDrawList`, which does not. A mockup can therefore show a gap that will not
  exist on screen. `ui_screenshot` exists for exactly this: capture the panel as the app draws it.
- **This ImGui (1.83) has no `AddConcavePolyFilled`.** `AddConvexPolyFilled` fan-triangulates, so a
  concave outline comes out as its own inverse — the Holes chamfer glyph's bucket-shaped hole drew as a
  solid block with a notch until it was split into three convex pieces (a rect plus two flare
  triangles) overlapped by half a pixel, since two adjacent fills of one colour otherwise leave the
  background visible between them.

**Where each drawing lives.** Anything drawn is *code*, in the gizmo that owns the iconography
(`pp_*` in `GLGizmoPerforation.cpp`, `me_*` in `GLGizmoHoles.cpp`), never a resource, so it scales with
the DPI without asking the resource packer for anything and a wrong glyph is fixed where it is used.

### The panel width (one expression, in font heights)

```cpp
inline float gizmo_panel_width() { return std::max(380.f, 22.f * gizmo_u()); }
```

Support Zones' own expression, promoted to the standard (patch `62_panel-width.patch`). The first
version was a fixed **360 px at the reference font** (`360 * font/13.5`), which is 26.7 font heights
against Support Zones' 22 — so every slider in the migrated panels came out about a third wider than
the one the owner was comparing them against. In font heights the panel also keeps its proportions at
any font or DPI, which a pixel width does not, and the 380 floor stops a small font squeezing a panel
into nothing.

**Two rows were sized for the wider panel** and had to change with it, because they were the widest
thing in their panel:

- Holes' **head choice** (`None | Socket | Button | Sink`) lost its caption and sits on its own line.
  Four head names plus the label column do not fit 396 px, and the names read on their own.
- Hole walls' strengthening **`Strengthen all vertical holes` + `Clear strengthening`** went from one
  row to one button per line — together they are ~415 px.

Anything that must fit a panel width should be measured against `gizmo_panel_width()`, never against a
number copied from a mockup.

### The recipe, as HF-3 does it

```cpp
ImGui::SetNextWindowSize(ImVec2(gizmo_panel_width(), 0.f), ImGuiCond_Always);
gizmo_push_window_style();                       // BEFORE GizmoImguiBegin - it sets WindowBg
GizmoImguiBegin(get_name(), ...AlwaysAutoResize | NoCollapse | NoTitleBar);
gizmo_push_panel_style();                        // widget colours + rounding
    ...
    const float label_w = gizmo_label_w(_u8L("Both directions").c_str());
    gizmo_row_slider("##eh", _u8L("Height").c_str(), &h, min, max, "%.2f", label_w, tooltip);
    gizmo_row_switch("##eb", _u8L("Both directions").c_str(), &both, tooltip);
    gizmo_segment("##mode", labels, n, selected, tooltip);
    gizmo_hint(_u8L("...a sentence that wraps...").c_str());
    // right-aligned primary action in the accent colour
gizmo_pop_panel_style();
GizmoImguiEnd();
gizmo_pop_window_style();                        // AFTER GizmoImguiEnd - skipping it leaks the
                                                 // light panel background into EVERY other panel
```

Two traps the header documents and that bit here:

- `gizmo_push_window_style()` **must** go before `GizmoImguiBegin()` (ImGui resolves `WindowBg` when
  the window opens) and its pop after `GizmoImguiEnd()`. Skipping the pop leaks the background into
  every other gizmo panel.
- A `PushStyleColor` whose pop is skipped by an early `return` leaks into every ImGui window. If a
  panel body has early exits, put the body in its own function and keep push/pop in the caller.

### HF-3 in one line

`GLGizmoExtrude` (renamed from `GLGizmoHoleFill`) now has two modes - **Extrude face** (default) and
**Cavity fill** - a click **picks** a face (it no longer extrudes), Height/Offset reshape the
preview live, and a single **Extrude** button commits. Highlights are the fork's own values (teal
hover, green pick, orange preview) and the object is ghosted while a face is picked. See
`porting/notes/HF-3.md`; the mockup the design was signed off against is
`docs/superpowers/extrude-gizmo-mockup.html` (gitignored - planning output).


## 29. EF-1b - the Edge chamfer / fillet mode row as icon toggles (branch `port/ef-mode-icons`)

The Edge chamfer / fillet panel's `Chamfer | Fillet` segmented control is now **two icon toggles** in
the icon-button standard of section 28 - the same shape as the Holes panel's operation row. Nothing else
in the panel moved, and the two names the buttons already had (`m_desc["chamfer"]` / `m_desc["fillet"]`)
went with them: they are now the button id and the hover tooltip, so no string was added, changed or
lost and there is nothing new to translate.

- **The glyphs are a part face with one corner dressed.** The plate is the one `gizmo_glyph_plate()`
  draws (rect `0.12..0.88`, rounding `0.06·s`, stroke `kGizmoGlyphStroke`), and its top-right corner is
  either cut at 45 degrees (chamfer) or rounded (fillet). `edge_dress_chamfer_glyph` /
  `edge_dress_fillet_glyph` are thin wrappers over `edge_dress_corner_glyph(dl, p, s, col, fillet)`. The
  outline is convex either way, so `AddConvexPolyFilled` + `AddPolyline` are enough - no concave fill
  (section 28).
- **🔑 Why the two differ by geometry and not by colour.** At rest an idle icon is repainted in a single
  flat ink colour (section 25), so the corner cannot be tinted the way the toolbar icon tints its fillet
  arc (`toolbar_edge_dress.svg` draws the arc in the accent). Shape is the only channel available.
- **🔑 The dressing size is the whole legibility budget.** `EDGE_GLYPH_CORNER = 0.36` of the plate side.
  An arc and the chord between the same two ends differ by `r·(1−cos 45°) ≈ 0.29·r`, which at the
  reference 22 px glyph is ~1.7 px, about one and a half stroke widths - that bow is what makes the
  curve read. Below ~25 % the two glyphs stop telling each other apart at 28 px. A solid-silhouette
  version was legible sooner but would have been the only solid glyph in the panel family.
- **Verification.** Build green (incremental, `-s --no-configure`). Panel captured with `ui_screenshot`
  in both modes, at 1× and 3×, on a 20 mm cube: each glyph reads as intended both active and idle, and
  next to its partner. The click that switches mode is a user click-test (there is no synthetic-click
  tool); the widget is `gizmo_icon_toggle`, shared with the Holes and Perforations rows. **Dark theme
  only** on screen - the light path is the same widget and the same `GizmoCol` lookups as every other
  fork icon.
- **Not done.** The Holes panel's `##rim_type` Chamfer/Fillet row (`GLGizmoHoles.cpp`) is still a
  segmented control: it picks the profile of a *hole mouth*, not of a part edge, so it wants its own
  glyph pair rather than these two.
- **Branch state**: on `port/ef-mode-icons`, branched from `port/integration`. Merged `--no-ff` as
  `db7e763343` (user-verified in the app 2026-10-17: both glyphs read as intended, the mode switches and
  the tool still dresses an edge).


## 30. HF-4 - the Extrude face distance is signed (branch `port/extrude-distance`)

The Extrude tool's **Height** is now **Distance**, and it is *signed*: a positive value grows a solid out
of the picked face (what Height did), a negative one adds the very same prism as a **negative volume**,
so it cuts a pocket into the model. Nothing else in the tool moved - the pick, the offset, Both
directions and cavity fill are untouched, and the shape of the prism at a given magnitude is identical.

- **The signed mapping.** `picked_prism()` builds the prism in the face's flattened frame, where `+z`
  grows out of the face: `height = |d|` and `z0 = both ? face_z - 0.5·|d| : face_z + min(d, 0)`. The same
  code path therefore puts the solid on either side of the surface, and `SurfaceExtrude::extrude_outline()`
  - which rejects `height <= 0` - never sees a negative. A distance of exactly 0 builds nothing and the
  panel says which way the sign goes, rather than adding an empty volume.
- **The sign picks the volume type.** `create_from_pick()` adds a `ModelVolumeType::NEGATIVE_VOLUME` for
  `!both && d < 0`, the same type the Holes, Edge dress and Perforations gizmos use, and a `MODEL_PART`
  otherwise. *Both directions* always straddles the face, so it ignores the sign and always produces a
  solid. The volume is named `Extrude#<id>` either way, so `is_fill_volume()` still claims it: it counts
  as applied, is what the red preview draws, and right-click / `clear_all` removes it.
- **Bounds.** `EXTRUDE_DISTANCE_MIN/MAX = -50 / 200` mm. The negative side only has to reach through a
  real part, which is what fixes its bound; the typed field beside the slider sets a precise value.
- **MCP.** `extrude_gizmo`'s `set_extrude` takes `distance`, still accepts `height` as the old name, and
  `status` reports `distance`. ⚠️ **A renamed MCP parameter is not callable from a session that started
  before the rename**: the client keeps the tool list it fetched at session start, and it drops an
  unknown property *silently* - the call returns `ok` with the value unchanged. Measured 2026-10-17:
  `set_extrude {distance: -7}` left the distance at -5, while `{height: -7}` applied it.
- **Verification.** Build green. On `20mm_cube.obj` through MCP: pick the -Y face and `create` with
  `distance = -5` reports the fill's object-space centre at y = **-7.5**, i.e. a 5 mm prism inside a cube
  that spans ±10; the old `height = +5` reports **-12.5**, outside it. The panel was read back at 1x in
  the app and shows `Distance -5.00`. The volume *type* is not exposed by any MCP tool, and
  `export_gcode` and `get_print_estimate` both hang in this build (untouched here and pre-existing), so
  what remains is the user's click-test: the pocket is visible on the face the moment it is applied.
- **Not done, pre-existing.** Parameters are baked when the face is committed (clear and re-apply to
  change one), and a negative distance together with the *Holes* target cuts the holes' own filled
  outline - empty space, so it removes nothing.
- **Branch state**: on `port/extrude-distance`, branched from `port/integration`. Merged `--no-ff` as
  `51be36ff37` (user-tested 2026-10-17 in a preview build that carried it with HF-5 and HF-6).


## 31. HF-5 - a panel flips to the left anchor sooner (branch `port/panel-flip-margin`)

The anchor rule from UI-1 (section 27) - left under the icon while the panel fits, hung to the left of the
icon once it would run past the canvas edge - flipped only on a genuine overrun. A panel that came *close*
to the edge therefore spent a band of widths butted up against it, which is what the owner reported as
wrong on the Holes tool ("it does not touch the edge but is very close to it"). The test is now
`gizmo_panel_near_right_edge()` in `GizmoStyle.hpp`, with `kGizmoPanelEdgeMargin = 1.0` icon: within one
icon cell of the canvas's right edge the panel is anchored at the icon's right edge instead.

- **Measured**, window 2552 px wide, every fork panel 396 px (**screen x**, from `ui_screenshot`, which
  reports the panel's own rect): Holes 2137, Hole walls 2085, Support Zones 2033, Align & Stack 1981,
  Perforations 1945, Extrude 1893, Edge dress 1841. Holes therefore sat `2552 - (2137 + 396)` = **19 px**
  from the edge, the next one (Hole walls) **71 px**. One icon cell is ~33 px of icon plus its spacing
  here, so one icon flips **Holes and nothing else**, which is the wanted effect; two icons would start
  moving Hole walls as well.
- **After**: Holes reports x = **1774** with its right edge at 1774 + 396 = **2170**, which is its icon's
  right edge (icon left 2137 + 33) - it hangs to the left of its own icon now. The other six panels report
  *exactly* their previous x, so nothing else moved at this window size.
- **One knob, one helper.** The four panels that had the condition copy-pasted (Holes, Edge dress, Extrude,
  Perforations) now share the helper, and the margin is the single constant - in icon cells, so it scales
  with the toolbar rather than with the font, and every caller moves together.
- **Verification**: build green, panel rects before and after as above (only the dark theme was needed;
  nothing here is theme dependent). The user's own check is a *resize*: the band of widths at which Holes
  hangs left is now one icon wider than it was.
- **Branch state**: on `port/panel-flip-margin`, branched from `port/integration`. Merged `--no-ff` as
  `ea6751d660` (user-tested 2026-10-17 in a preview build that carried it with HF-4 and HF-6).


## 32. HF-6 - the Extrude mode row as icon toggles (branch `port/extrude-mode-icons`)

The Extrude panel's `Extrude face | Cavity fill` segmented control is now two icon toggles in the
icon-button standard of section 28, with the name on hover. The two names the buttons already carried
(`_u8L("Extrude face")` / `_u8L("Cavity fill")`) are now the button ids and the tooltips, so no string was
added or lost. This is the third row of its kind: Holes' operations (ME-2), Edge chamfer / fillet's modes
(EF-1b) and this one.

- **The glyphs.** The same part face twice, with the feature either standing out of it or recessed into
  it: a solid **boss** for Extrude face, a **plug flush in a slot** for Cavity fill. Both share one
  footprint (`EXTRUDE_GLYPH_FEATURE` = 0.40 of the plate side) and one depth (`EXTRUDE_GLYPH_DEPTH` = 0.24
  of the glyph box), mirrored about the face's top edge, so the pair reads as one thing done in two
  directions.
- **🔑 The face's top edge is at 0.30, not at the shared plate's 0.12.** The glyph box leaves only 12
  units of headroom above the shared rect - 2.2 px at the real 28 px button - which is too thin for a boss
  to read as standing *out*. Lowering the face buys the boss a 24-unit rise, and the face still reads as a
  square (a slightly wide one).
- **🔑 The recess is a slot outline plus a plug, not a notch in the silhouette.** A notch makes the
  plate's outline concave, and this ImGui (1.83) has no concave fill: the plate would have to be tiled
  from convex pieces, and two adjacent 26% fills either show a hairline of the panel between them or
  double up into a darker seam (the me_chamfer note in GLGizmoHoles.cpp). The mockup the design was signed
  off against dipped the plate's outline at the recess; the shipped version keeps the approved composition
  (squat face, boss out / plug flush, one footprint, one depth) and the plate's fill, and shows the recess
  with the slot's own stroke. Going outline-only for this pair would allow the dip, if the owner prefers
  that look over matching the other panels' filled plates.
- **Verification.** Build green; both glyphs captured at 3x in both states through `ui_screenshot` - the
  boss reads as standing on the face, the plug as closed flush in its slot, and each reads beside its
  partner. The click that switches mode is a user click-test (no synthetic-click tool); the widget is
  `gizmo_icon_toggle`, shared with the Holes, Perforations and Edge chamfer / fillet rows. Dark theme only
  on screen.
- **Branch state**: on `port/extrude-mode-icons`, branched from `port/integration`. Merged `--no-ff` as
  `0afb593498` (user-tested 2026-10-17 in a preview build that carried it with HF-4 and HF-5).


## 33. HF-7 - the applied Extrude volumes are coloured by what they do (branch `port/extrude-applied-colors`)

A committed volume was drawn as one overlay in the hole gizmo's applied red at alpha 0.80, which reads as
a solid block of paint and says nothing about what the volume does. It is now **two** overlays, split by
the sign of the distance that made the volume, at the preview's own translucency (0.55 - the alpha the
orange prism uses): **green** for a solid that adds material, **red** for a cut that removes it.

- **The split is the volume TYPE, not a name parse.** `create_from_pick()` already chooses `MODEL_PART`
  vs `NEGATIVE_VOLUME` from the sign (HF-4), so `rebuild_preview()` sorts on
  `v->type() == ModelVolumeType::NEGATIVE_VOLUME` into `m_preview_applied_add` / `m_preview_applied_cut`,
  and `on_render()` draws both.
- **The green is the pick's own green** (`PICK_COLOR`'s RGB): one ramp, and the two share a meaning -
  "material this tool is acting on". It is also what the preview would have shown for a positive
  distance, so the applied colour does not jump when the volume is committed.
- **Cavity fill lands in green as well**: it commits a positive `MODEL_PART`, and by material it does add.
- **MCP**: `list_fills` now reports `negative` per fill volume, so the sign is observable from a script -
  which is how this change was verified.
- **Verification**: a 20 mm cube scaled 4x (so the overlays are large on screen), `+12` top-to-bottom
  through MCP: `+12` draws a **green** slab and `-12` a **red** pocket, and in both captures the plate's
  own "snapmaker" text and grid read *through* the overlay (that is the translucency, checked, not
  assumed). `list_fills` reports `negative: false` then `true`. Captured with `ui_screenshot` region=window,
  dark theme.
- **Left alone deliberately**: the orange prism preview (that is "what you are about to make" - the user's
  "yellow highlight"), and the other gizmos' applied red at 0.80 (each has its own constant, and a hole
  ring or a rim dress has no sign to colour by).
- **Branch state**: on `port/extrude-applied-colors`, branched from `port/integration`. Merged `--no-ff`
  as `b3658fd246` (user-tested 2026-10-17; the same batch then carried the shared colour scheme in
  section 34).


## 34. PP-3 - one 3D overlay colour scheme for every fork gizmo (branch `port/gizmo-colors-perforation-pick`)

Every fork gizmo that draws a preview or a committed volume on the part now uses one meaning per colour,
defined once at the bottom of `GizmoStyle.hpp` (`gizmo_surface_colour()` and friends):

| role | colour | sites |
|---|---|---|
| the surface the cursor is POINTING AT | teal .55 | Holes place-on-face, Perforations, Cut pick-face, Cut shaped cut, Move pivot, Align & Stack mate hover, Extrude hover |
| what you CLICKED - the action's target | green .55 | Extrude pick, Perforations pick |
| the geometry the panel values WOULD make | yellow .55 | Holes hovered feature + place-on-face ghost + active snap marker, Edge dress hovered edge, Hole walls hovered counterbore, Perforations proposal, Extrude prism preview |
| committed material REMOVED | red .55 | Holes applied, Edge dress applied, Perforations applied, Extrude cut |
| committed material ADDED | green .55 | Extrude solid, Hole walls strengthen rings |

- **🔑 Every role is TRANSLUCENT (0.55).** The applied reds were 0.80 and the hover greens 0.90, which read
  as solid paint on the part. An overlay is there to say WHERE, not to hide the part.
- **🔑 "Pointing at" and "picked" must never read the same**: a hovered surface is teal and only a click
  turns a surface green (the owner's rule, 2026-10-17). Candidate/available hints stay blue.
- **Hole walls is the one gizmo whose "applied" overlay is ADDED material** - in Strengthen mode it holds
  the reinforcement rings, a `PARAMETER_MODIFIER` - so it takes the scheme's green, and it was the only
  place where an applied red was not a cut.
- **Deliberately left alone**, each for a reason that would otherwise collapse two colours into one:
  Extrude's *picked face* stays green (its yellow is the prism preview on that same face), Align & Stack's
  mate pair stays green/orange (source vs target), the snap markers stay categorical (one of five hint
  colours is green, but they are hints, not selections), and Support Zones' `kTargetCol` green stays
  (it means "taken", a paint state).
- The teal was unified to one alpha (0.55) at the same time - Perforations was the one site at 0.40 - so
  the fork has one hovered-surface value rather than five copies of the same RGB.
- **Verification**: build green. The four new Perforations overlays exercise the picked/proposed/cut
  values end to end over MCP (captures in section 35): green picked face, olive where the yellow proposal
  stacks over it, red over the applied cut. The **hover** colours sit under the real cursor, which no MCP
  tool can move, so those three - Holes' hovered feature, Edge dress' hovered edge, Hole walls' hovered
  counterbore - are the user's click test. The colour *assignment* is a one-line grep per file.
- **Branch state**: on `port/gizmo-colors-perforation-pick`, branched from `port/integration`. Not merged
  until the user has tested it (section 0).


## 35. PP-2 - Perforations becomes pick -> adjust -> cut (same branch)

The Perforations tool no longer stamps the pattern where you click. A left click **picks** the face
(nothing is cut), the panel's values reshape a **live preview** on that face, and the panel's
**Perforate** button commits the cut - the same shape as Extrude, as the owner asked.

- **The pick.** `Pick {mv, facet, hit_obj, horizontal}` is cached by `pick_at()`: the pattern is anchored
  where the user clicked and only the values move it, so the anchor is remembered instead of being
  recomputed from the cursor every frame (which is exactly what the old hover ghost did - the ghost
  followed the cursor and the click cut where it happened to be). Right-click or Esc drops it.
  `data_changed()` keeps it across a volume-list change, because that is usually this tool's own cut, and
  drops it only when the volume it pointed at is gone.
- **Four overlays**, in the scheme of section 34: the surface under the cursor (teal), the picked face
  (green), the pattern the values would cut (yellow), every committed perforation (red). The hover
  highlight is suppressed while the cursor sits on the picked face, and the tool's own negatives are
  picked *through*, so clicking an already-cut area still picks the face behind it.
- **The applied overlay is new.** Committed perforations are read back from their volumes and drawn red,
  so an already-perforated area is visible in the tool for the first time (before, only the real negative
  volume in the scene showed it).
- **Rebuild policy.** The proposal is one mesh per hole, so it is rebuilt only when it can have changed: a
  new pick, a value change, or the release of the slider that set it (the existing `m_editing` "not while
  dragging" rule). Rebuilding it per frame is what made the old hover ghost flicker while a slider moved.
- **MCP.** `pick_at`, `create`, `clear_pick`; `status` reports `pick_valid` and `applied_count`; `apply`
  stays pick + create in one call, so a PP-1 script behaves exactly as it did.
- **Strings.** `click_hint` was reworded and `drop_hint` / `perforate` / `applied` are new msgids. The
  button says **Perforate**, not "Cut", because Cut is a different tool in the same toolbar - and the
  button is the one that says what it does. The fork's catalogs are stale, so the gettext pass still has
  to pick these up.
- **Also in this batch**: an Extrude pick now survives its own create (it is dropped only when the volume
  it pointed at goes away). Its own comment had promised that, but `data_changed()` invalidated the pick
  the moment a volume was added, so pressing Extrude twice did not work; now it does.
- **Verification**: build green. On a 20 mm cube scaled 4x, all over MCP: `pick_at` -> `pick_valid` true and
  `applied_count` 0 (a pick cuts nothing); `set_params {grid, spacing 9, width 5, boundary 5}` -> the green
  picked face with the yellow proposal over it (olive where the two stack), green showing in the hole gaps
  and in the 5 mm boundary inset; `create` -> `applied_count` 1 and the region reads **red** with the green
  face in the holes, the proposal having hidden itself; `create` again -> 2 (one pick, two cuts);
  `clear_pick` -> `pick_valid` false, both cuts still counted. Extrude's pick was re-checked in the same
  run: after its own `create` it is still valid and a second `create` makes a second solid (before this
  batch `data_changed()` dropped it).
- **Two traps found on the way**, both worth keeping:  1. **An overlay drawn over the volume it IS loses the depth test.** An applied pattern's geometry is the
     negative volume already in the scene, at exactly the same depth, so a strict `GL_LESS` rejected the
     overlay and the red was never visible (the first check showed the real volume's grey instead). The
     overlay pass now draws with `GL_LEQUAL` and restores `GL_LESS` afterwards. Extrude sidesteps this by
     drawing its whole overlay pass with depth testing OFF, but that would show a whole-face pattern through
     the part, which is why this one keeps the test on.
  2. **Yellow over red reads as neither colour.** With the proposal and the applied pattern coincident, the
     region went muddy. The proposal now hides itself while it is exactly what was just cut
     (`m_show_proposal`) and comes back the moment a value or the pattern changes.
- **Branch state**: on `port/gizmo-colors-perforation-pick`, branched from `port/integration`. Merged
  `--no-ff` as `4654027520` (user-tested 2026-10-17; the same day's follow-up fixes are section 36).


## 36. PP-4 - overlay blending, cutters a boolean can eat, and right-click as the commit key (branch `port/gizmo-fixes`)

Six things the owner reported after living with PP-2/PP-3, and what each turned out to be.

- **A placed teardrop read as solid red, not a wash.** Measured, not guessed: a pixel inside an applied
  placed feature came back `(246,73,73)`, which is the scheme's 0.55 red **composited twice** over the
  part's face (a single pass is `(234,115,115)`). Two independent causes:
  1. **Two shells in one volume.** `bore_negative_mesh` built the round clearance bore and then
     `its_merge`d a teardrop prism into it - its own comment calls the teardrop "a superset of the round
     bore". One volume therefore held two overlapping closed shells, and the overlay blended both. The
     teardrop IS the shaft's cross-section now (`its_make_teardrop_prism` replaces the round bore instead
     of joining it; a vertical hole still gets the round one, since the prism comes back empty). The same
     doubled shell is what made **Mesh boolean fail on a teardrop** while a plain bore at least cut: two
     intersecting shells are exactly the input an mcut subtraction cannot resolve, and
     `GLGizmoMeshBoolean::generate_new_volume` replaces the source volume with whatever comes back.
     ⚠️ The *head* teardrop is still merged into the stepped bore (a local case, documented in the code):
     doing it properly needs a stepped section builder, not a merge.
  2. **A closed volume drawn without back-face culling**, which blends its far wall too. The overlay
     passes of Holes, Extrude, Edge dress, Hole walls and Perforations now keep culling ON. Measured on
     an Extrude boss (a single-shell volume): the green now reads as one 0.55 pass instead of two.
  Open: a *plain* vertical bore still measured the doubled value after both fixes, so a third blend
  source remains unpinned on that path. The next diagnostic is to sample the same pixel with the applied
  overlay disabled and with the volume hidden in turn.
- **A placed through-hole left a sliver of wall after Mesh boolean.** The cutter's far end already
  overshot by `max(0.5, 0.25·d)`, but its entry cap was exactly IN the picked face - a coplanar pair for
  the boolean. The cutter now starts `max(0.2, 0.1·d)` *outside* the surface, with `depth` still measured
  from the face, so the visible cut is unchanged and the superset is harmless for a negative volume.
- **Edge chamfer / fillet left a sliver, and cut past the end of the edge.** Both confirmed in the code:
  `chamfer_profile` starts at the edge corner with its legs lying exactly in the two faces (the same
  coplanar pair), and `mesh_for_edge` passed `margin = 0.5·size`, which `extrude_profile` applies as an
  extension past **both ends of the edge** - the cut ran past the pick as if there were solid there.
  There are two margins now: `side = max(0.05, 0.05·size)` pushes the whole cross-section out of both
  faces (invisible: the cut is still bounded by the faces) and `end = max(0.02, 0.02·size)` keeps only
  enough overlap for neighbouring features at a corner. `make_edge_fillet`/`make_edge_chamfer` and the
  loop sweep take a `side_margin`, with a new `[EdgeProfiles]` case pinning that the flush prism pokes
  out of neither face and the margined one pokes out of both by exactly `side`.
- **Right-click is now the commit key.** Holes and Hole walls: right-click presses **Done** (and inside
  place-on-face mode it cancels that mode first, so the key never traps the user). Extrude: right-click
  confirms the picked face, the same as its Extrude button. Perforations: right-click confirms, the same
  as Perforate. **Esc** is what drops a pick in those two now, and the hints say so.
- **Consistency**: the Cut tool's active snap marker was hardcoded opaque green while Holes' follows the
  scheme; both are the proposal colour now.
- **Test status**: the `[EdgeProfiles]` side-margin case **passes**, and so do all 19 pre-existing
  `[HoleDetector]`/`[EdgeProfiles]` cases with the PP-5 detector change in place (288 of 289 assertions).
  The new teardrop case **fails and is marked `[NotWorking]`**, deliberately: see section 37.
- **Branch state**: on `port/gizmo-fixes`, branched from `port/integration`. Merged `--no-ff` as
  `9cf2a9def0` (user-confirmed in the app: right-click works, both sliver reports are gone).


## 37. PP-5 - a teardrop hole is invisible to the Holes tool, and Mesh boolean stops working after a boolean

Two bugs reported after PP-4. **Both are fixed** on `port/PP-5-fixes`; built, the `libslic3r` suite
is 103170/103170, and both were runtime-verified over MCP. The stale-index fix from the merge is
kept - neither bug was that. The user still has to test the build before it is merged.

### A teardrop tunnel was not detected (no chamfer, no resize, no conversion to a round hole)

**What the instrumentation showed** (temporary `std::printf` before each `continue` in
`detect_holes`, one label per gate; run with the recipe below):

```
[PP5] seed pts=132 r=2.543 residual=0.902           <- the roof pulls the first circle fit out
[PP5] refit inliers=130/132 r=2.516 residual=0.986  <- the apex is back in the refit; 0.986 > 0.201
```

- The patch is the whole 132-face side wall (arc + roof, one smooth patch because they meet
  tangentially; the two caps are separate patches the cylinder test drops). The axis fit is fine - a
  roof plane contains the axis, so its normal is perpendicular to it like a wall's.
- The gate was the **circle residual**, and the band meant to exclude the roof selected **faces by
  centroid**. A teardrop roof quad is two triangles and the one sharing the arc's tangent vertex has
  its centroid ON the circle (2.635 against the seed r = 2.543, inside the 0.08 r band) while it
  still carries the apex vertex at 3.5 r. `inlier_pts` then took **all three vertices** of every
  inlier face, so the apex re-entered the fit, and `fit_circle()` returns a maximum residual - one
  apex vertex killed the patch. (The earlier note "a roof face's centroid sits ~0.35 r off" was
  wrong for this triangulation: half the roof triangles have a centroid on the circle.)
- **Fix**: band the **vertices** of the seed circle, refit on those and gate the residual on those;
  the roof faces stay out of the convexity average by centroid and the angular coverage uses the
  same vertices. Measured after: `circle pts=130/132 r=2.500 residual=0.000 tol=0.200`,
  `gap max=90.0 deg`, `convexity avg=-0.999`.
- The fixture's case (`tests/libslic3r/test_holedetector.cpp`, "A teardrop tunnel is detected as a
  hole") is un-tagged now; it was `[NotWorking]` (hidden, so the suite looked green).

### Mesh boolean did nothing once the object had already had a boolean

Reported with a perforation, an edge fillet and a place-hole-on-face cutter; the trigger was "an
earlier boolean". Ruled out **by measurement**, not by reading:

- **The stale index** - already fixed at the merge; the symptom survived it.
- **The raycaster array** - `raycasters_count == volume_count` at every step of the repro.
- **The activation gate** - `on_is_activable()` wants a full instance with more than one volume
  selected. A perforation and an edge-dress commit both leave the whole object selected, so the
  gizmo opens with the natural selection.
- **The boolean on clean input** - a second Difference with two counters on a fresh box worked
  (12 -> 900 -> 2140 triangles).

**Root cause**: `extrude_perforation_pattern()` (`PerforationPattern.cpp`) built the cutter with
**three fresh vertices per cap triangle** plus its own per wall strip - geometrically closed but
topologically an open soup (measured 360 open edges, 90 patches on a honeycomb; a proper cutter is
12 patches, 0 open edges). mcut is handed that as a *solid*: it fails or crashes, and a result made
from one is not watertight either, so the **next** boolean fails. That is "does nothing once the
object has already had a boolean" - and the crash is real: an `ACCESS_VIOLATION` inside mcut's
`hmesh_t::source`, on a honeycomb-cutter result fed to a circles-cutter Difference. The edge-fillet
and place-hole cutters are watertight, which is why every *second* operation failed whichever of the
three the user tried: the first boolean's result was the broken input.

**Fix**: `its_merge_vertices()` at the end of `extrude_perforation_pattern()`. The cap and wall
vertices come from the same `Point` coordinates through the same scaling, so the weld is exact.
After: cutter 0 open edges; honeycomb-then-circles runs clean (904 -> 7604 triangles, 0 open edges).
New `[PerforationPattern]` case pins the closed solid for Lines/Grid/Honeycomb/Circles.

### Reproducing it without a human (the tool that made this possible)

The reproducer is a UI sequence, but the whole thing is now scriptable: MCP-3's
`mesh_boolean_gizmo` opens the gizmo, picks a volume by index (no raycast) or at a screen point (the
interactive ray; `probe_pick` finds a pixel that reaches a cutter), reports
`raycasters_count`/`volume_count` and each volume's `open_edges`, and `run` does mode + both picks +
apply in one call. Flow: box -> perforation cutter -> Difference -> second cutter -> Difference. The
`open_edges` column is what pins "mcut cannot eat this".

### Running the failing case, and instrumenting it (this tree ships with the tests off)

```powershell
# one-time per session: turn the tests on in the existing tree, build the suite, run the case
cmake -B build -DBUILD_TESTS=ON
cmake --build build --config Release --target libslic3r_tests -- -m
build\tests\libslic3r\Release\libslic3r_tests.exe "A teardrop tunnel is detected as a hole" -s
cmake -B build -DBUILD_TESTS=OFF      # put the tree back the way the build script expects
```

- `-s` prints the passing assertions too.
- The instrumentation that produced the numbers: a temporary `std::printf("[PP5] ...\n", ...)`
  immediately before each `continue` in `detect_holes`, one label per gate (removed again; the
  numbers live in the case's comment and in `porting/notes/PP-5.md`).
- Tag a case `[NotWorking]` while it is knowingly red and say why - a hidden case is invisible to
  `ctest`, which is why this section exists at all.

- **Merged** into `port/integration` as `21aa42f159` (user-tested 2026-10-17). Three product
  commits (`ee9e313d1f` MCP-3, `8f69ee6840` teardrop, `a1aeb336af` boolean) plus the `port:` docs
  commit; `port/PP-5-fixes` is pushed. Section 37 is closed.


## 38. PP-6 - Perforations: the angle that would not let go, grooves for Grid, and a depth you pick (branch `port/PP-6`)

Five follow-ups on Perforations the owner asked for after living with PP-1/PP-2, plus the MCP schema
regeneration MCP-3's own note asked for.

- **Lines turns through the full quarter.** The panel's angle bound was a single shared constant,
  `PERFORATION_MAX_ANGLE_DEG = 45`, used in exactly one place. The reasoning behind the 45 (a family
  plus its 90-degree cross covers every orientation) is true for a **pair** and false for a **single
  family**: Grid at θ and at 90-θ is the same pair mirrored, but Lines at 100° is not Lines at 80° - it
  is that family mirrored, which is different geometry on an asymmetric face. So 45° was only ever the
  diagonal and **vertical lines were unreachable**. `PERFORATION_MAX_ANGLE_LINES_DEG = 90.0` now sits
  beside it, the header comment says which bound belongs to which pattern, and `render_panel_body()`
  picks `ANGLE_MAX` from the current kind. Nothing in the generator changed - `line_family()` rotates
  the domain by `-theta` and always accepted any angle, so this was a UI bound, not a geometry limit.
- **An angle the new pattern cannot express goes back to 0.** This is the bug the owner reported:
  switching Lines → Grid with the angle over 45 left the old value in place, so the slider sat pinned
  at its maximum while the geometry went on using 80°. `perforation_angle_for_kind(kind, angle)` is the
  one rule - in range it is **kept** (so switching to the wider pattern never disturbs a value Grid was
  using), out of range it is 0 - and it runs on the panel's pattern row and in the MCP `set_params`,
  where it is applied after every field so the order of a request cannot matter. An `angle` sent in the
  same call as `pattern` is deliberately **not** immune: grid-at-80 is a state the panel cannot show,
  and it is exactly the state this rule exists to remove. Worth stating plainly: no in-range Grid angle
  reproduces an 80° grid anyway (Grid at 80° is the family pair {80°, 170°}, i.e. {80°, -10°}; the
  in-range mirror is 10°), so something had to move whatever the rule: the point is that the number
  and the picture agree afterwards.
- **Grid is always cut as grooves.** With `invert` off, the two crossing families subtracted as *holes*
  leave the material between the crossings as squares that touch nothing - floating regions the slicer
  cannot print. That is the only thing a user who picks Grid ever wants the complement of, so
  `make_perforation_pattern()` now forces
  `invert || kind == Grid` **in the builder** - one place every caller routes through, so neither the
  panel nor the MCP tool nor a test can ask for the island version - the Invert row is not drawn for
  Grid, and both the panel's pattern row and `set_params` set the flag so the state on screen matches
  what is cut. The curve's cost is that the row disappears when Grid is selected; the panel is
  auto-sized, so it just gets shorter.
- **"To": a second face decides the depth.** The depth probe stops at the first surface behind the
  picked face, which is right for a wall and wrong when the pattern has to cross a void or stop on a
  chosen far wall. The panel now has a **To** row: `Pick face` arms a pick, the next left click sets a
  target face, and the pattern stops there. The target is accepted only when it is **parallel**
  (`|n·n_to| >= 0.99`) and **behind** the picked face (plane projection < 0); anything else sets a
  reason that the panel shows as an amber warning and the MCP tool returns as text
  (`gizmo_target_refusal()` maps one enum to both, so neither side parses the other's strings). The
  distance is the plane-to-plane projection in object space, so the target's lateral offset does not
  matter. The cut ends `PP_TARGET_OVERSHOOT = 0.2` mm **past** that face: stopping exactly on it would
  put the prism's cap coplanar with a real surface, which is the sliver PP-4 taught us to avoid, and a
  fifth of a millimetre is under one extrusion width (200000 units at the slicer's 1e-6 mm
  resolution). The target face is drawn in the picked green - the two green patches are the two ends of
  the hole path - and it is dropped with the pick, on a volume-list change that removes its volume, and
  when the tool closes, because it is measured from the picked face and cannot outlive it. Two things
  the panel does that a click cannot undo: a right click while the pick is armed *cancels* it instead
  of committing a pattern, and the row is drawn only once there is a picked face to measure from (no
  dead control, and no `ImGui::BeginDisabled`, which this tree's ImGui does not have).
- **The proposal follows a drag.** `on_render()` rebuilt the yellow pattern only when
  `! m_editing && m_pick_dirty`, where `m_editing` latched `ImGui::IsAnyItemActive()` and re-armed the
  flag when the item went inactive - so the highlight only caught up when the mouse was released. The
  sliders already set `m_pick_dirty` on **every** frame whose value changed, so the gate was the whole
  delay. Its stated reason died with the cursor-following hover ghost that PP-2 replaced with a
  preview anchored to a cached pick; `GLGizmoExtrude` has rebuilt its own preview live
  (`m_preview_dirty`, no gate) since it was written. The gate and the `m_editing` member are gone.
- **Cost, accepted, no throttle** (owner's call): a rebuild is the whole pipeline - coplanar-region
  union, Clipper union of the pattern, triangulation, the PP-5 weld, then `GLModel::reset()` + a VBO
  upload. At the extremes the UI allows (spacing 1 mm on a large face) that is a few frames per second
  during the drag, and no throttle changes that: it is the mesh rebuild, not the frame rate. The
  builder's own `MAX_FEATURES = 20000` is what bounds it; a frame-time guard on `m_pick_dirty` is the
  upgrade path if it ever bites.
- **The MCP `angle` description** now carries both ranges, since that string is what a client shows
  the model: "0-90 for lines, 0-45 for grid. Honeycomb and circles are isotropic and ignore it." The
  tool also reports `to_valid` / `to_distance` on every call, and 'pick_to' / 'clear_to' join its
  actions.
- **Verification.** `build_win.bat -s --tests` green, then an incremental `-s --no-configure` after the
  one fix below; the touched translation units are in both logs. `[PerforationPattern]` is **9 cases /
  434 assertions**:
  - the new orientation case (0° slots wider than tall, 90° slots taller than wide - that is what
    "vertical lines" means, and it fails on the old bound, a 45° slot being a near-square diagonal).
    Not asserted on purpose: equal area between 0° and 90°. On that rectangle the two happen to agree
    (6 rows x 100 mm = 10 columns x 60 mm = 600 mm of slot) but that is the numbers lining up, not a
    property - the strip phase differs between the two axes.
  - the grid case: a Grid asked for with `invert = false` leaves **one** connected region standing
    (`diff_ex(inset, pattern).size() == 1`) and is area-identical to the explicit grooves call. The
    pre-existing case in the same file asserted the *opposite* contract (`grid.front().holes.size() >
    1`, the lattice-with-holes reading) and was updated with it - the one assertion this batch had to
    flip rather than add.
  - the angle rule: 80 → 0 on Grid, 30 and 45 kept, Grid → Lines kept, 120 → 0, and the isotropic
    patterns carry the pair bound.
  - Full `ctest --test-dir build/tests -C Release`: **2413 passed, 0 failed** (2411 before + the two new
    cases; seven `RequiresApp`/plugin cases skipped as usual).
- **Runtime, over MCP** on the 20 mm cube (Release build, `ORCA_EXT_MCP=1`): `open` → honeycomb/0/off;
  `pick_at` at the viewport centre → `pick_valid` true; `set_params {pattern: grid, angle: 80}` → the
  angle comes back **0** and `invert` **true**; `{pattern: lines, angle: 60}` → 60 kept; `{pattern:
  grid}` alone → 0 again (the reset works from a stale value, not only from a same-call one - which is
  what the first cut of the MCP rule got wrong, see below); `pick_to` on the same face → refused with
  "The target face has to be behind the picked one"; `create` → `applied_count` 1; `slice_all` →
  `idle` with only the two pre-existing warnings (objects too close, `precise wall` ignored).
- **What the MCP run could NOT reach - and the owner then checked it in the app**: the *accepted*
  target path. Three attempts over MCP, three dead ends, all geometry rather than code: a 20 mm cube is
  convex and symmetric, so the far face is always occluded and a 180 degree rotation maps it onto the
  near one; `rotate_object` or `mirror_object` leave face 1's object-space normal pointing away from the
  model, so nothing is behind it any more; and there is no MCP camera control to view the far side (the
  tool list has no view/orbit entry). A concave fixture - a pocket whose floor is a second visible
  parallel face - is what the check needs, and `tests/data` has none. It was therefore left on the manual
  checklist, and the owner worked through it: pick a face, orbit to a parallel face behind it, *Pick
  face*, read the distance, Perforate - **confirmed working 2026-10-17**.
- **One real bug this run caught**, in the first cut of the MCP rule: it reset the angle only when the
  kind *changed*, so `{pattern: grid, angle: 80}` in one call slipped through and produced exactly the
  grid-at-80 state the rule exists to prevent (the response said so: `"angle": 80.0`). The rule now runs
  after every field with `pattern` present, whichever order they arrive in.
- **Also in this batch: `scripts/tools_schema.py` regenerated.** The static list the bridge serves
  when the app is not reachable had **63** of the server's tools; the live server now reports **77**,
  the 14 new ones being `cut_gizmo`, `edge_dress_gizmo`, `extrude_gizmo`, `find_holes`,
  `get_app_preferences`, `get_log_tail`, `get_object_features`, `get_suppressed_dialogs`,
  `holes_gizmo`, `list_orcaext_keys`, `mesh_boolean_gizmo`, `perforation_gizmo`,
  `set_app_preference` and `ui_screenshot`. Regenerated against a build started with `ORCA_EXT_MCP=1`,
  and the file proves which binary produced it: `perforation_gizmo`'s `angle` description is the new
  one. Trap for the next time: the endpoint took about two minutes after launch before it accepted a
  request, so a regen that gives up after 60 s gets a false "not running" (the listener is up but
  `Invoke-WebRequest` times out); poll with `curl`/`urllib` and a longer budget.
- **Branch state**: **merged** into `port/integration` as **`23e8ec3f98`** (user-tested 2026-10-17) and
  pushed. `port/PP-6` held six commits (the angle + live preview; the angle reset / Grid grooves / To;
  two `tools_schema.py` regenerations; two `port:` docs+patch commits) and its merge tree is
  byte-identical to the branch that was built and tested. Everything in this section and the next is now
  user-verified in the app.

## 39. PP-6b - the pattern is anchored at the centre of the face (same branch `port/PP-6`)

The owner's report: with a spacing that does not divide the face, the cut at one boundary edge does not
match the cut at the parallel opposite edge. It was the phase, not the clipping.

- **What it was.** Three loops anchored the lattice on the bounding box's minimum corner: `line_family`
  laid its first strip at `y0 + spacing/2` and stepped by `spacing`; `honeycomb` and `circles` started a
  row at `bbox.min - dy` and drove the stagger from a *counter* that began at that corner. The phase was
  therefore f(`bbox.min`, `spacing`), so the leftover at one end of the face was unrelated to the
  leftover at the other, and the lattice was not symmetric about the face centre at all.
- **The fix.** Every index now runs symmetrically about the centre (`-n .. n`, with
  `n = ceil(farthest extent / pitch)`), features sit at `centre + k*pitch`, and the stagger follows the
  ROW INDEX's parity instead of a counter - so the mirror of every strip or cell about either centre axis
  is another strip or cell, in the negative half too (the parity of a negative index is its own, two's
  complement). Index ranges are built symmetric by construction rather than from the two ends, because a
  range derived from both ends can add one strip on one side in a sub-unit parity case. **Lines is
  included** and Grid inherits it twice over, being two `line_family` calls.
- **Why not the other ways out.** Deriving the spacing from the face would change a value the user set,
  against the tool's own rule ("the geometry uses exactly what is set; the UI never clamps one value
  against another"), and it cannot work for the staggered lattices at all: their row pitch is
  `spacing * sqrt(3)/2`, irrational against the column pitch, so no spacing puts a whole number of
  features across both axes. A button or a mode would add a second code path for a state that is never
  better than the centred one - the corner phase is not aligned to anything meaningful. Anchoring on the
  clicked point instead of the centre (the pattern's origin is already there) only matches the two edges
  if the click happens to be dead centre, so it does not solve the report.
- **What did not change.** The two `spacing` `width` `boundary` `angle` inputs, the margin, the clipping,
  the price of the extrusion, and the partial feature at the edge: a boundary still cuts through a cell,
  which the owner accepted. Nothing about the face is recomputed, and `spacing` is still exactly the
  pitch the user typed - only the phase moved. Each loop now places up to one extra feature beyond each
  edge (clipped by the intersection), and the `MAX_FEATURES = 20000` guard is untouched.
- **Verification.** A new symmetry case: a 180-degree turn about the **face** centre leaves the pattern
  unchanged, for all four kinds, `invert` both ways, and the angle both 0 and 30 where the angle applies;
  it also asserts that the inset's centre equals the face centre, which is what a uniform margin gives
  and which is why anchoring on the inset lands on the face. `[PerforationPattern]` is now **10 cases /
  474 assertions**; full `ctest` **2414 passed, 0 failed**. Runtime over MCP: all four kinds created on a
  4x cube face at `spacing 7` (80/7 is deliberately not whole) and sliced to `idle` with only the
  pre-existing `precise wall` warning.
- **Not reachable over MCP, checked by the owner instead**: seeing the pattern in the app. A negative
  volume is buried inside the wall it cuts, so the prepare view shows nothing new, and there is no MCP
  switch into the sliced preview. The eyeball check was left on the manual checklist - pick a face, use a
  spacing that does not divide it, compare the two parallel edges - and the owner ran it:
  **confirmed symmetric in the app 2026-10-17**.
- **Branch state**: same branch as section 38, one more product commit, docs and patch in the `port:`
  commit. No `tools_schema.py` regen: nothing about the tool's schema changed. Merged with the rest as
  `23e8ec3f98`.

## 40. MB-1 - a cutter can only be the tool, and the mode follows the picks (branch `port/MB-1`)

The owner, on a real project (`meshbooleantest.3mf`): perforation, mesh boolean, several edge chamfers,
mesh boolean again, then a hole added with **Place hole on face**, then "tried to mesh boolean through the
top of the perforated square and it doesn't mesh boolean and **makes the circle just vanish**".

**Measured on that object** - two volumes, an unnamed `part` (29 284 triangles) and `FacePocket#1`
**negative** (256), both closed:

| operation | result |
|---|---|
| Difference, source = the part, tool = the hole | hole cut: 29 284 -> **42 072** triangles, cutter consumed with Delete input - what the owner wanted |
| Difference, source = the hole, tool = the part (reversed) | the cutter is **replaced by its own leftovers outside the part**: 256 -> **12 796** triangles, renamed `FacePocket#1 - difference`, still `negative`; part untouched -> "no boolean, the circle vanished" |
| Union (the mode the gizmo opens in) | one volume, **43 786** triangles, cutter consumed, no hole - also "the circle vanished" |

And a raycast sweep over the visible circle, with the gizmo's own `probe_pick`, shows the first cause:

```
   ..PPPPPPKKPPPPP..     P = the ray reaches the part
   ..PPPPPPKPPPPPP..     K = the ray reaches the cutter (3 px at most)
```

**Place hole on face** makes the cutter protrude 0.5 mm above the top face, so from any off-axis view the
part's top surface occludes it: the cutter is a 2-3 pixel target and ordinary clicking selects the part for
both slots. Three causes, in order of weight: the cutter is nearly unclickable; the mode opens as **Union**
while only Difference relabels the slots, so nothing says the *first* pick is the volume that survives; and
`generate_new_volume()` inherits the **source's** type and name and replaces the source, so a reversed
difference leaves a near-invisible negative leftover and reports nothing.

- **A. The slot follows the volume type.** `assign_pick()`: a `NEGATIVE` volume is a cutter by construction
  (Holes, Edge dress, Perforations and Extrude all author their tool that way), so it goes to the **tool**
  slot whichever slot was asked for; a `MODEL_PART` still goes where it was asked (a part may be the tool -
  "cut this wall with that block"). A redirected pick does not advance the selecting state, because it did
  not fill the source slot: the operation is still owed a source. Both entry points (the interactive click
  and `pick_volume_index()` for MCP) go through it, so a mouse and a script behave identically, and the
  reversed difference can no longer be expressed at all.
- **B. The mode follows the picks.** With a cutter in the tool slot the mode becomes **Difference**, unless
  the user has chosen one themselves (`m_operation_chosen`, set by the three tabs / `set_operation()`,
  reset when the gizmo opens and when the object changes). Union was only ever the mode the gizmo happens
  to open in - it is not a decision, and it is meaningless for a cutter.
- **C. The outcome is recorded and reported.** `perform_operation()` fills an `Outcome` (`performed`,
  `no_op`, `triangles_in/out`, `volumes_before/after`, `message`) that MCP `run` and `apply` report.
  `no_op` is the case the kernel produces when a boolean has no resulting fragment: `do_boolean()` keeps
  the part it could not cut, `make_boolean()` hands it back as a valid result, and the gizmo then replaces
  the source with a copy of itself and consumes the tool - "nothing happened and something vanished",
  reported as success. It is detected exactly (same triangle count and volume; the kept part is the
  source's own mesh copied through mcut) and raises the same warning a failure does. The refused and
  no-op paths now report the volume counts unchanged instead of 0, and `apply` no longer reads the source
  volume *after* the operation, which may have been consumed.
- **E. `on_set_state()` no longer shadows its own members.** Upstream writes `bool m_diff_delete_input =
  false;` inside the function - a local copy, so the Delete input checkbox never reset and carried over
  from the previous operation. The intent is now implemented, and the new mode flag and outcome reset with
  it.
- **F. (found while diagnosing, and not part of the reported bug) MCP tools must refresh the gizmo data
  after a hand-written selection.** Setting a `Selection` by hand bypasses the canvas path that calls
  `GLGizmosManager::update_data()` (`GLCanvas3D` does it after a real click; `GizmoObjectManipulation.cpp`
  line 639 after the same kind of hand-written change). Without it a gizmo that is already open keeps
  working against the **previous** object - `SelectionInfo` and the raycaster still point at it, so a pick
  that names a volume by index resolves against the old volume list and the operation lands on the wrong
  object, while every report in those tools reads the canvas selection and names the new one. That is how a
  scripted Mesh boolean on a duplicate came to modify the original: the owner's live object was damaged
  during diagnosis and restored by reloading the saved project (verified: one object, part 29 284 +
  `FacePocket#1` 256). `refresh_gizmo_data()` now runs at **13** sites in `OrcaMCPGizmoTools.cpp`, one per
  hand-written selection.
- **Deliberately not done.** (1) Letting the UI pick a volume no ray reaches (the Object List's selected
  volume as the pick) - the MCP surface can pick by index, the panel cannot, which is why a buried cutter is
  unpickable; that is the follow-up with the most value. (2) Making "Place hole on face" protrude further so
  its cutter is a bigger click target.
- **Verification.** Build clean (`build_win.bat -s --tests`, then an incremental `-s --no-configure` for the
  reporting fix). No unit test: this is gizmo behaviour, and the suites do not cover the panel. Runtime over
  MCP, **on a duplicate clone with `object_id` on every call**:
  - `pick_source` naming the cutter -> the reported mode becomes **difference** and the pick lands in the
    tool slot; `pick_source` naming the part fills the source. (The status report prints the picks under
    its own keys, so the proof is the run below.)
  - one `run {difference, source 0, tool 1}` -> `performed true`, `no_op false`, `triangles_in 29284`,
    `triangles_out 42072`, volumes 2 -> 1, and the clone's only volume is `Cube - difference` with the hole;
    **the owner's object 0 was untouched** (29 284 + 256) - which also proves F.
  - the reversed call `run {source 1, tool 0}` is now **refused** (`applied false`, `performed false`,
    warning "Pick a source volume and a tool volume", nothing consumed) instead of replacing the cutter with
    a 12 796-triangle leftover.
  - **Not exercised**: the `no_op` branch itself. It needs a Difference whose tool misses the source, and
    there is no per-volume move over MCP (`transform_objects` is object-level) and no fixture for it. Its
    logic is an exact-copy comparison; the owner's checklist has it.
- **Branch state**: **merged** into `port/integration` as **`e673b82a9d`** (user-tested 2026-10-17) and
  pushed: `port/MB-1` held two product commits (MB-1, then MB-2), two `port:` docs commits and the
  `tools_schema.py` regeneration, and its merge tree is byte-identical to the branch that was built and
  tested. The owner tested MB-1 and MB-2 together in one build, as the branch model intends.

## 41. MB-2 - the right-click Mesh boolean: not a union, and no longer silent (same branch `port/MB-1`)

The owner asked: "is the right click mesh boolean a union then? is there no way to make it automatically
make negatives into differences and positives into unions?" - and reported that it "doesn't actually do
it" with **no error and no toast**.

- **It is not a union, and it already does what was asked.** `GUI_Factories.cpp` attaches the context-menu
  item to `obj_list()->boolean()`, which calls `Plater::combine_mesh_fff()`. That builds
  `csg::model_to_csgmesh(mo, ..., mpartsPositive | mpartsNegative)`, where each volume maps as
  `vol->is_model_part() ? CSGType::Union : CSGType::Difference` - every model part unioned, every
  **negative** volume subtracted, in one operation - and then runs the same mcut kernel the gizmo uses.
  One click, all volumes, type-aware. The gizmo is the *pairwise* tool; this is the one-shot.
- **Two silent ways for a negative volume to do nothing.** (1) The pre-flight was *stricter than the
  kernel*: `combine_mesh_fff` gated with `csg::check_csgmesh_booleans(range)`, which defaults to the
  **CGAL** predicates (empty / `does_bound_a_volume` / `does_self_intersect`), and then executed with
  **mcut**. A mesh carrying the slivers an earlier mcut boolean leaves behind can be refused there; a
  refusal skips the whole boolean and falls back to "keep only the positive parts", so the cutter vanishes
  and no cut is made. (2) `detail_mcut::perform_csg` applied each Difference with
  `MeshBoolean::mcut::do_boolean(*dst, *src, "A_NOT_B")` and **discarded the outcome**: when mcut finds no
  fragment for a cut, `do_boolean` keeps the part it could not cut, so the accumulated mesh comes out
  exactly as it went in - the run reports success, the object is replaced by the result, the negative
  volume is gone and its cut is missing, with nothing said. That is the same behaviour MB-1 found inside
  the gizmo, now reported there too.
- **What was changed.** `MeshBoolean::mcut::do_boolean` now **returns whether it changed the mesh** (the
  one place that knows; callers that ignore it are unaffected). `csg::DifferenceReport` counts the
  Difference parts asked for and how many changed nothing, including a Difference dropped because the
  accumulated mesh or the part's kernel mesh was missing - equally silent before - and
  `perform_csgmesh_booleans_mcut(range, report)` fills it. `Plater::combine_mesh_fff` passes the report,
  fills a `Plater::ObjectBooleanReport` for its caller, **tells the user** through the same error channel
  the failure path uses when a negative volume was not subtracted, and **logs it**
  (`BOOST_LOG_TRIVIAL(info)`, so it is in `%APPDATA%\OrcaSlicer\log` and in `get_log_tail`). The gate now
  checks with the kernel that runs (`use_mcut = true`: it keeps the part-is-empty test and drops the two
  CGAL geometry predicates), and the fallback path logs its reason too. `ObjectList::boolean()` returns
  that report, and the new MCP tool **`boolean_parts`** runs this exact path (it selects the object in the
  Object list, which is what `boolean()` reads) and reports objects/volumes/triangles before and after
  plus `negative_parts` / `negative_parts_not_subtracted` / `boolean_failed` / `message`.
- **Verification.** Unit: a new case in `tests/libslic3r/test_meshboolean.cpp` builds the part list by
  hand - a 20 mm cube as Union and a 4 mm cube as Difference - and asserts that a cutter 100 mm away is
  **counted** (`offered 1`, `not_applied 1`) with the body's volume unchanged, while a cutter centred on
  the body is applied (`not_applied 0`) and leaves the closed cavity of 7936 mm^3 (20^3 - 4^3).
  `[MeshBoolean]` is 3 cases / 17 assertions; full `ctest` **2415 passed, 0 failed**. Runtime, over MCP on
  a **duplicate** of the owner's object: `boolean_parts` gives `triangles_before 29540` (29 284 + the
  256-triangle pocket) -> `triangles_after 42072`, volumes 2 -> 1, `negative_parts 1`,
  `negative_parts_not_subtracted 0`, `boolean_failed false` - the same 42072 the gizmo's difference
  produces, so **the right-click path cuts the hole correctly on the state the owner left behind**. The
  gate's refusal lines (`check_csgmesh_booleans fails! ...`) appear in none of the recent sessions' logs,
  so the earlier failure was not reproduced on that state: what is now certain is that if it recurs, it
  says why - which is what could not be established before, and why the failure read as "it is a union".
- **Deliberately not done.** *Why* mcut cannot subtract a given cutter (as opposed to reporting it) needs
  a state that reproduces it; the report and the log now make that a one-line answer instead of a guess.
  The trade-off accepted with the gate change: a mesh the CGAL predicates would have refused is now
  handed to mcut, which can throw (caught, fallback + message) or return the source unchanged (now
  counted) rather than being refused up front.
- **Branch state**: same branch as MB-1, product commit + `port:` docs commit, patch `76_MB-2.patch`.
  Merged with MB-1 as `e673b82a9d` (user-tested 2026-10-17) and pushed.

## 42. ME-3 - a screw head's cutter starts outside the face, so a mesh boolean cannot skin the recess (branch `port/ME-3`)

The owner, in the Holes tool: **Bore**, Screw category, a **head** (socket), applied to a **detected**
hole, then **Mesh boolean** - *"it still shows a surface as still being solid but it does make the hole
with a screw head when you slice it"*. Detected hole, **Place hole on face** to be covered as well;
scope: screw heads only.

- **What it is.** A **zero-thickness skin across the whole counterbore**, coplanar with the face. The
  slicer throws a sheet with no volume away, so the print has the head while the mesh and the 3D view say
  the surface is still solid. Reproduced against the kernel the gizmo uses (`mcut::make_boolean(...,
  "A_NOT_B")`), a 20x20x10 plate, a 6 mm socket counterbore 3 mm deep, entrance on the face (z = 10), a
  ray straight down: with the cap **in** the face both the axis and r = 2.4 meet material at **10.0**;
  with the cap **0.34 mm outside** they meet the bore's floor at **4.0** and the head's shoulder at
  **7.0**. `10.0` is the sheet.
- **Why a detected hole is exposed.** `feature_frame()` starts the cutter at the end of the detected
  wall, which the detector puts on real mesh vertices - i.e. **in the surface plane**. A plain re-bore
  only has its cap disc inside the hole's existing void there; a **head is wider than the hole**, so its
  cap ring covers the face around it. Coplanar pair, sheet. The countersink is the same (its cone's wide
  cap is at the entry plane). PP-4 had already cured this for a hole **placed** on a face and for a
  through teardrop; the screw heads were the gap, on **both** paths.
- **What was changed.** `hole_cutter_entry_margin(bore_d)` (PP-4's `max(0.2, 0.1*d)`, world mm) is now
  one helper shared by `face_hole()` and `bore_negative_mesh()`. `its_make_counterbore()` and
  `its_make_countersink()` take an optional `out_margin` (default 0) that puts the entrance cap that much
  further out than `entry` while **everything the shape removes below the entry plane keeps the geometry
  `depth` / `cbore_depth` / `csink_d` describe** - the countersink's cone is extended along its own
  surface (widened by `out / tan(half)`), so it still cuts `csink_d` at the entry plane; without that the
  margin would have narrowed an M3's sink by 0.68 mm. `bore_negative_mesh()` asks for it for Socket,
  Button and Sink heads and for the optional head teardrop prism (a prism does not taper, so only its cap
  moves). No option, no config key, no `.3mf` migration: an applied hole keeps its volume, a hole applied
  as a plain negative volume is unchanged, since the extra cutter is outside the part - the one ceiling,
  shared with PP-4's own placed-path margin, is that the cutter reaches `max(0.2, 0.1*d)` beyond the
  mouth, so a wall of the same object within that distance across the mouth is in range.
- **Verification.** Unit: the two `[MeshBoolean]` cases above (the reproduction, and the clean cut with
  `hole_cutter_entry_margin(3.4)` = 0.34), plus two `[HoleShapes]` cases - the counterbore's cap moves
  out while its bbox far end, head radius and shoulder at `cbore_depth` do not, and a countersink's
  half-width across the entry plane stays 3.15 for `out` = 0, 0.34, 1.5. `[HoleShapes]` 28 cases / 155
  assertions, `[MeshBoolean]` 5 / 23, `[HoleDetector]` 9 / 25, full `libslic3r` **1557 cases / 103 266
  assertions, 0 failed**; the slicer builds clean. Runtime over MCP, on a generated washer fixture (a
  *detected* 3.4 mm hole): the created `HolePocket#0` (2 patches) exports as z in [-4.000, **3.340**]
  with the face at 3.000, and after Mesh boolean (difference, Delete input) rays from above find
  **nothing** at r = 0 (open through), -0.000 at r = 2.4 (the shoulder, 3 mm under the face) and
  3.000 / -3.000 at r = 5 - **no skin**; the same for a pocket **placed** on the face (whose cutter
  carries both its own 0.34 and this one, both in air); and a countersink's mouth is still the standard's
  6.0 mm at the entry plane. All results `open_edges: 0`.
- **Deliberately not taken.** The same exposure on a plain screw bore with **no** head, the nut / magnet
  pockets, a **blind** teardrop and the rim ring (owner's scope: just screw heads - each is the same
  couple of lines). The counterbore is still two overlapping shells merged into one volume; harmless here
  (`do_boolean` splits the tool per component) but a stepped section builder is still the right follow-up
  for the head teardrop. And two **pre-existing** placed-path seat errors were left exactly as they were:
  a placed counterbore's shoulder sits one PP-4 margin (0.34 mm) deeper than `socket_k`, and a placed
  countersink's mouth is 0.68 mm narrower than `csink_d` at the face - both because a head is measured
  from the offset entry instead of from the face, which needs the entry-to-surface distance carried into
  the shape builder. The detected path, the one reported, lands both exactly.
- **Branch state**: `port/ME-3` off `port/integration` `2fa3fb01f3`, product + tests commit then a
  `port:` docs commit, patch `77_ME-3.patch`. **Not merged, not pushed** - awaiting the owner's test.

