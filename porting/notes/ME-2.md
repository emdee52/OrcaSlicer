# ME-2 - Holes tool (teardrop / bore-pocket / rim)

- Source: **fork-original**, no Neotko release note. Design log: `docs/handoff.md` sections 3, 11, 12.
- Category: B (feature). Not a port of `OrcaFS-NeotkoCM` — that fork has no `GLGizmoHoles`.
- Status: **ported**, merged into `port/integration` (`port/ME-1`, `port/ME-2`, `port/ME-2c`,
  `port/ME-2d`, `port/RIM-1`, and this session's `port/ME-2-fix2` = `61f46aabff`,
  `port/ME-2-fix3` = `030e512e89`, `port/ME-2-screw-td` = `be3367cdeb`, all user-verified
  2026-10-15). The flush shrink tube below is merged `--no-ff` as `098e627827`
  (branch `port/ME-2-fix`), user-verified in the app 2026-10-15: the ring step at both mouths is
  gone and the shrunk holes are otherwise unchanged.

## What it is

`DetectedHole`s (cylindrical walls found by `detect_holes`) are listed as candidates; each picked
hole gets one *operation* volume: a teardrop negative (horizontal holes), a bore/pocket negative
(screw, heat-set insert, magnet, nut, custom), or a rim chamfer/fillet negative. Everything is a
mesh volume — no CAD, no boolean on the object itself. Bore/pocket can also be authored on any
flat face (*Place on face*).

- `src/libslic3r/HoleDetector.{hpp,cpp}` — patch growth by vertex adjacency, PCA-of-normals axis,
  Kasa circle fit, angular coverage, cap flood-fill for `through`. Returns
  `DetectedHole{axis, center, radius, depth, through, confidence, facets}`. `axis` is only
  *canonicalized* (toward +Z, else +X/+Y) — **its sign carries no material information**.
- `src/libslic3r/HoleShapes.{hpp,cpp}` — the solid primitives: `its_make_teardrop(_for_hole)`,
  `its_make_bore`, `its_make_counterbore`, `its_make_countersink`, `its_make_tube`,
  `its_make_shrink_tube`, `its_make_nut_pocket`, `its_make_rim_chamfer` / `its_make_rim_fillet`,
  and the shared right-handed `place_on_axis`.
- `src/libslic3r/HoleStandards.{hpp,cpp}` — screw/insert/magnet/nut table + `HoleFit`.
- `src/slic3r/GUI/Gizmos/GLGizmoHoles.{hpp,cpp}` — the tool (`EType::Holes`, `Ctrl+J`).
- `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` — `find_holes`, `holes_gizmo`.
- Tests: `tests/libslic3r/test_holeshapes.cpp`.

## Keys / markers

- No config or preference keys; the tool writes ordinary model volumes on demand.
- Markers `ME-1` (detection, teardrop, registration) and `ME-2` (shared standards, unified tool,
  bore/pocket, shrink tube) appear in `GLGizmoHoles.{hpp,cpp}`, `GLGizmosManager.{hpp,cpp}`,
  `OrcaMCPGizmoTools.cpp`, `OrcaMCPServer.{hpp,cpp}`.

## Shim: shrinking a hole (M4 -> M3)

Shrinking cannot remove material, so the rework adds a **positive** annulus (`MODEL_PART`): it
fills the gap between the old wall and the new smaller bore. `toggle_bore` adds the tube first and
then the negative bore, so the bore also clips the tube's inner wall.

Both caps of the tube land exactly on the ends of the detected wall, and the outer wall overlaps the
old one by 0.4 mm in diameter. The radial overlap is what keeps the union watertight; putting the
caps on the wall's ends is what makes the fill flush with whatever the hole was bored through.
Because the ends are taken from the wall itself, the result does not depend on the sign of
`feature_frame`'s `dir`.

