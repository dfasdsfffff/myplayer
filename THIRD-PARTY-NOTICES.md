# Third-Party Notices

MyPlayer includes or links to third-party components. Keep this file with
binary distributions together with each component's complete license text.

## FFmpeg

Used for media demuxing, decoding, filtering, and format conversion.

Portable archives include the complete dependency license texts under
`licenses/ffmpeg-copyright`. Source: <https://ffmpeg.org/download.html>.

## SDL2

Used for audio/video output and platform integration.

Portable license text: `licenses/sdl2-copyright`.
Source: <https://github.com/libsdl-org/SDL>.

## SoundTouch

Used for audio tempo, pitch, and rate processing.

Portable license text: `licenses/soundtouch-copyright`.
Source: <https://www.surina.net/soundtouch/>.

## sigslot

Used for signal/slot utilities.

Portable license text: `licenses/sigslot-LICENSE`.
Source is included under `lib/sigslot-1.2.3/` in the project repository.

## Qt 6

Used for the Qt Widgets desktop interface. Complete license texts are included
under `licenses/Qt/`. Qt source: <https://download.qt.io/archive/qt/6.9/6.9.3/>.

## libass And Transitive Dependencies

libass renders ASS/SSA subtitles. Portable archives include its license text
as `licenses/libass-copyright`, together with the copyright/license texts
for the deployed vcpkg dependency set (including FreeType, FriBidi, HarfBuzz,
libpng, zlib, Brotli, bzip2, OpenSSL, libiconv and libevent).
libass source: <https://github.com/libass/libass>.

The Windows packages also include Microsoft's app-local Visual C++ runtime
redistributable DLLs and Qt's deployed graphics support DLLs.
