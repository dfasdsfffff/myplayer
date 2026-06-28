# MyPlayer

MyPlayer is a C++20 desktop media player built with Qt Widgets, FFmpeg, SDL2, and SoundTouch. The internal executable and CMake product name is `myplayer`.

This repository is based on the original GPL project [`itisyang/playerdemo`](https://github.com/itisyang/playerdemo). The product name, UI branding, build outputs, and packaging metadata have been changed for this maintained fork, while upstream attribution and GPL licensing are preserved.

Project repository and source distribution:

- <https://github.com/dfasdsfffff/playerdemo-master>

## Features

- Local audio/video playback through FFmpeg decoding and SDL2 output.
- Qt Widgets main application with playlist, playback controls, custom title bar, settings, and about dialog.
- Optional Dear ImGui player entry point for lower-level playback experiments.
- Network stream support for HTTP(S), RTSP, RTP, UDP, and related playback sources.
- Playlist import/export support for local files and network stream URLs.
- Playback speed and audio processing support through SoundTouch.
- `play_core/` library that keeps playback runtime logic separate from the Qt UI layer.

## Build

Requirements:

- CMake 3.20 or newer.
- C++20 compiler. Windows builds are tested with MSVC 2022.
- Qt 6 with Core, Gui, and Widgets.
- Bundled or externally provided FFmpeg, SDL2, SoundTouch, and sigslot dependencies.
- vcpkg for the Dear ImGui dependency.

Typical Windows build:

```powershell
cmake -S . -B build -DPLAYERDEMO_QT_ROOT="C:/Qt/6.9.3/msvc2022_64"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Build outputs:

```text
bin/myplayer_debug.exe
bin/myplayer.exe
bin/myplayer_imgui_debug.exe
bin/myplayer_imgui.exe
```

The `PLAYERDEMO_*` CMake cache variables are kept for compatibility with the existing build scripts:

- `PLAYERDEMO_DEPS_ROOT`
- `PLAYERDEMO_FFMPEG_ROOT`
- `PLAYERDEMO_SDL2_ROOT`
- `PLAYERDEMO_SOUNDTOUCH_ROOT`
- `PLAYERDEMO_SIGSLOT_ROOT`
- `PLAYERDEMO_QT_ROOT`
- `PLAYERDEMO_OUTPUT_DIR`

## Project Layout

```text
apps/qt_player/       Qt Widgets application
apps/imgui_player/    Dear ImGui application
play_core/            Playback runtime, decoding, queues, clocks, networking
tests/                Build and behavior regression tests
lib/                  Bundled third-party dependency roots
docs/                 Developer, release, and QA documentation
LICENSE               GPL license text
NOTICE                Attribution and third-party notice summary
myplayer.aip          Advanced Installer project seed, updated for MyPlayer metadata
```

## Packaging

`myplayer.aip` is an Advanced Installer project file. It is not needed to build or run MyPlayer from CMake. It is kept only as a Windows installer project seed and must be validated in Advanced Installer before release.

Before shipping, follow:

- [Release checklist](docs/release-checklist.md)
- [Player QA checklist](docs/player-qa-checklist.md)

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
