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

## Port progress

### Status: in-progress (branch `port/SU-5`). Foundation landed; engine + gizmo remain.

Feasibility verified: target's `SupportMaterial.cpp` differs from the fork's base (`snapmaker` tag
`v2.3.5`) by only 18 hunks, so the engine ports cleanly.

**Done (build clean):**
- New `src/libslic3r/Feature/SupportZones/SupportZoneProbe.{hpp,cpp}` (probe/render helper; NeoDebug
  dropped; target's `igl::Hit<float>` API adapted). Registered in `src/libslic3r/CMakeLists.txt`.
- Per-volume keys (neutral names): `support_zone_gesture`, `support_zone_lean_deg`,
  `support_zone_roof_only`, `support_zone_solid`, `support_zone_land_only` in `PrintRegionConfig`
  (`PrintConfig.{hpp,cpp}`, hidden `comDevelop`) + `Preset.cpp` key list.
- Data model in `Print.hpp`: `SupportZoneSlices` (priority, volume, lean, solid, land_only, slices,
  roof_layer), `PrintObject::SupportFamily`/`SupportFamilyAreas`, `support_family_areas()` /
  `support_family_areas_at()` / `set_support_family_areas()`, `mutable m_support_family_areas`.
- `PrintObjectSlice.cpp`: `slice_support_enforcers_per_zone()` (one stream per enforcer volume, no
  union) and `PrintObject::support_family_areas_at()` (roof-layer computation included).
- `Layer.hpp`: `SupportLayer::support_fills_family`.
- The copied `GLGizmoSupportZones.{hpp,cpp}` are staged in the tree but **not** registered yet
  (Phase 2 UI); they are not in CMake, so they do not affect the build.

**Phase 1b done (build clean):**
- `Model.{hpp,cpp}`: `model_support_volume_config_changed()` (a zone recipe edit now invalidates
  `posSupportMaterial`), wired in `PrintApply.cpp`.
- `Support/SupportMaterial.cpp`: the ~1,400-line zone engine (enforcer seeding, `SupportAnnotations`,
  corridor descent, roof-layer, zone merge, family build) ported from the fork's `v2.3.5` diff,
  keys renamed to `support_zone_*`, NeoDebug/Gravity/wave-bugfix stripped.
- `Support/SupportCommon.{hpp,cpp}`: `split_support_fills_by_family()` + the `generate_support_toolpaths`
  `object_for_families` argument and split call; `PrintObject::push_support_zone_style_warning()`.
- Per-zone filament routing: `ToolOrdering::collect_extruders` schedules each family's body/interface
  filament (roles actually present, 1-based, clamped to physical extruders); `GCode::process_layer`
  routes each family to its own `ObjectByExtruder` cube; `GCode::extrude_support` filters the
  family-tagged entities. All gated on non-empty `support_fills_family`.
- `PrintObject::invalidate_state_by_config_options`: the descriptive `support_zone_gesture` key is
  skipped (no reload), while `support_zone_lean_deg/roof_only/solid/land_only` invalidate
  `posSupportMaterial`.

**Phase 2 done (build clean):**
- `GLGizmoSupportZones.{hpp,cpp}` (the full ~6.7k-line aimed-pillar / block-tree author) registered in
  `GLGizmosManager` (`EType::SupportZones`, icon, ctor) with two toolbar SVGs. LibreMode gate removed;
  `NeoDebug` calls inert via a local no-op shim.
- Shared look `GizmoStyle.hpp`, ported from the fork's `GizmoNeotkoStyle.hpp` with neutral names
  (`NeoCol`->`GizmoCol`, `neo_`->`gizmo_`).
- `GLCanvas3D`: zone highlight / coverage overlays (`_update_support_zones` / `_render_support_zones`)
  and the sterile-zone warning (`EWarning::SterileSupportZone`). `GLVolumeCollection` gains the
  transparent-depth-write ghost toggle used by the gizmo.

**Status: ported** (build clean; runtime verification pending).


