# PP-6 — Perforations: Lines up to 90°, a live preview, grooves for Grid, a depth you pick, and a centred lattice

Status: **implemented, build clean, tests green** (ctest 2414/2414). Product commits on `port/PP-6`,
docs+patch in the follow-up `port:` commit. Not merged and not pushed.

## What it is

Six changes to the Perforations gizmo (PP-1/PP-2), asked for by the user after living with it:

1. **Lines gets 0..90°**, so the single slot family can be turned all the way to vertical. The other
   three patterns keep their ranges.
2. **The yellow proposal updates while a slider is dragged**, instead of only when the mouse is
   released (spacing, hole width, boundary and angle).
3. **An angle the new pattern cannot express goes back to 0** when the pattern is switched, instead of
   being left in place for a pattern that cannot show it.
4. **Grid is always cut as grooves** (`invert` on), and the Invert row is not offered for it: with it
   off, the holes leave floating squares of material the slicer cannot print.
5. **"To": pick a second, parallel face** that the pattern stops on, instead of the first surface the
   depth probe finds - so a pattern can cross a void and stop on a chosen far wall.
6. **The lattice is anchored at the centre of the face**, so a face that is not a whole number of pitches
   across is cut the same on both parallel edges instead of differently on each.

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

## 3. An angle the new pattern cannot express goes back to 0

`perforation_angle_for_kind(kind, angle_deg)` in `PerforationPattern.hpp` is the one rule: in range it
is **kept** (so switching to the wider pattern never disturbs a value Grid was using), out of range it
is 0. It runs in two places:

- the panel's pattern row, when the clicked pattern differs from the current one;
- the MCP `set_params`, applied **after every field**, so the order of a request cannot matter and an
  `angle` sent in the same call as `pattern` is not immune.

That last part was a real bug the MCP run caught in the first cut of the rule: it reset the angle only
when the kind struck it as changed, so `{pattern: grid, angle: 80}` sailed through and left grid at 80°
- exactly the state this rule exists to remove (the response said `"angle": 80.0`). Worth saying plainly:
no in-range Grid angle reproduces an 80° grid (Grid at 80° is the family pair {80°, 170°}, i.e.
{80°, -10°}; the in-range mirror is 10°), so any rule moves something. The point is that the number in
the field and the picture on the face agree afterwards.

## 4. Grid is always cut as grooves

With `invert` off, the two crossing families subtracted as *holes* leave the material between the
crossings as squares that touch nothing - floating regions the slicer cannot print, and the opposite of
what a user who picks Grid wants. `make_perforation_pattern()` now forces `invert || kind == Grid` **in
the builder**, which is the one place every caller routes through: neither the panel nor the MCP tool nor
a test can ask for the island version. On top of that the Invert row is not drawn for Grid, and both the
panel's pattern row and `set_params` set the flag so the state on screen says what is cut.

This changed an existing contract, so an existing assertion had to flip: the first case in
`test_perforationpattern.cpp` asserted `grid.front().holes.size() > 1` (the lattice-with-holes reading).
It now asserts the complement's several contours, and the dedicated case pins the property that matters -
what is left standing is ONE connected region.

## 5. "To": a second face decides the depth

Gizmo state, not a `PerforationParams` value: a target is a face reference, not a number, so it has its
own pick actions over MCP and is not part of `set_params`.

