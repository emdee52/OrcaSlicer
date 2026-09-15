# PF-6 - Preview Clipping Plane

Source: preFlight v1.3.0 (`github.com/oozebot/preFlight`), fork sha `f74dc69`.
Files: `src/slic3r/GUI/preFlight.PreviewClipController.{hpp,cpp}` (~220 lines), plus
`src/libvgcode` shader/API changes, plus `Plater.cpp` preview right-click menu.

## Intent

Right-click an object in the G-code Preview to activate an interactive clipping plane that
cuts through **both** the toolpaths and the model shells, with a filled cap at the cut face,
so the internal structure (walls, infill, skins) can be inspected. A slider moves the plane
across the object's bounding box along the camera-forward direction; "Reset Direction"
re-captures the camera forward; "Close" restores normal preview.

Category D (preFlight is a PrusaSlicer fork). GUI-only: no config keys, no slicing change,
inert unless the user invokes it.

## Why it touches the vendored `src/libvgcode`

Orca renders Preview in two independent paths:
- **Toolpaths** - `libvgcode::Viewer` (Vulkan/GL, its own shaders).
- **Shells** - Orca's `GLVolumeCollection` via `gouraud_light`.

Orca's `src/libvgcode` had **no clipping support at all** (preFlight patched its own copy).
So the port required, in `src/libvgcode` (git-tracked, not `deps/`, all edits marked
`[ORCAPORT:PF-6]`):

| File | Change |
|------|--------|
| `include/Viewer.hpp` | public `set_clipping_plane(nx,ny,nz,offset)` / `reset_clipping_plane()` |
| `src/Viewer.cpp` | forward to `m_impl` |
| `src/ViewerImpl.hpp` | impl methods, `std::array<float,4> m_clipping_plane`, two uniform ids, `<limits>` |
| `src/ViewerImpl.cpp` | look up `"clipping_plane"` in the segments + options shaders and include in the init assert; upload per draw; impl set/reset |
| `src/Shaders.hpp` | segments + options: `clipping_plane` uniform, `clipping_dist` varying, `discard` when `< 0`, and **cross-section cap projection** in the segment vertex shader |
| `src/SegmentTemplate.cpp` | `VERTEX_DATA` 24 -> 36 indices: 4 cap triangles using vertex ids 8..15 |

The port **re-implemented** the shader changes against Orca's drifted libvgcode (Orca's
segments shader carries an extra `bias` from `hwa.w` and per-platform option scaling that
preFlight's does not); the cap code is preFlight's logic, but `vertex_id` was substituted
with `eff_id` only where Orca still used `vertex_id`.

Shells use a new clipped shader variant `gouraud_light_clip` (both `110/` and `140/`),
registered in `GLShadersManager.cpp`; `GCodeViewer::render_shells` selects it and sets the
shell collection's clipping plane when a plane is active.

## Orca-side wiring

- `src/slic3r/GUI/OrcaExt/PreviewClipController.{hpp,cpp}` - `Slic3r::OrcaExt::Gui::PreviewClipController`,
  ported from preFlight, re-based on Orca APIs. New file stamp `[ORCAPORT FILE]`.
- `src/slic3r/GUI/GCodeViewer.{hpp,cpp}` - owns the controller + `m_preview_clipping_plane`;
  accessors `get_shells_volumes` / `are_shells_visible` / `set_shells_visible` /
  `get_libvgcode_viewer` / `set_preview_clipping_plane` / `reset_preview_clipping_plane`;
  `render_shells` uses `gouraud_light_clip` when active; `render` calls `render_imgui()`;
  `reset()` deactivates the controller (kills the "plane persists after re-slice" bug).
- `src/slic3r/GUI/GLCanvas3D.hpp` - `get_first_hover_object_id()` (hovered volume -> model
  object id).
- `src/slic3r/GUI/Plater.cpp` - the Preview canvas did **not** bind
  `EVT_GLCANVAS_RIGHT_CLICK`; the port adds `on_preview_right_click` and a small local menu
  with "Clipping Plane" that activates the controller for the object under the cursor. This
  differs from preFlight's rewritten native/custom menu (Orca's preview had no right-click
  menu at all), but is the minimal Orca-idiomatic hook.
  - The object under the cursor is found by `PreviewClipController::pick_object`, which
    raycasts the preview **shell** volumes (`GLVolume::mesh_raycaster`) and returns the
    nearest hit's `composite_id.object_id`. This is required because the Preview canvas has
    `picking` disabled (`Preview::init` never calls `enable_picking(true)`), so
    `m_hover_volume_idxs`/`GLCanvas3D` picking is always empty in Preview, and
    `wxGetApp().is_gcode_viewer()` is only true in the standalone G-code viewer app mode, not
    the editor's Preview tab.
  - **Primary entry point is a button, not the context menu.** Right-click turned out not to
    reach the preview canvas reliably, so `GCodeViewer::render` draws an always-visible
    "Clipping Plane" button (top-center ImGui window) in Preview. It activates clipping for
    the selected object, else the first object that has shells. The right-click handler is
    kept but is not the main path.
- `src/slic3r/CMakeLists.txt` - the two new controller files.

## Anchors (for a future upstream rebase)

- `src/libvgcode/src/Shaders.hpp :: Segments_Vertex_Shader` - uniform, `eff_id` cap map, cap projection.
- `src/libvgcode/src/Shaders.hpp :: Segments_Options_*_Shader` - uniform + discard.
- `src/libvgcode/src/SegmentTemplate.cpp :: VERTEX_DATA` - cap triangles.
- `src/libvgcode/src/ViewerImpl.cpp :: init()` / `render_segments` / `render_options` - uniforms.
- `src/slic3r/GUI/GCodeViewer.cpp :: render_shells` (shader pick + shell clip), `render` (overlay), `reset` (deactivate).
- `src/slic3r/GUI/Plater.cpp :: priv` - preview right-click binding + `on_preview_right_click`.

## Verification

Build clean (`build_win.bat -s`). Manual protocol:
1. Slice a hollow part with sparse/solid infill. Switch to Preview.
2. Right-click an object -> "Clipping Plane". A slider window appears.
3. The slider cuts **toolpaths and shell** with a filled cap; markers cut too.
4. "Reset Direction" re-captures the camera forward. "Close" restores the preview.
5. Re-slice -> the plane is gone (controller deactivated by `GCodeViewer::reset`).
6. Never invoking it leaves Preview byte-identical (no config keys, no slicing changes).

## Known limitations / decisions

- **ES shaders not patched** (`ShadersES.hpp`): matches preFlight, which also only patched the
  desktop shaders. `ENABLE_OPENGL_ES` builds get toolpath clipping without the cap shader path.
  No Windows impact.
- **Right-click entry is a small local menu**, not Orca's cached `MenuFactory` menus, because
  Orca's Preview canvas had no right-click menu binding. The entry only appears for the hovered
  object; it does not change any other context menu.
- Shell visibility save/restore mirrors preFlight; `activate` does not force-isolate the object
  (preFlight's comment says "isolates" but its code only saves/restores visibility).
- Vendored-lib edits mean a future `libvgcode` refresh will conflict here; all are marked
  `[ORCAPORT:PF-6]`.
