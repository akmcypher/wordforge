#!/usr/bin/env bash
# Rebuilds MSYS2's Skia package for Windows x64 so it runs on every x86-64 CPU.
#
# MSYS2's MinGW patch (0002-add-mingw-toolchain-skia.patch) compiles ALL of Skia with
# -mavx2 -mavx512f -mf16c -mfma, so its libskia.dll executes AVX-512 instructions in ordinary
# code and crashes on CPUs without AVX-512 (most consumer PCs; x64 emulation on Arm). Skia
# also compiles SkOpts_hsw.cpp / SkOpts_skx.cpp and skcms's Hsw/Skx transforms with
# -march=haswell / -march=skylake-avx512; inline functions from the headers those include can
# be kept by the linker for every caller. This builds MSYS2's own recipe, at a pinned commit
# and the same Skia commit, with: the global flags removed, Skia's per-CPU files compiled for
# baseline x86-64 (Skia still detects the CPU, then installs ordinary SSE code), and skcms's
# AVX2/AVX-512 transforms disabled as Skia already does for non-x64 targets. Symbols are kept
# so tools/package-windows.py can check the DLL, then strips its shipped copy.
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

# Wordforge: baseline x86-64 code throughout (see tools/build-skia-msys2.sh).
pkgrel=5.1
options=('!strip')
eval "_msys2_$(declare -f prepare)"
prepare() {
  _msys2_prepare
  cd "${srcdir}/${_realname}"
  # Global ISA flags added by MSYS2's MinGW toolchain patch.
  sed -E -i '/^[[:space:]]*"-m(avx2|avx512f|f16c|fma)",$/d' gn/skia/BUILD.gn
  # Skia's runtime-dispatched per-CPU files: same code, baseline instructions.
  sed -E -i 's#cflags = \[ "(/arch:AVX2|/arch:AVX512|-march=haswell|-march=skylake-avx512)" \]#cflags = []#' BUILD.gn
  # skcms: no AVX2/AVX-512 transforms, as on non-x64 targets.
  sed -E -i -e 's#enabled = current_cpu == "x64" && target_os != "android"#enabled = false#' \
            -e 's#if \(target_cpu != "x64" \|\| target_os == "android"\) \{#if (true) {#' \
            modules/skcms/BUILD.gn
  if grep -En -- '-mavx|-mf16c|-mfma|march=(haswell|skylake)|arch:AVX' gn/skia/BUILD.gn BUILD.gn; then
    echo "x86 ISA flags still present" >&2
    return 1
  fi
  [[ $(grep -c 'enabled = false' modules/skcms/BUILD.gn) -eq 2 ]] && grep -q 'if (true) {' modules/skcms/BUILD.gn || {
    echo "skcms per-CPU transforms were not disabled" >&2
    return 1
  }
}
RECIPE

MINGW_ARCH=clang64 makepkg-mingw --holdver --syncdeps --noconfirm --cleanbuild --force
cp ./*.pkg.tar.zst "$out/"
ls -l "$out"
