# MyPlayer Comprehensive Optimization Design

**Date:** 2026-09-18

## Context

MyPlayer is a Windows-first C++20 desktop media player built with Qt 6 Widgets, FFmpeg, SDL2, and SoundTouch. The playback core has recently been decomposed into focused components (`StreamReader`, `DecoderWorkers`, `AudioOutput`, `ReconnectController`, `VideoFrameConverter`, and related helpers), and the current Debug build and ten registered CTest tests pass.

The remaining work is not another large rewrite. It is a staged program that closes incomplete user-facing behavior, removes known concurrency risks, improves media correctness, bounds frame/audio work, and makes releases reproducible. Each stage must leave the player working and independently releasable.

## Goals

- Give users actionable playback, buffering, reconnect, and failure feedback.
- Make settings, playlist navigation, resume behavior, menus, and supported media filters consistent.
- Render embedded and external subtitles and expose explicit audio/subtitle track selection.
- Remove C++ data races around clocks, pause/seek, and track switching.
- Bound cross-thread frame delivery and reduce avoidable CPU conversion and memory growth.
- Move expensive audio preparation out of the SDL real-time callback.
- Add optional Windows hardware decoding with automatic software fallback.
- Establish CI, deterministic integration fixtures, warning/sanitizer gates, packaging verification, and accurate documentation.

## Non-goals

- Replacing Qt Widgets, FFmpeg, SDL2, SoundTouch, CMake, or vcpkg.
- Rewriting `VideoCtl` or changing all public APIs in one step.
- Supporting DRM-protected media, Blu-ray/DVD navigation, capture devices, television tuners, or streaming-server features.
- Adding cloud accounts, telemetry, update services, or media-library scraping.
- Implementing zero-copy GPU presentation in the first hardware-decoding iteration.
- Preserving disabled placeholder menu items that do not map to implemented behavior.

## Delivery Stages

### Stage 0: Engineering Baseline

Introduce standard build switches, focused test registration, warning configuration, and Windows CI. This stage changes no playback behavior. It gives every later session a repeatable verification path.

### Stage 1: Functional Closure

Connect playback status and media information to the UI, implement real settings, migrate configuration storage, protect sensitive network locations, centralize supported media formats, repair playlist navigation, and remove dead menu/full-screen behavior.

### Stage 2: Concurrency and Lifecycle Safety

UI threads stop mutating live playback state directly. A command mailbox transfers pause, seek, and track-selection requests to their owning playback threads. Clock state receives a coherent lock-free snapshot representation, and track replacement is synchronized against packet routing and frame refresh.

### Stage 3: Media Correctness and Capability

Expand `MediaInfo`, add explicit track menus, render embedded and external subtitles, preserve sample aspect ratio/rotation/color metadata, and make resume completion-aware. This stage prioritizes correct software playback over acceleration.

### Stage 4: Performance

Coalesce video delivery to the newest pending frame, introduce an SDL YUV texture path with BGRA fallback, move resampling/SoundTouch preparation out of the SDL callback, and add opt-in D3D11VA decoding with software fallback. Every optimization requires a before/after measurement and must not weaken correctness.

### Stage 5: Release Quality

Add deterministic media fixtures and lifecycle stress tests, broaden CI and sanitizer coverage, make install/package outputs self-contained, update QA documentation, and run the release checklist on a clean machine.

## Target Architecture

```text
Qt Widgets
  MainWid / SettingWid / Playlist / Show
          |
  PlaybackRuntimeBridge
          |-- PlaybackStatusPresenter
          |-- LatestVideoFrameMailbox
          |-- Subtitle presentation events
          v
  PlaybackController
          v
  VideoCtl (facade and session lifecycle)
          |-- PlaybackCommandMailbox
          |-- StreamReader
          |-- DecoderWorkers
          |-- AudioOutput + AudioRenderQueue
          |-- SubtitleDispatcher
          |-- VideoFrameConverter
          `-- optional HardwareDecodeContext
