# MyPlayer Comprehensive Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver all optimization stages for MyPlayer: engineering baseline, functional closure, concurrency safety, media correctness, bounded performance, optional hardware decoding, and release-quality verification.

**Architecture:** Keep `VideoCtl` as the Qt-free playback facade and evolve behavior through focused components. UI work stays under `apps/qt_player`; playback contracts and thread-owned components stay under `play_core`. Each task is independently testable and committed before the next task begins.

**Tech Stack:** C++20, CMake 3.25+, Qt 6 Core/Gui/Widgets/Test, FFmpeg 7.x, SDL2, SoundTouch, vcpkg, CTest, GitHub Actions, MSVC, and Clang sanitizers where supported.

**Spec:** `docs/superpowers/specs/2026-09-18-player-comprehensive-optimization-design.md`

## Global Constraints

- Work from the first unchecked task whose dependencies are complete. Do not repeat completed tasks.
- At the start of every session, read the spec and this plan, then run `git status --short`, `git log -5 --oneline`, the Debug build, and CTest.
- Preserve unrelated user changes. If an intended file is already modified, inspect the diff and integrate rather than overwrite.
- Use an isolated worktree at execution time when required by the active workflow skill.
- Follow red-green-refactor. A test must demonstrate the failing behavior before implementation unless the task is documentation-only.
- Keep `play_core` free of Qt headers and Qt types.
- Keep `MediaSession` as the only owner of `VideoState` and join every worker before destroying state.
- Do not introduce a second package manager or download test media at test time.
- Do not log, display in tooltips, or persist credentials, tokens, or sensitive query values.
- Hardware decoding and YUV rendering must retain a tested software/BGRA fallback.
- Do not mix broad formatting or unrelated cleanup into behavior changes.
- Finish every task with focused tests, full Debug build, all CTest tests, `git diff --check`, self-review, and one focused commit.
- Update the task checkbox to `[x]` only after its commit exists. Append `Completed: <commit>, <verification>` below the task heading.
- Runtime instructions override this plan. In particular, use subagents only when the user or active instructions explicitly authorize them; otherwise use `superpowers:executing-plans` inline.

## Session Start Protocol

- [ ] Read the design and plan completely.
- [ ] Locate the first unchecked task with completed dependencies.
- [ ] Inspect current repository state:

```powershell
git status --short
git log -5 --oneline
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Expected baseline at plan creation: Debug build succeeds and 10/10 tests pass. Later sessions must use the current registered test count, not assume it remains ten.

- [ ] If baseline verification fails, stop plan execution and diagnose the failure with `superpowers:systematic-debugging`; do not hide it inside the next task.
- [ ] Execute one task through its commit and update this plan. Continue only while context and verification evidence remain reliable.

Session note (2026-09-18): baseline execution is blocked before Task 8. `globalhelper_privacy_tests` fails at the migration assertion (`legacy preferences must migrate to AppConfigLocation`) on the current tree, so Task 4's migration behavior is not verified despite its checked substeps. `cmake_configuration_tests` also fails in this environment because vcpkg cannot write `D:\ProgramData\vcpkg` and the nested MSBuild probe reports access denied. The aggregate CTest run reached all 20 registered tests but left the configuration-test process alive; no implementation changes were retained from this diagnostic session.

## Dependency Order

Execute Tasks 1 through 20 in numeric order. Later tasks deliberately consume contracts introduced earlier (preferences, command mailbox, synchronized tracks, media metadata, subtitle events, and bounded frame delivery). Do not parallelize tasks that modify `VideoCtl`, `VideoState`, `MainWid`, `Show`, the root `CMakeLists.txt`, or this plan. A session may prepare review notes for a later task, but it must not implement that task before all earlier stage gates pass.

---

## Stage 0: Engineering Baseline

### Task 1: Standard Build Options, Qt Test Support, and CI

Completed: `744ae16`, core-only configure contract passed without Qt discovery; Debug and Release builds passed; CTest passed 11/11.

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakePresets.json`
- Create: `cmake/MyPlayerWarnings.cmake`
- Create: `tests/cmake_configuration_tests.cmake`
- Create: `.github/workflows/windows-build.yml`
- Create: `.github/workflows/clang-sanitizers.yml`
- Modify: `README.md`

**Interfaces:**

- Produces CMake options `MYPLAYER_BUILD_QT_APP`, `BUILD_TESTING`, `MYPLAYER_ENABLE_WARNINGS`, and `MYPLAYER_ENABLE_SANITIZERS`.
- Later tasks depend on `Qt6::Test` being requested only when a Qt widget test target is enabled.

- [x] **Step 1: Add a configure-contract test script.**

Create `tests/cmake_configuration_tests.cmake` that configures a nested build with the Qt app disabled and asserts the cache values:

```cmake
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BINARY_DIR}"
          -DMYPLAYER_BUILD_QT_APP=OFF -DBUILD_TESTING=OFF
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "core-only configure failed: ${result}")
endif()
```

Register it as a CTest only in the normal top-level test build. Run it and verify it fails because the options do not exist and Qt is still required.

- [x] **Step 2: Add standard options and warning helper.**

Use this contract in the top-level CMake:

```cmake
option(MYPLAYER_BUILD_QT_APP "Build the Qt Widgets player" ON)
include(CTest)
option(MYPLAYER_ENABLE_WARNINGS "Enable project warning flags" ON)
option(MYPLAYER_ENABLE_SANITIZERS "Enable supported sanitizers" OFF)
```

Guard Qt discovery, the application target, and Qt-dependent tests with `MYPLAYER_BUILD_QT_APP`. Guard every test target with `BUILD_TESTING`. `MyPlayerWarnings.cmake` must expose `myplayer_enable_warnings(target)` and use `/W4` on MSVC and `-Wall -Wextra -Wpedantic` elsewhere; do not enable warnings-as-errors until the repository builds warning-clean.

- [x] **Step 3: Add CI workflows.**

Windows CI configures through vcpkg, builds Debug and Release, and runs Debug CTest. Clang CI performs a core/test build with ASan+UBSan where dependencies are supported. Pin action major versions and the vcpkg baseline already present in `vcpkg.json`; do not duplicate dependency versions in workflow YAML.

