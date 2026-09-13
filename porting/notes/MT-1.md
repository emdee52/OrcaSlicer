# MT-1 - Hotends that finished switch off

- Source: `NEOTKOCM_RELEASE_2_45.md` (Hotends that have finished their work now switch off).
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741` (2_45 release).
- Category: B. Target lacks any equivalent.

## New files
- `src/slic3r/GUI/NeotkoBedNozzleExtras.{hpp,cpp}` - app-level settings + UI (shared with MT-2).

## Upstream anchors (fork lines)
- `src/libslic3r/GCode/GCodeProcessor.cpp`
  - `apply_config()`: `m_neotko_toolsleep_enabled = config.neotko_idle_tool_power_down.value;` (~729)
  - `reset()`: clear `m_neotko_toolsleep_enabled` (~1214)
  - new `neotko_toolsleep_prepare()` (~4058) and `neotko_toolsleep_rewrite_line()` (~4107); rewrite to `S0` at ~4136-4189
  - `run_post_process()` calls prepare before the write pass (~4850) and rewrite per line (~4894)
- `src/libslic3r/GCode/GCodeProcessor.hpp` - members/methods (~750-769)
- `src/libslic3r/PrintConfig.cpp` - `neotko_idle_tool_power_down` (~5696)
- `src/libslic3r/Print.cpp` (~671), `Preset.cpp` (~955)
- `src/slic3r/GUI/Plater.cpp` - sidebar button + mirror into config (~1040, 2283, 12800)

## Keys
- `neotko_idle_tool_power_down` | `coBool` | default `false` | `PrintConfig.cpp:5696` (mode `comDevelop`)
- Same string is the app-level preference (owned by Bed and Nozzle Extras).

## Gates to remove / change
- No LibreMode or `ORCA_DEBUG_*` feature gate. The opt-in key is the feature switch.
- Change: `comDevelop` mode hides it in the UI; expose the Bed and Nozzle Extras dialog normally (we want it ungated).
- Decision: keep the preferences app-level (not per-profile), as the fork does.

## Coupling / blockers
- Depends on `NeoDebug` (`src/libslic3r/NeoDebug.{hpp,cpp}`) for log lines. We must stub or reduce NeoDebug (shared by several selected features).
- Relies on upstream's `;cooldown` marker + preheat backtrace (present in 2.5).
- The fork mirrors app prefs into `Plater::priv::neotko_full_config()`, which also injects unselected Neotko keys; port only the two toolsleep lines.

## Verification
- Multi-tool G-code: a tool with no later extrusion gets standby 0; tools returning inside the preheat window keep heat; single-tool file emits nothing; off = byte-identical.

## Open questions
- Whether `run_post_process()` is the only export path; upstream adds a second preheat injector pass after it that may interact with the `S0` rewrite.
