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
## Fix: wave roof over air on curved overhangs (2026-09-16, `26_SU-4-fix.patch`)

Symptom (user, `baloondog test.3mf`): the interface/roof + support sections at layers
~715 (`z≈64.4`) and ~991 (`z≈100.8`) print floating in the air. Reproduced and
diagnosed by slicing the project through the MCP server and dumping the G-code.

Root cause: the NeoWave hollow body only draws the perimeter of `base_polys` (the
base region at that layer). The base region is the projection of *all* contacts above,
so on a curved overhang a given roof/interface is an **interior island** of that
region. Its edges are nowhere near the region boundary wall, so it is not anchored.
This is not the thin-sliver case: at `z≈98.9–100.1` the base is a normal-sized region
whose walls sit ~9 mm from the roof edge (confirmed: `wavesupport_wall_loops=0` prints
a base line at the same XY, `=1` prints nothing there).

Fix (in `Support/SupportCommon.cpp::generate_support_toolpaths`, `[ORCAPORT:SU-4]`):

- Precompute `interface_above[i]`: support layer `i`'s next layer up (`i+1`) carries a
  top-contact/interface polygon (matched by `print_z`, so it is independent of the
  container indexing). If so, the base layer `i` — the "floor" directly under an
  interface stack — is printed with the **normal base pattern** instead of hollow
  walls, so the roof always rests on a floor that spans the base region and is
  anchored at its own boundary walls.
- Safety net: if an inset wall produces no path for a base region (region narrower
  than ~one bead width), that sliver is filled with the normal base pattern instead of
  being dropped. (`thin_parts` via a per-component opening test.)

Behavior-neutral when NeoWave is off / `wavesupport_wall_loops == 0`. Slightly more
material at each interface stack.

Verification (2026-09-16, Windows; `build_win.bat -s --no-configure`, 0 errors):

- baloondog `(144, 79)` column: before, interface at `z≈100.76` had no base below it;
  after, base `z=100.446` is present and the interface is anchored. Material +0.3%.
- Total G-code "interface with a >5 mm void below it" points 618 → 482. The residual
  floats are also present with stock `normal(auto)` support (they are not NeoWave
  regressions).

## Files

- `src/libslic3r/Fill/FillWaveRoof.{hpp,cpp}` (new)
- `src/libslic3r/PrintConfig.{hpp,cpp}`, `Preset.cpp`, `src/libslic3r/CMakeLists.txt`,
  `Support/SupportCommon.cpp`, `Support/SupportMaterial.cpp`,
  `src/slic3r/GUI/Tab.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`
- Patch: `porting/patches/10_SU-4.patch`, fix in `porting/patches/26_SU-4-fix.patch`.
