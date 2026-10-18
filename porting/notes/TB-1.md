# TB-1 — Mounting tabs: a plate on a picked face, with a screw passage and an optional boss

Status: **in progress on `port/TB-1`** off `port/integration` `2706b1a6db`. P1 (the geometry, with
tests) is **landed and verified** — `a5a39044f2`, build clean, `libslic3r` 1571/1571 via ctest. P2
(gizmo), P3 (MCP) and the patch are not written yet; this note is updated as they land. Nothing is
merged until the owner has tested the built app.

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

## Still to do

- **P2** `GLGizmoTabs.{hpp,cpp}` + registration (`GLGizmosManager` EType + construction + the icon
  pair, `Shortcuts.hpp`, `localization/i18n/list.txt`). The panel is being shown to the owner as a
  mockup before it is written.
- **P3** the MCP control surface in `OrcaMCPGizmoTools.cpp` + `tools_schema.py` regeneration.
- **P4** `78_TB-1.patch`, the handoff section, and the manifest row's status.

Deliberately not in v1: one hole per tab (its position offsettable along the tab); one bore diameter
for the whole stack (no clearance + pilot pair); no nut / insert pocket; no root taper or gusset; no
conform-to-surface (the bite covers it); no persistence.
