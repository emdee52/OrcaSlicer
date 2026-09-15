# SU-4b - NeoWave contact layer (Mecanismo 2)

- Source: `NEOTKOCM_RELEASE_2_37.md` (contact-layer toggle shipped).
- Fork: `OrcaFS-NeotkoCM` @ `c3508e5a92`.
- Category: B/D. Status: **ported** (branch `port/SU-4b`); build clean, runtime pending.
- Ported from the SU-4 "second half" that was previously dropped as "needs ColorStitch".

## What the fork actually does

The earlier note claimed the contact layer needs the unselected ColorStitch/`NeoweaveEngine`
pack. That is wrong for the shipped form: it only reuses a ~60-line Z-wave loop that happens
to live in `ColorStitch.cpp`. The Sandwich/color/paint system is not involved. The shipped
mechanism is 3 Support keys plus a GCode-time Z-ripple on the object's bridge fill.

- Fork keys (`PrintConfig.cpp:6182-6216`, class `PrintObjectConfig`):
  `support_neoweave_enabled` (bool, off), `support_neoweave_amplitude` (float, 0.1, 0-2 mm),
  `support_neoweave_period` (float, 0.6, 0-10 mm).
- Fork UI: Support > Advanced (`Tab.cpp:7869-7873`), visibility at `Tab.cpp:8277-8285`.
- Fork GCode gate (`GCode.cpp:9399-9438`): `sloped == nullptr && enabled &&
  role == erBridgeInfill && m_layer_index > 0 && amplitude > 1e-9`. It is a **role gate**, not a
  geometric "above a support roof" test; the fork accepts that genuine over-air bridges are also
  waved (tooltip warns).
- Fork wave (`ColorStitch.cpp:3223-3254`, contact mode): upward-only rectified sine
  `amplitude * |sin|` so it never digs into the roof; period floored to `max(0.2, line_width)`;
  8 micro-segments/period (cap 4096/line); XY feedrate capped so the implied Z speed stays within
  `interlayer_neoweave_max_z_speed` (default 20 mm/s). `restore_z` -> `travel_to_z(nominal_z)`.

## Decision: add a target selector (both sides)

The user wants to compare "wave the part" vs "wave the support". So the port adds a 4th control
`support_neoweave_target`:
- `part_bottom` (default) - fork behaviour: wave the object's own bridge fill (upward-only).
- `support_top` - new: keep the part flat and wave the support's **top-contact interface**
  (downward-only). This is **not** in the fork; it reuses the same wave math inverted.

## Target implementation (2.5)

New neutral module `src/libslic3r/OrcaExt/NeoWaveContact.{hpp,cpp}` (namespace
`Slic3r::OrcaExt::NeoWaveContact`), registered in `src/libslic3r/CMakeLists.txt`:

- `emit_part_wave(...)` / `restore_z(...)` - PartBottom target; emits G-code directly (upward sine).
- `apply_support_wave(ExtrusionPath&, ...)` and the collection overload - SupportTop target;
  rewrites a support path's polyline into micro-segments with a scaled negative Z offset and sets
  `z_contoured`, so target's existing variable-Z GCode emitter (`extrude_to_xyz`) is used.
- `xy_feedrate_cap(...)` / `resolve_period_mm(...)` - shared geometry.

Config (`PrintObjectConfig`, appended after the SU-4 `wavesupport_*` block):
`support_neoweave_enabled`, `support_neoweave_target` (enum `NeoWaveContactTarget`:
`nwctPartBottom`, `nwctSupportTop`; serialized `part_bottom` / `support_top`),
`support_neoweave_amplitude`, `support_neoweave_period`, `support_neoweave_max_z_speed`.
`Preset.cpp` whitelist.

Anchors:
- `GCode.cpp` :: `_extrude` ::
  - object-side: after `m_writer.set_speed(F, ...)` and before the arc-fitting branch, added the
    PartBottom wave as an `if` sibling of the G1/arc branch (forces G1 for those paths).
  - support-side: before `double F = speed * 60;`, cap the feedrate of z_contoured support paths
    when target == SupportTop.
- `Support/SupportCommon.cpp` :: `generate_support_toolpaths` :: after
  `extrude_interface(top_contact_layer, ...)`, if target == SupportTop and the layer is
  `SupporLayerType::TopContact`, wave `top_contact_layer.extrusions`.
- `src/slic3r/GUI/Tab.cpp` :: Support > Advanced, 5 option lines.
- `src/slic3r/GUI/ConfigManipulation.cpp` :: visibility (master when support is on; target/params
  when enabled).
- `src/slic3r/GUI/Gizmos/GLGizmoSupportZones.cpp` :: restored the SU-5 `support_neoweave_enabled`
  seed (the key is registered again; this is the exact key whose absence crashed undo snapshots).

## Behavior-neutral at defaults

`support_neoweave_enabled` defaults off, so no wave is emitted on either side and the toolpath is
unchanged. `support_neoweave_target` defaults to `part_bottom`.

## Deviations / gaps

- `support_top` is original work (not in the fork); it is slice-only until printed.
- The fork reads a shared `interlayer_neoweave_max_z_speed`; here it is its own key
  `support_neoweave_max_z_speed` (default 20 mm/s).
- Orca's G-code preview does not render the Z variation (same as NeoWave/ZBump) - it looks flat.
- The PartBottom gate also catches genuine over-air bridges (fork trade-off).

## Verification

- Build: `build_win.bat -s -j 8` (configure for the new files) -> 0 errors; `NeoWaveContact.cpp`,
  `SupportCommon.cpp`, `GCode.cpp`, `ConfigManipulation.cpp`, `Tab.cpp` compiled; `OrcaSlicer.dll`
  linked 2026-09-15 03:38.
- Manual (pending): slice a part with a flat bottom over support; with the toggle off the G-code is
  byte-identical to stock. On + `part_bottom`: Z on the `erBridgeInfill` lines oscillates upward by
  the amplitude at the period. On + `support_top`: the part's bridge lines stay flat and the support
  top-contact lines dip downward. Compare both prints for support-removal force.

## Files

- `src/libslic3r/OrcaExt/NeoWaveContact.{hpp,cpp}` (new)
- `src/libslic3r/PrintConfig.{hpp,cpp}`, `Preset.cpp`, `src/libslic3r/CMakeLists.txt`,
  `src/libslic3r/GCode.cpp`, `src/libslic3r/Support/SupportCommon.cpp`,
  `src/slic3r/GUI/Tab.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`,
  `src/slic3r/GUI/Gizmos/GLGizmoSupportZones.cpp`
- Patch: `porting/patches/13_SU-4b.patch`.
