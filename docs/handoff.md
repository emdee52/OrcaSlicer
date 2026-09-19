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

### SU-4b — NeoWave contact layer — REMOVED
The `support_neoweave_*` contact layer (part-bottom bridge-fill wave + support-top interface wave,
`OrcaExt/NeoWaveContact`, `NeoWaveContactTarget`) was removed completely on `port/SU-13` at the
user's request; it was not useful. Note the unrelated SU-4 **wave roof** (`wavesupport_roof_*`,
`FillWaveRoof`) remains. Existing files with the removed keys load fine (unknown keys are ignored).

### SU-6 — Base-interface layer count — REMOVED (reverted to hardcoded)
The `support_interface_base_layers` option was removed on `port/SU-13`: the base-material interface
layer count is hardcoded again exactly as before SU-6 exposed it (auto 1 when base/interface
filaments differ and there is more than one interface layer, otherwise 0; the soluble branch keeps
its `min(n/2, 2)` rule in `Support/SupportParameters.hpp`). One layer is enough; the weave host and
everything else rely on the automatic value.

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
- `support_interface_weave_layers` (int 1-6, default **1**, changed on `port/SU-13`)
- `support_interface_weave_pitch` (float mm, default 2.0)
- `support_interface_weave_flush` (bool, off) — woven **base** strips printed at **0.8x height** so
  they do not telegraph through (0.9x before `port/SU-13`).

Mechanism (`SupportCommon.cpp`): on the lowest K **top-interface** layers above the base, each
connected interface component is split into an even number of alternating base/interface strips sized
from its own width (min 2). Base strips print with the base filament; the rest stays interface. The
strip direction rotates 90° on alternate layers, so crossing strips lock laterally. Only
top-interface layers are woven; base-material layers are untouched; only the contact layer is
reserved solid (before `port/SU-13` the contact plus one interface was reserved, so a stack of fewer
than 3 interface layers could not weave at all). Woven layers use one speed
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
weaves a bottom stack against the object-side interface. Reuses the SU-9 `_weave_layers`/`_weave_pitch`;
`_flush` does not apply. The contact is never woven. Two shapes, chosen by the stack height:

- **Two interface layers** (contact + one base-material layer): that single base-interface layer is
  the weave host, with interface-material strips inserted; the base support lands on it.
- **Three or more interface layers** (inverse sandwich): the base-material layer under the base
  support stays **solid** — printed as one serpentine whose lines cross the weave below — and the
  interface layer directly beneath it becomes the host, with **base-material** strips inserted. So
  e.g. 5 bottom interface layers give `3x PLA -> 1 woven interface -> 1 solid base cap -> base
  support`, and the base support always bonds to solid base material rather than PLA strips.

Host detection uses `SupportGeneratorLayer::is_bottom_base_interface` and the `bottom_cap` /
`bottom_iface_weave` markers set during toolpath generation.

When the toggle is enabled the UI forces `support_bottom_interface_spacing=0` and
`support_bottom_z_distance=0` (the weave needs a solid zero-gap contact; the base-interface host
comes from the automatic 1-layer rule). The support-interface suggestion popup was extended to zero
the bottom spacing/Z as well. The bottom weave toggle sits directly under "Woven interface".

**Settings consolidation (same branch):**
- Removed `support_interface_serpentine`, `support_interface_base_bridge`,
  `support_interface_perimeter`. Serpentine fill, straight base-interface bridging, and the
  base-material perimeter are now implicit whenever weave is on. Straight bridging is applied only
  while weaving, so defaults stay byte-identical. Existing 3mf/preset files with the old keys load
  fine (unknown keys are ignored).
- New **"Multi material"** section on the Support page (`support_interface_weave_enable`,
  `support_interface_bottom_weave_enable`, `_weave_layers`, `_weave_pitch`, `_weave_flush`,
  `support_interface_base_line_width`) and a
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
| `port/SU-13` | `4addbe2c97` (SU-13 bottom weave + settings consolidation), `4a1b13ba8a` (handoff), `cd6d2a85dc`+`fb71627e21` (remove SU-4b/SU-6, SU-8 temp fix), `a4d3bc0972` (handoff) |

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
- **Weave band**: only top-interface layers are woven; only the contact stays solid. The weave can
  now sit directly under the contact, so a 3-layer top-interface stack yields 1 woven layer. The
  `support_interface_weave_layers` default is 1; raise it to weave more of the stack.
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
