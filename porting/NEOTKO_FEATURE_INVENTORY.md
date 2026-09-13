# Neotko FullSpectrum - Feature Inventory for Porting

Source: `OrcaFS-NeotkoCM/NEOTKOCM_RELEASE_2_3.md` ... `NEOTKOCM_RELEASE_2_45.md`
(Neotko 2.3 - 2.4.5, on a Snapmaker Orca 2.3.4/2.3.5 base).

**Read this first - scope and honesty:**

- This list is transcribed from Neotko's own release notes, **not** verified against the
  fork's diff or against upstream OrcaSlicer 2.5. Per `AGENTS.md`, the notes are an index,
  not ground truth. Every item must be confirmed against `v2.3.5..HEAD` before porting.
- "Since" is the first release note that describes the item, not necessarily the first
  commit.
- Effort (S/M/L/XL) and "value" are my **estimates**, not measured. They will move once the
  real diff is inspected.
- Features are grouped by **use case**, not by release order, so you can pick the areas you
  care about. IDs (e.g. `MT-2`) are for referring to a single item.
- Almost everything is **opt-in and off at defaults** by design. The large color-mixing
  pack is deeply interdependent - treat the `CM-*` group as one coupled unit.
- Category letters follow `AGENTS.md`: (A) already upstream, (B) portable, (C) branding /
  ecosystem - excluded, (D) conflict / needs rework.

Legend: **Status** = stable / experimental / WIP / untested per the notes.
**Cat** = suggested category (A/B/C/D). **Eff** = rough porting effort.

---

## 1. Surface color & material mixing (the FullSpectrum core) - `CM`

This is the fork's reason to exist. HUGE surface area, tightly coupled: ColorStitch,
the Sandwich pass stack, PathBlend, the Painter, NeoTower and RealColor all share data
formats and engine code. Porting one in isolation is likely impractical.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| CM-1 | **Surface ColorStitch / "Sandwich"** | Top/penultimate (later bottom) surfaces built as a stack of 1-3 passes; each pass Solid / ColorStitch / PathBlend with its own Z share + angle. Config keys `colormix_profiles_b64`, `paint_colormix`, `interlayer_colormix_*`. | 2.3 (base pack older) | stable-ish | D | XL |
| CM-2 | **ColorStitch Painter** (3D paint gizmo) | Paint colors per-line on top surfaces: palette groups, up to 254 slots/object, undo/redo, Pro mode, eyedropper, stickers. Reorganized into Palette/Generator/Pro/Object departments (2.3.4). | 2.3 | stable | D | XL |
| CM-3 | **PathBlend** | Real physical Z gradient *within a layer*: one filament ramps scanline-by-scanline, the other caps to layer height. No density dithering. | 2.3 | stable | D | L |
| CM-4 | **PathBlend start/end zone + "Techo" ceiling removal** | Draggable ADV editor: where the ramp starts/ends, floor/ceiling height; full layer height reachable (100% top color). Drops sub-micron closing lines. Blob schema v2->v3. | 2.3.5 | stable | D | L |
| CM-5 | **Bottom Surface painting** | Paint/sandwich the underside (bed, bridge, stacked-contact roles). Full pass stack as of 2.4.0 (was 1 pass). Restrictions: max 2 solid, 1 ColorStitch, PathBlend always Full. | 2.3.3 | stable | D | L |
| CM-6 | **MixedFilament Object mode** | Object toggle auto-builds a TD-aware (parallel-blend) sandwich matching an assigned MixedFilament; governs whole object incl. Bottom (2.4.0). | 2.3.3 | beta | D | L |
| CM-7 | **Sandwich Stickers** | Single-color SVG decals on top faces with their own independent sandwich recipe; stackable, top-wins. Move/rotate/scale. | 2.3.4 | WIP / rough | D | L |
| CM-8 | **ColorStitch pattern editor redesign** | Categorized Pattern style selector: Custom / MixedFilament recipe / Textile weaves (Plain, Twill, Satin, Houndstooth) / Smooth blend 2-3 / Stripes. Unified preview. | 2.3.6 | stable | D | M |
| CM-9 | **NeoTower** | Fork's wipe-tower variant; default tower type "Classic" with Zigurat taper. Auto-promoted for sandwich scenes. The wipe tower as a whole is described as the most fragile code. | 2.3 | stable | D | L |
| CM-10 | **RealColor View** | G-code viewer render of physically mixed colors (depth peeling + Beer-Lambert), later directional extrusion sheen, per-surface finishes, >4 filaments (16). | 2.3.3 | experimental | D | L |
| CM-11 | **ColorStitch on continuous Monotonic (always on)** | Makes ColorStitch work on Monotonic/Rectilinear/Hilbert etc., not just Monotonic Line. | 2.4.0 | stable | D | M |
| CM-12 | **Gradient fixes** | LaneQuant/DirCluster dropped the last color; gradient axis mirrored on some objects; now uses authored angle. | 2.4.0 | stable | D | M |
| CM-13 | **MMU paint and Sandwich share a surface** | Geometric split: MMU wins where painted, Sandwich elsewhere; per-facet overlap warnings in both gizmos. | 2.4.0 | stable | D | L |
| CM-14 | **Paint visible outside the painter** | Painted objects render their full weave in the normal Prepare view and MMU painter. | 2.4.0 | stable | D | M |
| CM-15 | **Pattern snapshots / mixed-filament renumbering fixes** | Patterns freeze to physical filaments on save; deleting a mixed filament no longer shifts numbering. | 2.4.4 | stable | D | M |

