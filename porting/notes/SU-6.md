# SU-6 - Support Zones (block trees)

- Source: `NEOTKOCM_RELEASE_2_45.md`.
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741` (stump engine), `9862461f75` (Windows fix).
- Category: B/D, very large; extends SU-5.

## New files
- none (extends SU-5's `GLGizmoSupportZones` and `SupportZoneProbe`).

## Upstream anchors (fork lines)
- `src/slic3r/GUI/Gizmos/GLGizmoSupportZones.cpp`
  - `build_block_tree_mesh` (~2445), `build_pillar_mesh` (~2570), `all_stumps` (~2389), `block_tree_head_z` (~2401), `block_tree_gap_mm` (~2421), "3 . Stumps" panel (~6155-6177)
- `Support/SupportMaterial.cpp`
  - `zone_stumps`/`zone_guided`/`zone_stump_top` (~3088-3137), stump-guided descent "GUIADO POR TOCON" (~3333-3482), unreachable-root warning (~3496-3520)

## Keys
- none. The mode is inferred from geometry: `SupportMaterial.cpp:3094-3098` sets `zone_guided=1` only when a gap exists between the bottom band and the top.
- Zone JSON v4 adds `stumps[]` and `stump_size_mm` (default 8, fixed `STUMP_HEIGHT_MM=2.0`).

## The SU-5 / SU-6 branch
- Shared gizmo `GLGizmoSupportZones`; the footprint selector (Patch/Round/Square/Brush) is the user chooser.
- `GLGizmoSupportZones.hpp:650` - `block_tree_mode() { return m_foot_shape == FootShape::Brush; }`
- `build_pillar_mesh()` first line: `if (block_tree_mode()) return build_block_tree_mesh(out_world);`
- Engine branch: SU-5 keeps `zone_guided=0` (contiguous loft, normal corridor); SU-6 uses the stump-guided descent.

## Gates to remove
- Same single gizmo gate as SU-5 (`GLGizmoSupportZones.cpp:95`).

## Coupling / blockers
- Requires the entire SU-5 zone subsystem plus the `neotko_zone_lean_deg` / corridor machinery.
- No new config key; the `SupportMaterial.cpp` addition is ~1400 diff lines to a file 2.5 may have restructured (category D).

## Verification
- Block tree print-verified by the fork (vase with two stumps); a plate with no zones slices as before.

## Open questions
- Confirm the user wants the whole zone subsystem before starting (SU-5+SU-6 together).
- Data-model migration for v1..v4 gesture blobs.
