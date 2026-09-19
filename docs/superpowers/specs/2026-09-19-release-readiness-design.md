# MyPlayer Release Readiness Design

**Date:** 2026-09-19

## Goal

Make the current MyPlayer revision reproducibly distributable as a Windows Release candidate: build and package from one staged install tree, validate that artifact automatically, and make its legal, version, CI, and manual-QA status explicit.

## Scope

- Eliminate concurrent Qt platform-plugin copying by giving Qt test executables distinct runtime directories.
- Use CMake install rules to stage the player executable, runtime dependencies, Qt plugins, notices, and complete bundled third-party licenses.
- Make the portable-package script package only the staged install tree; verify the staged executable with `--smoke-test`, reject debug DLLs, and emit a SHA-256 sidecar.
- Add Windows executable version metadata sourced from CMake project version.
- Extend Windows CI to test Release and verify a portable package.
- Add automated tests for release metadata and the package-script contract, and add a dated release-evidence report template.

## Non-goals

- Purchasing, storing, or using a code-signing certificate. No certificate is available. The release process must state that unsigned binaries are not production-publication ready.
- Automatically executing clean-profile extraction/deletion or eight-hour media QA. Those require a clean Windows environment and real hardware/media evidence.
- Adding telemetry, auto-update, or an installer.

## Design

### One source of truth for release contents

`cmake --install` will produce a configuration-specific staging directory. CMake owns executable installation, runtime DLL deployment, Qt plugin deployment, project notices, and a `licenses/` directory containing the complete local license files that are available in the repository. The portable script will never reconstruct that dependency graph independently.

MyPlayer ships only as a portable archive. Users extract the archive, run `myplayer.exe`, and delete the extracted directory to remove it. No installer project is maintained or validated.

### Deterministic test runtime layout

Each Qt test target receives its own output directory below `build/<config>/tests/<target>`. Its post-build command copies Qt runtime files and `platforms/` only into that target directory. No two parallel targets write to the same destination.

### Portable-package contract

`scripts/package-portable.ps1` first builds the Release app, runs `cmake --install` into a temporary staging tree, and validates that tree. Validation requires `myplayer.exe`, `platforms/qwindows.dll`, project notices, the license directory, and no debug DLL names. It launches `myplayer.exe --smoke-test` with `Start-Process -Wait`; a nonzero exit fails packaging. The script archives the stage and writes `<archive>.sha256` using SHA-256.

### Version and signing posture

The Windows resource script embeds the CMake project version in the executable, including product name, file description, original filename, and company/publisher name. The package script checks the executable version resource. The release report includes an explicit unsigned-artifact gate. A future certificate can be wired through a separate signing command without changing package layout.

### CI and acceptance

Windows CI configures once, builds and tests both Debug and Release, then invokes portable packaging. The portable artifact and SHA-256 are uploaded. The release report template records commit, tools, checksums, unsigned/signed status, clean-profile extraction results, long-run media results, and hardware decode results. It cannot be marked complete until every manual QA item has evidence.

## Verification

- CMake contract tests assert distinct Qt test runtime directories and install rules.
- A PowerShell contract test asserts the package script invokes install, validates the smoke test, writes SHA-256, and rejects Debug DLLs.
- Build Debug and Release with parallel execution; run both CTest configurations.
- Run the portable packager, inspect the ZIP, validate its checksum, and run its smoke test from the staged tree.
- On a separate clean Windows profile, extract the archive, launch it, play media, delete the extracted directory, and complete the documented long-run QA before public release.

## Acceptance Criteria

- Parallel Debug and Release builds do not have shared Qt-plugin output writes.
- The generated portable ZIP contains current Release binaries, Qt plugins, notices, and complete bundled license files; its SHA-256 sidecar matches it.
- The staged executable exposes product version metadata and passes its smoke test.
- CI gates Debug and Release CTest plus portable-package validation.
- The repository contains a release-evidence report that visibly blocks public distribution while code signing and clean-machine/manual QA are absent.
