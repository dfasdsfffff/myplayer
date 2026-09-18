# Subtitle Rendering Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver embedded ASS/text and bitmap subtitle frames safely to the Qt video surface and render them at the correct playback time.

**Architecture:** `DecoderWorkers` copies an `AVSubtitle` into the existing immutable `SubtitleFrame` before its FFmpeg storage can be released. `VideoCtl`, `PlaybackRuntime`, and `PlaybackRuntimeBridge` forward that value to `Show`; a Qt-only `SubtitleRenderer` converts libass output and decoded bitmap rectangles into SDL blend textures layered after video.

**Tech Stack:** C++20, FFmpeg, libass via vcpkg CMake config target, SDL2, Qt 6 Widgets, CTest.

**Spec:** `docs/superpowers/specs/2026-09-18-player-comprehensive-optimization-design.md`

## Global Constraints

- `play_core` has no Qt dependency and must never expose an owning or borrowed `AVSubtitle*` outside the decoder thread.
- Render only immutable `std::shared_ptr<const SubtitleFrame>` values; clear UI overlays on seek, subtitle-track change, and stop.
- Use synthetic subtitle cues/rectangles in tests; do not add network media fixtures.
- Link the Windows vcpkg libass package through its exported CMake target rather than Unix-only `pkg-config` discovery.

---

### Task 1: Core subtitle event delivery

**Files:**
- Modify: `play_core/decoder_workers.cpp`, `play_core/videoctl.*`, `play_core/playback_runtime.*`
- Modify: `apps/qt_player/playback_runtime_bridge.*`
- Modify: `tests/subtitle_dispatcher_tests.cpp`

**Interfaces:**
- Produces: `Signal<std::shared_ptr<const SubtitleFrame>> SigSubtitleFrame` on `VideoCtl` and `PlaybackRuntime`.
- Consumes: `SubtitleDispatcher::Copy(const AVSubtitle&, double)`.

- [ ] **Step 1: Write failing signal-forwarding tests** that attach to a `PlaybackRuntime`/bridge test seam, publish a frame, and assert its copied text and timing arrive unchanged; publish an empty frame and assert it represents a clear event.
- [ ] **Step 2: Run the focused target**

Run: `ctest --test-dir build/Desktop_Qt_6_9_3_MSVC2022_64bit-Debug -R subtitle -C Debug --output-on-failure`

Expected: a failure because no subtitle signal is exposed.

- [x] **Step 3: Implement the smallest event path**: copy in `DecoderWorkers::Subtitle` before queue ownership changes; have `VideoCtl` emit when the current subtitle cue changes or expires; forward it unchanged through runtime and bridge using the existing queued `QPointer` pattern.
- [x] **Step 4: Run focused subtitle tests** and confirm the expected event sequence passes.

### Task 2: libass/SDL subtitle renderer

**Files:**
- Modify: `CMakeLists.txt`, `play_core/CMakeLists.txt`
- Create: `apps/qt_player/subtitle_renderer.h`, `apps/qt_player/subtitle_renderer.cpp`
- Create: `tests/subtitle_renderer_tests.cpp`

**Interfaces:**
- Produces: `SubtitleRenderer::setFrame(std::shared_ptr<const SubtitleFrame>)`, `clear()`, and `render(SDL_Renderer*, QSize videoSize, double clockSeconds)`.
- Consumes: `SubtitleFrame::assOrText`, `SubtitleFrame::bitmaps`.

- [ ] **Step 1: Write failing renderer tests** for a synthetic bitmap rectangle: a valid cue produces a texture-sized draw operation in bounds, expired/cleared cues draw nothing, and text input produces a non-empty libass image list when a system font is available.
- [x] **Step 2: Run `subtitle_renderer_tests`** and confirm failure because the target and class do not exist.
- [x] **Step 3: Configure libass and implement the renderer**: use vcpkg's imported libass target, hold one `ASS_Library`/`ASS_Renderer`, cache a track per text source, translate libass images to transient BGRA blend textures, and draw bitmap rectangles with `SDL_BLENDMODE_BLEND` after video.
- [x] **Step 4: Run focused renderer tests** and confirm valid, expired, and clear behavior passes.

### Task 3: Show integration and clearing lifecycle

**Files:**
- Modify: `apps/qt_player/show.*`, `apps/qt_player/mainwid.cpp`, `CMakeLists.txt`
- Modify: `tests/main_window_behavior_tests.cpp` or add a focused offscreen subtitle integration test.

**Interfaces:**
- Consumes: `PlaybackRuntimeBridge::SigSubtitleFrame(std::shared_ptr<const SubtitleFrame>)`.
- Produces: video followed by subtitle composition in `Show::RenderCurrentFrame()`.

- [ ] **Step 1: Write a failing offscreen test** that delivers a subtitle frame, starts/stops playback, and verifies the renderer clear method is called on stop; invoke dimensions changes to assert rendering remains viewport-bounded.
- [x] **Step 2: Run the focused Qt test** with `QT_QPA_PLATFORM=offscreen` and confirm it fails because `Show` lacks a subtitle slot.
- [x] **Step 3: Add `Show::OnSubtitleFrame` and compose the renderer after `SDL_RenderCopy`**; clear its state in `OnStopFinished` and connect bridge events in `MainWid`.
- [x] **Step 4: Run the focused Qt test** and confirm it passes.

### Task 4: External subtitle file workflow and final validation

**Files:**
- Modify: `apps/qt_player/mainwid.*`, `apps/qt_player/show.*`, `apps/qt_player/subtitle_renderer.*`
- Modify: `tests/subtitle_renderer_tests.cpp`, `CMakeLists.txt`

- [x] **Step 1: Write a failing test** that loads synthetic SRT/ASS text into an external renderer track, checks playback-time alignment, then unloads it without clearing an embedded bitmap cue.
- [x] **Step 2: Run the focused test** and confirm external-track APIs are absent.
- [x] **Step 3: Implement an “Open Subtitle” action** that loads SRT/ASS/SSA into a distinct libass track and supports unload/reload; preserve embedded-frame state.
- [x] **Step 4: Run focused tests, the full Debug build, and full CTest**; inspect the final diff and record manual QA still requiring real embedded text/bitmap/external files.

Completed: `748bb3c`, focused renderer and offscreen menu tests passed; full Debug build passed; CTest passed 27/27. Real-media manual QA is deferred to the release checklist because this workspace has no media fixtures.
