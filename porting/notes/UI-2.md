# UI-2 - Gizmo panel screenshot MCP tool (`ui_screenshot`)

Built because the panel-style work (docs/handoff.md section 28) cannot be verified any other way.

## Why it has to exist

A gizmo panel is ImGui drawn into the GL canvas. Nothing else can look at it:

- `render_plate_view` and the other 3D renderers draw the *scene*, not the overlay;
- the window geometry knows nothing about an ImGui window;
- reading the framebuffer from the MCP thread is unreliable (the back buffer is undefined between
  frames, and the plates render into an off-screen FBO for exactly that reason).

So the only route is a screen capture of the window, cropped to the panel.

## How it works

1. Optionally opens the named gizmo (`gizmo=perforation|holes|edge_dress|hole_walls|support_zones|
   align_stack|extrude|cut|seam|...`), selecting the object first if one is given.
2. **Drives the canvas until the panel's rect settles.** Two traps here, either of which alone makes
   a one-pass capture useless:
   - ImGui needs *two* passes before an auto-sized window has its real size. `GLGizmoBase::
     render_input_window` says so itself and forces a second frame. A single pass reported every
     just-opened panel as one row tall.
   - The canvas only paints from the app's idle cycle, which cannot run while the call is inside the
     event handler. `wxYield()` therefore waits forever for a frame that never comes. The tool calls
     `GLCanvas3D::render()` directly - the paint handler's own entry point, recursion-guarded - and it
     ends in `SwapBuffers()`, so the frame is also on screen for the blit.
3. Blits the window's screen rect with `wxScreenDC` (physical pixels; a DPI-unaware capture gives a
   1.25x-scaled crop and silently hides one side of the window - which is how this tool got written).
4. Crops to `GLGizmoBase::last_input_window_rect()` (recorded every frame, in ImGui pixels, which Orca
   sets to the canvas's physical size, so the panel's offset inside the canvas *is* a pixel offset and
   only the canvas's screen origin has to be added) and upscales by `scale` (default 2).

`region=window` skips the crop - for when *where* the panel sits is the question (anchoring, clipping,
overlap). `save_to_file=true` (default) writes a PNG and returns its path.

## What it found

- the label column was measured from the window's left edge while `SameLine()` wants a window-relative
  offset, so every panel's widest caption had its tail covered by the widget ("Hole width" -> "Hole
  wid"). This was in the *shared* row helpers and in `render_combo`, which the stock Cut and Assembly
  panels also use;
- the right-aligned primary button was drawn with `SameLine()` over the second line of its own wrapped
  hint (both the HF-3 reference and Align & Stack);
- the honeycomb glyph's cell wall was 0.4 px, so the comb stopped reading as cells at 28 px while not
  one coordinate had changed - an SVG mockup antialiases that gap, `ImDrawList` does not.
