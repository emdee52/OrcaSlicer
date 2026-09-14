# AS-2 - Align & Stack gizmo (+ AS-2F face-mate)

- Source: `NEOTKOCM_RELEASE_2_3.md` (gizmo), `2_39` (ghost previews / two-object cap).
- Fork: `OrcaFS-NeotkoCM` @ `cca8426cfe`, `9299cb368a`, `c3508e5a92`.
- Plus: PreFlight3D `GLGizmoAlign` (AGPLv3) for slanted face-mating and feature-center snapping.
- Category: B/D. Status: in progress.

## Decisions
- Port the Neotko base gizmo, then add a face-mate mode (PreFlight algorithm) on top.
- Face-mate moves the **picked instance** only.
- Include extras: depth, rotate around target normal, flip, mirror H/V.
- Feature-center snapping: holes on the flat face **and** negative-volume centers; snap on hover;
  markers only on the active face.
- No assembling/weld: objects stay separate (align-only).

## Why this is portable (not a PreFlight gizmo port)
PreFlight's `GLGizmoAlign` is on Prusa's newer API (`MouseInput`, `ImGuiPureWrap`) that target 2.5
does not have. But its *algorithm* uses only primitives Orca already exposes:
- `Eigen::Quaterniond().setFromTwoVectors(from, to)` - Orca's own `Selection::flattening_rotate`
  already uses it (`Selection.cpp:1441`).
- `Selection::rotate(object_idx, instance_idx, const Transform3d& overwrite_tran)` - apply an
  arbitrary instance transform (`Selection.cpp:1784`).
- `MeshRaycaster::unproject_on_mesh` - world hit point/normal/facet (Neotko already uses it).
- `TransformHelper::world_to_ss` / `world_to_clip` / `clip_to_ndc` / `ndc_to_ss` - all present in
  target `GLGizmoMeasure.hpp` (the fork uses these).
- `indexed_triangle_set` (+ `its_face_edge_ids`/`its_face_neighbors`), `ModelVolume::is_negative_volume()`,
  `Camera::get_projection_matrix/get_view_matrix/get_viewport` - all present.

## Phase 1 - base gizmo (Neotko)
- New files `GLGizmoAlignStack.{hpp,cpp}` (copy from fork), adapted:
  - drop `GizmoNeotkoStyle.hpp` (used twice: `neo_push_window_style`/`neo_pop_window_style`).
  - drop the Libre Mode gate in `on_is_activable` (`neotko_libre_enabled`).
  - no NeoDebug/neotko_ keys.
- Register: `GLGizmosManager.hpp` (enum `AlignStack`), `GLGizmosManager.cpp` (include, icon case,
  ctor emplace), `src/slic3r/CMakeLists.txt`.
- Icons: `resources/images/toolbar_align_stack{,_dark}.svg`.
- Optional `GLCanvas3D` tweak: exempt AlignStack from `reset_all_states()` on empty selection.
- Verify: builds, gizmo appears, aligns/stacks #2 against #1.

## Phase 2 - face-mate mode (AS-2F core)
- Pick source face on #2 and target face on #1 (raycast both; reuse hover/highlight).
- `align_rot = setFromTwoVectors(src_normal, -tgt_normal)`; `new_linear = align_rot * orig_linear`;
  translate so the source hit lands on the target hit (+ depth); apply via
  `Selection::rotate(obj, inst, overwrite)` after `take_snapshot` + `changed_object`.

## Phase 3 - extras
- Depth along target normal, rotate around normal, flip to other side, mirror H/V: transform
  compositions on the Phase 2 result.

## Phase 4 - feature-center snapping
- Portable version of PreFlight `detect_feature_centers`: negative-volume centers + coplanar-face
  boundary loops (holes). Cache per picked face; project to screen; hover-snap within N pixels.

## Verification
- Compile/link each phase.
- Base: two objects align/stack as previewed; undo restores.
- Face mode: pick two faces on slanted geometry, confirm the source face mates flush to the target
  normal; extras adjust the result.
- Snap: hovering a hole center snaps the placement to it.
