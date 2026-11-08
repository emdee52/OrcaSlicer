# Upstream sync 2026-11-08

Third sync of mainline `OrcaSlicer/OrcaSlicer` into the port. This file records the decisions the
merge forced, so a later session can review it without re-reading every hunk.

## Facts

| item | value |
|------|-------|
| merge base | `6d2869c430` (2026-10-27, the tip merged by `porting/notes/UPSTREAM_SYNC_20261027.md`) |
| upstream tip | `e23af6d7b0` (2026-11-08, first commit after the 2.6.0-dev bump) |
| commits merged | 191 |
| range size | 883 files, +73 425 / -50 701 lines |
| branch | `sync/upstream-20261108`, from `port/integration` `e3f043e3f8` |
| backup ref | `backup/port-integration-20261108` |
| merge commit | `b4efe45661` "Merge upstream/main (191 commits, e23af6d7) into the fork" |
| conflicts | 9 files, 9 hunks, all resolved |
| files both sides changed | 60 (the other 51 merged silently and are the review surface) |
| upstream deletions | 64 files (the vendored `deps/EXPAT/expat` and `deps_src/expat` trees, FELIX/FlyingBear profile assets) - none fork-modified |
| `ORCAPORT` markers | 1212 in 190 files before and after the merge |

Conflict style was `zdiff3`. `HEAD` in the working file was the port, `upstream/main` upstream.

`v2.5.0` (`40ecc66f`, 2026-11-06) is *not* a usable merge target: the release branch diverged from
our base (merge base `65aab6bc`), so its history would come along as a second line. Tip of `main`
is 9 commits past the release cut and contains the release content.

## What upstream brought in this range

Only what touches files the port also edits is listed.

- **Tree support rework**: base areas united in groups (`e23af6d7`), one overhang per layer
  (`d8807e03`), faster bottom-contact search (`33da2fab`), tips in a fixed order (`5191a5ef`),
  index arrays in the MST (`d4e57d86`), per-call profiler timers dropped (`561b8445`),
  `a4a51008` build-plate-only clearance fix. These land under SU-1/SU-4/SU-5's code but in
  different functions, so they applied silently.
- **Outline rework** (`670db4b772` "Faster and better outline", `4a01d5dda3` "Outline options"):
  the selection outline moved to `SelectionOutline.cpp` with its own shaders, `3DScene.cpp` lost
  ~187 lines, and the Preferences dialog gained an outline page. This is the largest silent change
  in the range for the GUI.
- **Geometry import with body names** (`fa5f7e3db4`): one STEP/AMF/3MF path, `ModelObject::
  imported_parts_hidden` and `ModelVolume::import_display_order` are new serialized fields,
  `convert_multipart_object()` gained `preserve_volume_names`, and `Plater.cpp` grew ~574 lines.
- **Documented G-code changes**: wipe tower M400 stalls (`cb80a0db`), precise-Z sliver layers joined
  to the tower below (`1c9f6f93`), travel back from a lift (`99fada6a`), wipe-inward retraction
  splitting (`0b041740`), bridge speed while slowing for curled perimeters (`533d4c3e`), printing
  filament's retraction lift (`98b539c3`), `printing_filament_types` placeholder (`f273fe8a`),
  belt-printer brim height (`3ac1b007`), `Line3::midpoint()` replacing hand-rolled midpoints.
- **Slice sharing**: identical objects no longer share sliced layers when their height ranges
  differ (`6d312b04`), by-object list order fixes (`719fdcf4`, `a620d804`, `8cdc68a6`), spiral
  vase override warning (`70c12f5c`), prime-tower requirement checks gated on a tower being
  printed (`7906d7e0`), post-slice extruder masks widened to 64 bits (`5ea64864`).
- **Dependencies**: EXPAT is now a pinned download (2.10.0) instead of a vendored tree and is
  always built on Windows, libjpeg-turbo installs into `lib`, wxWidgets moved to
  `b44c39c100975a0ac0e46f937aa3f3eae5e18631` with `-DXML_STATIC` for its expat use.
- **Preferences**: the dialog was reorganised into Behaviour and Files tabs (`d9f6b695`), with
  dark-mode/outline items added.
- Version bump to **2.6.0-dev** (`da77fcee`).

## Resolutions

Union unless noted. `HEAD` = the port.

- `src/libslic3r/Model.hpp` - upstream's `has_individual_part_settings()` /
  `hides_imported_parts()` declarations kept next to PF-2's `is_counterbore_bridge_painted()`.
- `src/libslic3r/Model.cpp` - same union; `has_individual_part_settings()` additionally counts
  `is_counterbore_bridge_painted()`, so a PF-2-painted part keeps the object's parts visible under
  upstream's new "hidden imported parts" rule.
