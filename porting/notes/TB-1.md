# TB-1 — Mounting tabs: a plate on a picked face, with a screw passage and an optional boss

Status: **code-complete on `port/TB-1`** off `port/integration` `2706b1a6db`, **not merged**: it is the
owner's to test in the app (MERGE_POLICY). P1 (the geometry, with tests) is `a5a39044f2`; P2 and P3
(the gizmo and its MCP tool) are `ff6d2c8ef7`; **P5** (the placement rule, after the owner's report)
is `128447f906`; P4 (this note, the manifest row, the handoff section and patch `78_TB-1.patch`)
follows. Build clean (`ALL_BUILD`, Release), `libslic3r` **1574/1574** via ctest, and the tool driven
end to end through the MCP endpoint on a 20 mm cube twice (a side click and the first cut's mistake
in the same place).

Not a port. No Neotko or preFlight source: this is new work on the fork's own feature loop
(`FEATURE_AGENT.md`), and it is the only ID in `manifest.tsv` with no `fork_sha`.

## What it is

A tab: a plate lying in the picked face's plane, extruded along its normal, with a screw passage
through it and an optional boss. Two `ModelVolume`s, no mesh boolean and no CAD: a `MODEL_PART`
(the plate, and the boss as its own solid) and a `NEGATIVE_VOLUME` (the seat + bore). Positives are
unioned and negatives subtracted at slice time (`PrintObjectSlice.cpp`), and the context menu's Mesh
boolean already unions `is_model_part()` and subtracts `is_negative_volume()`
(`ModelToCSGMesh.hpp`) — so Bake works with no new code.

## Decisions, and who made them

| decision | choice |
|---|---|
| orientation | the tab lies in the picked face's plane, screw along the normal (the mounting ear: on a bottom face it lies flat and overhangs; on a side wall it is the same figure turned 90°) |
| re-editing | **ephemeral** — no per-volume config key, so no `PrintConfig` / `Preset` registration and no `.3mf` surface. After a reload a tab is a plain `MODEL_PART` + `NEGATIVE_VOLUME`; volumes are named `Tab#N` / `TabBoss#N` / `TabHole#N` so the object list reads and a later session can find them by prefix |
| catalogue | **screws only** (no nut / insert / magnet pocket), so the whole negative is one entry from the plate's top |
| shapes | **one parametric outline, not a shape menu**, plus a slot flag. Rect / Rounded / Stadium / TaperedU / Circle are presets that only write fields |
| fit defaults | **Free** (screws into a heat-set insert, plastic or wood, which is what the owner prints), socket-head counterbore seat, sink 0, `depth` = through the stack. **Boss off for Free**, on with a suggested height for Tap |
| placement | hover shows the ghost, a click anchors it, the panel's fields fine-tune, **Add tab** commits (house style: `GLGizmoExtrude`'s pick → adjust → commit, not Holes' place-on-click) |
| the anchor | the **passage centre**, not the tab's centroid: aligning a tab means aligning its screw |

## P1 — geometry (`libslic3r/TabShapes.{hpp,cpp}`)

One frame, everything in the caller's units: **+x** outward, **+y** the width direction, **+z** out
of the surface, `z = 0` the surface, the passage at the origin. The gizmo divides the panel's mm by
the volume's uniform scale before calling it, so this file never sees two units.

**The outline is exact, not approximate.** The flank runs at `taper_deg` from the root corner and
the tip is a circle tangent to it, whose radius is solved from the taper:

```
R = ( (root_w/2) * cos(a) - (root_len + tip_len) * sin(a) ) / (1 - sin(a))
```

At `a = 0` that is `root_w/2` — a stadium — and a steeper taper narrows the tip, so "12 degrees"
means 12 degrees and the tip width is the value that answers to it. Built as root corners → tangent
points → arc, with the tangent points *as vertices*: this is what the first test round caught, see
below.

**Minimum-material rules** (`min_wall`, default 2 mm) only ever GROW the tab, and set
`TabOutline::clamped`. Material behind the passage and ahead of it; beside it, measured against the
flank LINE so it follows the taper; and at the tip, where the flanks meet the arc — that last one is
why the tip radius answers to `min_wall` and not only to the taper, and it can lower the delivered
`taper_deg`, which is reported rather than hidden. The taper is capped at 60°, or the side rule
answers a near-vertical flank by growing the root without bound.

**Two invariants, both from bugs this tree already paid for** (they are the reason the file exists in
this shape):

- the plate reaches `bite` (default 0.6 mm, never 0) **below** the surface, and the boss reaches
  `bite` below the **plate's top** — never flush with the surface, with the plate, or with the
  host's own face. A solid that ends flush is a zero-thickness contact, the mirror of ME-3's cutter
  overshoot. The boss's base started at the surface and then at the plate's bottom in the first
  draft; both are coplanar pairs, and the second is measurably worse for a Bake (see below).
- the screw cutter is entered **above** the tab's top through `hole_cutter_entry_margin()`, the same
  overshoot ME-3 added to the head shapes, so the seat it opens cannot be skinned over.

**`tab_probe_surface()`** samples a grid inside the footprint and raycasts the mesh through
`frame_to_its`, returning the share of the footprint that has the surface under it and how far that
surface falls away from the tab's plane. The gizmo uses the first to refuse a tab that misses the
material and the second to auto-bite on a curved pick (a flat plate on a 5 mm cylinder needs
`sagitta + weld`): exactly the two ways a cursor-placed tab can be born broken.

## P1 — what the test round caught (all fixed)

| symptom | cause |
|---|---|
| flank ran at 12.11°, not 12°, and was not tangent | the upper **tangent point was computed but never pushed** into the contour, so the flank joined the first arc sample and cut the corner |
| `tab_probe_surface` reported 0 coverage on every mesh | the ray **origin was mapped through `its_to_frame`** instead of `frame_to_its`; the direction needs the inverse, the origin does not |
| a slot of 40 mm was refused instead of narrowed | the root-length rule bounded itself on the **raw requested width** (20 mm of material behind the passage), which changed the tip through the taper and left no room for the prongs. It is bounded by the tab's own sides first now |
| the Bake left 300+ `its_num_open_edges` | a mesh boolean with T-junctions; not a coplanar-pair fault (moving the boss's base off the plate's bottom changed it by 4 edges). The bake test asserts the shape through rays instead, and says why |

## Verification (P1)

`tests/libslic3r/test_tabshapes.cpp`, `[TabShapes]` **14 cases / 165 assertions**:

- the flank angle and the tangency are measured **off the shipped contour** (the longest upper edge
  of the shipped polygon, then `|T − C| = R` and `(T − P)·(T − C) = 0`), so the test checks the shape
  and not the formula that made it;
- Stadium (parallel flanks, tip = root width) and a flat tip cut exactly at `tip_len`;
- a slot: the outline is one expolygon with no holes, `make_tab_screw` returns nothing, and a ray
  along the slot's axis entering from beyond the tip meets the slot's **closed end** where a drilled
  tab would have met its tip wall;
- the clamps: a 4 mm tab with a 3.4 mm passage grows until **16 directions** out of the passage keep
  `min_wall`, and a spec with no room for prongs is refused;
- the bite (a requested 0 still buries the plate), the plate's volume = outline area × height, and
  the plate's mesh has no open edges;
- the boss passes through the plate and stops at `thickness + boss_h`;
- the cutter's entrance cap is above the tab's top by `hole_cutter_entry_margin(d)` and its floor is
  still `depth` below the top, coaxial;
- one **end-to-end bake** onto a 20×20×10 cube: down the axis nothing is hit at all (the hole is
  open through, which is also what proves no skin was left), the counterbore's shoulder is 3 mm
  below the tab's top, the boss's top is 7 mm above the cube's face, and the cube's own face is
  untouched beside the tab.

Whole suite: `ctest --test-dir build/tests/libslic3r -C Release` — **1571/1571**, 0 failed.
(The `libslic3r` suite was 1557 cases before ME-3's 14.)

## P5 — the placement rule, after the owner's report

The owner, testing the built app on a side-face click:

> these are mounting tabs not whatever weird thing you have implemented right now with them on the
> damn part itself... there needs to be an orientation or something because by default i want tab to
> be placed parallel to the bed... the root of the tab should be attached to the face that you clicked

The first cut built the frame from the picked facet's normal, so a wall gave a **vertical plate lying
in the wall's own plane** with a horizontal screw. That is not a mounting tab: a tab on a wall is a
**foot that stands out sideways and is screwed down from above**. The rule is now libslic3r's
(`TabShapes::tab_frame`, so it is testable) and it is:

- the plate's normal is **the bed's up** on a wall, so the tab prints flat and its screw goes down; the
  frame's `+x` is the face's normal projected into the bed's plane, so the tab stands straight out of
  the wall and the panel's Angle turns it about the bed;
- **the root edge is placed ON that face**, `bite` into it (that sideways overlap is the weld), and the
  screw sits `root_len` out from it - so on a wall the click marks where the ROOT attaches (the owner's
  words) while on a flat face it still marks the screw. `Root length` therefore reads "how far the tab
  reaches back from the screw, which on a wall is how far the screw sits out from the face";
- a face within **25 degrees of horizontal keeps the plate in its own plane**, which is the same thing
  for a bottom or top face (a foot under the part, welded by the bite in z, walking out from under it).
  Anything steeper is a wall, including a 45 degree face.

Two things followed from the rule, both found at runtime rather than by a test:

- the probe now measures the **root band**, not the whole plate: a tab is meant to reach out past the
  material, so what has to be over material is the strip it is welded by. It is also skipped entirely
  for a wall, where the plate hangs beside the face and the root is welded by construction - measuring
  the plate there reported **0 %** and, because the refusal did not know about walls, set `why` and
  would have refused **every wall tab**.
- with nothing to march through ("through" on a tab hanging in the air), the bore's fallback is the
  tab's own thickness + bite, not the blind-depth field's value.

**Tests** (17 cases / 183 assertions, `[TabShapes]`): a wall click gives a **horizontal slab** - its z
extent IS its thickness, its underside is at the click, its root reaches into the wall and it stands
out of it, with the width running along the wall; a bottom/top face keeps the plate in that face's
plane; 25 degrees off horizontal is still the plate's plane and 45 is a wall; and the angle turns it
about the bed, not the wall.

**Runtime**, on the 20 mm cube with a side click (Tapered U, boss on, through): `wall: true`, the
frame's x is `(0, -1, 0)` - straight out of the wall with no vertical component - and adding it takes
the object to **20 x 35.7973 x 20 mm** and **8493.1 mm3**, i.e. 15.8 mm out of the wall
(`root_len 6 - weld 0.2 + tip_len 10`). The panel's own line for a wall tab reads "Root on the face,
plate parallel to the bed, screw 6.00 mm out of it."

## P2 — the tool (`GLGizmoTabs.{hpp,cpp}`)

**The frame.** `+x` outward along the tab, `+y` the width, `+z` **the plate's normal**, the passage at
the ORIGIN - so the click marks the screw (or, on a wall, the root: see P5 above) and every length is
measured from it. Which normal that is, and where the origin lands, is `TabShapes::tab_frame`'s job and
is the P5 rule. The panel's mm are divided by `object_scale()` before `TabShapes` sees them, so one
unit is decided in one place; the geometry is built in the frame and transformed to object space by
`frame()`, which is what the volumes are added in (identity volume matrix, the convention the Holes and
Extrude gizmos use).

**The default angle** points away from the object's own centre, projected into the face plane: a
mounting tab hangs off the nearest edge, and that is the one default that makes the first click usually
right. On a symmetric object (the test cube) the anchor lands on the centre and the projection is
degenerate - it then reads 90 degrees, which is harmless and is why the panel's slider is there.

**Three volumes per tab, one id.** `Tab#N` and `TabBoss#N` as `MODEL_PART`, `TabHole#N` as
`NEGATIVE_VOLUME`. The slice unions the first two and subtracts the third, and the context menu's Mesh
boolean bakes the same three (`ModelToCSGMesh`), so there is no boolean in this tool. `is_tab_volume()`
matches any of the three prefixes, which is how the right-click removal, the applied preview and
`Clear all` find a whole tab. One `take_snapshot` per Add tab\*, so one undo removes the tab.

\* `Plater::TakeSnapshot` for the removals, `plater->take_snapshot()` for the add - the same split the
Extrude gizmo uses.

**Right-click** removes the tab under the cursor, and otherwise commits an anchored tab (PP-4's
"right-click confirms"). The two are distinguished by the raycast: the placement hover picks *through*
the tabs (a committed tab must not stop the next one being placed beside it), while the removal raycast
accepts *only* them. That is why `remove_at(screen_pos)` exists next to `remove_hovered()`.

**The preview** is the fork's PP-2 scheme with the surface measured, not guessed: teal for the
coplanar face under the cursor (`coplanar_region` + `build_coplanar_patch`, the Cut and Holes face
picking), then the whole proposed tab - plate, boss and cutter in ONE orange mesh, because a tab is one
thing to the eye and the cutter is inside it - and every tab already on the object with its parts in
green and its cut in red. There is deliberately no "picked face" colour: the anchor is a point, not a
face, and what it picks is the tab the orange mesh already shows. The object is NOT ghosted (unlike the
Extrude gizmo's whole-face work): a tab is a small local overlay and the part has to stay readable.

**The probe decides two things.** `tab_probe_surface()` gives the coverage - a tab the face is not
under is REFUSED (below 30 %) and warned about below 60 % - and the fall-away, which raises the bite
above the panel's value (`max(bite, fall_away + 0.2 mm)`) so a curved pick still welds; past 3 mm the
face is too curved for the footprint and the tab is refused with that reason. The probe is the only
O(mesh) work the panel does, so it is throttled to one per 100 ms while a slider is dragged and forced
fresh on an anchor, a shape change and `Add tab` - the one place that must not act on a stale reading.

**The panel** is the owner's approved mockup: five shape presets as glyph toggles (the presets only
write fields - `tab_apply_shape` is the single definition), the two lengths and the taper with the
*resolved* tip width and flank angle on the hint line, `Passage Hole|Slot`, and the Holes tool's own
screw rows: the same standards table, `Free|Tap`, the same counterbore/countersink seats, with the boss
sized to the head (`head_d + 2 x 2 mm`) and given the thread's height for a tap. The boss rows and the
whole screw section disappear in Slot mode, because a slot IS the passage. `Head` has no caption: four
names plus the label column do not fit the 380 px panel, which is the same compromise the Holes panel
records. A seat deeper than the plate is called out in amber (it opens through the tab's underside).
`Add tab` is drawn only once there is an anchor: it is not an action while there is nothing to add.

**Registration**: `GLGizmosManager::EType::Tabs` (after Perforation, `is_orcaport_gizmo`, the icon
switch), `Shortcut::GizmoTabs` on bare `V` (every other bare letter is taken: the port's are K/J/X/G/Z,
upstream's A-Z others, and I/O are the canvas zoom), `toolbar_tabs.svg` + `_dark.svg`,
`localization/i18n/list.txt`, `src/slic3r/CMakeLists.txt`.

## P3 — MCP

`tabs_gizmo`, 29 actions, plus the state it reports: every resolved number, the coverage, the effective
bite, `why` when a tab cannot be added, and every tab already placed (its centroid in object space).
`scripts/tools_schema.py` regenerated (79 tools). Note the trap MCP-3 recorded: the opencode client
caches its tool list at session start, so a tool added since is reachable only by talking to
`http://localhost:13618/mcp` directly - which is how the runtime check below was made.

## Verification (P2/P3)

Build: `cmake --build build --config Release --target ALL_BUILD` - clean, no warnings in the new files.

Runtime through the endpoint, on `tests/data/20mm_cube.obj` loaded into a fresh instance:

| step | evidence |
|---|---|
| `tabs_gizmo open` | `open: true`, `opened_now: true`; the panel's defaults report as `shape tapered_u`, `standard M3` (index 4), `diameter 3.4`, `head socket`, `screw_fit free`, `thickness 3`, `bite 0.6` |
| `anchor` (viewport centre) | `anchored: true`, `coverage 1.000`, `fall_away 2.9e-06` on a flat face, so `effective_bite` stayed at the panel's 0.6 |
| `set_boss on` | the boss sized itself to the head: `boss_d 9.5` (M3 socket 5.5 + 2 x 2), `boss_h 3.0` |
| `add` | `added: true`; the object then carries `20mm_cube.obj`, **`Tab#1` (type 0 = MODEL_PART)**, **`TabBoss#1` (type 0)** and **`TabHole#1` (type 1 = NEGATIVE_VOLUME)** - the three volumes, exactly as designed |
| the tab's placement | centroid `(0.0003, -11.2001, 0.9812)` in object space: the cube is 20 mm on a side about the origin, so the anchor took a side face and the plate's centroid sits 1.2 mm out of it (0.6 of bite in, 3.0 of thickness out) - a mounting ear on a wall, which is the same figure as a bottom-face tab turned 90 degrees |
| `ui_screenshot` (`gizmo`) | the real panel: the five glyphs with Tapered U selected, "Tapered U, 8.9 mm tip, 12 deg flanks" - the panel's own numbers agreeing with `tab_outline()` - and every row the mockup showed |

**Found by looking at the real panel** (not by a test): Stadium and Tapered U drew the *same* glyph,
because both presets have a round tip; the glyph's tip half width now follows the preset's own taper,
so a stadium (parallel flanks) and a tapered U differ. Fixed in `ff6d2c8ef7`.

**What was NOT verified**: the slice of the assembled model. The volumes are ordinary
`MODEL_PART`/`NEGATIVE_VOLUME`s and the union/subtract at slice time is pre-existing OrcaSlicer
behaviour, but the export step did not finish in this session (the owner cancelled the export call), so
"slices into a tab with a screw hole" is the owner's check, not a claim here.

## Still to do

- **The owner's test** in the app: place a tab on a real part, dial it, `Add tab`, and slice it. Nothing
  merges into `port/integration` before that (MERGE_POLICY).
- Not part of v1, in the order they would be felt: a nut / heat-set insert pocket on a tab's free side
  (they are `HoleShapes` builders and an entry side away); a clearance bore in the tab with a smaller
  pilot in the host, i.e. two diameters in one stack; several tabs in one placement (a row, a mirror);
  snapping the passage to a detected hole's axis or a negative volume's centre (`HoleDetector`, and
  `GLGizmoAlignStack::detect_feature_centers`); a root taper or gusset for the bending load.

Deliberately not in v1: one hole per tab (its position offsettable along the tab); one bore diameter
for the whole stack (no clearance + pilot pair); no nut / insert pocket; no root taper or gusset; no
conform-to-surface (the bite covers it); no persistence.
