# Neotko fork 2.4.6 – 2.4.8 — new-feature review

Scope: `OrcaFS-NeotkoCM` **`cf1255c741`** (2.4.5, our last inventory point) → **`e68dcc8551`**
(`origin/main`, 2026-10-16). Nine commits, three release notes.

This is a **review document**, not a port plan. Nothing here has been ported. Every claim below
was checked against the fork's diff unless it says "(notes only)". Where I could not confirm
something I say so.

Read `HANDOFF.md` and `NEOTKO_FEATURE_INVENTORY.md` for the existing port; this file only covers
what is new since 2.4.5.

---

## 0. Headline findings (read this first)

1. **The fork moved its base from Snapmaker Orca 2.3.5 to 2.4.0** (commit `7e426bff85`,
   845 files, 391 500 insertions). Our whole diffing workflow in the root `AGENTS.md` —
   "diff Neotko against Snapmaker `v2.3.5`" — is now **wrong**: from this point the meaningful
   base is Snapmaker 2.4.0. Diffing against `v2.3.5` attributes Snapmaker's 2.4.0 work to Neotko.
   This is the single most important operational consequence of this update.

2. **Roughly 85–90 % of the "new features" in 2.4.6–2.4.8 sit on the ColorStitch / ColorMix /
   Painter / Sticker / NeoTower subsystem that our port deliberately never took** (inventory
   `CM-1` … `CM-15`, all category D). PathBlend direction fixes, pattern placement, sandbox
   editor removal, sticker tabs, multicolour SVG stickers, gradient bands, Palette hover cards,
   NeoTower Local-Z — none of it is reachable from our integration branch and none of it is
   portable in isolation. Treat those entries as *informational*: they tell you what Neotko is
   doing, not what we can take.

3. **Only six new items are genuinely relevant to our port.** Three extend features we already
   have (NeoArachne, Support Zones, Align & Stack) and three are self-contained new modules
   (PaintTransfer delta, SurfaceExtrude, NeoStroke). See §4.

4. **"Booleans and Split keep the painting" is largely already upstream in our 2.5 target.**
   Our `OrcaSlicer/` already ships a `keep_painting` app key wired into `GLGizmoMeshBoolean`,
   `GUI_ObjectList` (Split to parts / to objects), `Plater` and `fix_model_with_cgal_gui`.
   Neotko's version is a *superset*: it adds cross-part painting with per-part filament mixing,
   a sampler-based resample, and a progress dialog. So this is category **A + a small B delta**,
   not a new feature. (Verified against the target tree, not the notes.)

5. **Drift touching already-ported code** (§5): the fork changed the NeoArachne default
   `inner_walls` from `ArachneStock` to `Classic` (2.46) and rewrote ~300 lines of
   `NeoArachnePlan.cpp`; our ported `PQ-3` is at the old behaviour. `GLGizmoSupportZones.cpp`
   (+1193 lines) and `SupportMaterial.cpp` (+96) drifted under our ported `SU-5`/`SU-6`.
   Neither breaks us today, but any future re-port of those features must start from the new
   code, not a patch replay.

---

## 1. The base move: Snapmaker Orca 2.4.0 (commit `7e426bff85`)

Not Neotko's work. 845 files. The release note (`NEOTKOCM_RELEASE_2_47.md`) lists it by PR
number; the big buckets are:

| Area | Evidence | Category |
|---|---|---|
| Flutter web device pages, `resources/web` (51 files, ~284 k lines) | diff stat | C (ecosystem) |
| `resources/profiles` — 338 files, Snapmaker/U1 filament + process packs | diff stat | C (vendor profiles) |
| `src/mqtt` (90 files) + `SnapLogClient.cpp/.hpp` (3.5 k lines) — printer connection, sign-in, device pages, logging | diff stat | C (Snapmaker cloud) |
| High Flow nozzle support (`HighFlowCompat.*`, `FlowTypeHelper.*`) | new files | A/D — upstream 2.5 may already have per-nozzle flow |
| Mixed-filament batch colour matching (`MixedFilamentBatchDialog.*`, `FilamentGroupDialog.*`) | new files | C — FullSpectrum/ColorMix, we have no ColorMix |
| Timelapse download, SliceModePopup, memory guard, Fit-all, 3mf >2 GB, Catch2 v3, OpenSSL 3.5.7 | new files / build | mostly A (upstream) or infrastructure |

