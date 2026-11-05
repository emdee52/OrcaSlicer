# MB-4 — splitting only the selected parts into their own objects

Status: **implemented, build clean, libslic3r suite green, runtime-checked over MCP**. Branch `port/MB-4`,
stacked on `port/MB-3` (it shares MB-3's selection gate). Neither branch is merged or pushed - the user's
test is the gate.

## The request

> "have 5 parts in an item, i select 3 and use the interaction and now there will be 4 items including the
> original item that has 2 parts now and the 3 items that were made into their own object."

The existing **Split → To Objects** explodes *every* part of the object and deletes the source
(`Plater::priv::split_object` → `ModelObject::split`), so there was no way to detach only some parts.

## Design

One new action on the multi-part selection: **Split selected parts**, in the right-click **Modify** submenu
next to *Merge selected parts*. Every selected part becomes its own object in the same model, at exactly the
place it already occupied, and is removed from the source object; the source keeps every part that was not
selected, its instances, its transform and its settings.

- **One core function**: `OrcaExt::split_parts_to_objects` (`src/libslic3r/OrcaExt/SplitParts.*`), testable
  without the GUI.
- Per selected `MODEL_PART`: a new `ModelObject` in the same model named after the part, object config =
  source object config + the part's config applied (a single-part object carries its settings on the object),
  one instance per source instance, the part's mesh as the only volume, and the part's painting
  (`supported`, `seam`, `mmu`, `fuzzy_skin`, `counterbore_bridge` facets) copied as-is - the triangle order
  does not change, so it stays valid.
- **Placement**: each new instance takes the composed transform `source_instance * part_transform`; the new
  volume is created centred (`modify_to_center_geometry = true`, geometry moved into the volume offset) and
  needs no transform of its own. This was the one thing to get right: `ModelObject::split` instead bakes only
  the part's *offset* into the instances, which, when tried here on a rotated part, moved it (the test
  `A split-off part keeps a rotated and translated transform` failed by exactly the part's bbox centre).
- **Removal**: the selected volumes go through `ModelObject::delete_volume()`, which also folds the last
  remaining part's transform into the instances when only one part is left - the invariant a one-part object
  needs. The source's `input_file` is cleared (it is no longer that file).
- **Refusals** (nothing changes, the caller gets a reason): no index, an index that is not a `MODEL_PART`
  (negatives/modifiers cannot become their own object), an empty part, or a selection that would leave the
  source without any model part. The last one is why the menu hides the action when every model part is
  selected - *Split → To Objects* is the action for that case (user decision).

## Where it hooks in

| Kind | Path | Anchor |
|------|------|--------|
| Core | `src/libslic3r/OrcaExt/SplitParts.{hpp,cpp}` | new files |
| Core | `src/libslic3r/CMakeLists.txt` | source list entry |
| GUI | `src/slic3r/GUI/GUI_ObjectList.{hpp,cpp}` | `split_selected_parts()`, `can_split_selected_parts()`, and the shared `selected_parts_of_one_object()` gate that MB-3's merge check now also uses |
| GUI | `src/slic3r/GUI/GUI_Factories.{hpp,cpp}` | `append_menu_item_modify()` grew a `split_parts` flag; the item sits after the Split group and before "Merge selected parts" |
| MCP | `src/slic3r/GUI/OrcaMCP/OrcaMCPServer.cpp` | new tool `split_parts(object_id, part_ids)` |
| MCP | `scripts/tools_schema.py` | regenerated (80 tools) |
| Test | `tests/libslic3r/test_model.cpp` | 4 cases |

Marker `[ORCAPORT:MB-4]`.

## Verification

- Unit (`[Model]`, 19 cases / 130 assertions in the tag):
  - one part split off a 3-part object: source keeps the other two, the extracted object is named after the
    part, holds one volume, has one instance, and its **world** bounding box (instance * volume) equals the
    part's previous world bounding box; the untouched parts did not move.
  - a part with a 90° rotation and a translation keeps its world placement (this is the case the offset-only
    approach failed).
  - a source object with two instances: the extracted part gets both, 30 mm apart, where the source had them.
  - refusals: selecting every model part, and selecting a `NEGATIVE_VOLUME`, change nothing.
- Full `libslic3r_tests.exe`: **1733 cases / 109 452 assertions passed**.
- Runtime over MCP on a cube cut into 3 parts (`split_parts(0, [1,2])`): `objects_before 1` →
  `objects_after 3`, source `volumes_before 3` → `volumes_after 1`, `model_parts_after 1`,
  `new_object_ids [1,2]`, source name kept; scene shows the three objects at z 5 / 7.5 / 2.5 (the parts'
  own centres) and each new object named after its part. `slice_all` finishes `idle`.
- **Expected new warning**: after the split the extracted parts still share the source's footprint, so the
  plate validator says *"… is too close to others, and collisions may be caused."* That is correct for
  objects that were one solid a moment ago; it is not a defect of the split.

## Known limits / deliberately not done

- Parts of **different objects** at once; cut objects (their cut info and connectors point at the volumes);
  text/SVG parts (the volume would land in a new object without the edit context it belongs to).
- Layer ranges and brim ears of the source object are not copied to the extracted objects (same as Split →
  To Objects).
- New objects join the **current plate** (`add_objects_to_list` path), not necessarily the source's plate.
- The source object's own painting is untouched by design - only whole parts move.

## Side effect on MB-3

`selected_parts_can_be_merged()` now delegates to `selected_parts_of_one_object()` (introduced here) instead
of carrying its own copy of the same checks; behaviour is unchanged (MB-3's tests still pass).
