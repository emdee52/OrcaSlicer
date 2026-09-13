# Porting Guidelines - how our OrcaSlicer changes must be written

Applies to every edit made under `OrcaSlicer/` for the features listed in
`PORTING_PLAN.md`. The objective is that the port survives an upstream refresh: when the
user updates OrcaSlicer (preferably by rebasing the port branch, otherwise by dropping in
newer upstream files), our changes can be re-applied quickly, mechanically, and with
localizable conflicts. The conventions below are mandatory; a change that ignores them is a
review failure even if it works.

Companion docs: `PORTING_PLAN.md` (what/when), `NEOTKO_FEATURE_INVENTORY.md` (source list),
`OrcaSlicer/AGENTS.md` (upstream code style and review rules - still binding).

---

## 1. Where port artifacts live

All port documents and machine-readable artifacts live **inside the target repo**, under
`OrcaSlicer/porting/`, so they are versioned with the code and pushed to the user's
OrcaSlicer fork. A fresh session, a fresh clone, or a new machine gets the entire port
definition from a single `git pull`.

```
OrcaSlicer/porting/
  PORTING_PLAN.md              # what/when: selected IDs, phases, decisions
  PORTING_GUIDELINES.md        # this file: conventions and update workflow
  NEOTKO_FEATURE_INVENTORY.md  # source list and categorization
  manifest.tsv                 # one row per feature: files, keys, markers, source sha
  patches/
    01_MT-3.patch              # git format-patch output, applied in numeric order
    ...
  notes/
    MT-3.md                    # per-feature recon: exact files, keys, anchors, decisions
    ...
  scripts/
    apply.ps1                  # re-apply all patches, verify markers (Phase 0 deliverable)
    verify-markers.ps1         # check BEGIN/END pairing and expected anchors
  excluded/                    # diffs we deliberately did NOT port, for reference
```

Rule: if a future session cannot re-create a change from `OrcaSlicer/porting/` alone, it is
not properly packaged. GitHub is the durable copy; nothing about the port should live only
on one machine.

**Fresh-upstream-drop caveat.** Because `OrcaSlicer/porting/` is inside the tree, a raw replacement of
`OrcaSlicer/` would erase it. Prefer the rebase route (Section 8). If a raw drop is
unavoidable, restore `OrcaSlicer/porting/` from the fork (or copy it aside) before dropping
the new upstream files in.

---

## 2. The marker format (mandatory, exact)

Every change to an upstream file must be locatable by a later script and human without
reading the diff. Use these exact tokens. The token `[ORCAPORT:` is the machine key; the ID
must match a manifest row.

Inserted block (wraps code that was not there before):

```cpp
// [ORCAPORT:MT-1] BEGIN - <one-line reason>
...added code...
// [ORCAPORT:MT-1] END
```

Modified single line or expression (cannot be wrapped):

```cpp
// [ORCAPORT:MT-1] <one-line reason>
upstream_expression = orcaext_adjusted_value;
```

Inline, when a preceding comment would be awkward:

```cpp
const float t = plate_max_bed_temp(...); // [ORCAPORT:MT-3]
```

Rules:

- One feature ID per marker block. Never nest two IDs inside one BEGIN/END.
- Reasons are lower-case, factual, and explain the change, not the obvious.
- Never put a marker inside a string literal or a translation `msgid`.
- BEGIN/END must balance. A file may hold several blocks of the same ID.
- New files carry a header stamp instead of a block marker:

```cpp
// [ORCAPORT FILE] OrcaExt<Feature> - <purpose>
// Source: NEOTKOCM_RELEASE_<x>.md; fork sha <short-sha>
```

- `ORCAPORT` is a neutral technical name. Do not put Snapmaker, Neotko, "Libre", or any
  vendor word in a marker or in a new symbol.

---

## 3. Naming and layout conventions

- **Namespace:** all new code goes in `namespace Slic3r::OrcaExt` (or `Slic3r::OrcaExt::Gui`
  for GUI). Never add symbols to upstream namespaces if a nested namespace works.
- **New files:** `OrcaExt<Name>.hpp` / `.cpp`, PascalCase name, one feature one pair. Large
  modules get a folder: `src/libslic3r/OrcaExt/` and `src/slic3r/GUI/OrcaExt/`.
- **Classes:** `OrcaExt::<Name>`. Do not prefix with vendor or product names.
- **Tests:** `tests/libslic3r/TestOrcaExt<Name>.cpp`, following `tests/AGENTS.md`.
- **Print/profile config keys:** preserve the fork's existing key strings verbatim (confirmed
  policy) so existing 3MF files and profiles remain loadable; record them in the manifest.
  Rename only a key that contains branding, and record the old->new mapping.
- **App/preference keys:** prefix `orca_ext_` so they cannot collide with upstream
  preferences. Keep app-level machine preferences out of print profiles (MT-1/MT-2 must
  stay app-level, as the fork intends).