**Neotko's own stated exclusions from 2.4.0** (notes only, not verified): dropped PR #501
(would break NeoTower), render changes #463/#737/#764/#940, new app icons.

**What it means for us:** this is mostly Snapmaker ecosystem code and profile packs, which the
workspace excludes anyway (category C). The parts that matter are (a) it invalidates the
`v2.3.5` diff base, and (b) any of it that our upstream 2.5 target already contains should not be
attributed to Neotko. Do a `snapmaker v2.3.5..v2.4.0` diff before reading any future Neotko
release note that touches a shared file.

---

## 2. 2.4.6 — commit `67d2415aab` (83 files, +13 734 / −6 942)

Theme: **a big ColorStitch/Painter UX overhaul**, plus two things that touch our port.

### ColorStitch / Painter group — informational (category D, unportable in isolation)

| ID | Feature (verified in the diff) | Notes |
|---|---|---|
| CS-1 | Pattern placement becomes position-based instead of print-order | Holes / short patterns no longer shift the weave; top and penultimate lines align |
| CS-2 | One way to build gradients and stripes | **Removes** "Line distribution mode" and "Monotonic Line Replan"; stripes in mm; gradient scale fit-or-repeat |
| CS-3 | Project migration on open | `ColorStitchLegacyMigration.cpp` (new) + a notice counting converted gradients/stripe recipes |
| CS-4 | Sandwich editor removed | Legacy recipes moved into the Painter palette as "From Sandwich editor"; process presets with an old recipe get it disabled at start |
| CS-5 | Painter loses the "WIP Beta" label | — |
| CS-6 | Painter panel reorganised | Four departments, icon tools, stacked zone cards, swatches |
| CS-7 | Fix: Pro stripe bar drew a half gradient with ends swapped | GUI-only |
| CS-8 | Fix: Painter "copy to" now copies the whole ColorStitch recipe | Was dropping band widths / scale / transition / colours 3–4 |
| CS-9 | Painted preview now matches the G-code | Bands start where they print; rotation/scale no longer rotate the preview stripes; both passes previewed |

All of these live in `src/libslic3r/ColorStitch*`, `src/slic3r/GUI/Gizmos/GLGizmoColorStitch*`
and the painter panel. **None of those files exist in our target.** Nothing to do.

`neotower_no_ramming` (`coBool`) is also added here — NeoTower-only, not ported.

### LibreMode settings folding — informational

| ID | Feature | Notes |
|---|---|---|
| LM-1 | Click a settings-section header to fold/unfold it; Alt+click folds all; state persists | Process tab, Global and Objects, **LibreMode on only**. We never ported LibreMode (we removed its gates instead), so this has no home in our tree today. |

### Relevant to our port

| ID | Feature | Category | Why it matters |
|---|---|---|---|
| **NA-2** | **NeoArachne single-line fill ("spine", S3D-style)** | **D** | Extends our ported `PQ-3`. With inner walls = Classic, whatever is left inside the walls is printed as **one line along the gap centre**, width following the gap, instead of cross-hatched gap fill. New keys: `neoarachne_spine` (default **true**), `_min_width_pct` (50), `_max_width_pct` (300), `_min_length` (0.1), `_sliver_pct` (35). New file `NeoArachneSpine.{cpp,hpp}` (424 + 65 lines). Placement: Quality > Wall generator > NeoArachne. |
| PR-1 | NeoArachne wall-path preview moved out of the Quality page into **NeoStroke → Advanced options…** | C/D | We excluded the NeoArachne preview panel in `PQ-3`; nothing to do unless we later take NeoStroke. |
| **NS-1** | **NeoStroke — wall generator that plans a wall as strokes, not loops** (debug-gated) | **B (WIP)** | First landing. New 4th `wall_generator` value `"neostroke"` + `NeoArachneWallSource::NeoStroke`. Engine files `NeoStroke.{cpp,hpp}`, `NeoStrokeSkeleton.*`, `NeoStrokeLink.hpp`, `GLGizmoNeoStroke.*`, `NeoStrokeAdvancedDialog.*`. Unlocked by `ORCA_DEBUG_NEOSTROKE=1` **or** the new Preferences checkbox `neotko_neostroke_enabled`. Neotko's own note says plainly: **not ready to print with**. |

