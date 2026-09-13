# AS-3 - Snap & Drag

- Source: `NEOTKOCM_RELEASE_2_39.md`, `2_40.md`, `2_43.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c3508e5a92` (new files), `e2cfcff6d9` (Snap to bed), `79a3cae5c6` (panel + group).
- Category: B (GUI-local).

## New files
- `src/slic3r/GUI/GravitySnap.cpp/.hpp` (registered `src/slic3r/CMakeLists.txt:211-212`)

## Upstream anchors (fork lines)
- `GLCanvas3D.cpp:5031-5235` - live drag resolution + hysteresis (`m_snapdrag_engaged`, engage 0.20 / release 0.08)
- `GLCanvas3D.cpp:5684-5810` - `do_move` commit path (`_snapdrag_active/_rigid/_group`, `floor_z_for_instance` ~5745/5765)
- `GLCanvas3D.cpp:1064,1145,1219,1459` - indicator + render; `GLCanvas3D.cpp:10840-10911` - "Snap & Drag" ImGui panel
- `GLCanvas3D.hpp:605,619,762` - state, `SNAPDRAG_ENGAGE_RATIO=0.20`, `SnapDragIndicator`
- `PartPlate.cpp:1045,1425` - magnet icon; `Plater.cpp:23173` toggles panel

## Keys (app_config only, never PrintConfig)
- `neotko_snap_drag` | bool | false | `GravitySnap.cpp:182`
- `neotko_snap_drag_bed` | bool | true (absent => true) | `GravitySnap.cpp:190`
- `neotko_snap_drag_group` | bool | false | `GravitySnap.cpp:200`

## Gates to remove / reconcile
- `GravitySnap.cpp:177-183` `enabled()` requires `gravity_allow_free_z()` (AS-1) then the key.
- `GravitySnap.cpp:214-219` `plate_icon_available()` gates on `neotko_libre_mode`, not True Objects. Decouple (remove the Libre Mode requirement).
- `MainFrame.cpp:1704-1707` master-off forces `neotko_snap_drag=false`; adapt to the AS-1 key model.
- Debug channel `ORCA_DEBUG_GRAVITY` in the drag hot path.

## Coupling / blockers
- AS-3 raycasts GLVolume meshes (`GravitySnap.cpp:131-173`, `AABBMesh::query_ray_hit`) and tests convex-hull footprint overlap (~222-320). It does **not** call `InstanceContact` or `GravityFloor`.
- Depends on AS-1's `gravity_allow_free_z()` for the free-Z context.
- `SnapDragIndicator` needs shadow/overlay GL code and shaders added in the same commit; may be simplifiable.

## Verification
- Drag over a lower object lands on it; grazing does not engage (hysteresis); hollow box lands on rim/interior; selection block moves rigidly; Snap-to-bed off keeps floating; nothing qualifies -> stays put.

## Open questions
- Which UI generation to port: only the 2.4.3 magnet panel exists at HEAD.
- `floor_z_for_instance` returns `nullopt` to mean "leave floating"; preserve that or everything slams to Z=0.
- Hysteresis thresholds are hard-coded (no key).
