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
- Confirm `bin/myplayer.exe` is produced for Release builds.
- Confirm runtime DLLs copied beside the executable match the actual Qt, FFmpeg, SDL2, and SoundTouch versions.

## Portable Package

- Run `scripts/package-portable.ps1 -BuildDir build -Configuration Release -Version <version>`.
- Confirm the ZIP contains `bin/myplayer.exe`, Qt6 runtime DLLs, current FFmpeg/SDL2/SoundTouch DLLs, and `bin/platforms/qwindows.dll`.
- Confirm the ZIP contains `LICENSE`, `NOTICE`, third-party licenses, and source distribution instructions.
- Confirm the `.sha256` sidecar matches the ZIP.
- Extract the ZIP to a clean Windows directory and smoke-test `myplayer.exe --smoke-test`.

## Release Artifact

- Attach the portable package checksum.
- Record compiler, Qt, FFmpeg, SDL2, SoundTouch, and vcpkg dependency versions.
- Smoke-test extraction, launch, playback, and deletion of the extracted directory on a clean Windows machine.
