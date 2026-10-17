# MCP-3 — a Mesh boolean gizmo MCP tool

`mesh_boolean_gizmo`, the tool that can drive the fork's one un-driven gizmo. It exists because
PP-5's two bugs (`docs/handoff.md` section 37) had no runtime reproducer: the Mesh boolean dialog
picks volumes by clicking in the 3D view and commits with a button, and no MCP tool could do
either. With this tool the whole two-boolean sequence runs from a script, which is how the
perforation-cutter root cause was found.

## Where it lives

- `src/slic3r/GUI/OrcaMCP/OrcaMCPGizmoTools.cpp` — the tool (registered in `register_gizmo_tools()`).
- `src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.{hpp,cpp}` — the gizmo-side surface it drives.

## Actions

| action | what it does |
|---|---|
| `open` / `close` | activate / deactivate. `select_object=false` opens with the current selection, to test the `on_is_activable()` gate. |
| `status` | the panel state plus the selection/volume facts. |
| `set_operation` | `union` \| `difference` \| `intersection`. |
| `set_delete_input` | the "Delete input" checkbox (per-mode members, set together). |
| `pick_source` / `pick_tool` | `volume_index` (no raycast) **or** `screen_x`/`screen_y` (the same ray an interactive click casts). |
| `probe_pick` | read-only: which volume a ray reaches at a screen point. |
| `run` | set the mode, pick both volumes by index and apply, in **one main-thread callback**. |
| `apply` | the boolean on the current picks. |

`run` is the one to use from a script: `open`/`pick`/`apply` as separate calls can interleave on
the main thread (the HTTP worker and `wxGetApp().CallAfter()`), and `run` cannot.

## What the state report carries (the diagnostics)

- `operation`, `delete_input`, `src`/`src_index`/`src_picked`, `tool`/`tool_index`/`tool_picked`,
  `button_enabled` (exactly what the Difference/Union/Intersection button's enable test reads).
- `selection_full_instance`, `selection_volume_count`, `can_mesh_boolean` — the facts
  `on_is_activable()` and the ObjectList's own gate read. Reported on the "cannot open" error path
  too, so a refused open says why.
- `raycasters_count` next to `volume_count` — the check `docs/handoff.md` section 37 asked for: they
  have to match, or a pick reads the wrong mesh or past the end.
- per volume: `index`, `name`, `type`, `triangles`, `vertices`, `patches`, `open_edges`, `center`,
  `screen`. `open_edges != 0` means the mesh is not a closed solid, which is what mcut cannot eat.

## Gizmo-side surface

`perform_operation()` is the boolean, extracted out of the ImGui button handlers so the tool runs
the same code the buttons do. `raycast_volume()` is the pick raycast, one bounded lookup shared by
`gizmo_event()` (the interactive click) and the tool; it loops over
`min(mo->volumes.size(), raycasters.size())`, so a pick after a volume-list change can never read
past either array. `pick_volume_index()` sets a volume directly, which is the only way to reach a
cutter buried inside the part, and it separates "the pick is wrong" from "the boolean is wrong".

## Getting it into a client

The opencode/OrcaMCP bridge answers `tools/list` from `scripts/tools_schema.py`, not from the live
app, so after building run `scripts/regen_tools_schema.py` and restart the client. Driving the
endpoint directly (`POST http://localhost:13618/mcp`, JSON-RPC `tools/call`) works without that and
is how it was verified here.

## Verified

Over MCP on the Release build: open with the natural selection, per-volume diagnostics, `run` on a
box + perforation cutter (first and second boolean), `probe_pick` scanning for a cutter pixel, and
the two-bug reproductions. No unit test — it is a GUI-driving tool; the geometry it exposed is
covered by PP-5's tests.
