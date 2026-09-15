# PF-1 - Nip & Tuck seams

- Source: `preFlight` (`github.com/oozebot/preFlight`) v1.3.0, `NEOTKOCM`-style changelog
  v0.9.7 / v0.9.8 ("Alternating Nip/Tuck").
- Category: D (PrusaSlicer GCode vs Orca GCode - re-implement).
- Status: in-progress (branch `port/PF-1`).

## What it is

A V-shaped notch formed into the external perimeter at the seam, with a taper, that absorbs the
start/stop blob. Modes: **Regular** (off), **Nip** (notch the start only), **Tuck** (notch the
end only), **Nip/Tuck** (both), **Alt. Nip/Tuck** (Nip on even layers, Tuck on odd). In the
asymmetric modes the non-notched endpoint is also clipped to keep the wall from doubling up.

It reshapes the wall **at** the seam; it does **not** move the seam - `seam_position` still
decides placement. It applies to every external perimeter (outer contours and holes) unless the
seam sits on a sharp corner already.

## Source (preFlight)

- `GCode.cpp:3979` `compute_seam_inward_direction` - bisector of the incoming/outgoing seam
  directions, flipped toward the interior using winding + the loop's `reversed` flag.
- `GCode.cpp:4041` `apply_notch_to_external` - subdivide the taper zone, linearise arcs, offset
  the first/last points inward by `depth * (1 - t)`.
- `GCode.cpp:4258` `apply_seam_notch_pair` - corner-sharpness skip, endpoint adjust, inner trim.
- `GCode.cpp:4530` `apply_seam_notch` - collect every external perimeter and pair it with the
  nearest inner perimeter (`perimeter_index == 1`) to trim.
- Keys: `seam_type` (`SeamNotchType`), `seam_notch_width` (default 2.0 x ext width),
  `seam_notch_angle` (default 44 deg corner-skip).

## Orca 2.5 anchors

- `GCode::extrude_loop` @ `GCode.cpp:7276`. Seam placed at `m_seam_placer.place_seam(...)`
  (`:7305`), loop copied, `is_hole = loop.loop_role() & elrHole` (`:7286`), then
  `loop.clip_end(clip_length, &paths)` (`:7336`) yields the `ExtrusionPaths` that are emitted
  via `_extrude` (`:7451`). **The notch is applied to `paths` right after `clip_end`.**
- `loop.polygon().is_counter_clockwise()` is already used in this function (`:7357`).
- `seam_position` lives in `PrintObjectConfig` (`PrintConfig.hpp:1164`), so the new keys go
  there and inherit per-object override.
- Orca has no `SmoothPath`/`ArcWelder` (preFlight's machinery); the notch is implemented on
  Orca's `Polyline3`/`ExtrusionPaths`.

## Port decisions (confirmed)

1. Keys in `PrintObjectConfig` -> **per-object** overridable.
2. New `seam_notch_target`: `All external` / **`Holes only` (default)** / `Outer only`.
   Holes are `(loop.loop_role() & ExtrusionLoopRole::elrHole) != 0`.
3. Re-implement the notch on `ExtrusionPaths`; no dependency on preFlight types.
4. **v1 scope: external-perimeter notch only.** The inner-perimeter trim (preFlight
   `apply_seam_notch_pair` splitting/trimming `perimeter_index == 1`) needs cross-loop pairing
   inside `extrude_loop`, which does not exist there - deferred to a follow-up. Documented as a
   known gap; the visible V channel is complete in v1.

## Inward direction (v1)

`T` = unit travel direction at the seam (from the first path's first segment / last path's last
segment). `L = (-T.y, T.x)` (rotate +90). `inward = ccw ? L : -L`, then negate when `is_hole`
(enclosed region is the void for a hole). The push direction is the seam bisector
`normalize(dir_start - dir_end)` (fallback `L`), oriented so `dot(bisect, inward) > 0`.

## Verification

- Off by default (`seam_type = regular`) -> no notch is applied; byte-identical G-code.
- Build: `build_win.bat -s -j 8` -> 0 errors; `SeamNotch.cpp`, `GCode.cpp`, `PrintConfig.cpp`,
  `ConfigManipulation.cpp`, `Tab.cpp` compiled; `OrcaSlicer.dll` linked 2026-09-15 05:16.
- Manual (pending): slice a part with bores + smooth outer walls; with `Holes only` the bore
  seams get a V channel and the outer contour seams stay normal; `Outer only` inverts that;
  `Nip` shapes only the start, `Tuck` only the end, `Alternating` swaps per layer; sharp-corner
  seams are skipped per `seam_notch_angle`.

## Known gaps

- v1 does **not** trim the adjacent inner perimeter (preFlight's `apply_seam_notch_pair`
  inner split). Requires pairing the external loop with `perimeter_index == 1` across
  separate `extrude_loop` calls; deferred. The visible external V channel is complete.

## Files

- `src/libslic3r/OrcaExt/SeamNotch.{hpp,cpp}` (new)
- `src/libslic3r/PrintConfig.{hpp,cpp}`, `Preset.cpp`, `src/libslic3r/CMakeLists.txt`,
  `src/libslic3r/GCode.cpp`, `src/slic3r/GUI/Tab.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`
- Patch: `porting/patches/14_PF-1.patch`.
