# VideoCtl Decomposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `VideoCtl` into focused reconnect, filter, decoder, demux, and audio components while preserving its public API and playback behavior.

**Architecture:** Keep `VideoCtl` as the facade and lifecycle coordinator. Move one cohesive responsibility at a time into a Qt-free `play_core` class, characterize the existing behavior before each move, and run the complete suite after every task. Ownership stays with `MediaSession` and worker lifetimes remain subordinate to the current `VideoState`.

**Tech Stack:** C++20, CMake 3.25+, Qt 6, FFmpeg, SDL2, SoundTouch, the repository's lightweight `Expect` test harness with CTest, and vcpkg.

**Spec:** `docs/superpowers/specs/2026-09-17-videoctl-decomposition-design.md`

## Global Constraints

- Work from the first unchecked task. Do not repeat completed tasks.
- Before editing, run `git status --short` and preserve unrelated user changes.
- Do not change public `VideoCtl` method names, parameters, signals, or status ordering.
- Keep every new `play_core` component free of Qt includes and Qt types.
- Move existing bodies first; do not combine extraction with behavior changes.
- Keep `MediaSession` as the sole owner of `VideoState`.
- Add source/test files to the nearest existing `CMakeLists.txt`; do not create a second build system.
- Use the current configured `build` directory. On this machine its vcpkg installed tree may be outside the repository; do not copy dependencies back into the source tree.
- Finish each task with a focused test, the full Debug build, all CTest tests, self-review, and one commit.
- If a characterization test exposes an existing defect, record it in the plan and fix it in a separate commit before continuing the extraction.

## Current Baseline

- [x] Commit `5a47e57` extracted `VideoFrameConverter` from `VideoCtl`.
- [x] `VideoState` no longer owns `img_convert_ctx`.
- [x] Debug build succeeded and CTest passed 5/5 after that extraction.
- [ ] Confirm the working tree and baseline before continuing:

```powershell
git status --short
git log -1 --oneline
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Expected: the tree is clean or contains only understood user changes, HEAD includes `5a47e57`, the build succeeds, and all tests pass.

---

## Task 1: Extract Reconnect Cancellation and Retry Timing

**Files:**

- Create: `play_core/reconnect_controller.h`
- Create: `play_core/reconnect_controller.cpp`
- Create: `tests/reconnect_controller_tests.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] Write focused tests before implementation.

Cover these cases:

1. A reset controller permits a zero-delay wait.
2. A cancelled controller rejects a wait.
3. Cancelling from another thread wakes a long wait promptly.
4. Retry is allowed only when the error is retryable, attempts are below `NetworkOptions::maxReconnectAttempts`, the stream is not aborted, and the controller is not cancelled.
5. Local file sources remain governed by the existing `ShouldReconnect` result; do not create a second network-policy implementation.

Use a synchronization barrier so the wake-up test does not depend on a blind sleep:

```cpp
std::promise<void> entered;
std::future<void> enteredFuture = entered.get_future();
std::atomic_bool waitResult{true};

std::thread waiter([&] {
    entered.set_value();
    waitResult = controller.wait(std::chrono::seconds(5));
});
enteredFuture.wait();
controller.cancel();
waiter.join();
if (!Expect(!waitResult.load(), "cancel wakes reconnect wait"))
    return 1;
```

- [ ] Run the new test target and verify that it fails because `ReconnectController` does not exist.

- [ ] Implement this interface:

```cpp
class ReconnectController final {
public:
    ReconnectController() = default;

    void reset();
    void cancel();
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] bool wait(std::chrono::milliseconds delay);
    [[nodiscard]] bool canRetry(const MediaSource& source,
                                PlaybackError error,
                                int completedAttempts,
                                bool streamAborted) const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic_bool m_cancelled{true};
};
```

`wait()` returns `true` only when the delay expires without cancellation. `cancel()` stores `true` while holding `m_mutex`, then calls `notify_all()`. `reset()` stores `false` while holding the same mutex. `canRetry()` delegates error policy to `ShouldReconnect(error)` and reads reconnect enablement and the attempt limit from `source.network`.

- [ ] Replace these `VideoCtl` members with `ReconnectController m_reconnectController`:

```cpp
std::mutex m_reconnectMutex;
std::condition_variable m_reconnectCv;
std::atomic_bool m_reconnectCancelled;
```

- [ ] Update `LoopThread`, `requestStop`, `StartPlay`, and the destructor to use `reset()`, `cancel()`, `canRetry()`, and `wait()`. Preserve the current retry counter, `Reconnecting` status emission, reopened-session cleanup, and `m_bPlayLoop` semantics.

- [ ] Run focused and full verification:

