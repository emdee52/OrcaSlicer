# AS-3 - Snap & Drag

- Source: `NEOTKOCM_RELEASE_2_39.md`, `2_40.md`, `2_43.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c3508e5a92` (new files), `e2cfcff6d9` (Snap to bed), `79a3cae5c6` (panel + group).
- Category: B (GUI-local). Status: **ported** (branch `port/AS-3`), full pass; build clean.

## Scope
- Floor-resolution module: footprint-overlap candidates, real-mesh raycast top, highest wins, bed as a
  last-place candidate.
- Three app-level toggles in the magnet **options panel** (opened from the plate column): `Snap & Drag`,
  `Snap to bed`, `Move selection as one block`.
- Live-drag resolve with hover lift + the landing overlay, and the matching `do_move` commit.
- The magnet icon in the plate column, with SVG icons.
- The three toggles live **only** in the panel, matching the fork: they are no longer duplicated in
  Preferences (the panel is the one selection-independent home for them).

## Implementation (target 2.5)
All inserted code tagged `[ORCAPORT:AS-3]`; new files carry the `[ORCAPORT FILE]` stamp.

- New module `src/slic3r/GUI/OrcaExt/GravitySnap.{hpp,cpp}` (namespace `Slic3r::OrcaExt::Gui::GravitySnap`):
  - `floor_z_for_instance` - footprint-overlap candidate test (2D convex hulls), real-mesh raycast
    top per candidate via `AABBMesh::query_ray_hit`, highest surface wins, bed as last-place
    candidate when `bed_is_floor()`.
  - `support_in_group` / `instance_footprint` for multi-object stack grouping.
  - `panel_open()` (transient UI state) and `plate_icon_available()` (always true - no Libre Mode).
  - Adaptations from the fork: dropped `NeoDebug::GRAVITY`; `enabled()` reads `OrcaExt::free_z()`
    (AS-1) instead of `gravity_allow_free_z()`; keys renamed `neotko_snap_drag*` -> `orca_ext_snap_drag*`;
    file in the `OrcaExt/` folder per `PORTING_GUIDELINES.md`.
- `src/slic3r/GUI/GLCanvas3D.{hpp,cpp}`:
  - `SnapDragIndicator` overlay: shadow rings, footprint fill/outline, ghost box, recognised contact
    zone, raycast sample points, hover-gap beam prism; colour encodes object floor (cyan) vs bed (amber).
  - `_render_snapdrag_indicator()` (called after `_render_sequential_clearance`) and
    `_render_snapdrag_panel()` (called from `_render_overlays` on the Prepare canvas only).
  - `snapdrag_group_roots()` + tuning constants (engage 0.20, release 0.08, stack gap 1.0, hover lift
    3-10 mm).
  - `on_mouse` drag path: resolve + apply the resting Z to the dragged GLVolumes, hover lift, and the
    indicator; `_snapdrag_rigid_frame()` for "move selection as one block".
  - `do_move`: resolve the floor per dragged instance and commit the shift, matching the live path
    exactly (by-stacks / rigid-group / single-object with the lowest target per object).
  - `m_snapdrag_engaged` hysteresis cleared at drag start.
- `src/slic3r/GUI/PartPlate.{hpp,cpp}`: `m_snapdrag_icon` at layout slot 7 / picking sub-id
  `SNAP_DRAG_HOVER_ID = 9`; `GRABBER_COUNT` 9 -> 10 (36*10=360 < 1000 budget); texture load/reset;
  `bed_icon_count` 6 -> 7.
- `src/slic3r/GUI/Plater.cpp`: the magnet click toggles `panel_open()` (no snapshot - nothing about the
  model changes).
- `resources/images/plate_snapdrag{,_dark,_hover,_hover_dark}.svg` copied from the fork (a magnet
  glyph; no branding).
- `src/slic3r/CMakeLists.txt`: register both new files.

## Behavior rules preserved
- `floor_z_for_instance` returning `nullopt` means "leave it floating", never "drop to Z=0";
  `bed_is_floor()` on makes the bed a real `FloorHit` candidate instead.
- Footprint overlap (not a raycast under the cursor) decides the floor; a candidate with no real
  surface under the overlap is rejected (hollow box interior).
- A dragged group never rests on itself (the drag members are excluded as candidates).
- Commit matches the live drag (by-stacks or rigid-group) so the selection does not jump on mouse-up.
- Hysteresis: engage at 0.20, keep an engaged **object** floor at 0.08. The bed never counts as
  engaged (it always answers).
- The hover lift needs no explicit undo: `Selection::translate` rebuilds each offset absolutely from
  the drag-start cache, so every frame arrives with the lift already gone; `do_move` recomputes the
  floor and seats the object exactly on it.

## Deviations / gaps
- Gated to FFF (`current_printer_technology() == ptFFF`); no SLA snap.
- Respects upstream per-instance `auto_drop` on the fallback bed-snap path; the Snap & Drag arms
  bypass it for the instances actually being dragged (explicit user action).
- `enabled()` requires AS-1 free-Z to be on (without it `do_move` force-drops to the bed anyway);
  the panel states this and dims the controls when it is off.
- The magnet icon is always shown (no Libre Mode gate). Gating it on a mid-session toggle would leave
  the plate's picking raycasters stale, so it is registered unconditionally.

## Keys (app_config only, never PrintConfig)
- `orca_ext_snap_drag` | bool | false | `GravitySnap.cpp::enabled`
- `orca_ext_snap_drag_bed` | bool | true (absent => true) | `GravitySnap.cpp::bed_is_floor`
- `orca_ext_snap_drag_group` | bool | false | `GravitySnap.cpp::move_as_group`
(fork mapping: `neotko_snap_drag`, `neotko_snap_drag_bed`, `neotko_snap_drag_group`)

## Verification
- Build: `build_win.bat -s --no-configure -j 8` -> 0 errors, `OrcaSlicer.dll` linked (2026-09-14).
- Off by default: keys absent/false -> `enabled()` false -> `do_move` runs the stock `auto_drop` /
  bed-snap path unchanged; no slicing is touched by this feature (GUI placement only).
- Reduced pass (GravitySnap + Preferences toggles + live-drag + commit) was confirmed working by the
  user before the panel/overlay were added; the full pass keeps the same engine.
- Manual protocol (pending):
  - Enable "Allow free Z placement" (Preferences) + "Snap & Drag" (magnet panel); drag an object over
    a shorter one -> it rests on the top surface; over nothing with "Snap to bed" on -> lands at Z=0,
    off -> stays floating.
  - Grazing the edge of a lower object (< 20% footprint overlap) does not engage; once resting,
    slides off only below 8%.
  - Hollow box: drag over the rim lands on the rim; over the open interior it does not treat the
    interior as a floor.
  - Select two objects and drag: unrelated objects fall independently; two stacked ones travel
    together. Turn on "Move selection as one block" -> the whole selection keeps its relative shape.
  - Mouse-up does not change the resting Z (live drag and commit agree).
  - The magnet icon opens/closes the panel; the panel's controls dim when free-Z is off.

## Files
- `src/slic3r/GUI/OrcaExt/GravitySnap.{hpp,cpp}` (new)
- `src/slic3r/GUI/GLCanvas3D.{hpp,cpp}`, `src/slic3r/GUI/PartPlate.{hpp,cpp}`,
  `src/slic3r/GUI/Plater.cpp`, `src/slic3r/CMakeLists.txt`,
  `resources/images/plate_snapdrag{,_dark,_hover,_hover_dark}.svg`
- Patch: `porting/patches/08_AS-3.patch`.
