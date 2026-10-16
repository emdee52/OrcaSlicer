# Porting plan - next phases (Support Zones 2.4.7, NeoArachne spine, unified Extrude/Fill gizmo)

**Status: PROPOSAL.** Written 2026-10-16. Nothing implemented. Open decisions in section 8.

Source of every item below: `OrcaFS-NeotkoCM` `cf1255c741` (2.4.5, our last inventory) →
`e68dcc8551` (`origin/main`, 2026-10-16), release notes `NEOTKOCM_RELEASE_2_46/2_47/2_48.md`.
The per-feature review, with the full evidence trail, is
`OrcaSlicer/porting/NEOTKO_RELEASE_2_46_2_48_REVIEW.md`.

Read with `PORTING_PLAN.md` (ground rules, ungating/uncabling policy, workflow) and
`PORTING_GUIDELINES.md` (markers, manifest, naming).

---

## 0. Sources, base, and what is already in our tree

### Source commits

| Workstream | Fork commits | Notes file |
|---|---|---|
| Support Zones | `3bd92365d0` (2.4.7) | `NEOTKOCM_RELEASE_2_47.md` |
| NeoArachne spine | `67d2415aab` (2.4.6) | `NEOTKOCM_RELEASE_2_46.md` |
| Extrude engine | `2c41b67aa3` (2.4.8), `NeotkoSurfaceExtrude.*` | `NEOTKOCM_RELEASE_2_48.md` |

### The diff base changed - this affects all three workstreams