- **Config enum values:** preserve the fork's value strings (e.g. a wall-generator value).
  Do not rename an existing enum value.

---

## 4. Hook-minimisation rules

The fewer lines of upstream code we touch, the cheaper every upstream refresh is.

1. **Prefer new files over edits.** Logic lives in `OrcaExt` files; upstream files get the
   thinnest possible call.
2. **One anchor per concern.** An "anchor" is a marked insertion point in upstream code.
   Target <= 3 anchors per feature. Record every anchor in `OrcaSlicer/porting/notes/<ID>.md` as
   `file :: enclosing-function :: what is inserted`.
3. **Batch registration.** Where the upstream code has an init/registration function,
   register the whole port with a single appended call, e.g. one call to an
   `OrcaExt::init_<area>()` at the end of the existing function, rather than several
   scattered registrations. Confirm the exact function name in Phase 0.
4. **One-way dependencies.** Upstream code may call into `OrcaExt`. `OrcaExt` may read
   upstream public APIs. `OrcaExt` must not include GUI headers from libslic3r code, and
   features must not include each other's headers - shared logic goes in the shared module
   (Section 6).
5. **Observer over surgery.** If upstream exposes a hook/callback list, use it. Only edit
   inline where no seam exists, and keep the edit to one marked line.
6. **G-code features are one pass.** MT-1/MT-2 are a single post-process rewrite invoked
   from one anchor, with the analysis and emission in their own file.

---

## 5. Ungating rules (user requirement: ungate everything)

- Remove every Libre Mode check, `ORCA_DEBUG_*` check, and master-switch dependency around
  the selected features. Delete the Libre Mode gate as a concept; do not port it.
- A feature is exposed through its own option, default **off**. Visibility rules:
  - Always visible for normally-scoped options (MT-1/MT-2/MT-3/MT-4, PQ-1, PQ-2, AS-2,
    AS-3, AS-1).
  - Visible when its own prerequisite selector is set: NeoWave controls when support type
    is NeoWave; NeoArachne options when `wall_generator = NeoArachne`; PerObject Support
    checkbox under Enable support.
  - Disabled-with-tooltip is allowed when a prerequisite is unmet. Hidden is not allowed.
- Removing a gate must not change output at defaults. Prove it with an off-by-default
  comparison (Section 10).
- No new environment-variable gates. No "expert" double-locks.

---

## 6. Uncabling patterns

When two features share behaviour, or a selected feature depends on an unselected one:

- **Extract a neutral module.** The shared floor/contact measurement (needed by AS-1,
  AS-3, SU-1) is the canonical example. Put it in `src/libslic3r/OrcaExt/FloorProbe.*` or
  equivalent neutral name, expose a narrow interface, and have consumers call it.
- **Port only what was selected.** Do not pull in an unselected feature's side effects.
  Example: port the measurement behind Snap & Drag, but not True Objects' area-based
  bridge/contact classification. Write the behavioural gap into `OrcaSlicer/porting/notes/AS-3.md` so
  it is a known limitation and not a mystery later.
- **No feature-to-feature includes.** If AS-3 and SU-1 both need measurement, both include
  `FloorProbe`, and neither includes the other.
- **Interface injection at one anchor.** Where a shared provider must reach an upstream
  pipeline, pass it in at a single marked anchor rather than editing several call sites.
- **Sub-feature extraction.** AS-1 is the floating trigger plus the accurate per-island
  detection (SU-3) it consumes; both come from the shared `FloorProbe` module, not from
  Libre Mode. Do not drag in assembled boolean, per-volume XY compensation, copy/paste
  process settings, or the rest of Libre Mode. Each unselected sub-feature is an explicit
  non-goal in the notes file.

---

## 7. Manifest schema

`OrcaSlicer/porting/manifest.tsv`, tab-separated, one header row, one row per feature. Columns:

| Column | Meaning |
|--------|---------|
| `id` | Inventory ID (`MT-1`, `SU-5`, ...) |
| `title` | Short name |
| `status` | planned / in-progress / ported / verified / upstream-present / blocked |
| `source_release` | `NEOTKOCM_RELEASE_<x>.md` |
| `fork_sha` | Commit sha in `OrcaFS-NeotkoCM` |
| `upstream_ref` | OrcaSlicer 2.5 commit/PR if an equivalent exists |
| `new_files` | Semicolon-separated new files |
| `modified_files` | Semicolon-separated upstream files, each with its anchor count |
| `config_keys` | Semicolon-separated print/profile keys (verbatim) |
| `pref_keys` | Semicolon-separated app preference keys |
| `markers` | Marker IDs used; must match `id` |
| `tests` | Test files / manual checks |
| `notes` | Open decisions, known gaps, ungating done? |

The manifest is the single source of truth for what the port contains. `verify-markers.ps1`
cross-checks it against the tree: every listed marker exists and pairs, and no marker
exists that is not in the manifest.

---

## 8. Packaging, applying, and surviving upstream updates