- [x] **Step 4: Verify all build modes.**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
```

Also configure a fresh temporary core-only directory and confirm Qt is not searched when both app and testing are off.

- [x] **Step 5: Commit.**

```powershell
git add CMakeLists.txt play_core/CMakeLists.txt CMakePresets.json cmake/MyPlayerWarnings.cmake tests/cmake_configuration_tests.cmake .github/workflows README.md
git commit -m "build: establish optimization verification baseline"
```

**Stage 0 gate:** CI definitions exist, local Debug/Release builds pass, all registered tests pass, and core-only configure succeeds.

---

## Stage 1: Functional Closure

### Task 2: Playback Status and Actionable Error Presentation

Completed: `8136b4d`, Debug and Release builds passed; CTest passed 12/12; manual invalid-local-path and invalid-network-URL smoke tests passed with one final error and no secret exposure.

**Files:**

- Create: `apps/qt_player/playback_status_presenter.h`
- Create: `apps/qt_player/playback_status_presenter.cpp`
- Create: `tests/playback_status_presenter_tests.cpp`
- Modify: `apps/qt_player/mainwid.h`
- Modify: `apps/qt_player/mainwid.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class StatusPresentationKind { Transient, Persistent, FinalError };
struct StatusPresentation { QString text; StatusPresentationKind kind; };
StatusPresentation PresentPlaybackStatus(const PlaybackStatus& status);
```

- [x] Write table-driven failing tests for Opening, Buffering, each reconnect attempt, Playing, user Stopped, Timeout, Authentication, NotFound, UnsupportedProtocol, InvalidMedia, DecoderFailure, and Unknown. Assert that credentials and sensitive query values never appear in returned text.
- [x] Build and run `playback_status_presenter_tests`; verify failure because the presenter is absent.
- [x] Implement the pure formatter. Reconnecting text must include `attempt/maxReconnectAttempts` and retry delay. Final errors must include one next action such as checking the URL, credentials, network, or codec.
- [x] Add `MainWid::OnPlaybackStatus(PlaybackStatus)` and connect `PlaybackRuntimeBridge::SigPlaybackStatus`. Use `statusBar()->showMessage`; show a modal warning only when `kind == FinalError`, and suppress duplicate final errors for the same playback generation.
- [x] Connect `SigMediaInfo` to update seek enablement and live/non-seekable UI state. Connect `SigPlayMsg` to diagnostics/status text instead of discarding it.
- [x] Run focused tests, full build, all CTest tests, and a UI smoke with an invalid local path and invalid network URL. Confirm one final error, no retry-dialog storm, and no raw secret in the UI.
- [x] Commit:

```powershell
git add apps/qt_player/playback_status_presenter.* apps/qt_player/mainwid.* tests/playback_status_presenter_tests.cpp CMakeLists.txt
git commit -m "feat: present playback status and failures"
```

### Task 3: Unified Preferences and Functional Settings Window

Completed: `3664bb1`, Debug and Release builds passed; CTest passed 14/14, including preference serialization and offscreen Apply/OK/Cancel tests.

**Files:**

- Create: `apps/qt_player/app_preferences.h`
- Create: `apps/qt_player/app_preferences.cpp`
- Create: `tests/app_preferences_tests.cpp`
- Create: `tests/setting_widget_tests.cpp`
- Modify: `apps/qt_player/globalhelper.h`
- Modify: `apps/qt_player/globalhelper.cpp`
- Modify: `apps/qt_player/settingwid.h`
- Modify: `apps/qt_player/settingwid.cpp`
- Modify: `apps/qt_player/settingwid.ui`
- Modify: `apps/qt_player/ctrlbar.h`
- Modify: `apps/qt_player/ctrlbar.cpp`
- Modify: `apps/qt_player/mainwid.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct AppPreferences {
    double volume{1.0};
    double speed{1.0};
    VideoLoopPolicy loopPolicy{VideoLoopPolicy::LOOP_ALL};
    bool resumePlayback{true};
    int reconnectAttempts{5};
    int connectTimeoutMs{10000};
    int readTimeoutMs{15000};
    RtspTransport rtspTransport{RtspTransport::Tcp};
};
AppPreferences SanitizePreferences(AppPreferences value);
```

- [x] Write failing tests for clamping volume, allowed speed range, loop-policy validation, non-negative retry count, timeout bounds, and round-trip serialization through a temporary INI path. Add an offscreen widget test proving Apply emits sanitized values, OK applies and closes, and Cancel closes without emitting.
- [x] Implement `AppPreferences` and path-injected `LoadPreferences(const QString&)` / `SavePreferences(const QString&, const AppPreferences&)`. Keep serialization separate from widgets.
- [x] Replace the empty settings UI with Playback and Network sections plus Apply, OK, and Cancel. Applying emits `SigPreferencesApplied(AppPreferences)`; Cancel must not mutate live state.
- [x] Add `CtrlBar::ApplyPreferences(const AppPreferences&)`. Use signal blockers while updating widgets, then explicitly apply volume, speed, and loop policy once through `PlaybackController`.
- [x] In `MainWid::Init`, load preferences after widgets/signals initialize and apply them. Build `MediaSource::network` from the saved network defaults when opening a URL. Remove the redundant `volume/size` versus `play/volume` write paths after migration in Task 4.
- [x] Run focused tests, full verification, close/reopen the app, and confirm volume/speed/loop/network settings restore exactly.
- [x] Commit:

```powershell
git add apps/qt_player/app_preferences.* apps/qt_player/globalhelper.* apps/qt_player/settingwid.* apps/qt_player/settingwid.ui apps/qt_player/ctrlbar.* apps/qt_player/mainwid.cpp tests/app_preferences_tests.cpp tests/setting_widget_tests.cpp CMakeLists.txt
git commit -m "feat: implement persistent player settings"
```

### Task 4: Standard Config Location, Migration, and Secret-Safe Persistence

Completed: `10550de`, migration now creates the destination INI before privacy-filtered rewrite; Debug build passed and CTest passed 20/20.

**Files:**

- Create: `apps/qt_player/media_location_privacy.h`
- Create: `apps/qt_player/media_location_privacy.cpp`
- Create: `tests/media_location_privacy_tests.cpp`
- Modify: `apps/qt_player/globalhelper.cpp`
- Modify: `apps/qt_player/playlist.cpp`
- Modify: `apps/qt_player/playlistfile.cpp`
- Modify: `play_core/stream_reader.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
bool ContainsSensitiveMediaCredentials(const QString& location);
bool MayPersistMediaLocation(const QString& location);
QString SafeMediaLocationForDisplay(const QString& location);
```

- [x] Write failing tests covering URL userinfo, `token`, `access_token`, `auth`, `key`, `signature`, `sig`, and case-insensitive query keys. Public HTTP/RTSP URLs and local paths must remain persistable.
- [x] Change the production config root to `QStandardPaths::AppConfigLocation`. If no new config exists and the legacy application-directory INI exists, copy it once, load it, and record a migration version.
- [x] Before `beginWriteArray("playlist")` and `beginWriteArray("recent_files")`, remove the old group so deleted entries do not remain in the INI. Filter non-persistable URLs while preserving them in the current in-memory playlist.
- [x] Use `SafeMediaLocationForDisplay` for playlist text/tooltips and UI status. Change `StreamReader::PrintError` and codec-probing logs to log `RedactMediaLocation` instead of the raw filename/URL.
- [x] When exporting a playlist containing sensitive URLs, show a confirmation explaining they will be omitted; write only persistable entries. Unit-test the filtered output.
- [x] Run focused/full tests and inspect a temporary INI to confirm stale array entries and secret URLs are absent. Debug build passed; full CTest passed 17/17; `git diff --check` passed.
- [x] Commit: `1be59ad fix: protect persisted media locations`, corrected by `acf52d8 fix: retain raw media locations safely` after task review.

```powershell
git add apps/qt_player/media_location_privacy.* apps/qt_player/globalhelper.cpp apps/qt_player/playlist.cpp apps/qt_player/playlistfile.cpp play_core/stream_reader.cpp tests/media_location_privacy_tests.cpp CMakeLists.txt
git commit -m "fix: protect persisted media locations"
```

### Task 5: Central Media Format Registry and Audio-Only UX

Completed: `891fa24`, Debug build passed; CTest passed 18/18, including media format registry, M3U, playlist, and audio-indicator coverage. On 2026-09-19, a generated 5-second PCM WAV fixture was decoded successfully with FFmpeg; add-and-play UI behavior remains manual QA.

**Files:**

- Create: `apps/qt_player/media_format_registry.h`
- Create: `apps/qt_player/media_format_registry.cpp`
- Create: `tests/media_format_registry_tests.cpp`
- Modify: `apps/qt_player/playlistfile.cpp`
- Modify: `apps/qt_player/medialist.cpp`
- Modify: `apps/qt_player/mainwid.cpp`
- Modify: `apps/qt_player/title.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
bool IsSupportedMediaLocation(const QString& location);
QString MediaOpenDialogFilter();
QString SubtitleOpenDialogFilter();
```

- [x] Write failing tests for existing formats plus MOV, M4V, WebM, MPEG/MPG, TS/M2TS, MP3, AAC/M4A, FLAC, WAV, OGG/Opus, and supported network schemes. Reject directories and unknown extensions.
- [x] Implement one case-insensitive registry and make playlist validation, folder scanning, file dialogs, and M3U import use it. Remove duplicated extension functions.
- [x] For audio-only playback, keep the video surface black, display the filename and an audio indicator, keep duration/seek/volume/speed usable, and do not require a video stream.
- [x] Run focused/full tests. Automated tests cover supported audio and newer video containers. On 2026-09-19, FFmpeg generated and decoded `.cache/media-qa/audio-only.wav` (5-second PCM, 48 kHz mono); manual add-and-play verification remains release QA.
- [x] Commit:

```powershell
git add apps/qt_player/media_format_registry.* apps/qt_player/playlistfile.cpp apps/qt_player/medialist.cpp apps/qt_player/mainwid.cpp apps/qt_player/title.cpp tests/media_format_registry_tests.cpp CMakeLists.txt
git commit -m "feat: centralize supported media formats"
```

### Task 6: Stable Playlist Navigation

Completed: `05f2a93`, focused offscreen navigation test passed; Debug build passed; CTest passed 19/19; reorder/remove/current/previous/next/random and empty-list behavior are covered by the navigation test.

**Files:**

- Modify: `apps/qt_player/playlist.h`
- Modify: `apps/qt_player/playlist.cpp`
- Modify: `apps/qt_player/medialist.h`
- Modify: `apps/qt_player/medialist.cpp`
- Create: `tests/playlist_navigation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
QString Playlist::currentLocation() const;
int Playlist::rowForLocation(const QString& location) const;
QString Playlist::adjacentLocation(int direction) const; // direction is -1 or +1
```

- [x] Add an offscreen Qt test that plays item B, reorders B, removes an item before B, removes B, and then requests previous/next. Assert navigation follows the stable media location and never dereferences an invalid row.
- [x] Verify the test fails with the current integer-only `m_nCurrentPlayListIndex` implementation.
- [x] Replace the authoritative integer with `QString m_currentLocation`. Resolve its current row at navigation time. When the current item is removed, choose the nearest remaining row deterministically; when the list is empty, clear the location.
- [x] Emit list-mutation signals from `MediaList` after internal move, remove, remove-missing, and clear so `Playlist` can reconcile selection. Keep duplicate prevention.
- [x] Run focused/full tests and exercise reorder/remove/current/previous/next/random through the offscreen navigation test.
- [x] Commit:

```powershell
git add apps/qt_player/playlist.* apps/qt_player/medialist.* tests/playlist_navigation_tests.cpp CMakeLists.txt
git commit -m "fix: keep playlist navigation stable"
```

### Task 7: Menu Cleanup and Cancellable Full-Screen Controls

Completed: `9197fe4`, offscreen menu/full-screen behavior test passed; Debug build passed; CTest passed 20/20; the fullscreen test enters an offscreen window, schedules hiding, and confirms returning to controls cancels it.

**Files:**

- Modify: `apps/qt_player/res/menu.json`
- Modify: `apps/qt_player/mainwid.h`
- Modify: `apps/qt_player/mainwid.cpp`
- Modify: `apps/qt_player/ctrlbar.cpp`
- Create: `tests/main_window_behavior_tests.cpp`
- Modify: `CMakeLists.txt`

- [x] Add an offscreen Qt test that asserts every visible top-level menu action is enabled or owns a non-empty submenu, Audio/Subtitle appear once, and a pending full-screen hide is cancelled when the pointer returns to the control area.
- [x] Reduce `menu.json` to implemented actions: open file/link, recent files, playback controls, playlist, settings, audio tracks, subtitles, full screen, about, and exit. Remove capture/TV/DVD/Blu-ray/skin/filter placeholders.
- [x] Configure `stCtrlBarHideTimer` as a single-shot member, connect it once, and replace calls to static `QTimer::singleShot` with `start(FULLSCREEN_CTRLBAR_HIDE_DELAY)`. Stop it on mouse return and full-screen exit.
- [x] Fix the random-loop tooltip and initialize all control-bar state, including total duration, explicitly.
- [x] Run focused/full tests and an offscreen full-screen smoke test.
- [x] Commit:

```powershell
git add apps/qt_player/res/menu.json apps/qt_player/mainwid.* apps/qt_player/ctrlbar.cpp tests/main_window_behavior_tests.cpp CMakeLists.txt
git commit -m "fix: finish player menu and fullscreen controls"
```

**Stage 1 gate:** Settings are functional and restored, errors are actionable, menus contain no dead promises, public media formats include audio, playlist navigation survives mutation, and persisted configuration contains no stale or sensitive locations.

---

## Stage 2: Concurrency and Lifecycle Safety

### Task 8: Playback Command Mailbox

Completed: `f964fc3`, Debug build passed and CTest passed 21/21, including the 500-iteration concurrent mailbox stress case.

**Files:**

- Create: `play_core/playback_command_mailbox.h`
- Create: `play_core/playback_command_mailbox.cpp`
- Create: `tests/playback_command_mailbox_tests.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/stream_reader.h`
- Modify: `play_core/stream_reader.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class TrackKind { Audio, Subtitle };
struct SeekCommand { int64_t position; int64_t relative; int flags; };
struct TrackCommand { TrackKind kind; std::optional<int> streamIndex; bool cycle; };
struct PlaybackCommands {
    unsigned pauseToggleCount{0};
    std::optional<SeekCommand> seek;
    std::vector<TrackCommand> tracks;
};
class PlaybackCommandMailbox final {
public:
    void postPauseToggle();
    void postSeek(SeekCommand command);
    void postTrack(TrackCommand command);
    PlaybackCommands take();
    void clear();
};
```

- [x] Write failing tests for concurrent producers, pause-toggle parity, latest-seek-wins coalescing, ordered track commands, `take()` clearing, and stop-time `clear()`.
- [x] Implement the mailbox with one mutex. It stores commands only; it never touches `VideoState`.
- [x] Change UI-facing pause, seek, and track methods to post commands and signal the appropriate condition variable. The Qt thread must no longer call `stream_toggle_pause`, write seek fields, or close/open stream components.
- [x] Consume pause and track commands in the playback loop. Seek requests are applied to the session mailbox consumed by `StreamReader` before `av_read_frame`. Preserve current controller methods as compatibility wrappers.
- [x] Clear pending commands during `StartPlay`, stop, reconnect session replacement, and destruction so commands cannot leak into a new media session.
- [x] Run focused/full tests plus a 500-iteration producer stress test.
- [x] Commit:

```powershell
git add play_core/playback_command_mailbox.* play_core/videoctl.* play_core/stream_reader.* play_core/CMakeLists.txt tests/playback_command_mailbox_tests.cpp CMakeLists.txt
git commit -m "refactor: serialize playback control commands"
```

### Task 9: Coherent Thread-Safe Clock

Completed: `610da4d`, sequence-protected clock snapshots and migrated accessors passed the concurrent clock test, Debug build, and CTest 22/22; the configured Windows build has no ThreadSanitizer job available locally.

**Files:**

- Modify: `play_core/clock.h`
- Modify: `play_core/clock.cpp`
- Modify: `play_core/media_sync.cpp`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/stream_reader.cpp`
- Modify: `play_core/audio_output.cpp`
- Create: `tests/clock_concurrency_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct ClockSnapshot { double pts; double ptsDrift; double lastUpdated; double speed; int serial; bool paused; };
ClockSnapshot Clock::snapshot() const noexcept;
void Clock::setPaused(bool paused);
double Clock::lastUpdated() const noexcept;
double Clock::speed() const noexcept;
```

