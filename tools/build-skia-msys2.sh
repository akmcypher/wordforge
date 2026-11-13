#!/usr/bin/env bash
# Rebuilds MSYS2's Skia package for Windows x64 so it runs on every x86-64 CPU.
#
# Skia compiles src/opts/SkOpts_hsw.cpp with -march=haswell (AVX2) and SkOpts_skx.cpp with
# -march=skylake-avx512. Both include Skia headers full of inline functions; the linker may
# keep those AVX2/AVX-512 copies for every caller. MSYS2's libskia.dll did (SkIRect code ran
# AVX-512 and crashed on CPUs without it). This builds MSYS2's own recipe, at a pinned commit
# and the same Skia commit, with those two files compiled for the baseline instead; Skia still
# detects the CPU but then installs ordinary SSE code. Symbols are kept so
# tools/package-windows.py can check the DLL, then strips its shipped copy.
#
# Run in an MSYS2 CLANG64 shell with base-devel and git installed. Usage: build-skia-msys2.sh <out dir>
set -euo pipefail
recipe_commit=c9a23ebb7b50bd0a05e3615094e5e2811bdae9eb
out=$(mkdir -p "${1:?output directory required}" && cd "$1" && pwd)
[[ "${MSYSTEM:-}" == CLANG64 ]] || { echo "run in an MSYS2 CLANG64 shell" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git clone --quiet --filter=blob:none --no-checkout https://github.com/msys2/MINGW-packages "$work/recipes"
git -C "$work/recipes" sparse-checkout set mingw-w64-skia
git -C "$work/recipes" checkout --quiet "$recipe_commit"
cd "$work/recipes/mingw-w64-skia"
grep -q '^pkgrel=5$' PKGBUILD || { echo "unexpected recipe revision; review before rebuilding" >&2; exit 1; }

cat >> PKGBUILD <<'RECIPE'

# Wordforge: baseline code in Skia's per-CPU files (see tools/build-skia-msys2.sh).
pkgrel=5.1
options=('!strip')
eval "_msys2_$(declare -f prepare)"
prepare() {
  _msys2_prepare
  cd "${srcdir}/${_realname}"
  sed -E -i 's#cflags = \[ "(/arch:AVX2|/arch:AVX512|-march=haswell|-march=skylake-avx512)" \]#cflags = []#' BUILD.gn
  if grep -En 'arch:AVX|march=(haswell|skylake)' BUILD.gn; then
    echo "Skia per-CPU flags still present" >&2
    return 1
  fi
}
RECIPE

MINGW_ARCH=clang64 makepkg-mingw --holdver --syncdeps --noconfirm --cleanbuild --force
cp ./*.pkg.tar.zst "$out/"
ls -l "$out"
