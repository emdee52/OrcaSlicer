# MT-4 - MMU painter Pro Mode (F1-F4)

- Source: `NEOTKOCM_RELEASE_2_36.md` (F1-F4); ungated from Libre Mode in 2.3.6.
- Fork: `OrcaFS-NeotkoCM` @ `d26e3c7b0e` (F1/F2/F3), `eeb0e3505f` (F4).
- Category: B (F1/F3), D (F2/F4). Status: **in-progress** - F1 and F3 ported.

## Ported (branch `port/MT-4`, build clean)
- **F1 - Paint perimeters only.** A "Pro Mode" collapsible section in the MMU paint gizmo with
  a checkbox that sets the object's existing `mmu_segmented_region_max_width` to
  `wall_loops * line_width` (or erases it). No new key.
- **F3 - Brush precision.** Slider 1x-8x that calls a new
  `TriangleSelector::set_precision_factor()`; `select_patch()` scales its triangle edge limit
  by the factor (1 = stock). Set on the selector right before each paint call in
  `GLGizmoPainterBase`.
- Files: `TriangleSelector.hpp/.cpp`, `GLGizmoPainterBase.hpp/.cpp`, `GLGizmoMmuSegmentation.cpp`.

## Still pending
- **F2 - Extra walls in the painted zone.** Needs a new region/object option
  (`mmu_segmented_region_extra_walls`) and wiring in `PrintApply::generate_print_object_regions`
  so the painted region's `wall_loops` is increased. Not yet done.
- **F4 - Rectangle / polygon masks.** Needs new `ToolType`s and projection cursors
  (`RectangleProjectionCursor`, `PolygonProjectionCursor`) plus camera view/projection wiring
  and overlay input handling in `GLGizmoPainterBase`. Largest sub-feature; not yet done.

## Out of scope (deliberately)
- The 2_38 "Per color" and "Surface depth" additions (`mmu_segmented_region_surface_depth`,
  `..._per_color`) are a separate commit and are not part of MT-4.

## Gates
- The fork already ungated Pro Mode from Libre Mode; nothing to remove.

## Verification
- Compile/link: `build_win.bat -s --no-configure -j 8` -> 0 errors, binary produced.
- F1: checkbox sets/clears `mmu_segmented_region_max_width` on the object; slice shows the
  painted color limited to a ring. Off = no key = stock.
- F3: factor 1 = stock (edge limit unchanged by construction); higher values subdivide the
  patch more finely at the stroke edge.
