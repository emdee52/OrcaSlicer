# MB-1 — Mesh boolean: a cutter can only be the tool, the mode follows the picks, and the outcome is reported

Status: **implemented, build clean, MCP runtime-verified** (see Verification; the `no_op` branch is the one path that could not be exercised). Branch `port/MB-1` off `port/integration`
`40f7e1a70a`. Not merged and not pushed.

## What was reported

The owner, on a real project (`meshbooleantest.3mf`): perforation applied, mesh boolean, several edge
chamfers, mesh boolean again, then a hole added with **Place hole on face**, then "tried to mesh boolean
through the top of the perforated square and it doesn't mesh boolean and **makes the circle just
vanish**".

## What was measured on that object

Two volumes: an unnamed `part` (29 284 triangles, 0 open edges) and `FacePocket#1` **negative**
(256 triangles, 0 open edges). Three runs, by index so no click was involved:

| operation | result |
|---|---|
| Difference, source = the part, tool = the hole | hole cut: 29 284 → **42 072** triangles, cutter consumed with Delete input — the operation the owner wanted |
| Difference, source = the hole, tool = the part (reversed) | the cutter is **replaced by its own leftovers outside the part**: 256 → **12 796** triangles, renamed `FacePocket#1 - difference`, still `negative`; the part untouched → "no boolean, and the circle vanished" |
| Union (the mode the gizmo opens in) | one volume, **43 786** triangles, cutter consumed, no hole — also "the circle vanished" |

And why the first click lands wrong so easily — a raycast sweep over the visible circle with the gizmo's
own `probe_pick`:

```
   ..PPPPPPKKPPPPP..     P = the ray reaches the part
   ..PPPPPPKPPPPPP..     K = the ray reaches the cutter (3 px at most)
```

**Place hole on face** makes the cutter protrude only 0.5 mm above the top face, so from any off-axis
view the part's top surface occludes it: the cutter is a 2–3 pixel target and normal clicking selects the
part for both slots.

Three causes, in order of weight: the cutter is nearly unclickable; the mode opens as **Union** while only
Difference relabels the slots, so nothing says the *first* pick is the volume that survives; and
`generate_new_volume()` inherits the **source's** type and name and replaces the source, so a reversed
difference leaves a near-invisible negative leftover and reports nothing.

## Changes

**A. The slot follows the volume type.** `assign_pick()`: a `NEGATIVE` volume is a cutter by construction
(Holes, Edge dress, Perforations and Extrude all author their tool that way), so it goes to the **tool**
slot whichever slot was asked for. A `MODEL_PART` still goes where it was asked - a part may legitimately
be the tool ("cut this wall with that block"). A redirected pick does **not** advance the selecting state,
because the click did not fill the source slot: the operation is still owed a source. Both entry points
(the interactive click in `gizmo_event()` and `pick_volume_index()` for MCP) go through it, so a script and
a mouse behave identically.

The reversed difference can no longer be expressed: with the cutter in the tool slot there is no way to
ask for it to be subtracted *from*.

**B. The mode follows the picks.** With a cutter in the tool slot the mode becomes **Difference**, unless
the user has chosen a mode themselves (`m_operation_chosen`, set by the three tabs / `set_operation()`,
reset when the gizmo opens and when the selected object changes). Union was only ever the mode the gizmo
happens to open in - it is not a decision the user made, and it is meaningless for a cutter.

**C. The outcome is recorded and reported.** `perform_operation()` now fills a small `Outcome`
(`performed`, `no_op`, `triangles_in/out`, `volumes_before/after`, `message`) that the MCP tool reports on
`run` and `apply`. `no_op` detects the case the kernel produces when a boolean has no resulting fragment:
`do_boolean()` keeps the part it could not cut, `make_boolean()` hands it back as a perfectly good result,
and the gizmo then replaces the source with a copy of itself and consumes the tool. It is detected exactly
- same triangle count and same volume; the kept part is the source's own mesh copied through mcut - and it
raises the same warning notification a failure does, so "nothing happened and something vanished" can no
longer read as success. `apply` no longer reads the source volume *after* the operation either (it may
have been consumed): the triangle count comes from the recorded outcome.

