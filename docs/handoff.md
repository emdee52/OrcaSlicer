# Handoff — mesh hole features (ME-1, ME-2)

Session summary so the next session can pick up: what was built, where it lives, and the failure
modes already understood so they are not rediscovered. Work is mesh-only (no CAD reconstruction):
Prepare-tab gizmos that add positive/negative volumes and paint-free edits to a loaded STL.

Living tracker with the full idea backlog lives in `docs/superpowers/ME-roadmap.md` (gitignored).

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

- **Branch**: `port/CUT-2`, stacked on `port/CUT-1` (CUT-1 is **not merged yet** — merge CUT-1
  into `port/integration` first, then CUT-2, both `--no-ff`).
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
