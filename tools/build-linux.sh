#!/usr/bin/env bash
# Run inside Ubuntu, not PowerShell. Dependencies stay outside the source tree.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
deps_dir=${WORDFORGE_DEPS_DIR:-"${HOME}/.local/share/wordforge-deps"}
build_dir=${WORDFORGE_BUILD_DIR:-"${HOME}/wordforge-build"}
jobs=${WORDFORGE_BUILD_JOBS:-6}
skia_revision=da51f0d60ea2b14e845a823dc11b405dbeef42d8
skia_sha256=63eee9235414e0171dec6b47d54a5b55057c4b38fce4514bdfc8003b5713a761
mkdir -p "$deps_dir"
if [[ ! -d "$deps_dir/skia" ]]; then
  curl --fail --location --retry 3 \
    "https://codeload.github.com/google/skia/tar.gz/${skia_revision}" \
    --output "$deps_dir/skia-m143.tar.gz"
  printf '%s  %s\n' "$skia_sha256" "$deps_dir/skia-m143.tar.gz" | sha256sum --check
  tar -xzf "$deps_dir/skia-m143.tar.gz" -C "$deps_dir"
  mv "$deps_dir/skia-${skia_revision}" "$deps_dir/skia"
fi
mkdir -p "$deps_dir/skia/out/Release" "$deps_dir/pkgconfig"
cat > "$deps_dir/skia/out/Release/args.gn" <<'ARGS'
is_official_build = true
is_debug = false
is_component_build = false
cc = "clang"
cxx = "clang++"
skia_enable_ganesh = false
skia_enable_graphite = false
skia_use_gl = false
skia_enable_pdf = true
skia_enable_skottie = false
skia_enable_svg = false
skia_use_fontconfig = true
skia_use_freetype = true
skia_use_harfbuzz = true
skia_use_icu = true
skia_use_system_freetype2 = true
skia_use_system_harfbuzz = true
skia_use_system_icu = true
skia_use_system_libjpeg_turbo = true
skia_use_system_libpng = true
skia_use_system_libwebp = true
skia_use_system_expat = true
skia_use_system_zlib = true
skia_use_piex = false
skia_use_dng_sdk = false
skia_use_wuffs = false
skia_use_perfetto = false
skia_use_fontations = false
skia_build_rust_targets = false
extra_cflags = [ "-I/usr/include/freetype2", "-I/usr/include/harfbuzz" ]
ARGS
(
  cd "$deps_dir/skia"
  gn gen out/Release
  ninja -C out/Release -j "$jobs" skia
)
cat > "$deps_dir/pkgconfig/skia.pc" <<PC
prefix=$deps_dir/skia
libdir=\${prefix}/out/Release
includedir=\${prefix}

Name: Skia
Description: Wordforge pinned Skia m143 raster/PDF build
Version: 143
Libs: -L\${libdir} -lskia -lskcms -ldl -lpthread -lfontconfig -lexpat -lfreetype -lpng -ljpeg -lz -lharfbuzz -lharfbuzz-subset -lwebp -lwebpdemux -lwebpmux
Cflags: -I\${includedir} -DSK_R32_SHIFT=16
PC
export PKG_CONFIG_PATH="$deps_dir/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel "$jobs"
ctest --test-dir "$build_dir" --output-on-failure
printf '\nLaunch the native Linux app: %s/wordforge\n' "$build_dir"