**Known gap (not fixed):** a flat cap can only be flush where the mouth is a plane square to the
hole axis (the usual case: the hole is bored straight through a flat face). Where the surface
curves away over the mouth, no flat cap can follow it — the cap sits proud of the surface by up to
the mouth's own axial spread, `(r_old² - r_new²) / 2R` for a mouth on a cylinder of radius `R`.
Closing that needs a fill whose end face follows the surrounding surface, i.e. a *through-hole*
variant of `cavity_fill_local` (the Cavity fill tool's plug, whose header defers full through
holes). Second, `feature_frame` derives `dir` from the canonical axis plus the user's *Flip*
toggle, so for a *detected* hole `dir` points out of the material about half the time; the shrink
tube no longer cares, and `rim_mesh` places its ring at whichever mouth *Flip* selects (the control
is available for bore and rim alike). The *default* side is still arbitrary: a material-aware axis
sign (a ray cast along ±axis against the merged mesh, or orienting toward the capped end when
`!through`) would pick the visible entry face without the user flipping by hand.

## Round (2026-10-15) - round features under a non-uniform scale

The gizmo built every feature in object space and only divided the user/standard dimensions by
`object_scale()` - the mean of the instance's three axis scales, the ME-2d fix for a **uniform**
scale. Under a non-uniform scale a circle in object space is still flattened by the instance
transform, so a Ø5 bore on a `(2,1,1)` instance read as a 7.5 x 3.75 ellipse in the viewport, exactly
like the detected hole it was reworking.

Fix: build each feature in **world space** and pull the vertices back to object space through
`instance_matrix().inverse()`; the instance transform then restores a round, correctly sized feature.

- New helpers `world_dir`, `world_point`, `axis_len_scale` (object length along the hole axis ->
  world), `world_to_object` (apply `M^-1`); `object_scale()` is repurposed as the object radius ->
  world radius factor (a single mean, the best choice for a quantity with no direction).
- `teardrop_mesh`, `bore_negative_mesh`, `bore_tube_mesh`, `rim_mesh` build in world and pull back;
  place-on-face inherits it (its sizes are already mm).
- The teardrop is built on a world-space hole with `up = UnitZ`, so the apex points along world up;
  `object_up()` stays for detection's horizontal test.
- Detection, pick cylinders and previews stay object-space; previews render through
  `instance_matrix()`, so they come out round with no change.
- Uniform scale and pure rotation are unchanged: for those, `M^-1` applied to a world feature is the
  same as the old object-space build (the pull-back is the exact inverse of the old division).

**Limitations (by nature, not bugs).** The rim and shrink-tube radii come from the detected hole,
which is elliptical under a non-uniform scale, so they use the mean `object_scale()` and are
approximate there; bore/pocket and teardrop sizes are the user's mm and exact. Re-scaling the
instance after a feature is added re-stretches it (detection is object-space, like every hole).

**Verification.** `libslic3r_tests [HoleShapes]` unchanged (26 cases / 143 assertions; the
primitives are untouched). The gizmo paths are not unit-tested - click-test: scale an instance
non-uniformly (e.g. 2x in X), add a bore/pocket, and confirm it is round (previously an ellipse).

## Round (2026-10-15) - screw-bore teardrop

New feature (user request): bore/pocket, **Screw** category, gets a **Teardrop** checkbox that shapes
the clearance bore's cross-section as a teardrop, so the top of a horizontal screw hole is a sloped
roof rather than a flat overhang.

- `libslic3r`: new `its_make_teardrop_prism(radius, depth, angle, axis, entry, up, segments)` -
  `its_make_teardrop` placed from `entry` along `axis` toward `up`. It is the old
  `its_make_teardrop_for_hole` body generalized; that function is now a thin wrapper over it and
  keeps its geometry, so its existing tests are unchanged. Returns empty when `axis` is parallel to
  `up`.
- `GLGizmoHoles`: `bore_negative_mesh` builds the round screw profile as before, then unions the
  shaft teardrop prism (radius `bore_d/2`, full depth). A teardrop is a superset of the round bore,
  so the shaft comes out teardropped above and round below. A counterbore head stays round; a
  **Teardrop head** checkbox unions a second prism of the head diameter x head height. New state
  `m_screw_teardrop` / `m_screw_teardrop_head` (+ setters); turning the shaft teardrop off also
  clears the head one.
- The **apex angle reuses the Teardrop operation's `m_angle_deg`** (not a separate field). The apex
  and the head option show only while the teardrop is on, and the head option only for a counterbore
  head - a countersink is a cone with no vertical wall to teardrop.
- A vertical hole has no up direction perpendicular to its axis, so the prism comes back empty and
  the bore stays round; the option is harmless there.
- MCP `holes_gizmo`: reports `screw_teardrop` / `screw_teardrop_head` and adds
  `set_screw_teardrop` / `set_screw_teardrop_head`.
