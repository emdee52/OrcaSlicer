# Porting Plan - Neotko features into upstream OrcaSlicer

Scope: port **only** the features selected by the user, ungated, into `OrcaSlicer/`
(upstream, `2.5.0-dev`). Nothing else from `OrcaFS-NeotkoCM/` is in scope.

Selected source items (IDs refer to `NEOTKO_FEATURE_INVENTORY.md`):

| ID | Feature |
|----|---------|
| MT-1 | Hotends that finished switch off |
| MT-2 | Extra Energy Save (idle tool cooldown) |
| MT-3 | Bed heats for the hottest filament on the plate |
| MT-4 | MMU painter Pro Mode (F1-F4) |
| SU-1 | PerObject Support |
| SU-3 | Real floating-object detection (feeds the AS-1 floating trigger) |
| SU-4 | NeoWave Support |
| SU-5 | Support Zones - aimed pillars |
| SU-6 | Support Zones - block trees |
| AS-1 | Libre Mode **floating-object trigger only** (place at any Z; empty first layer = warning) |
| AS-2 | Align & Stack gizmo |
| AS-3 | Snap & Drag |
| PQ-1 | Bridging infill extra expansion |
| PQ-2 | Slow down inner walls next to overhangs |
| PQ-3a | NeoArachne: pin outer wall width + configurable blend distance (low-risk slice) |
| PQ-3b | NeoArachne: full hybrid routing, Edge Closure, wall-count hysteresis (riskiest) |

Explicitly **out of scope**: the ColorStitch/Sandwich/PathBlend/Painter/NeoTower/RealColor
color pack (`CM-*`), variable-layer-height items (`VL-*`), Photo Mode/Studio, G-code
Reprocessor, Bump Mapping, NeoStitch, Texture Bump, and all branding/ecosystem items.

**Reality check.** This plan is built from Neotko's release notes only. No symbol, file,
config key, or commit hash below has been verified against the fork. Phase 0 exists to
replace every "expected" with a measured fact before any port begins. Per `AGENTS.md`, the
release notes are an index, not ground truth.

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
   - exact config keys / enum values / pref keys used (record verbatim);
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

Two features drive most of the coupling:

- **A shared "floor / contact measurement" module** is needed by AS-1 (floating trigger),
  AS-3 (Snap & Drag), and SU-1 (PerObject Support). The fork implements it once and True
  Objects (SU-2) additionally uses it for slicing. Build the measurement module in Phase 1;
  do not port True Objects' slicer behaviour.
- **Support Zones (SU-5 -> SU-6)** share one gizmo and one corridor/column-stepping engine.
  SU-6 is an extension of SU-5, not an independent feature.

Everything else is largely independent. Recommended order, lowest risk and lowest coupling
first:

```
Phase 1  shared floor/contact measurement module (internal, no UI)
Phase 2  standalone config + G-code:  MT-3, PQ-1, PQ-2, MT-1, MT-2, PQ-3a
Phase 3  painting UI:                 MT-4
Phase 4  placement:                   AS-1 (floating) + SU-3, AS-2, AS-3
Phase 5  supports:                    SU-1, SU-4, SU-5 -> SU-6
Phase 6  wall engine (riskiest last): PQ-3b
Phase 7  hardening, tests, final build
```

Rationale: Phases 2-3 are small and de-risk the toolchain/conventions. Phases 4-6 are the
real cost. PQ-3 was split: PQ-3a (pin outer width + blend distance) is small, low-risk and
lands early with the other config work; PQ-3b (full hybrid routing) is the risky half and
stays last, so the wall engine is not in the tree while everything else is validated.

---

## 3. Phase detail

Each entry: intent, expected touch points (confirm in Phase 0), ungating, uncoupling,
verification, risk.

### Phase 1 - Shared floor/contact measurement module

- Intent: for a given object instance and XY footprint, measure the real surface height
  underneath (bed, own geometry, other objects' bodies, other objects' supports), per
  instance.
- Also builds the accurate per-island floating-object detection (SU-3) that AS-1 consumes:
  real Z gap and XY overlap against the bed and every other object's instances, judged per
  instance. It is a measurement API, not a slicer behaviour, so it does not pull in True
  Objects.
- Expected touch points: new module only; the gizmos/support consume it through a narrow
  interface. No existing slicer behaviour changes in this phase.
- Ungating: N/A (internal).
- Uncabling: this module is the uncoupling artifact. It must not include True Objects'
  area-based bridge/contact classification.
- Verification: unit tests over synthetic scenes (object on bed, object stacked on object,
  two instances at different heights, hollow rim vs interior floor); assert measured heights
  and contact/no-contact per the release notes' rules.
- Risk: geometry correctness. The fork notes long-standing bugs here (phantom tower,
  stale triangle copies), so expect to write this carefully rather than lift it.

### Phase 2 - Standalone config and G-code features

**MT-3 Bed heats for the hottest filament**
- Intent: first-layer and layer-2 bed target = max over the filaments actually used, not
  whichever prints first.
- Touch point: the function that chooses initial/transition bed temperature. One call site.
- Ungating: correctness fix, always active. No change when all filaments agree.
- Verification: multi-tool plate with differing bed temps; assert `M140`/`M190` equal the
  max; all-equal plate unchanged.
- Risk: low. Note: changes emitted G-code on mixed-temp plates (document it).

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

**PQ-3a NeoArachne low-risk slice (pin outer wall width + blend distance)**
- Intent: two standalone changes to the Arachne beading solver - "Pin Outer Wall Width"
  (default on) extends constant outer width down into 1-2 bead regions, and the
  SkeletalTrapezoidation transition blend distance becomes user-configurable instead of a
  100 mm hardcode. Both are usable without the full NeoArachne routing.
