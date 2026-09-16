# MCP-1 - Embedded MCP server (port of okets/OrcaMCP)

Status: in progress (core ported; build pending verification; filament tools deferred).

## Goal

Give an AI agent backend access to *this* fork of OrcaSlicer - load/arrange/configure,
slice, read warnings and errors, and render previews - over the Model Context Protocol,
instead of relying on screenshots. Runs inside the app so it exercises the fork's actual
OraExt pipelines.

## Source and traceability

- Source repo: `https://github.com/okets/OrcaMCP` (branch `mcp`), sha **`6c672ad2e9`**.
- Base line: OrcaSlicer `2.5.0.1-dev`; the fork contains `Merge upstream OrcaSlicer 2.5.0-dev`,
  i.e. the same dev line as this tree.
- Isolated delta used for the port: `git -C OrcaMCP diff 2431f7466c^2 HEAD` (second parent of
  the last upstream merge = upstream 2.5.0-dev tip).
- License: AGPL-3.0, same as OrcaSlicer.
- De-branded: app stays "OrcaSlicer"; no OrcaMCP rename, purple theme, homepage card, or
  vendor images. MCP server identity string is `orca-slicer`.

## Architecture (ported as-is)

```
MCP client (opencode)  --stdio-->  scripts/orcamcp-bridge.py  --HTTP/JSON-RPC-->  app :13618/mcp
```

- The GUI app runs a `boost/asio` HTTP server (already upstream, used for OAuth). MCP adds a
  `/mcp` JSON-RPC 2.0 endpoint (`initialize`, `ping`, `tools/list`, `tools/call`) on the same
  server.
