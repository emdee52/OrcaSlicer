# MT-4 - MMU painter Pro Mode (F1-F3)

- Source: `NEOTKOCM_RELEASE_2_36.md` (F1-F4); ungated from Libre Mode in 2.3.6.
- Fork: `OrcaFS-NeotkoCM` @ `d26e3c7b0e` (F1/F2/F3), `eeb0e3505f` (F4).
- Category: B. Status: **ported** (F1 + F2 + F3). F4 dropped by the user.

## Ported
All inserted code is tagged `[ORCAPORT:MT-4]`.

- **F1 - Paint perimeters only.** "Pro Mode" section in the MMU paint gizmo; checkbox sets the
  object's existing `mmu_segmented_region_max_width` to `wall_loops * line_width` (or clears
  it). No new key.
- **F2 - Extra walls in the painted zone.** New object option
  `mmu_segmented_region_extra_walls` (`coInt`, 0-8, default 0, Advanced). In
  `PrintApply::generate_print_object_regions`, the painted region's `wall_loops` is raised by
  the option before the region is created, so only painted (multi-material) regions get the
  extra walls. UI: an "Extra walls" input in the Pro Mode section, plus a line in
  Multimaterial > Advanced.
- **F3 - Brush precision.** Slider 1x-8x backed by `TriangleSelector::set_precision_factor()`;
  `select_patch()` scales its triangle edge limit by the factor (1 = stock). Applied to the
  selector right before each paint call.

## Files
`TriangleSelector.hpp/.cpp`, `GLGizmoPainterBase.hpp/.cpp`, `GLGizmoMmuSegmentation.cpp`,
`PrintConfig.cpp/.hpp`, `Preset.cpp`, `PrintObject.cpp`, `PrintApply.cpp`, `Tab.cpp`.

## Dropped
- **F4 - Rectangle / polygon masks.** User opted out (projection cursors, camera
  view*projection wiring, overlay input). Not ported.

## Out of scope
- 2_38 "Per color" and "Surface depth" (`mmu_segmented_region_surface_depth`, `..._per_color`).

## Gates
- The fork already ungated Pro Mode from Libre Mode; nothing to remove.

## Verification
- Compile/link: 0 errors, binary produced.
- F1: checkbox set/clear = object key present/absent; off = stock.
- F2: 0 = no change (byte-identical); >0 raises `wall_loops` only on painted regions.
- F3: factor 1 = stock; higher subdivides the patch more finely at the stroke edge.
- Pending: a runtime slice showing a painted region printing with extra walls while the rest
  of the object keeps its `wall_loops`.
