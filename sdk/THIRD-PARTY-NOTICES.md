# Third-Party Notices

MyPlayer and MyPlayer Monitor include or link to third-party components.
Keep this file with binary distributions and each component's complete
license text. The portable license paths below describe the dual-application
archives produced by `scripts/package-portable.ps1`, not a bare build tree.
That script copies the configured dependency installation's copyright files;
actual linked/deployed versions depend on the build. The monitor's optional
standalone bundled-dependency build has a separate deployment path and is not
automatically a complete licensed portable package.

For the project v2.1.0 source state, the SDK source archive includes its
`LICENSE`, `NOTICE`, this notice, and sigslot's license at
`third_party/sigslot/LICENSE`; external dependency binaries are not included.
The Windows x64/MSVC Release C ABI install component uses a different layout:
when a vcpkg share tree is available it copies copyright files to
`licenses/vcpkg/<port>/copyright`. It does not currently copy sigslot's full
license separately; include that license from the SDK source before distributing
the binary SDK. Neither installation nor packaging is a complete compliance
audit, and this document does not claim v2.1.0 attachments have been published.

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
Source is included at `sdk/third_party/sigslot/` in the project repository,
or `third_party/sigslot/` within the standalone SDK source archive.

## Qt 6

Used for both Qt Widgets desktop interfaces. Complete license texts are
included under `licenses/Qt/` by the portable packaging script. The published
V1.1.0 Windows packages used Qt 6.9.3:
<https://download.qt.io/archive/qt/6.9/6.9.3/>.
For another Qt build, distribute the matching license texts and corresponding
source/other materials required by the applicable license.

## libass And Transitive Dependencies

libass renders ASS/SSA subtitles in the shared Qt Widgets SDK component used
by both applications, including standalone monitor builds. It is not a Core
SDK dependency. The repository development manifest declares libass even
when configuring Core only; external Core consumers need not provide it.

Portable archives include `licenses/libass-copyright` and copyright/license
texts from the configured vcpkg installation (including FreeType, FriBidi,
HarfBuzz, libpng, zlib, Brotli, bzip2, OpenSSL, libiconv and libevent when
present). This is an installation inventory, not proof that every DLL is used
by both executables. libass source: <https://github.com/libass/libass>.

The Linux Qt SDK may explicitly link the configured vcpkg Fontconfig library
when present; include its matching notices and those of the dependencies
actually distributed. Source: <https://www.freedesktop.org/wiki/Software/fontconfig/>.

The Windows packages also include Microsoft's app-local Visual C++ runtime
redistributable DLLs and Qt's deployed graphics support DLLs.

## Monitor Reference Code And Resources

The monitor UI, calendar, camera configuration and reference icons were adapted
from the supplied `h265-ffmpeg/2_project/xviewer` reference project under
Apache-2.0. The complete license is preserved in the source repository at
`apps/monitor/REFERENCE-LICENSE.txt`. Preserve applicable attribution and
license notices when distributing these components.

The current install/portable scripts do not automatically copy that reference
license into the monitor archive. Carry it with monitor binary distributions;
the packaging smoke check is not a license-compliance check.

## Source Distribution

Project source is maintained at <https://github.com/dfasdsfffff/myplayer> and
mirrored at <https://gitee.com/hu-xiao-yun/myplayer>. Use the matching release
tag for project source. Upstream links in this document identify components;
they do not replace the obligation to provide complete corresponding source,
build materials, notices or other required distribution materials under each
applicable license.
