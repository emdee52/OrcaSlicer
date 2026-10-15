# AS-1 - Free-Z placement (align without assembling)

- Source: `NEOTKOCM_RELEASE_2_3.md` (floating objects), re-keyed in `2_39`.
- Fork: `OrcaFS-NeotkoCM` @ `cca8426cfe`, `c3508e5a92`.
- Category: B. Status: **ported** (branch `port/AS-1`), build clean.

## Scope (decided)
Placement only. The user aligns objects floating, then assembles before slicing. So:
- Port ONLY "objects stay off the bed" (no forced bed snap).
- Not ported: the `GCode.cpp` empty-first-layer warning downgrade, SU-3 detection, True Objects,
  or anything else from Libre Mode.

## Implementation (target 2.5)
All inserted code tagged `[ORCAPORT:AS-1]`.
- New app-level global: `src/libslic3r/OrcaExt/FreeZ.{hpp,cpp}` -
  `OrcaExt::set_free_z(bool)` / `OrcaExt::free_z()`.
- `src/slic3r/GUI/Preferences.cpp` - checkbox "Allow free Z placement" (app key
  `orca_ext_free_z`, default false), writes the global. **Moved (toolbar fix):** the checkbox is
  gone; the plate-column gravity button is now the one home for the key. See "Toolbar button" below.
- `src/slic3r/GUI/GUI_App.cpp` - mirrors `app_config` into the global at startup.
- Guarded snap sites (skip the snap when free-Z is on):
  - `GLCanvas3D.cpp` - the four "Fixes sinking/flying instances (snaps object to buildplate)"
    post-move blocks (move/drag/rotate).
  - `Selection.cpp` - `Selection::ensure_on_bed()` returns early.
  - `SurfaceDrag.cpp` - face-drag snap.

## Notes / limits
- Only the move/transform snap paths are guarded. Sites that intentionally place a *new*
  object on the bed (import, paste, emboss, orient, object-list add) still snap; that is
  desirable and matches the fork's selection of guarded sites.
- If a specific action still snaps a floated object back, add the same
  `if (!OrcaExt::free_z())` guard at that call site.

## Toolbar button (fix 1, branch `port/AS-1-fix1`)

The key's only UI is now a toggle button in the plate column, between the "move plate to the front"
up-arrow and the Snap & Drag magnet. It still writes the same app key; an existing ini value keeps
working, and the `GUI_App.cpp` startup mirror and the MCP key are untouched.

- Layout: `PartPlate.cpp::update_bed_shape()` -> `calc_vertex_for_icons(dual_bbl ? 7 : 6,
  m_freez_icon)` (slot 6 single-extruder, 7 dual), `m_snapdrag_icon` slot 7 / 8. The single-extruder
  column had a blank slot 6 there (reserved for the dual-only filament map); the button fills it, so
  the column is gap-free on both printer types. `calc_vertex_for_icons`' circular-bed centering
  divisor 7 -> 8 (a circular bed is never dual-extruder, so 8 is the full count).
- Picking: `FREE_Z_HOVER_ID = 10`, `GRABBER_COUNT` 10 -> 11 (36 * 11 = 396 <
  `SceneRaycaster::EIdBase::Volume` 1000, which `SceneRaycaster.cpp` static_asserts).
- Icons: `resources/images/plate_freez{_dark,_hover,_hover_dark}.svg` (off: part + motion lines +
  gravity arrow) and `plate_freez_on{_dark,_hover,_hover_dark}.svg` (on: part floating above a bed
  line). Tile colours follow `plate_locked`/`plate_unlocked`: idle `#e9e9e9`/`#54545a`, hover glyph
  `#009688`, on tile `#009688` (hover `#26a69a`).
- Click: `Plater.cpp::select_plate_by_hover_id` toggles `orca_ext_free_z` (`set_bool` + `save` +
  `OrcaExt::set_free_z`), then dirties the canvas and requests a frame, the same as the magnet. No
  snapshot: nothing about the model changes. The redraw also un-dims the AS-3 panel, which reads
  `GravitySnap::enabled()` per frame.
- `src/slic3r/GUI/Preferences.cpp` - the AS-1 checkbox and its `FreeZ.hpp` include are removed.

## Verification
- Compile/link: 0 errors, binary produced.
- Off: preference false -> every guarded site behaves exactly as stock.
- On: drag an object up in Z and it stays; place a second object against it for alignment.
- No slicing change (GUI/placement only).
- Toolbar fix, manually unchecked at the time of writing:
  - Default (no ini entry): gray gravity icon; drag/drop snaps exactly as stock.
  - Click the button: icon switches to the floating glyph, `orca_ext_free_z` appears in the ini, and
    the AS-3 panel's controls stop being dimmed.
  - On: dragging an object up leaves it there; the magnet panel can then seat it on another object.
  - Both printer types: no blank square in the column, and on a dual-extruder (H2D) profile the
    button sits below the up-arrow, not on top of the filament-map icon.
