#!/usr/bin/env bash
set -euo pipefail

if (( $# < 1 || $# > 2 )); then
    echo "Usage: $0 RELEASE_BUILD_DIR [VERSION]" >&2
    exit 2
fi

build_dir=$(realpath "$1")
version=${2:-1.0.0}
if [[ ! "$version" =~ ^[0-9A-Za-z._-]+$ ]]; then
    echo "Version may contain only letters, digits, dots, underscores and hyphens." >&2
    exit 2
fi
repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache="$build_dir/CMakeCache.txt"
if [[ ! -f "$cache" ]]; then
    echo "Missing CMake cache: $cache" >&2
    exit 1
fi

cache_value() {
    sed -n "s/^$1:[^=]*=//p" "$cache" | head -n 1
}

if [[ $(cache_value CMAKE_BUILD_TYPE) != Release ||
      $(cache_value MYPLAYER_ENABLE_SANITIZERS) != OFF ||
      $(cache_value MYPLAYER_BUILD_QT_APP) != ON ||
      $(cache_value MYPLAYER_BUILD_MONITOR) != ON ]]; then
    echo "Configure a Release build of both applications with sanitizers OFF." >&2
    exit 1
fi

qt_root=$(cache_value PLAYERDEMO_QT_ROOT)
vcpkg_root="$(cache_value VCPKG_INSTALLED_DIR)/$(cache_value VCPKG_TARGET_TRIPLET)"
output_dir=$(cache_value PLAYERDEMO_OUTPUT_DIR)
extra_library_dir=${EXTRA_LIBRARY_DIR:-}
extra_library_license=${EXTRA_LIBRARY_LICENSE:-}
if [[ -n "$extra_library_dir" && ( ! -d "$extra_library_dir" || ! -f "$extra_library_license" ) ]]; then
    echo "EXTRA_LIBRARY_DIR requires an existing directory and EXTRA_LIBRARY_LICENSE file." >&2
    exit 1
fi
library_search_path="$qt_root/lib:$vcpkg_root/lib${extra_library_dir:+:$extra_library_dir}"
if [[ ! -d "$qt_root/lib" || ! -d "$vcpkg_root/lib" || ! -d "$output_dir" ]]; then
    echo "Qt, vcpkg, or application output directory is missing from the CMake cache." >&2
    exit 1
fi
qt_license_dir=${QT_LICENSE_DIR:-"$qt_root/../Src/LICENSES"}
if [[ ! -d "$qt_license_dir" ]]; then
    echo "Qt license texts are missing; set QT_LICENSE_DIR to the matching Qt source LICENSES directory." >&2
    exit 1
fi
patchelf_bin=${PATCHELF:-$(command -v patchelf || true)}
if [[ ! -x "$patchelf_bin" ]]; then
    echo "patchelf is required; install it or set PATCHELF to its executable path." >&2
    exit 1
fi

cmake --build "$build_dir" --config Release --target myplayer myplayer_monitor
package="MyPlayer-$version-linux-$(uname -m)"
mkdir -p "$repo_dir/dist"
archive="$repo_dir/dist/$package.tar.gz"
if [[ -e "$archive" ]]; then
    echo "Archive already exists: $archive" >&2
    exit 1
fi

stage_parent=$(mktemp -d)
trap 'rm -rf -- "$stage_parent"' EXIT
stage="$stage_parent/$package"
mkdir -p "$stage/bin" "$stage/lib" "$stage/plugins/platforms" "$stage/licenses"
cp "$output_dir/myplayer" "$output_dir/myplayer_monitor" "$stage/bin/"
cp "$repo_dir/LICENSE" "$repo_dir/NOTICE" "$repo_dir/THIRD-PARTY-NOTICES.md" "$stage/"
cp "$repo_dir/sdk/third_party/sigslot/LICENSE" "$stage/licenses/sigslot-LICENSE"
cp "$repo_dir/apps/monitor/REFERENCE-LICENSE.txt" "$stage/licenses/monitor-REFERENCE-LICENSE.txt"
if [[ -n "$extra_library_dir" ]]; then
    cp "$extra_library_license" "$stage/licenses/extra-library-copyright"
fi
printf '[Paths]\nPrefix=..\nPlugins=plugins\n' > "$stage/bin/qt.conf"

declare -A scanned=()
copy_dependencies() {
    local file=$1 dependency name destination
    [[ -z ${scanned[$file]+yes} ]] || return 0
    scanned[$file]=1
    while IFS= read -r dependency; do
        if [[ "$dependency" == "$qt_root"/lib/* ||
              "$dependency" == "$vcpkg_root"/lib/* ||
              ( -n "$extra_library_dir" && "$dependency" == "$extra_library_dir"/* ) ]]; then
            name=$(basename "$dependency")
            destination="$stage/lib/$name"
            if [[ ! -e "$destination" ]]; then
                cp -L "$dependency" "$destination"
                copy_dependencies "$dependency"
            fi
        fi
    done < <(LD_LIBRARY_PATH="$library_search_path" ldd "$file" |
        awk '$2 == "=>" && $3 ~ /^\// { print $3 }')
}

for file in "$stage/bin/myplayer" "$stage/bin/myplayer_monitor"; do
    copy_dependencies "$file"
done
for name in libqxcb.so libqwayland-egl.so libqwayland-generic.so libqoffscreen.so; do
    file="$qt_root/plugins/platforms/$name"
    if [[ -f "$file" ]]; then
        cp -L "$file" "$stage/plugins/platforms/"
        copy_dependencies "$file"
    fi
done
for type in imageformats iconengines tls xcbglintegrations wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    if [[ -d "$qt_root/plugins/$type" ]]; then
        mkdir -p "$stage/plugins/$type"
        for file in "$qt_root/plugins/$type/"*.so; do
            [[ -f "$file" ]] || continue
            cp -L "$file" "$stage/plugins/$type/"
            copy_dependencies "$file"
        done
    fi
done

for file in "$stage/bin/myplayer" "$stage/bin/myplayer_monitor"; do
    "$patchelf_bin" --set-rpath '$ORIGIN/../lib' "$file"
done
for file in "$stage/lib/"*.so*; do
    [[ -f "$file" ]] && "$patchelf_bin" --set-rpath '$ORIGIN' "$file"
done
while IFS= read -r -d '' file; do
    "$patchelf_bin" --set-rpath '$ORIGIN/../../lib' "$file"
done < <(find "$stage/plugins" -type f -name '*.so' -print0)

for file in "$vcpkg_root/share/"*/copyright; do
    [[ -f "$file" ]] || continue
    cp "$file" "$stage/licenses/$(basename "$(dirname "$file")")-copyright"
done
cp -a "$qt_license_dir" "$stage/licenses/Qt"

while IFS= read -r -d '' file; do
    if ldd "$file" | grep -q 'not found'; then
        echo "Missing packaged dependency: $file" >&2
        ldd "$file" >&2
        exit 1
    fi
    if ldd "$file" | grep -F -q -e "$qt_root" -e "$vcpkg_root"; then
        echo "Build-machine dependency remains in $file" >&2
        ldd "$file" >&2
        exit 1
    fi
done < <(find "$stage/bin" "$stage/lib" "$stage/plugins" -type f \( -name 'myplayer*' -o -name '*.so*' \) -print0)
(
    cd "$stage_parent"
    QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy "$stage/bin/myplayer" --smoke-test
    QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy "$stage/bin/myplayer_monitor" --smoke-test
)
tar -C "$stage_parent" -czf "$archive" "$package"
(cd "$repo_dir/dist" && sha256sum "$package.tar.gz") > "$archive.sha256"
echo "Created $archive"
