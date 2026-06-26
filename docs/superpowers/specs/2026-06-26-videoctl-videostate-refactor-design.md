# VideoCtl and VideoState Internal Refactor Design

## Context

The public playback surface is already isolated behind `PlaybackRuntime`, `PlaybackController`, and `MediaSession`. The remaining architectural problem is inside `play_core`: `VideoCtl` still owns too many responsibilities, and `VideoState` is a shared structure that mixes session state, media stream state, queues, clocks, filters, buffers, thread state, and resource cleanup.

This refactor should not change the public playback API. It should reduce internal coupling in small, verifiable steps while preserving playback behavior.

## Goals

- Keep `PlaybackRuntime` and `PlaybackController` stable for UI and application code.
- Keep `VideoCtl` as the internal facade during the transition.
- Split `VideoState` into responsibility-focused state groups.
- Move low-risk synchronization algorithms out of `VideoCtl` first.
- Move resource cleanup into the state objects that own those resources.
- Defer thread and pipeline ownership changes until the state and algorithm boundaries are clear.

## Non-Goals

- Do not rewrite the playback engine in one pass.
- Do not change Qt or ImGui integration points.
- Do not replace FFmpeg, SDL, SoundTouch, or the existing queue implementations.
- Do not change user-visible playback behavior as part of the structural work.

## Target Shape

`VideoCtl` remains the internal facade. Its long-term responsibilities are limited to command handling, session ownership, signal wiring, renderer dispatching, and top-level playback lifecycle coordination.

The current monolithic `VideoState` is split into smaller internal state groups:

- `SessionState`: filename, format context, read thread coordination, abort, pause, seek, eof, realtime, and stream selection defaults.
- `MediaClockState`: audio, video, and external clocks plus sync policy.
- `AudioState`: audio stream, decoder, packet/frame queues, SDL audio buffer state, resampling, SoundTouch state, volume, sample display data, and audio filter source params.
- `VideoTrackState`: video stream, decoder, packet/frame queues, frame timing, video conversion contexts, frame drop counters, and video output geometry.
- `SubtitleState`: subtitle stream, decoder, packet/frame queues, and subtitle conversion context.
- `FilterState`: audio/video filter contexts and filter graphs.

The first transition may keep the name `VideoState` as an aggregate:

```cpp
struct VideoState {
    SessionState session;
    MediaClockState clocks;
    AudioState audio;
    VideoTrackState video;
    SubtitleState subtitle;
    FilterState filters;
};
```

This keeps existing call sites easy to migrate incrementally. A later change can rename the aggregate to `PlaybackSessionState` or remove it after pipeline ownership is introduced.

## Component Responsibilities

`PlaybackSession` should eventually own one active playback session and its state. It becomes the semantic replacement for passing raw `VideoState*` through all internal functions.

`AudioPipeline` should own audio decoding, audio callbacks, resampling, SoundTouch integration, audio synchronization, and audio resource cleanup.

`VideoPipeline` should own video decoding, video frame queuing, frame timing, refresh calculation, and rendered frame dispatch preparation.

`SubtitlePipeline` should own subtitle decoding and subtitle queue lifecycle.

`MediaSync` or `MediaClockController` should own synchronization calculations currently implemented as static or near-static functions in `VideoCtl`.

## Migration Plan

### Phase 1: State Groups

Introduce the grouped state structs and move fields out of the flat `VideoState` layout. Preserve behavior and keep existing functions in `VideoCtl`. Update call sites mechanically from `is->field` to `is->group.field`.

This phase intentionally creates a visible boundary before moving behavior.

### Phase 2: Synchronization Extraction

Move these functions out of `VideoCtl`:

- `get_master_sync_type`
- `get_master_clock`
- `check_external_clock_speed`
- `compute_target_delay`
- `vp_duration`
- `update_video_pts`

The new module should depend on the grouped state it needs, not on `VideoCtl`. Add focused tests for master clock selection and target delay behavior where practical.

### Phase 3: Resource Ownership

Move cleanup from `VideoState::~VideoState()` into the destructors of the specific state groups:

- `SessionState` closes `AVFormatContext` and owns read-thread coordination primitives.
- `AudioState` releases SoundTouch, audio buffers, RDFT data, and resampler context.
- `VideoTrackState` releases image conversion context.
- `SubtitleState` releases subtitle conversion context.
- `FilterState` releases filter graphs and contexts when ownership is explicit.

The aggregate destructor should become simple and should not know about every subsystem resource.

### Phase 4: Pipeline Ownership

After state and cleanup are separated, move behavior into pipelines:

- `ReadThread` becomes session or demux reader behavior.
- `audio_thread`, `audio_decode_frame`, `synchronize_audio`, and audio callback helpers move toward `AudioPipeline`.
- `video_thread`, `get_video_frame`, `queue_picture`, and refresh helpers move toward `VideoPipeline`.
- `subtitle_thread` moves toward `SubtitlePipeline`.

At this point `VideoCtl` should mostly delegate to `PlaybackSession` and pipelines.

## Error Handling And Lifecycle

Stop and teardown must remain deterministic. Each phase should preserve these rules:

- Stop requests set abort flags before joining threads.
- SDL condition variables are signaled before waiting for read thread exit.
- Queue and decoder abort behavior remains unchanged during early phases.
- Resource ownership moves only when the corresponding destructor or owner is covered by tests or build verification.

## Testing Strategy

Keep existing controller and session tests passing. Add focused tests as boundaries become testable:

- `MediaSync` tests for master clock selection and delay calculation.
- State destructor tests where resource ownership can be checked without live FFmpeg or SDL playback.
- Compile-time or smoke tests confirming `PlaybackRuntime` and `PlaybackController` do not expose `VideoCtl`.

Each phase should build independently. Avoid large patches that mix state movement, behavior changes, and thread ownership changes.

## First Implementation Slice

The first implementation slice should include only:

1. Add responsibility-focused state structs.
2. Convert `VideoState` into an aggregate of those structs.
3. Update `VideoCtl` call sites mechanically.
4. Extract the synchronization helpers into a dedicated module if the state grouping compiles cleanly.
5. Run the existing play core refactor tests and build verification.

This slice should not change pipeline ownership or thread entry points.