The fork rebased from Snapmaker Orca **2.3.5** onto **2.4.0** in `7e426bff85` (845 files,
+391 500 lines, not Neotko's work). Every diff must now be taken against Snapmaker **2.4.0**
(`snapmaker/OrcaSlicer` has `v2.4.0-alpha`; its worktree is already 2.4.x).

- `AGENTS.md` still says "diff against Snapmaker `v2.3.5`". **That instruction is stale** and
  must be corrected before the first branch of this plan (section 9, task 0).
- Until it is, any per-line diff mixes 391 k lines of Snapmaker work into the comparison and
  the anchors cited below cannot be trusted line-for-line.

### Already in our tree (checked, not assumed)

- `PQ-3` NeoArachne hybrid is ported (`notes/PQ-3b.md`), with neutral `hybrid_*` keys and
  `HybridWallSource`. The fork's equivalent keys are `neoarachne_*`.
- `SU-5`/`SU-6` Support Zones are ported, base state as of 2.4.5.
- `HF-1` cavity fill is in the tree (`GLGizmoHoleFill`, `EType::HoleFill`, shortcut `G`,
  MCP `hole_fill_gizmo`, kernel `HoleShapes::cavity_fill_local`).
- `HF-2` **Fill from above** is merged and user-confirmed, and lives inside the **Cut** gizmo
  (`GLGizmoCut.cpp`, kernel `CutUtils::fill_from_above_mesh`). It has no `manifest.tsv` row.
- **The support cut is not in our tree.** `grep support_cut` finds only an unrelated stock
  `//BBS support cut` comment in `Plater.cpp:24171` (a clipboard helper). So `SU-19` is
  genuinely new work; nothing to reuse.

### ID registry - do not collide

Markers already in the tree: `AS-1..3`, `BT-1`, `CUT-1`, `EF-1`, `HF-1`, `MCP-1/2`,
`ME-1/2`, `MT-1/2/4`, `MV-1/2/3`, `PF-1/2/2b/6/9`, `PP-1`, `PQ-1/2/3`, `RF-1`, `SNAP-8`,
`SU-1/4/5/8/9/10/11/13`, `UI-1`.

- `CUT-1` = the MCP `cut_gizmo` tool, **not** the support cut.
- `SNAP-8` = `OrcaExt::Gui::ObjectSnap` (MV-2's Alt object snap).
- `SU-8..SU-13` are taken by other, already-ported support work (transition-layer fan/temps,
  woven interface, contact-width overrides, boustrophedon/perimeter interface, bottom weave).
  They are **not** the 2.4.7 zone work.

Therefore this plan uses **SU-14 … SU-19** (new, no collision), **PQ-4**, and **HF-3**.

---

## 1. Ground rules (inherited, plus three additions)

Everything in `PORTING_PLAN.md` section 0 applies: one feature = one branch = one commit
series, re-implement intent on current 2.5 APIs, behaviour-neutral at defaults (byte-identical
when off), no Libre Mode / env gate, no branding, never touch `deps/`, do not commit or push
unless asked, per-feature `notes/<ID>.md`.

**Addition A - workstream C is not a port.** The unified Extrude/Fill gizmo (HF-3) is our own
design change on top of two features we already own, plus a selective port of the fork's
`SurfaceExtrude` geometry. It must not be filed or reviewed as a Neotko port.

**Addition B - shared kernels are additive-only.** These files are used by features outside
this plan; do not move, rename or restructure them, only append:

| Kernel | File | Also used by |
|---|---|---|
| `cavity_fill_local` | `src/libslic3r/HoleShapes.{hpp,cpp}` | ME-2 (Holes), PF-2b/RF-1 (hole walls) |
| `fill_from_above_mesh` | `src/libslic3r/CutUtils.{hpp,cpp}` | the Cut tool (HF-2) |

**Addition C - volume name prefixes are load-bearing.** `GLGizmoCut.cpp:4439` skips volumes
whose name starts with `FillFromAbove#` or `HoleFill#` so its fill does not stack on top of
them. Renaming those volumes silently changes the Cut tool's behaviour.

---

## 2. Phase 8 - Support Zones bug fixes (SU-14, SU-15, SU-16)

Three fixes to behaviour **we already shipped** in SU-5/SU-6. Smallest diffs in this plan,
highest confidence, and the only items here that fix something a user can already hit.

### SU-14 - the overhang map must use the plate, not the object's own axes

- **Bug.** The red/green "what still needs holding" map and the "this zone catches nothing"
  warning look for "down" in the object's local axes, *before* the user's rotation. On a
  rotated part a flat side is marked as needing support.
- **Fork evidence.** `GLGizmoSupportZones.hpp:352` (`overhang_normal_z_cut()`) plus the
  release-note section "The overhang map follows the object's rotation".
- **Our touch points.** The zone-map/canvas projection and the per-zone warning in
  `GLGizmoSupportZones.{hpp,cpp}`. Should be a direction substitution (object Z → plate Z),
  reusing the same direction the slice uses.
- **Ungating.** None (no gate on the map).
- **Verification.** Rotate a part 90°; the map must be identical to the unrotated case; the
  "catches nothing" warning must agree with the slice. Regression: unrotated parts unchanged.

### SU-15 - painting down to the bed must make a foot, not break the tree

- **Bug.** A zone is head + stumps with nothing between. Painting down to the bed leaves no
  gap, so the stumps do nothing and it prints as a plain block.
- **Fix.** Whatever is painted below (stump floor + 1 mm) becomes a **foot**: a prism from the
  bed that the engine treats as one more stump; the head starts above it. New warning "Not a
  tree: stumps unused" when everything painted is at stump height.
- **Fork evidence.** `GLGizmoSupportZones.hpp` `struct HeadFoot` + `head_foot()`; consumed by
  the zone path in `SupportMaterial.cpp` (`zone_stumps`, `zone_bottom_layer`).
- **Our touch points.** The zone gesture data (a new foot record), the stub/stump plumbing
  shared with SU-6, and the warning.
- **Risk.** Medium: this changes the zone geometry hand-off, the least-documented part of
  SU-5/SU-6.
- **Verification.** Paint a zone to the bed: a foot appears, the head starts above it, the
  tree is a tree; paint only at stump height: the unused-stumps warning fires.

### SU-16 - a zone part that cannot reach a stump goes straight down

- **Bug.** A zone column part that cannot reach any stump, or arrives wider than its stump, is
  trimmed away and the support above it is left hanging above the bed.
- **Fix.** That part goes straight down to the bed as normal support; the log still names it.
- **Fork evidence.** `SupportMaterial.cpp` around the `n_unreached` handling (~3539-3590).
- **Touch points.** The zone corridor/stump-stepping engine in `SupportMaterial.cpp`.
- **Verification.** The ring/stand-on-edge case from the release note: support reaches the bed
  around a removed part.

**Order:** SU-14 → SU-15 → SU-16. Each its own branch and commit series, merged to
`port/integration` only after a build and the user's test.

---

## 3. Phase 9 - Support Zones new UX (SU-17, SU-18)

### SU-17 - "Overhang only" brush (new, default **on** in the fork)

- **Intent.** The brush currently marks everything in its circle (underside, edge, top). With
  this on it keeps only faces past the overhang angle (taken from *Highlight overhangs*), while
  the stroke still sweeps across walls so it is not cut short.
- **Fork evidence.** `GLGizmoSupportZones.hpp`: `overhang_only`, `overhang_cut_deg` in the
  gesture, plus `m_paint_overhang_only` / `m_paint_cut_deg` (the threshold is **frozen per
  gesture**, so reopening a zone repaints it the same way even if the global overhang angle
  changed).
- **Data model.** Two new fields in the zone gesture JSON, then a JSON **version bump**
  (ours is currently read-compatible at v4 per `notes/SU-6.md`; the fork's struct carries its
  own `version` field and an explicit note that a silent format change breaks old projects).
  Old gestures must read as `overhang_only = false`.
- **Ungating.** None. Old zones keep the off behaviour, so existing projects are unchanged.
- **Verification.** Painted zone on the release note's surfboard case: only real overhang is
  marked; a pre-change gesture reopens and paints exactly as before; version bump reads both.

### SU-18 - "View from below" button

- **Intent.** One click puts the camera under the part (same as Bottom view), because what
  needs holding faces the bed.
- **Fork evidence.** Release-note section "View from below"; an entry in the gizmo's view-icon
  row.
- **Touch points.** Gizmo view row only. Small.
- **Verification.** Click moves the camera to the bottom view; no effect on picking/state.

---

## 4. Phase 10 - Support Zones cut tool (SU-19)

New feature, new data model. This is the only item in the plan that adds a slice-time
behaviour instead of fixing one.

- **Intent.** Draw a polygon on screen; everything behind it, in the direction you are
  looking, gets no support - including columns coming down from above, which a normal support
  blocker lets through. Saved as "Support cut" in the object list, undoable. Buttons: remove
  last cut, merge cuts, apply to zones.
- **Fork mechanism (verified).** A cut is a **`SUPPORT_BLOCKER` volume carrying a boolean
  config key** (`neotko_support_cut`), not a side list:

  | Step | Fork site |
  |---|---|
  | volume created as `SUPPORT_BLOCKER`, key set, named "Support cut" | `GLGizmoSupportZones.cpp:4611-4616` |
  | screen polygon + GL overlay + close radius | `GLGizmoSupportZones.cpp:1049-1086`, `4477-4507` |
  | per-layer slicing of the cut volumes | `PrintObjectSlice.cpp:5819` `PrintObject::slice_support_cutters()` |
  | subtracted at the last point before support generation | `SupportMaterial.cpp:588-593` |
  | invalidation on key change | `PrintObject.cpp:1308` |
  | merge cuts (mesh union) | `GLGizmoSupportZones.cpp:4708` |
  | apply to zones (subtract from zones, delete the cuts) | `GLGizmoSupportZones.cpp:~4774` |
- **Neutral naming.** Key becomes a descriptive `support_cut_blocker` (per-object volume key),
  volume name stays user-visible "Support cut". Record the fork key in the manifest mapping.
- **Constraints.**
  - The key must be a **registered** config key. (Hard-won: an unregistered key seeded into a
    model config crashed on undo - see `HANDOFF.md`, SU-5 lesson.)
  - Cuts work on normal/snug supports only; tree supports ignore them. The tool must warn,
    as the fork does.
  - "Apply to zones" is destructive and changes what the zones mean afterwards (not
    reopenable, automatic support no longer cut). Undo must restore fully.
- **Verification.** Cut from a side view removes the support behind the polygon through the
  whole part including descending columns; right-click cancels; merge preserves the cut
  exactly; apply-to-zones leaves an equivalent slice; undo restores the zones and the cuts;
  a tree-support object warns instead of silently doing nothing.

---

## 5. Phase 11 - NeoArachne "single-line fill" / spine (PQ-4)

### Why this is safe to default off - verified, not assumed

The fork's dispatch in `NeoArachnePlan.cpp` is:

```cpp
if (!outer_is_arachne_family && cfg.inner_walls == WallSource::Classic) {
    if (cfg.spine) { run_classic_spine(g, cfg, original_cfg); return; }
    g.process_classic();     // <- spine off == exactly the pre-2.46 fork path
    return;
}
```

So with the spine flag **off**, the code path is byte-identical to the old one. This is the
one NeoArachne change in 2.46–2.48 that can be ported default-off with no behaviour delta.

### Intent

With outer wall = Classic and inner walls = Classic, whatever is left inside the walls is
printed as **one line along the centre of the gap**, with a width that follows the gap
(Simplify3D-style single extrusion), replacing Classic's short cross-hatched gap-fill strokes
and scraps - fewer paths, fewer starts and stops. Intended for thin strokes / lettering; the
release note is explicit that bold lettering should keep Classic or Arachne.

### New code and keys

- New files: `NeoArachneSpine.hpp` (65 lines) / `NeoArachneSpine.cpp` (424). Public API:
  `fits_in_one_line(const ExPolygon&, double ceiling_scaled)` and
  `run_spine(PerimeterGenerator&, const ExPolygons& regions, const SpineParams&, SpineStats*,
  ExPolygons* rejected, const ExPolygons* forced, ...)`.
- Changes are otherwise **one self-contained function** `run_classic_spine()` (+242 lines)
  plus its dispatch. No other NeoArachne file is structurally touched.
- Five keys, neutral names (ours are `hybrid_*`, the fork's are `neoarachne_spine*`):

| Ours (proposed) | Fork | Type | Fork default | **Our default** |
|---|---|---|---|---|
| `hybrid_single_line_fill` | `neoarachne_spine` | bool | **true** | **false** |
| `hybrid_single_line_min_width_pct` | `_min_width_pct` | % | 50 | off-value |
| `hybrid_single_line_max_width_pct` | `_max_width_pct` | % | 300 | off-value |
| `hybrid_single_line_min_length` | `_min_length` | mm | 0.1 | off-value |
| `hybrid_single_line_sliver_pct` | `_sliver_pct` | % | 35 | off-value |

- Placement: Quality > Wall generator, in the NeoArachne group, shown when
  `wall_generator = hybrid` and `hybrid_inner_walls = Classic` (option-driven visibility, per
  the ungating policy).

### The one thing we must NOT copy

The fork **also flips the default `inner_walls` from `ArachneStock` to `Classic`** in the same
commit. Taking that flip changes the default wall output of every project using NeoArachne -
a direct violation of our behaviour-neutral rule. **Recommendation: keep our current default
and port the spine default-off.** The user still gets the S3D single-line look by choosing
Classic inner walls and enabling the option.

### Drift

The fork's `NeoArachnePlan.cpp` grew +344 lines and `NeoArachneConfig.hpp` +45 between 2.4.5
and 2.4.8, and 2.46–2.48 also add `NeoStroke` into the same directory (out of scope, section
9). Our ported files use `hybrid_*` names, so **this is a re-implementation, not a patch**.

### Verification

- Spine off (our default) = **byte-identical** G-code against the current `port/integration`
  tree on a wall/lettering fixture. This is the acceptance gate.
- Spine on: narrow pockets get one centre line instead of cross-hatch; the line tapers at a
  pocket tip and never exceeds max width; slivers below the threshold are left empty; Y-shaped
  branches join.
- Regression: `wall_generator = Classic` and `Arachne` paths untouched; NeoArachne with inner
  walls = Arachne is untouched.
- Unit test where a deterministic fixture exists (the fork ships none for the spine, so this
  is new work); at minimum a headless slice comparison.

**Risk: medium**, and the risk is *print quality*, not build stability: it is a new motion
pattern that changes extrusion geometry, and the release note itself restricts it to thin
strokes.

---

## 6. Phase 12 - unified Extrude / Fill gizmo (HF-3)

**This is our own design work, not a port** (Addition A). It (a) ports the fork's
`SurfaceExtrude` geometry, and (b) merges three existing "add positive geometry to fill or
extrude something" mechanisms under one tool.

### What exists today - three overlapping mechanisms

| # | Name | Where | Kernel | Method | Status |
|---|---|---|---|---|---|
| 1 | `HF-1` Cavity fill | own gizmo `GLGizmoHoleFill`, `EType::HoleFill`, shortcut `G`, MCP `hole_fill_gizmo` | `HoleShapes::cavity_fill_local` | brush over a recess; each facet raised to a surface fitted to its own neighbourhood | in tree, **not accepted by the user**: flush on a flat wall, residual recess under the curved rim (`docs/handoff.md` section 23) |
| 2 | `HF-2` Fill from above | inside the **Cut** gizmo | `CutUtils::fill_from_above_mesh` | cut plane + "Lower by": the slab above the plane, lowered, is the un-engraved wall exactly on prismatic walls | merged, user-confirmed (`docs/handoff.md` section 24) |
| 3 | `SurfaceExtrude` (new) | fork only; used solely by Align & Construct | `NeotkoSurfaceExtrude.{hpp,cpp}`, 69 + 184 lines | coplanar region → outline → closed prism, grown/shrunk by an offset | not ported |

### Proposed design

One gizmo, mode selector:

- **Extrude to distance** - the new default mode. Pick a flat face → `coplanar_region()` →
  `region_outline()` → `extrude_outline(outline, offset_mm, z0, height)`. Options: **Height**,
  **Offset** (grow/shrink outline, negative shrinks), **Both directions** (half out, half in),
  **Face / Holes** (`outline_holes()` makes a plug of the holes), **Part / Object** placement.
- **Fill to surrounding surface** - today's HF-1 brush, unchanged, using
  `cavity_fill_local`. Kept deliberately: it is the only kernel that works on a **curved**
  surface, and it is the behaviour the user likes ("automatic cavity fill"). See section 8,
  decision 5.
- **Fill from above** (optional, section 8 decision 4) - HF-2's plane + "Lower by", either
  staying in the Cut gizmo or joining here as a third mode reusing `fill_from_above_mesh`.

### Why not simply "bin HF-1"

- HF-1's kernel is the only one that handles a concavity on a **curved/irregular** wall.
  `SurfaceExtrude` requires one coplanar region; `fill_from_above_mesh` requires a prismatic
  wall. Deleting `cavity_fill_local` would remove the one method that covers the curved case.
- `HoleShapes` is shared with ME-2 and PF-2b/RF-1, so the kernel cannot simply be dropped.
- HF-2 is already merged and user-confirmed; the Cut tool's `HoleFill#` filter depends on the
  volume naming.

### Port scope for `SurfaceExtrude`

- `src/libslic3r/NeotkoSurfaceExtrude.{hpp,cpp}` → rename to a neutral module name, keeping
  the API: `coplanar_region`, `region_outline`, `outline_holes`, `extrude_outline`.
- **Drop** the `s363c` "extrusion with curve" half (`CurveNode`, `sample_curve`,
  `curve_value`, `extrude_outline_curved`) - the fork marks it "hidden, debug only,
  undocumented on purpose". Take it only if the user asks.
- Port the fork's unit test `tests/libslic3r/test_surface_extrude.cpp` (78 lines) and add it to
  the test CMake lists.

### Constraints

- `HoleShapes` and `CutUtils` are additive-only (Addition B).
- If HF-1's gizmo is repurposed rather than duplicated, keep `EType::HoleFill`'s registration
  order and the `HoleFill#` volume prefix (Addition C), and keep the MCP tool name
  `hole_fill_gizmo` working (or add the new tool and keep the old name as an alias - decision 7).
- Any rename of the module must not break `GLGizmoCut`'s volume-name filter.

### Verification

- Unit: the ported `test_surface_extrude` cases (coplanar region detection, outline with holes,
  prism dimensions for positive/negative offset, zero/negative height).
- Manual: extrude a face by a height with 0 / positive / negative offset; Holes mode produces
  the hole plug; Both directions splits the height around the face; Part vs Object placement;
  the Cut gizmo's fill still skips fill volumes; ME-2 (Holes) and PF-2b/RF-1 unaffected.
- Regression: with the gizmo unused, the tree behaves as before; HF-1's brush mode produces
  the same result as today (before/after file comparison).

**Risk: medium-high.** Not algorithmic risk - the risk is scope creep and collateral damage in
a shared kernel and a shared gizmo registry, plus the panel/toolbar conventions in
`UI-1`/`docs/handoff.md` section 25.

---

## 7. Phase order and rationale

```
Phase 8   Support Zones fixes        SU-14 (map rotation), SU-15 (foot), SU-16 (straight down)
Phase 9   Support Zones UX           SU-17 (overhang only), SU-18 (view from below)
Phase 10  Support Zones cut          SU-19 (SUPPORT_BLOCKER + slice-time subtraction)
Phase 11  NeoArachne single-line     PQ-4  (spine, default off)
Phase 12  Extrude / Fill gizmo       HF-3  (SurfaceExtrude port + merge of HF-1/HF-2)
Phase 13  Hardening, tests, manifest, docs
```

Rationale: the fixes come first because they are small, they repair behaviour we already
shipped, and they de-risk our understanding of the zone data model before SU-19 changes it.
SU-19 is next because it is a self-contained new feature with a clear mechanism. PQ-4 follows:
safe to land default-off, but it needs print validation. HF-3 is last because it is our own
design change, touches shared kernels, and has open decisions that should not block anything
else.

Effort (relative, then rough working days for an experienced dev with AI assistance; build
time dominates):

| Phase | Content | Relative | Days |
|---|---|---|---|
| 8 | SU-14/15/16 fixes | small | 1-2 |
| 9 | SU-17/18 | small | 1 |
| 10 | SU-19 cut tool | medium | 2-3 |
| 11 | PQ-4 spine | medium | 2-3 |
| 12 | HF-3 extrude/fill | large | 3-5 |
| 13 | Hardening + docs | medium | 1-2 |

Total: roughly **10-16 working days**, with the usual caveat that slice-time support work
(SU-15/16/19) has the least reliable estimates.

---

## 8. Open decisions (need the user)

1. **Phase 8 scope.** Do all three fixes, or land SU-14 (smallest) alone and re-assess?
2. **SU-19 data model.** Accept the `SUPPORT_BLOCKER` volume carrying a bool key? It is the
   fork's design and it makes cuts appear in the object list and be undoable, but it means
   cuts are real model volumes that travel in the `.3mf`. The alternative (a per-object JSON
   list) was not used by the fork and would need new slice-time plumbing.
3. **PQ-4 default.** Confirm `hybrid_single_line_fill` defaults **off** and we do **not** adopt
   the fork's `inner_walls` default flip. (Fork has the line on by default.)
4. **PQ-4 naming.** `hybrid_single_line_*` (proposed) or `hybrid_spine_*`?
5. **HF-3 shape.** Replace HF-1's gizmo with the unified one (reusing `EType::HoleFill`), or add
   a second gizmo and leave HF-1 alone? And is "cavity fill" a mode of the new gizmo, or should
   HF-1 be retired once the extrude mode exists?
6. **HF-2.** Leave "Fill from above" in the Cut gizmo, or fold it into HF-3 as a third mode?
   (It is already merged and works where it is.)
7. **EF-1.** Should the edge chamfer/fillet gizmo be folded in too? My recommendation is **no** -
   it is a different operation (a fillet along an edge, not a face extrusion) and it works.
8. **MCP surface.** Keep the tool name `hole_fill_gizmo` for the unified gizmo, and add
   `extrude_gizmo` as a new tool? The MCP server is our verification path, so decide before
   HF-3.
9. **SU-17 gesture version.** Confirm the zone gesture JSON version bump and the
   "old gestures read as overhang_only = off" rule.

---

## 9. Explicitly NOT in scope

Unchanged from `PORTING_PLAN.md`, plus the 2.4.6-2.4.8 items deliberately dropped:

- The ColorStitch / Sandwich / PathBlend / Painter / **Stickers** pack, **NeoTower**, RealColor,
  and the whole `CM-*` group. Most of 2.4.6-2.4.8 is this. It needs decisions of its own, and
  most of it is category D.
- **NeoStroke** (NS-1..NS-10). The only genuinely new slicing engine in the batch, but WIP and
  declared unstable by its author, XL, and gated through `NeoDebug` which we do not ship.
  Read-only recon first, on request.
- **Align & Construct** (AC-1) and **Stacked objects: real layers** (SL-1): category D, they
  need LibreMode / assembled-object models and NeoTower that we do not have.
- **SkyLane** and any other private extension behind `NEOTKO_PRIVATE_*`. Not in the public
  tree, so not reviewable and not portable (recorded in the review doc). The user expects it
  in a later public release; revisit then.
- The Snapmaker 2.4.0 import (`7e426bff85`) as a whole: Snapmaker ecosystem code and vendor
  profile packs. Only the parts that overlap upstream 2.5 matter, and that is a separate
  analysis, not part of these three workstreams.
- The in-app intro tour (`docs/tour/**`) and the Flutter/Windows build fixes: category C.

---

## 10. Housekeeping this plan depends on

0. **Fix the diff base instruction** in `AGENTS.md` and `HANDOFF.md`: Neotko's base is
   Snapmaker **2.4.0**, not `v2.3.5`. Do this before the first branch, or the anchors above
   cannot be trusted line-for-line. (Also a candidate for `PORTING_GUIDELINES.md`.)
1. **Register the missing marker rows.** `CUT-1`, `SNAP-8`, `SU-8..SU-13`, `MV-3` and
   `HF-2` have markers in the tree and no `manifest.tsv` rows; `verify-markers.ps1` already
   flags this. Add the rows when the first branch of this plan is opened, so the new IDs land
   in a clean registry.
2. **Per-feature notes** to write before each branch: `notes/SU-14.md` … `notes/SU-19.md`,
   `notes/PQ-4.md`, `notes/HF-3.md` (recon, anchors, fork key mapping, verification evidence).
3. **Branches**: `port/SU-14`, `port/SU-15`, `port/SU-16`, `port/SU-17`, `port/SU-18`,
   `port/SU-19`, `port/PQ-4`, `feat/extrude-fill` (note: **not** a `port/` branch - it is our
   own feature, per Addition A).
