# MyPlayer Release Checklist

Use this checklist before publishing a MyPlayer binary package.

## Legal And Source Distribution

- Confirm `LICENSE` is included.
- Confirm `NOTICE` is included.
- Confirm third-party license files from `lib/` are included or linked from the release notes.
- Confirm the release provides corresponding source code, or a clear written source offer, as required by GPL.
- Confirm product support, about, update, and source links point to `https://github.com/dfasdsfffff/playerdemo-master`.
- Confirm UI branding uses `MyPlayer` and does not present upstream author links as the product support channel.
- Confirm upstream attribution to `itisyang/playerdemo` remains in `README.md` and `NOTICE`.

## Build

- Configure a clean build directory.
- Build Release configuration.
- Build Debug configuration when symbols are needed for troubleshooting.
- Run `ctest --test-dir build -C Debug --output-on-failure`.
- Confirm `bin/myplayer.exe` and `bin/myplayer_imgui.exe` are produced for Release builds.
- Confirm runtime DLLs copied beside the executable match the actual Qt, FFmpeg, SDL2, and SoundTouch versions.

## Installer

- Open `myplayer.aip` in Advanced Installer.
- Confirm product name is `MyPlayer`.
- Confirm installed executable is `myplayer.exe`.
- Confirm shortcuts show `MyPlayer`.
- Replace stale Qt5 or old FFmpeg entries with the DLLs produced by the current CMake build.
- Confirm Control Panel support, about, and update links do not point to the upstream author unless intentionally used as an attribution link.
- Confirm the installer includes `LICENSE`, `NOTICE`, third-party license files, and source distribution instructions.

## Release Artifact

- Attach checksums for installer and portable package.
- Record compiler, Qt, FFmpeg, SDL2, SoundTouch, and vcpkg dependency versions.
- Smoke-test installation, launch, playback, uninstall, and reinstall on a clean Windows machine.