**Per feature, when it lands:**

1. Work on a branch `port/<id>`; keep the feature's commits self-contained.
2. Generate the patch from the workspace root:
   `git -C OrcaSlicer format-patch -1 --no-numbered -o OrcaSlicer/porting/patches` and rename
   with a numeric prefix that preserves apply order (`NN_<id>.patch`).
3. Update `OrcaSlicer/porting/manifest.tsv` and `OrcaSlicer/porting/notes/<id>.md`.
4. Run `verify-markers.ps1` and the build/tests.

**When upstream OrcaSlicer updates (the user's main workflow):**

1. Preferred route: on the port branch, `git fetch` the upstream remote and `git rebase`
   onto it. `OrcaSlicer/porting/` is tracked in the same repo, so it travels with the
   branch and cannot be lost.
2. Fallback route (raw file replacement): restore `OrcaSlicer/porting/` from the fork first
   (it lives inside the tree, so a raw drop would erase it), then run
   `OrcaSlicer/porting/scripts/apply.ps1`. It applies patches in numeric order and aborts
   on the first failure, reporting the file, the marker, and the manifest row.
3. Because new files apply cleanly and anchor edits are one-liners wrapped in markers, most
   conflicts are confined to a handful of lines. Resolve each conflict against
   `OrcaSlicer/porting/notes/<id>.md`, which records the original intent, not just the diff.
4. Re-run `verify-markers.ps1`. Then build: `.\OrcaSlicer\build_win.bat -s`.
5. Run `ctest` and the off-by-default equivalence checks (Section 10).
6. If an upstream change has adopted a feature (category A), set its manifest status to
   `upstream-present`, drop the patch, and note it in the feature's notes file.
7. Commit the updated `OrcaSlicer/porting/` folder and push, so the fork stays the durable
   copy.

**Rules that make this work:** never reformat untouched code; never rename upstream
symbols; never scatter a feature across many upstream files; never let a feature's patch
depend on another feature's patch unless ordered in the manifest.

---

## 9. Git hygiene

- One feature per branch and per patch. No "misc fixes" commits mixed with a port.
- Commit subject: `port(<id>): <short description>`. Body records the source release,
  fork sha, and the ungating/uncoupling performed.
- Do not commit `build/`, `deps/build/`, or editor files with product code. Commit the
  `OrcaSlicer/porting/` folder deliberately (it is the port definition and must be pushed),
  in its own commit rather than mixed into a product-code commit.
- Do not commit or push without explicit instruction.

---

## 10. Verification requirements

For every feature:

1. **Off-by-default equivalence.** With the feature's options at defaults, the sliced
   output must match stock OrcaSlicer 2.5 byte-for-byte (or explain precisely, with a test,
   why not - e.g. MT-3 is an intentional always-on correctness fix).
2. **On-behaviour test.** The feature does what its notes describe. Prefer a Catch2 test for
   deterministic geometry/config logic; otherwise a documented manual protocol with a
   fixture file.
3. **Ungating proof.** The control works with no Libre Mode / env var set, verified by
   launching a clean profile.
4. **Regression scope.** Slicing logic changes get a targeted test; profile/format changes
   follow the version-migration rules in `OrcaSlicer/AGENTS.md`; GUI changes get a manual
   checklist.
5. **Build + lint.** Confirm the port compiles with `build_win.bat -s --no-configure` and
   that changed regions match the repo `.clang-format` (format changed regions only).

Record the evidence (command, expected, actual) in `OrcaSlicer/porting/notes/<id>.md`.

---

## 11. Review checklist (run before declaring a port done)

- [ ] Marker IDs match the manifest; every BEGIN has an END.
- [ ] New symbols are in `Slic3r::OrcaExt`; no vendor/branding strings anywhere.
- [ ] No Libre Mode / `ORCA_DEBUG_*` / master gate remains for this feature.
- [ ] No feature includes another feature's header; shared logic is in a neutral module.
- [ ] Anchor count is minimal and each anchor is documented in the notes file.
- [ ] Config/pref keys are recorded; existing 3MF/profile loading is preserved.
- [ ] Off-by-default output is identical; on-behaviour is tested.
- [ ] `OrcaSlicer/porting/patches/<NN>_<id>.patch`, `manifest.tsv`, and `notes/<id>.md` are current.
- [ ] Feature builds and the relevant tests pass.

---

## 12. Forbidden

- Touching `deps/` or `deps_src/`.
- Reformatting or re-ordering unchanged upstream code (including whole-file clang-format).
- Renaming or moving upstream symbols to "make room" for a port.
- Adding a feature without markers, or markers without a manifest row.
- A global "enable extended features" master switch, or any hidden gate.
- Copying fork code verbatim instead of re-implementing against 2.5 APIs.
- Branding: Snapmaker/Neotko names, logos, URLs, sponsor text, vendor profiles.
- Bundling two inventory IDs in one patch/commit.
- Claiming a feature works without a build and a verification record.
