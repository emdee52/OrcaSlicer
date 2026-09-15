# PF-2 - Counterbore smart bridging (global + gizmo)

- Source: `preFlight` v1.3.0 (`github.com/oozebot/preFlight`), `GLGizmoCounterboreBridge.*`,
  `apply_counterbore_bridge_geometry()` (`PrintObjectSlice.cpp:622`), `Fill.cpp` counterbore
  angle override, key `counterbore_bridge_layers`.
- Category: D (Bambu/Orca slicing pipeline differs). Split into **PF-2a (global option)** and
  **PF-2b (painting gizmo)**.
- Status: **PF-2a ported** (branch `port/PF-2a`); PF-2b planned.

## What preFlight does (verified)

"Smart bridging": for a counterbore hole it takes `bore` (large hole on layer L) and `shaft`
(small hole on L+1), `ring = bore - shaft`, then builds **N corridors**, each the convex hull of
the shaft shifted +/-2x shaft extent perpendicular to an angle rotating `step*pi/N`. Cumulative
intersection shrinks the un-bridged remainder; `bridge_material = ring - remainder` grows each
layer, so the ring is closed in N steps. Each step stores `(bridge_material, angle+pi/2)` in
`Layer::counterbore_bridge_regions`; `Fill.cpp` copies the per-hole angle into the fill so every
hole's bridge runs along its own corridor direction. Material is written into `layer.lslices`
and `LayerRegion` slices. `counterbore_bridge_layers` (int 2-9, default 2) is the step count.

Painting: `GLGizmoCounterboreBridge : GLGizmoPainterBase` (`PainterGizmoType::COUNTERBORE_BRIDGE`)
stores facets in a dedicated per-volume `ModelVolume::counterbore_bridge_facets` annotation; the
triangle state encodes the step count (2-9). Painted facets -> `Layer::counterbore_bridge_painted_areas`
(per layer, grouped by count) -> the same stepping pass. Persisted to 3mf; invalidated by
`model_counterbore_bridge_data_changed()`.

## Orca gap

Orca already has a **global** `counterbore_hole_bridging` enum `{none, partiallybridge,
sacrificiallayer}` implemented in `PerimeterGenerator::process_no_bridge` (SuperSlicer
`BridgeDetector`-based). It is a *different* algorithm (bridges from the infill boundary, not a
stepped corridor). Orca has no `counterbore_bridge_layers`, no `Layer::counterbore_bridge_regions`,
no counterbore fill-angle override, no counterbore painter, no `counterbore_bridge_facets`.

Orca pieces to build on: `FacetsAnnotation`, `GLGizmoPainterBase`, `PainterGizmoType`,
`GLGizmosManager::EType`, `LayerRegion::slices` (public `SurfaceCollection`), `Layer::lslices`,
`PrintObject::slice` (`PrintObjectSlice.cpp:825`), `FillBase::_infill_direction` (uses
`surface->bridge_angle`).

## PF-2a - global option

1. `PrintConfig`: append `chbSmart` to `CounterboreHoleBridgingOption` ("smartbridge", label
   "Smart bridging", last). Add `counterbore_bridge_layers` (coInt 2-9, default 2) in
   `PrintRegionConfig` next to `counterbore_hole_bridging`. `Preset.cpp` whitelist.
2. `Layer.hpp`: `std::vector<std::pair<ExPolygons, double>> counterbore_bridge_regions`.
3. `FillBase`: `float counterbore_fill_angle{-1.f}`; use it first in `_infill_direction`;
   `Fill.cpp` sets it per surface by intersecting `layer.counterbore_bridge_regions`.
4. `PrintObjectSlice.cpp`: port `apply_counterbore_bridge_geometry(PrintObject&)`; call at the end
   of `PrintObject::slice()` when `counterbore_hole_bridging == chbSmart`. Port it in a new
   module (`OrcaExt/CounterboreBridge.{hpp,cpp}`) or as a marked free function; `LayerRegion::slices`
   is public so no friendship is needed.
5. **Auto-detection (new; preFlight was paint-only):** for each layer L, detect a counterbore as a
   hole present at L that differs from the matching hole at neighbouring layers (a bore/shaft
   step), then step-bridge the ring over `counterbore_bridge_layers` layers. Detection rule is
   the open question below.

## PF-2b - painting gizmo (per-region choice)

- Per-volume `ModelVolume::counterbore_bridge_facets` (`FacetsAnnotation`) + `ModelObject::
  is_counterbore_bridge_painted()`, Model copy/ctor/reset/id, 3mf read/write, invalidation.
- `GLGizmoCounterboreBridge : GLGizmoPainterBase` (`PainterGizmoType::COUNTERBORE_BRIDGE`,
  `EType::CounterboreBridge`), toolbar icon, registration; slider 2-9.