**Why NA-2 is category D for us, concretely:** the fork's key names are `neoarachne_*`; our port
renamed the whole NeoArachne key family to neutral `hybrid_*` (`hybrid_inner_walls`,
`HybridWallSource`, …) per the no-branding rule. So NA-2 cannot be applied as a patch — it needs
re-implementation on our renamed `NeoArachnePlan.cpp`/`Config.hpp`. Worse, 2.46 **changes the
default `inner_walls` to `Classic`**, which is exactly the "behaviour-neutral at defaults" rule we
hold: taking NA-2 at face value would change the default wall output of every existing project.
If we ever want it, it must default **off**.

---

## 3. 2.4.7 — commits `3bd92365d0` + `612d94a975` (40 files, +5 933)

Theme: **NeoStroke becomes real** (a new planner, a whole preview tool, per-island settings), plus
**Support Zones upgrades**.

### NeoStroke group (the bulk of the release)

| ID | Feature (verified) | Notes |
|---|---|---|
| NS-2 | Letters come out closed | NeoStroke now reaches under Classic's outer wall by the **Infill/Wall overlap**; recommended ~18 % for lettering; join line ends; drop lines where a stroke narrows |
| NS-3 | **NeoStroke Preview tool** | New toolbar tool (`GLGizmoNeoStroke`, 2702 lines + `Preview/` modules `PreviewMetrics`, `PreviewEffectiveConfig`). Slices each object with its **own** settings, one layer, marks risks (hills/grooves/pits/pile-ups), A/B freeze, presets. |
| NS-4 | New field planner | Shares out the whole section width at once via a Euclidean distance transform + level curves (`NeoStrokeField.cpp`, 1131 lines). Old planner kept for wide islands. |
| NS-5 | Three path settings | `neostroke_lane_overlap` (0 %), `neostroke_end_at_junctions` (**true**), `neostroke_lead_in` (**0.4 mm**). Defaults confirmed in `PrintConfig.cpp`, not just the notes. |
| NS-6 | Thin strokes no longer over-extrude | Middle line also limited by the real room inside the wall |
| NS-7 | Limit NeoStroke to a band next to the wall | `neostroke_band_mm` (0 = off) |
| NS-8 | Previews cut the model like the slice | Uses "Slice gap closing radius" and slice resolution |
| NS-9 | **Per-island (per-letter) settings** | Stored as JSON in a single `neostroke_island_overrides` `coString`; islands identified by an interior point, so they survive move/rotate |
| NS-10 | Much faster | Islands computed in parallel; note claims 9.3 s → 2 s on a 15-letter logo, G-code identical |

**Assessment:** NeoStroke is a serious, well-documented new wall engine (Voronoi medial-axis
skeleton + EDT field + level curves, ~5 k lines of engine + ~3 k of GUI). It is the only genuinely
novel *slicing* feature in this batch. It is also explicitly **unstable**, lives in the same
`NeoArachne/` directory as our ported `PQ-3`, and would be an XL port that drags in the preview tool
and a new `wall_generator` enum value with a `NeoDebug` gate (which we'd have to re-gate, since we
do not ship `NeoDebug`). **Recommendation: not now.** Keep it on the "watch" list; if the user
wants it, do a read-only recon first (new `notes/NS-1.md`) and expect a large, coupled port.

### Support Zones group — extends our ported `SU-5`/`SU-6`

| ID | Feature (verified in `GLGizmoSupportZones.{cpp,hpp}`) | Category | Effort |
|---|---|---|---|
| **SZ-1** | Overhang map (and the "this zone catches nothing" warning) now uses **plate down**, not the object's own axes — fixes a wrong map on rotated objects | B (bug fix) | S |
| **SZ-2** | **Overhang only** brush (new, default on), threshold frozen per-gesture in the saved JSON (`overhang_only`, `overhang_cut_deg`) | B | M |
| SZ-3 | "View from below" button | B | S |
| **SZ-4** | Painting down to the bed no longer breaks the tree: what is painted below the stump floor becomes a **foot** (`HeadFoot`), head starts above it; new "Not a tree: stumps unused" warning | B (bug fix) | M |
| **SZ-5** | A zone part that cannot reach a stump now goes **straight down** as normal support instead of being trimmed and left hanging | B (bug fix) | M |
| **SZ-6** | **Cut supports** — draw a polygon, everything behind it gets no support; saved as "Support cut", undoable, with remove-last / merge / apply-to-zones | B | L |

