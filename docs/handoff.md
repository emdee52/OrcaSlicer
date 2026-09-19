# Handoff — multi-material support interface (SU-6 … SU-12)

Session summary so the next session can pick up, and a map of everything built here. All work is in
`src/libslic3r` + GUI plumbing; nothing is merged into `port/integration` yet.

---

## 1. Build notes (read first)

Windows build is run from the repo root with the VS environment preamble:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\bin;$env:PATH"
$env:CMAKE_TLS_VERIFY = "0"
.\build_win.bat -s --no-configure -j 8
```

`-j 8` is the normal build. Drop to `-j 4` only when the machine is already memory-pressured
(e.g. weeks of uptime, or a game running alongside the build). The one corruption seen before was
exactly that edge case, not a property of `-j 8`: the compiler OOMs on the precompiled header
(`C1076: compiler limit: internal heap limit reached`) and leaves a **corrupt `cmake_pch.pch`**.
The retry then reports "up to date" and links a binary with mixed class layouts, which shows up as
an **access violation on every slice**. Recovery:

```powershell
Remove-Item "build\src\libslic3r\libslic3r.dir\Release\cmake_pch.*" -Force
Remove-Item "build\src\slic3r\libslic3r_gui.dir\Release\cmake_pch.*" -Force
.\build_win.bat -s --no-configure -j 4
```

`-j 4` builds libslic3r in ~20-36 min; `-j 8` is faster. Kill `orca-slicer.exe` before building
(it holds the DLL). The binary is `build/src/Release/orca-slicer.exe`.

Verification was done with the `orca-slicer_*` MCP tools: load -> apply config -> slice -> export
G-code -> parse `;TYPE:` / `F` / `E` lines.

---

## 2. Features built this session

### SU-6 — Base-interface layer count — `support_interface_base_layers`
Number of interface layers nearest the base reprinted with the **base** filament. `-1` = automatic
(1 when base/interface filaments differ), `0` disables. Implemented in
`Support/SupportParameters.hpp` (overrides `num_top/bottom_base_interface_layers`).
Status: verified, committed on `port/SU-6-7`.

### SU-7 — Anchor pins — REMOVED
The fixed-pitch square pins (`support_interface_anchor_pins/_spacing/_size`) were scrapped: a pin had
to fit entirely inside the region, so narrow/ragged footprints got few or none. Superseded by SU-9.
The keys no longer exist.

### SU-8 — Transition-layer treatment (`[ORCAPORT:SU-8]` on `port/SU-8`)
Layer deposited on a different filament than the layer below. Three independent groups
(`PrintObjectConfig`), each `_enable` / `_layers` (1-5) / `_speed` (%<=100) / `_flow` (%) /
`_fan` (%, -1 off) / `_temp_delta` (°C, signed, multi-nozzle only):

- `transition_interface_base_*` (A: interface on base support)
- `transition_object_interface_*` (B: object on support interface)
- `transition_interface_object_*` (C: interface on object / bottom contact)

Slice-time tagging: `Layer::transition_joint` and `Layer::transition_area` set in
`generate_support_toolpaths`; consumed by `GCode::m_transition_joint`.
- speed/flow applied in `GCode::_extrude` **only inside `transition_area`** (so the whole layer is
  not affected);
- fan via a `;SU8_FAN <pct>` marker parsed in `CoolingBuffer::apply_layer_cooldown`;
- temperature emitted as an explicit wait command at the layer's extrusion point, tracked **per
  filament** (`m_transition_temp_last`) so different objects on the same plate do not fight.

### SU-9 — Woven interface (`[ORCAPORT:SU-9]` on `port/SU-9`)
Mechanical interlock for a non-bonding interface (PETG base / PLA interface). Options:
- `support_interface_weave_enable` (bool, off)
- `support_interface_weave_layers` (int 1-6, default 2)
- `support_interface_weave_pitch` (float mm, default 2.0)
- `support_interface_weave_flush` (bool, off) — woven **base** strips printed at **0.9x height** so
  they do not telegraph through.

Mechanism (`SupportCommon.cpp`): on the first K **top-interface** layers above the base, each
connected interface component is split into an even number of alternating base/interface strips sized
from its own width (min 2). Base strips print with the base filament; the rest stays interface. The
strip direction rotates 90° on alternate layers, so crossing strips lock laterally. Only
top-interface layers are woven; base-material layers are untouched; the top 2 layers of each stack
(contact + one solid interface) are reserved solid. Woven layers use one speed
(`support_interface_speed`) for both roles via `Layer::support_weave` -> `GCode::m_weave_layer`.

### SU-10 — Contact interface speed/line width (`[ORCAPORT:SU-10]`)
Split into top and bottom (the values that work for one do not work for the other):
- `support_interface_contact_speed` (mm/s absolute, 0 off) / `_line_width` (mm or %, 0 off) —
  **top** contact (interface under the object).
- `support_interface_bottom_contact_speed` / `_line_width` — **bottom** contact (interface on top of
  an object).

`Layer::support_contact_top` / `support_contact_bottom` -> `GCode::m_support_contact_top/bottom`;
speed in `_extrude`, width in `extrude_interface` (flow + fill spacing).
A `support_interface_base_perpendicular` option was added here then **removed** (see SU-11).

### SU-11 — Interface edge bridging (`[ORCAPORT:SU-11]` on `port/SU-11`)
The weave/base-interface edge threads had nothing to anchor to. Three opt-in checkboxes:
- `support_interface_serpentine` (off): one top-to-bottom serpentine **per strip** (per connected
  component) along the strip's long axis, ordered from the inner side outward. Replaces the generic
  fill for the woven layer. Per-strip so connectors never cross the other material.
- `support_interface_base_bridge` (off): base-interface layer under the weave prints straight,
  perpendicular to the support base pattern (bridges the sparse base). When on, the weave is shifted
  90° so the woven threads **cross** the base-interface instead of running parallel.
- `support_interface_perimeter` (off): closed perimeter loop around each woven layer; the strips are
  generated from a region inset by one line width so the loop does not collide with them.

### SU-12 — Base interface line width (`[ORCAPORT:SU-12]` on `port/SU-12`)
`support_interface_base_line_width` (mm or % of nozzle, 0 off): line width of the base-material
interface layer under the weave.

### SU-13 — Woven bottom interface + settings consolidation (`[ORCAPORT:SU-13]` on `port/SU-13`)
Mirror of SU-9 for support resting on an object. `support_interface_bottom_weave_enable` (bool, off)
weaves the **base-interface layer directly above a bottom contact** (the lowest base-interface layer
of the stack) with interface-material strips, so the base support above is keyed to the object-side
interface. Reuses the SU-9 `_weave_layers`/`_weave_pitch`; `_flush` does not apply. The bottom
contact is never woven; higher base-interface layers stay solid. Host detection uses a new
`SupportGeneratorLayer::is_bottom_base_interface` flag, set where the base-interface layer is
projected from a bottom contact.

When the toggle is enabled the UI forces `support_bottom_interface_spacing=0`,
`support_bottom_z_distance=0`, and `support_interface_base_layers=1` if it was `0` (the weave needs
a solid zero-gap host). The support-interface suggestion popup was extended to zero the bottom
spacing/Z as well.

**Settings consolidation (same branch):**
- Removed `support_interface_serpentine`, `support_interface_base_bridge`,
  `support_interface_perimeter`. Serpentine fill, straight base-interface bridging, and the
  base-material perimeter are now implicit whenever weave is on. Straight bridging is applied only
  while weaving, so defaults stay byte-identical. Existing 3mf/preset files with the old keys load
  fine (unknown keys are ignored).
- New **"Multi material"** section on the Support page (`support_interface_base_layers`,
  `support_interface_weave_enable`, `_weave_layers`, `_weave_pitch`, `_weave_flush`,
  `support_interface_bottom_weave_enable`, `support_interface_base_line_width`) and a
  **"Transition layers"** section (all 18 SU-8 keys). Both are shown only when `support_filament`
  and `support_interface_filament` are both explicitly set and resolve to different `filament_type`s.
  Contact speed/line width stay in Advanced so single-material users keep them.
- The gate is computed in `TabPrint::toggle_options()` from the preset bundle; every line in the two
  groups is toggled together, so an all-hidden group collapses (title included).

Verified on `interface on object test.3mf` (MCP): the z=3.65 base-interface layer carries both
T0/PETG (`Support`) and T3/PLA (`Support interface`) extrusion, the bottom contact is dense, and
`[SupportMaterial]` Catch2 tests pass (including a new bottom-weave role test).

---

## 3. Branch / commit map

| Branch | Commits |
|---|---|
| `port/SU-6-7` | `b5e07903f3` SU-6/7 |
| `port/SU-8` | `d0748eaeb1`, `189d75e673`, `07a0482872` |
| `port/SU-9` | `c26faaf369`, `9126e854cd`, `51b2aada4e` |
| `port/SU-10` | `3712aad6f2`, `daae8f3454` |
| `port/SU-11` | `26bb253519`, `791638257b` |
| `port/SU-12` | `3bf873ff96` (SU-12), `feb198f139` (SU-10 top/bottom split) |
| `port/SU-13` | `4addbe2c97` (SU-13 bottom weave + settings consolidation) |

Branches are a linear chain (`SU-8` off `SU-6-7`, `SU-9` off `SU-8`, ..., `SU-13` off `SU-12`), so
`port/SU-13` contains everything. All pushed to `origin`. Merge `--no-ff` into `port/integration`
only when print-verified.

---

## 4. Recommended working values (from the user's prints)

- **Top contact**: `support_interface_contact_speed = 55` mm/s,
  `support_interface_contact_line_width = 0.32` mm. Mirrors the user's perfect top-layer PLA profile;
  smooth PLA surface, PETG releases from it well.
- **Bottom contact**: needs its own values (55/0.32 does **not** work there).
- **Weave**: **1 woven layer is sufficient** to hold the higher interface layers during print.
- **Base-interface layer** under the weave is the most common failure point (edge droop) — SU-11
  targets it.
- Interface-side scarring was further reduced by increasing interface flow.

---

## 5. Gotchas / limitations

- **Temperature is per physical nozzle.** Per-object `transition_*_temp_delta` values on objects that
  share a tool cannot be independent. Print temperature variants on **separate plates** (or different
  tools). Fan is likewise per-layer/global and cannot be spatially scoped.
- **Weave band**: only top-interface layers are woven; the top 2 layers of each stack stay solid. A
  4-layer interface stack therefore yields **1 woven layer**; add interface layers for more.
- **MCP bool quirk**: `apply_config` with JSON `true` stored `0`; pass `1`/`0` (integers).
- New UI strings were added; `run_gettext.bat` runs during the build but localization catalogs were
  not updated/committed.

---

## 6. Key code locations

- `src/libslic3r/Support/SupportCommon.cpp` — `generate_support_toolpaths`: weave/transition tagging,
  top weave host marking (SU-9), bottom weave host marking (SU-13, lowest base-interface layer of a
  bottom stack), `split_weave_strips`, `emit_strip_serpentine`, `emit_region_perimeter`, and the
  base-interface fill (SU-12 width).
- `src/libslic3r/Support/SupportLayer.hpp` — `SupportGeneratorLayer::is_bottom_base_interface`
  (SU-13), set in `SupportCommon.cpp::insert_layer`.
- `src/libslic3r/GCode.cpp` — `process_layer` (sets `m_transition_joint`, `m_weave_layer`,
  `m_support_contact_top/bottom`; `emit_transition_temp`), `_extrude` (speed/flow overrides).
- `src/libslic3r/Layer.hpp` — `transition_joint`, `transition_area`, `support_weave`,
  `support_contact_top`, `support_contact_bottom`.
- `src/libslic3r/PrintConfig.{hpp,cpp}` — all options above; `Preset.cpp`, `PrintObject.cpp`
  (invalidation), `OrcaMCP/OrcaExtTools.cpp` (MCP key list).
- `src/slic3r/GUI/ConfigManipulation.{hpp,cpp}` — per-line visibility; the `multi_material_support`
  parameter gates the "Multi material"/"Transition layers" lines.
- `src/slic3r/GUI/Tab.cpp` — the two new Support-page sections (~line 2999); `TabPrint::toggle_options`
  computes the filament-type gate and passes it in; the support-interface popup (~line 2053) and the
  bottom-weave toggle handler (~line 2095).

---

## 7. Open items / likely next changes

- Print-verify the bottom-contact overrides and the SU-11 edge-bridging options, then tune.
- Regenerate the test matrix with the new weave/contact options (previous matrix files predate them).
- Tree/organic support coverage: the weave/edge code is on the classic + NeoWave toolpath path;
  tree floors/roofs would need handling.
- Merge the SU branches into `port/integration` once print-verified.
- Consider whether per-object vs per-part overrides are wanted for these keys.
