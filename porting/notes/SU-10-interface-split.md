# SU-10-interface-split - contact overrides must not swallow the interface layer

- Source: our own bug, reported from a slice of an organic (tree) multimaterial support test.
- Branch: `port/SU-10-interface-split`, based on `port/PF-10-paint-marks`.
- Category: B (fix). Status: **implemented, build clean, G-code verified; print pending.**

## Symptom

With `support_type = tree(auto)` and a different support-base / support-interface filament, most
support-interface layers printed with the **contact** line width and speed (0.32 mm / 55 mm/s)
instead of the interface values (0.42 mm / 120 mm/s). The woven interface (SU-9) also never
appeared on a sloped overhang, only on a handful of layers.

Measured before the fix, from the exported G-code of the test model:

| | layers |
|---|---|
| layers carrying a support interface | 379 |
| of those, contact-only (0.32 mm) | **350** |
| of those, containing a real interface layer (0.42 mm) | 29 |

## Cause

Organic support places a roof **contact** on every layer of a sloped overhang, so at most print_z a
`TopContact` and the `TopInterface` of the roof above coexist. In `generate_support_toolpaths`:

```cpp
// SupportCommon.cpp
if (top_contact_layer.could_merge(interface_layer) && ! raft_layer)
    top_contact_layer.merge(std::move(interface_layer));
```

`could_merge()` (`SupportGeneratorLayerExtruded`) compares **height and bridging only** - no layer
type, no material. Both layers therefore merged: the interface polygons were appended to the contact
layer and the interface layer was emptied. Everything then went through
`extrude_interface(top_contact_layer, InterfaceLayerType::TopContact)`, the only path that applies
`support_interface_contact_line_width` and the contact flow, and the layer was flagged
`support_contact_top`, whose speed override was applied to **every** support path in the layer
(`GCode.cpp`, `is_support(path.role())`).

Two consequences, both reported by the user:

1. Interface geometry printed at 0.32 mm / contact flow / 55 mm/s.
2. `interface_layer` was empty when the SU-9 weave stage ran
   (`iface_host_top = !interface_layer.empty() && layer_type == TopInterface`), so no strips were
   ever split on those layers.

Classic supports never hit this: contact and interface never share a print_z there.

Instrumented confirmation (temporary dump of the toolpath stage, since reverted) at z = 44.45:

```
contact=12746573 (type 7 TopContact)  iface=46950464 (type 6 TopInterface)  base_iface=18851125
```

i.e. the interface was 3.7x the contact and still lost.

## Fix

1. `SupportCommon.cpp`: never merge a `TopInterface` into a `TopContact`. Trim the interface against
   the contact instead (`diff`) so nothing is extruded twice, and keep it as its own layer. Other
   layer types keep the old merge, which exists for non-synchronized contacts.
2. `Layer.hpp` + `SupportCommon.cpp` + `GCode.cpp` + `GCode.hpp`: record the contact **footprint**
   per layer (`support_contact_top_area` / `support_contact_bottom_area`) and apply the contact
   speed only to paths that touch it, the same way `transition_area` already works (SU-8). An empty
   or absent footprint keeps the old layer-wide behaviour.

## Verification

- Build: `cmake --build build --config Release --target OrcaSlicer` -> 0 errors.
- G-code, same project, same settings, before -> after:

| | before | after |
|---|---|---|
| interface moves at 0.42 mm / 120 mm/s | 3 491 | **12 072** |
| interface moves at 0.32 mm / 55 mm/s | 23 812 | 11 107 |
| layers contact-only | 350 | **28** |
| layers with a real interface layer | 29 | **351** |

- Layer 221 (the surface in the report), before: `{0.32/55: 346}`. After:
  `{0.42/120: 135, 0.32/55: 100}` - the contact region keeps the contact values, the interface
  around it gets interface values.
- Woven-flag layers: 20 -> 22. Unchanged by design, see the gap below.

## Still open (needs a decision)

SU-9 marks the woven layer as **the lowest interface layer of each z-contiguous run**
(`SupportCommon.cpp`, `weave_layer`). Because a sloped overhang produces a contact on every layer,
one run spans the whole slope (on this model: 379 interface layers collapse into 21 runs), so only
one layer per slope is woven. Marking per contact instead would weave roughly every interface layer
(~350 instead of 22) - a large change in material mixing and tool changes, so it is deliberately
not in this patch.

## Files

- Modified: `src/libslic3r/Support/SupportCommon.cpp`, `src/libslic3r/Layer.hpp`,
  `src/libslic3r/GCode.cpp`, `src/libslic3r/GCode.hpp`
- Patch: `porting/patches/85_SU-10-interface-split.patch`