---

## 2. Multi-tool / MMU / toolchanger + wipe-tower correctness - `MT`

Mostly standalone. `MT-1` is broadly useful. The wipe-tower fixes are correctness work on a
fragile subsystem; some are Neotko's, some are Snapmaker-upstream (see `UP-*`).

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| MT-1 | **Hotends that finished switch off** | Standby temp set to 0 for tools with no extrusion left; decided on final G-code (tower visits counted); never causes reheat wait. | 2.4.5 | stable, print-verified | B | M |
| MT-2 | **Extra Energy Save** | Also cools tools during long idle gaps; preheat window removes shorts; needs Ooze prevention. | 2.4.5 | stable, print-verified | B | M |
| MT-3 | **Bed heats for hottest filament** | First-layer (and layer-2 transition) bed temp = max over used filaments. Changes M140/M190 on mixed plates. | 2.4.4 | stable | B | S |
| MT-4 | **MMU painter Pro Mode F1-F4** | Perimeters-only, extra walls in paint zone, brush precision slider, rectangle/polygon masks. Ungated from Libre Mode. | 2.3.6 | stable | B | M |
| MT-5 | **Wipe-tower drawer fix** | Deterministic tie-break (Z, object, layer) stops missing purge boxes / "empty first layer" abort. | 2.3.1 | stable | B | S-M |
| MT-6 | **Wipe-tower correctness batch** | Z-crash fix, bridge purge hardcoded-zero fix, "phantom tower" assembled-transform fix. | 2.3.3 | stable | B | M |
| MT-7 | **Purge-tower five bugs** | 2.45x-too-tall tower, skipped purge on PathBlend->ColorStitch, wrong-tool purge, bad adaptive layer heights, double purge into shared gap. | 2.4.1 | stable | B | M |
| MT-8 | **Tower gaps** | Paces tower by deposited layer height; prints missing structural passes. | 2.4.2 | stable | B | M |
| MT-9 | **Tower on non-existent layers** | Adaptive + fixed grid mixed: stops building tower on grid layers that aren't there; empty layers no longer overrule printing ones. | 2.4.5 | stable, print-verified | B | M |
| MT-10 | **Multi-tool ramming flow note** | Documentation only: lower ramming flow for adaptive + toolchanger; nothing clamps it. | 2.4.5 | doc | - | - |

---

## 3. Variable / adaptive layer height - `VL`

Standalone and generally high-value. Mostly Libre Mode gated.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| VL-1 | **Variable layer height (Experimental) unlock** | Lets scenes mix objects of different layer heights and adaptive + multi-filament; NeoTower purges at real height. Gated: NeoTower + Libre Mode. | 2.3.1 | experimental | B | M |
| VL-2 | **Precision Adaptive Layer Height** | Point-based curve editor (exact Z + height, per-segment tension, monotone). Replaces stock brush; stock tool untouched. 3D slice-band highlight. | 2.3.5 | stable (not print-validated) | B | L |
| VL-3 | **Precision ALH - Adapt to Color** | Shades height editor with color-safe ranges (pattern-resolution ceiling, TD color-fidelity floor); snap-to-optimal. | 2.3.8 | WIP | B | L |
| VL-4 | **Precision ALH - Slope Pattern Recolor** | Per-object recolor plan so exposed interior rings on slopes print a mix matching the recipe color. | 2.3.8 | WIP | B | L |
| VL-5 | **Height Adaptive Effects** | Gizmo: drive settings by a curve drawn over real layer bands - sparse infill width, outer/inner wall width, fuzzy skin thickness + character. Steps or ramp. | 2.4.3 | stable | B | L |
| VL-6 | **Variable layer height persistence/reload/resize fixes** | Preset serialization, survive reload when first segment mismatched, rescale on object resize instead of discarding. | 2.3.8 / 2.4.4 / 2.4.5 | stable | B | S-M |
| VL-7 | **Sandwich zones no longer vanish on VL** | Collapse too-thin passes and grow survivors; warn which slots; avoids G-code void. | 2.4.4 | stable | B | M |

