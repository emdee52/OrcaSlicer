# Building this OrcaSlicer locally (Windows)

Decision for this fork: **build locally, on Windows only.** GitHub Actions is not used
for the port work; it builds every platform and is unnecessary here. Everything below
runs on this machine and produces only a Windows x64 binary.

The build driver is `OrcaSlicer/build_win.bat`. It handles the MSBuild generator, dep
builds, caching and the dev environment. Run it from the workspace root (it `cd`s itself).

## Prerequisites

Installed and verified on this machine:

- Visual Studio 2022 Community, with `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`.
- **Official Kitware CMake** (`C:\Program Files\CMake\bin`), not the MinGW/WinLibs CMake
  that a `BrechtSanders.WinLibs` package puts on `PATH`. The script sets
  `CMAKE_POLICY_VERSION_MINIMUM=3.5` for the deps that still ask for old policies.
- Git (with Git Credential Manager, needed to push branches).
- Strawberry Perl (`C:\Strawberry`) - **required for the dependency build** (OpenSSL and
  friends).

Install or repair the prerequisites with WinGet:

```powershell
.\OrcaSlicer\build_win.bat -u            # install/update CMake, Perl and Git
.\OrcaSlicer\build_win.bat --install-vs ide   # fresh machine only, then restart the shell
```

A prerequisite installed by WinGet only reaches new shells. Every build shell needs this
preamble - Kitware CMake first, then Strawberry, plus the TLS variable explained below:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\bin;$env:PATH"
$env:CMAKE_TLS_VERIFY = "0"
```

## TLS / dependency downloads (required on this machine)

The dependency build downloads its source archives with CMake's `file(DOWNLOAD)`. On this
machine Windows Schannel refuses the TLS handshake with `CRYPT_E_REVOCATION_OFFLINE`: it
cannot reach the certificate revocation servers. Every download then fails with a
certificate/"SSL connect error", which surfaces as a failed dependency (`dep_libnoise`
first, in our case). Git is unaffected because it uses OpenSSL rather than Schannel, which
is why `git push` works while the build's downloads do not.

CMake's own error names the fix: set `CMAKE_TLS_VERIFY=0` in the environment before
building. This disables certificate verification for CMake's downloads only. It is a
workaround for a dead revocation path, not a preference - on a machine whose revocation
servers are reachable, do not set it.

Verified on this machine: `curl --ssl-no-revoke https://github.com` returns 200, and
`file(DOWNLOAD ... TLS_VERIFY OFF)` succeeds, while the default fails with SSL error 35.

## First build (the long one)

`OrcaSlicer/deps/build` does not exist yet, so the first run downloads and compiles every
dependency. This is the multi-hour part. Do it once, before porting, so later builds and
tests are fast and the toolchain is proven. Apply the preamble above first.

```powershell
.\OrcaSlicer\build_win.bat -ds -j 8
```

- `-d` builds the dependencies into `OrcaSlicer/deps/build` (cached afterwards).
- `-s` builds the slicer.
- Expect roughly 1-3+ hours for the deps on this machine, then the first slicer build.
  Later `-s` builds reuse the dep tree and only compile what changed.

Outputs:

- `OrcaSlicer/build/src/Release/orca-slicer.exe` - the runnable binary.
- `OrcaSlicer/deps/build/OrcaSlicer_dep/` - the cached dependency tree.

## Iteration loop (after the first build)

```powershell
.\OrcaSlicer\build_win.bat -s --no-configure -j 8   # rebuild changed sources only
.\OrcaSlicer\build_win.bat -s --run-tests           # build + run the unit tests
```

Standalone tests:

```powershell
ctest --test-dir OrcaSlicer/build/tests -C Release --output-on-failure
```

Use `-i` to install a self-contained tree (binary + resources) at
`OrcaSlicer/build/OrcaSlicer/orca-slicer.exe`, which is the better thing to actually run:

```powershell
.\OrcaSlicer\build_win.bat -s -i
```

## Useful flags

| Flag | Purpose |
|------|---------|
| `-h` | full help; every flag documented |
| `-d` | build dependencies |
| `-s` | build the slicer |
| `-ds` | dependencies then slicer (first build) |
| `-s --no-configure -j N` | incremental rebuild, N parallel jobs |
| `-s --run-tests` | build and run tests |
| `-i` | install into the build tree's `OrcaSlicer/` folder |
| `-c` | clean the tree(s) this run builds (with `-d` also wipes deps) |
| `-k` | kill running build/compiler processes |
| `-v` | verbose compiler command lines |
| `-D` | dry run: print commands without running |
| `-l -x` | clang-cl + Ninja (optional alternate toolchain) |
| `--slicer-target libslic3r` | build one target only |

## Troubleshooting

- Dependency downloads fail with a certificate/"SSL connect error": set
  `$env:CMAKE_TLS_VERIFY = "0"` (see the TLS section above). This is the known issue on
  this machine.
- A build that fails after changing CMake: clean the tree and rebuild, because the
  generator records the CMake that configured it - `.\OrcaSlicer\build_win.bat -d -c`.
- A dependency build that fails deep in package resolution: retry that stage with
  `.\OrcaSlicer\build_win.bat -d -c -v`.
- A stale or half-configured tree: `.\OrcaSlicer\build_win.bat -s -c`.
- Compiler processes stuck: `.\OrcaSlicer\build_win.bat -k`.
- The script refuses a config or tree it did not create; `-h` lists the valid values.
- If Strawberry Perl shadows a real CMake, the script reorders `PATH` itself; do not
  "fix" it by hand.

## Known local build fixes

Workarounds for this machine's older toolchain, applied to upstream sources. They are
**not** part of the port: they carry a `[BUILDFIX:<id>]` marker instead of `[ORCAPORT:]`,
are kept in their own commits, and are dropped once the underlying toolchain is updated.

| ID | Where | Why | Drop when |
|----|-------|-----|-----------|
| `VS17.8` | `src/libslic3r/GCode/GCodeProcessor.cpp` (`store_move_vertex`, the `axis_jerk_for_preview` lambda) | MSVC 14.38 (VS 17.8) wrongly raises C3493 for a non-odr-used constant-expression enum used without capture; current MSVC, GCC and Clang accept it. Adding `normal_mode` to the capture list is a behaviour-neutral no-op. | Visual Studio is updated to 17.10+ (or the build uses VS 2026). Remove the extra capture and delete this row. |

## GitHub Actions in this fork

Not used for the port. For reference, `OrcaSlicer/.github/workflows/build_all.yml`
triggers on push/PR to `main`/`release/*` and nightly, and fans out to Linux, Windows,
macOS and flatpak. To keep the fork from doing that:

- Settings -> Actions -> General -> Disable, or
- do not push port work to `main` (use the `port/*` branches below), or
- trim `build_all.yml` to the Windows job (accepting conflicts when syncing upstream).

Committing only `OrcaSlicer/porting/**` does not match the workflow's `paths`, so it does
not trigger a build. Committing `src/**` does.

## Branch model

- `main` - mirror of the fork; never commit port work here. This is the rebase target.
- `port/integration` - holds `OrcaSlicer/porting/` and integrates the port. **Build from
  this branch.**
- `port/<id>` - one branch per feature (e.g. `port/MT-1`), branched from
  `port/integration`, merged back when the feature is verified.

To sync with upstream later:

```powershell
git -C OrcaSlicer fetch upstream
git -C OrcaSlicer rebase upstream/main      # on the port branch
```
