# SU-3 - Real floating-object detection

- Source: `NEOTKOCM_RELEASE_2_39.md`.
- Fork: `OrcaFS-NeotkoCM` @ `c3508e5a92`.
- Category: B. Self-contained (most independent of the batch).

## Module
- `src/libslic3r/InstanceContact.{hpp,cpp}` (shared with SU-1):
  - `analyze_object()` (~80), `island_rests_on_other_object()` (~169)
  - constants: `CONTACT_GAP_THRESHOLD_MM=0.01`, `FOOTPRINT_EROSION_MM=0.02`, sample cap 256

## Upstream anchors (fork lines)
- `src/libslic3r/GCode.cpp:1776-1786` - lazy contact cache + `contact()` lambda
- `src/libslic3r/GCode.cpp:1816-1848` - empty-first-layer site; throw at `:1844`
- `Support/TreeSupport.cpp:854-864` - sharp-tail suppression when an island rests on another object

## Keys
- none. Thresholds are compile-time constants.

## Gates to remove
- Debug only (`NeoDebug::CONTACT`, `ORCA_DEBUG_CONTACT`).

## Coupling / blockers
- Uses only `InstanceContact`; no `GravityFloor`.
- Shares the GCode empty-first-layer block with AS-1. Porting SU-3 alone leaves the `throw` in place (stock behavior) - the measured gap is only appended to the message.
- Requires all objects' `lslices` to be stable.

## Verification
- Object stacked on another no longer warns floating; a genuinely floating island warns per island; two instances judged independently.

## Open questions
- At fork HEAD the warning-vs-throw decision is keyed on `neotko_true_objects`, not SU-3. Decide how AS-1 and SU-3 combine (AS-1 note).
