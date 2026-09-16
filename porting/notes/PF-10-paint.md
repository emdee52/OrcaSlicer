# PF-10-paint - extensible per-style support painting gizmo

- Source: preFlight v1.3.0 (`github.com/oozebot/preFlight`), fork sha `f74dc69`; NeoWave from the
  Neotko SU-4 port (`NEOTKOCM_RELEASE_2_36.md`, fork sha `e25039606d`).
- Branch: `port/PF-10-paint`, based on `port/integration` @ `77c012654d`.
- Category: B/D (re-implementation on Orca's paint/enforcer pipeline). Status: **ported including
  multi-pass composition, build clean; runtime pending.**

## Intent

preFlight's support paint gizmo offers four enforcer styles (Snug/Grid/Organic/Baobab) so one object
can carry different support types in different regions, and a multi-pass dispatcher builds each
region with its native engine. Orca's paintable support states are only the generic
`ENFORCER`/`BLOCKER` pair (`TriangleSelector.hpp`), and support is single-pass
(`PrintObject::_generate_support_material`).

This branch ports the *extensible* gizmo and the slicing plumbing, and adds **NeoWave** (our SU-4
support type) as a paintable type with a hardcoded recipe. Baobab is added in `PF-10` once its
engine exists.

## Backward-compatibility design (important)

Orca keeps support painting (`ModelVolume::supported_facets`) and MMU painting
(`ModelVolume::mmu_segmentation_facets`) in **separate** `FacetsAnnotation` objects, so support
states 3+ do not collide with the MMU extruder states. Values 3..17 already serialise in the
existing `TriangleSelector` bitstream (`TriangleSelector.cpp:1745-1768`), so **no format change**.

Crucially, the legacy meaning of `ENFORCER`/`BLOCKER` is **preserved**: they still mean "generic
enforcer / blocker using the object's own support style". Existing painted projects therefore slice
identically. Explicit styles use new states:

| State | Meaning |
|-------|---------|
| `ENFORCER` (1) | legacy generic enforcer - object's own style (unchanged) |
| `BLOCKER` (2) | blocker (unchanged) |
| 3 | Snug |
| 4 | Grid |
| 5 | Organic |
| 6 | NeoWave |
| (7) | Baobab - added in PF-10 |

## What was ported

- **`src/libslic3r/OrcaExt/SupportPaintTypes.{hpp,cpp}`** (new, `[ORCAPORT FILE]`): the single
  extension point. A data-only `SupportPaintType` table row carries the paint state, label, colour,
  engine selector (`is_tree`, `support_type`, `support_style`) and a typed `SupportPaintOverrides`
  block. New support types only need a new row; the gizmo and dispatcher iterate the table. Also
  exposes `support_paint_types()`, `support_paint_type(state)`, `has_painted_support_style()`,
  `has_painted_support_styles()`.
- **NeoWave recipe** (hardcoded in the painter's row): `support_type = stWaveSupport`,
  base = `Hollow` (`smpNone`), interface = `Wave` (`smipWave`), roof shape = `Wave` (`smwrpWave`),
  roof order = `Smart` (`smwroSmart`), wall loops = `1`.
- **Gizmo** (`GLGizmoFdmSupports.{hpp,cpp}`): registry-driven "Support type" radio row; the left
  button paints the selected state (`get_left_button_state_type()`), the right button still paints a
  blocker; per-state colour table so each painted type renders distinctly.
- **Projection into support** (`PrintObject` transient `painted_support_state`, consumed in
  `SupportMaterial.cpp` `SupportAnnotations`, `TreeSupport.cpp` `detect_overhangs` and
  `TreeSupport3D.cpp` `generate_support_areas`): facets painted with the active support style are
  projected as enforcers, so support is generated under them.
- **Dispatcher** (`PrintObject::_generate_support_material`): when a registered style is painted,
  the object config is scoped-overridden to that style's engine and forced options for the support
  pass, then restored. Nothing painted => the legacy path runs byte-identically.

## Multi-pass composition (PF-10-paint-b)

The dispatcher (`PrintObject::_generate_support_material`) is **region-limited**: it runs one pass
per painted type plus a default pass, then merges them.

- **Default pass**: the object's own support type/style (auto) over the unpainted overhangs and the
  legacy generic enforcer, with every explicitly painted state projected as a blocker.
- **One enforcer-only pass per painted type**: classic styles use `stNormal` (manual, so no auto
  overhangs) and tree styles use `stTree`; each projects only its own state as an enforcer and
  blocks the other painted states plus the legacy `ENFORCER` (owned by the default pass). NeoWave
  runs as a manual Normal pass with hollow base, wave interface, wave roof, smart order and one
  wall loop.
- **Wave decoupling**: the NeoWave roof/hollow-body triggers now key on the `smipWave` interface
  pattern and `wavesupport_wall_loops` instead of on `support_type == stWaveSupport`, so a manual
  per-region NeoWave pass gets the wave too.
- **Composition plumbing**:
  1. `TreeSupport::detect_overhangs` no longer clears unconditionally; the dispatcher owns the
     clear and sets `PrintObject::support_pass_appends()` for every pass after the first.
  2. `generate_support_layers` continues the support-layer id sequence when appending.
  3. `PrintObjectSupportMaterial::generate` and `TreeSupport3D::generate_support_areas` toolpath
     only the layers they just created, so earlier passes are not re-toolpathed.
  4. `PrintObject::merge_duplicate_support_layers` (ported from preFlight, with
     `clip_extrusion_entities`) merges same-`print_z` layers: fills clipped against the base
     islands, islands unioned, ids reassigned.
  5. **Each pass runs against an EMPTY support-layer set.** Both engines assume
     `support_layer_count() == 0` while generating: `PrintObject::total_layer_count()` is
     `layer_count() + support_layer_count()` (`Print.hpp:419`) and the classic descent walks
     `*object.get_layer(i)` up to `total_layer_count()-2` (`SupportMaterial.cpp`). With a second
     classic pass the earlier layers made that index run past `m_layers` -> ACCESS_VIOLATION.
     The dispatcher now moves the existing layers aside, runs the pass, then composes
     (prior + fresh) and merges.

### Gizmo defaults / settings

- The overhang "Highlight overhangs" slider defaults to **30 deg** when the gizmo opens.
- `support_on_build_plate_only` is honored by painted per-region passes: their enforcers are
  trimmed by the build-plate-covered mask too (the legacy generic enforcer keeps its historical
  exemption; `SupportAnnotations::painted_pass`).
- Every other process/object/region setting is inherited by each pass unchanged; the dispatcher
  only overrides `support_type`/`support_style` per pass and the NeoWave recipe (hollow base,
  wave interface/roof, smart order, one wall loop). Painted passes are manual (`stNormal`/`stTree`),
  so automatic-overhang settings (`support_threshold_angle`, sharp-tail/critical-region filters)
  act on the default/auto pass, not on painted facets.

Known gap: with **raft layers** enabled, each pass may add its own raft (merged by z); multi-pass
with a raft is untested. `Default` painting is the legacy generic enforcer and keeps the object's own
style, so it is the recommended way to "leave this region to the object default".

## Automatic painting

A support-region auto-painter (preFlight's disabled "Automatic painting" button, reimplemented
Orca-native) is tracked separately as `PF-10-auto`.

## Gizmo layout / QOL (follow-up)

- Layout: the tool's own controls (brush size, smart fill angle, gap area) sit directly under the
  tool selector. Dividers separate the sections; each section owns its controls:
  tool settings | support-type palette (+ "Remap support types") | automatic painting (button
  "Auto paint", per-type checkboxes, "Min overhang area") | highlight ("Highlight overhangs" +
  "On highlighted overhangs only") | section view. The support painter opens with
  "On highlighted overhangs only" checked.
- Each support-type radio shows a colour swatch of its prepare-view colour, sized to the text line
  so it is centred with the radio.
- "Remap support types": a collapsible From/To pair (registry types + Blocker) that rewrites every
  facet painted with one state to another, via `TriangleSelector::remap_triangle_state`, in one undo
  step. Mirrors the colour-painting gizmo's "Remap filaments" but for support states.

## Behavior-neutral at defaults

No style facets painted => `paint == nullptr` => the original single-pass path, byte-identical.
New states are inert unless painted. No enum reorder, no new config keys in this branch.

## Verification

- Build: `build_win.bat -s -j 8` (new files require configure) -> 0 errors; then
  `build_win.bat -s --no-configure -j 8` -> 0 errors; `OrcaSlicer.dll` linked
  2026-09-15 16:xx.
- Manual (pending): paint Organic on a region -> tree supports under it; paint NeoWave -> hollow/wave
  body with a wave roof; paint Grid/Snug -> the base/interface change; blocker still excludes; erase
  returns to stock; off (no paint) slice is byte-identical to stock.

## Files

- New: `src/libslic3r/OrcaExt/SupportPaintTypes.{hpp,cpp}`
- Modified: `src/libslic3r/CMakeLists.txt`, `src/libslic3r/Print.hpp`,
  `src/libslic3r/PrintObject.cpp`, `src/libslic3r/Support/SupportMaterial.cpp`,
  `src/libslic3r/Support/TreeSupport.cpp`, `src/libslic3r/Support/TreeSupport3D.cpp`,
  `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.{hpp,cpp}`
- Patch: `porting/patches/19_PF-10-paint.patch`
