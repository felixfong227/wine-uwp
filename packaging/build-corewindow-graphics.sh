#!/usr/bin/env bash
# Build pinned DXVK/vkd3d-proton with CoreWindow support. This does not install the DLLs.
# Usage: build-corewindow-graphics.sh OUTPUT
# Requires Git, Meson, Ninja, glslang and x86_64 MinGW GCC/G++ on PATH.
set -euo pipefail
recipe=$(cd "$(dirname "$0")" && pwd)
mkdir -p "${1:?Fresh output directory required}"
output=$(cd "$1" && pwd)
for name in dxvk vkd3d; do
    if [[ -e "$output/source/$name" || -e "$output/$name" ]]; then
        echo 'Choose a fresh output directory; existing builds will not be overwritten.' >&2
        exit 1
    fi
done
mkdir -p "$output/source" "$output/licenses"
dxvk_commit=a78a467cadb22bee928904290cf8e7921b121851
vkd3d_commit=cae2e003bb8251627ff93c0f471ae18a28234490

fetch_source() {
    local name=$1 url=$2 commit=$3
    git init "$output/source/$name"
    git -C "$output/source/$name" fetch --depth=1 "$url" "$commit"
    git -C "$output/source/$name" checkout --detach FETCH_HEAD
    test "$(git -C "$output/source/$name" rev-parse HEAD)" = "$commit"
    git -C "$output/source/$name" submodule update --init --recursive --depth=1
}
fetch_source dxvk https://github.com/doitsujin/dxvk.git "$dxvk_commit"
fetch_source vkd3d https://github.com/HansKristian-Work/vkd3d-proton.git "$vkd3d_commit"

cd "$output/source/dxvk"
git apply "$recipe/dxvk-corewindow.patch"
meson setup --cross-file build-win64.txt --buildtype release --strip \
    -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false build.x64
nice -n 10 ninja -C build.x64 -j2 src/dxgi/dxgi.dll src/d3d11/d3d11.dll
mkdir -p "$output/dxvk/x64"
cp build.x64/src/dxgi/dxgi.dll build.x64/src/d3d11/d3d11.dll "$output/dxvk/x64/"
cp LICENSE "$output/licenses/DXVK.txt"

cd "$output/source/vkd3d"
# The tested Flatpak MinGW header omits this enum value; Windows defines it as 0.
meson setup --cross-file build-win64.txt --buildtype release --strip \
    -Dc_args=-DPATHCCH_NONE=0 --prefix "$output/vkd3d" --bindir x64 --libdir x64 build.x64
nice -n 10 ninja -C build.x64 -j2 install
cp LICENSE "$output/licenses/vkd3d-proton.txt"

cd "$output"
{
    printf 'DXVK %s\nvkd3d-proton %s\n' "$dxvk_commit" "$vkd3d_commit"
    printf 'CoreWindow adapted from Rosentti/dxvk-uwp 467bcf423560af41d937206858f0f49050c4aa26\n'
    sha256sum "$recipe/dxvk-corewindow.patch"
    for name in dxvk vkd3d; do
        printf '\n%s submodules:\n' "$name"
        git -C "source/$name" submodule status --recursive
    done
    printf '\nMinGW: '; x86_64-w64-mingw32-gcc -dumpfullversion
    printf 'Meson: '; meson --version
    printf 'Ninja: '; ninja --version
} > SOURCE-MANIFEST.txt
sha256sum dxvk/x64/dxgi.dll dxvk/x64/d3d11.dll \
    vkd3d/x64/d3d12.dll vkd3d/x64/d3d12core.dll > SHA256SUMS
sha256sum -c SHA256SUMS
printf 'Built graphics candidate in %s; application validation is still required.\n' "$output"
