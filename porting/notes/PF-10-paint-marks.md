# PF-10-paint-marks - painted passes keep their per-layer support treatment

- Source: our own bug (no fork counterpart). Report: "auto painted supports do not get the woven
  interface / transition-layer settings, tree(auto) and normal(auto) do".
- Branch: `port/PF-10-paint-marks`, based on `port/integration`.
- Category: B (fix). Status: **implemented, build clean, tests pass; runtime print pending.**

## Symptom

With the support type at `normal(auto)` / `tree(auto)` and nothing painted, the SU-8..SU-13
settings work (transition layer speed/flow/temperature/fan, woven interface speed, contact speeds).
After painting support styles - hand-painted, or the painter's "Automatic painting" which paints
Snug/Grid/Organic - the painted supports no longer get them.

## Cause

`PrintObject::_generate_support_material()` runs one engine pass per painted style plus a default
pass (`PrintObject.cpp:5407-5431`, classic painted -> tree painted -> default/auto **last**), then
`merge_duplicate_support_layers()` (`PrintObject.cpp:5190`) collapses every pass's layer at a given
`print_z` into the first pass's layer.

That merge composed geometry only: `support_fills.entities` (clipped by the base layer's islands)
and `support_islands`. It dropped every per-layer mark the SU features record in
`generate_support_toolpaths()` (`Layer.hpp:162-176`):

| Mark | Consumer | Effect when lost |
|---|---|---|
| `transition_joint`, `transition_area` | `GCode.cpp:6637-6643`, `:7725-7990`, `:9606-9620`, `:9767-9769` | SU-8 speed/flow/temp/fan/slowdown never applied (`m_transition_joint == 0`) |
| `support_weave` | `GCode.cpp:6640`, `:9690` | SU-9 woven layer's base strips print at `support_speed` instead of the one interface speed |
| `support_contact_top`, `support_contact_bottom` | `GCode.cpp:6642-6643`, `:9693-9698` | SU-10 contact speed overrides ignored |

So every pass except the first at a `print_z` lost the treatment. With auto paint that is the whole
automatic/default pass (last) and every Organic (tree) region. Nothing painted = one pass = no
merge, which is why the auto types looked fine.

## Fix

1. New `merge_support_layer_marks(SupportLayer &base, const SupportLayer &dup)` in
   `Support/SupportCommon.{hpp,cpp}`, called from the merge loop:
   - OR `support_weave`, `support_contact_top`, `support_contact_bottom`;
   - take `transition_joint` + `transition_area` when the base has none;
   - when both are tagged, keep the first joint and union the second's footprint, so its paths
     still get a treatment.
   - `shortcut:` a `Layer` carries one joint, so two passes tagging *different* joints at the same
     `print_z` share one speed/temp. Upgrade to a per-layer joint list if a print shows it.
2. `support_fills_family` (SU-5 per-zone filament) must not survive unaligned. It is index-paired
   with the first-level entities and **an untagged entity is dropped by both readers**
   (`GCode.cpp:7161-7205` emits only families it finds; `ToolOrdering.cpp:998-1006` schedules the
   same way), so appending painted fills to a zone-tagged layer lost support geometry. The merge now
   clears the base layer's tags when it appends, and the layer prints with the normal support
   filament.
3. `SupportPaintType::support_type` removed. The dispatcher forces `stNormal` / `stTree` on purpose
   (manual = enforcer-only, no automatic overhang detection), so the field was dead data - notably
   NeoWave's `stWaveSupport`. Rows express their engine through `is_tree` plus `support_style` and
   `overrides`; the header now says so.

## Not changed on purpose

The painted pass still runs manual. Passing the row's `stNormalAuto` / `stWaveSupport` would
re-enable automatic overhang detection for that pass and generate support outside its painted
facets.

## Woven strips that are simply not possible

Independent of painting: a layer is woven only when it is a `TopInterface` layer whose stack has a
reserved contact (`SupportCommon.cpp:1923-1955`), and the strip region is inset by one interface
line width before `split_weave_strips()` (`:2377`), which needs an even `n >= 2`
(`:1636-1654`). On a narrow organic region at a 2 mm pitch the strips collapse and the layer
silently keeps the generic fill. Test a flat cantilever (>= 60 mm span, 3 interface layers) to tell
a geometry limit from a bug.

## Verification

- Build: `cmake --build build --config Release --target fff_print_tests` -> 0 errors.
- Tests: `ctest --test-dir build/tests/fff_print -C Release` -> **603/603 passed**, including the new
  `[SupportMaterial]` case "Merging duplicate support layers keeps the later pass's layer treatment"
  (weave/contact flags reached, transition taken over, two joints keep one and cover both
  footprints, an untagged pass leaves the base alone).
- Runtime (pending): on a flat cantilever with weave + all three transition groups on, slice once
  unpainted and once auto-painted; the transition temperature delta / fan marker must appear in
  both, and a woven layer must emit base and interface roles at one speed.

## Files

- New: (none)
- Modified: `src/libslic3r/Support/SupportCommon.hpp`, `src/libslic3r/Support/SupportCommon.cpp`,
  `src/libslic3r/PrintObject.cpp`, `src/libslic3r/OrcaExt/SupportPaintTypes.hpp`,
  `src/libslic3r/OrcaExt/SupportPaintTypes.cpp`, `tests/fff_print/test_support_material.cpp`
- Patch: `porting/patches/84_PF-10-paint-marks.patch`
