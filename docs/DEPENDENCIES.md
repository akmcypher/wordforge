# Dependency evidence

Initial host: Windows 11 ARM64, Qualcomm Adreno X1-45, 2026-11-11.
Native CLANGARM64 packages are installed under
`C:/dev/wordforge-deps/msys64`; the MSYS shell itself runs under Windows x64
emulation, while the compiler output and Wordforge executable are ARM64.

| Component | Verified package version | License family |
|---|---|---|
| Clang | 23.1.5 | Apache-2.0 with LLVM exception |
| CMake | 4.4.5 | BSD |
| Ninja | 1.13.2 | Apache-2.0 |
| Qt6 base | 6.12.0 | LGPL/GPL/commercial; shared libraries used |
| Skia | 143.r77119.da51f0d6-5 | BSD-3-Clause and bundled notices |
| HarfBuzz | 15.1.0 | MIT-style |
| FreeType | 2.15.0 | FTL/GPL dual license |
| ICU | 78.3 | Unicode license |
| pugixml | 1.16 | MIT |
| libzip | 1.12.1 Windows; 1.7.3 Ubuntu | BSD-3-Clause |
| Hunspell | 1.7.5 Windows; 1.7.2 Ubuntu | MPL-1.1 / GPL-2.0+ / LGPL-2.1+ (tri-licence) |
| English Hunspell dictionaries | hunspell-en 2026.02.25 Windows; hunspell-en-us 2020.12.07 Ubuntu | SCOWL licence (permissive) |
| GoogleTest | 1.18.0 | BSD-3-Clause |

The exact installed package inventory is `dependencies-msys2.lock.txt`.
Signed package archives remain in the external dependency root's
`var/cache/pacman/pkg` directory. Preserve that cache with the lock file to
reproduce this build: MSYS2 is a rolling distribution and package URLs alone
are not a permanent archive. Upgrades require a new lock/evidence run.

Install the requested packages in a CLANGARM64 shell using:

```sh
pacman -S --needed mingw-w64-clang-aarch64-{clang,cmake,ninja,pkgconf,qt6-base,skia,harfbuzz,freetype,icu,gtest,pugixml,libzip,hunspell,hunspell-en}
```

Linux dependencies must supply the same CMake/pkg-config interfaces, including
Fontconfig and a Skia m143 build. The executed Ubuntu environment, native
dependency versions and reproducible source-build commands are in UBUNTU.md.
The Windows package's Skia GPU capabilities are not
proof that Wordforge has a working GPU backend.

This source tree uses the parent repository's Apache-2.0 license. Dependencies
retain their own licenses. A distributable bundle must include their notices
and meet the applicable Qt/FreeType source and relinking obligations; the
current launch helper uses installed shared libraries and is not an installer.