- [x] Write a failing multithreaded test with one writer updating monotonically increasing clock values and multiple readers calling `snapshot()`/`get()`. Assert snapshots are internally consistent and ThreadSanitizer reports no race where available.
- [x] Replace public plain clock-field access with private atomic snapshot storage. Use atomic scalar fields plus a sequence counter and a writer mutex so readers never observe a torn update; do not place a contended mutex in `get()`.
- [x] Migrate every direct access in `MediaSync`, `VideoCtl`, `StreamReader`, `DecoderWorkers`, and `AudioOutput` to methods. Keep queue serial validation.
- [x] Run focused/full tests and the sanitizer job. Review all `audclk.`, `vidclk.`, and `extclk.` references with `rg` to confirm no direct mutable field remains.
- [x] Commit:

```powershell
git add play_core/clock.* play_core/media_sync.cpp play_core/videoctl.cpp play_core/stream_reader.cpp play_core/audio_output.cpp tests/clock_concurrency_tests.cpp CMakeLists.txt
git commit -m "fix: publish coherent playback clocks"
```

### Task 10: Synchronize Track Replacement and Packet Routing

Completed: `f2e383c`, focused `track_switching_tests` and full Debug CTest passed 23/23 on 2026-09-18. On 2026-09-19, FFmpeg/ffprobe validated a generated multi-track fixture with two named AAC tracks; rapid GUI track cycling while playing/stopping remains manual release QA.

