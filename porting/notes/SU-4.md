# SU-4 - NeoWave Support (wave-roof half)

- Source: `NEOTKOCM_RELEASE_2_36.md`, `2_37`, `2_39`.
- Fork: `OrcaFS-NeotkoCM` @ `e25039606d` (engine), `c3508e5a92`, `c1d9cc2890`.
- Category: B/D. Status: **ported** (branch `port/SU-4`), wave-roof half; build clean, runtime pending.

## Scope (decided)
Only the **wave-roof half** (Mecanismo 1). The NeoWave contact layer (Mecanismo 2) is **not ported**:
it needs the unselected ColorStitch/`NeoweaveEngine` pack, which target 2.5 does not contain.

## Approach: re-implement, do not copy the duplicate
The fork ships `Support/WaveSupport.cpp` (~3,400 lines), a copy of `SupportMaterial.cpp` with the wave
divergence inside. Diffing the fork's `generate_support_toolpaths` against its forked
`wavesupport_generate_toolpaths` showed the actual wave logic is only ~70 lines: a `FillWaveRoof`
interface fill and a hollow-wall-loops base branch. So instead of copying the engine, target's
existing Normal engine (`PrintObjectSupportMaterial` / `SupportCommon`) is reused and the wave logic
is added, gated on the new support type. That keeps the port reviewable and avoids maintaining a
3,400-line fork copy.

## Implementation (target 2.5)
All inserted code tagged `[ORCAPORT:SU-4]`; the new file pair carries the `[ORCAPORT FILE]` stamp.

- New `src/libslic3r/Fill/FillWaveRoof.{hpp,cpp}` - the Wave-Huygens roof fill (Wave/Concentric
  shape, Smart/ZigZag/Monotonic order, reverse). Adapted from the fork (itself adapted from
  OrcaSlicer-WaveOverhangs by Klappe, AGPLv3): `NeoDebug`/`WAVEROOF_LOG` dropped. **API drift fixed:**
  target 2.5 stores extrusion paths as 3D `Polyline3`, so the planar algorithm converts at the
  `ExtrusionPath` boundary (`wave_to_points3` / `wave_to_polyline`).
- `PrintConfig.{hpp,cpp}` - appended **last** (serialized indices stay stable):
  `SupportType::stWaveSupport`, `SupportMaterialInterfacePattern::smipWave`, and the two enum mirrors
  `SupportMaterialWaveRoofPattern{Concentric,Wave}` / `SupportMaterialWaveRoofOrder{Smart,ZigZag,Monotonic}`.
  New keys `wavesupport_roof_pattern`, `wavesupport_roof_order`, `wavesupport_roof_reverse`,
  `wavesupport_wall_loops`. `Preset.cpp` key list.
- `Support/SupportCommon.cpp::generate_support_toolpaths`:
  - roof fill: when `support_type == stWaveSupport && support_interface_pattern == smipWave` and the
    layer is TopContact/Interface, fill with `FillWaveRoof`; if it yields nothing, fall back to the
    normal interface fill (a roof is never left empty).
  - base: NeoWave hollow body as N concentric perimeters (`wavesupport_wall_loops`), 1st-layer flange
    kept solid for adhesion.
- `Support/SupportMaterial.cpp` - the three `stNormalAuto` guard sites also accept `stWaveSupport`
  (auto-overhang detection and the top-contact guard), matching the fork's local extensions.
- GUI: `Tab.cpp` appends the four roof options under Support > Advanced. `ConfigManipulation.cpp`
  coerces base=`Hollow`/interface=`Wave` when NeoWave is selected, greys those two fields, and toggles
  the roof options only when the NeoWave roof is active. The NeoWave/Wave combo entries come from
  `PrintConfig.cpp` automatically.
- `src/libslic3r/CMakeLists.txt` registers the new fill files.

## Behavior-neutral at defaults
`support_type` still defaults to `stNormalAuto`; the wave branches require `stWaveSupport`, and
`smipWave`/`wavesupport_*` are inert outside it. Normal/Tree output is unchanged.

## Deviations / gaps
- The fork's forced `tree_support_wall_count = 2` NeoWave default is not ported (it touches an
  unrelated tree key); the hollow walls are opt-in via `wavesupport_wall_loops`.
- The fork's `SupportType::stWaveSupport` was LibreMode-gated; the gate is removed per policy.
- Contact-layer Z-oscillation (Mecanismo 2) is out of scope (see above).

## Verification
- Build: `build_win.bat -s -j 8` then `-s --no-configure` after killing a running slicer that held
  the DLL -> 0 errors; `FillWaveRoof.cpp`, `SupportCommon.cpp`, `ConfigManipulation.cpp`, `Tab.cpp`
  compiled; `OrcaSlicer.dll` linked (2026-09-14).
- Manual (pending): select Support Type = NeoWave -> base/interface lock to Hollow/Wave; a wave roof
  is emitted over flat overhang footprints; `wavesupport_wall_loops` produces a hollow walled body;
  roof shape/order/reverse change the toolpath; Normal/Tree support unchanged.

## Files
- `src/libslic3r/Fill/FillWaveRoof.{hpp,cpp}` (new)
- `src/libslic3r/PrintConfig.{hpp,cpp}`, `Preset.cpp`, `src/libslic3r/CMakeLists.txt`,
  `Support/SupportCommon.cpp`, `Support/SupportMaterial.cpp`,
  `src/slic3r/GUI/Tab.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`
- Patch: `porting/patches/10_SU-4.patch`.