---

## 4. Supports & object-aware slicing - `SU`

Mostly standalone and broadly useful for multi-object plates. `SU-1`/`SU-2` are strong general
features; `SU-5`/`SU-6` are Libre Mode expert tools.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| SU-1 | **PerObject Support** | An object's support avoids every other object's body *and* support; moving a part re-solves neighbors. First cross-object support avoidance in this slicer family. Trade-off: drops first-layer base expansion. | 2.3.9 | stable | B | L |
| SU-2 | **True Objects (Gravity)** | Measures real floor under every surface; slices by area (contact vs real bridge), correct overhang/elephant-foot, forces PerObject Support. Driven by Libre Mode button. | 2.3.9 | stable | B | L |
| SU-3 | **Real floating-object detection** | Replaces "empty first layer = floating" heuristic with measured per-instance contact. | 2.3.9 | stable | B | M |
| SU-4 | **NeoWave Support** | Wave-roof hollow support + neoweave contact-layer oscillation to ease removal. Type "NeoWave" locks base/interface to Hollow/Wave. | 2.3.6/2.3.7/2.3.9 | WIP, partly print-validated | B | L |
| SU-5 | **Support Zones - aimed pillars** | Two-click pillar from overhang patch to chosen landing; real-surface roof, lean angle capped by slicer. Soluble roof per zone, reopenable zones, zone merging. | 2.4.4 | printed | B | XL |
| SU-6 | **Support Zones - block trees** | Paint an area, a stump grows plumb below, gap walked toward stump; flat roof. | 2.4.5 | print-verified | B | XL |

---

## 5. Assembly / object placement / Libre Mode - `AS`

`AS-1` is a master gate for many `CM`/`VL`/`SU` features. The rest are usable gizmos.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| AS-1 | **Libre Mode** | Master switch (Preferences + toolbar) gating many features. Adds floating objects, assembled-boolean, per-volume XY comp, copy/paste process settings, full assembled-part options. | 2.3 | stable | B/D | L |
| AS-2 | **Align & Stack gizmo** | Align/stack objects against anchor #1; place-against chained or flush; Z gap; drop to bed. Reworked 2.3.9: capped to 2 objects, live ghost previews. | 2.3 / 2.3.9 | stable | B | M-L |
| AS-3 | **Snap & Drag** | Dragging drops an object onto the real surface below (footprint overlap, real mesh raycast, highest wins, no fake bed-drop). Own toolbar panel; "move selection as one block". | 2.3.9 / 2.4.0 / 2.4.3 | stable | B | L |
| AS-4 | **Remove Slice Cache** | Right-click action dumping all cached slice results for the plate. | 2.3.9 | stable | B | S |
| AS-5 | **S3DFactory .factory import** | Opens Simplify3D `.factory` as one Assembled object, world-space layout preserved. | 2.3 | stable | B | M |

---

## 6. Print quality & general settings - `PQ`

These are the most "normal slicer" features and the least coupled to the color pack.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| PQ-1 | **Bridging infill extra expansion** | Adds mm of bridge extension over the auto wall-count-derived anchoring; can claim whole layer for a seam-free single-direction bridge. 0 = byte-identical. | 2.4.0 | stable | B | S-M |
| PQ-2 | **Slow down inner walls next to overhangs** | Grades the inner wall as if it were the overhang it supports (reach %). Better undersides on shallow slopes. Libre Mode. | 2.4.5 | printed | B | M |
| PQ-3 | **NeoArachne hybrid wall generator** | Per-feature Classic / Arachne / NeotkoEdge routing; Edge Closure; pin outer wall width; wall-count hysteresis; configurable blend distance. Libre Mode. | 2.3.8 | stable, print-tested | B | XL |
| PQ-4 | **Spiral lift bed-bounds check** | Upstream OrcaSlicer `spiral_lift_fits_printable_area`: degrade to straight lift when the arc leaves the bed; plus reporting (fit / count / first Z). | 2.4.5 | stable | A (upstream) | S |
| PQ-5 | **Perimeter-override double-wall fix** | Restores per-pass perimeter suppression guard. | 2.3.1 | stable | B | S |
| PQ-6 | **Penultimate zone fix** | Penultimate ColorStitch/PathBlend now generate with normal "Ensure moderate" (not just "Ensure all"). Gated on Penultimate top layers > 0. | 2.3.2 | stable | B | M |
| PQ-7 | **Typographic Spacing / font kerning** | Real kern-pair support (incl. Apple `kern` v1.0 tables) + `Emboss::layout_text()` refactor; fixes font search box (also broken upstream). | 2.3.9 | stable | B | M |
| PQ-8 | **Small-island XY-compensation reporting** | Warns when negative XY comp merges/removes small islands, naming lowest Z. | 2.4.3 | stable | B | S |

