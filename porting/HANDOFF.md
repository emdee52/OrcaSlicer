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

Root `AGENTS.md` has the workspace mission and session protocol.

## Where the port stands

- Working branch of record: **`port/integration`** (what to build and branch from). `main` is the
  untouched upstream mirror. `origin` = `https://github.com/emdee52/OrcaSlicer.git`; `upstream`
  push is disabled.
- Latest integration commit at handoff: **`b1a23c6e19`** "Merge port/PF-9: print-friendly interlocking
  bead width". PF-6 and PF-9 are ported and PF-9 is runtime/visual-verified; PF-3/4/5/7/8 excluded.
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
| AS-1 | Free-Z placement | ported | notes/AS-1.md | 06_AS-1.patch |
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
| PF-6 | Preview Clipping Plane | ported | notes/PF-6.md | 17_PF-6.patch |
| PF-7 | Manual fan controls | excluded (user) | - | - |
| PF-8 | Serpentine | excluded (user) | - | - |
| PF-9 | Interlocking perimeters (native re-impl) | ported (user-verified) | notes/PF-9.md | 18_PF-9.patch |
| PF-10-paint | Per-style support painting gizmo + multi-pass dispatch (extensible registry; NeoWave hardcoded recipe) | ported (runtime pending) | notes/PF-10-paint.md | 19_PF-10-paint.patch |
| PF-10-auto | Automatic support painting (mesh-driven region scoring, respects blockers, per-type opt-in) | ported (build+tests clean, runtime pending) | notes/PF-10-auto.md | 20_PF-10-auto.patch |
| PF-10-multisupport | Multi-pass support collision avoidance (later passes avoid earlier supports) | ported (build clean; runtime pending) | notes/PF-10-multisupport.md | 21_PF-10-multisupport.patch |
| PF-10 | Baobab supports | abandoned (user) - attempted, removed | - | - |

## What to do next

**PF-10-paint** (per-style support painting + multi-pass dispatch) is ported on `port/PF-10-paint`
and merged into `port/integration` (build clean, runtime pending). It adds an extensible
`OrcaExt/SupportPaintTypes` registry (a new support = one row), extends the support gizmo with
Default/Snug/Grid/Organic/NeoWave, and runs one classic pass + one enforcer-only tree pass then
merges them, so different support engines coexist on one object. **PF-10-auto** (Automatic painting
button) was **rewritten on `port/PF-10-auto-fix`**: the support-preview-driven version did nothing at
runtime, so it is now a synchronous mesh analysis that scores each overhang region
(area/span/height/wall-angle/curvature/gap-below) against the registry's per-type rules, honours the
highlight angle / on-overhangs-only, never overwrites painted/blocker facets (split-safe
`TriangleSelector::painted_facet_mask`), and has per-type checkboxes + undo. Build clean and
`ctest` 807/807; runtime pending. **PF-10 Baobab was abandoned by user decision**
(attempted across 4 rounds, then fully removed: code, keys, paint row, `port/PF-10` branch;
`port/integration` reset to `c1e9289f6c`). Do not re-attempt unless the user asks. PF-3/4/5/7/8 were
excluded; PF-6 and PF-9 are ported (PF-9 runtime-verified).

One ID per branch (`port/PF-<n>`), product commit then a separate `port:` docs+patch commit, push,
`--no-ff` merge into `port/integration`, push. Don't merge an incomplete feature.

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

Every ported feature compiles and links. Runtime/visual-verified by the user so far: PQ-1, PF-1,
PF-6 and PF-9 (PF-9 after several geometry fixes). Pending: MT-1/MT-2, PQ-2, MT-4 F1/F2/F3, AS-1,
AS-2, AS-3, SU-1/SU-4/SU-5/SU-6, PQ-3, SU-4b (both sides), PF-2a (Smart bridging) and PF-2b (paint
smart/partial). Protocols are in each `notes/<ID>.md`.

## Cheat-sheet

- Target: `OrcaSlicer/` | Neotko fork: `OrcaFS-NeotkoCM/` | preFlight: `preFlight/` |
  Snapmaker base for Neotko diffs: `snapmaker/OrcaSlicer` tag `v2.3.5`.
- Isolate a Neotko change: `git -C OrcaFS-NeotkoCM diff v2.3.5 HEAD -- <path>`.
- Port artifacts: `OrcaSlicer/porting/` (plan, guidelines, 2 inventories, manifest, notes,
  patches, BUILD).
- Startup: `OrcaSlicer/build/src/Release/orca-slicer.exe`.
