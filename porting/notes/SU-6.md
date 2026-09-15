# SU-6 - Support Zones (block trees)

- Source: `NEOTKOCM_RELEASE_2_45.md`.
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741` (stump engine), `9862461f75` (Windows fix).
- Category: B. Status: **ported** - as part of the SU-5 port (see Findings).

## What it is
Painting a zone with the **Brush** footprint builds a **block tree**: a stump is planted (from the
painted footprint), the column grows plumb below it, walks toward the stump through the gap and
closes onto it once there. One stump grows one trunk (no branching); additional stumps split a long
or forked area into two legs. The panel names the layer and how far short an unreachable column fell.
No new config key - the mode is inferred from geometry (`zone_guided` is set only when a gap exists
between the bottom band and the top).

## Findings: already ported with SU-5
The fork's SU-6 code is confined to exactly three files (verified by an `src`-wide search for
`stump`/`block tree`/`tocon`): `SupportMaterial.cpp`, `GLGizmoSupportZones.hpp`,
`GLGizmoSupportZones.cpp`. All three were ported from the fork's **HEAD** in the SU-5 work (the gizmo
copied verbatim; the whole `v2.3.5..HEAD` `SupportMaterial.cpp` diff applied). So SU-6 shipped inside
`port/SU-5` and `porting/patches/11_SU-5.patch`; there is no separate delta and no `12_SU-6.patch`.

Evidence (normalizing the `neo_` -> `gizmo_` / `NeoCol` -> `GizmoCol` rename done for SU-5):
- gizmo `.cpp`: 89 fork `stump` lines -> 0 missing; `.hpp`: 10 -> 0 missing.
- engine: 80 `zone_guided|zone_stump|support_stump_cell|stump` lines -> 0 missing.
- UI symbols present: `build_block_tree_mesh`, `block_tree_mode`, `all_stumps`, `block_tree_head_z`,
  `block_tree_gap_mm`, `StumpSpot`, `m_extra_stumps`, `m_stump_size_mm`, the "3 . Stumps" panel,
  the Brush-footprint branch.
- Engine symbols present: `zone_stumps`, `zone_guided`, `zone_stump_top`, `support_stump_cell`, the
  stump-guided descent and the unreachable-root warning.
- Data model: gesture JSON v4 writes `tk` (stumps) + `tz` (stump size) and the reader accepts
  `v1..v4`, so the schema migration is ported.
- Windows fix `9862461f75` (file-scope `PI_D` for MSVC C3493) present at
  `GLGizmoSupportZones.cpp:1258`.
- Builds clean as part of SU-5.

## Keys
- none (mode inferred from geometry; stumps travel in the `support_zone_gesture` JSON, version 4).

## Gates removed
- Same single gizmo gate as SU-5 (the LibreMode requirement in `on_is_activable`), already removed.

## Verification
- Build: clean with SU-5.
- Manual (pending, the fork's own test): a vase-like part with two painted stumps; the column walks
  to the stump and closes onto it; a plate with no zones slices as before.

## Open questions
- Resolved: the whole zone subsystem (SU-5 + SU-6) landed together; no separate data migration is
  needed beyond the ported v1..v4 gesture reader.
