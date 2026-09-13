# PQ-1 - Bridging infill extra expansion

- Source: `NEOTKOCM_RELEASE_2_40.md`.
- Fork: `OrcaFS-NeotkoCM` @ `e2cfcff6d9` (bundled into a large 2.40 commit; isolate by the `bridge_expansion_extra` hunks).
- Category: B, low risk. Additive; default 0 = byte-identical.

## Upstream anchors (fork lines)
- `src/libslic3r/LayerRegion.cpp` `process_external_surfaces` (~697)
  - `expansion_bottom_bridge = expansion_top + scaled<float>(std::max(0., config.bridge_expansion_extra.value));` (~729)
  - add a 4th `ExpansionZone{ stBottom, expansion_params_into_solid_infill }` (~786)
  - remove `stBottom`, append leftover expolygons, pop_back (~864-870)
  - optional `NeoDebug::BOTTOM` trace (~811) - drop
- `src/libslic3r/PrintObject.cpp:1359` invalidation -> `posPrepareInfill`
- `PrintConfig.cpp:1170`, `PrintConfig.hpp:1239`, `Preset.cpp:913`, `src/slic3r/GUI/Tab.cpp:7625`

## Keys
- `bridge_expansion_extra` | `coFloat` | `0` (min 0, max 999, no practical cap) | `PrintConfig.cpp:1170` (Quality, `comAdvanced`)

## Gates to remove
- none (no LibreMode/env behavior gate). Drop only the debug trace.

## Coupling / blockers
- Relies on existing `ExpansionZone` / `expand_merge_surfaces` / `expand_bridges_detect_orientations`.
- Target's bridge block has newer relative/align bridge-angle logic (`LayerRegion.cpp:538-557`) the fork base lacks: re-implement intent there, not a paste.

## Verification
- 0 = byte-identical to stock; increasing value expands the bridge into stBottom; custom bridge angle honoured; only external bridges affected.

## Open questions
- Confirm `expansion_params_into_solid_infill` is still the right shared params for the 4th zone in 2.5.