```

`play_core` remains free of Qt. UI-specific presentation and settings widgets remain under `apps/qt_player`. Data-only contracts such as `PlaybackStatus`, `MediaInfo`, `TrackInfo`, `VideoFrame`, and `SubtitleFrame` live in `play_core`.

## Threading and Ownership Rules

- `MediaSession` remains the sole owner of the active `VideoState`.
- `m_playbackMutex` serializes start, stop-and-wait, and destruction.
- The Qt thread may post commands and update atomic scalar preferences; it may not directly mutate stream pointers, queues, clocks, or decoder lifecycle.
- The read thread owns seek execution and packet routing.
- The playback loop owns pause-clock transitions and track replacement. Packet routing takes a shared track lock; replacement takes the exclusive lock.
- `Clock` publishes and reads coherent snapshots; no plain clock field is concurrently accessed.
- The SDL callback only consumes already prepared PCM from a bounded ring buffer and performs bounded copying/mixing.
- Cross-thread video delivery holds at most one not-yet-presented frame per bridge. Older pending frames may be dropped; audio timing remains the master.
- Stop/cancel always wakes reconnect, reader, decoder, audio-render, and UI-delivery waits before joining threads.

## User-Facing Behavior

- Status bar shows opening, buffering, reconnect attempt/delay, playing, and stopped states.
- Final failures show one actionable message; retries do not create modal-dialog storms.
- Settings persist volume, speed, loop policy, resume behavior, and network defaults.
- Authenticated or tokenized stream URLs are not persisted automatically. Public URLs may be persisted.
- Menus contain only implemented actions. Audio and subtitle menus list named tracks and update checked state.
- Embedded text/ASS and bitmap subtitles are supported; external subtitle loading supports at least SRT, ASS, and SSA.
- Audio-only formats are selectable from normal file dialogs and playlists.
- Resume positions are cleared near successful completion and are applied only after seekability/duration are known.
- Rendering honors sample aspect ratio and rotation. Unsupported HDR/color paths fall back safely and report limitations in diagnostics.

## Compatibility and Migration

- Preserve existing public `PlaybackController` calls while adding explicit track-selection methods.
- Preserve `VideoCtl::StartPlay`, stop, seek, pause, volume, speed, loop, and cycle methods until all application call sites migrate.
- Read legacy `bin/config/player_config.ini` once when the standard application config does not yet exist, then write only to `QStandardPaths::AppConfigLocation`.
- Existing non-sensitive playlist entries and recent files migrate. Stale QSettings array keys are removed on the first save.
- Hardware decoding is disabled automatically when device creation, codec negotiation, or frame transfer fails.

## Verification Strategy

- Every behavior change follows red-green-refactor and ends with focused plus full Debug verification.
- Pure policies receive deterministic unit tests.
- Qt widgets receive offscreen QTest coverage.
- Integration fixtures are generated locally by test helpers and contain no user media or downloaded assets.
- Lifecycle stress covers repeated open/stop, seek/pause, track switching, reconnect cancellation, and destruction.
- Windows CI builds Debug and Release. A supported Clang/Linux job runs ASan/UBSan; TSan is an additional concurrency gate where FFmpeg/SDL dependencies support it.
- Performance work records CPU, peak pending frames, memory, dropped presentation frames, and audio underruns on the same reference clips before and after each change.

## Completion Criteria

- Every task in `docs/superpowers/plans/2026-09-18-player-comprehensive-optimization.md` is checked and references its commit and verification result.
- Debug and Release builds succeed from a clean checkout.
- All CTest tests pass, including offscreen Qt and generated-media integration tests.
- No known data race remains in Clock, pause/seek, track switching, or session teardown paths.
- Subtitle, audio-only playback, explicit track selection, status/error feedback, settings restoration, and resume behavior pass automated and manual QA.
- Frame delivery is bounded, YUV rendering has BGRA fallback, audio callbacks do bounded work, and hardware decode falls back to software.
- Portable package and installer pass `docs/release-checklist.md` and `docs/player-qa-checklist.md` on a clean Windows machine.

