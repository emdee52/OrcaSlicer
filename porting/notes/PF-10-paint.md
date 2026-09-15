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

The dispatcher (`PrintObject::_generate_support_material`) now runs at most one classic pass plus
one tree pass and merges them:

- **Classic pass** (object default + painted Snug/Grid, or a sole painted NeoWave): the single
  classic style is forced on the object config; other painted tree styles are projected as
  blockers. Several classic styles in one object fall back to the object's own style (Orca's
  classic style is object-level) - a documented limitation.
- **Tree pass** (painted Organic/Baobab): runs **enforcer-only** (`support_type = stTree`) when it
  composes with the classic pass, so auto overhangs are not built twice; classic facets are
  projected as blockers.
- **Composition plumbing** (the blockers found during recon):
  1. `TreeSupport::detect_overhangs` no longer clears unconditionally; the dispatcher owns the
     clear and sets `PrintObject::support_pass_appends()` for a composing tree pass.
  2. `generate_support_layers` continues the support-layer id sequence when appending instead of
     restarting at 0.
  3. `TreeSupport3D::generate_support_areas` toolpaths only the layers it just created, so the
     classic layers are not re-toolpathed.
  4. `PrintObject::merge_duplicate_support_layers` (ported from preFlight, with
     `clip_extrusion_entities`) merges same-`print_z` layers: fills are clipped against the base
     islands, islands unioned, ids reassigned.

Still deferred: mixing **NeoWave with Snug/Grid** in one object needs a second classic pass, which
`generate_support_layers`/`generate_support_toolpaths` do not support without a wider refactor.

## Automatic painting

A support-region auto-painter (preFlight's disabled "Automatic painting" button, reimplemented
Orca-native) is tracked separately as `PF-10-auto`.

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
