# ME-3 — a screw head's cutter starts outside the face, so a mesh boolean cannot skin the recess

Status: **implemented, build clean, unit- and runtime-verified** (see Verification). Branch `port/ME-3`
off `port/integration` `2fa3fb01f3`. Not merged and not pushed.

## What was reported

The owner, in the Holes tool: **Bore** operation, Screw category, a **head** picked (socket), applied to a
**detected** hole, then **Mesh boolean** —

> it still shows a surface as still being solid but it does make the hole with a screw head when you
> slice it. i think the surface just has a sliver of material in there left that the stl thinks its solid

Detected hole, and the fix has to cover **Place hole on face** as well. Scope: **screw heads only**.

## What was measured

The symptom is a property of the boolean, not of the gizmo's arithmetic, so it was reproduced against the
kernel the gizmo uses (`MeshBoolean::mcut::make_boolean(..., "A_NOT_B")` → `do_boolean` → one subtraction
per connected component, the same path the Mesh boolean gizmo takes). A 20x20x10 plate with a 3.4 mm
clearance bore 6 mm deep (floor at z = 4) and a 6 mm socket counterbore 3 mm deep (shoulder at z = 7),
entrance **on** the top face at z = 10, then a ray straight down at the axis and at r = 2.4 (inside the
head, outside the bore):

| cutter entrance | 2nd hit at r = 0 | 2nd hit at r = 2.4 | reads as |
|---|---|---|---|
| cap in the face (`out_margin` 0) | **10.0** | **10.0** | the recess is covered by a skin |
| cap 0.34 mm outside the face | **4.0** (bore floor) | **7.0** (head shoulder) | the recess is open |

`10.0` at both probes: a zero-thickness **sheet across the whole counterbore**, coplanar with the face.
That is precisely the fault PP-4 fixed for a hole placed on a face (`GLGizmoHoles.cpp`: *"the hole is
through, but there is still a skin on one side" that shows up in an exported mesh while the slicer
silently discards it*) — which is why the print has the head while the mesh and the 3D view say the
surface is still solid.

## The cause, and why a *detected* hole is exposed

