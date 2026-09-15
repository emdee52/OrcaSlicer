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

Behaviour:
- When enabled, `process_classic` and `process_arachne` set `loop_number = base + count` on **every**
  layer (subject to the gates), so the onion loop generates `count` extra interlocking rings inside
  the regular walls. `interlock_regular_perimeters` overrides the regular wall count first.
- **Classic tier overrides** (`traverse_loops`): for depths beyond `m_interlock_base_depth`,
  - shell 0 (adjacent to the regular walls): boundary bead, flow `INTERLOCK_BOUNDARY_FLOW` =
    `(3 + 2*sqrt2)/4` ~= 145.7% on **even** layers, nominal 100% on **odd** layers;
  - inner shells: 200% flow.
  Width and `mm3_per_mm` are both scaled by `flow_ratio = 1 + (tier_flow - 1) * strength`, so
  `strength = 0` is a no-op (normal walls) and `strength = 100%` gives the PF-9 tiers. Width scales
  as `sqrt(flow_ratio)` (round-bead assumption), matching PF-9's `main_w = w*sqrt(2)`.
- **`interlock_perimeter_overlap`** tightens the onion spacing for the interlocking depths
  (`distance -= overlap`), compressing more material into the pattern.
- **Solid margins**: `LayerRegion::make_perimeters` walks `upper_layer`/`lower_layer` and marks
  `g.interlock_solid_margin_ok = false` if a top surface (layer extends beyond the one above) is
  within `interlock_solid_layers_top` layers or a bottom surface within `..._bottom`. Interlocking is
  skipped on those layers.
- **Arachne**: gets `count` / `interlock_regular_perimeters` / solid margins only; the tier
  width/flow alternation is Classic-only (`m_interlock_active` stays false in `process_arachne`).
- Gated off for spiral vase and when sparse infill density is 0, and requires
  `ensure_vertical_shell_thickness != All` (warned/auto-fixed in `ConfigManipulation.cpp`).

## Approximation vs PF-9 (known limitations)

- The interlocking rings use Orca's **standard perimeter spacing** (adjusted only by the overlap
  key), not PF-9's bespoke `il_adjacent`/`il_gapped`/`il_external`/`il_innermost` centerline
  distances. The alternating tier widths and over-extrusion are reproduced; the exact nesting
  geometry (and the derived boundary shift that aligns 200% beads across layers) is not. Expect a
  functionally similar but not pixel-identical pattern.
- Generated from the onion rings (all infill types work), not Athena's skeleton, so no
  flow-through/visibility rework and no `interlock_flow_detection`.
- Arachne has no tier overrides.

## Anchors

- `src/libslic3r/PrintConfig.hpp :: PrintRegionConfig` - the seven keys (after `alternate_extra_wall`).
- `src/libslic3r/PrintConfig.cpp` - registration (`[ORCAPORT:PF-9]` block).
- `src/libslic3r/Preset.cpp :: s_Preset_print_options` - key list.
- `src/libslic3r/PerimeterGenerator.hpp` - `interlock_solid_margin_ok` + interlock state.
- `src/libslic3r/PerimeterGenerator.cpp :: traverse_loops` (tier width/flow), `process_classic`
  (loop count + state + spacing), `process_arachne` (count only).
- `src/libslic3r/LayerRegion.cpp :: make_perimeters` - solid-margin precompute.
- `src/slic3r/GUI/Tab.cpp` (Strength > Walls), `ConfigManipulation.cpp` (master toggle + EVST conflict).

## Verification

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