---

## 7. G-code preview, viewer & post-processing - `GV`

Standalone and mostly additive. Good candidates if you want preview/analysis tools.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| GV-1 | **Expert G-code Reprocessor** | Layer-ranged post-processing before export: M220 speed, M106 fan, M221 flow, SET_GCODE_OFFSET Z; GLOBAL/BY TOOL; interactive chart; "Avoid Wipetower". Libre Mode. | 2.3.7 / 2.3.8 / 2.4.5 | beta / print-verified | B | L |
| GV-2 | **RealColor view** | See `CM-10`; viewer-side rendering. | 2.3.3+ | experimental | D | L |
| GV-3 | **Photo Mode (Prepare) + Photo Studio (G-code)** | Studio lighting, aimable key/fill/rim, per-filament materials, environment, floor reflection, shadows, Hide-UI. G-code viewer adds real mesh shadow + Save PNG / Copy (1080/1440/2160). | 2.4.2 / 2.4.4 | stable | B | XL |
| GV-4 | **Two-handle G-code preview slider** | Trim the start of a layer's move range, not just the end. | 2.4.2 | stable | B | S |
| GV-5 | **Filament finish + by-eye TD calibration** | Window with per-filament finish preset + TD slider, preview-live, Save applies and reslices. | 2.4.3 | stable | B | M |
| GV-6 | **Collapsible Color Mixing list** | Sidebar section collapse state remembered. | 2.4.2 | stable | B | S |

---

## 8. UI / theming / experience - `UI`

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| UI-1 | **Dark/light mode fixes** | Sandwich editor dark mode; painter combos; tooltip bg in light mode (Pro Mode/ADV/Bump); full macOS Light mode (sidebar, scene, titlebar, gizmo panels). | 2.3.1 / 2.3.7 / 2.4.4 | stable | B | M |
| UI-2 | **Realistic shading in Prepare** | Phong, fresnel, SSAO (slope-corrected), contact + real directional shadows. Libre Mode. | 2.3.9 / 2.4.0 | stable | B | L |
| UI-3 | **Smooth shading of imported meshes** | Area-weighted averaged normals with crease angle; fixes sliver-normal noise + facets. Preference, on by default, shading only. | 2.4.0 | stable | B | M |
| UI-4 | **Notification band** | Collapses Libre Mode validation cards into one amber/red band; clears superseded warnings. Preference toggle. | 2.4.3 | stable | B | M |
| UI-5 | **Detached Process panel width** | Remembers dock width continuously. | 2.4.3 | stable | B | S |
| UI-6 | **Home / Device dark theme** | Recolors Snapmaker's canvas app to follow app theme; experimental, fails quietly. | 2.4.3 | experimental | C (Snapmaker app) | M |
| UI-7 | **Separate settings folder + installer coexistence** | Own config dir + one-time migration; uninstaller/links/file-associations stop clobbering official Snapmaker Orca. | 2.4.2 | stable | C (branding/ecosystem) | M |

---

## 9. Expert / experimental, deliberately gated - `EX`

Keep these gated if ported at all.

| ID | Feature | What it does | Since | Status | Cat | Eff |
|----|---------|--------------|-------|--------|-----|-----|
| EX-1 | **Bump Mapping Editor** | Grayscale image -> physical relief (wall displacement All/Painter via planar/cylindrical/spherical/cubic; Top "ZBump" surface height). Double-gated: Libre Mode + `ORCA_DEBUG_TEXTUREBUMP` / `ZBUMP` / `ALL`. Arachne-only; known over-extrusion risks. Based on prior art (Poikilos / undingen). | 2.3.5 | gated, partly print-validated | D | XL |
| EX-2 | **NeoStitch Interlock** | Z-axis layer interlocking via alternating notch/fill wall segments, phase-flipped per layer. Per-region. | 2.3.7 | untested (preview only) | D | L |
| EX-3 | **Texture Bump Mapping** | Bump/relief texture engine + gizmo. | 2.3.4 | disabled (over-extrusion) | D | - |