```powershell
cmake --build build --config Debug --target reconnect_controller_tests
ctest --test-dir build -C Debug -R reconnect_controller --output-on-failure
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- [ ] Review `git diff --check` and `git diff`. Confirm that reconnect policy is implemented in one place and no facade lock moved into the component.

- [ ] Commit:

```powershell
git add play_core/reconnect_controller.h play_core/reconnect_controller.cpp tests/reconnect_controller_tests.cpp play_core/videoctl.h play_core/videoctl.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "refactor: extract reconnect controller"
```

---

## Task 2: Extract FFmpeg Filter Configuration

**Files:**

- Create: `play_core/filter_configurator.h`
- Create: `play_core/filter_configurator.cpp`
- Create: `tests/filter_configurator_tests.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] Add a smoke test for the generic graph connector before moving code. Under `CONFIG_AVFILTER`, allocate a graph with a two-by-two `buffer` source and `buffersink`, call the new `ConfigureFilterGraph`, and assert success. Free the graph on every path. When `CONFIG_AVFILTER` is disabled, compile an interface test instead of silently skipping the target.

The public functions must have these signatures:

```cpp
int ConfigureFilterGraph(AVFilterGraph* graph,
                         const char* filtergraph,
                         AVFilterContext* sourceCtx,
                         AVFilterContext* sinkCtx);

int ConfigureVideoFilters(AVFilterGraph* graph,
                          VideoState* state,
                          const char* filters,
                          AVFrame* frame,
                          bool autorotate);

int ConfigureAudioFilters(VideoState* state,
                          const char* filters,
                          bool forceOutputFormat);
```

- [ ] Build the test and verify failure because the new header/functions do not exist.

- [ ] Move the bodies of these `VideoCtl` methods unchanged into `filter_configurator.cpp`:

```text
configure_filtergraph
configure_video_filters
configure_audio_filters
```

Convert `m_bAutorotate` access into the explicit `autorotate` argument. Keep all existing `CONFIG_AVFILTER` guards and error returns.

- [ ] Remove the three declarations from `videoctl.h`. Update decoder call sites to call the free functions. Do not change filter strings, graph ordering, autorotation rules, output formats, or logging.

- [ ] Run verification:

```powershell
cmake --build build --config Debug --target filter_configurator_tests
ctest --test-dir build -C Debug -R filter_configurator --output-on-failure
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- [ ] Review `git diff --check` and confirm `videoctl.cpp` contains no filter graph construction helpers.

- [ ] Commit:

```powershell
git add play_core/filter_configurator.h play_core/filter_configurator.cpp tests/filter_configurator_tests.cpp play_core/videoctl.h play_core/videoctl.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "refactor: extract filter configurator"
```

---

## Task 3: Extract Decoder Worker Loops

**Files:**

- Create: `play_core/decoder_workers.h`
- Create: `play_core/decoder_workers.cpp`
- Create: `tests/decoder_workers_tests.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] Write characterization tests for the two deterministic boundaries before moving them:

1. `GetVideoFrame` returns the existing result for an aborted/empty video decoder queue.
2. `QueuePicture` returns the existing abort result when the picture queue is aborted.
3. The three worker entry points have the exact `int(void*)` shape required by the current thread creation sites.

Initialize and destroy packet/frame queues with the same helpers used by production code. Do not duplicate queue internals in the test.

- [ ] Verify that the test fails because `DecoderWorkers` is absent.

- [ ] Implement this stateless interface:

```cpp
class DecoderWorkers final {
public:
    DecoderWorkers() = delete;

    static int Audio(void* opaque);
    static int Video(void* opaque);
    static int Subtitle(void* opaque);

    static int GetVideoFrame(VideoState* state, AVFrame* frame);
    static int QueuePicture(VideoState* state,
                            AVFrame* sourceFrame,
                            double pts,
                            double duration,
                            int64_t pos,
                            int serial);
};
```

- [ ] Move these bodies from `VideoCtl` without changing their control flow:

```text
get_video_frame
queue_picture
audio_thread
video_thread
subtitle_thread
```

Calls to filter setup must use `FilterConfigurator` from Task 2. Calls to `MediaSync`, queues, clocks, and FFmpeg remain direct because those are already `VideoState` dependencies.

- [ ] Update decoder thread construction in `stream_component_open` to use `DecoderWorkers::Audio`, `DecoderWorkers::Video`, and `DecoderWorkers::Subtitle`. Remove the five declarations from `videoctl.h`.

- [ ] Run focused and full verification:

