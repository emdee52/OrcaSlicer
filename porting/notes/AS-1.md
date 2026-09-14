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
  `orca_ext_free_z`, default false), writes the global.
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

## Verification
- Compile/link: 0 errors, binary produced.
- Off: preference false -> every guarded site behaves exactly as stock.
- On: drag an object up in Z and it stays; place a second object against it for alignment.
- No slicing change (GUI/placement only).
