# SU-8-followup - object-on-interface transitions reach tree supports

- Source: our own bug, found by triaging a slice of an organic multimaterial test.
- Branch: `port/SU-8-object-transition`, based on `port/integration`.
- Category: B (fix). Status: **implemented, build clean, G-code verified; print pending.**

## Symptom

`transition_object_interface_*` (the "object on interface" joint: speed, flow, fan, temperature
delta on the object layer that rests on a support contact) never applied with tree/organic
support. On the test model the G-code had no `;SU8_FAN` marker, no temperature delta and no slowed
object layer at all, while `transition_object_interface_enable = 1`.

## Cause

`SupportCommon.cpp`, joint 2 tagging, skipped every contact that does not know its object layer:

```cpp
for (const SupportGeneratorLayer *l : top_contacts) {
    if (l == nullptr || l->polygons.empty() || l->idx_object_layer_above == size_t(-1))
        continue;
```

`SupportGeneratorLayer::idx_object_layer_above` is written only by the classic engine
(`SupportMaterial.cpp:2076`, `:2094`). The tree engine allocates its contacts through
`InterfacePlacer::add_roof_unguarded` -> `layer_allocate_unguarded` -> `layer_initialize`
(`TreeSupportCommon.hpp`), which sets `print_z`, `bottom_z` and `height` only, so the field keeps
its `size_t(-1)` default and every organic / slim / strong / hybrid contact was skipped. Nothing was
ever tagged, so `GCode::process_layer` read `object_layer->transition_joint == 0` and none of the
four values was consulted.

Measured on the test model (`tree(auto)`, organic, 537 layers, `layers = 2`): 506 of 518
interface-carrying layers carry a top contact, and ~519 distinct object layers would have been
tagged. Actual: 0.

## Fix

Derive the object layer from the contact's `print_z` when the field is unset, in the same place
(`SupportCommon.cpp`, joint 2 block):

- `layer_initialize` makes a support layer's `print_z` its **top**, so the object layer above it is
  the first object layer with `print_z > contact.print_z + EPSILON` (`std::upper_bound` over
  `PrintObject::layers()`). That also holds with a non-zero `support_top_z_distance`, and it agrees
  with what the classic engine records.
- With `layers > 1` two contacts reach the same object layer, so the footprint is now accumulated
  (`transition_area` unioned) instead of the later contact replacing the earlier one.

Classic supports are unaffected: their contacts already carry the index, which is used as before.

## Verification

- Build: `cmake --build build --config Release --target OrcaSlicer` -> 0 errors.
- `ctest --test-dir build/tests/fff_print -C Release` -> **604/604 passed**, including the new
  `[SupportMaterial]` case "An organic contact tags the object layer above it for the
  object-on-interface treatment" (slices `support_capital()` with `tree(auto)` + organic and
  requires the `;SU8_FAN` marker).
- Same project, same settings, before -> after this patch:

| signal | before | after |
|---|---|---|
| object layers whose outer wall median is 200 mm/s (50 % of 400) | 1 | **134** |
| `M109 S240 T3` / `M109 S255 T1` (base + the configured +10) | none | **present** |
| `;SU8_FAN` markers | n/a (fan = -1 in this project) | n/a |

## Caveat

The joint now applies on **every** object layer that rests on a contact - on an organic model that
is most of the print. With `layers = 2` and a fan override, part cooling is forced for a large share
of the object, and the 50 % speed applies inside the contact footprint on those layers. That is what
the settings ask for, but check the preview before printing; a fan value of `-1` disables the fan
part and `100`/`-1` the speed part.

## Files

- Modified: `src/libslic3r/Support/SupportCommon.cpp`, `tests/fff_print/test_support_material.cpp`
- Patch: `porting/patches/86_SU-8-object-transition.patch`
