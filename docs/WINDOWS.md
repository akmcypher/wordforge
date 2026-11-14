# Windows portable package

Wordforge for Windows is the same application as on Linux, built with MSYS2's
Clang toolchain (`CLANG64` for x64, `CLANGARM64` for ARM64) against the same Qt 6,
Skia m143, HarfBuzz, FreeType, ICU, pugixml, libzip and Hunspell versions that
MSYS2 ships.

## x64 Skia is rebuilt

MSYS2's x64 `libskia.dll` (143.r77119.da51f0d6-5) runs AVX-512 instructions in
ordinary code, so it crashes with `0xC000001D` (illegal instruction) on CPUs
without AVX-512: most consumer Intel CPUs since 12th generation, AMD before Zen 4,
and x64 emulation on Windows on Arm. MSYS2's MinGW toolchain patch compiles all of
Skia with `-mavx2 -mavx512f -mf16c -mfma` (the crash was a `vpcmpgtq zmm` in
`SkIRect` code). GitHub's x64 runners have AVX-512, so the first v0.6.0 x64 ZIP
passed CI and then crashed under emulation on Windows 11 ARM64. It was withdrawn
from the release. A first rebuild that only changed Skia's per-CPU files still
contained AVX2 in 3,032 functions; the package check below caught it.

`tools/build-skia-msys2.sh` rebuilds MSYS2's own recipe (pinned commit, same Skia
commit) as package `143.r77119.da51f0d6-5.1` with the global flags removed, Skia's
`SkOpts_hsw.cpp`/`SkOpts_skx.cpp` compiled for baseline x86-64 (Skia still checks
the CPU at run time, then installs ordinary SSE code), and skcms's AVX2/AVX-512
transforms disabled as Skia does on other CPUs. Raster drawing and colour
conversion lose their AVX2 fast paths on newer CPUs. The
rebuild keeps symbols: `tools/package-windows.py` disassembles `libskia.dll` and
refuses an x64 package if any function outside Skia's CPU-checked `avx::`,
`hsw::` and `skx::` namespaces uses AVX2 or AVX-512 registers, then strips it.
The release workflow caches the rebuilt package. The Linux snap builds Skia
itself, and its amd64 binary has AVX code only in `avx::` functions. ARM64 is not
affected.

## Building a package

From an MSYS2 shell of the matching environment, with the packages listed in
`.github/workflows/windows-release.yml` installed:

```sh
cmake -S . -B build/package -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build/package
ctest --test-dir build/package --output-on-failure
python tools/package-windows.py --build-dir build/package \
  --prefix "$(cygpath -m "$MINGW_PREFIX")" --output-dir out --arch x64   # or arm64
```

`tools/package-windows.py`:

1. copies `wordforge.exe` as `Wordforge.exe` and runs `windeployqt` for the Qt
   libraries and the platform, style and image-format plugins (network, TLS and
   touch plugins are skipped because Wordforge does not use them);
2. walks the PE imports of every staged EXE and DLL and copies each DLL that comes
   from the MSYS2 prefix, until nothing new is needed;
3. **fails** if any import is neither in the package nor a Windows system DLL;
4. adds Hunspell dictionaries (en_US, en_GB, en_AU, en_CA, en_IN) under `dictionaries/`;
5. looks up every bundled file's MSYS2 package in the pacman database, copies its
   licence texts to `licenses/<package>/` and lists it in `THIRD-PARTY-NOTICES.txt`;
6. writes `BUILD-INFO.txt` (version, commit, architecture, date), `README.txt` and
   `LICENSE.txt`, then zips the folder and writes a `.sha256` file.

`Wordforge.exe` carries an icon, version information and an application manifest
(`packaging/windows/`). The manifest declares Windows 10/11 support, long paths and
the **UTF-8 process code page**. Hunspell opens dictionaries through narrow file
APIs, so without it spell check failed when Wordforge was unpacked into a folder
whose name is not representable in the system's ANSI code page (found by the
package test; covered by `spellCheckLoadsDictionaryFromUnicodeFolder`). The UTF-8
code page needs Windows 10 version 1903 or later.

## Installer

`packaging/windows/wordforge.iss` (Inno Setup 6.3+) packs the same staged folder as
the ZIP into `Wordforge-<version>-windows-<arch>-setup.exe`. It installs per user by
default (no administrator prompt; an all-users install is offered), adds a Start menu
shortcut and an optional desktop shortcut, registers an uninstaller, makes Wordforge
the handler for `.wfg` and adds it to "Open with" for `.docx` and `.txt` without
taking those types from their current program. CI compiles it after the ZIP test:

```powershell
ISCC /DAppVersion=0.6.1 /DArch=x64 /DSourceDir=<abs path>\out\Wordforge-0.6.1-windows-x64 /Oout packaging\windows\wordforge.iss
```

## Checking a package

```powershell
tools\test-windows-package.ps1 -Package out\Wordforge-0.6.1-windows-x64.zip
```

Given a `-setup.exe` instead, it installs silently for the current user into the
Unicode folder, checks the `.wfg` association, runs the self-test, then uninstalls
and checks that the files and the association are gone. For a ZIP,
it verifies the checksum, unpacks into a temporary folder with a Cyrillic name,
reduces `PATH` to the Windows system folders, removes `QT_*` and `WORDFORGE_*`
variables, and runs `Wordforge.exe --self-test <report>`. The self-test, also run by
CTest as `self_test`, checks without opening a window:

* the Qt platform plugin, and PNG/JPEG image plugins;
* WFG and DOCX save and reopen through a folder named `Wordforge тест ✓ 文档`;
* system font discovery by Wordforge's shaper and by Qt;
* the en_US dictionary loads and classifies words correctly;
* rendering the main window, PDF export, and printing to a PDF printer;
* the clipboard (the user's clipboard contents are put back) and that the
  per-user data folder is writable.

## Evidence and limits

* 2026-11-13, Windows 11 ARM64 (Snapdragon, the developer's machine): ARM64
  package passes the package test with MSYS2 removed from `PATH`, from a
  Unicode folder. This shows the package carries every DLL and plugin it loads;
  it is not a clean virtual machine, so it cannot rule out a dependency on
  something else installed on that machine outside `PATH`.
* x64 packages are built and package-tested on GitHub's `windows-2025` runners.
  The x64 build also runs on Windows 11 ARM through x64 emulation.
* Not yet tested on a fresh Windows 10 or Windows 11 installation, with a screen
  reader, or with a physical printer. Interactive features (editing, tables,
  pictures, undo, themes, recovery, dialogs) are covered by the desktop test suite
  on Windows rather than by the package test.
* The packages and installers are not code-signed, so SmartScreen warns on first run.
  Signing (Microsoft Store/MSIX, SignPath or Azure Trusted Signing) needs an account
  in the publisher's name and is not set up.
