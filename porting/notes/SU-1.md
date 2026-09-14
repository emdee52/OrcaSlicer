# SU-1 - PerObject Support

- Source: `NEOTKOCM_RELEASE_2_39.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c3508e5a92`.
- Category: B. Status: **ported** (branch `port/SU-1`), build clean; runtime verification pending.

## Intent
An object's support treats every other object's body **and already-generated support** as an
obstacle, keeping the normal support/object XY distance. Moving a part invalidates the neighbours'
support. By-layer printing only (in by-object mode the neighbour may not exist yet). The
first-layer base/brim expansion is dropped while the mode is on (deliberate adhesion/no-collision
trade-off).

## Implementation (target 2.5)
All inserted code tagged `[ORCAPORT:SU-1]`; the new file pair carries the `[ORCAPORT FILE]` stamp.

- New module `src/libslic3r/OrcaExt/InstanceContact.{hpp,cpp}` (namespace `Slic3r::OrcaExt`):
  - `neighbor_occupancy(object)` - per-object-layer footprint of every OTHER `PrintObject`'s
    instances (body + already-generated support) in this object's local frame. Z mapping by real
    `[bottom_z, print_z)` overlap (adaptive-safe), conservative multi-instance union, 50 mm bbox
    pair pruning. Returns empty when inactive.
  - `cross_object_active(object)` - toggle on AND by-layer print.
  - Re-implemented from the fork's `InstanceContact` module, which also holds the SU-2/SU-3
    contact/floating detector; **only the cross-object occupancy half is ported** (the detector is
    unselected). NeoDebug dropped.
- `src/libslic3r/Layer.hpp` - public read-only `SupportLayer::tree_roof_areas()/tree_roof_1st_layer()/
  tree_floor_areas()` accessors for the occupancy builder.
- `src/libslic3r/PrintConfig.{hpp,cpp}` - `support_cross_object_avoidance` (`coBool`, default
  `false`), label "PerObject Support", under Support. `Preset.cpp` key list; `PrintObject.cpp`
  support-step invalidation; `src/slic3r/GUI/Tab.cpp` checkbox right under *Enable support*.
- Tree support (`TreeSupport3D::TreeModelVolumes`, the live 3D engine):
  - Constructor appends the neighbours as an extra outline group (`support_material_buildplate_only`,
    raft prefix mirrored). `calculateCollision` then dilates it with the standard support/object gap
    and every derived avoidance inherits it.
  - The group is **collision-only**: it contributes no placeable ("rest on") areas.
  - Fixed the last-group finalize index to the actually-last non-empty group (the appended neighbour
    group can change which group is processed last).
- Tree-support preview (`TreeSupportData`): `m_neighbor_occupancy` populated in the ctor and merged
  into `calculate_collision`, so the GUI preview matches.
- Classic/grid (`SupportMaterial`): occupancy built once in `generate()`, then the support polygons
  are trimmed by it (at `gap_xy`) in `trim_support_layers_by_object`.
- Organic first layer (`SupportCommon`): the uncollided first-layer inflation is clamped to 0 when
  the mode is active.
- Pipeline (`Print.cpp`): opted-in objects are excluded from the parallel support batch and generate
  serially in plate order, so each sees the neighbours' finished support; layer dedup
  (`is_print_object_the_same`) is disabled for opted-in objects (identical objects at different plate
  positions would otherwise inherit support that avoids the wrong relative neighbours).
- Invalidation (`PrintApply.cpp`): any plate-geometry change invalidates `posSupportMaterial` for
  every opted-in object; a cohort-coherence pass regenerates the whole opted-in set together when
  any one is pending.

## Uncabling (critical)
`cross_object_active()` reads **only** `support_cross_object_avoidance` - the fork's
`|| neotko_true_objects.value` term (True Objects) is **removed**, so SU-1 does not depend on any
unselected feature. The `Gravity` branches in the fork's `PrintApply`/`Print` are dropped.

## Behavior-neutral at defaults
`support_cross_object_avoidance` defaults `false`, so `neighbor_occupancy` returns empty, the serial
list stays empty, dedup is unchanged, and `PrintApply` invalidates nothing. The stock path is
byte-identical.

## Known gaps
- The fork's `WaveSupport` occupancy call site is not ported (NeoWave/SU-4 is unselected).
- `TreeSupport.cpp` (the legacy `TreeSupportData`/`TreeSupport` path) still has some fork XOBJ
  contact-point/first-layer clamps; the live 2.5 engine is `TreeSupport3D` + `TreeModelVolumes`,
  which is covered. Revisit only if a legacy path is exercised.

## Verification
- Build: `build_win.bat -s -j 8` -> 0 errors (2026-09-14); `InstanceContact.cpp`, `SupportMaterial.cpp`,
  `TreeModelVolumes.cpp`, `TreeSupport.cpp`, `Print.cpp`, `PrintApply.cpp` all compiled.
- Manual (pending):
  - Two close non-assembled objects: with PerObject Support on, supports do not run through or touch
    the neighbour (normal/grid and tree); moving one re-solves the other.
  - Off (default): output identical to stock.
  - Organic: the wide first-layer base does not spill across objects when on.
  - By-object print sequence: feature inert.

## Files
- `src/libslic3r/OrcaExt/InstanceContact.{hpp,cpp}` (new)
- `src/libslic3r/Layer.hpp`, `PrintConfig.{hpp,cpp}`, `Preset.cpp`, `PrintObject.cpp`,
  `Print.cpp`, `PrintApply.cpp`, `src/slic3r/GUI/Tab.cpp`,
  `Support/SupportCommon.cpp`, `Support/SupportMaterial.{hpp,cpp}`,
  `Support/TreeModelVolumes.{hpp,cpp}`, `Support/TreeSupport.{hpp,cpp}`,
  `src/libslic3r/CMakeLists.txt`
- Patch: `porting/patches/09_SU-1.patch`.