---

## 10. Correctness fixes worth calling out on their own - `FX`

Not "features", but many are real bugs that also exist upstream / in the base, so they may be
independently portable or already fixed in 2.5. Verify each.

| ID | Fix | Since |
|----|-----|-------|
| FX-1 | Split (To Objects/Parts) could silently lose Sandwich recipes/Stickers - now warns. | 2.3.7 |
| FX-2 | Assemble slot-number collisions printing wrong recipe; sticker re-centring drift. | 2.3.7 |
| FX-3 | Painted colours losing their recipe on erase+undo (silently plain 3MF). Save/load audit + recovery. | 2.4.1 |
| FX-4 | Single-filament object with a painted pass in another filament got no tower. | 2.4.1 |
| FX-5 | G-code file open regression (toolpaths vanish / crash); no longer invalidates slice; extra extruders tolerated. | 2.4.2 |
| FX-6 | PathBlend on assembled objects printed cap before ramp; oversized prime tower. | 2.4.4 |
| FX-7 | PathBlend angle now points the gradient correctly. | 2.4.4 |
| FX-8 | Assorted crashes/dead ends (moved G-code, double drop, plate slot collision, tool ordering, painted-model grid, divide-by-zero). | 2.4.4 |
| FX-9 | 18 Snapmaker 2.3.6/2.3.7 fixes (tree support hang, crashes, non-ASCII profile paths, filament sync, etc.). | 2.4.5 |

---

## 11. Upstream Snapmaker patches Neotko integrated - `UP`

These are Snapmaker's work, not Neotko's. Most should already be in upstream OrcaSlicer 2.5
or be irrelevant to it. Listed so we do not mistake them for Neotko additions.

| ID | Patch set | Since |
|----|-----------|-------|
| UP-1 | Snapmaker 2.3.5 beta: Filament Sync v2, Filament Color Library, top-cover detection, splash, MQTT, misc | 2.3 |
| UP-2 | Wipe-tower filament-waste fix (upstream #501: stop force-flagging non-solubles) | 2.3 |
| UP-3 | Tooltip/format hardening, firmware/version, filament color gradient - 8 patches | 2.3.5 |
| UP-4 | Ramming clamp + gap-wall travel revert | 2.3.8 |
| UP-5 | 18 fixes from 2.3.6/2.3.7 | 2.4.5 |
| UP-6 | Spiral lift printable-area check (`spiral_lift_fits_printable_area`) | 2.4.5 |

---

## Quick view - most likely worth porting (my opinion, not verified)

Practical, standalone, broadly useful, low coupling to the color pack:

- `PQ-1` Bridging infill extra expansion
- `PQ-2` Slow down inner walls next to overhangs
- `PQ-7` Typographic kerning + font search fix
- `SU-1` PerObject Support, `SU-3` real floating detection (SU-2 True Objects is the bigger gate)
- `MT-1` hotend switch-off, `MT-2` extra energy save, `MT-3` hottest-filament bed temp
- `MT-4` MMU painter Pro Mode
- `VL-1` variable-layer-height unlock; `VL-2` Precision ALH (larger)
- `GV-4` two-handle preview slider, `GV-6` collapsible list
- `UI-3` smooth shading; `UI-1` theming fixes
- The `MT-5..MT-9` / `FX-*` wipe-tower and correctness batch (needs diff verification vs 2.5)
- `PQ-4` / `UP-6` spiral-lift check (upstream; confirm 2.5 already has it)

Heavy, coupled, or experimental - port only if you specifically want them:

- Entire `CM-*` group (ColorStitch / Sandwich / PathBlend / NeoTower / RealColor / Painter)
- `SU-5`/`SU-6` Support Zones, `SU-4` NeoWave
- `PQ-3` NeoArachne
- `VL-3`/`VL-4`/`VL-5` color-aware / curve-driven ALH
- `GV-1` G-code reprocessor, `GV-3` Photo Studio
- `EX-*` Bump Mapping / NeoStitch / Texture Bump

Excluded by policy unless explicitly requested:

- `UI-6` Snapmaker Home/Device theming, `UI-7` installer/ecosystem coexistence,
  `UP-*` Snapmaker-branded syncs, all Snapmaker/Neotko profiles, sponsor/branding text.
