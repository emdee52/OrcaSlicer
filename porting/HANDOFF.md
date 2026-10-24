# HANDOFF - read this first

Start-here doc for a new agent session on this workspace. It says where the port stands, how to
build/test, the operational rules that cost real time, and what to do next. Per-feature truth
lives in `porting/notes/<ID>.md`; this file is the map.

## Read order

1. `OrcaSlicer/porting/HANDOFF.md` (this file)
2. `OrcaSlicer/porting/PORTING_PLAN.md` (scope, phases, decisions; Neotko + preFlight addendum)
3. `OrcaSlicer/porting/PORTING_GUIDELINES.md` (mandatory coding format)
4. `OrcaSlicer/porting/NEOTKO_FEATURE_INVENTORY.md` and `PREFLIGHT_FEATURE_INVENTORY.md`
5. `OrcaSlicer/porting/manifest.tsv` (status per feature ID - source of truth)
6. `OrcaSlicer/porting/notes/<ID>.md` for the feature about to be worked on
7. `OrcaSlicer/porting/BUILD.md` before building
8. `OrcaSlicer/porting/notes/UPSTREAM_SYNC_<date>.md` - the most recent upstream sync: what the
   merge changed, what it deliberately dropped, and what is still open (latest:
   `notes/UPSTREAM_SYNC_20261015.md`)

Root `AGENTS.md` has the workspace mission and session protocol.

## Where the port stands

- Working branch of record: **`port/integration`** (what to build and branch from). `main` is the
  untouched upstream mirror. `origin` = `https://github.com/emdee52/OrcaSlicer.git`; `upstream`
  push is disabled.