New key: `neotko_support_cut` (`coBool`, `comDevelop`). The rest of the state rides in the gesture
JSON, so it stays `.3mf`-compatible. SZ-1/SZ-4/SZ-5 are **fixes for behaviour we already shipped**
(SU-5/SU-6) — those are the highest-value items in the whole batch for us. SZ-2/SZ-6 are new UX.

**Drift warning:** the fork's `GLGizmoSupportZones.cpp` grew by 1193 lines and `SupportMaterial.cpp`
by 96 around the code we ported. A future `SU-5`/`SU-6` touch-up must re-diff, not replay
`11_SU-5.patch`.

### Fix — informational

| ID | Feature | Category |
|---|---|---|
| CS-10 | Painter eyedropper / sticker placing no longer select every object the cursor crosses | D (their painter) |

---

## 4. 2.4.8 — commit `2c41b67aa3` (+ `ebffae1e8d`, `dc5418bdad`, `e68dcc8551`)

82 files, +9 431 / −1 199, then four small Windows/Flutter fix commits. Theme: **Stickers, keeping
paint through geometry ops, and Align & Construct.**

### Sticker group — informational (category D)

New `Stickers` tab in the ColorStitch Painter; `ColorStitchStickerPreview.{cpp,hpp}` (new); full
undo; multicolour SVG import with per-colour recipes; CSS-class colours, background removal,
opacity merge, canvas clipping; linear gradients as flat bands with split/join; bottom-face
stickers; line-by-line colour preview; Palette hover card. **All of it lives on the ColorStitch
subsystem we do not have.** Nothing to do.

### Relevant to our port

| ID | Feature | Category | Detail |
|---|---|---|---|
| **PT-1/2** | **Booleans and Split keep the painting** | **A + B delta** | New module `PaintTransfer.{cpp,hpp}` (450 lines) + `TriangleSelector::resample(sampler, edge_limit)` + `apply_resample_plan`. **Our target already keeps painting** via the `keep_painting` app key (boolean gizmo, Split to parts/objects, Plater, CGAL repair) — so the *core* is category A. Neotko's delta: painting carried **across differently-filamented parts** (Part 1 supplies the result's base filament; a Difference's cut walls take it; each unpainted part keeps its own filament), plus a sampler-based resample that survives re-triangulation, plus a "Keeping the painting…" progress dialog. Ships with **`tests/libslic3r/test_paint_transfer.cpp`**. |
| **AC-2** | **SurfaceExtrude** | **B** | New `NeotkoSurfaceExtrude.{cpp,hpp}` (184 lines) — turn a picked flat mesh face into a real solid: `coplanar_region`, `region_outline`, `outline_holes`, `extrude_outline(offset, z0, height)`. Pure geometry, no config keys, no GUI. Ships with **`tests/libslic3r/test_surface_extrude.cpp`**. This is the most self-contained new thing in the batch and the easiest to port. |
| **AC-1** | **Align & Construct** (rework of Align & Stack) | **D** | `GLGizmoAlignStack.cpp` +1950 lines. Adds Extrude-from-face, Union/Difference/Intersection **inside an assembled object**, per-part picking (click-through whole→part), ghost previews. Depends on Neotko's assembled-object/LibreMode model and the boolean pipeline above. Our ported `AS-2` is the *old* gizmo; taking this is a re-implementation, not a patch. The "no LibreMode needed now" line is worth noting: Neotko removed the gate, which matches our own convention. |
| **SL-1** | **Stacked objects: real layers** | **D** | New `Feature/Gravity/GravityLayering.{cpp,hpp}` + `NeoTowerLocalZ.hpp`; key `neotko_stacked_real_layers` (`coBool`). Off by default; only shows with **NeoTower + Variable layer height**, i.e. two things we do not have. It makes a part resting on another start its layer grid at its own base and land its top on the real height. Genuinely interesting research, but it is bolted to NeoTower. |
| W-1 | Windows link fix ("Camera is a struct"), Flutter dark-mode cache (`NeotkoFlutterDark.*`), `ColorStitchStickerPreview.hpp` fix | C | Ecosystem/build only. |

### Fixes — relevance

Most fixes are ColorStitch/sticker/NeoTower (informational). Two are worth knowing:

