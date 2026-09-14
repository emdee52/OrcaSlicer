# MT-1 - Hotends that finished switch off

- Source: `NEOTKOCM_RELEASE_2_45.md`.
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741`.
- Category: B. App-level preference.
- Status: **ported** (branch `port/MT-1`), build clean.

## Design (chosen for low coupling)
The fork mirrored the app preference into a hidden PrintConfig option. We avoid adding a
print key: the GUI writes an app-level global that the G-code post-processor reads. This
keeps it out of profiles (it survives profile updates) and adds one anchor in libslic3r.

- New module: `src/libslic3r/OrcaExt/IdleToolPowerDown.{hpp,cpp}` - `OrcaExt::set_idle_tool_power_down(bool)` / `idle_tool_power_down_settings()`.
- `src/libslic3r/GCode/GCodeProcessor.cpp` - `orcaext_toolsleep_prepare()` builds a per-tool
  "last extruding line" table from `m_result.moves`; `orcaext_toolsleep_rewrite_line()`
  rewrites a parked tool's standby `M104 S<idle> T<n> ;cooldown` to `S0` once
  `line_id > last_use`. Called before `export_line.update()` so byte offsets stay in sync.
- `GCodeProcessor::apply_config(const PrintConfig&)` reads the global into members.
- `src/slic3r/GUI/Preferences.cpp` - checkbox "Turn off unused hotends fully (0 °C)"
  (app key `orca_ext_idle_tool_power_down`); schedules a background process on change.
- `src/slic3r/GUI/GUI_App.cpp` - mirrors app_config into the global at startup.

## Key
- `orca_ext_idle_tool_power_down` | app bool | default `false`.

## Notes
- Only `;cooldown` M104 lines with an explicit `T` are touched; other M104 lines are working
  temperatures and are left alone.
- Requires Ooze prevention (only then is a standby command emitted). If nothing is switched
  off on a multi-tool print, a non-critical warning says so.

## Verification
- Compile/link: `build_win.bat -s -j 8` -> 0 errors, binary produced.
- Off-by-default: flag false -> no rewrite, output byte-identical (by construction).
- Pending: runtime G-code check on a multi-tool slice with Ooze prevention on, asserting
  `S0` appears on the finished tool's cooldown and nowhere else.
