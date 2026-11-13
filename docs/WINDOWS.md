# Windows portable package

Wordforge for Windows is the same application as on Linux, built with MSYS2's
Clang toolchain (`CLANG64` for x64, `CLANGARM64` for ARM64) against the same Qt 6,
Skia m143, HarfBuzz, FreeType, ICU, pugixml, libzip and Hunspell versions that
MSYS2 ships.

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

## Checking a package

```powershell
tools\test-windows-package.ps1 -Package out\Wordforge-0.6.0-windows-x64.zip
```

It verifies the checksum, unpacks into a temporary folder with a Cyrillic name,
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
* The packages are not code-signed, and there is no installer yet.