- Latest integration commit at handoff: **`51b24f3b75`** "Merge port/BF-1 (PA-1): plate automation is the
  first-layer flow, per plate type, saved on this computer" - user-tested 2026-10-24. **PA-1** replaces
  BF-1's hardcoded -20 % cool-plate default with a local per-plate-type map (`AppConfig` `[app]
  first_layer_flow_plates`), edited in the **Plate automation...** popup (plate tiles multi-select,
  four steps + None + a custom percentage), with the flow row following it while
  `initial_layer_bottom_surface_flow_auto` is on, **Reset to auto** restoring it, and any click making
  the value the project's own. Emission is unchanged (layer 1's bottom-surface flow only, no re-slice).
  The same feature briefly also automated the bed temperature (size matrix, buckets, per-plate
  offsets); **the user does not want that and it was removed before merge** - do not re-attempt
  unless asked. Notes `PA-1.md`; patch 81. libslic3r 1579/1579, fff_print 460/460. Before it:
  **`18d79a2438`** "Merge port/BF-1: first-layer flow in the send dialog, and the plate picture under
  the logo"; **`29a5f92560`** "port: record the port/BF-1 merge, user-tested"; **`01f754c26b`** "port:
  record the PF-6 removal"; before that **`22e322b2de`** "Merge port/ME-3: a screw head's cutter
  starts outside the face, so a mesh boolean cannot skin the recess" - section 42 of `docs/handoff.md`;
  `porting/notes/ME-3.md`; user-tested 2026-10-17. In the Holes tool a screw head's entrance cap landed
  exactly in the part's face: a coplanar pair, which mcut answers with a zero-thickness sheet across the
  whole recess - the slicer discards it, so the print had the counterbore while the mesh and the 3D view
  said the surface was still solid. A head is wider than the hole, so a **detected** hole's head ring
  covered the face around it on top of that; PP-4 had cured the coplanar cap only for a hole **placed**
  on a face and for a through teardrop. `hole_cutter_entry_margin()` is now one helper shared by
  `face_hole()` and `bore_negative_mesh()`, and `its_make_counterbore()` / `its_make_countersink()` take
  an `out_margin` that puts the cap that much further out while everything the shape cuts below the entry
  plane keeps the geometry `depth` / `cbore_depth` / `csink_d` describe - the countersink's cone is
  extended along its own surface, so it still cuts `csink_d` at the entry plane. Socket, Button and Sink
  heads and the optional head teardrop all take it; no option, no key, no `.3mf` migration. Merged from
  `port/ME-3`, branched from `2fa3fb01f3`, and the merge tree is byte-identical to the branch that was
  built and tested (the one follow-up, patch `77_ME-3.patch` rewritten from PowerShell's UTF-16 to a
  readable LF diff, touches `porting/` only). Before it: **`e673b82a9d`** "Merge port/MB-1: a cutter can
  only be the tool, and the object's own Mesh boolean reports what it could not subtract" - the Mesh
  boolean batch (sections 40 and 41; `notes/MB-1.md`, `notes/MB-2.md`), user-tested 2026-10-17: a
  NEGATIVE volume always lands in the gizmo's tool slot and the mode follows the picks, `mcut::do_boolean`
  reports whether it changed the mesh so a Difference that was not applied is counted and told to the
  user, and the MCP tool `boolean_parts` runs the whole-object path (78 tools). Before it:
  **`23e8ec3f98`** "Merge port/PP-6: 90-degree Lines, a live proposal, always-groove Grid, a picked depth,
  and a centred lattice" - eight changes on the Perforations gizmo the owner asked for after living with
  PP-1/PP-2 (sections 38 and 39 of `docs/handoff.md`; `porting/notes/PP-6.md`), user-tested 2026-10-17,
  plus the `tools_schema.py` regeneration MCP-3's note asked for, all user-verified. Before it: **`21aa42f159`** "Merge port/PP-5-fixes: the teardrop detector
  banded on vertices and the perforation prism welded" - it closes the two open bugs of the 2026-10-17
  batch (EF-1b, HF-4..HF-7, PP-2..PP-5; sections 29-37 of `docs/handoff.md`), all user-tested, and adds
  MCP-3 (`mesh_boolean_gizmo`; see `porting/notes/MCP-3.md`). Before it: **`8b0ffc1c83`** "port: record
  the port/gizmo-fixes merge", on **`9cf2a9def0`** "Merge port/gizmo-fixes: overlay blending,
  boolean-friendly cutters, right-click to commit, and the PP-5 detector/boolean work". Before them,
  **`4654027520`** "Merge port/gizmo-colors-perforation-pick: the
  shared overlay scheme and the Perforations pick/adjust/cut rework", on
  **`db7e763343`** "Merge port/ef-mode-icons: the Edge chamfer /
  fillet mode row as icon toggles" (docs `bd6b965b70`), on top of **`5838d7eb6e`** "Merge
  port/gizmo-panel-width: one panel width
  in font heights" - the panel style work finished end to end: the six migrated panels, Support Zones'
  own widget consolidation, the panel's translucency, the one width in font heights, and the
  `ui_screenshot` tool (UI-2) that made any of it verifiable. All user-verified 2026-10-17. Before it:
  **`6b6425ba9e`** "Merge port/SU-5-widgets" (Support Zones adopts the shared widgets), on
  **`a41a0c7bd8`** "Merge port/gizmo-style" (the migration itself), on **`cab498fc6d`**
  "Merge feat/extrude-fill: HF-3 Extrude gizmo". Read `docs/handoff.md` section 28,
  `porting/notes/PANEL_STYLE.md`, `porting/notes/SU-5-widgets.md` and `porting/notes/UI-2.md` for the
  standard, the traps it produced and what could not be captured. Before all that, the 2026-10-15
  upstream sync (1352 commits, 44 conflicts) merged on top of **`00f4969058`**; it is built (Release)
  and the suite passes 2407/2407, and `porting/notes/UPSTREAM_SYNC_20261015.md` records what it changed,
  the four fallout fixes and what it left open (the stale gettext catalogs, the manifest rows still
  missing for the later phases, and the two behaviour deltas - the port's gizmos now open on bare
  K/J/X/G/Z because upstream owns every Ctrl chord they used, and the mesh Holes tool's inch sizes
  follow upstream's ASME table). The largest merge before that was **`e0d43bbb95`** "Merge
  port/PF-10-multisupport: multi-pass support collision avoidance" (on top of `87a114c90f` Merge
  port/PF-10-auto-fix). PF-10-paint, PF-10-auto, PF-10-multisupport and PF-10-paint-qol are merged into
  `port/integration` and pushed; the user runtime-verified auto-painting (region scoring,
  build-plate-only tree fallback, min-area/facet split, gizmo QOL) and multi-pass collision avoidance.
  PF-3/4/5/7/8 excluded.
- Two sources: `OrcaFS-NeotkoCM/` (Snapmaker/Neotko Orca) and `preFlight/` (PrusaSlicer fork).
  Both are outside `OrcaSlicer/`; only selected features are ported.

### Neotko ports - all landed

| ID | Feature | Status | Notes | Patch |
|----|---------|--------|-------|-------|
| PQ-1 | Bridging infill extra expansion | ported | notes/PQ-1.md | 01_PQ-1.patch |
| MT-1 | Hotends that finished switch off | ported | notes/MT-1.md | 02_MT-1-2.patch |
| MT-2 | Extra Energy Save (idle tool cooldown) | ported | notes/MT-2.md | 02_MT-1-2.patch |
| MT-3 | Bed hottest filament | upstream-present (skip) | notes/MT-3.md | - |
| MT-4 | MMU painter Pro Mode F1-F3 (F4 dropped) | ported | notes/MT-4.md | 04/05 |
| SU-1 | PerObject Support | ported | notes/SU-1.md | 09_SU-1.patch |
| SU-4 | NeoWave (wave-roof half) | ported | notes/SU-4.md | 10_SU-4.patch |
| SU-4b | NeoWave contact layer (part-bottom + support-top) | ported | notes/SU-4b.md | 13_SU-4b.patch |
| SU-5 | Support Zones aimed pillars | ported | notes/SU-5.md | 11_SU-5.patch |
| SU-6 | Support Zones block trees | ported (with SU-5) | notes/SU-6.md | - |
| AS-1 | Free-Z placement (plate-column gravity toggle) | merged | notes/AS-1.md | 06_AS-1.patch;30_AS-1-toolbar.patch |
| AS-2 | Align & Stack + face-mate | ported | notes/AS-2.md | 07_AS-2.patch |
| AS-3 | Snap & Drag | ported | notes/AS-3.md | 08_AS-3.patch |
| PQ-3 | NeoArachne (a+b together) | ported | notes/PQ-3a.md, PQ-3b.md | 12_PQ-3.patch |

Excluded by user decision: SU-2 (True Objects), SU-3 (real floating detection). The Neotko port
is feature-complete.

### preFlight ports (`PF-*`)

Source: `preFlight/` (`github.com/oozebot/preFlight` v1.3.0, a PrusaSlicer fork). Source list:
`PREFLIGHT_FEATURE_INVENTORY.md`; plan: the preFlight addendum in `PORTING_PLAN.md`.
Key architectural fact: **preFlight's "Athena" is its Arachne fork** (`Athena/` mirrors `Arachne/`;
`"classic"` aliases Athena). Orca already has Arachne (+ our NeoArachne), so **never port Athena** -
port intent onto Orca's pipelines.

| ID | Feature | Status | Notes | Patch |
|----|---------|--------|-------|-------|
| PF-1 | Nip & Tuck seams (Holes-only default) | ported | notes/PF-1.md | 14_PF-1.patch |
| PF-2a | Counterbore smart bridging (global option) | ported | notes/PF-2.md | 15_PF-2a.patch |
| PF-2b | Counterbore bridge painting gizmo | ported | notes/PF-2.md | 16_PF-2b.patch |
| PF-3 | Auto Speed | excluded (user) | - | - |
| PF-4 | Width Control | excluded (user) | - | - |
| PF-5 | Max Commands Per Second | excluded (user) | - | - |
| PF-6 | Preview Clipping Plane | dropped (upstream section view) | notes/PF-6.md | - |
| PF-7 | Manual fan controls | excluded (user) | - | - |
| PF-8 | Serpentine | excluded (user) | - | - |
| PF-9 | Interlocking perimeters (native re-impl) | ported (user-verified) | notes/PF-9.md | 18_PF-9.patch |
| PF-10-paint | Per-style support painting gizmo + multi-pass dispatch (extensible registry; NeoWave hardcoded recipe) | ported (user-verified) | notes/PF-10-paint.md | 19_PF-10-paint.patch |
| PF-10-auto | Automatic support painting (mesh-driven region scoring, respects blockers, per-type opt-in) | ported (user-verified) | notes/PF-10-auto.md | 20_PF-10-auto.patch |
| PF-10-multisupport | Multi-pass support collision avoidance (later passes avoid earlier supports) | ported (user-verified) | notes/PF-10-multisupport.md | 21_PF-10-multisupport.patch |
| PF-10-paint-qol | Support painter QOL: grouped layout, colour swatches, remap support types | ported (user-verified) | notes/PF-10-paint.md | 22_PF-10-paint-qol.patch |
| PF-10 | Baobab supports | abandoned (user) - attempted, removed | - | - |

### Original features (`PP-*`, `BT-*`, not from a fork)

| ID | Feature | Status | Notes | Patch |
|----|---------|--------|-------|-------|
| PP-1 | Perforations gizmo (planar-face through holes) | merged (user-verified) | notes/PP-1.md | 23_PP-1.patch |
| BT-1 | Bed temperature readout + print-time offset | merged (user-verified) + 2 fixes merged | notes/BT-1.md | 27_BT-1.patch;28_BT-1-fix.patch;29_BT-1-fix2.patch |

BT-1 draws the first-layer plate temperature under the plate number and adds six
+-5/10/15 offset buttons to the "Send G-code to printer host" dialog. The offset is a
project-config key (`bed_temperature_offset`, default 0) applied in
`GCode::get_bed_temperature()`, so it re-exports the G-code without re-slicing - no
post-processing script needed. Branch `port/BT-1` from `port/integration` `3d2099554d`,
merged as `b9ba044107`. Verified by the user, including the plate readout. Known gap (not a
bug): `.gcode.3mf` uploads (`use_3mf` printers) build the bundle from the already exported
G-code, so the controls are disabled for them. The auto-recommend button was **dropped by
user decision** - do not build it. See `notes/BT-1.md`.

PP-1 stamps a pattern (Lines/Grid/Honeycomb/Circles) into a picked planar face as one
`NEGATIVE_VOLUME`; the slicer subtracts it per layer, so no mesh boolean and no new config keys.
Depth stops at the first surface behind the face. MCP tool `perforation_gizmo`. Merged into
`port/integration` as `544885e96a` (branch `feat/perf-pattern`).

## What to do next

**NEXT TASK: nothing queued for the panel style - that work is finished.** Every gizmo this fork adds
now uses the standard (see `docs/handoff.md` section 28 and `porting/notes/PANEL_STYLE.md`), including
Support Zones, whose private widget copies were consolidated on top of it
(`porting/notes/SU-5-widgets.md`). Use `ui_screenshot` (UI-2) for any further panel work: a gizmo panel
is ImGui drawn into the GL canvas and nothing else can look at it.

What is actually left, in priority order:

0. **PP-5's two bugs are DONE and merged** as `21aa42f159` (user-tested 2026-10-17; `libslic3r`
   suite 103170/103170). `docs/handoff.md` section 37 has the narrative and `porting/notes/PP-5.md`
   the per-bug detail; MCP-3, the gizmo tool that made the reproducer scriptable, is in
   `porting/notes/MCP-3.md`:
   - **the teardrop tunnel is detected**: the gate was the circle residual, and the band meant to
     exclude the roof selected faces by centroid, which re-admitted the roof's apex into the refit
     (residual 0.986 against a 0.201 limit). It now bands on the circle's vertices (residual 0.000,
     gap 90 deg, convexity -0.999) and the case is un-hidden.
   - **Mesh boolean after a boolean works**: not the stale index, not the raycaster array, not the
     activation gate. `extrude_perforation_pattern()` built the cutter as an open vertex soup (360
     open edges on a honeycomb), so mcut failed or crashed and the next boolean could not eat its
     result. The prism is welded now. The new MCP tool **MCP-3** (`mesh_boolean_gizmo`) is what made
     the reproducer scriptable - use it for any further Mesh boolean work; detail in
     `porting/notes/MCP-3.md`.
1. **Today's seven changes are all merged and pushed** (2026-10-17, user-tested in preview builds that
   carried them, all in `docs/handoff.md` sections 29-35):
   - `port/ef-mode-icons` (EF-1b) - the Edge chamfer / fillet `Chamfer | Fillet` row is two icon toggles
     (a face with one corner 45-degree cut / one corner radius). Merged `db7e763343`.
   - `port/extrude-distance` (HF-4) - the Extrude face **Height is now a signed Distance**: positive
     grows a solid out of the face, negative adds the same prism as a `NEGATIVE_VOLUME`, so it cuts a
     pocket. Bounds -50..200 mm. Merged `51be36ff37`.
   - `port/panel-flip-margin` (HF-5) - a fork panel hangs to the left of its icon once it comes within one
     icon cell of the canvas's right edge (`gizmo_panel_near_right_edge`, `kGizmoPanelEdgeMargin`).
     Measured: the Holes panel sat 19 px from the edge and did not flip; Hole walls (71 px) still does
     not, so one icon moves Holes only. Merged `ea6751d660`.
   - `port/extrude-mode-icons` (HF-6) - the Extrude panel's `Extrude face | Cavity fill` segmented control
     is two icon toggles (a solid boss on a face / a plug closed flush in a slot), name on hover. Merged
     `0afb593498`.
   - `port/extrude-applied-colors` (HF-7) - a committed Extrude volume is coloured by what it does and
     drawn at the preview's translucency. Merged `b3658fd246`.
   - `port/gizmo-colors-perforation-pick` (PP-3 + PP-2) - one 3D overlay colour scheme across the fork
     gizmos (section 34) and Perforations reworked to pick -> adjust -> Perforate (section 35). Merged
     `4654027520`.
   Bookkeeping for all seven is in (`manifest.tsv` rows/notes and patches `64`-`69`), and every patch
   reverse-checks against this tree.
1. **Support Zones' non-empty states** are the one part of the panel-style work that is styled but
   unverified - the footprint shape row, the coverage-mode row and the editing cards only exist once a
   zone is painted, which needs a zone drawn in the app. The checklists are in `notes/SU-5.md` and
   `notes/SU-6.md`.
2. **Hole walls' strengthening mode** is likewise styled but unverifiable over MCP: nothing in the tool
   surface can click into that mode.
3. **Queued gaps** from before, if the user asks: **PF-1b** (Nip/Tuck inner-perimeter trim) and
   **PF-2b partial** (the painted "Partial" is one bridge layer, not Orca's `chbBridges` coverage).
4. **Pending user test:** branch `port/SU-4-fix` (the NeoWave wave roof printing over air on curved
   overhangs) was still awaiting a runtime check as of the last handoff.

**Panel style migration is DONE and user-verified (2026-10-17).** `port/gizmo-style` merged as
`a41a0c7bd8`; Support Zones followed on `port/SU-5-widgets`. Patches **53**-`59`.

**Gizmo panel chrome is DONE on `port/gizmo-ui-panels` and awaits your test.** Branched from
`port/integration` `9e5ce7fb2b`; nine single-ID commits (UI-1 panel anchor + overflow flip, PF-2b/RF-1
hole-walls selectors, EF-1 edge chamfer, HF-1 cavity fill, PP-1 perforations, MV-1 pick-pivot shortcut
and button spacing) plus a `port:` docs commit, patches `38`-`46`. Not merged and not pushed. The
rationale, the manual checklist and the two pre-existing gaps it leaves are in `docs/handoff.md` section
27; `manifest.tsv` gained rows for EF-1, HF-1 and RF-1, which had markers in the tree without a row.

**BT-1 fix 2 is DONE and merged into `port/integration` (user-verified).** Branch `port/BT-1-fix2` from
`port/integration` `e1fe5d221f`, merged as `8fa72e4493`. Labels with an offset suffix
(`65°C (+10)`, `60°C (+5)`) rendered squished and sat at a varying distance from the plate number: the
readout's quad used the text's *original* aspect while `generate_from_text_string()` pads the texture
to a power of two, so the glyphs were scaled by `(orig_w/orig_h)/(POT_w/POT_h)` - ~1.0 for short
labels, ~0.58 for one that crosses into the 256 bucket. The quad now follows the padded aspect and
anchors the visible text. Measured: `70°C (+15)` vs `55°C` width ratio 1.32 -> 2.32 (2.32 is the
natural ratio) and the right edge no longer moves with the string (125 px -> 3 px). See
`notes/BT-1.md` "Fix 2 (2026-10-15)" and patch `29_BT-1-fix2.patch`.

**BT-1 fix is DONE and merged into `port/integration` (user-verified).** Branch `port/BT-1-fix` from
`port/integration` `91bf03992b`, merged as `ae832a04aa`. The plate bed-temperature readout did not
follow a **per-object filament change** (the print dialog did):
`ObjectList::set_extruder_for_selected_items()` mutates model state and only calls
`plater()->update()`, never `Plater::on_config_change`, so the cached readout texture was never
invalidated. The invalidation now lives in `Plater::priv::update()` - the funnel every model/config
mutation passes through. The label was also moved from below/right of the plate number (off the plate
and out of the default fit-plate view, behind the icon column) to **on the plate, immediately left of
the number**. Verified over MCP + window captures on a Textured PEI plate whose per-filament
`textured_plate_temp_initial_layer` is `80,80,60,80`:
- object on filament 3 (PLA, 60) -> label `60°C`, plate G-code `M190 S60`;
- object switched to filament 1 (PETG, 80) -> label `80°C`, `M190 S80`;
- a second object on filament 1 while the first stayed on 3 -> label `80°C` = the highest of the used
  filaments, matching `bed_temperature_formula = by_highest_temp`. (No G-code for this row: slicing
  PLA + PETG on that printer is refused by the nozzle-temperature compatibility validation.)
See `notes/BT-1.md` "Fix (2026-10-14)" and patch `28_BT-1-fix.patch`.

**PF-10-paint / PF-10-auto / PF-10-multisupport / PF-10-paint-qol are DONE and merged into
`port/integration` (user-verified).** `port/PF-10-auto-fix` and `port/PF-10-multisupport` are pushed
to `origin` and merged (`--no-ff`). Summary: an extensible `OrcaExt/SupportPaintTypes` scoring
registry (a new support = one row), a synchronous mesh-driven auto-painter that scores each overhang
region (area/span/height/wall-angle/curvature/gap-below), honours the highlight angle /
on-overhangs-only, never overwrites painted/blocker facets (split-safe
`TriangleSelector::painted_facet_mask`), splits regions by normal and by per-facet part-below for the
build-plate-only tree fallback, has a min-overhang-area cut, per-type checkboxes, undo, colour
swatches, a remap-types panel and a grouped layout; multi-pass supports avoid each other via a
per-pass occupancy channel. **PF-10 Baobab was abandoned by user decision**
(attempted across 4 rounds, then fully removed: code, keys, paint row, `port/PF-10` branch). Do not
re-attempt unless the user asks. PF-3/4/5/7/8 were excluded; PF-6 was ported and then
**removed** on 2026-10-23 in favour of upstream's section view (see `notes/PF-6.md`); PF-9 is
ported (runtime-verified).

One ID per branch (`port/PF-<n>`), product commit then a separate `port:` docs+patch commit, push,
`--no-ff` merge into `port/integration`, push. Don't merge an incomplete feature.

**Pending user test (2026-09-16):** branch `port/SU-4-fix` (from `port/integration` f9b08708d6),
product `a499db16a4` + docs `14196ca94d`. Fixes the NeoWave wave roof printing over air on curved
overhangs (`baloondog test.3mf` layers ~715/~991): the base layer directly under an interface stack
is now filled with the normal base pattern (floor), plus a thin-sliver fallback. Verified by slicing
via MCP; the residual float also occurs with stock `normal(auto)` support. Not merged/pushed - the
user tests first. See `notes/SU-4.md` "Fix" and patch `26_SU-4-fix.patch`.

Also queued as known gaps (do if the user asks):
- **PF-1b**: Nip/Tuck inner-perimeter trim. v1 only notches the external perimeter; preFlight also
  trims/splits the first inner wall. Needs cross-loop pairing, which `GCode::extrude_loop` lacks.
- **PF-2b partial**: painted "Partial" currently = a single bridge layer, not Orca's `chbBridges`
  coverage algorithm (per-region routing to Orca's partial path needs region splitting).

Before the big ones (PF-8 Serpentine ~164 KB module) do a read-only recon and write `notes/<ID>.md`
first; see the preFlight inventory for portability verdicts. (PF-8 stays excluded; PF-10 Baobab is
abandoned.)

## Environment & build

**Windows only, local builds only. Do not use GitHub Actions.** Read `porting/BUILD.md`.

Before every build shell (PowerShell):

```powershell
$env:PATH = "C:\Program Files\CMake\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\bin;$env:PATH"
$env:CMAKE_TLS_VERIFY = "0"
```

`CMAKE_TLS_VERIFY=0` is mandatory (Windows Schannel revocation checking is offline here).

```powershell
# add/remove a source file -> needs configure
.\OrcaSlicer\build_win.bat -s -j 8
# header/source edit only -> incremental (NO configure)
.\OrcaSlicer\build_win.bat -s --no-configure -j 8
```

Result: `OrcaSlicer/build/src/Release/orca-slicer.exe` (thin launcher; the code is in
`OrcaSlicer/build/src/Release/OrcaSlicer.dll`). Full rebuild ~20 min, small change 1-5 min.

### How builds are run (important)

Start detached, poll, keep sleeps SHORT:

```powershell
$log="C:\Users\emdee\Downloads\Projects\ORCA\build-logs\<name>.log"
Remove-Item $log -EA SilentlyContinue
$p=Start-Process cmd.exe -ArgumentList '/c',"OrcaSlicer\build_win.bat -s --no-configure -j 8 > `"$log`" 2>&1" `
   -WorkingDirectory "C:\Users\emdee\Downloads\Projects\ORCA" -PassThru -WindowStyle Hidden
