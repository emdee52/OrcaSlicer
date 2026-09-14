# AS-1 - Free-Z placement (align without assembling)

- Source: `NEOTKOCM_RELEASE_2_3.md` (floating objects), re-keyed in `2_39`.
- Fork: `OrcaFS-NeotkoCM` @ `cca8426cfe` (Libre Mode), `c3508e5a92` (Gravity axis).
- Category: B. **Placement only** (decision).

## Scope (decided)
The user aligns objects floating, then **assembles before slicing**. Therefore:
- Port ONLY the placement half: objects can be moved off the bed in Z and stay there.
- Do NOT port the `GCode.cpp` empty-first-layer "warning instead of error" downgrade.
- Do NOT port SU-3 measured floating detection (dropped).
- Do NOT port anything else from Libre Mode.

## Implementation shape
- One app preference `orca_ext_free_z` (default `false`; off = stock bed snapping).
- Many GUI call sites currently force `ensure_on_bed()`. In the fork a helper
  (`gravity_allow_free_z()`) short-circuits them. We will add an equivalent predicate and
  make the same call sites consult it. Expected sites (target 2.5 - confirm line numbers):
  `GLCanvas3D.cpp`, `Selection.cpp`, `SurfaceDrag.cpp`, `GUI_ObjectList.cpp`,
  `Gizmos/GLGizmoSimplify.cpp`, `Jobs/EmbossJob.cpp`, `Plater.cpp`.
- No `PrintConfig` key, no slicing change, no `InstanceContact`.

## Fork reference (for behaviour only)
- `src/slic3r/GUI/GUI_App.cpp:7625` `gravity_allow_free_z()` reads app_config.
- `MainFrame.cpp:1662-1707` merges Libre Mode + True Objects; we replace with our single key.
- `AppConfig.cpp:177-178` Libre Mode -> True Objects migration; not ported.

## Verification
- Preference off: placed objects snap to the bed exactly as stock.
- Preference on: an object stays where it is placed; AS-2 Align & Stack and AS-3 Snap & Drag
  can leave objects floating.
- No change to sliced output (GUI/placement only).
