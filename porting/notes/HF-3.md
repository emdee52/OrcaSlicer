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

The cavity-fill gizmo becomes an **Extrude** tool with two modes, chosen by a segmented selector at
the top of the panel. **Extrude face** is the default.

| Mode | What it does |
|---|---|
| **Extrude face** | Click a flat face to **pick** it, then Height/Offset reshape a live preview, then the **Extrude** button commits it as a solid. |
| **Cavity fill** | Unchanged from HF-1: paint the recessed faces of an engraving/watermark and each facet is measured against the surface fitted to its own neighbourhood, so the plug sits flush and follows a curved or coarsely facetted wall. |

Both add an ordinary positive `MODEL_PART` volume, in object space, so one undo removes the result.

## UI rework (2026-10-17) - the pick/adjust/create workflow

The first cut extruded on **click**, using whatever Height/Offset happened to be set, which is no way
to dial a solid in. Reworked to the fork's workflow and its own style:

- **Click picks, the button commits.** `on_mouse` no longer extrudes. `pick_at()` stores the pick;
  `create_from_pick()` (the panel's **Extrude** button, right-aligned in the accent colour) commits
  it. Right-click and `Esc` drop the pick. `gizmo_apply_hovered()` / `gizmo_apply_at()` are kept as
  MCP aliases that pick-then-create in one call, so existing scripts still work.
- **Three highlights, in the fork's own RGBA** (lifted from its Align & Construct preview, not
  eyeballed): **teal** `0.10,0.80,0.74,0.55` the face under the cursor, **green**
  `0.16,0.85,0.30,0.60` the picked face, **orange** `1.00,0.55,0.00,0.55` the solid you would get.
  The green face shows a margin around the orange solid whenever the offset is negative - that is the
  feature, not decoration.
- **The object is ghosted** while a face is picked (the volumes are pushed towards a dark grey and
  made translucent). Reuses the Support Zones discipline: **restore first, always**, or a second call
  saves an already-tinted colour as "the original" and the scene fades to nothing; restored on gizmo
  close and on any object/mesh change.
- **Both modes share one panel style** - fixed 360 px width, wrapped hints, a label column,
  slider+typed field with no `[-][+]` buttons, a switch for *Both directions*, segmented buttons for
  the mode and for *Face | Holes*. This is the fork's chrome and the first use of the shared widget
  layer in `GizmoStyle.hpp`; the standard and the migration list are in `docs/handoff.md` section 28.
- **Holes is a segmented pair with Face**, not a checkbox, matching the fork's panel.

### Perf: the pick is cached

`build_pick()` computes the expensive half once per pick - facet normals and neighbours (cached per
volume, O(mesh)), the coplanar region, the outline and its holes. Height/Offset then only re-run the
prism build (`picked_prism()`). Before this, `face_extrude_mesh()` rebuilt normals + region + outline
**twice a frame** (once for the preview, once for the panel's "no face here" message), which would
have made a live slider drag crawl on a dense mesh. The panel now reads a cached `m_pick_why`.

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

- **Build clean** (11m23s first pass, 15m17s after the UI rework), no errors.
- **`ctest` 2407/2407 passed** (both passes).
- Still to do, all runtime: pick a face and confirm the teal hover and green pick; drag Height and
  Offset and confirm the orange solid follows live and the green margin opens up as the offset goes
  negative; press **Extrude** and confirm the volume appears; check **Holes**; check **Both
  directions**; check a face on an angled or curved part; check the object is ghosted while a face is
  picked and returns to normal on close; check the cavity mode still behaves as before; check the Cut
  tool still ignores fill volumes; and reopen a project saved before the rename and confirm its
  `HoleFill#` plugs are still listed and removable.

## Traceability

`porting/patches/52_HF-3.patch` reverse-applies cleanly. Manifest row `HF-3`. The `HF-1` row still
describes HF-1's kernel and is superseded for the tool identity by this row.