```

Then poll with **3-5 minute sleeps max** and check
`Select-String -Path $log -Pattern ": error |fatal error"` and
`(Get-Process MSBuild -EA SilentlyContinue).Count`. The user explicitly asked for short sleeps -
long ones get interrupted. `build-logs/` is gitignored.

Kill a running slicer before rebuilding (it holds the DLL -> `LNK1104`):
`Get-Process orca-slicer,OrcaSlicer,MSBuild,cl,link -EA SilentlyContinue | Stop-Process -Force`.

## Operational lessons (cost real time - do not rediscover)

- **Repo rejects a UTF-8 BOM in source** (`encoding-check-libslic3r_gui`). Never write source with
  `Set-Content`/`Out-File`; use `[IO.File]::WriteAllText($f,$t,(New-Object Text.UTF8Encoding($false)))`.
  When scripting multi-site edits, do the replacements one pattern at a time and verify counts;
  a bad PowerShell replace silently corrupts a file (happened once on a lambda - re-read after edits).
- **`edit` anchors must be unique.** Grep first; include context if the anchor repeats.
- **Tab-aligned files** break `edit`; use targeted regex replace (no BOM).
- **Do not `-c`** to "fix" a code error - it discards the working tree. `-c` is only for a broken
  configure/toolchain.
- **Runtime debugging is user-driven.** The user builds, launches, slices, and reports symptoms +
  screenshots. Prefer instrumenting the code with a log file over guessing. Instrumentation that
  already exists:
  - Crash dumps: `%APPDATA%\OrcaSlicer\log\crash_*.log` (text stack; e.g. an ACCESS_VIOLATION in
    cereal serialization is a bad/unknown config key in a ModelObject/ModelVolume config - see
    `notes/SU-5.md`).
  - Per-run log: `%APPDATA%\OrcaSlicer\log\debug_*.log.0`; `BackgroundSlicingProcess` logs
    `got other exception` for slicing errors (the dialog shows `what()`).
  - **PF-1 seam notch**: every run appends `%APPDATA%\OrcaSlicer\log\seam_notch_debug.log`
    (`layer, hole, ccw, type, width, loop_len, taper, depth, seam, push`, plus skip reasons and
    inner-relief lines). This is how the Nip/Tuck bugs were diagnosed. Keep this pattern.
- **The stub/placeholder confusion**: `verify-markers.ps1` and `apply.ps1` referenced by
  `PORTING_GUIDELINES.md` do **not** exist (`porting/scripts/` only has `.gitkeep`). Verify markers
  manually (grep `[ORCAPORT:<ID>]`).

## Session learnings worth keeping (2026-09 sessions)

- **NeoArachne (PQ-3)** slices only after porting the fork's `Inc2e` change: `GCode::extrude_entity`
  must accept a nested `ExtrusionEntityCollection` (NeoArachne appends per-island buckets to
  `g.loops`; target's dispatcher only knew Path/MultiPath/Loop and threw "Invalid argument supplied
  to extrude()"). See `notes/PQ-3b.md`.
- **Support Zones crash (SU-5)** was an unregistered config key (`support_neoweave_enabled`) seeded
  into a model config; `DynamicPrintConfig::save` does `print_config_def.get(key)` -> nullptr ->
  crash on any undo snapshot. Never seed an option that is not registered.
- **PF-1 Nip/Tuck** - hard-won details, all in `notes/PF-1.md`:
  - Orca's preview records a seam only when the external loop end returns within **0.25 mm** of its
    start (`GCodeProcessor.cpp:5416`). Moving one seam endpoint breaks that -> the seam "vanishes".
    We use **approach A**: seam endpoints stay nominal, only interior points inside the taper move.
  - `GCode::extrude_loop` (external perimeters only) is the hook, after `loop.clip_end`. Spiral
    vase excluded. Notch is skipped on **layer 0**.
  - `seam_notch_angle` default is **0** (never skip): the 44 deg corner test misfired on
    polygonized round walls. Holes never use the test.
  - "One layer printed opposite" is **Orca's own SeamPlacer** relocating the seam (proven with
    `seam_type = Regular`); the notch just follows it.
  - `seam_notch_target`: All external / **Holes only (default)** / Outer only. Keys are in
    `PrintObjectConfig` (per-object).
- **PF-2 counterbore**: preFlight's "smart bridging" = stepped rotating-corridor fill of the
  `bore - shaft` ring; bore = larger hole on layer L, shaft = smaller nested hole on L+1, bridged
  upward (hole shrinks going up). Orca's existing `counterbore_hole_bridging` (`partiallybridge` /
  `sacrificiallayer`) is a *different* SuperSlicer algorithm - left untouched. PF-2a added
  `chbSmart` + `counterbore_bridge_layers` (PrintRegionConfig) + auto-detection;
  PF-2b added the `counterbore_bridge_facets` annotation, `GLGizmoCounterboreBridge`, 3mf attribute
  `slic3rpe:counterbore_bridge`, and the painted routing. See `notes/PF-2.md`.
- **Adding a per-volume FacetsAnnotation** (needed for any new painter) is a ~25-site mechanical
  mirror of `fuzzy_skin_facets` across `Model.hpp`/`Model.cpp`, plus 3mf read/write, plus
  `model_<x>_data_changed()` used in `PrintApply.cpp`. Do it with scripted replacements and build.

## Git workflow used

- One feature per branch `port/<id>`, branched from `port/integration`.
- Commit product code first, then a separate `port:` commit for `manifest.tsv` + `notes/` + the
  regenerated patch.
- Patch: `porting/patches/NN_<ID>.patch`, from
  `git diff "<base>..HEAD" -- src resources` where `<base>` is the integration commit the feature
  branched from. Verify with `git apply --check -R <patch>` (should succeed).
- `manifest.tsv` is the source of truth; keep it and the notes in sync with the code.
- **Commit policy (updated): always commit** each completed change on its port branch (product
  commit, then a separate `port:` docs+patch commit). **Do not merge into `port/integration` and do
  not push** until the user has tested the build and explicitly asks - the merge is the user's gate.
  Do not merge an incomplete feature.

## Built but not runtime/print-verified (user should test)

Every ported feature compiles and links. Runtime/visual-verified by the user so far: PQ-1, PF-1, PF-9
(and PF-6, which has since been removed). Pending: MT-1/MT-2, PQ-2, MT-4 F1/F2/F3, AS-1
(the plate-column toggle is user-verified; the free-Z drag/stay-off-bed protocol in
`notes/AS-1.md` is not), AS-2, AS-3, SU-1/SU-4/SU-5/SU-6, PQ-3, SU-4b (both sides), PF-2a (Smart
bridging) and PF-2b (paint smart/partial). Protocols are in each `notes/<ID>.md`.

## Cheat-sheet

- Target: `OrcaSlicer/` | Neotko fork: `OrcaFS-NeotkoCM/` | preFlight: `preFlight/` |
  Snapmaker base for Neotko diffs: `snapmaker/OrcaSlicer` tag **`v2.4.0`** (current base,
  2026-09-17). Tag `v2.3.5` is the OLD base - correct only for Neotko 2.4.5 (`cf1255c741`) and
  earlier. The rebase to 2.4.0 is fork commit `7e426bff85` (Snapmaker's work, not Neotko's).
- Isolate a Neotko change: **prefer the fork's own commit** - `git -C OrcaFS-NeotkoCM show <sha>`
  (exact, no vendor tag needed). Aggregate view: `git -C OrcaFS-NeotkoCM diff v2.4.0 HEAD -- <path>`;
  for pre-rebase features `git -C OrcaFS-NeotkoCM diff v2.3.5 cf1255c741 -- <path>`. A tag diff is a
  guide, not ground truth: the fork also adds `tests/catch2/**`, `docs/tour/**` and its own Flutter
  web layout.
- Port artifacts: `OrcaSlicer/porting/` (plan, guidelines, 2 inventories, manifest, notes,
  patches, BUILD).
- Startup: `OrcaSlicer/build/src/Release/orca-slicer.exe`.
