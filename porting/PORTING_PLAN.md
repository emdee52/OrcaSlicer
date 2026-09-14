# Porting Plan - Neotko features into upstream OrcaSlicer

Scope: port **only** the features selected by the user, ungated, into `OrcaSlicer/`
(upstream, `2.5.0-dev`). Nothing else from `OrcaFS-NeotkoCM/` is in scope.

Selected source items (IDs refer to `NEOTKO_FEATURE_INVENTORY.md`):

| ID | Feature |
|----|---------|
| MT-1 | Hotends that finished switch off |
| MT-2 | Extra Energy Save (idle tool cooldown) |
| MT-3 | Bed heats for the hottest filament on the plate - **ALREADY UPSTREAM, dropped (Phase 0)** |
| MT-4 | MMU painter Pro Mode (F1-F4) |
| SU-1 | PerObject Support |
| SU-4 | NeoWave Support (wave-roof half only) |
| SU-5 | Support Zones - aimed pillars |
| SU-6 | Support Zones - block trees |
| AS-1 | Free-Z placement only (move objects off the bed to align without assembling) |
| AS-2 | Align & Stack gizmo |
| AS-3 | Snap & Drag |
| PQ-1 | Bridging infill extra expansion |
| PQ-2 | Slow down inner walls next to overhangs |
| PQ-3a | NeoArachne: pin outer wall width + configurable blend distance (low-risk slice) |
| PQ-3b | NeoArachne: full hybrid routing, Edge Closure, wall-count hysteresis (riskiest) |

Explicitly **out of scope**: the ColorStitch/Sandwich/PathBlend/Painter/NeoTower/RealColor
color pack (`CM-*`), variable-layer-height items (`VL-*`), Photo Mode/Studio, G-code
Reprocessor, Bump Mapping, NeoStitch, Texture Bump, and all branding/ecosystem items.

**Phase 0 status: reconnaissance complete.** The baseline build is green, and every
selected feature has been located in the fork diff (`git diff v2.3.5 HEAD`). Each ID now
has a verified note in `OrcaSlicer/porting/notes/<ID>.md` and a row in
`OrcaSlicer/porting/manifest.tsv`. The release notes were the index; the code is the ground
truth.

## Phase 0 results

- **MT-3 is already upstream** and is dropped. Target 2.5 already sets the first-layer bed
  temperature from the hottest used filament: `bed_temperature_formula` (default
  `btfHighestTemp`, `PrintConfig.cpp:2956`) and `GCode::get_highest_bed_temperature`
  (`GCode.cpp:4637`). No port needed.
- **Every other selected key is absent from target** (verified by grep), so the remaining
  IDs are genuine ports, not duplicates.
- **Scope is much larger than the release notes implied**, in three places:
  - `SU-4`: the contact layer depends on the unselected ColorStitch/`NeoweaveEngine`
    (`ColorStitch.cpp`), which target lacks entirely. Only the wave-roof half is
    self-contained, and that half is a ~3400-line duplicate of `SupportMaterial`.
  - `SU-5`/`SU-6`: one very large subsystem - ~1400 diff lines in `SupportMaterial`, plus
    zone slicing, family/area hashing, per-volume JSON gesture data, and a 366 KB gizmo.
  - `PQ-3a` cannot ship standalone: its two keys are read only on the
    `wall_generator == NeoArachne` path, so port it with `PQ-3b` or re-plumb the options
    into the stock Arachne path (category D).
- **Shared infrastructure to port or stub first:** `NeoDebug` (referenced by MT-1/MT-2,
  SU-1/3/4/5/6 and PQ-3) and the `neotko_libre_mode` / `neotko_libre_enabled` gate that
  surrounds almost every feature's UI and dispatch.
- **Coupling to cut:** `InstanceContact::cross_object_active()` ORs with
  `neotko_true_objects` (`InstanceContact.cpp:246`). Remove that term so SU-1/SU-3 do not
  drag in unselected True Objects.
- **Upstream drift is real (category D):** MT-4's shared painter/`TriangleSelector`, PQ-1's
  bridge-angle path, PQ-2's extrusion-quality estimator, and PQ-3's Arachne internals have
  all changed since the fork's base. Re-implement intent against 2.5; do not paste patches.

---

## 0. Ground rules (apply to every phase)

