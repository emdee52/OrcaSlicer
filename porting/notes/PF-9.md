# PF-9 - Interlocking perimeters (native re-implementation)

Source: preFlight v1.3.0 (`github.com/oozebot/preFlight`), fork sha `f74dc69`.
Files: `PerimeterGenerator.cpp` (Athena-based), `GCode.cpp`, `LayerRegion.cpp`, `ExtrusionRole.*`,
`Fill.cpp`, keys `interlock_*`.

## Intent

Add interlocking shells at the infill boundary whose bead width and flow alternate from layer to
layer, so consecutive layers compress into each other's gaps and Z bonding improves (~10-15%
strength claim) with no extra material or time. PF-9 also offers a "Perimeters while interlocking"
wall-count reduction and solid top/bottom margins.

## Why this is a re-implementation, not a port (category D)

preFlight's implementation depends on things Orca does not have:

- **Athena skeletal trapezoidation** to generate the interlocking shells (variable-width lines per
  inset). Orca has Arachne/NeoArachne, not Athena.
- A **16-bit `ExtrusionRoleModifier` bitmask** with an `Interlocking` modifier. Orca's
  `ExtrusionRole` is a plain enum.
- **Per-segment flow multipliers** in GCode. Orca's GCode pipeline has no such primitive.
- A large coverage-walking **visibility/clip system** to keep the pattern off solid and near-surface
  areas, plus `interlock_flow_detection` boundary sampling.

The user's insight drove the port: Orca already has `alternate_extra_wall`, which adds one extra
wall on alternate layers (Classic `PerimeterGenerator.cpp:1409`, Arachne `:2394`). That is the
seed of the mechanism; the port generalizes it and adds the alternating bead tiers.

## What was ported (intent, on Orca's shell generator)

New `PrintRegionConfig` keys (default off, PF-9 defaults):
- `interlock_perimeters_enabled` (bool, false)
- `interlock_perimeter_count` (int, min 3, default 5) - interlocking shells inside the regular walls
- `interlock_regular_perimeters` (int, 0 = off) - regular wall-count override on interlocking layers
- `interlock_solid_layers_top` / `interlock_solid_layers_bottom` (int, 3) - solid margins
- `interlock_perimeter_strength` (percent, 100)
- `interlock_perimeter_overlap` (float-or-percent over layer height, 10.73%)

Dropped: `interlock_flow_detection` (Athena-specific boundary sampling) and
`interlocking_perimeter_extruder` (multi-extruder scope). Reuses `erPerimeter` (no new role).

Behaviour (Classic wall generator only):
- `process_classic` produces the normal walls, then `generate_interlocking_perimeters` adds the
  interlocking rings inside the area left by them (all infill types work). `interlock_regular_perimeters`
  overrides the regular wall count first.
- The rings follow PF-9's **alternating spacing schedule** so consecutive layers nest:
  - `w = perimeter width`, `main_w = w*sqrt2`, `boundary_w = w*sqrt(INTERLOCK_BOUNDARY_FLOW)`,
    `boundary_shift = (boundary_w - w)/2`;
  - `overlap_amount = (w - perimeter_spacing) + interlock_overlap`,
    `il_adjacent = (w + main_w)/2 - overlap_amount`, `il_gapped = 2*il_adjacent`;
  - first-ring offset `spacing_0` = odd ? `il_adjacent` : `il_gapped - boundary_shift`; ring-to-ring
    `spacing_x = il_gapped`; innermost `spacing_innermost` = odd ? `il_gapped - boundary_shift`
    : `il_adjacent`. The `spacing_0`/`spacing_innermost` swap shifts the stack by ~half a spacing
    between layers -> the diamond nesting.
- Per ring: shell 0 geometry width is `boundary_w` on even layers and `w` on odd; inner shells are `w`.
  The flow tier (shell 0: `INTERLOCK_BOUNDARY_FLOW` even / 1.0 odd; inner: 2.0) multiplies
  `path.mm3_per_mm` by `1 + (tier - 1)*strength`, so the over-extrusion is in the flow (as PF-9 does),
  not the width. `strength = 0` is a no-op; `strength = 100%` gives the PF-9 tiers.
- Ring footprints (`offset(ring, width/2)`) are subtracted from the fill area so the infill fills the
  channels between the rings.
