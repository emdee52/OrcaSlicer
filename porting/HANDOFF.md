# HANDOFF - read this first

This is the start-here doc for a new agent session on this workspace. It summarizes where the
port stands, the hard-won operational rules, and the next task (PQ-3). The detailed, per-feature
truth lives in the other `porting/` docs; this file tells you what to read and what bit us.

## Read order

1. `OrcaSlicer/porting/HANDOFF.md` (this file)
2. `OrcaSlicer/porting/PORTING_PLAN.md` (scope, phases, decisions)
3. `OrcaSlicer/porting/PORTING_GUIDELINES.md` (mandatory coding format)
4. `OrcaSlicer/porting/NEOTKO_FEATURE_INVENTORY.md` (source list)
5. `OrcaSlicer/porting/manifest.tsv` (status per feature ID)
6. `OrcaSlicer/porting/notes/<ID>.md` for the feature you are about to work on
7. `OrcaSlicer/porting/BUILD.md` before building

Root `AGENTS.md` has the workspace mission and session protocol.

## Where the port stands

Working branch: **`port/integration`** (this is what to build and branch from). `main` is the
untouched upstream mirror. `origin` = `https://github.com/emdee52/OrcaSlicer.git`.

| ID | Feature | Status | Notes file | Patch |
|----|---------|--------|-----------|-------|
| PQ-1 | Bridging infill extra expansion | ported | notes/PQ-1.md | 01_PQ-1.patch |
| MT-1 | Hotends that finished switch off | ported | notes/MT-1.md | 02_MT-1-2.patch |
| MT-2 | Extra Energy Save | ported | notes/MT-2.md | 02_MT-1-2.patch |
| MT-3 | Bed hottest filament | upstream-present (skip) | notes/MT-3.md | - |
| MT-4 | MMU painter Pro Mode F1-F3 | ported (F4 dropped) | notes/MT-4.md | 04/05 |
| SU-1 | PerObject Support | ported | notes/SU-1.md | 09_SU-1.patch |
| SU-4 | NeoWave (wave-roof half) | ported | notes/SU-4.md | 10_SU-4.patch |
| SU-5 | Support Zones aimed pillars | ported | notes/SU-5.md | 11_SU-5.patch |
| SU-6 | Support Zones block trees | ported (with SU-5) | notes/SU-6.md | - |
| AS-1 | Free-Z placement | ported | notes/AS-1.md | 06_AS-1.patch |
| AS-2 | Align & Stack + face-mate (AS-2F) | ported | notes/AS-2.md | 07_AS-2.patch |
| AS-3 | Snap & Drag | ported (full pass) | notes/AS-3.md | 08_AS-3.patch |
| PQ-3 | NeoArachne (a+b together) | ported | notes/PQ-3a.md, PQ-3b.md | 12_PQ-3.patch |

SU-2 (True Objects) and SU-3 (real floating detection) are **excluded** by user decision.
AS-2F extra decisions: picked-instance only; extras = depth/rotate/flip/mirror; feature-center
snapping included (click-time, not hover).

## Environment & build

**Windows only, local builds only. Do not use GitHub Actions.** Read `porting/BUILD.md`.

