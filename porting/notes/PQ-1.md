# PQ-1 - Bridging infill extra expansion

- Source: `NEOTKOCM_RELEASE_2_40.md`.
- Fork: `OrcaFS-NeotkoCM` @ `e2cfcff6d9`.
- Category: B, low risk. Additive; default 0 = byte-identical.
- Status: **ported** (branch `port/PQ-1`), build clean.

## Key
- `bridge_expansion_extra` | `coFloat` | `0` | min 0, max 999 | Quality, `comAdvanced`
  - `PrintConfig.cpp` (after `bridge_flow`), `PrintConfig.hpp` (region config), `Preset.cpp`,
    `Tab.cpp` (Quality > Bridging).

## Implementation (target 2.5)
All inserted code is tagged `[ORCAPORT:PQ-1]`.
- `src/libslic3r/LayerRegion.cpp` - `process_external_surfaces`:
  - `expansion_bottom_bridge = expansion_top + scaled(max(0, bridge_expansion_extra))`.
  - When `> 0`, `stBottom` is added as a fourth `ExpansionZone` (bridge donor) so the bridge
    can grow into the supported bottom region.
  - When `> 0`, the remaining `stBottom` area is returned to `fill_surfaces` before the
    existing `stTop` block pops the top zone (mirrors the stock stTop pattern).
- `src/libslic3r/PrintObject.cpp` - re-export invalidation: `bridge_expansion_extra` ->
  `posPrepareInfill`.
- `src/libslic3r/Preset.cpp` - region option list.
- `src/slic3r/GUI/Tab.cpp` - line added to the Bridging optgroup.

## Verification
- Off-by-default equivalence: with the value at 0 the expression reduces to `expansion_top`
  and the fourth zone is not created, so the zone list and output are stock. (By construction.)
- Compile: `build_win.bat -s --no-configure -j 8` -> 0 errors, link clean.
- Pending: a runtime G-code comparison (slice a bridge model at 0 vs a raised value) in the
  GUI; the feature is pure geometry and the default path is untouched.
