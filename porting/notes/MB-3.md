# MB-3 — merging only the selected parts of one object into one part

Status: **implemented, build clean, libslic3r suite green, runtime-checked over MCP**. Branch `port/MB-3`,
off `port/integration`. Not merged and not pushed - the user's test is the gate.

## The request

> "if an item in OrcaSlicer has 5 parts, i want to be able to select 2 of the parts then click assemble and
> those two become 1 part and we have an item with 4 parts now" - without assembling the whole object.

Until now the right-click **Mesh boolean** was all-or-nothing: it took `obj_idxs.front()`, ignored the
selected part indices, unioned **every** model part, subtracted every negative volume, deleted the object
and added a new one (`ObjectList::boolean`, `Plater::combine_mesh_fff`). With two part rows selected the
item was not even offered, because `can_mesh_boolean()` went through `get_selected_obj_idx()`, which returns
`-1` for more than one selected item. Nothing merged a subset, and nothing merged *in place*: the existing
"parts to a single part" helpers (`ModelObject::merge()`, the dead BBS `ModelObject::merge_volumes`) either
concatenate meshes without applying the parts' transforms or build a new object.

## Design

One new action on the multi-part selection: **Merge selected parts**. It unions exactly the selected parts
and replaces them in the object; the object itself, its instances, its transform, its object settings and
every part that was not selected stay untouched.

- **One core function**: `OrcaExt::merge_parts_into_one` (`src/libslic3r/OrcaExt/MergeParts.*`) so the
  behaviour is testable without the GUI. It unions the selected `MODEL_PART` volumes in the **object's own
  coordinate system** (each part's `get_matrix()` applied, no instance transform), which is what lets the
  result stay inside the same object.
- **The kernel is the existing one**: `csg::check_csgmesh_booleans(range, use_mcut = true)` then
  `csg::perform_csgmesh_booleans_mcut`, exactly the pair `Plater::combine_mesh_fff` uses after MB-2 aligned
  the pre-flight with the kernel. When the kernel refuses (or returns nothing) the parts are **joined
  unwelded** instead - the object still ends up with one part - and the user is told; the parts are never
  silently lost.
- **Placement**: the merged part takes the **lowest selected index**, so the part order stays stable, and
  inherits that part's name and per-part settings (the other selected parts' settings go away with them -
  one part can only carry one set).
- **No re-centering**: the volume is added with `modify_to_center_geometry = false` because its geometry is
  already in object space, so its transform stays identity.
- **Parts are removed with a raw delete loop**, not `ModelObject::delete_volume`: that helper bakes the last
  remaining volume's transform into the instances when the object drops to one volume, which would be wrong
  in the middle of this replacement.
- **Painting** is carried over when the `keep_painting` app preference is on: saved per selected part in
  object space and remapped onto the merged mesh, the same idea the whole-object boolean uses.

Deliberately gated off (`ObjectList::selected_parts_can_be_merged`): parts of **different objects**, parts of
a **cut object** (its cut info and connectors point at those volumes), **text/SVG** parts (fusing the mesh
would lose the properties that make them editable) and **negative/modifier** volumes (they are not solid
parts; they keep subtracting from the merged part where they overlap it).

## Where it hooks in

| Kind | Path | Anchor |
|------|------|--------|
| Core | `src/libslic3r/OrcaExt/MergeParts.{hpp,cpp}` | new files |
| Core | `src/libslic3r/CMakeLists.txt` | source list entry |
| GUI | `src/slic3r/GUI/GUI_ObjectList.cpp` | `boolean()` takes a partial branch; new `merge_selected_parts()` + `selected_parts_can_be_merged()`; `can_mesh_boolean()` also accepts a part selection |
| GUI | `src/slic3r/GUI/GUI_ObjectList.hpp` | the two new private methods |
| GUI | `src/slic3r/GUI/GUI_Factories.cpp` | "Merge selected parts" in the multi-selection (part) menu, gated by `can_mesh_boolean()` |
| MCP | `src/slic3r/GUI/OrcaMCP/OrcaMCPServer.cpp` | `boolean_parts` gained the optional `part_ids` |
| MCP | `scripts/tools_schema.py` | regenerated (`python scripts/regen_tools_schema.py`) |
| Test | `tests/libslic3r/test_model.cpp` | 4 cases |

Marker `[ORCAPORT:MB-3]`. The `boolean()` branch reuses the existing menu item's entry point, so the menu
item and the MCP tool run the same code.

One trap this hit: selecting two part rows programmatically (`ObjectList::select_items`) without the list's
part selection mode makes `fix_multiselection_conflicts()` pop a modal **"Selection conflicts"** dialog; the
MCP path never returns from that (a modal blocks the main-thread queue). The MCP handler now sets
`smVolume` first, which is what a real Ctrl-click sets anyway.

## Verification

- Unit (`tests/libslic3r/test_model.cpp`, `[Model]`, 4 new cases / 24 assertions; 15 cases / 84 assertions
  in the tag):
  - three parts, merge the first two -> 2 volumes, merged part keeps index 0 and the first part's name,
    the untouched part keeps its world bounding box, the object still has one instance, the merged volume
    transform is identity and its bounding box spans **both** parts (`[0,15]` on X for cubes at 0 and 5) -
    this is the case that fails if the parts' transforms are dropped.
  - the only two parts -> one volume spanning both.
  - two parts that share a **whole face** (the cut-in-half case) -> one volume spanning both.
  - refusals: a single index, and a list containing a `NEGATIVE_VOLUME`, leave the three volumes untouched.
- Full `libslic3r_tests.exe`: **1728 cases / 109 402 assertions passed**.
- Runtime over MCP on a cut object (a 20 mm cube split twice with `keep_as_parts`, so 3 parts):
  `boolean_parts(object_id 0, part_ids [0,1])` -> `status ok`, `volumes_before 3`, `volumes_after 2`,
  `objects_before 1`, `objects_after 1`, `result_object_id 0` (the object was **kept**, not replaced),
  `merged_part_ids [0,1]`, `boolean_failed false`, `negative_parts 0`; the part list then reads
  `obj_A_A` + `obj_B_B`, and `slice_all` finishes `idle` with only the profile's usual precise-wall warning.
- The endpoint returned `volumes_after 1` on the first attempt because the bridge's cached tool schema
  (`scripts/tools_schema.py`) still had the old `boolean_parts` input and stripped `part_ids`, so the
  whole-object path ran. Regenerating the schema fixes that; the schema file is part of this change.

## Known limits / deliberately not done

- Parts of **different objects** are not handled; assemble the objects first, then merge the parts.
- The union drops the other selected parts' per-part settings and any per-part painting that
  `keep_painting` cannot remap; painting remap on a heavily painted union is best-effort, as in the
  whole-object boolean.
- `get_print_estimate` / `export_gcode` over MCP wedge the app **after slicing** in this build; that happens
  without this feature too (reproduced on a plain two-part cut with no merge), so it is not part of MB-3 and
  is left for a separate investigation.