| ID | Fix | Relevance |
|---|---|---|
| FX-1 | LibreMode crash slicing with Support Cubic / Adaptive Cubic / Lightning infill (Sparse infill) | **Not applicable** — we did not port LibreMode. But it is a reminder: any port that skips internal-bridge preparation will null out the data those three infills need. Worth remembering if we ever add such a skip. |
| FX-2 | Mesh boolean result used to move so its centre sat where Part 1's centre was; now stays in place | This is a **bug in the code we would touch for PT-1**, and arguably a bug upstream may share. Worth checking our target's `GLGizmoMeshBoolean` recentre behaviour if the user has ever seen a boolean jump. |
| FX-* | ColorStitch ordering/penultimate-gradient/one-filament-pattern/uniform-ramp, sticker gradient area/ramp/slivers, Pro saved colour one change behind, RealColor legend text pile-up, bottom-on-bed first layer + wipe tower, import keeps recipes/stickers, copy/paste keeps stickers, "Wipe tower generation failed", Classic-tower-after-NeoTower | ColorStitch / NeoTower / PaintHost — informational. The Real Color legend one *might* touch shared `GCodeViewer` legend layout; low priority. |

### Intro tour — category C

Commit `aa15ddd999` and the `docs/tour/**` growth (2.46 + 2.48) add a large in-app HTML tour
(`docs/tour/index.html`, `place.html`, `stickers.html`, …) plus glyph/optics assets. Grep confirms
Neotko/Snapmaker branding throughout. **Excluded** per the no-branding rule.

---

## 5. Drift in features we already ported

| Ported feature | Fork drift since `cf1255c741` | Impact today |
|---|---|---|
| `PQ-3` NeoArachne | `NeoArachneConfig.hpp` +45, `NeoArachnePlan.cpp` +344 (301 added lines beyond the spine feature). Default `inner_walls` changed `ArachneStock` → `Classic`. | None at rest — our copy is frozen at the old behaviour. But our `PQ-3` patch cannot be replayed on the new fork code; a future refresh is re-implementation. |
| `SU-5`/`SU-6` Support Zones | `GLGizmoSupportZones.cpp` +1193, `SupportMaterial.cpp` +96, `SupportZoneProbe.*` +38, plus the SZ-1/SZ-4/SZ-5 **bug fixes**. | None at rest; the SZ fixes are *desirable* and not yet in our tree. |
| `AS-2` Align & Stack | `GLGizmoAlignStack.cpp` +1950 (rework into Align & Construct). | None at rest. |
| `AS-3` Snap & Drag, `GravitySnap.*` | no change in this range | — |
| `MT-1`/`MT-2`, `PQ-1`, `PQ-2`, `SU-1`, `SU-4`, `SU-4b`, `PF-*`, `PP-1`, `BT-1`, `ME-2` and the rest | no change in this range | — |

`NeoDebug` note: NeoStroke is gated through `NeoDebug::NEOSTROKE` (an env-var-or-prefs channel).
We do not ship `NeoDebug`; if NeoStroke is ever ported, that gate must be replaced with a normal
app key, keeping it off by default.

---

## 6. Recommendation

**Do nothing now.** This batch is overwhelmingly an update to Neotko's own ColorStitch ecosystem,
which our port deliberately excludes; there is no change here that is both portable and
behaviour-neutral without a decision from you.

If you want to act, the ranked shortlist is:

1. **SZ-1 + SZ-4 + SZ-5** (Support Zones bug fixes) — smallest diff, fixes behaviour we already
   shipped, directly user-visible. *Category B, effort S–M.*
2. **AC-2 SurfaceExtrude** — self-contained geometry module with tests; useful on its own
   (extrude a face into a solid/cutter) and a prerequisite for any Align & Construct work.
   *Category B, effort M.*
3. **PT-1/PT-2 paint transfer delta** — only the part our target lacks (cross-part filament-aware
   painting + sampler resample). Take it with its unit test. *Category B delta, effort M–L.*
4. **SZ-2 + SZ-6** (overhang-only brush, cut supports) — new Support Zones tooling. *Category B,
   effort M–L.*
5. **NA-2 NeoArachne single-line fill** — valuable for fine lettering, but must be re-implemented
   on our renamed keys and forced **off** by default. *Category D, effort M.*
