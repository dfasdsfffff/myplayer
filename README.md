# MyPlayer

MyPlayer is a C++20 desktop media player built with Qt Widgets, FFmpeg, SDL2, and SoundTouch. The internal executable and CMake product name is `myplayer`.

This repository is based on the original GPL project [`itisyang/playerdemo`](https://github.com/itisyang/playerdemo). The product name, UI branding, build outputs, and packaging metadata have been changed for this maintained fork, while upstream attribution and GPL licensing are preserved.

Project repository and source distribution:

- <https://github.com/dfasdsfffff/playerdemo-master>

## Features

- Local audio/video playback through FFmpeg decoding and SDL2 output.
- Qt Widgets main application with playlist, playback controls, custom title bar, settings, and about dialog.

- Network stream support for HTTP(S), RTSP, RTP, UDP, and related playback sources.
- Playlist import/export support for local files and network stream URLs.
- Playback speed and audio processing support through SoundTouch.
- `play_core/` library that keeps playback runtime logic separate from the Qt UI layer.

## Build

Requirements:

- CMake 3.25 or newer (required by the version 6 CMake Presets schema).
- C++20 compiler. Windows builds use Visual Studio 2026 (MSVC).
- Qt 6 with Core, Gui, and Widgets.
- vcpkg, which resolves FFmpeg, SDL2, and SoundTouch from the manifest.


Typical Windows build:

```powershell
cmake -S . -B build -DPLAYERDEMO_QT_ROOT="C:/Qt/6.9.3/msvc2022_64"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The standard CMake options are:

- `MYPLAYER_BUILD_QT_APP` — build the Qt Widgets application (default: `ON`).
- `BUILD_TESTING` — build and register CTest targets (default: `ON`).
- `MYPLAYER_ENABLE_WARNINGS` — enable project warning flags (default: `ON`).
- `MYPLAYER_ENABLE_SANITIZERS` — enable supported Address/Undefined Behavior Sanitizers (default: `OFF`).

For a Qt-free playback-core configuration, use:

```powershell
cmake -S . -B build-core -DMYPLAYER_BUILD_QT_APP=OFF -DBUILD_TESTING=OFF
```

Build outputs:

```text
bin/myplayer_debug.exe
bin/myplayer.exe
```

The `PLAYERDEMO_*` CMake cache variables are kept for compatibility with the existing build scripts:

- `PLAYERDEMO_DEPS_ROOT` (retained for the bundled sigslot headers)
- `PLAYERDEMO_SIGSLOT_ROOT`
- `PLAYERDEMO_QT_ROOT`
- `PLAYERDEMO_OUTPUT_DIR`

## Project Layout

```text
apps/qt_player/       Qt Widgets application
play_core/            Playback runtime, decoding, queues, clocks, networking
tests/                Build and behavior regression tests
lib/                  Bundled third-party dependency roots
docs/                 Developer, release, and QA documentation
LICENSE               GPL license text
NOTICE                Attribution and third-party notice summary
scripts/package-portable.ps1  Portable Windows package build and verification
```

## Packaging

MyPlayer is distributed only as a portable Windows archive. Build it with `scripts/package-portable.ps1`, extract the resulting ZIP, and run `myplayer.exe` from the extracted directory. No installer is built, maintained, or required.

Before shipping, follow:

- [Release checklist](docs/release-checklist.md)
- [Player QA checklist](docs/player-qa-checklist.md)

## Playback Performance Baseline

Build Release first, then run from an interactive Windows desktop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark-video-presentation.ps1 `
  -Player bin/myplayer.exe -Ffmpeg ffmpeg -RunMs 10000 -TimeoutMs 30000
```

The script generates 1080p60 and 4K60 H.264 fixtures, runs the actual Qt/SDL
player, and writes `.cache/benchmarks/baseline.json`, per-player reports,
process CPU/memory samples, and logs. FFmpeg null-output decoding is recorded
separately as a reference, not as player presentation performance.
Use `-SkipFixtureGeneration` to reuse fixtures and `-SkipDecodeReference` to
omit that reference.

To measure an existing local video directly:

```powershell
bin/myplayer.exe --benchmark --benchmark-media C:/media/video.mp4 `
  --benchmark-output C:/existing-directory/player.json `
  --benchmark-run-ms 10000 --benchmark-timeout-ms 30000
```

Benchmark mode uses software decoding, speed 1, no looping/resume, and a
requested 1280×720 window. Reports distinguish GUI-delivered frames from
successful SDL-present returns; neither proves physical screen visibility or
counts decoder drops. CPU percentages use one logical core as 100%; memory
peaks are periodic samples. These video-only fixtures do not measure audio/video
drift. Compare repeated runs on the same machine, display setup, configuration,
and media; a short run does not establish long-term stability.

## Media Compatibility And Hardware Fallback

Build the acceptance runners and run the matrix with a caller-provided FFmpeg
executable that includes libx264, libx265, AAC, and MPEG4 encoders:

```powershell
cmake --build build --config Release --target media_compatibility_tests hardware_decode_policy_tests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify-media-compatibility.ps1 `
  -BuildDir build -Configuration Release -Ffmpeg ffmpeg
```

Each run creates an isolated directory under `.cache/media-compatibility/`.
`compatibility.json` aggregates runtime playback results and separate
`HardwareDecodeContext` decoding/transfer evidence. Fixtures include H.264/AAC
MP4, Main8 H.265 MKV, MPEG4 AVI, WAV, AAC, MP3 when the generator supports it,
invalid bytes, and an unusable ftyp-only MP4.

Runtime cases check decoded frame storage/dimensions, audio metadata, timed
natural completion, and failure behavior using dummy SDL sinks. Runtime frame
callbacks can repeat for redraws; only the separate direct-context decoder
asserts exact decoded-frame counts. Its adapter identity is read from an actual
hardware frame's D3D11 device, not inferred from installed GPUs. A successful
software fallback is not hardware coverage. These checks do not establish
speaker output, audio/video synchronization, all adapters/profiles, or release
readiness. The no-argument CTest cases need neither external encoders nor a GPU.

## License And Attribution

MyPlayer is distributed under the GNU General Public License because it is based on GPL-licensed upstream code from `itisyang/playerdemo`.

Keep these files in source and binary distributions:

- `LICENSE`
- `NOTICE`
- Third-party license files under `lib/`
- Corresponding source code or a clear written source offer, as required by GPL

Original upstream:

- Project: [`itisyang/playerdemo`](https://github.com/itisyang/playerdemo)
- Author: [`itisyang`](https://github.com/itisyang)
- Homepage: <https://itisyang.github.io/playerdemo/>