- **The row.** `To` with a `Pick face` button, drawn only once a face is picked (no dead control, and no
  `ImGui::BeginDisabled` - this tree's ImGui does not have it). Armed, the same button reads `Cancel`; a
  target set, it reads `Clear`. Where the exact size of "stop on the face" is read: the hint line at the
  bottom, which switches from the static depth sentence to "The pattern stops on the picked face: N mm
  deep".
- **The pick.** The same `raycast_object_face` the first pick uses, with the same exclusion of this
  tool's own negatives. Accepted only when the target is **parallel** (`|n·n_to| >= 0.99`) and **behind**
  the picked face (projection along `-n` strictly negative); otherwise an amber warning in the panel and
  an error over MCP (`gizmo_target_refusal()` maps one enum to both audiences).
- **The distance** is the plane-to-plane projection in object space, so the target's lateral offset does
  not matter. **The cut ends `PP_TARGET_OVERSHOOT = 0.2` mm past the face** - stopping exactly on it
  would make the prism's cap coplanar with a real surface, the sliver PP-4 taught us to avoid, and a
  fifth of a millimetre is under one extrusion width (200000 units at the slicer's 1e-6 mm resolution).
- **It is drawn** in the picked green: the two green patches are the two ends of the hole path.
- **It does not outlive the pick**: dropped with the pick, when the volume-list change removes its
  volume, and when the tool closes.
- **A right click while armed cancels the mode** instead of committing a pattern - the gesture that
  commits should not cut while the user is aiming at a face.
- **MCP**: `pick_to` (screen_x/screen_y, or the viewport centre), `clear_to`, and `to_valid` /
  `to_distance` in every report.

## 6. The lattice is anchored at the centre of the face

The reported symptom: with a spacing that does not divide the face, the cut at one boundary edge does not
match the cut at the parallel opposite one. The cause was the phase, not the clipping - three loops
anchored the lattice on the bounding box's minimum corner (`line_family`'s first strip at
`y0 + spacing/2`; `honeycomb`/`circles` starting a row at `bbox.min - dy` with the stagger driven by a
counter that began at that corner), so the phase was a function of where the bounding box happened to
start. The leftover at one end of the face was unrelated to the leftover at the other, and the lattice
was not even symmetric about the face centre.

The fix, in `PerforationPattern.cpp` only:

- every index runs symmetrically about the centre, `-n .. n` with `n = ceil(farthest extent / pitch)`, and
  features sit at `centre + k * pitch`;
- the stagger follows the **row index's parity**, not a counter, so the negative half behaves (the parity
  of a negative index is its own, two's complement);
- therefore the mirror of every strip or cell about either centre axis is another strip or cell, and the
  clipped pattern is invariant under a 180-degree turn about the centre - which is exactly the property
  that makes the two parallel edges match;
- index ranges are symmetric by construction rather than derived from both ends, because a range computed
  from both ends can add one strip on one side in a sub-unit parity case;
- **Lines is included**, and Grid inherits the fix twice over (it is two `line_family` calls).

Rejected alternatives, and why: deriving the spacing from the face changes a value the user set, against
the tool's own rule, and cannot work for the staggered lattices at all (their row pitch is
`spacing * sqrt(3)/2`, so no spacing puts a whole number of features across both axes); a button or a mode
adds a second code path for a state that is never better than the centred one (the corner phase is not
aligned to anything meaningful); and anchoring on the clicked point - the pattern's origin is already
there - only matches the two edges when the click is dead centre, so it does not answer the report.

Unchanged: the four inputs, the margin, the clipping, the extrusion, and the partial feature at the edge
(accepted by the user - a boundary still cuts through a cell). `spacing` remains exactly the pitch that
was typed; only the phase moved. Each loop now places up to one extra feature beyond each edge, clipped by
the intersection; `MAX_FEATURES = 20000` still bounds the work.

## Files

| Kind | Path |
|------|------|
| Core | `src/libslic3r/PerforationPattern.{hpp,cpp}` (the two constants, the angle + kind rules, the forced Grid invert, the centred lattice) |
| Gizmo | `src/slic3r/GUI/Gizmos/GLGizmoPerforation.{hpp,cpp}` |
| MCP | `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` (`angle` description, `pick_to`/`clear_to`, `to_valid`/`to_distance`, the two rules) |
| Test | `tests/libslic3r/test_perforationpattern.cpp` |

Marker: `[ORCAPORT:PP-6]` at the sites listed by `grep -n "ORCAPORT:PP-6"`.

## Verification