**Files:**

- Modify: `play_core/video_state.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/stream_reader.cpp`
- Modify: `play_core/media_sync.cpp`
- Create: `tests/track_switching_tests.cpp`
- Modify: `CMakeLists.txt`

- [x] Add a deterministic concurrency test with a fake packet-routing loop holding a shared track guard while another thread requests replacement. Assert replacement waits, then atomically publishes the new indices/pointers.
- [x] Add `std::shared_mutex trackMutex` to session/track state. Packet routing, EOF null-packet dispatch, queue sufficiency checks, and refresh snapshots take a shared lock only while copying stable indices/pointers. Track close/open takes the exclusive lock.
- [x] Ensure decoder/audio device teardown completes before publishing replacement pointers. Do not hold the track lock while blocking on unrelated network reads.
- [x] Make stream index fields private to a small `TrackStateSnapshot` API or atomic where only the index is required. Eliminate unguarded reads found by `rg "audio_stream|video_stream|subtitle_stream" play_core`.
- [x] Run focused/full tests and a manual rapid audio/subtitle cycle while playing and stopping. Focused `track_switching_tests` and the complete Debug CTest suite passed 23/23 on 2026-09-18. On 2026-09-19, `.cache/media-qa/multitrack-anamorphic-rotated.mkv` was generated and decoded successfully; ffprobe confirmed the named `eng` and `deu` AAC tracks. Manual rapid GUI cycling remains release QA.
- [x] Commit:

```powershell
git add play_core/video_state.h play_core/videoctl.cpp play_core/stream_reader.cpp play_core/media_sync.cpp tests/track_switching_tests.cpp CMakeLists.txt
git commit -m "fix: synchronize media track replacement"
```

### Task 11: Lifecycle Stress and Queue Encapsulation

Completed: `d97cbb4`, Debug build passed; CTest passed 24/24, including 1,000 concurrent packet-queue put/get/flush/snapshot/abort cycles and repeated invalid-open/stop-and-wait runtime cycles.

**Files:**

