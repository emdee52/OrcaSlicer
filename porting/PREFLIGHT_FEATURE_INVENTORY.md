# preFlight 3D - Feature Inventory for Porting

Source: `preFlight/` (`https://github.com/oozebot/preFlight`), version **1.3.0**.
preFlight is a **PrusaSlicer** fork (AGPLv3) with its own "Athena" perimeter generator.
Target: our `OrcaSlicer/` (2.5.0-dev, Bambu-derived).

**Read this first - scope and honesty:**

- This list is transcribed from preFlight's own website + `CHANGELOG.md` and a code survey,
  not verified against a `diff` of any specific upstream. Treat the release/changelog text as
  an index; confirm every item in the source before porting.
- **preFlight is not OrcaSlicer.** The two share Slic3r DNA but the perimeters, seams, tree
  support, fill and GCode pipelines diverged. Everything here is therefore category **D
  (conflict / needs rework)** unless noted - port the intent, re-implement on 2.5 APIs.
- **"Athena" is preFlight's Arachne.** `Athena/` is a copy of Arachne's architecture
  (`BeadingStrategy/{Distributed,Limited,Redistribute,Widening}`, `utils/ExtrusionLine`,
  `WallToolPaths`); `"classic"` maps to `PerimeterGeneratorType::Athena`
  (`PrintConfig.cpp:325-329`). OrcaSlicer already has Arachne (+ our NeoArachne). **We never
  need to port or recreate Athena** - features built on it are re-implemented on Orca's
  Classic/Arachne/NeoArachne.
- Effort (S/M/L/XL) and value are my **estimates**, not measured.
- Category letters follow `AGENTS.md`: (A) already upstream, (B) portable, (C) branding /
  ecosystem - excluded, (D) conflict / needs rework.

Legend: **Status** = stable / experimental / WIP per the changelog.
**Cat** = suggested category. **Eff** = rough porting effort.

---

## Selected for porting (user decision)

| ID | Feature | What it does | Source (preFlight) | Cat | Eff |
|----|---------|--------------|--------------------|-----|-----|
| PF-1 | **Nip & Tuck seams** | V-shaped notch at the external-perimeter seam (start and/or end) with a taper, plus trimming the first inner perimeter; modes Regular / Nip / Tuck / NipTuck / Alt. | `GCode.cpp:4037-4830` (`apply_notch_to_external`, `apply_seam_notch_pair`, `apply_seam_notch`), `GCode/SeamPlacer.cpp:278`, keys `seam_type`, `seam_notch_width`, `seam_notch_angle` | D | M |
| PF-2 | **Counterbore Bridge gizmo** | Paint per-hole regions; each painted facet stores a bridge-layer count (2-9); slicing adds bridge layers above the painted layer and aligns bridge fill to each hole's corridor angle. | `GLGizmoCounterboreBridge.*`, `ModelVolume::counterbore_bridge_facets`, `apply_counterbore_bridge_geometry`, `FillBase::counterbore_fill_angle`, `Fill.cpp`, key `counterbore_bridge_layers` | D | M |
| PF-10 | **Baobab supports** | New support style between Organic and Snug: interface-shaped canopy (solid taper of concentric rings) on thick trunks, lightning-infill bridging deck, plant-on-object. | `Support/BaobabSupport.*`, canopy builder in `Support/OrganicSupport.cpp` (~128 refs), `PrintObject.cpp` multi-pass dispatch, 8 `support_baobab_*` keys | D | XL |

### PF-1 design decisions (confirmed)

- Keys in `PrintObjectConfig` (next to `seam_position`), so the setting is **per-object
  overridable** for free.
- New `seam_notch_target` selector: `All external` / **`Holes only` (default)** / `Outer only`.
  preFlight notches **every** external perimeter (outer contours and holes alike, no
  distinction) unless the seam sits on a sharp corner; we add the hole/outer targeting the
  user asked for. Holes are identified in Orca by
  `ExtrusionLoop::loop_role() & ExtrusionLoopRole::elrHole`.
- Nip/Tuck reshapes the wall at the seam; it does **not** move the seam (`seam_position`
  still decides placement).

---

## Additional candidates (not yet selected)

| ID | Feature | What it does | Source (preFlight) | Cat | Eff |
|----|---------|--------------|--------------------|-----|-----|
| PF-3 | **Auto Speed** | One checkbox derives every feature's print speed from each filament's max volumetric flow, capped by a max-print-speed ceiling. | changelog v1.1; `filament_max_volumetric_flow`, `filament_max_print_speed` | D | M |
| PF-4 | **Width Control** | Max-bead-width ceiling + widened beads slow to hold volumetric flow; real-time warning when a wall/fill bead exceeds a width threshold. | changelog v1.3; `max_width*`, width-warning keys | D | S-M |
| PF-5 | **Max Commands Per Second** | Diagnostic: G-code segments/second per feature type, to catch firmware-stalling setting combos. | changelog / preview | D | S-M |
| PF-6 | **Preview Clipping Plane** | Right-click an object in Preview to clip it and inspect internal toolpaths. | changelog; `GCodeViewer`/preview GUI | D | S-M |
| PF-7 | **Manual fan controls** | Per-feature manual fan speeds when auto cooling is off (`manual_fan_speed_*`, `fan_spinup_*`). | `PrintConfig.cpp:839-861`, `1597` | D | S-M |
| PF-8 | **Serpentine** | New region mode: one continuous serpentine bead replacing perimeters + infill; depth-limit band, relaxed ribs, outer loop, ridge stacking. Self-contained module (`Serpentine.{hpp,cpp}`, ~164 KB, deps: ExPolygon/ExtrusionEntity/Flow/Polyline only - generator-agnostic). | `Serpentine.*`, `PerimeterGenerator.cpp:2537+`, keys `serpentine_*` | D | L |
| PF-9 | **Interlocking perimeters** | Extra shells at the wall/infill boundary whose bead widths/spacing alternate per layer (100 / ~146 / 200% flow) for compression-bonded Z strength; feature-aware flow reduction. | `PerimeterGenerator.cpp` (Athena), `LayerRegion.cpp`, `GCode.cpp`, `Fill.cpp`, keys `interlock_*` | D | L |

---

## Likely redundant / already in Orca (skip)

| Feature | Orca equivalent |
|---------|-----------------|
| Beam/multi-material interlocking (`Feature/Interlocking/InterlockingGenerator`) | Orca **already has** the Cura-derived beam interlock (`interlocking_beam*`) - **different** from PF-9. |
| Stability / "support needed" analysis | Orca `Support/SupportSpotsGenerator.*`. |
| 2-opt / travel ordering | Orca `GCode/OrderingStrategies.cpp` (2-opt, serpentine infill ordering). |
| Clipping (gizmo) | Orca has a gizmo clipping plane; PF-6 is the *Preview right-click* UX. |
| Per-filament pressure advance | Orca already supports it. |
| Align to Face gizmo | Orca assemble/align + the Align&Stack we ported (`AS-2`). |
| OrcaSlicer profile import | N/A (we are Orca). |

## Large / ecosystem (out of scope unless asked)

- **CMYK+W color mixing** (Beer-Lambert palette painting) - XL, comparable to the Neotko `CM-*` pack.
- **G-code Preprocessing** (embedded Python, 150 APIs) and **Export to Script**.
- **64-bit / dependency modernisation / GPU / platform** items.
- **Narrow-to-Athena**, **Athena width model** - Athena-specific, subsumed by Arachne/NeoArachne.

## License

preFlight is AGPLv3; OrcaSlicer is AGPL-3.0. Porting with attribution is compatible; record
the source release and file in each feature's notes.
