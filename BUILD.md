# Building

Requires CMake >=3.25, Ninja, a C++20 compiler, pkg-config, Qt6 Widgets/Test,
Skia, HarfBuzz, FreeType, ICU, pugixml, libzip >=1.7, Hunspell (with at least one
dictionary, e.g. hunspell-en-us) and GoogleTest. Qt PrintSupport is part of qt6-base.
All document operations are native.

Initial Windows ARM64 environment: `C:/dev/wordforge-deps/msys64`, CLANGARM64.
Launch its shell with `MSYSTEM=CLANGARM64`, then:

```sh
cd /c/dev/wordforge-public   # your clone
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/wordforge.exe
```

From PowerShell on this host, `./tools/run.ps1 -Sample` sets the dependency
search path for that process and launches the editable sample. Omit `-Sample`
for a blank document; use `-Document C:/path/file.wfg` to open a file.
The launcher defaults to the optimized Release executable. Build the `dev`
presets and pass `-Configuration Debug` when debugging. The Windows app uses
the GUI subsystem, so launching it does not open an extra console window.
When an already-open Windows executable prevents relinking, use the separate
optimized `preview` configure/build/test presets and
`./tools/run.ps1 -Configuration Preview -Sample`. This keeps the open document
available while the updated executable is built alongside it.

For performance measurements, configure/build/test with the `release` presets,
then run `./build/release/wordforge_benchmark.exe`. Its default workload covers
1/100/500/1000 pages and emits CSV. Pass one page count to run a single case.
The corpus uses 34 short paragraphs per US Letter page with the initial Arial
font; actual page count is always reported rather than assumed.

`wordforge --sample --snapshot output.png` launches the real widget application,
captures its content and exits. Set `QT_QPA_PLATFORM=offscreen` only for headless
testing; the Windows platform capture was also executed on this host.

See `docs/DEPENDENCIES.md` and its package lock before upgrading dependencies.

`wordforge_desktop_benchmark` measures entry-to-first-widget-paint, 100 caret
frames and 100 forced repaint frames. It reports page rasterizations, exposing
cache regressions rather than merely measuring a blank widget. See
`docs/PERFORMANCE.md` for measured results and timing boundaries.

For Ubuntu, use the dependency commands and pinned source-build helper in
[docs/UBUNTU.md](docs/UBUNTU.md). Ubuntu 24.04 ARM64 with Qt 6.4.2 was built and
tested natively, including a WSLg/X11 window launch. Linux x86_64 CI is configured
but has not been executed remotely. Other Unix platforms remain unverified.