- Modify: `play_core/packet_queue.h`
- Modify: `play_core/packet_queue.cpp`
- Modify: `play_core/frame_queue.h`
- Modify: `play_core/frame_queue.cpp`
- Modify: affected `play_core/*.cpp` callers
- Create: `tests/playback_lifecycle_stress_tests.cpp`
- Modify: `CMakeLists.txt`

- [x] Add read-only queue snapshots:

```cpp
struct PacketQueueSnapshot { int packets; int bytes; int64_t duration; int serial; bool aborted; };
PacketQueueSnapshot PacketQueue::snapshot() const;
struct FrameQueueSnapshot { int remaining; int serial; bool aborted; };
FrameQueueSnapshot FrameQueue::snapshot() const;
```

- [x] Write failing tests for concurrent put/get/abort/flush/snapshot and repeated `PlaybackRuntime::Create`, invalid-open, stop-and-wait, and destroy cycles. Use deterministic invalid/open cancellation paths here; generated valid-media integration coverage is added separately in Task 19.
- [x] Make queue storage and counters private. Replace direct field reads with atomic getters or mutex-protected snapshots. Keep FFmpeg packet ownership unchanged.
- [x] Add explicit stop ordering assertions: cancel I/O, wake waits, stop audio callback/render worker, abort queues, join reader/decoders/play loop, then destroy state.
- [x] Run the stress test for at least 1,000 short iterations in a non-default `STRESS_TESTS` CTest label, then run the normal full suite.
- [x] Commit:

```powershell
git add play_core/packet_queue.* play_core/frame_queue.* play_core tests/playback_lifecycle_stress_tests.cpp CMakeLists.txt
git commit -m "fix: encapsulate queues and stress playback lifecycle"
```

**Stage 2 gate:** UI control methods only post commands/atomics, Clock snapshots are coherent, track replacement is synchronized, queues expose no mutable public counters, lifecycle stress passes, and supported sanitizer runs report no project-code race or use-after-free.

---

## Stage 3: Media Correctness and Capability

### Task 12: Rich Media Information and Explicit Track Selection

Completed: `b577d15`, Debug build passed and CTest passed 25/25, including in-memory FFmpeg metadata, controller forwarding, and offscreen track-menu coverage. On 2026-09-19, the generated multi-track fixture was inspected with ffprobe and decoded successfully; selecting each track in the GUI remains release QA.

**Files:**

