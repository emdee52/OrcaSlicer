# PQ-4 - NeoArachne single-line fill ("spine")

- Source: `NEOTKOCM_RELEASE_2_46.md` ("NeoArachne: Classic walls with a single line inside, for
  fine lettering").
- Fork: `OrcaFS-NeotkoCM` @ `67d2415aab` (2.4.6). Base for this diff: Snapmaker `v2.4.0`.
- Category: B (a new mode on a ported feature). Status: **ported** - build clean, `ctest`
  2407/2407; the off-is-identical slicing comparison and a print check are still pending.
- Markers: `[ORCAPORT:PQ-4]`; the new module carries an `[ORCAPORT FILE]` stamp.

## What it is

With **outer wall = Classic** and **inner walls = Classic**, whatever is left inside the walls is
printed as **one line along the centre of the gap**, with a width that follows the gap, whenever the
whole gap fits under the ceiling. Wider areas go to infill as usual. It replaces Classic's short
cross-hatched gap-fill strokes and the scraps between walls in narrow pockets, so small letters come
out with fewer paths and fewer starts and stops.

Intended for **thin strokes**; the release note is explicit that bold lettering is still better on
Classic or Arachne, where the gap inside an outline is 1-1.5 mm and one line cannot fill it well.

## What was ported

- **New module** `src/libslic3r/NeoArachne/NeoArachneSpine.{hpp,cpp}` (the fork's files, unchanged
  except for the `[ORCAPORT FILE]` stamp). It has two entry points:
  `fits_in_one_line(ExPolygon, ceiling)` and
  `run_spine(g, regions, params, stats, rejected, forced, out)`.
- **One new function** in `NeoArachnePlan.cpp`: `run_classic_spine()`, plus its dispatch case.
- **Five keys**, placed with the other NeoArachne options in Quality > Wall generator.

| Ours | Fork | Type | Fork default | Our default |
|---|---|---|---|---|
| `hybrid_single_line_fill` | `neoarachne_spine` | bool | true | **false** |
| `hybrid_single_line_min_width_pct` | `_min_width_pct` | % | 50 | 50 |
| `hybrid_single_line_max_width_pct` | `_max_width_pct` | % | 300 | 300 |
| `hybrid_single_line_min_length` | `_min_length` | mm | 0.1 | 0.1 |
| `hybrid_single_line_sliver_pct` | `_sliver_pct` | % | 35 | 35 |

The 300% ceiling is not arbitrary: this slicer's wall spacing (0.357 at a 0.4 nozzle) leaves a wider
gap than the spacing the feature was tuned against, so the same pocket needs a higher ceiling to
become a single line.

## How it behaves (measured rules, kept from the fork)

1. Walls keep a constant width and are only emitted where they fit whole.
2. **Before each interior wall**: if what is left fits entirely in one line under the ceiling, no
   more walls go there - the zone becomes spine. If no walls are left, it goes to infill.
3. The minimum width is a **floor**, not a cut: a pocket tip is still printed at that width.
4. The maximum width is a **ceiling**: the line is never split in two.
5. At a crossing the branches are chained (a 3-branch Y becomes two lines).
6. A gap that never reaches `_sliver_pct` anywhere is not a pocket but a **crack between two passes
   of the same wall** (typically the narrow tail of a letter) and is left empty - this is what
   removes the thin gap-fill strokes. It is a separate threshold from the floor on purpose.

Order matters and is kept: the spine is the **innermost** line, so with "InnerOuter" it is emitted
first (spine -> interiors -> outer wall) and the nozzle does not come back inside after the outer
wall; with "OuterInner" it goes last. Interior loops and the spine are emitted in **one bucket per
island** so the printer finishes an island before moving on.

## Deliberately NOT taken from the fork

- **The default.** The fork ships `single_line_fill` **on**. Ours is **off**: it is an experimental
  motion pattern (the fork itself restricts it to thin strokes), and options default off here.
- **The inner-walls default flip.** The same fork commit changes `neoarachne_inner_walls` from
  `ArachneStock` to `Classic`. Taking it would change the default wall output of every existing
  NeoArachne project - a direct violation of the behaviour-neutral rule. Not taken.

Together those two are why this port cannot change anyone's output: with the flag off, Case A of
`Plan::run` calls `g.process_classic()` exactly as before, and the spine path is additionally
unreachable unless the user picks Classic inner walls (not our default).

Also dropped: the fork's `NeoDebug` dispatch probe (we do not ship `NeoDebug`); a `log_dispatch`
line records the branch instead. Labels are neutral ("Single-line fill"), and the tooltips drop the
fork's references to a third-party slicer.

## Adaptations to this tree

- `ExtrusionPath::polyline` is a `Polyline3` here, so the interior loops are built with
  `Polyline3(poly.split_at_first_point())` (matching `PerimeterGenerator.cpp:244`).
- `chain_and_reorder_extrusion_entities` lives in `ShortestPath.hpp`; that include was added.
- Two new source files -> the build needs a **configure**, not `--no-configure`.

## Verification

- **Build clean** (20m14s, configure + full rebuild of the touched projects), no errors.
- **`ctest` 2407/2407 passed.**
- Still to run, and the one that matters most: slice a lettering fixture twice, with
  `hybrid_single_line_fill` off on the current build and on the pre-PQ-4 build, and compare the
  G-code. Off is byte-identical by construction (the same `process_classic()` call), but it has not
  been measured.
- Then: with it on, narrow pockets should show one centre line instead of cross-hatch, the line
  should taper at a pocket tip and never exceed the ceiling, slivers should be left empty, and a
  3-branch Y should print as two lines. Regression: `wall_generator = Classic` and `Arachne` paths
  untouched; NeoArachne with inner walls = Arachne unaffected.
