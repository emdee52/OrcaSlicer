# Handoff — mesh hole features (ME-1, ME-2)

Session summary so the next session can pick up: what was built, where it lives, and the failure
modes already understood so they are not rediscovered. Work is mesh-only (no CAD reconstruction):
Prepare-tab gizmos that add positive/negative volumes and paint-free edits to a loaded STL.

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

| Branch | State | Contents |
|---|---|---|
| `port/ME-1` | **merged** into `port/integration` (`eac3472582`) | detection, teardrop, gizmo, MCP |
| `port/ME-2` | pushed, **unmerged** | shared standards, unified Holes tool, all categories |

ME-1 commits: `3166fa8d39` (detector + shaper + tests), `72eccfdba9` (teardrop gizmo),
`9b68a49315` (MCP `find_holes`, nested-hole fix, partial-bridge pass later removed),
`cfccddb6ea` (angle/colors, superseded), `efa59827b2` (teardrop-only + preview depth-mask fix).

ME-2 commits (oldest first): `b309f93851`, `6a920ee4d1`, `afd0aa5b9e`, `db3dddc39b`, `a2c5ba9ca0`,
`f7439b423f`, `5993585a3f`, `1e9809e58a`, `9e3a47cdf3`, `4a404d4c6d`, `2cfa6412e3`, `c3faed5f2a`,
`7e5f304b78`, `0a03d1d23a`, `3085e98a73`.

Merge ME-2 into `port/integration` (`--no-ff`) once click-verified.

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
- `src/libslic3r/HoleStandards.{hpp,cpp}` — shared screw/insert/magnet/nut table + `HoleFit`
  (Tight/Slip) and `hole_fit_diameter_delta`, `screw_nominal_diameter`. Always compiled (works with
  `SLIC3R_CAD=OFF`). `CadDocument::add_hole_standard` consumes it (socket head is the CAD counterbore).

### GUI
- `src/slic3r/GUI/Gizmos/GLGizmoHoles.{hpp,cpp}` — "Holes" tool, `EType::Holes`, shortcut `Ctrl+J`.
  Operation buttons (Teardrop / Bore-pocket); Bore reveals category image buttons
  (Screw/Nut/Magnet/Insert/Custom) then the item dropdown and parameters.
  - Teardrop: horizontal holes only, apex 45-60°.
  - Bore: screw bore with optional socket/button/countersink head, heat-set insert, magnet, nut,
    or custom diameter. Tight/Slip fit for inserts/magnets; Free/Tap for screws; editable
    diameter + tolerance; read-only True diameter; editable pocket Depth; Height buttons for
    inserts; Head-fit (Flush / -0.08 / -0.16). Shrinking adds a positive tube.
  - Preview: candidate ghosts blue, applied volumes red, hover green.
  - Icons: `resources/images/hole_cat_{screw,nut,magnet,insert,custom}.svg`.
- MCP: `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` — `find_holes` and `holes_gizmo`
  (`open | status | set_operation | set_category | set_angle | set_standard | set_head |
  set_screw_fit | set_fit | set_diameter | set_tolerance | set_head_fit | set_through | set_depth |
  set_flip | toggle | apply_all | clear_all | refresh | close`). Registered from
  `OrcaMCPServer::register_builtin_tools`.

### Tests
- `tests/libslic3r/test_holedetector.cpp`, `test_holeshapes.cpp`, `test_holestandards.cpp`.

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

---

## 5. Key code locations

- Detection / shapes: `src/libslic3r/HoleDetector.cpp`, `HoleShapes.cpp`.
- Shared data: `src/libslic3r/HoleStandards.cpp` (table + fit), `src/libslic3r/CAD/CadDocument.cpp`
  (`add_hole_standard` consumes it).
- Gizmo: `src/slic3r/GUI/Gizmos/GLGizmoHoles.cpp` (detect, feature_frame, bore_negative_mesh,
  bore_tube_mesh, rebuild_previews, on_render_input_window). Registration in
  `GLGizmosManager.{hpp,cpp}` (`EType::Holes`) and `src/slic3r/CMakeLists.txt`.
- MCP: `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp`.
- Icons: `resources/images/hole_cat_*.svg`; magnet reuses the `plate_snapdrag.svg` horseshoe path.

---

## 6. Open items / likely next

- **Click-verify ME-2** (categories, heads, head fit, insert heights/nuts), then merge into
  `port/integration`.
- Reliability of detection on a wider corpus; `HoleDetectorParams` may need tuning
  (`min_facets`, `radial_tolerance`, `max_angular_gap_deg`).
- **Insert fit**: pocket OD uses the max (knurl) diameter; Tight/Slip are flat deltas of 0.05 mm and
  0.16 mm on the pocket diameter.
- Next feature candidates (see the roadmap): cut-tool shape cuts (independent), bosses/ribs, local
  tolerance adjuster, mesh rim chamfer.
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
  SNAP-6 were merged (`2b323c1aca`), unmerged until the user has tested it (see section 0).