- One feature = one branch = one commit series. Never bundle two IDs in one commit.
- Port the **intent and behaviour**, re-implemented on current 2.5 APIs - do not paste fork
  code. Expect API drift (fork base is Snapmaker Orca 2.3.5; target is 2.5.0-dev).
- Every ported change must be behaviour-neutral at its default. Options default off; the
  existing output must be byte-identical when a feature is off.
- No Libre Mode gate, no debug environment gate. See "Ungating" below.
- No branding: no Snapmaker/Neotko names, URLs, sponsor text, profiles.
- Never modify `deps/` or `deps_src/`.
- Do not commit or push unless asked.
- Every edit inside `OrcaSlicer/` obeys `OrcaSlicer/AGENTS.md` and the conventions in
  `PORTING_GUIDELINES.md` (markers, manifest, naming).
- Traceability: each feature records source release note + source commit sha + upstream
  OrcaSlicer PR/commit where the same idea/API exists.

### Ungating policy (user requirement: ungate everything)

- Remove Libre Mode / `ORCA_DEBUG_*` / master-switch checks around the selected features.
- Replace a gate with the feature's own option, default off. The UI control is always
  visible (or visible when its own prerequisite option is selected, e.g. NeoArachne appears
  when `wall_generator = NeoArachne`; NeoWave appears when support type = NeoWave). This is
  option-driven visibility, **not** a hidden gate.
- If a gate's removal would expose controls only meaningful in combination (e.g. Support
  Zones), keep the control visible but disabled with an explanatory tooltip when the
  prerequisite is not met. Disabled-with-reason is acceptable; hidden is not.
- Do not keep a single "Neotko Libre Mode" master toggle. Delete it as a dependency.

### Uncabling policy

- A shared behaviour used by two features becomes a **new, neutral, self-contained module**
  (see Phase 1), consumed through a narrow interface. No feature reaches into another's
  code.
- Do not port a feature's side effects that the user did not select. Example: Snap & Drag
  (AS-3) needs the "what is under this footprint" measurement that lives with True Objects
  (SU-2), but the user did **not** select SU-2's area-based slicing. Port the measurement,
  not the slicing change.
- When a release note says feature X "is a sub-behaviour of" or "reuses" feature Y, treat Y
  as a candidate shared module, not a hard dependency.

---

## 1. Phase 0 - Reconnaissance and baseline (do first, no porting)

Goal: turn the notes into verified facts and a working baseline build.

Tasks:

1. **Isolate the fork diff.** Fetch Snapmaker `v2.3.5` into the fork and diff, per
   `AGENTS.md`:
   `git -C OrcaFS-NeotkoCM fetch ".../snapmaker/OrcaSlicer" tag v2.3.5` then
   `git -C OrcaFS-NeotkoCM diff --stat v2.3.5 HEAD` and per-directory diffs.
2. **Locate each selected feature** in the diff. For each ID record, in
   `OrcaSlicer/porting/notes/<ID>.md`:
   - source files touched and a one-line purpose each;
   - neutral names for our keys (never `neotko_*`), with the fork key recorded as a mapping;
   - source commit sha(s) and the `NEOTKOCM_RELEASE_x` that describes it;
   - gate(s) present (Libre Mode, env var, default) to be removed;
   - hidden dependencies on other Neotko features;
   - names of the upstream 2.5 APIs the fork's code calls (these will have drifted).
3. **Build the baseline.** `OrcaSlicer/deps/build` does not exist, so the first build is a
   full dependency build. Start it early and in the background because it is long:
   `.\OrcaSlicer\build_win.bat -ds`. Confirm `OrcaSlicer/build/src/Release/orca-slicer.exe`
   runs and `ctest` passes before touching anything.
4. **Create the port scaffolding inside the target repo** so it is versioned and pushed:
   `OrcaSlicer/porting/manifest.tsv`, `OrcaSlicer/porting/patches/`,
   `OrcaSlicer/porting/notes/`, `OrcaSlicer/porting/scripts/`. The plan, guidelines and
   inventory already live there. Manifest schema is defined in `PORTING_GUIDELINES.md`.
