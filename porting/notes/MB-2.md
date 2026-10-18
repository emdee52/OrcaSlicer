# MB-2 — the right-click Mesh boolean: it is not a union, and it is no longer silent

Status: **implemented, build clean, tested and runtime-checked** (see Verification). Branch `port/MB-1` (same batch as MB-1), off
`port/integration` `7a1f8dbd18`. Not merged and not pushed.

## The question it answers

> "is the right click mesh boolean a union then? is there no way to make it automatically make negatives
> into differences and positives into unions?"

**It already does exactly that, and it is not a union.** `GUI_Factories.cpp` attaches the context-menu
item to `obj_list()->boolean()`, which calls `Plater::combine_mesh_fff()`. That builds
`csg::model_to_csgmesh(mo, ..., mpartsPositive | mpartsNegative)`, where each volume maps as

```cpp
vol->is_model_part() ? CSGType::Union : CSGType::Difference
```

so every model part is unioned and every **negative** volume (pockets, chamfers, perforations, edge
dressers) is subtracted, in one operation, and then `csg::perform_csgmesh_booleans_mcut` runs the same
mcut kernel the gizmo uses. One click, all volumes, type-aware. The gizmo is the *pairwise* tool; this is
the one-shot the owner wanted - it just was not getting that far, and said nothing when it didn't.

## What was actually wrong - two silent paths

**1. A pre-flight stricter than the kernel (fixed, but not what the owner hit).**
`combine_mesh_fff` gated with `csg::check_csgmesh_booleans(range)`, which defaults to the **CGAL**
variant - empty, `does_bound_a_volume`, `does_self_intersect` - and then executed with **mcut**. A mesh
carrying the slivers an earlier mcut boolean leaves behind can be refused there, and a refusal skips the
whole boolean and falls back to "keep only the positive parts": the cutter vanishes, no cut is made, and
the error text says exactly that. The owner's part is a candidate - 150 zero-area triangles out of
29 284, welded, 0 open edges - but the app log carries **no** `check_csgmesh_booleans fails!` line in any
session examined (those log at `info`), so this is not what they hit. It is the same class of failure and
it is fixed: the check now uses the kernel that runs (`use_mcut = true`), which keeps the part-is-empty
test and drops the two CGAL geometry predicates.

**2. A Difference that does nothing reports nothing (the owner's symptom).**
`detail_mcut::perform_csg` applies each `Difference` with
`MeshBoolean::mcut::do_boolean(*dst, *src, "A_NOT_B")` and **discards the outcome**. When mcut finds no
fragment for the cut, `do_boolean` keeps the part it could not cut, so `dst` comes out exactly as it went
in; the run reports success, `combine_mesh_fff` sees a perfectly good mesh, and the object is replaced by
it - the negative volume is gone and the cut it was there to make is missing, with **no error, no toast,
nothing**. That is "it doesn't work" with no message, and it also explains why the result reads as a
union (what survives is the union of the model parts). This is the same behaviour MB-1 found inside the
gizmo, where it is now reported.

## Changes

- **`MeshBoolean::mcut::do_boolean` returns whether it changed the mesh.** It is the one place that
  knows - every early return and the "kept the part it could not cut" path mean nothing changed. Callers
  that ignore the result are unaffected.
- **`csg::DifferenceReport`** (`PerformCSGMeshBooleans.hpp`) counts the Difference parts the list asked
  for and how many changed nothing, including a Difference dropped because the accumulated mesh or the
  part's kernel mesh was missing (equally silent before).
  `csg::perform_csgmesh_booleans_mcut(range, report)` fills it.
- **`Plater::combine_mesh_fff`** passes the report and, when a negative volume was not subtracted,
  **tells the user** through the same error channel the failure path uses - with a sentence that says
  what to do - and **logs it** (`BOOST_LOG_TRIVIAL(info)`) so the reason is in
  `%APPDATA%\OrcaSlicer\log\debug_*.log.0` and in the MCP `get_log_tail`. The fallback path logs too.
- **The gate now matches the kernel** (`use_mcut = true`), with a comment saying why.
- **New MCP tool `boolean_parts`** runs the *same* action - it selects the object in the Object List (what
  `boolean()` reads) and calls it - and reports objects/volumes/triangles before and after plus
  `triangles_kept_everything`. This path could not be driven from a script at all, which is why the
  owner's click was the only way to see its failure; it also gives scripts the one-shot above.

## Files

| Kind | Path |
|------|------|
| Core | `src/libslic3r/MeshBoolean.{hpp,cpp}` (`do_boolean` reports a change) |
| Core | `src/libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp` (`DifferenceReport` + counting) |
| GUI | `src/slic3r/GUI/Plater.cpp` (`combine_mesh_fff`: gate + report + log) |
| MCP | `src/slic3r/GUI/OrcaMCP/OrcaMCPServer.cpp` (`boolean_parts`) |
| Test | `tests/libslic3r/test_meshboolean.cpp` |

Marker `[ORCAPORT:MB-2]`.

## Verification

- Unit: a new case in `tests/libslic3r/test_meshboolean.cpp` builds the part list by hand - a 20 mm cube as
  `Union`, a 4 mm cube as `Difference` - and asserts that a cutter 100 mm away is **counted**
  (`offered == 1`, `not_applied == 1`) with the body's volume unchanged, while a cutter centred on the body
  is applied (`not_applied == 0`) and leaves the closed cavity of 7936 mm^3 (20^3 - 4^3). `[MeshBoolean]`
  is 3 cases / 17 assertions.
- Full `ctest --test-dir build/tests -C Release`: **2415 passed, 0 failed** (2414 before + the new case).
- Runtime over MCP, on a **duplicate** of the owner's object (`boolean_parts`): `triangles_before 29540`
  (29 284 + the 256-triangle pocket) -> `triangles_after 42072`, volumes 2 -> 1, `negative_parts 1`,
  `negative_parts_not_subtracted 0`, `boolean_failed false`, `message` absent. 42 072 is exactly what the
  gizmo's difference produces for the same pair, so **the right-click path cuts the hole correctly on the
  state the owner left behind**, and the path is now drivable from a script.
- **The owner's earlier failure was not reproduced on that state.** The gate's refusal lines
  (`check_csgmesh_booleans fails! ...`) appear in none of the recent sessions' logs, so on the evidence
  available the pre-flight was not what refused it; the no-op path is the one that fits "no error, no
  toast" but it does not reproduce either. What this change guarantees is that if it happens again it says
  which part and why (notification + log + `boolean_parts` fields) instead of reading as a silent union.
- **My own first report field was wrong and is gone**: `triangles_kept_everything` guessed "kept
  everything" from `triangles_after >= triangles_before`, and the run above (a *successful* cut) fired it,
  because a subtraction adds the hole's wall triangles. It is replaced by the exact counts from
  `Plater::ObjectBooleanReport`, which `combine_mesh_fff` fills from the CSG driver.

## Deliberately not done

- **Why** mcut cannot subtract a given cutter (rather than merely reporting it) needs the owner's object
  reproduced with the new tool; if the log says one difference was not applied on their model, that is a
  geometry question about where the pocket sits, and the report now says so instead of hiding it.
- The object context menu could show the no-op *without* a notification storm when several negatives
  fail; the current message counts them, which is enough for one object.
