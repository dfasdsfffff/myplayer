# MyPlayer Release Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Release build, portable package, installer seed, CI gate, and release evidence reproducible and auditable.

**Architecture:** CMake owns a configuration-specific install tree containing executable, runtime dependencies, plugins, notices, and licenses. The portable script packages and verifies only that tree. Qt test targets receive per-target runtime directories so parallel builds never copy plugins into a shared directory.

**Tech Stack:** CMake 3.25+, C++20, PowerShell, Qt 6, vcpkg, CTest, GitHub Actions, Advanced Installer seed.

**Spec:** `docs/superpowers/specs/2026-09-19-release-readiness-design.md`

## Global Constraints

- Work only in the isolated `codex/release-readiness` worktree.
- Keep `play_core` Qt-free and do not change playback behavior.
- Do not claim a binary is signed or public-release ready without a certificate and clean-machine QA evidence.
- Do not add a package manager or download media during verification.
- Every production behavior change starts with a failing automated test or contract check.
- Scripts and installer metadata must consume the CMake install layout rather than discover DLLs independently.

## Review Focus

- Parallel Release builds must never share a `platforms/` write directory.
- Package validation must reject missing plugins, notices, licenses, Debug DLLs, or a failed smoke test.
- No Qt5, FFmpeg 57, or obsolete `..\\myplayer\\bin` installer reference may remain.
- CI must test and package Release, not merely compile it.
- The release report must preserve unmet signing and manual-QA gates.

---

