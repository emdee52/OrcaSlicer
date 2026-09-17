# MCP-1 - Embedded MCP server (port of okets/OrcaMCP)

Status: ported (core + filament/color tools + Preferences toggle). Build clean; runtime verified.

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
- `src/slic3r/GUI/OrcaMCP/OrcaMCPServer.{hpp,cpp}` - server + builtin tools.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPCommon.{hpp,cpp}` - dialog-suppression guard + warnings JSON.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPConfigKeys.hpp` - key categories.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPPlateUtils.{hpp,cpp}` - scene/plate/preview render.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPPresetConfigUtils.{hpp,cpp}` - presets/config apply.
- `src/slic3r/GUI/OrcaMCP/OrcaMCPFilamentTools.{hpp,cpp}` and `OrcaMCPFilamentUtils.{hpp,cpp}` -
  filament/mixed-filament, colour-palette, flush-volume and toolchanger tools.

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
- `src/slic3r/GUI/Plater.{hpp,cpp}` - `export_gcode_to_file()`, the dialog-free
  `Sidebar::apply_mixed_filament()` core (shared by the sidebar UI and MCP), and the
  `EVT_SCHEDULE_BACKGROUND_PROCESS` declaration (definition already upstream).
- `src/slic3r/GUI/Preferences.cpp` - "Enable MCP server" toggle (`orca_ext_mcp`); starts/stops
  the HTTP server live.
- `src/libslic3r/ColorDecomposeRecipe.{hpp,cpp}` - `color_decompose_delta_e()` used by the
  colour tools.
- `src/slic3r/CMakeLists.txt` - sources + `GUI/OrcaMCP` include dir.
- `opencode.json` - `mcp.orca-slicer` local server entry.

Not ported (deliberately): MCPClientConfig (7-client auto-config UI), Preferences "MCP Clients"
tab, homepage "Connect AI" card, version/branding changes, AppConfig update-URL changes.

## Opt-in gating

Server starts only when **either** `ORCA_EXT_MCP` is set non-empty/non-"0" **or** app key
`orca_ext_mcp` is true. A normal launch is unchanged (`start_http_server` still only runs for
OAuth as before). The bridge sets `ORCA_EXT_MCP=1` when it launches the app; the Preferences
checkbox toggles the app key and starts/stops the server immediately.

## Tool surface (63 tools)

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

Filament/colour group: `get_filaments`, `set_mixed_filament`, `delete_mixed_filament`,
`set_object_filament`, `suggest_color_mix`, `get_color_palette`, `get_flush_volumes`,
`set_flush_volumes`, `auto_calc_flush_volumes`, `get_toolchanger_config`. These use the ported
`Sidebar::apply_mixed_filament()` dialog-free core and the new `color_decompose_delta_e()`.

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

Done (2026-09-16, Windows; `build_win.bat -s -j 8`, 0 errors):

- `GET http://localhost:13618/mcp` -> server info JSON.
- `tools/list` -> **63 tools**.
- Core: `load_model` (`tests/data/20mm_cube.obj`) -> `slice_all` (surfaced a `ValidateWarning`)
  -> `get_slicing_status` -> `export_gcode` (612 KB written).
- Filament: `set_mixed_filament` (components `[1,2]` @ 50/50) created slot 5 with blended
  colour `#021CBB`.
- Colour: `get_color_palette` returned 6 mixes; `suggest_color_mix` for `#8A2BE2` returned a
  recipe (`components [1,3]`, `#3303B7`) and `delta_e` (uses `color_decompose_delta_e`).
- Bridge: stdio `initialize` + `tools/list` return `start_orca` + the tool set.

## Known gaps / follow-ups

- MCPClientConfig / "MCP Clients" auto-config tab not ported; opencode is configured directly.
- `get_scene_info` `with_model_object_features` will not report OraExt per-volume annotations.
- OraExt-specific tools (app prefs, Support Zones geometry) are candidate `MCP-2`.
