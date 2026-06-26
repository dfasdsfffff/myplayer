# VideoCtl VideoState Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start the medium-depth internal refactor by making `VideoState` responsibility boundaries explicit and moving media synchronization helpers out of `VideoCtl`.

**Architecture:** Keep `VideoCtl` as the internal facade. Introduce grouped state structs while preserving behavior, then extract clock/sync calculations to a standalone `MediaSync` module that depends on `VideoState` rather than `VideoCtl`.

**Tech Stack:** C++20, CMake, FFmpeg, SDL2, existing `play_core` static library and lightweight executable tests.

---

### Task 1: Add State Group Structs Without Behavior Changes

**Files:**
- Modify: `play_core/video_state.h`

- [ ] **Step 1: Introduce grouped structs**

Add `SessionState`, `MediaClockState`, `AudioState`, `VideoTrackState`, `SubtitleState`, and `FilterState` above `VideoState`.

- [ ] **Step 2: Convert `VideoState` to aggregate groups**

Replace flat fields with grouped members named `session`, `clocks`, `audio`, `video`, `subtitle`, and `filters`.

- [ ] **Step 3: Move destructor cleanup to matching group destructors**

Move existing cleanup code into the owning group destructors while keeping cleanup order deterministic.

### Task 2: Update Existing Call Sites Mechanically

**Files:**
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/videoctl.h`
- Modify: any `play_core/*.cpp` or `play_core/*.h` that directly accesses moved `VideoState` fields

- [ ] **Step 1: Replace session fields**

Map `VideoState` session fields to `is->session.<field>`.

- [ ] **Step 2: Replace clock fields**

Map clocks and sync policy to `is->clocks.<field>`.

- [ ] **Step 3: Replace audio fields**

Map audio stream, queues, decoder, buffers, resampler, SoundTouch, volume, and sample display fields to `is->audio.<field>`.

- [ ] **Step 4: Replace video fields**

Map video stream, queues, decoder, frame timing, conversion, frame drops, and geometry fields to `is->video.<field>`.

- [ ] **Step 5: Replace subtitle fields**

Map subtitle stream, queues, decoder, and conversion fields to `is->subtitle.<field>`.

- [ ] **Step 6: Replace filter fields**

Map filter contexts and graph to `is->filters.<field>`.

### Task 3: Extract Media Sync Helpers

**Files:**
- Create: `play_core/media_sync.h`
- Create: `play_core/media_sync.cpp`
- Modify: `play_core/videoctl.h`
- Modify: `play_core/videoctl.cpp`
- Modify: `play_core/CMakeLists.txt`

- [ ] **Step 1: Add `MediaSync` declarations**

Create free functions for master sync type, master clock, external clock speed checks, target delay calculation, frame duration, and video pts updates.

- [ ] **Step 2: Move implementation out of `VideoCtl`**

Move the existing helper bodies to `media_sync.cpp` and update call sites to use `MediaSync::`.

- [ ] **Step 3: Remove obsolete `VideoCtl` declarations**

Remove the moved static helper declarations from `VideoCtl`.

- [ ] **Step 4: Add the new source file to `play_core`**

Add `media_sync.cpp` to `play_core/CMakeLists.txt`.

### Task 4: Add Focused Refactor Tests

**Files:**
- Modify: `tests/play_core_refactor_tests.cpp`

- [ ] **Step 1: Add compile-time state grouping checks**

Assert that `VideoState` exposes the expected grouped members.

- [ ] **Step 2: Add sync helper smoke checks**

Create a default `VideoState`, initialize clock sync policy enough to call the new helper functions, and verify they are callable without depending on `VideoCtl`.

### Task 5: Verify And Commit

**Files:**
- Verify all modified files

- [ ] **Step 1: Build the test target**

Run the existing CMake build for the configured build directory and target used by the repository.

- [ ] **Step 2: Run `play_core_refactor_tests`**

Run the existing play core refactor test executable.

- [ ] **Step 3: Review the diff**

Run `git diff` and confirm the change is limited to the planned internal refactor.

- [ ] **Step 4: Commit**

Commit with message `Refactor VideoState internal state groups`.
