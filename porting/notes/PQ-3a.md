# PQ-3a - NeoArachne low-risk slice (pin outer width + blend distance)

- Source: `NEOTKOCM_RELEASE_2_38.md`.
- Fork: `OrcaFS-NeotkoCM` @ `2c90c65bea`, current form `9299cb368a`.
- Category: B, but the split is not clean (see blocker).

## Intended pieces
- "Pin Outer Wall Width" (default on): extend constant outer width down into 1-2 bead regions.
  - `neoarachne_pin_outer_width` (coBool, true) -> `NeoArachneBeadingStrategy.cpp:78-96`, `NeoArachneInterior.cpp:100`.
- User-configurable transition blend distance replacing the stock 100 mm hardcode.
  - `neoarachne_transition_filter_dist_mm` (coFloat, 100) -> `WallToolPaths.cpp:548` (target's hardcode is `WallToolPaths.cpp:541`).

## Keys
- `neoarachne_pin_outer_width` | coBool | true | `PrintConfig.cpp:8662`
- `neoarachne_transition_filter_dist_mm` | coFloat 1-500 | 100 | `PrintConfig.cpp:8690`

## BLOCKER - cannot ship standalone as implemented
- Both keys are read only from `NeoArachnePlan.cpp` (~101, ~104), which is reachable only when `wall_generator == NeoArachne` (`LayerRegion.cpp:345`).
- The stock Arachne `make_paths_params` (`WallToolPaths.cpp:26-60`) reads no `neoarachne_*` key and only receives object/global config, not region config.
- `pin_outer` also needs `NeoArachneBeadingStrategy`, instantiated only via `NeoArachne::Interior`.
- Shipping PQ-3a alone requires **re-plumbing** these two options into the stock Arachne path (category D re-implementation), not a cherry-pick.

## Gates to remove (if ported with the rest)
- `LayerRegion.cpp:345-350` LibreMode branch; `Tab.cpp:8238`; `ConfigManipulation.cpp:1006`.

## Recommendation
- Treat PQ-3a and PQ-3b as one port (the enum/routing is a prerequisite for the two knobs to be reachable), or explicitly re-implement the two knobs on the stock path. Decide with the user.

## Verification
- Pin off = stock Arachne for thin regions; blend distance changes only transition smoothing; Classic/Arachne unchanged.

## Resolution
- Shipped together with PQ-3b on `port/PQ-3` (decision 7). The two knobs are reachable as
  `hybrid_pin_outer_width` and `hybrid_transition_filter_dist_mm` (fork keys
  `neoarachne_pin_outer_width`, `neoarachne_transition_filter_dist_mm`). The blocker above is
  resolved by the enum/routing, exactly as recommended. See `notes/PQ-3b.md` for the anchors,
  build evidence, and verification.
