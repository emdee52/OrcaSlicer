# Marker registry gaps (housekeeping, 2026-10-16)

Written while closing out the manifest gaps flagged by `UPSTREAM_SYNC_20261015.md` and
`scripts/verify-markers.ps1`. No code was changed; this is a record of what was found and what
was done about it.

## What was fixed

`manifest.tsv` gained rows for the eleven IDs that had markers in the tree but no row:

| ID | Feature | Status |
|----|---------|--------|
| CUT-1 | Cut plane aligned to a picked face | merged (`08a73dd0d3`) |
| CUT-2 | Cut a single part of a multi-part object | merged (`353e64f18c`) |
| CUT-3 | Shaped ("cookie cutter") cut | merged |
| SNAP-8 | Alt-drag object-to-object point snap | merged |
| MV-3 | Alt pivot snap in the Move gizmo | merged |
| HF-2 | Fill from above (inside the Cut gizmo) | merged, user-verified |
| SU-8 | Support transition layers (interface/base) | ported |
| SU-9 | Woven support interface | ported |
| SU-10 | Support contact interface line width | ported |
| SU-11 | Support interface loop / boustrophedon | ported |
| SU-13 | Support bottom-stack weave and flush | ported |

## Root cause 1 - three different marker formats

The rows were missing because the markers are not written consistently, and a bracket-based
check only finds one form:

| Form | Example | IDs using it |
|---|---|---|
| Bracketed | `[ORCAPORT:CUT-1]` | most IDs; for CUT-1 only in `OrcaMCPGizmoTools.cpp` |
| Unbracketed comment | `// ORCAPORT: MV-3` | MV-2, MV-3 (`GLGizmoMove.*`) |
| Plain feature comment, no marker at all | `// CUT-2`, `// CUT-3`, `// HF-2` | CUT-2, CUT-3, HF-2 (`GLGizmoCut.*`); HF-2 also has `// HF-2` in `CutUtils` |

So CUT-2, CUT-3 and HF-2 have **no `[ORCAPORT:...]` marker anywhere** - only descriptive
comments. That is why they never appeared in a marker scan and never got a row. Verify them by
grep for the bare ID plus a read of the surrounding code, not by a bracket pattern.

**Decided (user, 2026-10-16): do NOT normalise these comments.** The marker form stays as it
is, so `verify-markers.ps1` will keep reporting CUT-2, CUT-3 and HF-2 as unmarked; the rows in
`manifest.tsv` are the record instead. Verify those three by grepping the bare ID in
`GLGizmoCut.*`.

## Root cause 2 - SU-8..SU-13 are our own build, not Neotko ports

SU-8..SU-13 are one coherent family (a woven support-interface treatment), and the evidence
says the code is **ours, not Neotko's**:

- `src/libslic3r/Support/SupportCommon.cpp` carries **no `NEOTKO_*` tag at all** (the only
  `NEOTKO_SUPPORTZONES_TAG` comments in that directory are in `SupportMaterial.cpp`, for SU-5/SU-6).
- Neotko's fork has **no** equivalent feature and no matching keys: `git grep -i
  "support_interface_weave|woven interface|entrelaz"` over the fork returns nothing, and the
  fork's only support-interface keys are the stock set plus `support_neoweave_*` (our SU-4b).
- The key family is registered in our tree under a single group tag: `Preset.cpp` and
  `PrintObject.cpp` both label `support_interface_weave_*` as `[ORCAPORT:SU-9]`.

**Confirmed with the user on 2026-10-16: these were built in one of their own sessions**, so
`source_release: original` is correct and there is no fork release note or sha to record. The
rows no longer say "unconfirmed".

## The SU family, for reference (verified)

| ID | Keys | Where |
|---|---|---|
| SU-8 | none - signalled by a `;SU8_FAN <percent>` G-code marker the generator emits; `CoolingBuffer` parses it | `SupportCommon.cpp`, `GCode.cpp`, `GCode.hpp`, `GCode/CoolingBuffer.cpp` |
| SU-9 | `support_interface_weave_enable`, `_layers`, `_pitch`, `_flush` | `SupportCommon.cpp` (+ `PrintConfig`, `Preset`, `PrintObject`) |
| SU-10 | `support_interface_contact_line_width`, `support_interface_bottom_contact_line_width` | `SupportCommon.cpp` (+ config plumbing) |
| SU-11 | reuses upstream `support_interface_loop_pattern` | `SupportCommon.cpp`, `GCode.cpp` |
| SU-13 | `support_interface_bottom_weave_enable` (+ `support_interface_weave_flush` shared with SU-9) | `SupportCommon.cpp`, `SupportLayer.hpp` (+ config plumbing) |

All of it is inert at defaults (`support_interface_weave_enable` off), so it does not affect
stock output.

## Still open

- `SU-12` has no marker anywhere in the tree. Either it was never used or it was folded into a
  neighbour; not reused for new work.
- The marker form of `MV-3`, `CUT-2`, `CUT-3` and `HF-2` is deliberately left as it is (user
  decision, 2026-10-16). They are tracked by their `manifest.tsv` rows, not by a marker scan.