Before every build shell (PowerShell), apply:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\bin;$env:PATH"
$env:CMAKE_TLS_VERIFY = "0"
```

`CMAKE_TLS_VERIFY=0` is mandatory: Windows Schannel's revocation servers are unreachable here,
so CMake's dependency downloads fail TLS otherwise. Prereqs already installed: VS 2022, Kitware
CMake, Strawberry Perl, Git.

Build commands:

```powershell
# add/remove a source file -> needs configure
.\OrcaSlicer\build_win.bat -s -j 8
# header/source edit only -> incremental
.\OrcaSlicer\build_win.bat -s --no-configure -j 8
```

Result binary: `OrcaSlicer/build/src/Release/orca-slicer.exe` (a thin launcher; the real code is
in `OrcaSlicer/build/src/Release/OrcaSlicer.dll`). A full clean build is ~20 min; incremental
1-3 min. Builds are run **detached** (see "How builds are run" below).

## Operational lessons (these cost real time - do not rediscover)

- **The repo rejects a UTF-8 BOM in source files** (`encoding-check-libslic3r_gui`). Never write
  source with PowerShell `Set-Content`/`Out-File` (they add a BOM and may re-encode to mojibake).
  Use the editor tools, or if you must script a patch, use
  `[IO.File]::WriteAllText($f, $t, (New-Object System.Text.UTF8Encoding($false)))`.
- **Mojibake in a string literal desyncs ImGui's UTF-8 decoder and mangles the text after it.**
  The fork files contain double-encoded bytes in comments (harmless) and previously in two
  literals (not harmless). If UI text looks wrong, grep the file for non-ASCII and check the
  literals, not just the comments.
- **`LNK1104: cannot open OrcaSlicer.dll`** means a running `orca-slicer.exe` holds the DLL. Kill
  all `orca-slicer`/`OrcaSlicer` processes before linking:
  `Get-Process orca-slicer,OrcaSlicer -ErrorAction SilentlyContinue | Stop-Process -Force`.
  The user tests while you work, so expect this.
- **`edit` anchors must be unique.** The gizmo had two `if (m_face_pick_mode) {` (one in
  `on_mouse`, one in the panel) and an edit landed in the wrong one. Always grep for the anchor
  first; if it occurs more than once, include more context.
- **Tab-aligned lines break `edit`.** Some fork files indent with literal tabs. Use a targeted
  regex replace via `[IO.File]::WriteAllText` (no BOM) for those lines.
- **Builds are run detached and polled**, because a build can take 20 min and the shell tool
  reaps the wrapper. Start with `Start-Process cmd /c build_win.bat ... -RedirectStandardOutput`,
  write the log path to `build-logs/current-build.paths`, then poll
  `Select-String -Path <log> -Pattern ": error "`. Expect a spurious `ChildProcess.kill` message;
  the detached build survives it. `build-logs/` is gitignored.
- **Do not `-c` to "fix" a code error** - that discards a working tree. `-c` is only for a bad
  configure/toolchain change.

## Git workflow used

- One feature per branch `port/<id>`, branched from `port/integration`.
- Commit product code first, then a separate `port:` commit for `manifest.tsv` + `notes/` +
  the regenerated patch.
- Patch files: `porting/patches/NN_<ID>.patch`, generated with
  `git diff "<base>..HEAD" -- src resources` where `<base>` is the integration commit the
  feature branched from (NOT `port/integration..HEAD`, which is empty once merged).
- Push the feature branch, then merge `--no-ff` into `port/integration` and push that.
- `manifest.tsv` is the source of truth; keep it and the notes in sync with the code.
- Do not push unless the user asks. (The user has been asking; commits here are already pushed.)

## All selected features ported - PQ-3 was the last

SU-6 is **ported** - it came in with SU-5 (same three files at the fork's HEAD), verified by
line-level parity, and has no separate patch. See `notes/SU-6.md`.

SU-5 is **ported** on `port/SU-5`: probe, per-volume keys, data model, per-zone slicing, the corridor
engine, per-zone filament routing, PrintObject invalidation, and the full `GLGizmoSupportZones` UI
(including the block-tree / stump mode). Build clean; runtime pending. See `notes/SU-5.md`.

The next task is PQ-3 NeoArachne (a+b together) - the largest and riskiest port; do it last.

PQ-3 NeoArachne is **ported** on `port/PQ-3` (a+b together): the `Slic3r::NeoArachne` module
(12 files), `wall_generator = "hybrid"` (label NeoArachne), the `HybridWallSource` enum, the
11 `hybrid_*` region keys, the Arachne `BeadingStrategyFactory`/`WallToolPaths` hookup, the
spiral-lift plumbing, and the Quality > Wall generator UI/validator. The preview panel is
excluded (user decision). Build clean; runtime pending. See `notes/PQ-3b.md`. This was the
last selected feature: the port is feature-complete pending runtime verification.

SU-4 is **ported** on `port/SU-4` (wave-roof half): `SupportType::NeoWave` handled by the existing
Normal engine, `FillWaveRoof`, and the `wavesupport_*` keys. The contact layer is dropped (needs the
unselected ColorStitch pack). Build clean; runtime pending. See `notes/SU-4.md`.

SU-1 is **ported** on `port/SU-1`: `OrcaExt/InstanceContact` (cross-object occupancy), the
`support_cross_object_avoidance` key + GUI, and the tree / classic-grid / organic / pipeline
integration. Build clean; runtime pending. See `notes/SU-1.md`.

AS-3 is **ported** on `port/AS-3` (full pass): the `GravitySnap` module, the magnet options panel,
the landing-shadow overlay, the live-drag resolve and the `do_move` commit. Build clean; runtime
pending. See `notes/AS-3.md`. The AS-3 recon below is kept for reference.

### AS-3 recon (historical)


Full recon: `porting/notes/AS-3.md`. Branch `port/AS-3` already exists (empty). Key facts:

- New module: copy `OrcaFS-NeotkoCM/src/slic3r/GUI/GravitySnap.{hpp,cpp}` (~530 lines). It is
  GUI-only (decides where a dragged instance lands), self-contained, uses `GLVolumeCollection`,
  `AABBMesh::query_ray_hit`, ClipperUtils, ConvexHull.
- Adapt: drop `NeoDebug::GRAVITY` lines; replace `gravity_allow_free_z()` with
  `OrcaExt::free_z()` (include `libslic3r/OrcaExt/FreeZ.hpp`); rename keys
  `neotko_snap_drag*` -> `orca_ext_snap_drag*`; drop/replace `plate_icon_available()` (it gates
  on LibreMode in the fork). Register both files in `src/slic3r/CMakeLists.txt`.
- Fork integration points (map to target by symbol, not line): `GLCanvas3D.cpp` live-drag
  resolution (~5031-5235), `do_move` commit (~5684-5810), `SnapDragIndicator` overlay class and
  its render, the ImGui options panel (~10840-10911); `GLCanvas3D.hpp` state +
  `SNAPDRAG_ENGAGE_RATIO = 0.20`; `PartPlate.cpp` magnet icon; `Plater.cpp` panel toggle.
- Behavior rules that MUST be preserved (from the notes): `floor_z_for_instance` returning
  `nullopt` means "leave it floating", never "drop to Z=0"; footprint overlap (not raycast under
  cursor) decides the floor; a dragged group never rests on itself; commit must match the live
  drag (by-stacks or rigid-group) or the selection visibly jumps on mouse-up.
- Recommended first pass to cut risk (confirm with user): port `GravitySnap`, add the three
  app-level toggles as **Preferences checkboxes** (the established pattern for MT-1/2 and AS-1),
  and integrate only the `do_move` commit + live-drag resolve. Defer the magnet panel/icon and
  the decorative landing-shadow overlay, then add them if wanted.

### App-level preference pattern (copy this for AS-3)

Established with MT-1/MT-2 and AS-1:

1. Small module `src/libslic3r/OrcaExt/<Name>.{hpp,cpp}` holding a global (atomic) the GUI sets
   and libslic3r/slicing reads. Register in `src/libslic3r/CMakeLists.txt`.
2. Checkbox in `src/slic3r/GUI/Preferences.cpp` (General page): sets `app_config` key
   (`orca_ext_*`), saves, updates the global. Pattern is inlined next to the MT-1/2 checkboxes.
3. Sync the global from `app_config` at startup in `src/slic3r/GUI/GUI_App.cpp` (next to the
   `OrcaExt::set_*` calls added for MT-1/2/AS-1).

For AS-3 specifically, GravitySnap already reads `app_config` directly each call, so you may not
need the global; just rename the keys and read `OrcaExt::free_z()` for the free-Z gate.

## Remaining after AS-3

- SU-1 PerObject Support (notes/SU-1.md): needs `InstanceContact`; **critical uncouple** - remove
  the `neotko_true_objects` OR term in `cross_object_active()`; requires serial support generation.
- SU-4 NeoWave **wave-roof half only** (notes/SU-4.md): the contact layer depends on the
  unselected ColorStitch pack, so it is out. Wave roof is a large SupportMaterial duplicate.
- SU-5/SU-6 Support Zones (notes/SU-5/6.md): one large subsystem; land SU-5 before SU-6.
- PQ-3 NeoArachne a+b together (notes/PQ-3a/3b.md): largest and riskiest; do last.

## Built but not runtime-verified (user should test)

Every ported feature compiles and links; only PQ-1 was runtime-confirmed by the user. Pending
manual checks: MT-1/MT-2 (multi-tool G-code with Ooze prevention on), PQ-2 (overhang inner-wall
slowdown), MT-4 F1/F2/F3 (paint gizmo), AS-1 (free-Z placement), AS-2/AS-2F (align/stack, face
mate, extras, snapping), SU-1/SU-4/SU-5/SU-6 (supports), PQ-3 (slice with `wall_generator =
NeoArachne` and sweep the per-feature selectors + pin/hysteresis/transition knobs). Test
protocols are in each `notes/<ID>.md`.

## Cheat-sheet

- Target tree: `OrcaSlicer/`  |  Source fork: `OrcaFS-NeotkoCM/`  |  Base for diffs:
  `snapmaker/OrcaSlicer` (tag `v2.3.5`).
- Isolate a fork change: `git -C OrcaFS-NeotkoCM diff v2.3.5 HEAD -- <path>`.
- Port artifacts (this docs set): `OrcaSlicer/porting/`.
