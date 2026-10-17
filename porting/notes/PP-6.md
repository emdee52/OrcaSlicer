# PP-6 — Perforations: Lines up to 90°, and a preview that follows the sliders

Status: **implemented, build clean, tests green** (ctest 2411/2411). Product commit on `port/PP-6`,
docs+patch in the follow-up `port:` commit. Not merged and not pushed.

## What it is

Two follow-ups on the Perforations gizmo (PP-1/PP-2), asked for by the user after living with it:

1. **Lines gets 0..90°**, so the single slot family can be turned all the way to vertical. The other
   three patterns keep their ranges.
2. **The yellow proposal updates while a slider is dragged**, instead of only when the mouse is
   released (spacing, hole width, boundary and angle).

## 1. Lines turns through the full quarter

The bound was one shared constant, `PERFORATION_MAX_ANGLE_DEG = 45` in
`src/libslic3r/PerforationPattern.hpp`, used in exactly one place: the panel's `ANGLE_MIN/ANGLE_MAX`
(`GLGizmoPerforation.cpp`, `render_panel_body`).

45° is correct for **Grid** and wrong for **Lines**:

- Grid is *two* families at `angle` and `angle + 90`. That pair at θ and at `90 - θ` is the same pair
  mirrored, so 0..45 already covers every distinct pair - the original comment's reasoning was right,
  but only for a pair.
- Lines is *one* family. Its family at 100° is not the family at 80°; it is that family mirrored, and
  on an asymmetric face that is visibly different geometry. 45° was therefore only ever the diagonal:
  **vertical lines were unreachable**.

The generator never had a limit - `line_family()` takes any angle and rotates the domain by `-theta`
before laying the strips - so this was a UI bound, not a geometry one.

What changed:

- `PERFORATION_MAX_ANGLE_LINES_DEG = 90.0` next to the existing constant, with the header comment
  corrected: the 45 is now documented as the *pair* (Grid) bound, and the new one as the single-family
  bound (0..90 spans horizontal → vertical; 90..180 is the mirrored half, and the user chose to stop at
  the vertical). `PerforationParams::angle_deg`'s "0..45 deg" comment no longer stated the truth and
  now points at the constants.
- The panel selects `ANGLE_MAX` from the current kind; the angle row is still drawn for Lines and Grid
  only.

Deliberate: a `kind` change does **not** clamp the angle into the new range. Switching
Lines(80°) → Grid leaves 80 in the field with the slider grabber pinned to its 45° maximum, and the
geometry uses 80°, which for Grid is exactly equivalent to -10°. ImGui clamps the *grabber ratio*
(`ScaleRatioFromValueT`) and never writes a clamped value back, so nothing moves behind the user's
back and the first slider touch pulls the value into range. That is the same rule PP-1 set ("neither
clamps the other, and neither is ever changed automatically") and it was kept.

The MCP tool's `angle` description now states the per-kind range, since that string is what a client
shows the model.

## 2. The proposal follows a drag

`on_render()` rebuilt the proposal only when `! m_editing && m_pick_dirty`, where `m_editing` was
`ImGui::IsAnyItemActive()` latched in the panel and released a "rebuild now" flag when the item went
inactive. The sliders already mark the proposal dirty on **every** frame whose value changed
(`gizmo_row_slider` → `ImGui::SliderFloat` returns true for each drag frame), so the flag was live and
the gate was the only thing holding the highlight back until release.

The gate's stated reason - "building it every frame is what made the old hover ghost flicker while a
slider was dragged" - no longer applies: the flickering ghost was the *cursor-following* preview that
PP-2 replaced with a preview anchored to a cached pick. The gate and the whole `m_editing` member are
gone, and `on_render()` rebuilds whenever the proposal is dirty. `GLGizmoExtrude` has always done
exactly this (its slider setters mark `m_preview_dirty`, `on_render()` rebuilds, no editing gate), so
Perforations is now consistent with its sibling rather than the outlier.

Cost, stated honestly and not hidden: the rebuild is the full pipeline - coplanar-region union,
Clipper union of the pattern, triangulation, the PP-5 weld, and a `GLModel` reset + VBO upload - so at
an extreme setting (spacing 1 mm on a large face, where the builder's own `MAX_FEATURES = 20000` cap
bites) a drag updates at a few frames per second. That is inherent to rebuilding the mesh, and a
throttle would not change it, so none was added (user decision). A frame-time guard on `m_pick_dirty`
is the upgrade path if it ever matters.

## Files

| Kind | Path |
|------|------|
| Core | `src/libslic3r/PerforationPattern.hpp` (the two constants + the angle comment) |
| Gizmo | `src/slic3r/GUI/Gizmos/GLGizmoPerforation.{hpp,cpp}` |
| MCP | `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` (`angle` description) |
| Test | `tests/libslic3r/test_perforationpattern.cpp` |

Marker: `[ORCAPORT:PP-6]` at the four sites.

## Verification

- Unit: a new case in `test_perforationpattern.cpp` builds a Lines pattern on a 100 x 60 mm face at 0°
  and at 90° and asserts the orientation of every contour: at 0° the slots are wider than they are
  tall, at 90° taller than they are wide. That is what "vertical lines" means, and it fails on the old
  0..45 bound (a 45° slot is a near-square diagonal).
  Not asserted: equal area between 0° and 90°. On that rectangle the two happen to agree
  (6 rows x 100 mm = 10 columns x 60 mm = 600 mm of slot), but that is the numbers lining up, not a
  property of the pattern - the strip phase differs between the two axes.
- Run: `[PerforationPattern]` 7 cases / 422 assertions, all passed (6 cases before this change).
  Full suite `ctest --test-dir build/tests -C Release`: **2411 passed, 0 failed**, seven
  `RequiresApp`/plugin cases skipped as usual. Build: `build_win.bat -s --tests`, 7m42s, the four
  touched translation units compiled (log lines for `PerforationPattern.cpp`, `GLGizmoPerforation.cpp`,
  `OrcaMCPGizmoTools.cpp`, `test_perforationpattern.cpp`).
- Manual (user, in the app): open Perforations on a flat face, pick it, and check
  (a) the Angle slider's right end reaches 90 and a Lines pattern goes vertical as it does;
  (b) with a face picked, dragging Spacing / Hole width / Boundary / Angle moves the yellow proposal
  under the cursor, releasing nothing needed. Screenshots via the `ui_screenshot` tool (UI-2) if a
  before/after is wanted.

## Also in this batch

`scripts/tools_schema.py` was regenerated against a running build (the MCP-3 row's own note asked for
it): the static list the bridge serves when the app is not reachable held **63** tools, the live server
reports **77**. Added: `cut_gizmo`, `edge_dress_gizmo`, `extrude_gizmo`, `find_holes`,
`get_app_preferences`, `get_log_tail`, `get_object_features`, `get_suppressed_dialogs`, `holes_gizmo`,
`list_orcaext_keys`, `mesh_boolean_gizmo`, `perforation_gizmo`, `set_app_preference`, `ui_screenshot`.
The regenerated file proves which binary produced it: `perforation_gizmo`'s `angle` description is this
change's new one. See the manifest rows for MCP-1/2/3 and `scripts/regen_tools_schema.py`.

Trap, for the next regen: after launch the endpoint accepted no request for about two minutes - the
listener is up well after the window appears, and a 60 s poll budget reports it as "not running". Poll
with `curl` or `urllib` (not `Invoke-WebRequest`, whose 3 s timeout throws first) and a longer budget.
