# Upstream sync 2026-11-19

Fourth sync of mainline `OrcaSlicer/OrcaSlicer` into the port. This file records the decisions the
merge forced, so a later session can review it without re-reading every hunk.

## Facts

| item | value |
|------|-------|
| merge base | `e23af6d7b0` (2026-11-08, the tip merged by `porting/notes/UPSTREAM_SYNC_20261108.md`) |
| upstream tip | `8db30fdb30` (2026-11-19) |
| commits merged | 123 |
| range size | 498 files, +38 285 / −12 108 lines |
| branch | `sync/upstream-20261119`, from `port/integration` `7d3133df54` |
| backup ref | `backup/port-integration-20261119` |
| merge commit | `3df45c66a0` "Merge upstream/main (123 commits, 8db30fdb) into the fork" |
| conflicts | 16 files, 48 regions, all resolved |
| files both sides changed | 70 (the other 54 merged silently and are the review surface) |
| upstream deletions | none that the port modified. `Model.cpp`'s copy of `add_cut_volume()` went away upstream (moved to `CutUtils.cpp`); the port did not touch it |
| `ORCAPORT` markers | 1192 in 190 files before and after the merge |

Conflict style was `zdiff3`. `HEAD` in the working file was the port, `upstream/main` upstream.
`rerere` was on; no recorded resolution was reused, all 16 were resolved by hand.

## What upstream brought in this range

Only what touches files the port also edits is listed.

