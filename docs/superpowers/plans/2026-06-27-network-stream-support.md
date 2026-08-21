# Network Stream Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add configurable, cancellable and observable HTTP/HTTPS/RTSP/RTP/UDP playback with bounded reconnection while preserving existing local-file APIs.

**Architecture:** Put source metadata, error mapping, redaction, FFmpeg option construction and retry policy in focused `play_core` units. Keep `VideoCtl` responsible for session ownership and use per-session I/O state to interrupt blocking FFmpeg calls; expose structured events through the existing controller/runtime/Qt bridge chain.

**Tech Stack:** C++20, FFmpeg libavformat/libavutil, SDL2, sigslot, Qt 6, CMake/CTest.

---

## File Map

- Create `play_core/media_source.h`: public source, network option, status, error and media-info value types.
- Create `play_core/network_input.h`: source classification, validation, redaction, retry policy, I/O deadline and FFmpeg option APIs.
- Create `play_core/network_input.cpp`: pure policy implementation plus FFmpeg dictionary construction and error mapping.
- Modify `play_core/video_state.h`: add per-session source, media capabilities, I/O control and read outcome.
- Modify `play_core/videoctl.h/.cpp`: accept `MediaSource`, apply `NetworkInput`, emit status/media events, guard seek and perform bounded reconnect.
- Modify `play_core/playback_controller.h/.cpp` and `playback_controller_videoctl.cpp`: forward structured play requests while preserving string play.
- Modify `play_core/playback_runtime.h/.cpp`: forward structured status and media info.
- Modify `apps/qt_player/playback_runtime_bridge.h/.cpp`: copy structured events onto the Qt event loop.
- Create `tests/network_playback_tests.cpp`: deterministic unit tests without public network access.
- Modify `tests/play_core_refactor_tests.cpp`: controller compatibility and structured forwarding tests.
- Modify `play_core/CMakeLists.txt` and top-level `CMakeLists.txt`: compile new source and register the new test.

### Task 1: Public Types And Pure Network Policy

**Files:**
- Create: `play_core/media_source.h`
- Create: `play_core/network_input.h`
- Create: `play_core/network_input.cpp`
- Create: `tests/network_playback_tests.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing source, validation, retry and redaction tests**

Create table-driven assertions equivalent to:

```cpp
Expect(ClassifyMediaSource("movie.mp4") == MediaSourceKind::LocalFile, "local file");
Expect(ClassifyMediaSource("https://host/live.m3u8") == MediaSourceKind::Http, "https");
Expect(ClassifyMediaSource("rtsp://host/live") == MediaSourceKind::Rtsp, "rtsp");
Expect(IsRealtimeSource(MediaSourceKind::Rtp), "rtp is realtime");
Expect(!ValidateMediaSource(MediaSource{}).ok, "empty source rejected");
Expect(ReconnectDelay(options, 1) == 1s, "first delay");
Expect(ReconnectDelay(options, 5) == 15s, "delay capped");
Expect(ShouldReconnect(PlaybackError::Timeout), "timeout retries");
Expect(!ShouldReconnect(PlaybackError::Authentication), "auth does not retry");
Expect(RedactMediaLocation("https://u:p@h/x?token=secret") ==
       "https://***:***@h/x?token=***", "credentials redacted");
```

- [ ] **Step 2: Build the test and verify RED**

Run:

```powershell
cmake --build build --config Debug --target network_playback_tests
```

Expected: compilation fails because `media_source.h` and `network_input.h` do not exist.

- [ ] **Step 3: Implement the public value types and pure policy**

Define the exact public enums and structs from the approved design. Expose these pure APIs:

```cpp
enum class MediaSourceKind { LocalFile, Http, Rtsp, Rtp, Udp, OtherNetwork };

struct ValidationResult {
    bool ok{false};
    std::string message;
};

MediaSourceKind ClassifyMediaSource(std::string_view location);
bool IsNetworkSource(MediaSourceKind kind);
bool IsRealtimeSource(MediaSourceKind kind);
ValidationResult ValidateMediaSource(const MediaSource& source);
std::chrono::milliseconds ReconnectDelay(const NetworkOptions& options, int attempt);
bool ShouldReconnect(PlaybackError error);
std::string RedactMediaLocation(std::string_view location);
```

Validation rejects an empty location, negative timeouts/retry counts, non-positive probe size and analyze duration, and a maximum delay smaller than the initial delay. Backoff uses `min(initial * 2^(attempt-1), maximum)` without integer overflow.

- [ ] **Step 4: Register and run the test to verify GREEN**

Add `network_input.cpp` to `play_core`, add a `network_playback_tests` executable linked to `play_core`, and copy runtime DLLs on Windows using the existing test pattern.

Run:

```powershell
cmake --build build --config Debug --target network_playback_tests
build\Debug\network_playback_tests.exe
```

Expected: exit code 0.

- [ ] **Step 5: Commit the policy slice**

```powershell
git add play_core/media_source.h play_core/network_input.h play_core/network_input.cpp tests/network_playback_tests.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "Add network media source policies"
```

### Task 2: FFmpeg Options, Error Mapping And I/O Deadlines

**Files:**
- Modify: `play_core/network_input.h`
- Modify: `play_core/network_input.cpp`
- Modify: `tests/network_playback_tests.cpp`

- [ ] **Step 1: Write failing FFmpeg adapter tests**

Add tests that inspect a real `AVDictionary` with `av_dict_get`:

```cpp
MediaSource rtsp{"rtsp://host/live"};
rtsp.network.rtspTransport = RtspTransport::Udp;
AvDictionary options = BuildInputOptions(rtsp);
Expect(DictionaryValue(options.get(), "rtsp_transport") == "udp", "RTSP transport");
Expect(DictionaryValue(options.get(), "rw_timeout") == "15000000", "read timeout");

