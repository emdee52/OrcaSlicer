# EF-1 - Edge chamfer / fillet gizmo

Chamfers or fillets a straight mesh edge with a negative prism, or every edge of a hole's or boss's rim
with "Whole loop / rim". Row added late, and this note was written late too: the feature was recorded
in the manifest and in docs/handoff.md section 20 without a `notes/EF-1.md`.

## Behaviour

- Picking is by cursor: left-click applies, right-click removes. The panel therefore has **no apply
  button** - the "Apply to hovered edge" button was removed in patch 41 precisely because it duplicated
  the viewport gesture. `gizmo_apply_hovered` stays for the mouse path and for the MCP tool
  `edge_dress_gizmo`.
- Whole loop / rim dresses the boundary loop of the coplanar patch containing the hovered facet as one
  closed feature instead of its tessellation segments.
- The geometry is a negative volume, so no config keys and no CAD.
- Shortcut **X**; off by default (nothing changes until a feature is applied).

## Panel style (2026-10-17, patch 54_EF-1-panel.patch)

Migrated to the Extrude panel standard (docs/handoff.md section 28):

- `gizmo_push_window_style()`/`pop` around `GizmoImguiBegin`/`End` - the panel previously had **no**
  window style at all, so it rendered on Orca's own background and did not read as one of ours;
- `SetNextWindowSize(gizmo_panel_width(), 0)` instead of an auto-sized width;
- Chamfer | Fillet as a `gizmo_segment` row, Whole loop / rim as `gizmo_row_switch` (it was a checkbox
  whose caption sat on the left, which is exactly the case the standard's drawn switch is for);
- Size as `gizmo_row_slider` - slider plus typed field clamped to the same range, no `[-][+]` buttons
  (they cost about a third of the row and the field still takes a typed value);
- the four status lines became `gizmo_hint()`, which wraps inside the panel width;
- the applied count line is unchanged (`Applied: 0`).

The `m_desc` keys and every msgid are untouched.

## Verification

Build clean; captured over MCP with `ui_screenshot` (UI-2) - the panel opens under its own icon with
the fork surface, the segmented mode, the switch and the wrapped hints. Runtime behaviour is unchanged:
this patch is GUI only.
