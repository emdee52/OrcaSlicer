# MCP-2 - OraExt introspection tools

Status: ported (build clean; runtime verified). Builds on `MCP-1`.

## Goal

Fill the gap the upstream OrcaMCP tools leave on this fork: they are config/pipeline generic
and know nothing about the OraExt features. MCP-2 adds tools for the fork's **app-level
preferences**, its **per-volume annotations** (paint facets, Support Zones) and its
**OraExt config keys**, plus app-log diagnostics.

Not a port - this is original work on top of the MCP-1 server. No source sha; the design is
recorded here.

## New tools (6; server total 69)

| Tool | What it does |
|---|---|
| `get_app_preferences` | The `orca_ext_*` AppConfig keys (idle-tool power down, free-Z, Snap & Drag, MCP) with current + default + description. Generic config tools cannot see AppConfig. |
| `set_app_preference` | Sets one `orca_ext_*` bool and applies the same side effects as the Preferences checkboxes (`OrcaExt` setters / MCP server start-stop). Rejects unknown keys. |
| `list_orcaext_keys` | The OraExt print/region/object config keys with current (active preset) value and default. Use with `apply_config` / `set_object_config`. |
| `get_object_features` | Per-volume paint state (`support`, `seam`, `multi_material`, `fuzzy_skin`, `counterbore_bridge`) and Support Zones settings (`gesture`, `lean_deg`, `roof_only`, `solid`, `land_only`). Fills the empty `get_scene_info(with_model_object_features)` in this fork. Optional `object_id` filter. |
| `get_log_tail` | Tail of the newest `%APPDATA%/OrcaSlicer/log/debug_*.log.0` (`lines`, default 200, max 5000). For diagnosing slicing errors/crashes without a screenshot. |
| `get_suppressed_dialogs` | Messages captured instead of shown while MCP is driving (complements `active_warnings`). |

## Files

- New: `src/slic3r/GUI/OrcaMCP/OrcaExtTools.cpp` (`OrcaMCPServer::register_orcaext_tools()`).
- Modified (`[ORCAPORT:MCP-2]`): `OrcaMCPServer.hpp` (declaration), `OrcaMCPServer.cpp`
  (register call), `src/slic3r/CMakeLists.txt` (source).

## Design notes

- **Main-thread dispatch is required.** Tool handlers run on the HTTP worker thread, but
  `AppConfig::save()` asserts it is on the main thread (first cut failed with `Calling
  AppConfig::save() from a worker thread!`). All app-touching handlers now go through
  `OrcaMCP::run_on_main_thread()` (from `OrcaMCPCommon.hpp`), as the MCP-1 tools do.
- `list_orcaext_keys` reads `PresetBundle::full_config()` and `print_config_def` defaults;
  `clonable_ptr` has no `!= nullptr`, so the default is guarded with `.get() != nullptr`.
- `get_object_features` reads Support Zones from the per-volume `DynamicPrintConfig` via
  `dynamic_cast` on `option(key)` (the `option<T>` template did not resolve for a
  `const ModelVolume`).
- Log selection uses `boost::filesystem::last_write_time` (returns `std::time_t` on Boost 1.84),
  scanning `Slic3r::data_dir()/log` for the newest `debug_*` file.

## Verification (2026-09-16, Windows; `build_win.bat -s -j 8`, 0 errors; 69 tools)

- `get_app_preferences` -> all 7 keys with values/defaults.
- `set_app_preference` `orca_ext_free_z` true then false -> `status: success` + `changed`; unknown
  key -> `status: error`.
- `list_orcaext_keys` -> 42 keys with `present`/`value`/`default` (e.g. `bridge_expansion_extra`
  0/0, `hybrid_outer_wall` classic/classic, `interlock_perimeter_count` 5/5).
- `get_object_features` after `load_model(20mm_cube.obj)` -> object 61 / volume 64 with paint
  flags all false and empty `support_zones` (none set).
- `get_log_tail` -> real newest log tail; `get_suppressed_dialogs` -> `count: 0`.

## Known gaps / follow-ups

- `get_object_features` reports paint **presence**, not per-facet counts or Support Zones
  geometry beyond the stored gesture JSON.
- Remaining OrcaMCP extras still not ported: MCPClientConfig auto-config UI and the homepage
  "Connect AI" card (MCP-1 decision).
