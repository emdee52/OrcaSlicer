# SU-1 - PerObject Support

- Source: `NEOTKOCM_RELEASE_2_39.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c3508e5a92` (2_39).
- Category: B. Target lacks the feature.

## New shared module
- `src/libslic3r/InstanceContact.{hpp,cpp}` - cross-instance contact/occupancy detector.
  - `cross_object_active()` (~246), `neighbor_occupancy()` (~251)
  - occupancy reads a neighbour's generated support via `support_islands/base_areas/tree_roof_*/tree_floor_areas` (~288-298)

## Upstream anchors (fork lines)
- `src/libslic3r/PrintConfig.cpp:5892` - key def; `PrintConfig.hpp:1069` - member (PrintObjectConfig)
- `Support/SupportMaterial.cpp:406` builds occupancy; consumed `:4627` (classic/grid trim)
- `Support/TreeSupport.cpp:3511` build; `:2066,2143,3333,3596` consume
- `Support/TreeModelVolumes.cpp:125` injects neighbours as a collision group
- `Support/SupportCommon.cpp:368`, `Support/TreeSupport.cpp:1398` - first-layer base/brim clamp (the trade-off)
- `Layer.hpp:302-307` - `tree_roof_areas()/tree_roof_1st_layer()/tree_floor_areas()`
- `Print.cpp:2658` - opted-in objects get support generated serially; `:2540` disables layer dedup
- `PrintApply.cpp:1881-1935`; `PrintObject.cpp:1232`; `src/slic3r/GUI/Tab.cpp:7823,8105`; `Preset.cpp:896`

## Keys
- `support_cross_object_avoidance` | `coBool` | `false` | `PrintConfig.cpp:5892` (label "PerObject Support", `comAdvanced`)

## Gates / uncoupling (critical)
- `InstanceContact.cpp:246`:
  `return (object.config().support_cross_object_avoidance.value || object.config().neotko_true_objects.value) && ...`
  **Remove the `|| neotko_true_objects.value` term** so SU-1 does not drag in unselected AS-1/True Objects.
- `TreeSupport.cpp:621` forces Organic -> Hybrid while the option is on; keep.

## Coupling / blockers
- Requires serial support generation (`Print.cpp:2658`) and layer-dedup disable (`:2540`), otherwise support-vs-support avoidance silently fails.
- `NeoDebug` infra needed for the trace lines (stub).
- `WaveSupport` has one occupancy call site but is unselected; droppable.
- No `GravityFloor` dependency at all.

## Verification
- Two close non-assembled objects generate non-colliding supports; moving one re-solves the other; first-layer base expansion absent only when on; tree and normal/grid both avoid.

## Open questions
- 3mf round-trip of the key not traced.
- Duplicate `m_neighbor_occupancy` members across TreeSupport/SupportMaterial/WaveSupport; port only the TreeSupport/SupportMaterial ones.
