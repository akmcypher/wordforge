# Native Ubuntu build

Wordforge builds as a native Linux C++ application. WSL2 is an optional way to
run Ubuntu on a Windows host; it does not reuse the Windows executable or its
CMake cache. The Linux build uses Qt Widgets, Skia's CPU raster/PDF backends,
Fontconfig, FreeType, HarfBuzz, ICU and pugixml.

## Dependencies and build

On Ubuntu 24.04, install the dependencies inside Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential clang cmake ninja-build generate-ninja \
  pkg-config ca-certificates curl python3 qt6-base-dev qt6-base-dev-tools \
  libicu-dev libharfbuzz-dev libfreetype6-dev libpugixml-dev libzip-dev libhunspell-dev hunspell-en-us libfontconfig1-dev \
  libgtest-dev libjpeg-dev libpng-dev libwebp-dev zlib1g-dev libexpat1-dev \
  libegl1-mesa-dev libgl1-mesa-dev libvulkan-dev libxkbcommon-dev \
  fonts-dejavu-core fonts-noto-core fonts-noto-color-emoji
bash tools/build-linux.sh
```

The script downloads Google's Skia source at revision
`da51f0d60ea2b14e845a823dc11b405dbeef42d8` (m143), checks the archive SHA256,
builds the raster/PDF library with system dependencies, generates `skia.pc`,
then builds Wordforge and runs CTest. It does not install system packages itself.
No prebuilt or placeholder Skia library is used.

Defaults are `$HOME/.local/share/wordforge-deps` for dependencies and
`$HOME/wordforge-build` for the separate CMake build. Override these with
`WORDFORGE_DEPS_DIR`, `WORDFORGE_BUILD_DIR` and `WORDFORGE_BUILD_JOBS`.
The default parallelism is six; reduce it on memory-constrained machines.
Never point the Linux build directory at a Windows build directory.

```sh
~/wordforge-build/wordforge --sample
QT_QPA_PLATFORM=offscreen ~/wordforge-build/wordforge --sample --snapshot /tmp/wordforge.png
ctest --test-dir ~/wordforge-build --output-on-failure
```

## Validation environment

On 2026-11-11 a dedicated `WordforgeUbuntu` WSL2 distribution was imported
from Canonical's Ubuntu Base 24.04.5 ARM64 archive. Its SHA256 matched the
official `SHA256SUMS`:
`a91d5a93010193712d346d761372b7c9db6dfcf093893161c64ca107f05914f2`.
The distribution is stored under `C:\dev\wordforge-deps\ubuntu\wsl`.
Existing Docker distributions and the default WSL distribution were unchanged.

Actual Linux tools/libraries: AArch64 Linux 6.18.54.1-microsoft-standard-WSL2,
Ubuntu 24.04.5 LTS, GCC 13.3.0, Clang 18.1.3, CMake 3.28.3, GN 1000
(`03d10f1`), Qt 6.4.2, ICU 74.2, HarfBuzz 8.3.0, FreeType 2.13.2,
pugixml 1.14, Fontconfig 2.15.0 and GoogleTest 1.14.0.
Skia was compiled from source inside this distribution at
`/opt/wordforge-deps/skia`; the application build is `/root/wordforge-build`.
WSLg supplies X11 and Wayland display sockets.

Final execution evidence:

* Native ELF64/AArch64 executable; Qt, ICU, HarfBuzz and FreeType resolve to the
  Ubuntu shared libraries. Skia is the pinned source-built static library.
* All five CTest suites pass: 40 cases excluding Qt setup/cleanup, including
  actual fixed-strike Noto Color Emoji shaping at 12/24 points.
* The Qt `xcb` backend launched a real window via WSLg and captured its contents.
* Full test output and toolchain/ELF details are saved in
  `docs/evidence/2026-11-11-ubuntu-ribbon-ctest.txt` and
  `docs/evidence/2026-11-11-ubuntu-environment.txt`.

On this Windows host the installed native Linux build can be opened with:

```powershell
wsl -d WordforgeUbuntu -- env QT_QPA_PLATFORM=xcb XDG_RUNTIME_DIR=/tmp/wordforge-runtime /root/wordforge-build/wordforge --sample
```

The dedicated runtime directory was created with mode 0700 for the test user.
An ordinary Ubuntu desktop user should use its existing XDG runtime directory.
Windows-specific fonts are not bundled; Linux font fallback can change page
geometry and is not evidence of cross-platform Word font-metric fidelity.

This validates the listed Linux ARM64 environment only. A native Ubuntu desktop,
Linux x86_64, distribution packaging and other Unix systems need their own runs.