`feature_frame()` puts the cutter's `entry` at the end of the detected wall, and the detector sets that
end to real mesh vertices (`HoleDetector.cpp`: `h.depth` = the wall's axial extent), i.e. **in the part's
surface plane**. A plain re-bore only has its cap disc inside the hole's existing void there, so it is
harmless. A **counterbore head is wider than the hole**: its entrance cap is a disc whose ring covers the
face *around* the hole, coplanar with it — a coplanar pair, and mcut answers those with the sheet above.
The countersink is the same (its cone's wide cap is at the entry plane), which is why both head shapes
needed it.

The cure already existed in three places and was missing from the head shapes:

| where the cutter starts | cured? |
|---|---|
| a hole **placed** on a face (`face_hole`, PP-4: `max(0.2, 0.1*d)`) | yes |
| a teardrop on a **through** hole (`teardrop_depth` adds `2*margin`) | yes |
| the same margin applied to a screw **head** | **no** |
| a teardrop on a **blind** hole, the rim ring, a plain bore / nut / magnet that enlarges a hole | **no** (out of scope here, see below) |

## Changes

**`src/libslic3r/HoleShapes.{hpp,cpp}` — the margin in one place.** `hole_cutter_entry_margin(bore_d)` is
PP-4's formula (`max(0.2, 0.1*bore_d)`, world mm), now shared by the gizmo's two entry points instead of
being written out twice.

`its_make_counterbore()` and `its_make_countersink()` take an optional `out_margin` (default 0, so every
existing caller and test is unchanged): the entrance cap is placed that much further out than `entry`,
and **everything the shape removes below the `entry` plane keeps exactly the geometry `depth`,
`cbore_depth` / `csink_d` describe** —

- counterbore: both barrels grow by `out` while the entry moves out by `out`, so the shoulder stays at
  `cbore_depth` and the bore's far end at `depth` from the entry plane;
- countersink: the cone is **extended along its own surface** (widened by `out / tan(half)` at the new
  cap), so the diameter it cuts at the entry plane is still exactly `csink_d`. Without that the margin
  would have narrowed the sink by `2*out` (0.68 mm for an M3 at 90°) and the head would no longer seat
  flush.

**`src/slic3r/GUI/Gizmos/GLGizmoHoles.cpp`.** `bore_negative_mesh()` asks for that margin for every screw
head — Socket and Button counterbores, and the countersink — unconditionally, so it covers a detected
hole's entry (on the face) and a placed hole's (already a hair outside, where the extra margin is simply
more air). The optional **head teardrop** prism gets the same shift: it does not taper, so only its cap
moves. `face_hole()` now takes its own object-space margin from the same helper instead of its own copy of
the formula (`max(0.2, 0.1*d_obj)`) — for a non-unit instance scale that is a hair (≤0.03 mm of air)
different and consistently a world-space margin; the placed cut is unchanged.

No new option, no config key, no `.3mf` migration: an already-applied hole keeps its volume, and a hole
applied as a plain negative volume (no boolean) is unchanged — the extra cutter is outside the part.
The one ceiling this shares with PP-4's own placed-path margin: the cutter now reaches up to
`max(0.2, 0.1*d)` beyond the mouth, so a wall of the *same object* standing within that distance across
the mouth is in its reach, for the slice-time subtraction as well as for a boolean. On the runtime
fixture that reach is the 0.34 mm of air above the face; nothing else in the part is.

## Verification

**The fault and the cure, on the kernel** (`tests/libslic3r/test_meshboolean.cpp`):
`A counterbore whose entrance cap is in the face comes out covered by a skin` (asserts the 10.0/10.0 above,
and is the case that reproduces the report) and `A counterbore whose entrance cap is outside the face cuts
the recess cleanly` (4.0 / 7.0, using `hole_cutter_entry_margin(3.4)` = 0.34 the way the gizmo does).
`[MeshBoolean]` 5 cases / 23 assertions.

**The shape arithmetic** (`tests/libslic3r/test_holeshapes.cpp`):
`A counterbore's entrance cap starts outside the entry it is given` — margin 0.34, bbox min.z −0.34 /
max.z 10 unchanged, head radius unchanged, and a ray up the counterbore's own wall finds the shoulder at
3.0 (i.e. the head sits exactly as deep as before). `A countersink keeps the diameter it cuts at the entry
plane` — the half-width across the entry plane is 3.15 for `out` = 0, 0.34 and 1.5 mm.
`[HoleShapes]` 28 cases / 155 assertions; `[HoleDetector]` 9 cases / 25 assertions; full `libslic3r`
suite **1557 cases / 103 266 assertions, 0 failed** (the failure prints in its log are the negative-path
tests). The slicer builds clean (`OrcaSlicer_app_gui`, Release).

**Runtime, through the real gizmo and Mesh boolean (MCP)**, on a generated washer fixture (a 20 mm disc,
6 mm tall, with a 3.4 mm vertical through hole) so the hole is *detected*, not placed:

| workflow | evidence |
|---|---|
| detected hole + M3 socket head (free fit) | the created `HolePocket#0` (512 triangles, **2 patches** = bore + head) exports as z ∈ [−4.000, **3.340**] — the part's face is at 3.000, so the cap really is 0.34 mm outside it |
| … then Mesh boolean (difference, Delete input) | 1024 → 3202 triangles, `performed: true`; rays from above: r = 0 → **nothing** (the hole is open through), r = 2.4 → −3.000 and **−0.000** (the shoulder, 3 mm under the face), r = 5 → 3.000 / −3.000 (the untouched plate). No hit at 3.000 — no skin |
| placed on face (top face, same M3 socket head) | the cutter spans up to 3.680 = face + 0.34 (PP-4's own 0.34) + 0.34 (this margin), both in air; after the boolean, r = 0 → nothing, r = 2.4 → −3.000 and 0.340, r = 4.0 → 3.000 / −3.000 |
| detected hole + M3 countersink | after the boolean, the cone's wall is met at z = 2.399 at r = 2.4 and 2.899 at r = 2.9, and the face at 3.000 is intact at r = 3.05: the mouth is the standard's own 6.0 mm at the entry plane, i.e. the extension kept the sink diameter (uncompensated it would have been 5.32 mm and the r = 2.9 ray would have met the face) |

All three exported results had `open_edges: 0`.

## Deliberately not taken

- **Other cutters that touch the surface plane.** A *plain* screw bore with no head (a detected hole it
  enlarges), the nut / magnet pockets (their hex and bore caps are in the entry plane), a *blind*
  teardrop, and the rim chamfer/fillet ring (its `z = 0` annulus always is) have the same exposure and
  none of them was reported. Out of the owner's scope ("just do screw heads"); each is the same couple of
  lines if wanted.
- **The counterbore is still two overlapping shells in one volume** (its head cylinder merged into the
  bore by `its_merge`, with coincident entry caps). It does *not* break the boolean — `do_boolean` splits
  the tool with `its_split` and subtracts each component separately — but it is why the head teardrop is a
  documented wart (`⚠️` in the source) and why a proper stepped section builder is still the right
  follow-up. Not needed for this report.
- **Two pre-existing placed-path seat errors**, both from measuring a head from the offset entry rather
  than from the face, and both left exactly as they were: a placed counterbore's shoulder is one PP-4
  margin (0.34 mm) deeper than `socket_k` under the face, and a placed countersink's mouth is
  `2*0.34 = 0.68 mm` narrower than `csink_d` at the face (an M3 head therefore does not sit quite flush).
  Fixing them needs the distance from the entry to the surface carried into the shape builder
  (`DetectedHole` has no such field today), which is a wider change than this report. The detected path,
  which is what was reported, lands both exactly.

## Branch state

`port/ME-3` off `port/integration` `2fa3fb01f3`: product + tests commit, then a `port:` docs commit
(this note, the manifest row, handoff section 42, patch `77_ME-3.patch`). Not merged, not pushed.
