# Upstream sync 2026-10-27

Second sync of mainline `OrcaSlicer/OrcaSlicer` into the port. This file records what the merge
had to decide, so a later session can review it without re-reading every hunk.

## Facts

| item | value |
|------|-------|
| merge base | `109d09e0b8` (2026-10-15, the tip merged by `porting/notes/UPSTREAM_SYNC_20261015.md`) |
| upstream tip | `6d2869c430` (2026-10-27); `v2.5.0-beta` (`345efec64c`) is 9 commits behind it |
| commits merged | 441 |
| range size | 1066 files, +112,556 / -118,194 lines |
| branch | `sync/upstream-20261027`, from `port/integration` `c713a251cb` |
| backup ref | `backup/port-integration-20261027` |
| merge commit | `73d471ad01` "Merge upstream/main (441 commits) into the fork" |
| merged into `port/integration` | `1b7483adcc`, after the owner tested the build (2026-10-28) |
| conflicts | 14 files, 20 hunks, all resolved |
| files both sides changed | 77 (the other 63 merged silently and are the review surface) |
| upstream deletions | 12 files, none of them fork-modified |
| `ORCAPORT` markers | 1104 in 183 files before and after the merge - no anchor was dropped |

Local `main` was fast-forwarded to `6d2869c430` with `git fetch upstream main:main --no-tags`
(no checkout). Conflict style was `zdiff3`, so each hunk also showed the merge base; `HEAD` in the
working file was the port and `upstream/main` was upstream.

The port does not touch `resources/profiles`, so the vendor-bundle version bumps in this range
(`a50b0a167b`, `e28af4e5f2`) applied cleanly. It also does not touch the `.po` catalogs (only
`localization/i18n/list.txt`, +3 lines), so the translation commits merged without a conflict.

## What upstream brought in this range

Only what affects files the port also edits is listed; the rest of the 441 commits is upstream's own.

- **Slicing performance.** Islands are generated in parallel (`1fc153308f`, `6d34d83e78`,
  `3167c3665a`, `d905f1a39b`), travel chaining rewritten (`69193d5681`, `54…`), Arachne wall order
  fixed at pinched holes (`176b1dc63d`, `0acf1e6680`). `process_arachne()` in
  `PerimeterGenerator.cpp` is now a `tbb::parallel_for` over a results array instead of a plain loop.
- **Bed exclusion volumes** (`19a454746a` and its 20-commit follow-up chain): multiple,
  height-aware volumes, multi-nozzle travel routing, exclusion-aware brim/support/arrange. This is
  the largest single feature in the range and it rewrote parts of `PartPlate.cpp`, `PrintObject.cpp`
  and `GCode.cpp`.
