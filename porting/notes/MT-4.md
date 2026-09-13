# MT-4 - MMU painter Pro Mode (F1-F4)

- Source: `NEOTKOCM_RELEASE_2_36.md` (F1-F4 shipped); ungated from Libre Mode in 2.3.6.
- Fork: `OrcaFS-NeotkoCM` @ `d26e3c7b0e` (F1/F2/F3), `eeb0e3505f` (F4).
- Category: B. Target has `mmu_segmented_region_max_width` but no extra_walls/precision/rect/polygon.

## Scope decision
- MT-4 is **F1-F4 only**.
- The 2_38 additions **Per color** and **Surface depth** (`f1d6650766`; keys `mmu_segmented_region_surface_depth`, `..._extra_walls_per_color`, `..._surface_depth_per_color`, plus `PrintObject::apply_painted_surface_depth`) are a separate commit and are **out of scope** unless the user asks.

## Files / anchors
- `src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.cpp/.hpp`
  - `render_pro_mode_section(...)` (~1470-1719); panel header (~1146); tool override (~1153-1155)
  - F1 checkbox (~1549) toggles `mmu_segmented_region_max_width`; F2 input (~1580) `mmu_segmented_region_extra_walls`; F3 slider (~1479) `m_precision_factor`
- `src/slic3r/GUI/Gizmos/GLGizmoPainterBase.cpp/.hpp`
  - `ToolType::RECTANGLE`/`POLYGON`; rectangle/polygon cursors and overlays; `apply_rectangle_mask`/`apply_polygon_mask`; precision members (min 1, max 8)
- `src/libslic3r/TriangleSelector.cpp/.hpp` - precision scaling, `RectangleProjectionCursor`, `PolygonProjectionCursor`
- `src/libslic3r/PrintApply.cpp` (~991-995, 1113-1121) - extra args into region generation (F2)
- `PrintConfig.cpp` (~4065), `PrintConfig.hpp` (~995), `Preset.cpp` (~991), `PrintObject.cpp` (~1139), `Tab.cpp` (~7954)

## Keys
- `mmu_segmented_region_extra_walls` | `coInt` | `0` (0-8) | `PrintConfig.cpp:4065`
- `mmu_segmented_region_max_width` | `coFloat` | `0` | existing upstream (reused by F1)

## Gates to remove
- None remain; the fork already made Pro Mode always visible (comment "Ungated from LibreMode (s201)").

## Coupling / blockers
- No ColorMix/ColorStitch dependency; ignore the ColorStitch includes in the same file.
- `GLGizmoPainterBase`/`TriangleSelector` are shared with Seam/FuzzySkin/Support/TextureBump: keep defaults (`m_precision_factor=1.f`) so other tools are unchanged.
- F2 threads two args through `PrintApply::generate_print_object_regions`, a shared call path.
- Target's `CursorType`/`cursor_factory` lacks the projection cursors; porting F4 requires wiring the camera view*projection matrix.

## Verification
- Each tool off = stock paint; F1 restricts to a ring; F2 adds walls sized with F1 in mind; F3 only changes subdivision; F4 paints the enclosed area on release; undo/redo intact.

## Open questions
- Precision clamp mismatch: fork clamps to 16 in TriangleSelector but UI max 8. Choose one.
- F4 front-facing / winding correctness: verify against target's cursor API.
