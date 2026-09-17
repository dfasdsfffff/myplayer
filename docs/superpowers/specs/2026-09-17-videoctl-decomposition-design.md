# VideoCtl Decomposition Design

**Date:** 2026-09-17

## Context

`VideoCtl` currently combines playback orchestration, reconnect timing, FFmpeg filter construction, demuxing, decoder worker loops, SDL audio output, session ownership, and the public playback API. The first extraction is already complete at commit `5a47e57`: video frame conversion now lives in `VideoFrameConverter`, and `VideoState` no longer owns `img_convert_ctx`.

The remaining work should proceed as a sequence of behavior-preserving extractions. Each extraction must compile and pass the full test suite before the next one starts. Public behavior and Qt-facing APIs must remain stable throughout the work.

## Goals

- Make `VideoCtl` a playback facade and lifecycle coordinator.
- Give reconnect, filtering, demuxing, decoding, and audio output one explicit owner each.
- Preserve playback, seek, pause, speed, track switching, reconnect, loop policy, and shutdown behavior.
- Keep the core classes independent from Qt.
- Make cancellation and ownership visible in class interfaces instead of relying on scattered fields.

## Non-goals

- Rewriting the player architecture.
- Changing FFmpeg, SDL2, SoundTouch, or Qt versions.
- Adding hardware decoding, new pixel formats, or new media features.
- Refactoring `MainWid`; it needs a separate plan after this work is stable.
- Changing public `VideoCtl` methods or signal names.

## Target Structure

### `VideoCtl`

Remains the public facade. It owns the extracted components, translates their callbacks into existing signals, serializes `StartPlay` and stop operations, and swaps the current `MediaSession` under `m_streamMutex`.

### `ReconnectController`

Owns reconnect cancellation, interruptible waits, and retry eligibility. This removes `m_reconnectMutex`, `m_reconnectCv`, and `m_reconnectCancelled` from `VideoCtl`.

### `FilterConfigurator`

Owns FFmpeg filter graph construction. It receives all configuration as arguments, including autorotation and forced output format. It keeps no mutable playback state.

### `DecoderWorkers`

Owns the audio, video, and subtitle decoder loops plus decoded-video queueing. Its entry points are static because all required runtime state is already in `VideoState`.

### `StreamReader`

Owns input probing, stream discovery, packet reads, EOF handling, queue throttling, and dispatch to packet queues. A narrow callback object lets `VideoCtl` open selected stream components and publish the existing status/media signals.

### `AudioOutput`

Owns the SDL audio device and callback path, audio sample synchronization, frame conversion, and SoundTouch output. `VideoCtl` only asks it to open, pause/resume, or close the device.

### `VideoFrameConverter`

Already implemented. It owns software pixel conversion and cached conversion resources.

## Dependency Direction

```text
Qt facade/signals
      |
   VideoCtl
      |-- ReconnectController
      |-- StreamReader --------> VideoState / FFmpeg
      |-- DecoderWorkers ------> FilterConfigurator
      |-- AudioOutput ---------> VideoState / SDL / SoundTouch
      `-- VideoFrameConverter -> FFmpeg swscale
```

Extracted classes must not call Qt code and must not know about widgets. `VideoCtl` is the only class in this group that emits application signals.

## Threading and Ownership Invariants

- `m_playbackMutex` continues to serialize start, stop, and destruction.
- `m_streamMutex` continues to protect installation and removal of the current session.
- Decoder and reader threads receive a `VideoState*` whose lifetime is owned by `MediaSession`; all worker threads are joined before session destruction.
- `requestStop()` must cancel reconnect waiting before joining the play loop.
- `ReconnectController::cancel()` must wake a waiting reconnect immediately.
- The SDL audio callback must never reference `VideoCtl`; its opaque pointer remains the live `VideoState*`.
- A component extraction must not add a second owner for an FFmpeg, SDL, thread, queue, or frame resource.
- Existing lock ordering must remain: `m_playbackMutex` before session changes, then `m_streamMutex` only for the shortest required interval. No extracted component may acquire these facade locks.

## Error and Status Invariants

- Existing `PlaybackStatus` values, error mapping, and signal order remain unchanged.
- Media locations emitted to logs/status remain redacted through `RedactMediaLocation`.
- Retry count and delay continue to use `NetworkOptions`, `ShouldReconnect`, and `ReconnectDelay`.
- User stop, stream abort, and reconnect cancellation never trigger another open attempt.
- A failed reopened stream is closed before the next attempt.

## Verification Strategy

Every extraction starts with a characterization test for the boundary being moved. Pure policy and lifecycle components receive focused unit tests. FFmpeg/SDL integration keeps the existing full CTest suite and adds small deterministic fixtures where practical. Each task ends with a Debug build and all CTest tests passing.

The final acceptance criteria are:

- `VideoCtl` retains the existing public interface.
- Reconnect state, filter construction, decoder worker loops, demux/read loop, and SDL audio callback are absent from `videoctl.cpp`.
- No duplicate implementations remain after moves.
- `cmake --build build --config Debug` succeeds.
- `ctest --test-dir build -C Debug --output-on-failure` passes.
- Start/stop/reconnect paths pass ThreadSanitizer where a supported toolchain is available; this is an additional check, not a Windows build gate.

