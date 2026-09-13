# AS-2 - Align & Stack gizmo

- Source: `NEOTKOCM_RELEASE_2_3.md` (new gizmo), `2_39` (ghost previews, two-object cap).
- Fork: `OrcaFS-NeotkoCM` @ `cca8426cfe` (origin), `9299cb368a` (current base), `c3508e5a92` (2.39 rework).
- Category: B. Pure transform math; good early candidate.

## New files
- `src/slic3r/GUI/Gizmos/GLGizmoAlignStack.cpp` (53.7 KB), `.hpp`
- `resources/images/toolbar_align_stack.svg`, `toolbar_align_stack_dark.svg`

## Upstream anchors (fork lines)
- `GLGizmosManager.hpp:94` (`AlignStack` enum), `.cpp:26,189,249` (include/icon/ctor)
- `GLCanvas3D.cpp:5477-5482` - exempt AlignStack from `reset_all_states()` on empty selection
- `src/slic3r/CMakeLists.txt:146-147`

## Keys
- none. Reads one app pref: `neotko_libre_enabled` (`GLGizmoAlignStack.cpp:265`).
- Params are transient members (`m_epsilon_mm=0.01`, `m_place_a_on_bed=true`, `m_flush_mode=false`).

## Gate to remove
- `GLGizmoAlignStack.cpp:259-266` `on_is_activable()` returns `app_config->get_bool("neotko_libre_enabled")`. Remove the gate; keep the gizmo always available.

## Operations and functions
- Place against: `apply_touch` (~794) -> `compute_place_delta(...,flush=false)` (~768)
- Align flush: `apply_flush` (~813) -> `compute_place_delta(...,flush=true)` (~774)
- Center: `apply_center` (~826); Drop to bed: `apply_all_on_bed` (~839)
- Place on picked face: `apply_place_on_picked_face` (~852); face picking `ensure_face_raycaster_for_A` (~565), `update_hover_face` (~674), `build_face_model` (~615)
- Ghost preview: `render_zone_and_ghosts` (~925); hit-test `on_mouse` (~481)
- Order (#1 anchor/#2 mover): `seed_order_from_selection` (~296), `toggle_object_order` (~313); panel `on_render_input_window` (~1082)

## Coupling / blockers
- No Gravity/InstanceContact dependency (verified). Does not touch PrintConfig.
- Depends on GLCanvas3D wire-box/overlay rendering added alongside it; verify those helpers exist or inline them.

## Verification
- Every face/centering op produces the previewed transform; ghost preview equals click result; undo restores; selecting a third object swaps #2.

## Open questions
- Gizmo uses `Selection::translate` and world AABBs; confirm the same APIs in 2.5.