**E. `on_set_state()` no longer shadows its own members.** Upstream declares

```cpp
bool m_diff_delete_input = false;    // a local copy, not the member
bool m_inter_delete_input = false;
```

inside `on_set_state()`, so the intent - reset the Delete input checkboxes when the gizmo opens or closes -
never happened and the checkbox carried over from the previous operation. The locals are gone and the
members (plus the new mode flag and the outcome) are reset where the code clearly meant to.

**F. MCP tools must refresh the gizmo data after a hand-written selection.** Not part of the reported bug,
but found by it: it is what damaged the owner's live project while I was diagnosing.

Setting a `Selection` by hand (`sel.clear(); sel.add_object(oid, true)`) bypasses the canvas path that
calls `GLGizmosManager::update_data()` - `GLCanvas3D` does it after a real click, and
`GizmoObjectManipulation.cpp:639` does it after the same kind of hand-written change. Without it a gizmo
that is already open keeps working against the **previous** object: `SelectionInfo` and the raycaster still
point at it, so a pick that names a volume by index resolves against the old volume list and the operation
lands on the wrong object - while every report in those tools reads the canvas selection and therefore
names the new one. That is how a scripted Mesh boolean on a duplicate came to modify the original instead.
`refresh_gizmo_data()` is now called at **13** sites in `OrcaMCPGizmoTools.cpp`, one per hand-written
selection, so the whole MCP surface re-targets correctly.

## Verification

- Build clean: `build_win.bat -s --tests`, then an incremental `-s --no-configure` after the reporting fix
  below. No unit test: this is gizmo behaviour and the suites do not cover the panel.
- Runtime over MCP, **on a duplicate clone with `object_id` on every call** (the discipline F's bug
  demands):
  - `pick_source` naming the cutter -> the reported mode becomes **difference** and the pick lands in the
    tool slot; `pick_source` naming the part fills the source. The status report prints the picks under its
    own keys, so the proof is the run itself.
  - one `run {difference, source 0, tool 1}` -> `performed true`, `no_op false`, `triangles_in 29284`,
    `triangles_out 42072`, `volumes_before 2`, `volumes_after 1`, and the clone ends with one volume named
    `Cube - difference` carrying the hole. **The owner's object 0 was untouched** (29 284 + 256), which also
    proves F: the operation acted on the object that was named.
  - the reversed call `run {source 1, tool 0}` is now **refused** - `applied false`, `performed false`,
    warning "Pick a source volume and a tool volume", nothing consumed - where before it replaced the
    cutter with a 12 796-triangle leftover.
- **Not exercised: the `no_op` branch.** It needs a Difference whose tool misses the source, and there is
  no per-volume move over MCP (`transform_objects` is object-level) and no fixture that provides one. Its
  logic is an exact-copy comparison (same triangle count and volume), so it reports only when the kernel
  really handed the source back. On the owner's checklist: move the placed hole off the part (Object List)
  and run Difference - it should report `no_op true` with the warning, and keep both volumes.
- One bug found in the reporting itself while checking: a *refused* operation reported
  `after_volumes 0`, which reads as the object having lost its volumes. Fixed: the outcome starts with
  `volumes_after = volumes_before`, so a refusal reports both counts unchanged.

## Deliberately not done

- **Letting the UI pick a volume no camera ray reaches** (the Object List's selected volume as the pick).
  The MCP surface can pick by index; the panel cannot, which is why a buried cutter is unpickable. This is
  the follow-up with the most value.
- **Making "Place hole on face" protrude further** so its cutter is a bigger click target. The cutter is
  authored to poke 0.5 mm above the face, which is what makes it a 2-3 pixel target.

## Files

| Kind | Path |
|------|------|
| Gizmo | `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.{hpp,cpp}` |
| MCP | `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` |

Marker `[ORCAPORT:MB-1]`.