6. **NS-1…NS-10 NeoStroke** — the only big *new slicing* feature, but WIP, unstable by the author's
   own words, XL, and coupled to `NeoDebug` and NeoArachne. Only on explicit request, with a recon
   first. *Category B/WIP, effort XL.*
7. **AC-1 / SL-1** — depend on LibreMode / assembled objects / NeoTower we do not have.
   *Category D, effort XL.* Do not start without a scoping decision.

**Non-feature work item:** correct the root `AGENTS.md` and `HANDOFF.md` diff instructions —
the Neotko base is now Snapmaker **2.4.0**, not `v2.3.5`. Until that is fixed, every future
Neotko diff will mislabel 391 k lines of Snapmaker work as Neotko's.

---

## Appendix — evidence

- Range: `cf1255c741..e68dcc8551`, 9 commits, linear (no merges).
- Commits and sizes:
  - `67d2415aab` 2.46 — 83 files, +13 734 / −6 942
  - `aa15ddd999` Fork Intro Tour — 11 files, +801 (category C)
  - `3bd92365d0` NeoStroke WIP — 37 files, +5 917 / −569
  - `7e426bff85` SM 2.4.0 import — 845 files, +391 500 / −85 726 (not Neotko)
  - `612d94a975` 2.47 patches due 2.4 — 3 files, +16 / −74
  - `2c41b67aa3` 2.48 — 82 files, +9 431 / −1 199
  - `ebffae1e8d`, `dc5418bdad`, `e68dcc8551` — Windows/Flutter fixes
- New engine/GUI files added by Neotko in this range (verified `git show --name-status`):
  `NeoArachne/NeoArachneSpine.{cpp,hpp}`, `NeoArachne/NeoStroke.{cpp,hpp}`,
  `NeoStrokeSkeleton.{cpp,hpp}`, `NeoStrokeLink.hpp`, `NeoStrokeField.{cpp,hpp}`,
  `NeoStrokeIslands.{cpp,hpp}`, `NeoArachne/Preview/{PreviewEffectiveConfig,PreviewMetrics}.{cpp,hpp}`,
  `GUI/Gizmos/GLGizmoNeoStroke.{cpp,hpp}`, `GUI/NeoStrokeAdvancedDialog.{cpp,hpp}`,
  `libslic3r/Feature/Gravity/GravityLayering.{cpp,hpp}`, `libslic3r/NeoTowerLocalZ.hpp`,
  `libslic3r/NeotkoSurfaceExtrude.{cpp,hpp}`, `libslic3r/PaintTransfer.{cpp,hpp}`,
  `libslic3r/NeotkoImportCarry.{cpp,hpp}`, `libslic3r/ColorStitchLegacyMigration.cpp`,
  `GUI/ColorStitchStickerPreview.{cpp,hpp}`, `tests/libslic3r/test_paint_transfer.cpp`,
  `tests/libslic3r/test_surface_extrude.cpp`.
- New print-config keys added by Neotko in this range (`PrintConfig.cpp` `add(...)`), grouped:
  `neoarachne_spine{,_min_width_pct,_max_width_pct,_min_length,_sliver_pct}`;
  `neostroke_{corner_hooks,min_width_pct,max_width_pct,width_ref,detail_min_pct,curve_overlap,
  lane_overlap,end_at_junctions,lead_in,island_overrides,max_bead_pct,max_stroke_width,band_mm,
  bead_min_pct,layer_jitter,skate,skate_detour,continuous_turns,variable_k}`;
  `neotko_support_cut`; `neotko_stacked_real_layers`; `neotower_no_ramming`.
  Verified defaults where a table above quotes one.
- NeoStroke unlock path (verified in `Preferences.cpp`): Preferences checkbox
  `neotko_neostroke_enabled`, or env `ORCA_DEBUG_NEOSTROKE=1`, both feeding
  `NeoDebug::set_enabled(NeoDebug::NEOSTROKE, …)`.
- Target-tree checks (our `OrcaSlicer/`, branch `port/gizmo-ui-panels`): `keep_painting` app key
  in `GLGizmoMeshBoolean.cpp`, `GUI_ObjectList.cpp`, `Plater.cpp`;
  `TriangleSelector` has **no** `resample()` — i.e. the PaintTransfer delta is genuinely absent;
  `GLGizmoAlignStack.{cpp,hpp}` present (our `AS-2`); no `*SurfaceExtrude*` file.
