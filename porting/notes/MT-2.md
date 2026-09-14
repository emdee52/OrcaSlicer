# MT-2 - Extra Energy Save (idle tool cooldown)

- Source: `NEOTKOCM_RELEASE_2_45.md`.
- Fork: `OrcaFS-NeotkoCM` @ `cf1255c741`.
- Category: B. App-level preference, subordinate to MT-1.
- Status: **ported** (branch `port/MT-1`), build clean.

## Design
Same mechanism as MT-1, widened to every park:
- `m_orcaext_toolsleep_deep = enabled && deep`.
- When deep, the `line_id > last_use` and "already switched off" guards are dropped, so any
  parked tool is set to 0 °C. Orca's preheat backtrace removes a shutdown that falls inside
  the preheat window, so a tool returning soon keeps its heat and no print waits for a
  reheat.

## Key
- `orca_ext_idle_tool_deep_sleep` | app bool | default `false`.
- UI: checkbox "Extra Energy Save mode" in Preferences, next to MT-1.
- Global: `OrcaExt::set_idle_tool_power_down_deep(bool)`.

## Verification
- Compile/link: 0 errors.
- Off-by-default: deep false -> identical to MT-1 (which is itself stock when MT-1 is off).
- Pending: runtime multi-tool check that long gaps cool the tool and short gaps do not.

## Deliberate deviation
MT-1 and MT-2 were committed together (one commit, one patch `02_MT-1-2.patch`) because they
are one mechanism sharing the same function and dialog. Both markers are present.
