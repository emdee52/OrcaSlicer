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
4. **Approach A - keep the loop closed.** The seam endpoints stay at nominal and only the
   interior points inside the taper are pushed in (profile `depth * sin(pi*d/taper)`). This is a
   deliberate deviation from preFlight, which moves the seam point itself.

## Amendment (post-test)

First build mangled bores and made the outer-wall seam vanish. Root causes:

1. **Seam vanishes (Nip / Tuck / Alt):** Orca's preview records a seam only when the external
   loop's end returns within 0.25 mm of its start (`GCodeProcessor.cpp:5416`,
   `squaredNorm() < 0.0625`). Moving only one endpoint by `depth = 0.9*width` (~0.38 mm) breaks
   that, so no seam is stored. Nip/Tuck moved both ends equally, so it kept the seam - exactly
   what the user saw.
2. **Mangled bore (gap + hook):** same open-loop cause, plus the push used preFlight's
   `normalize(d0 - d1)` bisector, which degenerates to noise on Orca's polygonized seam.
3. **Corner check on holes:** a polygonized bore's seam sits on a vertex, so the 44 deg check
   disabled the notch intermittently (on/off per layer).

Fixes applied:
- **Approach A** (endpoints nominal, interior sine taper) -> loop stays closed -> seam detected
  in every mode and no open wall.
- **Corner check dropped for holes**, kept for outer contours.
- **Push along the inward normal** (`left` oriented to the solid), not the bisector.
- **Min-loop-length guard** (`loop_len < notch_width*3` -> skip) + taper clamped to <= 25% of
  the loop.
- **Inner relief** (`trim_inner`): the first inner perimeter emitted after a notched external,
  within 3 mm of the projected V-leg, is nudged deeper along the notch direction. Orca-native
  (offset) instead of preFlight's cut-a-gap, so the inner loop also stays closed. Consumed once
  per external via `m_seam_notch`/`m_seam_notch_trimmed` in `GCode`.
- **Debug flag:** set `ORCA_SEAM_NOTCH_DEBUG=1` to log per-loop `layer, hole, ccw, width,
  loop_len, apply/skip+reason, taper, depth, push` (and the inner relief) at warning level.

## Inward direction

`T` = unit travel direction at the seam. `left = rotate(T, +90)`. `inward = loop_ccw ? left : -left`,
negated for a hole. That is the radial direction into the solid on a bore and is stable
regardless of the loop being reversed for wall direction.

## Verification

- Off by default (`seam_type = regular`) -> byte-identical G-code.
- Build: `build_win.bat -s --no-configure -j 8`.
- Manual (pending): with `Holes only`, bores get a closed V channel and keep their seam; outer
  seams untouched. With `Outer only`, the outer wall keeps its seam in every mode (Nip, Tuck,
  Nip/Tuck, Alt). Sharp outer corners are skipped per `seam_notch_angle`; holes always notch.

## Known gaps

- v1 trims only the inner perimeter emitted **after** the external (default OuterInner wall
  order) and only the one nearest the seam (within 3 mm). InnerOuter order would miss it.
- The inner relief is an offset, not preFlight's gap cut; no inner split/seam is introduced.

## Files

- `src/libslic3r/OrcaExt/SeamNotch.{hpp,cpp}` (new)
- `src/libslic3r/PrintConfig.{hpp,cpp}`, `Preset.cpp`, `src/libslic3r/CMakeLists.txt`,
  `src/libslic3r/GCode.cpp`, `src/slic3r/GUI/Tab.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`
- Patch: `porting/patches/14_PF-1.patch`.