### Task 1: Isolate Qt Test Runtime Directories

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/cmake_configuration_tests.cmake`

**Produces:** Every Qt test has Debug/Release runtime output at `${CMAKE_BINARY_DIR}/<config>/tests/<target>`; plugin deployment is confined to `$<TARGET_FILE_DIR:<target>>/platforms`.

- [ ] **Step 1: Write the failing configuration contract.** Add a CMake assertion that generated Qt test projects cannot have two `copy_directory` commands writing a configuration-root `platforms` directory.
- [ ] **Step 2: Run RED.** Run `ctest --test-dir build -C Debug -R cmake_configuration_tests --output-on-failure`. Expected: failure because current targets share `build/<config>/platforms`.
- [ ] **Step 3: Implement.** Add a CMake helper assigning each Qt test explicit Debug and Release runtime directories before its existing deployment command; retain target-relative plugin destinations.
- [ ] **Step 4: Run GREEN.** Run `cmake --build build --config Debug --parallel; ctest --test-dir build -C Debug -R cmake_configuration_tests --output-on-failure; cmake --build build --config Release --parallel`. Expected: all commands exit zero.
- [ ] **Step 5: Commit.** Run `git add CMakeLists.txt tests/cmake_configuration_tests.cmake; git commit -m "build: isolate Qt test runtime directories"`.

### Task 2: Define and Test the CMake Release Install Tree

**Files:**
- Modify: `CMakeLists.txt`
- Create: `cmake/MyPlayerInstall.cmake`
- Create: `tests/install_layout_tests.cmake`

**Produces:** `cmake --install <build> --config Release --prefix <stage>` creates `myplayer.exe`, runtime DLLs, `platforms/qwindows.dll`, project notices, and `licenses/` containing the complete local third-party licenses.

- [ ] **Step 1: Write the failing install-layout test.** Create `tests/install_layout_tests.cmake` that installs to `${BINARY_DIR}/install-layout-test`, requires the executable/plugin/notices/license directory, and rejects `*d.dll` runtime files.
- [ ] **Step 2: Run RED.** Run `ctest --test-dir build -C Release -R install_layout_tests --output-on-failure`. Expected: failure because current install rules install only the executable.
- [ ] **Step 3: Implement.** Add CMake install rules for executable, `$<TARGET_RUNTIME_DLLS:myplayer>`, Qt platform plugin, notices, and local license files. Put generator-expression-specific installation in `cmake/MyPlayerInstall.cmake`.
- [ ] **Step 4: Run GREEN.** Run `cmake --build build --config Release --parallel; ctest --test-dir build -C Release -R install_layout_tests --output-on-failure`. Expected: staging contains only Release dependencies and required legal material.
- [ ] **Step 5: Commit.** Run `git add CMakeLists.txt cmake/MyPlayerInstall.cmake tests/install_layout_tests.cmake; git commit -m "build: stage complete release install tree"`.

### Task 3: Add Version Metadata and Replace the Obsolete Installer Seed

**Files:**
- Modify: `apps/qt_player/myplayer.rc`
- Modify: `CMakeLists.txt`
- Modify: `myplayer.aip`
- Create: `tests/release_metadata_tests.cmake`

**Produces:** `myplayer.exe` has CMake-derived `1.0.0` version and MyPlayer file metadata; the AIP references `dist/stage` with Qt6/current FFmpeg names.

- [ ] **Step 1: Write the failing metadata contract.** Create `tests/release_metadata_tests.cmake` that rejects Qt5, `avcodec-57`, `..\\myplayer\\bin`, missing `VS_VERSION_INFO`, and AIP version different from the CMake project version.
- [ ] **Step 2: Run RED.** Run `ctest --test-dir build -C Debug -R release_metadata_tests --output-on-failure`. Expected: failure identifying legacy installer paths and missing resource metadata.
- [ ] **Step 3: Implement.** Pass `PROJECT_VERSION_*` to the resource compiler; add Windows version fields. Update AIP product version, publisher/support links, source root, and component/file references to the stage tree; do not add false signature declarations.
- [ ] **Step 4: Run GREEN.** Run `cmake --build build --config Release --parallel; ctest --test-dir build -C Debug -R release_metadata_tests --output-on-failure`. Expected: version resource exposes `1.0.0` and MyPlayer fields.
- [ ] **Step 5: Commit.** Run `git add apps/qt_player/myplayer.rc CMakeLists.txt myplayer.aip tests/release_metadata_tests.cmake; git commit -m "release: align version and installer metadata"`.

### Task 4: Package and Verify the Staged Release Artifact

**Files:**
- Modify: `scripts/package-portable.ps1`
- Create: `tests/package_script_contract_tests.ps1`
- Create: `tests/package_script_contract_tests.cmake`
- Modify: `CMakeLists.txt`

**Produces:** `package-portable.ps1` stages through CMake install, validates the layout, smoke-tests the executable, rejects Debug DLLs, and writes ZIP plus `.sha256`.

- [ ] **Step 1: Write failing contract tests.** The PowerShell test must require `cmake --install`, `Start-Process -Wait`, required-file validation, Debug-DLL rejection, `Get-FileHash -Algorithm SHA256`, and prohibit direct vcpkg DLL enumeration. Register it through a CMake wrapper.
- [ ] **Step 2: Run RED.** Run `ctest --test-dir build -C Debug -R package_script_contract_tests --output-on-failure`. Expected: failure because the script copies vcpkg DLLs and lacks smoke/hash validation.
- [ ] **Step 3: Implement.** Stage via CMake install; validate file layout and `licenses/`; reject `*d.dll`; run `myplayer.exe --smoke-test` with `Start-Process -Wait -PassThru`; archive only validated files; emit SHA-256. Keep the workspace-boundary safety check before removing old output.
- [ ] **Step 4: Run GREEN.** Run `ctest --test-dir build -C Debug -R package_script_contract_tests --output-on-failure; powershell -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -BuildDir build -Configuration Release -Version 1.0.0`. Expected: ZIP and matching SHA-256 exist and ZIP has notices/licenses but no Debug DLLs.
- [ ] **Step 5: Commit.** Run `git add scripts/package-portable.ps1 tests/package_script_contract_tests.ps1 tests/package_script_contract_tests.cmake CMakeLists.txt; git commit -m "release: verify staged portable package"`.

### Task 5: Gate CI and Record Remaining Manual Release Evidence

**Files:**
- Modify: `.github/workflows/windows-build.yml`
- Modify: `README.md`
- Modify: `docs/release-checklist.md`
- Modify: `docs/player-qa-checklist.md`
- Create: `docs/releases/2026-09-19-release-candidate.md`
- Modify: `docs/superpowers/plans/2026-09-18-player-comprehensive-optimization.md`
- Modify: `tests/release_metadata_tests.cmake`

**Produces:** CI tests Release and packages an artifact; release evidence explicitly blocks public distribution while signing and clean-machine/manual QA lack evidence.

- [ ] **Step 1: Write failing CI/document contract.** Extend `release_metadata_tests.cmake` to require Release CTest, portable-packaging, and artifact upload workflow steps; require report fields for signing, clean-machine install, and eight-hour local/network QA.
- [ ] **Step 2: Run RED.** Run `ctest --test-dir build -C Debug -R release_metadata_tests --output-on-failure`. Expected: failure because CI stops after Release compilation and no release report exists.
- [ ] **Step 3: Implement.** Add CI Release CTest, package, and upload steps. Update README/checklists and create the report with current automated evidence and explicitly unchecked external gates; mark only actually completed earlier-plan tasks.
- [ ] **Step 4: Run GREEN.** Run `cmake --build build --config Debug --parallel; ctest --test-dir build -C Debug --output-on-failure; cmake --build build --config Release --parallel; ctest --test-dir build -C Release --output-on-failure; powershell -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -BuildDir build -Configuration Release -Version 1.0.0; git diff --check`. Expected: commands succeed; report retains unsigned and manual QA as blockers.
- [ ] **Step 5: Commit.** Run `git add .github/workflows/windows-build.yml README.md docs/release-checklist.md docs/player-qa-checklist.md docs/releases/2026-09-19-release-candidate.md docs/superpowers/plans/2026-09-18-player-comprehensive-optimization.md tests/release_metadata_tests.cmake; git commit -m "ci: gate release artifacts and record QA evidence"`.

## Final Verification

- [ ] Build Debug and Release in parallel three times.
- [ ] Run all Debug and Release CTest tests.
- [ ] Build the portable package; validate its SHA-256 sidecar and list ZIP contents.
- [ ] Inspect executable version metadata and signature status; record unsigned status accurately.
- [ ] Check every release and player-QA item against the evidence report.
- [ ] Run `git diff --check` and whole-branch review before integration.