5. **Map upstream equivalents.** For each selected feature, check whether 2.5.0-dev already
   has it or a superseding mechanism. Mark as `upstream-present` and stop planning a port
   for that ID (MT-3 and PQ-1 are the most likely to have partial upstream equivalents;
   PQ-2 references OrcaSlicer issue #3891; SU-1 claims no equivalent exists).

Exit criteria: every selected ID has a notes file with verified files/keys/sha; baseline
build runs; manifest skeleton exists.

Estimated: 1-2 working days plus background build time.

---

## 2. Dependency analysis and build order

The coupling is small:

- **`InstanceContact`** (cross-instance contact) is now used only by SU-1. It is not a
  shared substrate: AS-1 is placement-only and AS-3 is GUI-local, so neither needs it.
  Build it as part of SU-1.
- **Support Zones (SU-5 -> SU-6)** share one gizmo and one corridor/column-stepping engine.
  SU-6 extends SU-5.
- **PQ-3a cannot stand alone** (recon), so PQ-3a and PQ-3b ship together.

Recommended order, lowest risk and lowest coupling first:

```
Phase 1  retired (the shared measurement module is folded into SU-1)
Phase 2  standalone config + G-code:  PQ-1, PQ-2, MT-1, MT-2
Phase 3  painting UI:                 MT-4
Phase 4  placement:                   AS-1 (free-Z), AS-2, AS-3
Phase 5  supports:                    SU-1 (incl. InstanceContact), SU-4 (wave roof), SU-5 -> SU-6
Phase 6  wall engine (riskiest last): PQ-3 (a+b together)
Phase 7  hardening, tests, final build
```

Rationale: Phase 2 is small and de-risks the toolchain/conventions. Placement is
self-contained GUI work. Supports are the real cost. The hybrid wall generator is the
riskiest single feature and stays last, so the wall engine is not in the tree while
everything else is validated.

---

## 3. Phase detail

Each entry: intent, expected touch points (confirm in Phase 0), ungating, uncoupling,
verification, risk.

### Phase 1 - retired

The shared floor/contact module is no longer a phase. AS-1 is placement-only (no slicing
change) and AS-3 is GUI-local, so only SU-1 needs `InstanceContact`, and it is built with
SU-1 in Phase 5. SU-3 is dropped: with the user's workflow (align floating, then assemble
before slicing) the slice-time floating warning is never exercised.

### Phase 2 - Standalone config and G-code features

**MT-3 Bed heats for the hottest filament - DROPPED (already upstream)**
- Target already computes this by default (`bed_temperature_formula` = `btfHighestTemp`,
  `GCode::get_highest_bed_temperature`). No port. See `notes/MT-3.md`.

**PQ-1 Bridging infill extra expansion**
- Intent: new Quality > Bridging option, mm, default 0, added on top of the existing
  wall-count-derived anchoring. Can expand until the whole layer is one bridge. Honours
  custom bridge angle. External bridges only.
- Touch point: bridge-region expansion computation; PrintConfig registration.
- Ungating: option default 0 -> byte-identical. Always visible.
- Verification: 0 = byte-identical to stock; increasing value expands the bridge; angle
  honoured. Catch2 where a deterministic fixture exists.
- Risk: low.

**PQ-2 Slow down inner walls next to overhangs**
- Intent: reuse the existing overhang-speed distance measurement, walked one wall outwards
  by a configurable reach (% line width) and slowdown (%). Needs "slow down for overhang";
  inert with outer-wall-first.
- Touch point: overhang speed/flow computation; new Speed > Overhang options.
- Ungating: remove Libre Mode condition; options default to the notes' gentle values; if
  this cannot be behaviour-neutral, default off and document.
- Verification: overhang test geometry; inner-wall speed changes only near overhangs; no
  change on fully supported inner walls.
- Risk: medium (speed pipeline).

**PQ-3a - moved to Phase 6 (ships with PQ-3b)**
- Recon shows the two "low-risk" knobs (pin outer width, blend distance) are reachable only
  on the hybrid-wall-generator path, so they cannot land alone. They are ported together
  with PQ-3b.

**MT-1 Hotends that finished switch off / MT-2 Extra Energy Save**
- Intent (MT-1): set a finished tool's standby command to 0 so it stops heating for the
  rest of the job; decided on the final G-code so tower visits are counted; never forces a
  reheat wait.
- Intent (MT-2): also cool a tool during long idle gaps; the existing preheat-window logic
  cancels short cooldowns; requires Ooze prevention.
