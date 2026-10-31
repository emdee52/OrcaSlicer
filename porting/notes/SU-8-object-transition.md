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

## Follow-up: the footprint is the printed contact, and a path has to be *on* it

The first version of this fix tagged the object layer with `object lslices ∩ nominal contact
polygons`. On organic support the nominal contact polygons cover a whole overhang while the branch
under it may touch a small patch, and `GCode::_extrude` treated a path as "in the footprint" when
**any** of its points was inside - so one small contact patch slowed the whole perimeter loop and the
whole layer:

| | slowed object moves (at exactly 50 % of the role speed) |
|---|---|
| nominal contact + "any point" | 41 581 (outer wall 28 095) |

Two changes:

1. The footprint is now the **printed** contact of that print_z. The toolpath stage records
   `top_contact_layer.polygons_to_extrude()` per layer (`contact_footprint`) and the joint-2 pass
   runs after it, intersecting the object layer's `lslices` with that.
2. A path is treated when **at least half of its points** are inside the footprint, not when any
   point is. A perimeter loop or infill line that merely clips the patch keeps the normal speed.

Result on the same project: 41 581 -> **2 093** slowed moves, outer wall 28 095 -> **150**, slowed
layers 135 -> **1**. The temperature delta is untouched (it is a layer property): the +10 commands
(`M109 S240 T3`, `M109 S255 T1`) still appear on 34 layers.

The residual 2 093 are the paths that really sit on the contact; the cost of the setting is now
proportional to the contact area instead of the layer area.

## Follow-up: fan is scoped to the same footprint

The fan was a layer-wide override: `_extrude` wrote one `;SU8_FAN <pct>` marker per layer and
`CoolingBuffer::process_layer` forced that value on every `M106` it rewrote, disabling the overhang /
support-interface / ironing markers for the whole layer. It is now a zone:

- `_extrude` emits `;_SU8_FAN_ZONE_BEGIN <pct>` / `;_SU8_FAN_ZONE_END` around the extrusions that
  touch the footprint, on state change only, and closes the zone at the end of the layer
  (`GCode::process_layer`). Fan uses **any point inside** rather than the majority rule: fan is
  airflow over the area, not a per-path material change, so merely crossing the contact counts.
- `CoolingBuffer` parses the two markers, emits `set_fan(pct)` on begin (same PWM floor clamp as the
  rest of the buffer) and restores its computed layer fan on end, with the zone winning over the
  layer fan and every role marker while it is open. The markers never reach the file.
- The layer-wide `;SU8_FAN` marker and its override are gone.

Verified: with `fan = -1` the exported G-code is **identical** to the previous build (only the
`; generated by ...` timestamp line differs); with `fan = 7` the output carries `M106 S17` where the
object rests on the contact and no markers. Test: "An organic contact tags the object layer above it
for the object-on-interface fan" (`[SupportMaterial]`), 604/604.

Limits worth knowing: the part fan is one airflow, so switching it over a small patch changes *when*
air is blown, not what the previous layer experienced; and the transition temperature cannot follow
this granularity - nozzle temperature is a whole-layer property.

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
