# MB-5 — moving selected volumes into one new object

Status: **implemented, build clean, libslic3r suite green, runtime-checked over MCP**. Branch `port/MB-5-6`
(a batch with MB-6), built on `port/integration` after MB-3 and MB-4 were merged. Not merged and not pushed -
the user's click test is the gate.

## The request

> "one option to split selected parts into object aka if i select 2 parts of a 5 part item, and click the
> option it will create 2 items, the original 5 part item with now 3 part and a new object with the 2
> selected parts. this should also include negatives, modifiers, etc as well."

So: 5 parts, 2 selected → source keeps 3, **one** new object holds both selected volumes. Negatives,
modifiers, support volumes and Precise Seam volumes travel with them because they are moved as volumes, not
merged. (MB-4's per-part action stays; the user chose "keep both".)

## Design

- **Core** `OrcaExt::split_volumes_to_one_object(ModelObject&, vol_idxs, new_object_idx, error)` in
  `src/libslic3r/OrcaExt/SplitParts.*`:
  - validates: non-empty, indices in range, and at least one volume remains in the source (refuses to leave
    the object empty);
  - creates one `ModelObject` in the same model named `<source>_parts`, copying the source object config and
    **all** of its instances;
  - **re-parents** the selected `ModelVolume`s instead of copying them (new
    `ModelObject::move_volume_to(ModelObject&, size_t)`), so type, name, per-volume settings, painting, text
    metadata and ID are the very same objects - nothing can drift, and no ID is duplicated;
  - when the source is left with a single volume, folds that volume's transform into the instances (the
    same invariant `ModelObject::delete_volume` maintains), then clears the source's `input_file`.
- **Why the new Model method**: `ModelVolume::set_model_object()` is protected and its friend is
  `ModelObject`, so re-parenting has to happen inside `ModelObject`. Six lines, marked `[ORCAPORT:MB-5]`.
- **Gate** `selected_volumes_of_one_object()` in the Object List: all selected rows are volume rows of one
  object, not a cut object (its cut info and connectors point at the volumes); any volume type allowed.
  `can_split_selected_volumes_to_object()` additionally requires that something stays in the source.
  MB-3's merge gate and MB-4's split gate now share this helper and add their own type rules.
- **UI**: `Modify → Split selected parts to an object`, next to MB-4's action in the part menu (single
  selection) and the multi-selection menu. MB-4's item was renamed from "Split selected parts" to
  **"Split selected parts to objects"** so the two read apart.
- **MCP**: `split_parts` gained `one_object: true` (same tool - the actions differ only in where the volumes
  land), and its selection-mode fix is reused.

## Verification

- Unit (`tests/libslic3r/test_model.cpp`, `[Model]`): source with 2 parts + 1 negative + 1 modifier; move
  the part, the negative and the modifier into one object →
  - source keeps exactly the one part that was not selected, in place (`[30,40]` on X);
  - the new object is named `<source>_parts`, holds **3** volumes in source order with the original types
    (`MODEL_PART`, `NEGATIVE_VOLUME`, `PARAMETER_MODIFIER`), names and per-volume settings (an `extruder`
    set on the part survives), and the volume IDs are unchanged (the volumes themselves moved);
  - every moved volume's world bounding box equals its old one;
  - moving every volume is refused and changes nothing.
- Full suite: **1736 cases / 109 493 assertions passed**.
- Runtime over MCP on a cube cut into 3 parts (`split_parts(0, [1,2], one_object=true)`):
  `objects_before 1` → `objects_after 2`, source `volumes_before 3` → `volumes_after 1`,
  `new_object_ids [1]`, new object `20mm_cube.obj_parts` holding the two moved parts (the part list shows
  both), `slice_all` → `idle`. The expected warning: the moved object still shares the source's footprint
  ("is too close to others") because the parts were one solid a moment before.

## Known limits

- Parts of different objects at once; cut objects (cut info/connectors), as in MB-3/MB-4.
- The new object joins the current plate via `add_objects_to_list`; layer ranges and brim ears stay on the
  source (as in MB-4).
- Volume order inside the new object follows the source order, not the click order.
- No option to rename the new object yet: it is `<source>_parts`.