- Tests (`test_holeshapes.cpp`): "A teardrop prism runs from the entry and rises toward up" (spans
  entry -> along the axis, `-r` to apex across, volume = teardrop area x depth) and "A teardrop
  prism on an axis parallel to up is refused"; `[HoleShapes]` 26 cases / 143 assertions pass.

Branch `port/ME-2-screw-td`, stacked on `port/ME-2-fix2` + `port/ME-2-fix3`; merged into
`port/integration` as `be3367cdeb`, user-verified 2026-10-15.

## Round (2026-10-15) - teardrop gating, Custom sizes, selected-state buttons

Four user-reported issues, all in `GLGizmoHoles.cpp` / `GLGizmoHoles.hpp` (+ the MCP mapping in
`OrcaMCPGizmoTools.cpp`).

1. **Teardrop acted on vertical holes.** `detect()` already flags `horizontal`
   (`|axis·up| < HORIZONTAL_COS`, 0.5), but only `gizmo_toggle_hole`/`gizmo_apply_all` consulted
   it. `rebuild_previews` built a ghost for every hole, and for a near-vertical axis (~1e-3 off the
   up vector) `its_make_teardrop_for_hole` does not hit its `1e-6` parallel guard, so it built a
   teardrop with a garbage `up` direction; the pick cylinder kept the hole hoverable too. A new
   `hole_usable(idx)` (= `m_operation != Teardrop || m_holes[idx].horizontal`) now gates the
   previews, `on_set_hover_id` and the `Detected holes:` count/`Apply all` enabled state. Bore and
   rim keep every hole.
2. **Wrong standard after editing the diameter.** The category combo listed only catalogue
   standards, so a size with no match left `m_standard == -1` (Custom) but the combo had no Custom
   row and fell back to `sel = 0` = the lowest-order entry (M2 screw / M3 nut) - the label lied.