MediaSource http{"https://host/vod.mp4"};
http.network.headers["Authorization"] = "Bearer secret";
AvDictionary httpOptions = BuildInputOptions(http);
Expect(DictionaryValue(httpOptions.get(), "reconnect") == "1", "HTTP reconnect");
Expect(DictionaryValue(httpOptions.get(), "reconnect_on_http_error") ==
       "500,502,503,504", "HTTP retry statuses");
```

Also assert `MapAvError(AVERROR(ETIMEDOUT), false) == PlaybackError::Timeout`, `AVERROR_HTTP_UNAUTHORIZED` maps to authentication, EOF on realtime maps to connection lost, and an expired deadline interrupts while a cleared deadline does not.

- [ ] **Step 2: Run and verify RED**

Run the test executable. Expected: compilation fails because the dictionary and I/O APIs are absent.

- [ ] **Step 3: Implement RAII options and per-session I/O control**

Expose:

```cpp
class AvDictionary {
public:
    ~AvDictionary();
    AvDictionary(AvDictionary&&) noexcept;
    AvDictionary& operator=(AvDictionary&&) noexcept;
    AVDictionary* get() const noexcept;
    AVDictionary** put() noexcept;
};

enum class IoOperation { None, Opening, Probing, Reading };

struct IoControl {
    std::atomic_bool cancelled{false};
    std::atomic<std::int64_t> deadlineUs{0};
    std::atomic<IoOperation> operation{IoOperation::None};
    void begin(IoOperation operation, std::chrono::milliseconds timeout);
    void end();
};

AvDictionary BuildInputOptions(const MediaSource& source);
PlaybackError MapAvError(int avError, bool realtime);
int InterruptNetworkIo(void* opaque);
```

Use `av_gettime_relative()` for monotonic deadlines. `InterruptNetworkIo` returns nonzero when cancelled or when a nonzero deadline has expired. Build CRLF-terminated custom headers, skip empty header names, and never log the raw result.

- [ ] **Step 4: Run and verify GREEN**

Run `network_playback_tests.exe`; expected exit code 0.

- [ ] **Step 5: Commit the FFmpeg adapter slice**

```powershell
git add play_core/network_input.h play_core/network_input.cpp tests/network_playback_tests.cpp
git commit -m "Add FFmpeg network input options"
```

### Task 3: Structured Controller And Runtime API

**Files:**
- Modify: `play_core/playback_controller.h`
- Modify: `play_core/playback_controller.cpp`
- Modify: `play_core/playback_controller_videoctl.cpp`
- Modify: `play_core/playback_runtime.h`
- Modify: `play_core/playback_runtime.cpp`
- Modify: `tests/play_core_refactor_tests.cpp`

- [ ] **Step 1: Write failing controller forwarding tests**

Change the test action to `std::function<bool(const MediaSource&)>` and assert both overloads:

```cpp
MediaSource played;
PlaybackController controller({[&](const MediaSource& source) {
    played = source;
    return true;
}, /* existing actions */});

Expect(controller.play("movie.mp4"), "string play accepted");
Expect(played.location == "movie.mp4", "string wrapped as source");