```powershell
cmake --build build --config Debug --target decoder_workers_tests
ctest --test-dir build -C Debug -R decoder_workers --output-on-failure
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- [ ] Review thread captures and lifetime. Each worker must still receive only the session-owned `VideoState*`; no worker may capture `VideoCtl`.

- [ ] Commit:

```powershell
git add play_core/decoder_workers.h play_core/decoder_workers.cpp tests/decoder_workers_tests.cpp play_core/videoctl.h play_core/videoctl.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "refactor: extract decoder workers"
```

---

## Task 4: Extract the Demux and Packet-Read Loop

**Files:**

- Create: `play_core/stream_reader.h`
- Create: `play_core/stream_reader.cpp`
- Create: `tests/stream_reader_tests.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] Add characterization tests for the helpers that currently live beside `ReadThread`:

1. `HasEnoughPackets` returns true for an absent stream, an aborted queue, and a queue above the current packet/duration thresholds.
2. `IsRealtime` recognizes the same RTSP/RTP/SDP conditions as the current implementation.
3. A read failure is reported through the callback with the same `PlaybackError` mapping.

Use a callback recorder with plain C++ fields; do not bring Qt into the test.

- [ ] Verify the tests fail because `StreamReader` is absent.

- [ ] Add this callback boundary:

```cpp
struct StreamReaderCallbacks {
    std::function<int(VideoState*, int)> openComponent;
    std::function<void(const PlaybackStatus&)> publishStatus;
    std::function<void(int)> publishDurationSeconds;
    std::function<void(const MediaInfo&)> publishMediaInfo;
    std::function<void()> stopRefreshLoop;
    std::function<void()> handleEndOfMedia;
};

class StreamReader final {
public:
    explicit StreamReader(StreamReaderCallbacks callbacks);
    void Run(VideoState* state);

    static bool HasEnoughPackets(AVStream* stream,
                                 int streamId,
                                 PacketQueue* queue);
    static bool IsRealtime(const AVFormatContext* context);

private:
    StreamReaderCallbacks m_callbacks;
};
```

The callback object is immutable after construction. Validate required callbacks in the constructor and fail fast with `std::invalid_argument` if one is empty.

- [ ] Move the full bodies of these methods into `stream_reader.cpp`:

```text
stream_has_enough_packets
is_realtime
ReadThread
```

Translate existing signal emissions to `publishStatus`, `publishDurationSeconds`, and `publishMediaInfo`. Translate the current `m_bPlayLoop = false` failure action to `stopRefreshLoop`. Translate loop-policy signal dispatch to `handleEndOfMedia`; keep loop-policy selection in `VideoCtl` so UI policy does not enter `StreamReader`.

- [ ] Add `StreamReader m_streamReader` to `VideoCtl`. Construct it with lambdas that call `stream_component_open`, emit the existing signals, stop the refresh loop, and execute the current loop-policy branch. Start the existing read thread with `m_streamReader.Run(is)`.

- [ ] Remove `ReadThread`, `stream_has_enough_packets`, and `is_realtime` from `VideoCtl`. Confirm the moved implementation still uses `RedactMediaLocation`, `BuildInputOptions`, and `IoControl` from the network input module.

- [ ] Run verification:

```powershell
cmake --build build --config Debug --target stream_reader_tests
ctest --test-dir build -C Debug -R stream_reader --output-on-failure
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- [ ] Review EOF and error branches line by line against the pre-extraction version with `git show HEAD^:videoctl.cpp`. Confirm every queue signal, status callback, and abort check still exists.

- [ ] Commit:

```powershell
git add play_core/stream_reader.h play_core/stream_reader.cpp tests/stream_reader_tests.cpp play_core/videoctl.h play_core/videoctl.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "refactor: extract stream reader"
```

---

## Task 5: Extract SDL Audio Output

**Files:**

- Create: `play_core/audio_output.h`
- Create: `play_core/audio_output.cpp`
- Create: `tests/audio_output_tests.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] Characterize audio synchronization before moving it. Construct a minimal `VideoState` and cover:

1. Master-audio mode keeps the requested sample count unchanged.
2. An invalid audio clock keeps the requested sample count unchanged.
3. A bounded positive and negative A/V difference changes the sample count but clamps it to the existing correction percentage.
4. A default-constructed output reports no open SDL device and can be closed safely twice.

- [ ] Verify the tests fail because `AudioOutput` is absent.

- [ ] Implement this interface:

```cpp
class AudioOutput final {
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    int Open(VideoState* state,
             AVChannelLayout* wantedChannelLayout,
             int wantedSampleRate,
             AudioParams* hardwareParams);
    void Pause(bool paused);
    void Close();
    [[nodiscard]] SDL_AudioDeviceID DeviceId() const noexcept;

    static int SynchronizeSamples(VideoState* state, int sampleCount);
    static int DecodeFrame(VideoState* state);

private:
    static void Callback(void* opaque, Uint8* stream, int length);
    static void UpdateSampleDisplay(VideoState* state,
                                    const int16_t* samples,
                                    int sampleCount);

    SDL_AudioDeviceID m_device{0};
};
```

