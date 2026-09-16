# Feature Engineering Agent - OrcaSlicer

This repository is a **feature-engineering workspace**: plan and implement new features and
ideas into OrcaSlicer, then build, test, branch, commit and merge them. Work only against
this repository. This file is loaded alongside `AGENTS.md`, which holds the build, code
style, review, testing and localization rules - those still apply.

## How to work (feature loop)

Run the whole loop **with the user**, and agree the plan before writing code:

1. **Plan first.** Clarify the idea, restate it in your own words, and agree on scope,
   defaults, edge cases and how it will be verified. Do not start implementing until the
   user agrees. For anything non-trivial, write the design down first (see the Documentation
   section in `AGENTS.md`).
2. **Implement** one minimal, reviewable logical change at a time.
3. **Build** locally on Windows (see below) until it is clean.
4. **Test** with a targeted Catch2 test and/or a documented manual protocol; record the
   evidence.
5. **Branch, commit, merge.** One feature per `feat/<id>` branch off `main`. Commit product
   code first, then docs, and merge `--no-ff` into the integration branch only when the
   feature is complete and verified. Never commit unverified work, build outputs or secrets.
   Do not amend or force-push - fix and commit again.

## Feature rules

- Features are **opt-in** and behavior-neutral at defaults: with the feature's options at
  their defaults, output must be byte-identical to stock OrcaSlicer.
- Preserve `.3mf` project and printer-profile backward compatibility, with version-migration
  handling.
- Follow the code style, review focus and localization rules in `AGENTS.md`.
- Never modify `deps/` or `deps_src/` (vendored).
- No branding: no vendor names, logos, URLs or sponsor text unless explicitly requested.

## Local build on this machine (Windows)

Use `build_win.bat` from the repo root (it `cd`s itself). Apply this preamble in every build
shell first:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;C:\Strawberry\c\bin;C:\Strawberry\perl\bin;$env:PATH"
$env:CMAKE_TLS_VERIFY = "0"   # mandatory here: Schannel revocation checking is offline
```

```powershell
.\build_win.bat -ds                        # first build: dependencies, then the slicer
.\build_win.bat -s --no-configure -j 8     # incremental rebuild after source edits
.\build_win.bat -s --run-tests             # build and run tests
```

The binary is `build/src/Release/orca-slicer.exe`. Kill a running slicer before rebuilding
(it holds the DLL and causes `LNK1104`). `build_win.bat -h` documents every flag.

## Branch model

- `main` - untouched upstream mirror; the rebase target. Never commit feature work here.
- Integration branch - where features are merged. `port/integration` today; the user may
  rename it.
- `feat/<id>` - one branch per feature, branched from `main` or the integration branch,
  merged back `--no-ff` when the feature is complete and verified.