- Touch points: application-level preferences (the notes put these in an app-level "Bed and
  Nozzle Extras" area, not a print profile). One post-slice G-code rewrite pass. One
  settings dialog.
- Ungating: visible normally (no Libre Mode); both checkboxes default off; MT-2 greys out
  until MT-1 is on; warn if Ooze prevention is off.
- Uncabling: self-contained tool-usage analyzer over the finished G-code; no support/color
  dependencies. The Snapmaker U1-specific `M220 B`/`M220 R` removal is **dropped** (decided);
  no Snapmaker-machine G-code cleanup is ported.
- Verification: multi-tool G-code; tool with no later extrusion gets standby 0; tools
  returning inside the preheat window keep heat; single-tool file emits nothing; exported
  G-code differs only in the intended commands.
- Risk: medium. This edits real machine commands; get the "no reheat wait" invariant
  right and test with Ooze prevention on and off.

### Phase 3 - MT-4 MMU painter Pro Mode (F1-F4)

- Intent: standard MMU paint gizmo gets an always-visible Pro Mode: F1 paint perimeters
  only (existing `mmu_segmented_region_max_width` mechanism), F2 extra walls in the painted
  zone, F3 brush precision (mesh subdivision scaling), F4 rectangle/polygon masks with live
  overlay. Also fix F1+F2 width interaction.
- Touch points: the standard painting gizmo UI; the paint/subdivision pipeline; the
  painted-region wall count path.
- Ungating: remove the Libre Mode requirement; F1-F4 each off by default.
- Scoped out: the U1-specific `M220 B`/`M220 R` removal is **dropped** (decided); it is not
  part of MT-4, and no Snapmaker-machine G-code cleanup is ported.
- Verification: each tool off = stock paint; F1 restricts to a ring; F2 adds walls sized to
  account for F1; F3 changes subdivision granularity only; F4 paints enclosed area on
  release. Undo/redo intact.
- Risk: medium (mesh subdivision performance, painting correctness).

### Phase 4 - Placement

**AS-1 Free-Z placement only (decided)**
- Intent: let an object be moved off the bed in Z and stay there, so two separate objects
  can be aligned without assembling. Printing is always done after assembling, so no
  slice-time behaviour changes.
- Scope: the placement half only - a dedicated app preference (`orca_ext_free_z`, default
  off) plus the `ensure_on_bed()` skip sites in the GUI (`GLCanvas3D`, `Selection`,
  `SurfaceDrag`, `GUI_ObjectList`, `GLGizmoSimplify`, `EmbossJob`, `Plater`).
- Explicitly NOT ported: the `GCode.cpp` empty-first-layer "warning instead of error"
  downgrade, SU-3 measured floating detection, and the rest of Libre Mode.
- Key: a new dedicated app key; do not reuse `neotko_true_objects`.
- Verification: preference off -> objects snap to the bed exactly as stock; on -> an object
  stays where it is placed and AS-2/AS-3 can leave objects floating.
- Risk: low.

**AS-2 Align & Stack**
- Intent: left-toolbar gizmo; anchor #1 and movable #2; "place against" (face-touch,
  chained) or "align flush"; Z gap; drop to bed; live ghost previews computed with the same
  math the click runs; capped to two objects.
- Touch points: new GUI gizmo; object transform/selection APIs.
- Ungating: icon always available (remove the Libre-Mode-disabled state).
- Uncabling: pure transform math; must not depend on Support Zones, True Objects, or the
  color pack.
- Verification: each of the face/centering operations produces the previewed transform;
  undo restores; selection of a third object swaps #2.
- Risk: medium.

**AS-3 Snap & Drag**
- Intent: while dragging, an object rests on the real surface found under its footprint
  (overlap hysteresis, real mesh raycasts, highest surface wins, multi-instance uses the
  lowest target, never invents a bed drop, landing indicator). Own magnet panel with
  "Snap & Drag", "Snap to bed", and "Move selection as one block".
- Touch points: drag/placement handling; GUI-local (raycasts GLVolume meshes).
- Ungating: no Libre Mode gate. The panel is available; per-object toggle off by default.
- Uncabling: GUI-only. It does not call `InstanceContact` or `GravityFloor`; it only needs
  AS-1's free-Z context so a dragged object may stay off the bed. No slicing changes, no
  True Objects.
- Verification: drag over a lower object lands on it; bare grazing does not engage
  (hysteresis); hollow box lands on rim/interior correctly; selection block moves rigidly;
  "Snap to bed" off keeps an object floating.
