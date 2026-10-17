# SU-5 / SU-6 - Support Zones: widget consolidation (2026-10-17)

The change docs/handoff.md section 28 had been deferring since the panel-style work: this panel carried
its **own copies** of widgets the shared layer provides, and two implementations of one widget had
already drifted apart.

## What was wrong with the copies

| Private copy | How it had drifted |
|---|---|
| `gizmo_row_slider` | a **different signature** - no label column, no typed field. The rest of the fork's rows have both, so the two could not be swapped by a rename |
| `gizmo_row_toggle` | a `Checkbox` with the caption on its left, where the standard draws a switch; different grid, different hit area |
| `gizmo_glyph_toggle` | 5 px rounding and its own hover colour, against the standard's `size * 0.15` |
| `gizmo_label_col()` | measured with its own `0.7 * font` padding instead of the shared `gizmo_label_w()` |
| `gizmo_u()` | a one-line font unit that existed twice |

## What was done

- The four above are deleted; the rows are `gizmo_row_slider` / `gizmo_row_switch` and the icon chrome
  is `gizmo_icon_toggle_sized`. Support Zones' nine slider rows go through a thin `zone_row_slider`,
  which supplies this panel's **one** label column - repeating the column at nine call sites is exactly
  where a different one would slip in unnoticed.
- `zone_label_w()` keeps this panel's caption list and its **46 % cap**: a very long translation must
  not leave the slider without room, and truncating the caption beats a twelve-pixel control.
- `gizmo_u()` moved to `GizmoStyle.hpp` with `kGizmoRefFont`, so the font unit and the reference size
  every panel is designed against live together.
- `gizmo_icon_toggle` became a **template** so a caller can pass a capturing lambda; Support Zones'
  `draw_glyph` takes a `Glyph` enum, which a plain function pointer cannot carry.
- The panel lost its **title bar** (`NoTitleBar | NoMove`), so it is no longer the only fork panel that
  can be dragged off its icon. It keeps its own width (22 font heights) - it has the elevation drawing
  and the zone cards, and forcing it to the standard 360 px would squeeze those.
- What stays local is what is genuinely this panel's: `draw_glyph`, `fit_text`, `gizmo_section`,
  `gizmo_stat_tile`, `gizmo_status_chip`, `gizmo_warn_row`, the zone cards, and `gizmo_glyph_button`
  (a button with a danger tint and an `always_lit` mode, not a toggle - nothing else wants one yet).

## Verification

Build clean. The empty state captured with `ui_screenshot` (UI-2): sliders with typed fields, the
"only my zones" switch, the shared label column, the shared icon chrome, no title bar.

**Not verified:** the panel's other states - a painted zone, the footprint shape row, the coverage-mode
row, an editing card - are where the switched rows are actually exercised, and they need a zone drawn
in the app. GUI only: no config keys, no geometry, no msgid changed.