- Unit: a new case in `test_perforationpattern.cpp` builds a Lines pattern on a 100 x 60 mm face at 0°
  and at 90° and asserts the orientation of every contour: at 0° the slots are wider than they are
  tall, at 90° taller than they are wide. That is what "vertical lines" means, and it fails on the old
  0..45 bound (a 45° slot is a near-square diagonal).
  Not asserted: equal area between 0° and 90°. On that rectangle the two happen to agree
  (6 rows x 100 mm = 10 columns x 60 mm = 600 mm of slot), but that is the numbers lining up, not a
  property of the pattern - the strip phase differs between the two axes.
- Run: `[PerforationPattern]` **10 cases / 474 assertions**, all passed (6 before this batch). Full suite
  `ctest --test-dir build/tests -C Release`: **2414 passed, 0 failed**, seven `RequiresApp`/plugin cases
  skipped as usual. Builds: `build_win.bat -s --tests` (the four touched TUs compiled), then an
  incremental `-s --no-configure` after the MCP rule fix below.
- New cases:
  - **Grid grooves**: a Grid asked for with `invert = false` leaves **one** connected region standing
    (`diff_ex(inset, pattern).size() == 1`) and is area-identical to the explicit grooves call - the
    floating-square property, stated as the thing that matters.
  - **The angle rule**: 80 → 0 on Grid, 30 and 45 kept, Grid → Lines kept, 120 → 0, and the isotropic
    patterns carry the pair bound.
  - **Symmetry**: a 180-degree turn about the FACE centre leaves the pattern unchanged, all four kinds,
    `invert` both ways, angle 0 and 30 where the angle applies - and the case asserts the inset's centre
    equals the face centre, which is the uniform-margin step that makes an inset anchor a face anchor.
  - The pre-existing Grid assertion in the same file (`grid.front().holes.size() > 1`) had to **flip** to
    the new contract - it was written against the holes reading.
- Runtime over MCP on the 20 mm cube (Release, `ORCA_EXT_MCP=1`): `open` → honeycomb/0/off; `pick_at` at
  the viewport centre → `pick_valid` true; `{pattern: grid, angle: 80}` → angle **0**, `invert` **true**;
  `{pattern: lines, angle: 60}` → 60 kept; `{pattern: grid}` alone → 0 again; `pick_to` on the same face
  → refused, "The target face has to be behind the picked one"; `create` → `applied_count` 1;
  `slice_all` → `idle` with only the two pre-existing warnings (objects too close, `precise wall`
  ignored).
- **NOT reached over MCP: the accepted target path.** Three attempts, three dead ends, all geometry:
  a 20 mm cube is convex and symmetric, so its far face is always occluded and a 180° rotation maps it
  onto the near one; `rotate_object` / `mirror_object` leave face 1's object-space normal pointing away
  from the model, so nothing is "behind" it any more; and the tool list has no camera control to look at
  the far side. A concave fixture (a pocket whose floor is a second visible parallel face) is what the
  check needs and `tests/data` has none. **Manual protocol for the user**: pick a flat face, orbit until
  a parallel face behind it is visible, press `Pick face` and click it, check the distance in the hint
  line against the wall thickness you know, then Perforate - and check that the cut reaches that face
  and not the first void. Two variants worth doing: a target that stops the cut *earlier* than the probe
  would have (a shorter hole), and a `Clear` that puts the depth back on the probe.
- **Runtime, the centred lattice**: all four kinds created on a 4x cube face at `spacing 7` (80/7 is
  deliberately not whole - the interesting case) and sliced to `idle` with only the pre-existing warning.
  **Not reachable over MCP: seeing it.** A negative volume is buried inside the wall it cuts, so the
  prepare view shows nothing new, and there is no MCP switch into the sliced preview - so the look of it
  is yours: pick a face, use a spacing that does not divide it, and compare the two parallel edges.
- Manual (user, in the app), the rest: (a) the Angle slider's right end reaches 90 and a Lines pattern
  goes vertical as it does; (b) switching Lines(80°) → Grid drops the angle to 0, and Grid shows no
  Invert row; (c) with a face picked, dragging Spacing / Hole width / Boundary / Angle moves the yellow
  proposal under the cursor, releasing nothing needed. Screenshots via the `ui_screenshot` tool (UI-2)
  if a before/after is wanted.


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