- **Settings UI: "explain why a setting is hidden or disabled"** (`acb4cce4ae`, #16657). Every
  `toggle_line` / `toggle_option` call in `Tab.cpp` and `ConfigManipulation.cpp` gained a reason
  string; the signature is `toggle_line(key, enabled, opt_index = -1, reason = {})`, so the port's
  unmigrated call sites still compile and simply show no tooltip.
- **Include hygiene**: forward declarations moved into `MainFrame.hpp`, `Plater.hpp`,
  `GUI_App.hpp` (`2a6e7a199b`, `d0966a0502`, `cbbe590230`) and `nlohmann/json.hpp` pulled out of
  widely included headers (`9cb9f9694f`). This is the main source of follow-up compile errors.
- **Dependencies**: OpenCV 4.6 -> 4.14 (`1008a47870`), the FreeType dependency removed
  (`1b8bc0b19d`, which also deletes `src/libslic3r/Shape/TextShape.{hpp,cpp}` and the Korean fonts).
  `deps/` changed, so the build tree needs a reconfigure and a deps build.
- **Fixes that touch port behaviour**: unindexed vector placeholders read the wrong filament slot
  (`e52c735d1a`, #16211); the re-applied config no longer discards the slice that just finished
  (`7125a546b8`, #15180 - relevant to MA-1's *Apply and reslice*); the prime tower no longer decided
  from the previous model's filaments (`4c7928d8aa`); by-object printing orders tall objects later
  (`a30ca5e54a`); extruder printable-area checks for non-BBL multi-nozzle printers
  (`c7bc1b27a5`); Moonraker `lane_data` filament matching (`3e8cd1a505`).
- `GUI_App::m_http_server` became a `std::unique_ptr<HttpServer>`, and `HttpServer.hpp` moved
  `io_service` to `io_context`.

## Resolutions

Union unless noted. `HEAD` = the port.

**Mechanical unions (12 hunks).**

- `docs/HLSD/filament_id.md` - the port's `filament_sync_profile_map` paragraph stays, upstream's
  reworded sentence ("Today the Bambu AMS integration ...") is adopted.
- `src/slic3r/GUI/ConfigManipulation.cpp` - PQ-2's inner-wall slowdown block plus upstream's
  `flush_into_objects` reason string.
- `src/slic3r/GUI/Gizmos/GizmoObjectManipulation.cpp` - MV-1's `m_move_window_extra_ui()` plus
  upstream's "coordinate system of the panel" wording (true after `49b433b69b`).
- `src/slic3r/GUI/PartPlate.hpp`, `src/slic3r/GUI/PartPlate.cpp` - BT-1's bed-temperature readout and
  MA-1's material badge stay next to upstream's new `uses_plate_name()` /
  `sync_plate_name_to_print()`.
- `src/slic3r/GUI/Plater.hpp` - `MixedFilamentResult` forward declaration plus upstream's
  `ObjectDataViewModelNode`.
- `tests/fff_print/CMakeLists.txt` - the port's three test files plus upstream's
  `test_multimaterial_segmentation.cpp`.

**Where one side had to win, or both had to be combined.**

- `src/slic3r/GUI/Tab.cpp` (EX-1, 2 hunks) - upstream added reason strings to the air-filtration rows
  but still indexes them with `-1` / `0`, so it has not fixed the bug EX-1 addresses. The port's
  `256 + variant_index` indices and per-variant `opt_bool` reads are kept and upstream's
  `air_filtration_reason` strings are adopted on top.
- `src/OrcaSlicer.cpp` (`[BUILDFIX:VS17.8]`, 2 hunks) - upstream's `load_config_file` gained
  `&sliced_info` and `load_project_preset` gained `refresh_unlisted`; both are kept and the
  `config_substitution_rule` capture is re-added to each, because the installed toolchain is still
  MSVC 14.38 (VS 17.8) and rejects the implicit capture with C3493.
- `src/slic3r/GUI/GLCanvas3D.cpp` (4 hunks) - `2457173764` (#16501) replaced the five
  `if (!mo->auto_drop) continue;` early-outs with an inline `mo->auto_drop &&` so the object list
  still gets its sink icon. The port's AS-1 gate `!OrcaExt::free_z()` is a different condition and is
  kept alongside it: `mo->auto_drop && !OrcaExt::free_z() && (...)`. In the move path the AS-3
  Snap & Drag branch stays first and the stock bed snap becomes its `else if`, which also stops the
  `continue` from skipping `obj_list()->update_info_items()`.
- `src/slic3r/GUI/GUI_App.cpp` (2 hunks) - MCP-1's three-argument `/mcp` router keeps its body and
  moves to `m_http_server->` to follow upstream's `unique_ptr`; the one-argument OAuth overload is
  untouched.
- `src/slic3r/GUI/Gizmos/GLGizmoMove.cpp` - upstream's `49b433b69b` resets the remembered selection
  to `-2` on gizmo entry so the coordinate system is recomputed; MV-1's `clear_face_frame()` is kept
  beside it. MV-1 does not compare against `-1` anywhere, so the sentinel change is free.
- `src/libslic3r/PerimeterGenerator.cpp` - the conflicting region is upstream's new parallel Arachne
  island loop; the port only had the PF-9 "interlocking perimeters are Classic-only" note there. The
  note moved above the loop; PF-9's Classic anchor is untouched.
- `src/libslic3r/Support/TreeModelVolumes.cpp` (SU-1, 2 hunks) - upstream switched the outline loop
  to `outline_pos`. SU-1's "last processed outline" is the last **non-empty** one (the injected
  cross-object group can be empty and sorts last), so it is now `last_processed_outline_pos`,
  resolved up front, and `processing_last_mesh = outline_pos == last_processed_outline_pos`.
  Upstream's `outline_pos + 1 == size()` is the equivalent of the old `outline_idx ==
  layer_outline_indices.back()` and was not used.

## Verification

Committed on `sync/upstream-20261027`: merge `73d471ad01`, then the two fallout commits
`413cdcc542` (includes) and `1f8b3b250e` (TB-1's Shift+W).

| gate | result |
|------|--------|
| Release build (`build_win.bat -ds -j 8`, then `-s --no-configure`) | clean, 0 errors |
| `ctest --test-dir build/tests -C Release` | **2888 / 2888 pass**, 9 skipped (the pybind sandbox and tree-grant set, unchanged) |
| `porting/scripts/verify-markers.ps1` | OK - every marker paired, every new-file stamp listed; the same legacy warnings (23 non-conforming `// ORCAPORT:` comments, no rows for `PF-2` / `PP-2`) |
| marker count | 1104 in 183 files, identical before and after the merge |
| off-by-default G-code | **byte-identical to stock** (below) |

**Build trap (cost one cycle).** `deps/` changed in this range (OpenCV 4.6 -> 4.14, FreeType
removed), so the merge needs a reconfigure and a deps build. The reconfigure then failed in
`dep_OCCT` with `C1083` on pre-8.0 package paths (`src/OSD/OSD.hxx`): OCCT's header collector skips
rewriting the generated `include/occt/*.hxx` shims when the first one already exists, so the old
layout survived the source update. Deleting that directory plus the `dep_OCCT`
configure/build/install/done stamps and rebuilding only that dep fixed it; nothing in the merge was
involved. Recorded in `porting/BUILD.md`.

**Off-by-default equivalence.** Comparing the pre-merge fork against the merged tree is *not* the
right yardstick this time: upstream itself changed emitted G-code in this range (travel chaining,
gap-fill and infill ordering, the `M73` progress placement), so the two fork builds legitimately
differ - 12010 vs 11270 lines on the fixture below. The claim that matters is that the fork is
neutral at its own defaults, so the check is **stock upstream `6d2869c430` vs the fork at
`73d471ad01`**, built from the same `deps/build` with the same commands and sliced with the same
profile trio (`Prusa CORE One 0.4 nozzle`, `0.20mm SPEED @CORE One 0.4`, `Generic PLA @Prusa CORE
One`) on `tests/data/test_stl/ASCII/20mmbox-LF.stl`.

- G-code body: **identical**, 10443 lines each, zero differing lines.
- Trailing config dump: the fork adds 76 lines, all of them the port's own keys at their off
  defaults (`hybrid_*`, `interlock_*`, `seam_notch_*`, `support_interface_weave_*`,
  `transition_*`, `support_zone_*`, `wavesupport_*`, `material_automation*`,
  `initial_layer_bottom_surface_flow_*`, `inner_wall_overhang_*`, `mmu_segmented_region_extra_walls`,
  `counterbore_bridge_layers`, `bridge_expansion_extra`, `bed_temperature_offset`). No stock key's
  value changed - the diff has no deletions.

Stock upstream does not build on this machine without the port's `[BUILDFIX:VS17.8]` captures: MSVC
14.38 rejects `normal_mode` in `GCodeProcessor::store_move_vertex` and `config_substitution_rule` in
five `OrcaSlicer.cpp` lambdas. The stock build used the fork's `OrcaSlicer.cpp` and the one capture in
`GCodeProcessor.cpp`, which is the only difference between the two files, so the comparison is
unaffected - and it confirms the workaround is still required and behaviour-neutral.

## Left open

Same four items `UPSTREAM_SYNC_20261015.md` recorded, deliberately kept out of this merge by the
owner's decision:

1. Regenerating the `porting/patches/*.patch` series for the features this merge touched (EX-1,
   PF-9, SU-1, AS-1/AS-3, MV-1, BT-1, MA-1, MCP-1).
2. `manifest.tsv` still has no rows for the later port phases (`EF-*`, `HF-*`, `CUT-*`, `MV-3`,
   `RF-1`, `PF-2`, `SU-8..13`), and 23 markers are still `// ORCAPORT:` comments no script can find.
3. The stale gettext catalogs, still parked in `stash@{1}` ("gettext --full regeneration 2026-10-16").
4. The per-feature MCP runtime pass; the off-by-default G-code equivalence check is done for this
   sync (see Verification) and only needs repeating when a port feature's default changes.