- `src/libslic3r/Print.cpp` - `identical_objects()` keeps SU-1's cross-object support gate and
  gains upstream's layer-config-range comparison; both `return false` before the shared `return
  true`.
- `src/libslic3r/GCode.cpp` - SU-8's `transition_segment()` speed override kept in front of
  `tag_belt_band()`, which now takes `line.midpoint()` (upstream's new `Line3` API). All three
  `tag_belt_band` call sites in the file use the `Point3` form.
- `src/slic3r/GUI/GLCanvas3D.cpp` - AS-3/SNAP-8's anonymous namespace (snap-drag tuning, grouping,
  overlay helpers) kept, upstream's new `is_only_one_button_down()` helper added after it.
- `src/slic3r/GUI/Gizmos/GLGizmosManager.cpp` - **upstream wins**: `c78272f583` deleted the
  "turn realistic view off while painting" block and its member. The port only contributed
  `CounterboreBridge` to that list, so the block and PF-2b's marker line are gone; PF-2b's painter
  now paints under realistic view like upstream's do.
- `src/slic3r/GUI/Preferences.cpp` - the MT-1/MT-2 idle-power block and the MCP-1 toggle keep their
  place in the general page; upstream's reorganisation left that section intact, so only the
  include block needed a union (`OrcaExt/IdleToolPowerDown.hpp` + `libslic3r/Color.hpp`).
- `src/slic3r/GUI/Tab.cpp` - SU-10's reworded soluble-interface message ("0 top and bottom Z
  distance") kept, upstream's sentence-joining fix (newline to space) adopted on top.
- `tests/libslic3r/test_meshboolean.cpp` - MB-2's includes plus upstream's `<libslic3r/Point.hpp>`
  and `<cstddef>`.

## Build trap (cost one cycle)

`deps/EXPAT` changed from a vendored source tree to a downloaded pin in this range, so the dep's own
build directory kept a CMake cache pointing at the old source:

```
CMake error : The source ".../dep_EXPAT-prefix/src/dep_EXPAT/CMakeLists.txt" does not match the
source ".../deps/EXPAT/expat/CMakeLists.txt" used to generate cache.
```

Fix: delete `deps/build/dep_EXPAT-prefix/src/dep_EXPAT-build` and the
`dep_EXPAT-configure`/`-build`/`-install`/`-done` stamps under
`deps/build/dep_EXPAT-prefix/src/dep_EXPAT-stamp/Release`, then rebuild deps. Recorded in
`porting/BUILD.md`. The rebuilt prefix reports `Found EXPAT ... 2.10.0` and links
`lib/expat.lib`, which upstream's own legacy-lib copy step produces on MSVC.

## Verification

| gate | result |
|------|--------|
| Release build (`build_win.bat -ds -j 8`, then `-s`) | clean, 0 errors, 23 min |
| `ctest --test-dir build/tests -C Release` | **3232 / 3233 pass**, 11 skipped; the one failure (2300 "The status stream reconnects when the printer stops answering", SEGFAULT, Moonraker `StatusStream`) passes on re-run - a timing-sensitive upstream test, not a merge regression |
| `porting/scripts/verify-markers.ps1` | OK - every marker paired, every new-file stamp listed; same 26 legacy warnings as before the merge |
| marker count | 1212 in 190 files, identical before and after |
| patch series (`git apply --check -R`) | **73 of 90 fail both before and after the merge** - the set is identical, so this sync breaks no patch; see "Left open" |
| off-by-default G-code | see below |

**Off-by-default equivalence.** Compared stock `e23af6d7b0` against the merged tree, both built from
the same `deps/build` prefix with `ALL_BUILD`, both slicing the same project file
(`sync_equiv.3mf`: the `tests/data/test_stl/ASCII/20mmbox-LF.stl` box with
`Elegoo Centauri Carbon 0.4 nozzle` + `0.20mm Standard @Elegoo CC 0.4 nozzle` +
`Elegoo PLA @ECC` baked in) through the CLI (`--slice 0 --outputdir ...`).

- fixture: 8650 lines on the fork, 8574 on stock;
- G-code **body identical**: the only difference inside the print is the `; generated by OrcaSlicer
  2.6.0-dev on ... at ...` timestamp line;
- trailing config dump: the fork adds 76 lines, **all of them the port's own keys at their off
  defaults** (`hybrid_*`, `interlock_*`, `seam_notch_*`, `seam_type`, `support_interface_weave_*`,
  `support_interface_*_contact_*`, `transition_*`, `support_zone_*`, `wavesupport_*`,
  `material_automation*`, `initial_layer_bottom_surface_flow_*`, `inner_wall_overhang_*`,
  `mmu_segmented_region_extra_walls`, `counterbore_bridge_layers`, `bridge_expansion_extra`,
  `bed_temperature_offset`, `support_cross_object_avoidance`). The diff has **no deletions and no
  changed stock value**.

Stock upstream does not compile on this machine without `[BUILDFIX:VS17.8]`: the comparison build needed
the `store_move_vertex` capture in `GCodeProcessor.cpp` and `config_substitution_rule` added to the
`ensure_cli_preset_bundle`, `resolve_preset`, `load_config_file` and `load_project_preset` lambdas in
`OrcaSlicer.cpp` (6 call sites), i.e. the same workaround the fork carries. Nothing else differed:
the comparison build is upstream `e23af6d7b0` plus those captures only.

## Left open

1. **Patch series.** 73 of the 90 `porting/patches/*.patch` files no longer apply to `HEAD` and did
   not apply before this merge either; the merge neither broke nor fixed any. Regenerating them
   means rebasing ~70 feature branches onto the post-merge integration, which is a separate piece of
   work, not part of a sync. Re-measure with the loop in this note before starting it.
2. `manifest.tsv` still has no rows for the later port phases (`EF-*`, `HF-*`, `CUT-*`, `MV-*`,
   `RF-1`, `PF-2`, `SU-8..13`) and 23 markers are still `// ORCAPORT:` comments no script finds.
3. The stale gettext catalogs are still parked in `stash@{2}`. Upstream refreshed every catalog in
   this range, so the stash is now older than the tree as well as incomplete.
4. The per-feature MCP runtime pass. The G-code equivalence check covers defaults only; features
   touched by this merge (SU-8/9/10, PF-1, BT-1, MA-1, AS-1, MB-3/4/5) still want a run in the app.
5. `BUILD.md` used to say to *rebase* onto upstream; it now documents the merge flow used here.