- **Per-role flow and speed moved into helpers** (`c49e32c1e7`, "Set feature speeds in mm³/s or % of
  max volumetric speed"): the role flow-ratio chain left `GCode::_extrude` for
  `GCode::extrusion_mm3_per_mm()`, and each speed became `FEATURE_SPEED(<feature>, _mm3_per_mm)`.
  New keys `enable_volumetric_speeds`, one `<feature>_volumetric_flow` per feature, and
  `filament_max_external_volumetric_speed`. This is the one that collides with MA-1/BF-1.
- **Per-nozzle settings for toolchangers** (`34de2b49df`, #17461): `sync_excluder()` and
  `UnsavedChangesDialog` were rewritten (831 lines) so the copy goes to every compatible tool and the
  dialog picks which parameters travel. New `ParameterUtils` helpers.
- **Painted modifiers** (`db9968b9ee`, #16213): `ModelVolumeType::PAINTED_MODIFIER`, a new
  `GLGizmoPaintedModifier`, `model_painted_modifier_data_changed()`, paint data in the `.3mf`.
- **Filament per feature**: `brim_filament_id` (`12c9ee813b`) - a brim can print in another filament.
- **Sparse infill strategy selector** (`8b3de0be86`, `sparse_infill_strategy`), **multiline
  rectilinear infill** optimization (`6268535d2f`, `FillRectilinear.cpp` +235), **layer cooling timed
  with acceleration** (`8a1cef365e`, `CoolingBuffer.cpp` +274).
- **Cut keeps per-part filament colours** (`97ee8d9197`, `CutUtils.cpp` +106), **assembly view**
  (`0291395132`), **variable layer height painted on objects** (`9a285770d4`), `LabeledCheckBox`
  (`ff93256eff`), plugin permission/audit rework + new `PluginFsUtils`.
- **Dependencies**: OpenSSL 3.5.9, libcurl 8.23.0, OCCT built with PCH, miniz 3.1.2, bundled uv
  0.13.7, a Boost.Locale patch for CMake 4.5. All of it forces a deps rebuild.
- **Tests**: ~15 new files (`test_bridge`, `test_cooling`, `test_painted_modifier`, `test_cut`,
  `test_brim`, `test_measure`, `test_slicing`, `test_plugin_update`, three CLI shell tests).

## Resolutions

`HEAD` = the port. Union unless noted.

- `src/libslic3r/GCode.cpp` (5 regions) - **the semantic one**. Upstream's `extrusion_mm3_per_mm()`
  now applies the role flow ratios and the sub-layer ratio that the port's inline chain applied, so
  that chain is gone and the port's recipes scale what the helper returns:
  - MA-1's top-surface recipe *replaces* `top_solid_infill_flow_ratio`, so the merge divides that
    ratio back out before applying it: `_mm3_per_mm *= material->top_flow / profile_top_flow`
    (guarded on `profile_top_flow > 0.`; a zero ratio already means no extrusion).
  - BF-1's first-layer bottom-surface reduction and MA-1's material first-layer reduction are extra
    factors and simply multiply after the helper.
  - The three speed overrides go through `feature_speed()` instead of setting a bare mm/min, so MA-1
    keeps its recipe under `enable_volumetric_speeds` too: outer wall, `erBridgeInfill` (overhang
    walls and support transitions deliberately keep the profile bridge speed), and top surface.
  - SU-8's per-segment transition block, SU-9's weave speed and SU-10's contact speeds are unchanged.
    `_mm3_per_mm` lost its `const` for the reductions above.
- `src/slic3r/GUI/Tab.cpp` (11 regions), `Tab.hpp`, `UnsavedChangesDialog.cpp/.hpp` - **upstream
  wins**. #17461 replaces `get_extruder_sync_enable_state()` and `update_extruder_sync_button()` with
  `get_extruder_sync_targets()` / `extruder_sync_enabled()`, and its dialog copies to every
  compatible tool with per-parameter checkboxes. That supersedes the port's "All" entry from
  `76d0fe6dcf`: the capability is kept by upstream's default behaviour, the menu entry is not.
  The port's `extruder_switch_label()` survives on top of upstream's new `extruder_display_name()`
  helper and is declared in `Tab.hpp`; the printer tab's tags now come from upstream's loop (which
  appends the nozzle flow type) rather than from `extruder_switch_label()`.
- `src/libslic3r/Model.hpp/.cpp`, `PrintApply.cpp`, `GLGizmoPainterBase.hpp`, `GLGizmosManager.cpp` -
  PF-2/PF-2b counterbore paint kept next to upstream's painted modifiers: `is_any_painted()` counts
  both, `counterbore_bridge_facets` and `painted_modifier_facets` are saved, loaded and reset side by
  side, `CounterboreBridge` and `PaintedModifier` are both in the section-view and mouse-wheel lists,
  and `_extrude`'s invalidating predicate carries both `*_data_changed()` calls. `Model.cpp`'s
  `add_cut_volume()` copy is dropped (unused; upstream moved it to `CutUtils.cpp`).
- `src/libslic3r/Print.cpp` - BF-1's two keys kept in the no-invalidate list next to upstream's
  `filament_max_external_volumetric_speed`.
- `src/slic3r/GUI/GUI_Factories.cpp` - MB-5's `split_to_object` part-menu item (the port's call has
  two more arguments than upstream's) plus upstream's new `append_menu_item_add_painted_modifier()`.
- `src/libslic3r/GCode.hpp`, `src/slic3r/GUI/Plater.cpp` - unions (PQ-3/PF-1 members, UI-3's include).
- `tests/fff_print/CMakeLists.txt`, `tests/libslic3r/test_model.cpp` - unions (`test_painted_modifier`,
  the MB-3..MB-6 cases, `<string>`/`<cstddef>`).

## Build trap (cost one cycle)

`CMakeLists.txt:212` downloads the bundled uv with `file(DOWNLOAD ... TLS_VERIFY ON)` - hardcoded,
so it ignores the `CMAKE_TLS_VERIFY=0` this machine needs, and the configure dies with
`Failed to download uv: "SSL connect error"` before anything compiles.

Fix, without touching upstream's file: fetch the pinned archive out of band and verify the hash the
pin names, then let CMake skip the download because the binary is already there.

```powershell
$dir="$repo\build\.uv\0.13.7"; New-Item -ItemType Directory -Force $dir | Out-Null
curl.exe -sSL --ssl-no-revoke -o "$dir\uv.zip" `
  https://github.com/astral-sh/uv/releases/download/0.13.7/uv-x86_64-pc-windows-msvc.zip
(Get-FileHash "$dir\uv.zip" -Algorithm SHA256).Hash   # 0cdc5b38...6e021, matches ORCA_UV_SHA256_*
Expand-Archive "$dir\uv.zip" -DestinationPath $dir -Force; Remove-Item "$dir\uv.zip"
```

The deps themselves rebuilt clean (`-ds`): OpenSSL, libcurl, OCCT, miniz all went through. No repeat
of the EXPAT trap of the last sync.

## Verification

| gate | result |
|------|--------|
| deps + Release build (`build_win.bat -ds -j 8`, then `-s --no-configure`) | clean, 0 errors |
| `ctest --test-dir build/tests -C Release` (`-s --run-tests`) | **3561 / 3561 pass**, 20 skipped (symlink/8.3-name tests that do not run on Windows), 561 s |
| `porting/scripts/verify-markers.ps1` | OK - every marker paired, every new-file stamp listed; the same legacy warnings as before (bare `// ORCAPORT:` comments, no PF-2/PP-2 rows) |
| marker count | 1192 in 190 files, identical before and after |
| CLI slice smoke | `--slice 0` on `tests/data/test_stl/ASCII/20mmbox-LF.stl` with `Elegoo Centauri Carbon 0.4 nozzle` + `0.20mm Standard @Elegoo CC 0.4 nozzle` + `Elegoo PLA @ECC`: 8662-line G-code, no `nan`/`inf`, version line `2.6.0-dev`, the port's keys present at their off defaults (`support_interface_weave_enable = 0`, `transition_interface_base_speed = 100`, `material_automation` empty) |
| off-by-default equivalence vs stock `8db30fdb30` | **not run** - needs a second build tree of upstream plus the `[BUILDFIX:VS17.8]` captures; see "Left open" |

## Left open

1. **Stock-vs-fork G-code equivalence** for this range, the way the 20261108 sync did it. The CLI
   smoke test proves the build slices, not that a default slice matches stock. The risk concentrates
   in `GCode::extrude()` (this sync rewrote how MA-1/BF-1 hook in), `CoolingBuffer.cpp` (upstream
   retimed cooling; the SU-8 fan-zone hooks were re-checked and their line-type bits still do not
   collide) and `FillRectilinear.cpp`.
2. **Patch series.** 73 of the 90 `porting/patches/*.patch` files did not apply before this merge
   either; the set is unchanged. Regenerating them means rebasing ~70 feature branches.
3. The stale gettext catalogs are still parked in `stash@{2}`; upstream refreshed more catalogs.
4. A per-feature MCP runtime pass on what this range touched: PF-2b's painter, MMU paint, brim ears,
   MA-1's recipes, MB-3/4, and upstream's new nozzle sync (the port's "All" is gone, so check the
   toolchanger copy path in the app).
5. `manifest.tsv` still has no rows for the later phases (`EF-*`, `HF-*`, `CUT-*`, `MV-*`, `PF-2`,
   `SU-8..13`); 23 markers are still `// ORCAPORT:` comments no script finds.