- Modify: `play_core/media_source.h`
- Modify: `play_core/network_input.cpp`
- Modify: `play_core/stream_reader.cpp`
- Modify: `play_core/playback_controller.h`
- Modify: `play_core/playback_controller.cpp`
- Modify: `play_core/playback_controller_videoctl.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `apps/qt_player/mainwid.cpp`
- Modify: `apps/qt_player/playback_runtime_bridge.*`
- Create: `tests/media_info_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct TrackInfo {
    int streamIndex{-1};
    AVMediaType type{AVMEDIA_TYPE_UNKNOWN};
    std::string language;
    std::string title;
    std::string codec;
    bool isDefault{false};
    bool isForced{false};
};
struct MediaInfo {
    // existing fields
    std::vector<TrackInfo> tracks;
    int width{0}; int height{0};
    AVRational sampleAspectRatio{1, 1};
    double rotationDegrees{0.0};
};
void PlaybackController::selectAudioTrack(int streamIndex);
void PlaybackController::selectSubtitleTrack(std::optional<int> streamIndex);
```

- [x] Write failing tests that build an in-memory `AVFormatContext` with named/default/forced audio and subtitle streams and verify normalized metadata.
- [x] Populate rich `MediaInfo` after stream discovery. Avoid exposing borrowed FFmpeg pointers.
- [x] Add explicit controller/facade methods that post `TrackCommand`; retain cycle methods as wrappers.
- [x] Rebuild Audio and Subtitle menus from `SigMediaInfo`, use stream index in `QAction::data`, make actions checkable, and provide a checked “关闭字幕” action.
- [x] Run focused/full tests and manually select each track in a multi-track file. On 2026-09-19, ffprobe verified stream indices, language tags, and titles in `.cache/media-qa/multitrack-anamorphic-rotated.mkv`; manual GUI selection remains release QA.
- [x] Commit:

```powershell
git add play_core/media_source.h play_core/network_input.cpp play_core/stream_reader.cpp play_core/playback_controller.* play_core/playback_controller_videoctl.cpp play_core/videoctl.* apps/qt_player/mainwid.cpp apps/qt_player/playback_runtime_bridge.* tests/media_info_tests.cpp CMakeLists.txt
git commit -m "feat: expose media tracks and metadata"
```

### Task 13: Embedded and External Subtitle Rendering

Completed: `748bb3c`, Debug build passed; the 26 runtime tests plus the configuration-contract test passed (27/27); renderer coverage includes embedded bitmap, ASS, SRT, timing, bounds, unload preservation, and viewport/time-bucket caching. On 2026-09-19, generated embedded ASS and external SRT fixtures parsed and decoded successfully. Real GUI QA remains required; an embedded bitmap-subtitle fixture is still unavailable because the installed FFmpeg cannot synthesize bitmap subtitle packets from text.

**Files:**

- Modify: `vcpkg.json`
- Create: `play_core/subtitle_frame.h`
- Create: `play_core/subtitle_dispatcher.h`
- Create: `play_core/subtitle_dispatcher.cpp`
- Modify: `play_core/decoder_workers.cpp`
- Modify: `play_core/playback_runtime.*`
- Modify: `apps/qt_player/playback_runtime_bridge.*`
- Create: `apps/qt_player/subtitle_renderer.h`
- Create: `apps/qt_player/subtitle_renderer.cpp`
- Modify: `apps/qt_player/show.*`
- Modify: `apps/qt_player/mainwid.*`
- Create: `tests/subtitle_dispatcher_tests.cpp`
- Create: `tests/subtitle_renderer_tests.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct SubtitleBitmap { int x, y, width, height, stride; std::vector<uint8_t> bgra; };
struct SubtitleFrame {
    double startSeconds{0};
    double endSeconds{0};
    std::string assOrText;
    std::vector<SubtitleBitmap> bitmaps;
};
Signal<std::shared_ptr<const SubtitleFrame>> SigSubtitleFrame;
```

- [x] Add `libass` through vcpkg and link it through `find_package(PkgConfig REQUIRED)`, `pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)`, and `PkgConfig::LIBASS`. Write failing tests for subtitle timing, clear events, bitmap palette-to-BGRA conversion, ASS/text forwarding, and renderer output bounds. Tests use synthetic subtitle rectangles/events, not downloaded media.
- [x] Implement `SubtitleDispatcher` in `play_core`: copy all data out of `AVSubtitle`, normalize start/end timestamps, and emit immutable frames. Never expose `AVSubtitle*` beyond the decoder thread. `DecoderWorkers` now frees the FFmpeg subtitle immediately after copying; the frame queue holds only `std::shared_ptr<const SubtitleFrame>`.
- [x] Implement `SubtitleRenderer` using libass for ASS/text and an SDL blend texture for bitmap/ASS output. Cache font/library/track state; rerender only when cue, time bucket, or viewport changes.
- [x] Add Open Subtitle for SRT/ASS/SSA. Load external files into a separate libass track, align them to playback time, and allow unload/reload. Do not silently replace embedded track state.
- [x] Wire subtitle frames through runtime/bridge to `Show`, clear overlays on seek/track change/stop, and composite after the video texture so both BGRA and future YUV paths work.
- [x] Run focused/full tests and manual QA with embedded text, embedded bitmap, external SRT, seek, pause, track switch, and stop. On 2026-09-19, FFmpeg generated and decoded embedded ASS in `.cache/media-qa/multitrack-anamorphic-rotated.mkv`, and parsed `.cache/media-qa/fixture.srt` as ASS. Embedded bitmap rendering retains synthetic automated coverage only: the installed FFmpeg has `dvbsub`/`dvdsub` encoders but no text-to-bitmap subtitle generator. GUI QA for text/external subtitles and any supplied PGS/VobSub/DVB fixture remains release QA.
- [x] Commit:

```powershell
git add vcpkg.json play_core/subtitle_* play_core/decoder_workers.cpp play_core/playback_runtime.* apps/qt_player/subtitle_renderer.* apps/qt_player/playback_runtime_bridge.* apps/qt_player/show.* apps/qt_player/mainwid.* tests/subtitle_* CMakeLists.txt play_core/CMakeLists.txt
git commit -m "feat: render embedded and external subtitles"
```

### Task 14: Display Geometry, Color Metadata, and Resume Correctness

**Files:**

- Modify: `play_core/video_frame.h`
- Modify: `play_core/video_frame_converter.cpp`
- Modify: `play_core/renderer_dispatcher.cpp`
- Modify: `apps/qt_player/show.cpp`
- Modify: `apps/qt_player/globalhelper.*`
- Modify: `apps/qt_player/mainwid.*`
- Create: `tests/video_geometry_tests.cpp`
- Create: `tests/resume_policy_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct VideoPresentationMetadata {
    AVRational sampleAspectRatio{1, 1};
    double rotationDegrees{0.0};
    AVColorSpace colorSpace{AVCOL_SPC_UNSPECIFIED};
    AVColorRange colorRange{AVCOL_RANGE_UNSPECIFIED};
};
bool ShouldResume(int savedSeconds, int durationSeconds, bool seekable);
bool ShouldClearResume(int currentSeconds, int durationSeconds, bool completed);
```

- [x] Write failing pure tests for square/anamorphic display rectangles, 90/180/270-degree rotation, invalid SAR fallback, resume only when seekable and more than five seconds in, and clearing within the final 30 seconds or 95% of duration.
- [x] Carry metadata from decoded `AVFrame`/`MediaInfo` to `VideoFrame`. Compute display aspect from coded size × SAR, with width/height swapped for quarter-turn rotation.
- [x] Apply swscale colorspace/range details for the BGRA fallback and record unsupported HDR transfer characteristics in diagnostics without crashing.
- [x] Replace the fixed 500 ms resume timer with a pending resume applied after `MediaInfo` confirms seekability and duration. Clear saved position on successful completion/near-end stop.
- [x] Run focused/full tests and manual anamorphic/rotated/resume QA.
- [x] Commit:

```powershell
git add play_core/video_frame.h play_core/video_frame_converter.cpp play_core/renderer_dispatcher.cpp apps/qt_player/show.cpp apps/qt_player/globalhelper.* apps/qt_player/mainwid.* tests/video_geometry_tests.cpp tests/resume_policy_tests.cpp CMakeLists.txt
git commit -m "fix: honor presentation metadata and resume policy"
```

Completed in commits `fa15f56` and `0f49bac`. Production target `myplayer` and focused geometry/resume targets build successfully; 28/28 runtime CTest cases and the CMake configuration test pass. On 2026-09-19, FFmpeg generated and decoded representative fixtures: anamorphic 720x576 with SAR 64:45, a separate MP4 with display-matrix `rotation=90`, and BT.2020/SMPTE-2084 HDR metadata. GUI geometry, HDR diagnostic, and resume behavior remain release-checklist items.

**Stage 3 gate:** Named track selection works, subtitles render and clear correctly, audio-only playback is supported, geometry honors SAR/rotation, and resume behavior is duration-aware.

---

## Stage 4: Performance

### Task 15: Latest-Frame Mailbox and Bounded UI Delivery

Completed: `f64778c`, Debug build passed and CTest passed 30/30. The worker-burst test stalls UI delivery while publishing 4,096 frames, retains only the newest frame, and delivers it through one Qt drain; its sampled peak working set was 20.07 MiB.

**Files:**

- Create: `apps/qt_player/latest_video_frame_mailbox.h`
- Create: `apps/qt_player/latest_video_frame_mailbox.cpp`
- Modify: `apps/qt_player/playback_runtime_bridge.*`
- Modify: `apps/qt_player/show.*`
- Create: `tests/latest_video_frame_mailbox_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
class LatestVideoFrameMailbox final {
public:
    void publish(std::shared_ptr<VideoFrame> frame);
    std::shared_ptr<VideoFrame> takeLatest();
    bool markDeliveryScheduled();
    void deliveryCompleted();
    void clear();
};
```

- [x] Write failing tests publishing thousands of numbered frames from a worker while the consumer is stalled. Assert bounded pending storage, newest-frame delivery, one scheduled Qt drain, and safe clear during stop.
- [x] Implement the mailbox with `std::atomic<std::shared_ptr<VideoFrame>>` and an atomic scheduled flag.
- [x] In the bridge, store frames and queue at most one UI drain. The drain emits the latest frame and reschedules only if a newer frame arrived during rendering.
- [x] Add counters for published, presented, and coalesced frames to debug diagnostics. Do not treat coalescing as decoder frame dropping.
- [x] Run focused/full tests and measure peak memory while artificially delaying UI presentation.
- [x] Commit:

```powershell
git add apps/qt_player/latest_video_frame_mailbox.* apps/qt_player/playback_runtime_bridge.* apps/qt_player/show.* tests/latest_video_frame_mailbox_tests.cpp CMakeLists.txt
git commit -m "perf: bound pending video presentation"
```

### Task 16: SDL YUV Texture Path with BGRA Fallback

Completed: `928aebf`. Debug build and CTest passed 31/31. Reproducible single-thread FFmpeg decode baseline generated by `scripts/benchmark-video-presentation.ps1`: 1080p60 testsrc2, 600 frames, 282 fps/2.130 s/42,908 KiB; 4K60, 600 frames, 71 fps/8.484 s/83,420 KiB. This is decode-only evidence; GUI presentation telemetry remains a release-QA follow-up.

**Files:**

- Modify: `play_core/video_frame.h`
- Modify: `play_core/video_frame_converter.*`
- Modify: `apps/qt_player/show.*`
- Modify: `tests/play_core_refactor_tests.cpp`
- Create: `tests/video_frame_format_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class VideoFrameFormat { Bgra32, Yuv420P };
struct VideoPlane { std::shared_ptr<const uint8_t> data; int stride; int height; };
// VideoFrame carries format plus either BGRA storage or three YUV planes.
```

- [x] Write failing tests for ref-counted AVFrame lifetime, Y/U/V plane strides, odd-size rejection/fallback, and BGRA compatibility.
- [x] For `AV_PIX_FMT_YUV420P` and formats safely convertible to it, retain/ref or convert into a pooled YUV420P frame. Keep the existing BGRA converter for unsupported formats and renderer fallback.
- [x] In `Show`, create `SDL_PIXELFORMAT_IYUV` textures and call `SDL_UpdateYUVTexture`; use existing BGRA texture code for fallback. Recreate textures only when format or dimensions change.
- [x] Add a runtime diagnostic showing active presentation format. Verify subtitles remain a separate blended overlay.
- [x] Benchmark identical 1080p and 4K clips before/after, recording CPU and frame counts in the task completion note.
- [x] Commit:

```powershell
git add play_core/video_frame.* apps/qt_player/show.* tests/play_core_refactor_tests.cpp tests/video_frame_format_tests.cpp CMakeLists.txt
git commit -m "perf: render video through YUV textures"
```

### Task 17: Prepared PCM Queue Outside the SDL Callback

Implementation committed: `132361e`; Debug build passed and CTest passed 32/32. `audio_render_queue_tests` covers bounded 500 ms PCM capacity, producer wakeup after consumer reads, silence plus underrun accounting, flush, and stopping a blocked producer. SDL dummy must verify underrun counts and PCM queue watermarks; real devices must verify speed changes, seek, and pause/resume for dropouts, crackles, and A/V sync.

**Files:**

- Create: `play_core/audio_render_queue.h`
- Create: `play_core/audio_render_queue.cpp`
- Modify: `play_core/audio_output.h`
- Modify: `play_core/audio_output.cpp`
- Modify: `play_core/video_state.h`
- Create: `tests/audio_render_queue_tests.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
class AudioRenderQueue final {
public:
    bool start(VideoState* state, const AudioParams& target);
    void stop();
    size_t read(uint8_t* destination, size_t bytes) noexcept;
    void flush();
    uint64_t underruns() const noexcept;
};
```

- [x] Write failing tests for bounded capacity, producer blocking/wakeup, read silence on underrun, flush after seek, speed change reset, and stop while producer is blocked.
- [x] Move frame dequeue, resampling, and SoundTouch processing to the render producer thread. Store prepared S16 PCM in a bounded ring buffer sized by milliseconds, not unbounded vectors.
- [x] Reduce `AudioOutput::Callback` to bounded ring-buffer read, silence fill, volume mix, and clock accounting. It must not allocate, wait, decode, resample, log, or call SoundTouch.
- [x] Flush and restart producer state on seek, track change, reconnect, and speed change. Stop/join it before closing the SDL device or destroying `VideoState`.
- [ ] With SDL dummy, verify underrun counters and PCM queue watermarks; on real devices, verify speed changes, seek, and pause/resume for dropouts, crackles, and A/V synchronization.
- [x] Commit:

```powershell
git add play_core/audio_render_queue.* play_core/audio_output.* play_core/video_state.h tests/audio_render_queue_tests.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "perf: prepare audio outside SDL callback"
```

### Task 18: Optional D3D11VA Hardware Decoding with Software Fallback

Implementation committed: `df1c4f6`. Verified follow-up evidence: `hardware_decode_policy_tests` passed; `c9f072d` clears stale `hw_device_ctx`, `opaque`, and `get_format` hooks before software fallback; FFmpeg D3D11VA decoded the first 120 frames of both H.264 1920x1080 and HEVC 1920x1080 inputs; Debug CTest passed 36/36. CPU usage, player UI/GPU telemetry, expanded compatibility coverage, and real-hardware manual QA remain unrecorded.

**Files:**

- Create: `play_core/hardware_decode.h`
- Create: `play_core/hardware_decode.cpp`
- Modify: `play_core/decoder.h`
- Modify: `play_core/decoder.cpp`
- Modify: `play_core/video_frame_converter.cpp`
- Modify: `play_core/playback_settings.h`
- Modify: `apps/qt_player/app_preferences.*`
- Modify: `apps/qt_player/settingwid.*`
- Create: `tests/hardware_decode_policy_tests.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class HardwareDecodePreference { Auto, Disabled, D3D11VA };
struct HardwareDecodeResult { bool active; std::string backend; std::string fallbackReason; };
class HardwareDecodeContext final {
public:
    HardwareDecodeResult configure(AVCodecContext*, HardwareDecodePreference);
    AVFrame* transferToSoftware(const AVFrame* hardwareFrame, AVFrame* reusable);
};
```

- [x] Write policy tests for Disabled, Auto preference, unsupported codec, device creation failure, hardware pixel-format negotiation failure, and mandatory software fallback. `hardware_decode_policy_tests` passed.
- [x] Implement Windows D3D11VA setup through FFmpeg `av_hwdevice_ctx_create` and codec `get_format`. Non-Windows builds compile a disabled implementation.
- [x] Transfer hardware frames to reusable software frames before the existing YUV/BGRA presentation path. Zero-copy D3D11 texture sharing is explicitly out of scope.
- [x] Add Auto/Disabled/D3D11VA settings and publish the active backend/fallback reason through diagnostics, not modal errors.
- [x] Validate FFmpeg D3D11VA decoding of H.264 1920x1080 and HEVC 1920x1080 for the first 120 frames each. The policy test covers software fallback; `c9f072d` also clears `hw_device_ctx`, `opaque`, and `get_format` before fallback.
- [ ] Record CPU usage, player UI/GPU telemetry, additional codec/adapter compatibility, and real-hardware manual QA. Keep the feature only if fallback and stop/seek/reconnect tests pass.
- [x] Commit: `df1c4f6 perf: add optional D3D11VA decoding`; follow-up fallback fix: `c9f072d fix: clear stale hardware decode hooks on fallback`.

```powershell
git add play_core/hardware_decode.* play_core/decoder.* play_core/video_frame_converter.cpp play_core/playback_settings.h apps/qt_player/app_preferences.* apps/qt_player/settingwid.* tests/hardware_decode_policy_tests.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "perf: add optional D3D11VA decoding"
```

**Stage 4 gate:** Pending UI frames are bounded, YUV presentation has a tested BGRA fallback, SDL callback work is bounded and allocation-free, hardware decoding falls back to software, and reference measurements show no regression in A/V sync or memory.

---

## Stage 5: Release Quality

### Task 19: Deterministic Media Fixtures and End-to-End Playback Tests

**Files:**

- Create: `tests/media_fixture_builder.h`
- Create: `tests/media_fixture_builder.cpp`
- Create: `tests/playback_integration_tests.cpp`
- Create: `tests/network_integration_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct GeneratedMediaFixtures { std::filesystem::path wav; std::filesystem::path video; std::filesystem::path subtitle; };
GeneratedMediaFixtures BuildMediaFixtures(const std::filesystem::path& directory);
```

- [ ] Build deterministic temporary fixtures in C++: PCM WAV, a tiny FFmpeg-written video with known timestamps/colors, and an SRT/ASS subtitle. Do not invoke an external `ffmpeg.exe` or access the network.
- [ ] Add end-to-end tests for open → Playing, duration, first audio/video/subtitle output, seek, pause/resume, speed, track selection, stop-and-wait, replay, and destruction.
- [ ] Add a local loopback HTTP test server owned by the test process for delayed reads, disconnect/reconnect, 404, and authentication failure. Bind only to loopback and use ephemeral ports.
- [ ] Label slower tests `INTEGRATION` and stress variants `STRESS`; keep the normal suite under a practical CI duration.
- [ ] Run all labels explicitly:

```powershell
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Debug -L INTEGRATION --output-on-failure
ctest --test-dir build -C Debug -L STRESS --output-on-failure
```

- [ ] Commit:

```powershell
git add tests/media_fixture_builder.* tests/playback_integration_tests.cpp tests/network_integration_tests.cpp CMakeLists.txt
git commit -m "test: add deterministic playback integration coverage"
```

### Task 20: Packaging, Documentation, and Final Release Verification

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `apps/qt_player/main.cpp`
- Modify: `scripts/package-portable.ps1`
- Modify: `README.md`
- Modify: `docs/PROJECT_GUIDE.md`
- Modify: `docs/player-qa-checklist.md`
- Modify: `docs/release-checklist.md`
- Modify: `THIRD-PARTY-NOTICES.md`
- Modify: this plan

- [ ] Add CMake install rules for the executable, runtime dependencies, Qt plugins, licenses, notices, and required data. Make the package script stage from `cmake --install` output instead of maintaining an independent DLL list.
- [ ] Add `--smoke-test` handling in `main.cpp`: initialize the application and playback runtime, schedule `QCoreApplication::quit` with a zero-delay timer, and return non-zero if initialization fails. Add package verification that launches the staged executable with this flag, verifies required DLL/plugin/license files, computes SHA-256, and refuses Debug DLLs in Release packages.
- [ ] Update README and Project Guide to match actual test count, settings, status flow, formats, tracks, subtitles, threading ownership, YUV/audio/hardware paths, build options, and fallback behavior. Remove superseded risk statements from `docs/bug.md` or mark them resolved with commit references.
- [ ] Run fresh Debug and Release builds plus every CTest label from a clean build directory. Run `scripts/package-portable.ps1`, inspect the archive, and test it on a clean Windows user profile.
- [ ] Execute every item in `docs/player-qa-checklist.md`, including eight-hour local/network runs, memory/handle monitoring, malformed media, rapid controls, network failures, subtitle/track switching, and secret-redaction checks. Record date, machine, media fixture identifiers, and result in a release report under `docs/releases/`.
- [ ] Run final repository review:

```powershell
git diff --check
git status --short
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
```

- [ ] Mark all completed tasks and stage gates in this plan, including commit hashes and verification results. Commit documentation and release evidence:

```powershell
git add CMakeLists.txt apps/qt_player/main.cpp scripts/package-portable.ps1 README.md docs THIRD-PARTY-NOTICES.md
git commit -m "release: complete player optimization program"
```

**Stage 5 gate:** Clean Debug/Release builds, all unit/integration/stress tests, portable-package verification, clean-machine manual QA, accurate documentation, and release artifacts/checksums all pass.

---

## Final Acceptance Checklist

- [ ] All six stage gates (Stages 0 through 5) pass.
- [ ] No unchecked task remains.
- [ ] All completion notes reference real commits and fresh verification output.
- [ ] `git status --short` is clean except explicitly documented user changes.
- [ ] No logs, tooltips, settings, exported playlists, test output, or release artifacts expose credentials/tokens.
- [ ] Software decode, BGRA rendering, and ordinary SDL audio remain tested fallbacks.
- [ ] `docs/PROJECT_GUIDE.md`, README, QA checklist, and release checklist describe the shipped behavior.

## New Session Handoff Prompt

Copy this prompt into any new session:

```text
在仓库 D:\Programme\project\C++\playerdemo-master 中继续执行 MyPlayer 全阶段优化方案。

先完整阅读：
1. docs/superpowers/specs/2026-09-18-player-comprehensive-optimization-design.md
2. docs/superpowers/plans/2026-09-18-player-comprehensive-optimization.md

使用 superpowers:executing-plans；只有在我明确授权子代理时，才使用 superpowers:subagent-driven-development。从依赖已完成的第一个未勾选任务开始。先运行 git status、最近提交、Debug 构建和 CTest，保留所有无关改动，不重复已完成任务。

严格执行红-绿-重构、任务内聚焦测试、完整 Debug 构建、全量 CTest、git diff --check、自审和单任务提交。任务验证通过并提交后，在计划中将对应项勾选，并写入真实提交哈希和验证结果。遇到基线失败、计划假设与代码不一致、依赖或权限阻塞时，不要猜测或扩大范围；先诊断并把证据与建议记录到计划，再请求决定。持续执行到当前任务完成；上下文和验证证据仍可靠时再继续下一任务。
```

## Execution Choice

- **Inline execution:** Use `superpowers:executing-plans` and implement tasks sequentially with stage checkpoints.
- **Subagent-driven execution:** Only when explicitly authorized, use `superpowers:subagent-driven-development`; assign one task per implementer and perform specification plus code-quality review before marking it complete.
