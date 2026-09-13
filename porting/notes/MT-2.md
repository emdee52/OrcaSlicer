# MT-2 - Extra Energy Save (idle tool cooldown)

- Source: `NEOTKOCM_RELEASE_2_45.md` (Extra Energy Save mode).
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741`.
- Category: B. Subordinate to MT-1.

## New files
- none beyond MT-1's `NeotkoBedNozzleExtras.*`.

## Upstream anchors (fork lines)
- `src/libslic3r/GCode/GCodeProcessor.cpp`
  - `apply_config()`: `m_neotko_toolsleep_deep = m_neotko_toolsleep_enabled && config.neotko_idle_tool_deep_sleep.value;` (~730)
  - `neotko_toolsleep_rewrite_line()`: deep eligibility (~4143, 4165); reason string "tool off while parked" (~4187)
- `src/libslic3r/GCode/GCodeProcessor.hpp` - `bool m_neotko_toolsleep_deep{false};` (~754)
- `src/libslic3r/PrintConfig.cpp` - `neotko_idle_tool_deep_sleep` (~5708)
- `src/libslic3r/Print.cpp` (~672), `Preset.cpp` (~955)
- `src/slic3r/GUI/Plater.cpp` (~12802), `NeotkoBedNozzleExtras.cpp` (DEEPSLEEP_KEY, ~19, 91-118, 145-163)

## Keys
- `neotko_idle_tool_deep_sleep` | `coBool` | default `false` | `PrintConfig.cpp:5708` (mode `comDevelop`)
- Same string is the app-level preference.

## Gates to remove / change
- No LibreMode or env gate. Keep the dependency on MT-1 (`enabled && deep`); that is design, not a gate.
- Expose the checkbox normally; grey it out while MT-1 is off.

## Coupling / blockers
- Depends on MT-1 and on upstream's `;cooldown` marker + preheat backtrace (present).
- `ooze_prevention` must be enabled for the emitted standby command to exist. Surface a warning when it is off.

## Verification
- Tool with no later extrusion gets standby 0; tools returning inside the preheat window keep heat; single-tool file emits nothing; off = byte-identical.
- The release note's "no print time added" claim only holds while the preheat cancels short cooldowns; verify on a real multi-tool slice.

## Open questions
- Whether upstream's separate post-`run_post_process` preheat injector pass conflicts with the `S0` rewrite.