3. **A custom nut became a circle.** `is_screw/is_nut/is_pocket` were read from `s->kind`; with
   `s == nullptr` (Custom) all three were false, so `bore_negative_mesh` fell through to a plain
   round bore and the label switched from "Across flats" to "Diameter".
   - Fix for 2+3: the combo now leads with a **Custom** row (id 0 -> `set_standard(0)` ->
     `m_standard = -1`), and `is_screw/is_nut/is_pocket` (and `bore_diameter`'s `fitted`) derive from
     `m_category`, so a Custom size keeps its category's geometry - a hex nut stays hex, an
     insert/magnet keeps its fit-derived tolerance. The standalone **Custom category button** is
     gone (`HoleCategory::Custom` removed from the enum, icon map, label map, `category_matches`, the
     button array and the MCP `category` string). `standard_kind(HoleCategory)` supplies the kind
     where `hole_fit_diameter_delta` needs one.
   - **Size-only Custom** (user decision): a Custom screw/nut carries no catalogue extras - no screw
     head styles (so `bore_negative_mesh`'s head block is guarded on `s != nullptr`), and no coaxial
     clearance bore for a nut (`its_make_nut_pocket` gets `clearance_d = 0`).
4. **Head / Fit buttons had no selected state.** The Head (None/Socket/Button/Sink), screw Fit
   (Free/Tap) and pocket Fit (Tight/Slip) buttons were plain; a shared `sel_button(label, on, cb)`
   helper (same teal fill as the op/category buttons) now highlights the active one.

No `HoleShapes` geometry changed; its tests are untouched. The UI paths are gizmo code and are not
unit-tested - app build + user click-test. Was stacked on `port/ME-2-fix2` (the flip fix); both are
now merged into `port/integration` (as `030e512e89` and `61f46aabff`, user-verified 2026-10-15).

## Round (2026-10-15) - rim side selectable, Place on face gated to bore

Two user-reported bugs, both in `GLGizmoHoles.cpp` (`on_render_input_window`, `set_operation`,
`set_place_face_mode`).

1. **Rim only on one end.** The *Flip* (entry) control was rendered inside the bore branch only, so
   `m_flip` - the sole way `feature_frame` moves a feature to the other mouth - could not be toggled
   while a rim operation was selected. On a detected hole the canonical axis sign ignores the
   material, so the ring could land on the end the user did not want with no way back. The control
   now renders for bore and rim; a teardrop is centred on the hole and ignores the entry, so it
   still gets none.
2. **Place on face outside bore.** The button was always shown and stamped `face_shape_mesh()`, the
   current operation's shape, onto a picked face: a meaningless ring groove for a rim (there is no
   hole edge to chamfer) and the "fall back to a plain bore" path for a teardrop. It now shows only
   for bore/pocket; `set_place_face_mode(true)` refuses to enable it for any other operation (covers
   the MCP `set_place_face` action) and `set_operation` exits the mode when leaving bore, so the mode
   cannot outlive its button.

No geometry changed; `HoleShapes` and its tests are untouched. The UI paths are gizmo code and are
not unit-tested - app build + user click-test.

## Round (2026-10-15) - flush shrink tube, apex field width

Two user-reported bugs, both in `GLGizmoHoles.cpp` / `HoleShapes.cpp`.

1. **Boss around a shrunk hole.** `bore_tube_mesh` extruded the tube for
   `h.depth + max(0.4, 0.2 * h.depth)` from `entry`, i.e. it overshot the far end of the detected
   wall on purpose. Since a detected hole's axis sign ignores the material, that overshoot landed
   on the *visible* face and the positive volume became a collar (OD `old_d + 0.4`, height
   `max(0.4, 0.2 * depth)` — ~1 mm on a 5 mm wall, which is exactly the ratio in the report).
   - Fix: the extent math moved into `libslic3r` as `its_make_shrink_tube(wall_d, inner_d,
     wall_depth, axis, entry)` (`HoleShapes.cpp`) and `bore_tube_mesh` became a one-liner over it.
2. **Ring step at both mouths (2nd round).** The first cut of that fix recessed both caps
   `min(0.05, 0.2 * wall_depth)` inside the wall's faces, to keep the caps off the part's surfaces.
   0.05 mm is not invisible after all: at a realistic zoom it reads as a ring step around the hole,
   and it is *both* ends, so a through hole shows two.
   - Measured, not inferred: on the user's `test.3mf` (two 6.2775 mm tabs, a Ø3.89 hole bored
     through each flat face) the fill spanned x ∈ [6.575, 12.752] against faces at 6.523 and
     12.800 — 0.052 and 0.048 short, i.e. exactly the constant.
   - Fix: drop the recess. The caps now go exactly on the wall's ends, which is where that wall
     meets the part's surfaces, so the fill is flush at both mouths. `entry` no longer needs the
     axial nudge, and the geometry is direction-independent by construction.
   - Cost, stated plainly: the fill's cap is coplanar with the part's face over the 0.2 mm
     circumference where the fill's outer wall overlaps into the material. That is the case the
     recess was there to dodge. The regression test below runs the union through `cgal::plus` and
     probes that ring, so if a future mcut/CGAL change makes coincident faces misbehave, the test
     caught it first, not a print.
3. **Apex angle field clipped.** The typed apex field used `PushItemWidth(scaled(4))`. ImGui carves
   the `+`/`-` step buttons out of the item width, leaving ~2 characters, so `50` rendered as `5(`
   (`on_render_input_window`). Widened to `scaled(5.5)`.

## Verification

- `tests/libslic3r/test_holeshapes.cpp`:
  - "A shrink tube spans the wall exactly" — M4 -> M3 over 5 mm: the caps land on the wall's ends
    (to 1e-4), the outer Ø is the M4 hole + 0.4 mm overlap, and the annulus volume matches.
  - "A shrunk hole comes out flush with the faces it was bored through" — the end-to-end guard for
    bugs 1 and 2 on a real part: a plate with a Ø4 through hole, `detect_holes`, the fill, then
    `cgal::plus`, and rays from both sides. Asserts the surface is the plate's face to **1e-4** at
    the filled annulus *and* across the overlap ring, and that the new bore is open clean through.
    It fails against either bug: the old `+max(0.4, 0.2·depth)` overshoot puts the surface outside
    the face, the 0.05 recess puts it 0.05 inside.
  - "A shrink tube is empty when it would not fill anything" — matching diameter, enlarging, no wall.
- Gizmo paths are not unit-tested (`bore_tube_mesh` is a private GUI method); the geometry it feeds
  is what the tests above cover. App build + user click-test for the rest.
- Not covered: the curved-mouth gap above, and the model-part union itself beyond the probe above.

