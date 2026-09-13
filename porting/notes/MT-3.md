# MT-3 - Bed heats for the hottest filament

- Source: `NEOTKOCM_RELEASE_2_44.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c1d9cc2890`.
- **Status: upstream-present. Do not port.**

## Why
Upstream OrcaSlicer 2.5 already implements this, and by default:
- `src/libslic3r/PrintConfig.cpp:2956` - `bed_temperature_formula` (coEnum), default `btfHighestTemp` (`:2965`).
- `src/libslic3r/GCode.cpp:4637` - `get_highest_bed_temperature(is_first_layer, print)`.
- Applied at `GCode.cpp:3530` (placeholder), `:4657` (first layer M140/M190), `:5774` (second layer).

Neotko's divergent implementation (`get_bed_temperature_max`) is redundant.

## Difference worth a small standalone check (optional)
- Neotko also adds a `btDefault -> PEI` fallback in `GCode::get_bed_temperature` (fork `GCode.cpp:3984`). Upstream's `get_highest_bed_temperature` does not have it.
- Determine whether upstream can still reach `btDefault` at those call sites; if yes, this fallback may be worth a tiny separate fix (not MT-3).

## Action
- Marked `upstream-present` in `manifest.tsv`; removed from the port work.
- Record the upstream commit that introduced it if a citation is needed later.
