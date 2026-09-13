# SU-5 - Support Zones (aimed pillars)

- Source: `NEOTKOCM_RELEASE_2_44.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c1d9cc2890` (2_44).
- Category: B/D, very large.

## New files
- `src/libslic3r/Feature/SupportZones/SupportZoneProbe.cpp/.hpp`
- `src/slic3r/GUI/Gizmos/GLGizmoSupportZones.cpp/.hpp` (366 KB)

## Upstream anchors (fork lines)
- `src/libslic3r/PrintObjectSlice.cpp:5834` `slice_support_enforcers_per_zone()`
- `Support/SupportMaterial.cpp` - zone families (~2990), guided detection (~3108), corridor descent (~3333-3482), enforcer seeding (~1401-1495), constants (~2803)
- `Print.hpp:254-288` `SupportZoneSlices`/`SupportFamilyAreas`
- `Model.cpp:3770` config hash; `PrintApply.cpp:1706,1751`; `PrintObject.cpp:1037,1263`
- `Preset.cpp:976`; `GCode.cpp:6430`; `GCode/ToolOrdering.cpp:1097`; `Layer.hpp:283`
- `src/slic3r/GUI/GLCanvas3D.cpp:12389,12593`; `GLGizmosManager.cpp:201,261`

## Keys (per-volume, in `ModelVolume::config`)
- `neotko_support_zone_gesture` | coString (JSON) | "" | `PrintConfig.cpp:7201` (hidden `comDevelop`)
- `neotko_zone_lean_deg` | coFloat 0-89 | 0 | `:7259`
- `neotko_zone_roof_only` | coBool | false | `:7232`
- `neotko_zone_solid` | coBool | false | `:7254`
- `neotko_zone_land_only` | coBool | false | `:7249`
- Per-zone soluble roof reuses `support_filament`/`support_interface_filament`.

## Gates to remove
- `src/slic3r/GUI/Gizmos/GLGizmoSupportZones.cpp:95` - `... !app_config->get_bool("neotko_libre_enabled")`.
- Registration comment `GLGizmosManager.hpp:98`.

## Coupling / blockers
- Full subsystem: zone slicing + family/area + per-volume config hashing + family->tool routing + GLCanvas3D probe/render.
- Uses `NeoDebug::SUPPORTZONES`.
- Reopenable zones stored as a per-volume JSON gesture blob; version migration (v1..v4) to review.
- Zone merging is in the engine, not the gizmo.

## Verification
- Leaned pillar and torus-gap prints (the fork's own tests); zone merge; reopen/edit/undo; no per-layer purge for a roof that exists on a few layers.

## Open questions
- Alias mismatch: gizmo gates on `neotko_libre_enabled` while the UI elsewhere uses `neotko_libre_mode`; reconcile.
- Confirm the gesture JSON schema/versioning before committing to the data model.