- Risk: medium-high (viewport picking, multi-instance, raycast cost).

### Phase 5 - Supports

**SU-1 PerObject Support**
- Intent: an object's support treats every other object's body and finished support as
  obstacles, keeping the normal support/object XY distance; moving a part re-solves
  neighbours; honours all tree styles, normal/grid, and NeoWave; only in by-layer printing.
  Trade-off when on: first-layer base/brim expansion is dropped.
- Touch points: support generation obstacle set; plate invalidation/dependency edge for
  object moves; a per-object option.
- Ungating: per-object checkbox, off by default, always visible under Enable support.
- Uncabling: consume the Phase 1 measurement; do not port True Objects.
- Verification: two close objects generate non-colliding supports; move one, the other
  regenerates; first-layer base expansion absent only when on; assembling vs separate.
- Risk: high (support engine, pair-wise performance, invalidation graph).

**SU-4 NeoWave Support**
- Intent: wave-roof hollow support plus a neoweave contact layer. Support type "NeoWave"
  locks base=Hollow and interface=Wave (greyed). Contact layer = Z-oscillation on the
  object's bridge fill above a support roof, with on/off + amplitude + period under
  Support > Advanced.
- Touch points: support type enum, roof/interface pattern generation, a bridge-fill Z
  modifier; new options.
- Ungating: remove Libre Mode; controls appear when the relevant support type/option is
  selected; toggles default off.
- Verification: NeoWave type forces Hollow/Wave; wave roof closes over a hollow pillar;
  contact-layer Z oscillation appears on bridge fill above the roof; G-code preview will
  look flat (known limitation, document).
- Risk: high. Notes: core wave roof is print-validated, contact layer print-pending.

**SU-5 Support Zones - aimed pillars -> SU-6 block trees**
- Intent (SU-5): two-click or brush-painted zone; pillar built from overhang patch to a
  landing; real-surface roof; lean angle capped by the slicer; per-zone soluble roof
  filament; reopenable zones; touching zones with equal settings merge; seeding of support
  settings on the object.
- Intent (SU-6): paint an area; a stump is planted plumb below; the column walks toward the
  stump; flat roof. Same tool; footprint choice selects aimed vs block-tree.
- Touch points: new left-toolbar gizmo; support enforcer/zone data model; support column
  stepping engine; per-object support settings seeding; wiping/toolchange interaction for
  per-zone roof filament.
- Ungating: remove Libre Mode; gizmo always available; nothing on by default; a plate with
  no zones slices as before.
