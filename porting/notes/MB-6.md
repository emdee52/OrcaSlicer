# MB-6 — Merge selected parts can subtract negative volumes

Status: **implemented, build clean, libslic3r suite green, runtime-checked over MCP**. Branch `port/MB-5-6`
(batch with MB-5), on `port/integration` after MB-3/MB-4. Not merged and not pushed.

## The request

> "merge selected parts option to be able to difference out positive parts with negative parts: 5 part item
> with 2 positive and 3 negative, select 1 positive and the 3 negatives - 2 inside the positive, 1 outside -
> then the 2 inside are applied and consumed, the outside one is consumed without affecting anything, and we
> are left with the new merged/differenced positive part."

## Design

MB-3's `OrcaExt::merge_parts_into_one` only accepted `MODEL_PART` volumes and unioned them. It now:

- accepts `MODEL_PART` **and** `NEGATIVE_VOLUME` in the selection; anything else (parameter modifiers,
  support enforcers/blockers, Precise Seam volumes) is refused whole, because a modifier has no CSG meaning;
- requires at least one positive and a reason to run (two or more positives, or a positive plus a negative);
- builds the CSG list **positives first (Union), then negatives (Difference)** - order matters, a leading
  Difference would cut nothing - in the object's own coordinate system, and runs the same mcut pipeline the
  whole-object boolean uses;
- puts the result at the **lowest selected positive's index** with that part's name and per-part settings;
- **deletes every selected negative**, whether or not it intersected the result (the "outside one is consumed
  without affecting anything" rule the user asked for). Unselected volumes are untouched;
- if the kernel refuses **and** negatives were selected, aborts and changes nothing (deleting cutters whose
  cut was not made would silently lose the holes). A positives-only selection keeps MB-3's unwelded-join
  fallback;
- painting is remapped from the **positive** volumes only (negatives carry no paint).

The Object List gate `selected_parts_can_be_merged()` mirrors the same rule, so the menu item and the MCP
tool offer exactly what the core accepts, and a text/SVG positive is still refused (fusing it would lose the
properties that make it editable).

## Verification

- Unit (`tests/libslic3r/test_model.cpp`, `[Model]`):
  - 20 mm part + a 4 mm cutter centred in it + a 4 mm cutter 100 mm away, all three selected → **one volume
    left**, a `MODEL_PART`, with `its_volume` = **7936 mm³** (8000 − 4³): the inside cutter cut, the outside
    one was consumed with no geometry effect. This is the requested scenario exactly.
  - negatives without a positive, a single positive alone, and a selection containing a modifier are all
    refused, and the object is unchanged.
- Full suite: **1736 cases / 109 493 assertions passed** (the MB-3 tests still pass).
- Runtime over MCP: an object with two parts plus a `NEGATIVE_VOLUME` created by the Extrude tool on its top
  face (`mesh_boolean_gizmo status` shows `part`, `part`, `negative`), then
  `boolean_parts(object_id 1, part_ids [0, 2])` →
  `volumes_before 3` → `volumes_after 2`, `objects_before == objects_after == 2` (the object kept, id 1),
  `boolean_failed false`; the cut part grew from 36 to 92 triangles (the pocket walls), the untouched part
  stayed at 28, and `slice_all` finished `idle`.
- Expected warning in that run: "is too close to others" - the two objects still share a footprint.

## Known limits / deliberately not done

- Modifiers cannot take part in the boolean; they are refused rather than ignored.
- A selected negative that lies outside the merged part is deleted as requested, so it also stops cutting
  anything else in the object. That is the semantics the user asked for; it is not an "only intersecting"
  filter.
- The `boolean_parts` report's `negative_parts` count comes from MB-2's whole-object path and stays 0 on this
  partial path; `negatives_before` in the same response is the honest count for the selection.
- Kernel refusal with negatives selected cannot be provoked on demand; that path is covered by the code rule
  and by the MB-2 no-op report knowledge, not by a unit test.
