# Panel style migration (2026-10-17, branch `port/gizmo-style`)

The follow-up recorded in docs/handoff.md section 28: every gizmo this fork adds should look like ours
and not like stock Orca. The style rules, the widget list and the traps live in **section 28**; this
note records what each port's manifest row points at and what was found on the way.

## Scope

Migrated: **PP-1** Perforations, **EF-1** Edge chamfer / fillet, **PF-2b** + **RF-1** Hole walls (both
modes), **AS-2** Align & Stack, **ME-2** Holes. Patches 53-57, one per feature (55 covers both Hole
walls rows - both modes are the same function, so splitting the patch was artificial).

**Out of scope, deliberately:** Support Zones. It is already on the palette; it carries private copies
of the shared widgets and switches to them in its own change (the next task).

## Bugs fixed on the way, and why they were invisible

| Bug | Where | Why it survived |
|---|---|---|
| The label column was measured from the window's left edge while `SameLine()` takes a window-relative offset | the shared row helpers **and** `render_combo` | it only bites the *widest* caption of a panel ("Hole width" -> "Hole wid"), so it looks like a font problem, not a layout one. `render_combo` is stock code, so Cut and Assembly had it too |
| The slider printed its value twice - inline and in the typed field | `gizmo_row_slider` (the HF-3 reference) | it reads as a deliberate readout until you compare it with the second field |
| The right-aligned primary button was printed across the second line of its own wrapped hint | Extrude (HF-3) and Align & Stack | `SameLine()` attaches to the previous line, and it only overlaps once the hint wraps |
| `gizmo_hint()` pushed `TextDim` over any colour the caller pushed | the HF-3 reference | the amber warning was dim, i.e. it looked like a style choice |
| Align & Stack set its width with `SetWindowSize` *after* `Begin` | AS-2 | the next frame overwrites it, so the panel was as wide as the widest row of that frame - it never looked broken, just inconsistent |
| A checkbox row where the standard wants a switch; a button repeating its own caption ("Flip [Flip]") | ME-2 | both are legible, just not the standard |

## The two glyph traps (both cost a round)

1. **Clamp gaps and strokes to a minimum pixel width.** A gap expressed as a fraction of the glyph
   (0.019*s for the honeycomb wall) is 0.4 px on a 28 px button, and `ImDrawList` does not antialias two
   adjacent filled polygons against each other - the gap vanishes and the glyph silently changes shape
   between sizes. `draw_glyph` in Support Zones protects its strokes the same way.
2. **Judging a glyph from a mockup or a downscaled screenshot does not work.** Mockups are SVG (they
   antialias a gap ImGui will not draw) and a downscaled capture of a near-white glyph on a light plate
   is unreadable. Measure it: `ui_screenshot` then the pixel runs give the answer. The honeycomb was
   declared fixed twice from a picture before being measured; the actual defects were a wall of 0.4 px,
   a clip whose cut did not land where it should (one cell half drawn) and a mirrored triangle with the
   opposite winding (ImGui's AA fill assumes clockwise), which made the chamfer's two shoulders
   different widths.

The chamfer glyph was then re-designed as a **hole in section**: a square plate with a blind hole whose
mouth is a 45 deg flare. An outer corner (CAD's edge chamfer) or a flat band around the rim both read as
"edge chamfer", which is the wrong operation - this one dresses a hole's rim. It is drawn as three
convex pieces because ImGui 1.83 has no `AddConcavePolyFilled` and a concave outline comes out as its
own inverse.

## Verification

Build clean (Release). Every panel captured with `ui_screenshot` (UI-2) and reviewed; the two icon rows
were measured pixel by pixel. The user then confirmed it in the app (2026-10-17). Two panels could not
be captured: Hole walls' strengthening mode (nothing in the MCP surface can click into it) and the Move
gizmo (not part of this migration).

GUI only: no config keys, no `.3mf` or profile impact, no msgid changed.