`Close()` must be idempotent: if `m_device != 0`, close it and then set it to zero. The callback opaque pointer remains `VideoState*`.

- [ ] Move these implementations from `videoctl.cpp` without changing formulas, buffer sizes, SoundTouch behavior, or clock updates:

```text
audio_callback
update_sample_display
synchronize_audio
audio_decode_frame
audio_open
```

- [ ] Replace `m_sdlAudio_dev` with `AudioOutput m_audioOutput`. Update `stream_component_open`, `stream_component_close`, pause/resume, stop, and destruction paths to call the component. Keep `VideoState` audio buffers and SoundTouch state in `VideoState` for this extraction; moving those resources would mix ownership changes with code movement.

- [ ] Run verification. If CI or the machine has no physical audio device, set SDL's dummy driver only for the focused test process; production code must not set it.

```powershell
cmake --build build --config Debug --target audio_output_tests
$env:SDL_AUDIODRIVER = 'dummy'
ctest --test-dir build -C Debug -R audio_output --output-on-failure
Remove-Item Env:SDL_AUDIODRIVER
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- [ ] Review teardown order. The audio device must be closed before audio buffers, SoundTouch state, queues, or `VideoState` are destroyed.

- [ ] Commit:

```powershell
git add play_core/audio_output.h play_core/audio_output.cpp tests/audio_output_tests.cpp play_core/videoctl.h play_core/videoctl.cpp play_core/CMakeLists.txt CMakeLists.txt
git commit -m "refactor: extract audio output"
```

---

## Task 6: Reduce `VideoCtl` to the Facade and Verify the Result

**Files:**

- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `tests/play_core_refactor_tests.cpp`
- Modify: `docs/superpowers/specs/2026-09-17-videoctl-decomposition-design.md` only if the implemented ownership differs from the approved design

- [ ] Remove dead includes, obsolete forward declarations, duplicate helpers, and declarations left behind by Tasks 1–5. Do not rename public methods during cleanup.

- [ ] Add compile-time facade checks to `play_core_refactor_tests.cpp` for the existing public methods used by the application: `StartPlay`, `OnStop`, `OnStopAndWait`, seek, pause, volume, speed, and track switching. These checks protect signatures without coupling tests to Qt signal internals.

- [ ] Inspect `VideoCtl` and confirm its remaining private responsibilities are limited to:

```text
public playback command handling
session install/remove and component open/close
play-loop orchestration and reconnect status publication
renderer dispatch and VideoFrameConverter use
translation of component callbacks into existing signals
```

- [ ] Run formatting only with the repository's existing formatter/configuration. Do not introduce a new style tool.

- [ ] Run final verification:

```powershell
git diff --check
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
git status --short
```

- [ ] Perform a manual smoke run when the sample media used by the project is available. Exercise: open, pause/resume, seek, speed change, audio/video/subtitle track cycling, stop, replay, and reconnect cancellation during stop. Record the media path and observed result in the commit message body without committing media files.

- [ ] Review the complete range from the baseline:

```powershell
git diff 5a47e57..HEAD --stat
git diff 5a47e57..HEAD -- play_core/videoctl.h play_core/videoctl.cpp
```

Confirm that no `VideoState` ownership changed, every worker is joined before state destruction, and the public API stayed stable.

- [ ] Commit the final cleanup and contract test:

```powershell
git add play_core/videoctl.h play_core/videoctl.cpp tests/play_core_refactor_tests.cpp docs/superpowers/specs/2026-09-17-videoctl-decomposition-design.md
git commit -m "refactor: finish VideoCtl decomposition"
```

## New Session Handoff Prompt

Copy the following into a new session:

```text
在仓库 D:\Programme\project\C++\playerdemo-master 中继续执行 VideoCtl 拆分。

先阅读：
1. docs/superpowers/specs/2026-09-17-videoctl-decomposition-design.md
2. docs/superpowers/plans/2026-09-17-videoctl-decomposition.md

使用 superpowers:executing-plans（如果适合并行且任务互不影响，可使用 superpowers:subagent-driven-development），从计划里第一个未勾选项开始。先检查 git status 和现有提交，不要重复已经完成的任务，也不要覆盖无关改动。严格按任务执行测试、全量 Debug 构建、CTest、自审和分任务提交。遇到现有行为与计划假设不一致时，先用测试固定实际行为，再做最小调整，并把原因记录到计划中。持续执行到当前任务完成；若验证通过则继续下一任务。
```