- The Python bridge translates stdio MCP to HTTP and can auto-launch the app (`start_orca`).
- Tools execute on the wx main thread (OrcaMCP's model); responses carry `active_warnings`.

## Ported surface

New (compiled):
- `src/slic3r/GUI/OrcaMCP/OrcaMCPServer.{hpp,cpp}` - server + ~50 builtin tools.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPCommon.{hpp,cpp}` - dialog-suppression guard + warnings JSON.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPConfigKeys.hpp` - key categories.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPPlateUtils.{hpp,cpp}` - scene/plate/preview render.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPPresetConfigUtils.{hpp,cpp}` - presets/config apply.

New (scripts, not compiled):
- `scripts/orcamcp-bridge.py`, `scripts/tools_schema.py`, `scripts/regen_tools_schema.py`
  (bridge de-branded for this fork: finds `orca-slicer.exe`, sets `ORCA_EXT_MCP=1` on launch).

Modified upstream files (all `[ORCAPORT:MCP-1]` marked):
- `src/slic3r/GUI/HttpServer.{hpp,cpp}` - `(method,url,body)` handler, `ResponseJson`, request
  body capture, `process_request()`, case-insensitive headers. Legacy URL-only handler kept so
  the OAuth flow is untouched.
- `src/slic3r/GUI/GUI_App.cpp` - include + `/mcp` route in both `start_http_server` overloads;
  opt-in start in `post_init`.
- `src/slic3r/GUI/GUI.{hpp,cpp}` - MCP dialog-suppression state + `show_info` capture.
- `src/slic3r/GUI/MsgDialog.{hpp,cpp}` - `ShowModal()` override returns a default under MCP.
- `src/slic3r/GUI/NotificationManager.{hpp,cpp}` - `get_active_warnings()`.
- `src/slic3r/GUI/Plater.{hpp,cpp}` - `export_gcode_to_file()`, `EVT_SCHEDULE_BACKGROUND_PROCESS`
  declaration (definition already upstream).
- `src/slic3r/CMakeLists.txt` - sources + `GUI/OrcaMCP` include dir.
- `opencode.json` - `mcp.orca-slicer` local server entry.

Not ported (deliberately): MCPClientConfig (7-client auto-config UI), Preferences "MCP Clients"
tab, homepage "Connect AI" card, version/branding changes, AppConfig update-URL changes.

## Opt-in gating

Server starts only when **either** `ORCA_EXT_MCP` is set non-empty/non-"0" **or** app key
`orca_ext_mcp` is true. A normal launch is unchanged (`start_http_server` still only runs for
OAuth as before). The bridge sets `ORCA_EXT_MCP=1` when it launches the app.

## Tool surface (categories)

Scene/project: `get_server_info`, `get_scene_info`, `new_project`, `load_project`,
`save_project`, `export_3mf`. Presets/config: `get_presets`, `select_preset`, `apply_config`,
`get_edited_presets`, `get_valid_config_keys`, `get_all_presets`. Models: `load_model`,
`auto_orient`, `arrange_objects`. Plate: `add_plate`, `select_plate`, `delete_plate`.
Objects: `get_object_info`, `rename_object`, `get_object_config`, `set_object_config`,
`reset_object_config`, layer ranges, adaptive layer height. Transforms: `move_object`,
`rotate_object`, `scale_object`, `mirror_object`, `transform_objects`, `clone_object`,
`flatten_object`, `cut_object`, `delete_object`. Slicing: `slice_all`, `get_slicing_status`,
`get_print_estimate`, `export_gcode`. Preview: `render_plate_view` (base64 or file).
Printers: `get_printers`, `select_printer`, `send_to_printer`. History: `undo`, `redo`.

Deferred: the ~10 filament/mixed-filament/color tools (`get_filaments`,
`set_mixed_filament`, `set_object_filament`, `suggest_color_mix`, `get_color_palette`,
flush-volume/toolchanger config tools). They depend on `Sidebar::apply_mixed_filament`, which
requires porting OrcaMCP's dialog-free extraction in `Plater.cpp`. Files are present but
excluded from CMake for the first build.

## Checklist: OraExt features vs the generic tools

"Auto" = the generic tool will surface the feature because it reads the config/model/print
registry, which the OraExt features already populate. "Custom" = the tool has no knowledge of
the feature and would need a dedicated tool.

| OraExt feature | Surface | Auto? |
|---|---|---|
| PQ-1 `bridge_expansion_extra` | config | Auto (apply_config / get_object_config / get_valid_config_keys) |
| PQ-2 `inner_wall_overhang_*` | config | Auto |
| PQ-3 `wall_generator=hybrid`, `hybrid_*` | config | Auto |
| SU-1 `support_cross_object_avoidance` | config | Auto |
| SU-4 `support_type=stWaveSupport`, `support_interface_pattern=smipWave`, `wavesupport_*` | config | Auto |
| SU-4b `support_neoweave_*` | config | Auto |
| SU-5 `support_zone_*` (5 keys) | config | Auto (keys) / Custom (zone geometry) |
| MT-4 `mmu_segmented_region_extra_walls` | config | Auto |
| PF-1 `seam_type`, `seam_notch_*` | config | Auto |
| PF-2 `counterbore_bridge_layers` | config | Auto |
| PF-9 `interlock_*` | config | Auto |
| `orca_ext_idle_tool_power_down`, `orca_ext_idle_tool_deep_sleep`, `orca_ext_free_z`, `orca_ext_snap_drag*` | app prefs | **Custom** (not in PrintConfig; no generic tool) |
| Support Zones gesture JSON, counterbore facets, support-paint facets | per-volume annotations | **Custom** (`get_scene_info` only knows standard features; unverified) |
| Slicing output/errors of any OraExt feature | print pipeline | Auto (`slice_all`, `get_slicing_status`, `active_warnings`, `export_gcode`, `render_plate_view`) |

Conclusion: config-driven features are already introspectable through generic tools; only the
app-level prefs and per-volume custom annotations need dedicated tools (candidate `MCP-2`).

## Verification

Planned: build with `.\OrcaSlicer\build_win.bat -s -j 8`; launch with `ORCA_EXT_MCP=1`; then
`GET http://localhost:13618/mcp`, `POST` `tools/list`, and a load -> slice -> status ->
export -> render loop. Evidence to be recorded here once run.

## Known gaps / follow-ups

- Filament/color tools deferred (see above).
- `get_scene_info` `with_model_object_features` will not report OraExt per-volume annotations.
- Preferences "MCP Clients" auto-config UI not ported; opencode is configured directly.
