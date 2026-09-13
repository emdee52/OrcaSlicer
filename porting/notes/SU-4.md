# SU-4 - NeoWave Support

- Source: `NEOTKOCM_RELEASE_2_36.md`, `2_37`, `2_39`.
- Fork: `OrcaFS-NeotkoCM` @ `e25039606d` (engine), later `c3508e5a92`, `c1d9cc2890`.
- Category: B/D. Two mechanisms with very different portability.

## Mechanism 1 - wave-roof hollow support (standalone-able)
- New files: `src/libslic3r/Support/WaveSupport.cpp/.hpp` (191 KB, ~3400-line duplicate of SupportMaterial), `src/libslic3r/Fill/FillWaveRoof.cpp/.hpp`.
- Anchors: `Support/SupportCommon.cpp:2054` `wavesupport_generate_toolpaths(...)`; `PrintObject.cpp:5009` dispatch; `PrintConfig.cpp:5856/6104/6126`.
- Gate to remove: `PrintObject.cpp:5009` `... && m_config.neotko_libre_mode.value`; UI gates `Tab.cpp:8266,8283,8297`.

## Mechanism 2 - neoweave contact layer (BLOCKED without ColorStitch)
- Applies Z-oscillation to the object's bridge fill above a support roof (`GCode.cpp:9399-9438`).
- Depends on the **unselected** ColorStitch/`NeoweaveEngine` (`ColorStitch.cpp:3038+`), which target 2.5 does not contain at all, plus `PrintRegionConfig` `interlayer_neoweave_*` / `infill_neoweave_*` keys.
- Recommendation: port mechanism 1 only, or defer SU-4 entirely until a decision is made.

## Keys (all default-off)
- `support_type` + enum `neowave` (`PrintConfig.cpp:5860`)
- `support_interface_pattern` + enum `wave` (`PrintConfig.cpp:6116`)
- `wavesupport_roof_pattern` | enum {concentric, wave} | `wave` | `:6127`
- `wavesupport_roof_order` | enum {smart, zigzag, monotonic} | `smart` | `:6142`
- `wavesupport_roof_reverse` | bool | `false` | `:6159`
- `wavesupport_wall_loops` | int 0-10 | `0` | `:6170`
- `support_neoweave_enabled` | bool | `false` | `:6183`
- `support_neoweave_amplitude` | float 0-2 | `0.1` | `:6195`
- `support_neoweave_period` | float 0-10 | `0.6` | `:6206`

## Coupling / blockers
- Wave roof duplicates `SupportMaterial`; re-implement intent, do not paste.
- `Support/SupportMaterial.cpp` may have been restructured in 2.5; expect category D.
- `SurfaceColorMix.cpp` carries an orphan duplicate of NeoweaveEngine (not compiled) - ignore.

## Verification
- Selecting NeoWave locks base=Hollow/interface=Wave; wave roof closes over a hollow pillar; contact-layer Z variation appears on bridge fill (G-code preview looks flat - known).

## Open questions
- Split wave roof from contact layer? Contact layer is not portable without the ColorStitch pack.
- Target already has `smipSpiralInset`; append `smipWave` last, never at the fork's index.
