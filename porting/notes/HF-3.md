# HF-3 - Extrude gizmo (extrude a flat face, or fill a cavity)

- Source: `NEOTKOCM_RELEASE_2_48.md` (the Align & Construct `SurfaceExtrude` engine), plus the user's
  own design decision to fold the existing cavity-fill tool into it.
- Fork: `OrcaFS-NeotkoCM` @ `2c41b67aa3` (2.4.8) for the geometry. Base for this diff: Snapmaker
  `v2.4.0`.
- Category: **our own design work, not a port** (plan Addition A). Status: **implemented** - build
  clean, `ctest` 2407/2407; runtime check pending.
- Branch: `feat/extrude-fill` (not a `port/` branch - this is our feature, not a Neotko port).
- Markers: `[ORCAPORT:HF-3]`, plus the original `[ORCAPORT:HF-1]` where the code is HF-1's.

## What it is

The cavity-fill gizmo becomes an **Extrude** tool with two modes, chosen by a selector at the top of
the panel. **Extrude face** is the default.

| Mode | What it does |
|---|---|
| **Extrude face** | Click a flat face and it becomes a solid. The face is grown into a coplanar region, projected to a polygon outline, and closed into a prism. |
| **Cavity fill** | Unchanged from HF-1: paint the recessed faces of an engraving/watermark and each facet is measured against the surface fitted to its own neighbourhood, so the plug sits flush and follows a curved or coarsely facetted wall. |

Both add an ordinary positive `MODEL_PART` volume, in object space, so one undo removes the result.

### Extrude-face controls

- **Height** - how thick the new solid is. Clamped to 0.05-200 mm.
- **Offset** - grows (+) or shrinks (-) the outline before extruding. Clamped to -20..20 mm; if it
  eats the whole shape the tool says so instead of adding nothing silently.
- **Holes** - extrude only the face's holes, each one filled. E.g. the floor of a screw hole becomes
  a peg you can grow with Offset, then carve a bigger hole with a boolean.
- **Both directions** - splits the height: half grows out of the face, half goes into the model, so
  the solid passes through the face without being moved.

## The geometry (new module)

`src/libslic3r/SurfaceExtrude.{hpp,cpp}`, ported from the fork's `NeotkoSurfaceExtrude`:

| Function | Job |
|---|---|
| `coplanar_region(normals, neighbors, seed, cos_thresh)` | flood-fill facets connected to the picked one whose normal stays within `acos(cos_thresh)` of it (we use ~2.6 deg) |
| `region_outline(its, region, trafo, mean_z)` | project the region's triangles into `trafo`, union them into an outline (holes kept), and report the region's mean Z. Welds hairline seams with a 0.01 mm closing |
| `outline_holes(outline)` | the holes of the outline, each one as a filled shape |
| `extrude_outline(outline, offset_mm, z0, height)` | closed prism: one slice = one slab, via `slices_to_mesh` |

**Not ported:** the fork's `s363c` half (curved extrusion with tension nodes: `CurveNode`,
`sample_curve`, `curve_value`, `extrude_outline_curved`). The fork marks it "hidden, debug only,
undocumented on purpose". Take it only if the user asks.

**Frames:** the picked face's normal is turned to +Z with `Quaterniond::FromTwoVectors` (a pure
rotation, so nothing is distorted), the prism is built there, and the result is turned back. The
whole calculation is done in **object space** (`volume matrix`), which is the same space HF-1's
`fill_mesh` returns its plug in, so `add_named_fill` treats both modes identically.

## Rename (as requested)

| Before | After |
|---|---|
| `GLGizmoHoleFill.{hpp,cpp}`, class `GLGizmoHoleFill` | `GLGizmoExtrude.{hpp,cpp}`, class `GLGizmoExtrude` |
| `EType::HoleFill` | `EType::Extrude` |
| display name "Cavity fill" | "Extrude" |
| `Shortcut::GizmoCavityFill`, app key `gizmo_cavity_fill` | `Shortcut::GizmoExtrude`, key `gizmo_extrude` |
| MCP tool `hole_fill_gizmo` | `extrude_gizmo` |

The shortcut **chord stays `G`**. A user who had rebound the old shortcut loses that binding (the
app key changed); nothing else is affected.

### Backward compatibility (.3mf) - the important part

Volumes are now written as `Extrude#<n>`. The legacy **`HoleFill#` prefix is still recognised on
read**, so fills made before the rename stay listed, editable and removable. Three places had to
accept both:

- `is_fill_volume()` in the gizmo (drives listing, removal and the "no cavity here" test),
- `next_feature_id()`, which counts both prefixes so a new id can never collide with an old name,
- the two external filters for the prefix: `GLGizmoCut`'s "fill volumes are not the object" test
  (which also keeps `FillFromAbove#`) and the MCP `list_fills` action.

If the prefix were changed without this, reopening an old project would show its existing plugs as
ordinary model parts and they could not be removed with the tool.

## Deliberately not changed

- **The icon file names** stay `toolbar_cavity_fill{,_dark}.svg`. Renaming the assets would touch
  resource packaging and the UI-1 record for no functional gain.
- **EF-1** (edge chamfer/fillet) stays a separate tool: it is a constant-width fillet along a picked
  *edge*, with its own picking model (left-click applies, right-click removes), not a face
  extrusion. Folding it in would mean two different apply models in one panel (plan decision 7).
- **HF-2** ("Fill from above") stays in the Cut gizmo: its mechanism needs a cut plane, which the Cut
  gizmo already owns (plan decision 6).

## Verification

- **Build clean** (11m23s, configure needed for the added/removed files), no errors.
- **`ctest` 2407/2407 passed.**
- Still to do, all runtime: extrude a face with 0 / positive / negative offset; check Holes produces
  a plug; check Both splits the height around the face; check a face on an angled or curved part;
  check the cavity mode still behaves as before; check the Cut tool still ignores fill volumes; and
  reopen a project saved before the rename and confirm its `HoleFill#` plugs are still listed and
  removable.

## Traceability

`porting/patches/52_HF-3.patch` reverse-applies cleanly. Manifest row `HF-3`. The `HF-1` row still
describes HF-1's kernel and is superseded for the tool identity by this row.
