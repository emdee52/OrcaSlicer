# AS-1 - Floating object trigger (no forced bed snap; empty first layer = warning)

- Source: `NEOTKOCM_RELEASE_2_3.md` (floating objects), re-keyed in `2_39`.
- Fork: `OrcaFS-NeotkoCM` @ `cca8426cfe` (Libre Mode), `c3508e5a92` (Gravity axis).
- Category: B/D. Only this sub-feature of Libre Mode is selected.

## Scope
- In: objects placeable at any Z with no forced bed snap; an empty first layer becomes a warning instead of an error, using SU-3's accurate detection.
- Out: assembled-boolean, per-volume XY compensation, copy/paste process settings, and the rest of Libre Mode.

## Upstream anchors (fork lines)
- `src/libslic3r/GCode.cpp:1837-1846` - downgrade:
  `if (object.config().neotko_true_objects.value) { active_step_add_warning(NON_CRITICAL, ...); } else throw SlicingError(...);`
- `src/slic3r/GUI/GUI_App.cpp:7625` - `gravity_allow_free_z()` reads app_config `neotko_true_objects` (`GUI_App.hpp:980`)
- `ensure_on_bed()` skip sites: `GLCanvas3D.cpp:5684,5941,6035,6140`; `Selection.cpp:1600`; `SurfaceDrag.cpp:383`; `GUI_ObjectList.cpp:2472,3152,3311,4246,5774,6373`; `GLGizmoSimplify.cpp:551`; `Jobs/EmbossJob.cpp:375,1142`; `Plater.cpp` (many)
- `MainFrame.cpp:1662-1707` (Libre Mode button forces true_objects); `AppConfig.cpp:177-178` (migration)
- `PrintObject.cpp:1176-1179` (invalidation keys)

## Keys (fork)
- app_config `neotko_true_objects` (bool, false), `neotko_libre_mode`, `neotko_libre_enabled`
- PrintObjectConfig `neotko_true_objects`, `gravity_contact_gap_ratio` (0.5)

## DECISION (recommended)
- Do **not** reuse `neotko_true_objects`, since True Objects (SU-2) is not ported. Introduce one dedicated app_config key for "allow placing objects off the bed / free Z" (e.g. `orca_ext_free_z`) and one slice-side mirror, then wire the `gravity_allow_free_z()` sites to it.
- Remove the Libre Mode master and the AppConfig migration.

## Coupling / blockers
- Depends on SU-3 `InstanceContact::analyze_object` for the warning path.
- "No forced bed snap" is app_config + many `ensure_on_bed()` call sites; enumerate all before starting.

## Verification
- Object placed above the bed does not error; stacked object does not warn; genuinely floating island warns once; no snap-to-bed while dragging unless AS-3Snap-to-bed is on.

## Open questions
- Whether to keep a single slice-side mirror key or read the app pref directly.
- Enumerate every `ensure_on_bed()` site in target 2.5 (line numbers will differ).