- **Per-user request:** the gizmo lets you paint **either** full smart bridging (preFlight) **or**
  Orca's existing partial bridging, because small horizontal holes' top sections bridge better
  partially than with a full stepped close. Implementation: a per-painted-region mode selector
  stored in the annotation (or a second facet state) that routes the region's counterbore to the
  smart stepping pass or to Orca's `chbBridges` path.

## Open question (must resolve before PF-2a)

PreFlight's convention is `bore` = large hole on the **painted layer L**, `shaft` = small hole on
**L+1**, with the transition extending **upward** from L. That is a hole that *shrinks* going up.
Confirm the geometry/direction of the user's counterbore and define the auto-detection rule:
for each layer L, is it "hole at L larger than at L+1" (preFlight) or the inverse? Also pick a
minimum ring width / overlap threshold to avoid flagging tapered holes.

## PF-2a port (done)

- `CounterboreHoleBridgingOption` += `chbSmart` (serialized `smartbridge`, label "Smart
  bridging"), appended last. `counterbore_bridge_layers` (coInt 2-9, default 2) in
  `PrintRegionConfig` next to it; `Preset.cpp` whitelist.
- `Layer::counterbore_bridge_regions` = `vector<pair<ExPolygons, double>>` (bridge material,
  fill angle).
- `FillBase::counterbore_fill_angle` (< 0 = not a counterbore bridge). `FillBase::_infill_direction`
  uses it ahead of `bridge_angle`; `Layer::make_fills` sets it per surface by intersecting the
  surface with `counterbore_bridge_regions`.
- `PrintObjectSlice.cpp`: `apply_counterbore_bridge_geometry(PrintObject&)` ports preFlight's
  stepping (rotating-angle convex-hull corridors, cumulative intersection, `ring - remainder`),
  with the guards below. Called from `PrintObject::slice()` after `groupingVolumesForBrim`,
  before the bbox/backup pass, when `counterbore_hole_bridging == chbSmart`.
- **Auto-detection (new):** for each layer L (>=1), for each hole, find smaller holes on L+1
  nested inside it (area < 0.9x, centroid inside). bore = L's hole, shaft = the nested hole;
  `ring = bore - shaft`. Guards: equivalent-diameter difference >= 0.6 mm, steps capped by
  layers available, layer 0 skipped.
- Off by default (`none`): byte-identical.

### Build

`build_win.bat -s --no-configure -j 8` -> 0 errors; `PrintObjectSlice.cpp`, `Fill.cpp`,
`FillBase.cpp`, `PrintConfig.cpp` compiled, `OrcaSlicer.dll` linked 2026-09-15 08:35.

### Verification

- Manual (pending): counterbore with a wide void under material; `Smart bridging` + N -> the ring
  closes over N layers with a rotating bridge direction; `None` -> unchanged.

## Files (Orca)

- `src/libslic3r/PrintConfig.{hpp,cpp}`, `Preset.cpp`, `Layer.hpp`,
  `src/libslic3r/PrintObjectSlice.cpp`, `PrintObject.cpp`, `Fill/Fill.cpp`, `Fill/FillBase.{hpp,cpp}`,
  `Model.hpp/.cpp`, `Format/3mf.cpp`, `src/slic3r/GUI/Gizmos/GLGizmoCounterboreBridge.{hpp,cpp}`,
  `GLGizmoPainterBase.hpp`, `GLGizmosManager.{hpp,cpp}`, `src/slic3r/CMakeLists.txt`, toolbar SVG.

## PF-2b port (done)

- ModelVolume::counterbore_bridge_facets (FacetsAnnotation), is_counterbore_bridge_painted(), model_counterbore_bridge_data_changed(), 3mf attribute slic3rpe:counterbore_bridge, PrintApply invalidation.
- GLGizmoCounterboreBridge : GLGizmoPainterBase (PainterGizmoType::COUNTERBORE_BRIDGE, EType::CounterboreBridge, Ctrl+K, Smart/Partial choice). The triangle state selects the mode: ENFORCER = smart, BLOCKER = partial (the counterbore annotation has its own TriangleSelector).
- Slicing: pply_counterbore_bridge_geometry() projects the painted triangles to per-layer areas and only bridges painted holes (plus auto-detection when counterbore_hole_bridging == smartbridge). Painted smart uses the stepped ramp (N = counterbore_bridge_layers); painted partial uses a single bridge layer.
- Build: uild_win.bat -s -j 8 -> 0 errors; GLGizmoCounterboreBridge.cpp, GLGizmosManager.cpp, PrintObjectSlice.cpp compiled, DLL 2026-09-15 09:32. Patch 16_PF-2b.patch.
- Known gaps: painted partial is a single-layer bridge (not Orca's chbBridges coverage algorithm); auto-detection convention is preFlight's (hole shrinks upward).