- Touch points: the beading solver's outer-width handling; the transition-smoothing
  distance; two new wall-generation options.
- Ungating: remove Libre Mode; options appear with the wall generator they belong to.
- Uncabling: neither change depends on the per-feature routing or the enum. Port them as
  focused edits to the existing Arachne path; do not add the new generator value yet.
- Verification: pin on/off changes only thin (1-2 bead) regions; blend distance changes only
  transition smoothing; Classic and stock Arachne output unchanged at defaults.
- Risk: low-medium. This deliberately de-risks the PQ-3b half.

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

**AS-1 floating-object trigger + SU-3 accurate detection (decided)**
- Intent: objects may be placed at any Z with no forced bed snap, and an empty first layer
  becomes a warning instead of an error. This is only the trigger behaviour, not the rest
  of Libre Mode.
- Detection: **use the accurate per-island detection (SU-3) built in Phase 1** rather than
  the "empty first layer" heuristic. An object resting on another no longer gets a bogus
  floating warning; a genuinely floating island still warns, per island. The old blanket
  suppression is not ported.
- Touch points: the placement / first-layer validation path; consumes the Phase 1 module.
- Ungating: remove the Libre Mode master switch; the behaviour is the ported behaviour.
- Uncabling: deliberately exclude Libre Mode's assembled-boolean, per-volume XY comp,
  copy/paste process settings, full assembled-part options.
- Verification: object placed above the bed does not error; an object stacked on another
  does not warn; a genuinely floating island warns once, on the right object; two instances
  of one object at different spots are judged independently.
- Risk: low-medium.

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
- Touch points: drag/placement handling; uses the Phase 1 measurement module.
- Ungating: no Libre Mode gate. The panel is available; per-object toggle off by default.
- Uncabling: implement against the Phase 1 measurement only. Do **not** port True Objects'
  slicing consequences. Document the resulting behaviour gap: the object lands correctly,
  but contact faces are still classified by stock slicing (possible false bridges) until/if
  SU-2 is ever ported. This is an accepted limitation, not a bug.
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

### Phase 6 - PQ-3b NeoArachne full hybrid routing

- Intent: `wall_generator` gains a third value (NeoArachne); per-feature routing (outer wall
  / inner walls / gap fill each Classic / Arachne / NeotkoEdge); a validator auto-aligns
  combos the beading solver cannot handle; Edge Closure (allowed overlap, min/max bead
  width, min feature size, preserve short closure tails); wall count hysteresis.
- Prerequisite: PQ-3a has already landed the pin-outer-width and blend-distance changes, so
  this phase adds only the routing/enum and the remaining controls.
- Touch points: wall generator selection and enum; Arachne/SkeletalTrapezoidation internals;
  new wall-generation options.
- Ungating: remove Libre Mode; options appear when `wall_generator = NeoArachne`.
- Verification: Classic and Arachne unchanged; NeoArachne hybrid produces the expected
  outer/inner/gap-fill routing; invalid combos auto-align; the notes' print tests.
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

1. Baseline builds and tests pass (end Phase 0).
2. Shared measurement module unit-tested, no behavioural change in the app (end Phase 1).
3. Phase 2 features verified off-by-default identical (end Phase 2).
4. Placement works and is independent of supports/color (end Phase 4).
5. PerObject Support verified; then NeoWave; then Support Zones (three sub-checkpoints).
6. PQ-3a and PQ-3b verified separately; final full build + test matrix.

---

## 5. Risk register

| Risk | Affects | Mitigation |
|------|---------|-----------|
| Fork code relies on moved/rewritten 2.5 APIs | all | Phase 0 records target APIs; re-implement, do not paste |
| Support engine changes regress normal supports | SU-1/4/5/6 | feature-gated, off by default; byte-identical check; targeted tests |
| Wipe tower interaction (roof filament, zone toolchanges) | SU-4/5/6 | route through existing toolchange planning; verify purge counts |
| Snap & Drag without True Objects gives false bridges on contact | AS-3 | document limitation; optional SU-2 later |
| NeoArachne destabilizes Arachne/Classic | PQ-3a, PQ-3b | PQ-3a is isolated and lands early; PQ-3b last; keep Classic/Arachne paths untouched behind the enum default |
| Upstream update wipes port | all | markers + patch series + manifest outside `OrcaSlicer/`; apply script |
| Config key collisions / branding in keys | all | **Decided: preserve fork keys verbatim**; rename only if a key contains branding, and record the mapping |
| MT-1/MT-2 emit wrong machine commands | MT-1/2 | G-code diff testing; machine-family conditional for U1-specific bits |

---

## 6. Decisions

### Resolved

1. **SU-3 accuracy for AS-1 - RESOLVED: fold in.** AS-1 uses the accurate per-island
   floating-object detection built in Phase 1. The empty-first-layer heuristic is not
   ported. SU-3 is now a selected item.
2. **Config key policy - RESOLVED: preserve fork keys verbatim.** Keys are recorded in
   `OrcaSlicer/porting/notes/<id>.md` and in the manifest. Rename only a key that contains branding,
   and record the old->new mapping. App preferences use the `orca_ext_` prefix.
3. **PQ-3 splitting - RESOLVED: split.** PQ-3a (pin outer wall width + configurable blend
   distance) lands early in Phase 2; PQ-3b (full hybrid routing, Edge Closure, wall-count
   hysteresis) stays last in Phase 6.
4. **U1-specific `M220 B`/`M220 R` cleanup - RESOLVED: dropped.** Not ported with MT-4 (or
   MT-1/MT-2). No Snapmaker-machine G-code cleanup is included.

### Still open (needed before or during Phase 0)

1. **True Objects (SU-2).** Confirmed excluded. Accept the AS-3 false-bridge limitation?