MediaSource source{"rtsp://host/live"};
source.network.maxReconnectAttempts = 3;
Expect(controller.play(source), "structured play accepted");
Expect(played.network.maxReconnectAttempts == 3, "options forwarded");
```

- [ ] **Step 2: Run and verify RED**

Build `play_core_refactor_tests`; expected compilation failure because the structured overload is absent.

- [ ] **Step 3: Implement compatible controller and runtime signals**

Replace only the play action signature:

```cpp
std::function<bool(const MediaSource&)> play;
bool play(const MediaSource& source);
bool play(const std::string& location);
```

The string overload calls `play(MediaSource{location})`. Bind the action to `VideoCtl::StartPlay(const MediaSource&)`. Add and forward:

```cpp
Signal<const PlaybackStatus&> SigPlaybackStatus;
Signal<const MediaInfo&> SigMediaInfo;
```

- [ ] **Step 4: Run and verify GREEN**

Build and execute `play_core_refactor_tests`; expected exit code 0.

- [ ] **Step 5: Commit the API slice**

```powershell
git add play_core/playback_controller.h play_core/playback_controller.cpp play_core/playback_controller_videoctl.cpp play_core/playback_runtime.h play_core/playback_runtime.cpp tests/play_core_refactor_tests.cpp
git commit -m "Expose structured network playback API"
```

### Task 4: Integrate Network State Into VideoCtl

**Files:**
- Modify: `play_core/video_state.h`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `tests/network_playback_tests.cpp`

- [ ] **Step 1: Write failing per-session behavior tests**

Add testable helper assertions for `BuildMediaInfo()` and `CanSeek()`:

```cpp
Expect(BuildMediaInfo(MediaSource{"udp://host:9000"}, nullptr).live, "UDP is live");
Expect(!BuildMediaInfo(MediaSource{"udp://host:9000"}, nullptr).seekable, "live not seekable");
Expect(!CanSeek(MediaInfo{true, true, false, std::nullopt}), "seek rejected");
```

Add a session-buffer assertion showing a realtime source selects unlimited read mode while a subsequent local source selects normal queue limiting.

- [ ] **Step 2: Run and verify RED**

Run `network_playback_tests.exe`; expected compilation failure for the media-info helpers.

- [ ] **Step 3: Add per-session state and input opening**

Extend `SessionState` with:

```cpp
MediaSource source;
MediaInfo mediaInfo;
IoControl io;
std::atomic<int> readResult{0};
std::atomic<PlaybackError> readError{PlaybackError::None};
bool unlimitedBuffer{false};
```

Pass `MediaSource` into `stream_open`. In `ReadThread`, build options, install `InterruptNetworkIo`, set opening/probing/reading deadlines around blocking FFmpeg calls, classify failures, and store the outcome. Set `unlimitedBuffer` from the opened format and source kind instead of using the global `infinite_buffer`.

- [ ] **Step 4: Implement observable playback and safe seek**

Add `VideoCtl::StartPlay(const MediaSource&)` and keep the string overload as a wrapper. Emit `Opening`, `Playing`, `Buffering`, `Reconnecting`, `Failed`, and `Stopped` through a single `publishStatus` helper. Emit `MediaInfo` after probing. Every seek method must require a non-null format context and `mediaInfo.seekable`.

On a retryable read failure, the playback worker must:

1. Capture the last finite master-clock position for seekable HTTP input.
2. Release and fully close the old `MediaSession`.
3. Publish `Reconnecting` with attempt and delay.
4. Wait on a condition variable that stop/new-play can wake.
5. Create a fresh `VideoState` and reopen the same `MediaSource`.
6. Best-effort seek the reopened HTTP input to the captured position.

Stop sets both the loop flag and session cancel flag, signals SDL and retry condition variables, joins the worker, then publishes `Stopped` once.

- [ ] **Step 5: Run focused tests and existing core tests**

Run:

```powershell
build\Debug\network_playback_tests.exe
build\Debug\play_core_refactor_tests.exe
```

Expected: both exit code 0.

- [ ] **Step 6: Commit the playback integration slice**

```powershell
git add play_core/video_state.h play_core/videoctl.h play_core/videoctl.cpp tests/network_playback_tests.cpp
git commit -m "Integrate reconnecting network sessions"
```

### Task 5: Qt Runtime Bridge

**Files:**
- Modify: `apps/qt_player/playback_runtime_bridge.h`
- Modify: `apps/qt_player/playback_runtime_bridge.cpp`

- [ ] **Step 1: Add bridge signal declarations and compile to verify RED**

Declare:

```cpp
void SigPlaybackStatus(PlaybackStatus status);
void SigMediaInfo(MediaInfo info);
```

Build `playerdemo`; expected compilation failure until the types are included and runtime connections exist.

- [ ] **Step 2: Implement queued copies**

Include `media_source.h`, connect both runtime signals, capture each value by copy, and emit it from `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Do not access QWidget or retain references from the core callback.

- [ ] **Step 3: Build the Qt application**

Run:

```powershell
cmake --build build --config Debug --target myplayer
```

Expected: the target builds.

- [ ] **Step 4: Commit the bridge slice**

```powershell
git add apps/qt_player/playback_runtime_bridge.h apps/qt_player/playback_runtime_bridge.cpp
git commit -m "Bridge network playback status to Qt"
```

### Task 6: Full Verification And Documentation Alignment

**Files:**
- Modify: `README.md` only if its existing usage section needs the new core API example.
- Modify: `docs/PROJECT_GUIDE.md` only if it is tracked before this task; do not stage unrelated untracked documentation.

- [ ] **Step 1: Run formatting and diff checks**

```powershell
git diff --check
```

Expected: no whitespace errors in changed files.

- [ ] **Step 2: Build all targets**

```powershell
cmake --build build --config Debug
```

Expected: successful build with no new compiler errors.

- [ ] **Step 3: Run all automated tests**

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Expected: all registered tests pass, including `network_playback_tests`.

- [ ] **Step 4: Review sensitive-data and lifecycle paths**

Search changed code and verify that raw URLs are not included in new `av_log`, `SigPlayMsg`, or `PlaybackStatus::message` calls; verify every `AVDictionary` is RAII-owned and every stop path wakes blocking I/O and retry waits.

- [ ] **Step 5: Commit any final tracked documentation update**

```powershell
git add README.md
git commit -m "Document network playback configuration"
```

Skip this commit when no tracked documentation change is needed.