- **Solid margins**: `LayerRegion::make_perimeters` walks `upper_layer`/`lower_layer` and marks
  `g.interlock_solid_margin_ok = false` if a top surface (layer extends beyond the one above) is
  within `interlock_solid_layers_top` layers or a bottom surface within `..._bottom`. Interlocking is
  skipped on those layers.
- **Arachne**: not supported; the keys are disabled when `wall_generator != Classic`.
- Gated off for spiral vase and when sparse infill density is 0, and requires
  `ensure_vertical_shell_thickness != All` (warned/auto-fixed in `ConfigManipulation.cpp`).

**Fix history.** The first implementation reused the onion loop's standard spacing and only varied
bead widths, so the rings stacked directly and did not interlock (confirmed by cross-section
comparison). The dedicated ring pass above was added to reproduce the alternating spacing. A
follow-up fixed a units error: the scaled bead width was written into `path.width` (which is mm) and
the coverage offset used the unscaled width, producing ~2 mm beads; `path.width` now uses the
unscaled width. A second follow-up fixed the fill coverage: `covered` was built with
`offset_ex(ring, +w/2)`, which grows the **filled** ring polygon and swallows the whole centre, so
the infill vanished (0.03 g on a 30 mm cube). It now subtracts only the ring band
(`offset_ex(ring, +w/2) − offset_ex(ring, −w/2)`). The geometry width uses PF-9's `sqrt(flow)·w`
(<=1.414x, ~0.59 mm inner) so the widest bead stays printable on a 0.4 mm nozzle; the over-extrusion
itself is carried by `mm3_per_mm` (PF-9 applies it as a per-segment flow in GCode). A true
over-extruded bead is also physically taller, which Orca's constant-layer-height preview cannot show
(known visual difference vs PF-9).

## Approximation vs PF-9 (known limitations)

- The rings are generated with plain polygon offsets on Orca's Classic path, not PF-9's Athena
  skeletal trapezoidation, so the exact bead placement (and the derived boundary shift that aligns
  200% beads across layers) is approximated rather than identical. The alternating spacing schedule
  and over-flow reproduce the nesting intent.
- Generated from the innermost area (all infill types work), not Athena's skeleton, so no
  flow-through/visibility rework and no `interlock_flow_detection`.
- Arachne has no tier overrides.

## Anchors

- `src/libslic3r/PrintConfig.hpp :: PrintRegionConfig` - the seven keys (after `alternate_extra_wall`).
- `src/libslic3r/PrintConfig.cpp` - registration (`[ORCAPORT:PF-9]` block).
- `src/libslic3r/Preset.cpp :: s_Preset_print_options` - key list.
- `src/libslic3r/PerimeterGenerator.hpp` - `interlock_solid_margin_ok` + interlock state.
- `src/libslic3r/PerimeterGenerator.cpp :: generate_interlocking_perimeters` (ring pass + spacing
  schedule), `process_classic` (state + call site + fill coverage), `process_arachne` (Classic-only).
- `src/libslic3r/LayerRegion.cpp :: make_perimeters` - solid-margin precompute.
- `src/slic3r/GUI/Tab.cpp` (Strength > Walls), `ConfigManipulation.cpp` (master toggle + EVST conflict).

## Verification

**Status: runtime/visual-verified by the user** after the fix history below (alternating spacing
confirmed, sparse infill correct, bead widths printable). Off-by-default equivalence is by
construction (new keys default off; no existing path reads them).

Build clean (`build_win.bat -s --no-configure`). Manual protocol:
1. Off by default -> byte-identical slicing.
2. Enable with `count=5, strength=0` -> extra walls only (no over-extrusion).
3. `strength=100` -> interlocking shells visible in Preview; even/odd layers show the boundary vs
   nominal shell-0 width and 200% inner beads.
4. Any sparse infill pattern works (unlike the alternating-extra-wall + concentric hack).
5. Solid top/bottom layers have no interlocking (margins); first/top layers excluded.
6. A print test at a small `overlap` then increase until over-extrusion, per the tooltip guidance.

## Bookkeeping

- Patch `18_PF-9.patch`; manifest row updated; `PORTING_PLAN.md` records PF-7/PF-8 excluded and
  PF-9 as a native re-implementation.