- Verification: printed output for a leaned pillar and a torus gap (the notes' own tests);
  zone merge rules; reopen/edit/undo; no purge scheduled on every layer for a roof that
  exists on a few.
- Risk: very high. This is the largest and least finished area. Budget accordingly and
  expect to land SU-5 before SU-6.

### Phase 6 - PQ-3 NeoArachne (a+b together)

- Intent: add the hybrid wall generator (third `wall_generator` value): per-feature routing
  (outer / inner / gap fill each Classic / Arachne / NeotkoEdge), a validator that
  auto-aligns invalid combos, Edge Closure controls, wall-count hysteresis, and the
  pin-outer-width + blend-distance knobs (PQ-3a, which cannot stand alone).
- Prerequisite: none beyond target 2.5; PQ-3a and PQ-3b are one port.
- Touch points: wall generator selection and enum; Arachne/SkeletalTrapezoidation internals;
  new wall-generation options; preview panel.
- Ungating: remove the Libre Mode checks; options appear when `wall_generator = NeoArachne`.
- Verification: Classic and Arachne unchanged; hybrid routing correct; invalid combos
  auto-align; the notes' print tests.
- Risk: very high. Do last.

### Phase 7 - Hardening

- Full build + `ctest`; add targeted Catch2 tests for the deterministic geometry/config
  paths (PQ-1, Phase 1 measurement, bed-temp selection).
- Manual verification matrix: off-by-default byte-identical checks for every feature; then
  each feature on.
- Regenerate the manifest; verify every marker pairs; verify no orphaned gate remains.
- Update `OrcaSlicer/porting/NEOTKO_FEATURE_INVENTORY.md` status column for the ported IDs.

---

## 4. Timeline (estimates, not commitments)

Build time is the hidden cost: the first deps build is long, and every support/wall change
triggers substantial recompilation. Durations assume an experienced dev with AI assistance.

| Phase | Content | Relative effort | Rough working days |
|-------|---------|-----------------|--------------------|
| 0 | Recon, manifest, baseline build | small + build wait | 1-2 |
| 1 | Shared floor/contact module | medium | 1-2 |
| 2 | MT-3, PQ-1, PQ-2, MT-1, MT-2, PQ-3a | medium | 3-4 |
| 3 | MT-4 | medium | 1-2 |
| 4 | AS-1 + SU-3, AS-2, AS-3 | large | 3-5 |
| 5 | SU-1, SU-4, SU-5 -> SU-6 | very large | 6-9 |
| 6 | PQ-3b NeoArachne full routing | large-very large | 3-5 |
| 7 | Hardening, tests, docs | medium | 2-3 |

Total: roughly **21-33 working days**, dominated by Phase 5 and the PQ-3b half of Phase 6.
Those two are also where estimates are least reliable; treat them as ranges and re-estimate
after Phase 0, when the real diff is known.

Suggested checkpoints (stop and reassess at each):

1. Baseline builds and tests pass (Phase 0 done).
2. Phase 2 features verified off-by-default identical.
3. Placement works (AS-1/AS-2/AS-3), no slicing change.
4. PerObject Support verified; then wave support; then Support Zones (three sub-checkpoints).
5. Hybrid wall generator verified; final full build + test matrix.

---

## 5. Risk register

| Risk | Affects | Mitigation |
|------|---------|-----------|
| Fork code relies on moved/rewritten 2.5 APIs | all | Phase 0 records target APIs; re-implement, do not paste |
| Support engine changes regress normal supports | SU-1/4/5/6 | feature-gated, off by default; byte-identical check; targeted tests |
| Wipe tower interaction (roof filament, zone toolchanges) | SU-4/5/6 | route through existing toolchange planning; verify purge counts |
| Snap & Drag changes drag landing | AS-3 | GUI-only, toggles default off; needs AS-1 free-Z; no slicing change |
| Hybrid wall generator destabilizes Arachne/Classic | PQ-3 | keep Classic/Arachne paths untouched behind the enum default; land it last |
| Upstream update wipes port | all | markers + patch series + manifest outside `OrcaSlicer/`; apply script |
| Config key naming | all | **Decided: neutral names, never `neotko_*`**; app keys `orca_ext_*`; record the fork key as a mapping in the manifest |
| MT-1/MT-2 emit wrong machine commands | MT-1/2 | G-code diff testing; machine-family conditional for U1-specific bits |

---

## 6. Decisions

### Resolved

1. **MT-3 - DROPPED, already upstream.** Target 2.5 sets the bed temperature from the
   hottest used filament by default.
2. **SU-3 - DROPPED.** With "align floating, then assemble before printing", the slice-time
   floating warning is never exercised. AS-1 is placement-only.
3. **AS-1 key - new dedicated app key** (`orca_ext_free_z`), default off. Do not reuse
   `neotko_true_objects`.
4. **Naming - neutral keys, keep user-visible feature names.** No `neotko_*` or vendor
   prefixes in any key; app preferences use `orca_ext_`; new print keys are descriptive
   (record the fork key as a mapping in the manifest). User-visible labels such as "NeoWave"
   and "NeoArachne" are kept.
5. **UI placement - hybrid.** Print/region/object settings go into their natural existing
   Process pages (Quality > Bridging; Speed > Overhang; Support; Quality > Wall generator;
   the paint gizmo). App-level machine/behavior preferences (MT-1/MT-2, AS-1, AS-3) live in
   Preferences or a small app-level Extras dialog, not in a profile. No single catch-all tab.
6. **SU-4 - wave-roof half only.** The contact layer is not portable without the ColorStitch
   pack.
7. **PQ-3 - one port (a+b together).** PQ-3a cannot stand alone.
8. **SU-2 True Objects - excluded.** Printing is done after assembling, so there is no
   unassembled floating contact to slice.
9. **NeoDebug - drop the debug lines.** No NeoDebug port; features are ported without the
   trace channels.
10. **U1-specific `M220 B`/`M220 R` cleanup - dropped.**

No open decisions remain. Next: implement Phase 2, starting with PQ-1.
