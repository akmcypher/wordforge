# Executed performance evidence — 2026-11-11

## Single-ribbon update (0.3)

The new grouped ribbon adds controls; old 0.2 startup numbers are not a claim
about its startup. Current recorded native desktop observations:

| Platform | Entry to first widget paint ms | 100 caret frames ms | Caret page rasters |
|---|---:|---:|---:|
| Ubuntu ARM64, X11/WSLg | 233.14 | 52.72 | 0 |
| Windows ARM64, immediately after the 1,000-page workload | 817.25 | 50.82 | 0 |

Three subsequent Windows native runs observed 508.54, 456.59 and 419.61 ms to
first paint. Offscreen runs observed 123.80–128.41 ms. Native window creation
and display work remain a startup optimization target; no cold-start SLA is
claimed. Timing still starts inside main, as described below.

The current 1,000-page engine workload produced 1,000 actual pages with 34,000
paragraphs: initial layout 3,011.18 ms, edit composition 88.85 ms with one paragraph
remeasured, page recording 98.33 ms, first-page raster 3.24 ms, native serialization
and validation 240.09 ms, process peak working set 621 MiB. These timing boundaries
remain as documented for the original workload; they exclude end-to-end UI editing.
Raw current results are `docs/evidence/2026-11-11-ribbon-*.csv`.

## Interactive viewport update (0.2)

`wordforge_desktop_benchmark` builds the actual sample window, paints its document,
sends 100 alternating arrow-key events with a forced paint per event, then forces
100 more repaint frames. Release executable, same host and dependencies below.

| Backend | Entry to first widget paint ms | 100 caret frames ms | Page rasters during caret movement | 100 repaint frames ms | Page rasters during repaint |
|---|---:|---:|---:|---:|---:|
| Offscreen before cache | 127.36 | 67.74 | 100 | 63.75 | 100 |
| Offscreen after cache and UI update | 116.90 | 36.92 | 0 | 35.53 | 0 |
| Native Windows after update | 143.55 | 42.07 | 0 | 40.17 | 0 |

The offscreen caret workload improved by about 45%. The native run used the
actual Windows Qt platform at the host's 125% scaling. These single observed
runs are not a latency distribution. Entry timing starts inside `main` and
includes QApplication/window construction; DLL loading before `main`, external
file opening, display compositor latency and cold-cache startup are excluded.
Forced full-viewport paints include Qt image presentation; actual caret blink
paints are clipped to the caret rectangle. Cache tests also exercise content,
zoom and scroll invalidation and assert that selection changes visible pixels.

Raw results are recorded in `docs/evidence/2026-11-11-desktop-*.csv`.

## Initial document workload (0.1 foundation)

Host: Windows 11 ARM64, Qualcomm Adreno X1-45; native Clang 23.1.5 Release build.
Skia CPU. Single observed run, not a statistical latency distribution.

Run `build/release/wordforge_benchmark.exe`. Each requested page contains 34
short Arial 12-point paragraphs on US Letter paper. Actual page counts match
requested counts. Repeated text intentionally isolates engine overhead; this
is not a representative OOXML, table, image or complex-script corpus.

| Pages | Initial layout ms | Edit composition ms | Paragraphs remeasured | Record page pictures ms | First-page raster ms | Native serialize/validate ms | Peak process MiB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4.32 | 0.24 | 1 | 3.16 | 2.85 | 0.23 | 23 |
| 100 | 356.47 | 6.22 | 1 | 9.67 | 2.87 | 15.76 | 84 |
| 500 | 1761.38 | 31.58 | 1 | 38.37 | 1.89 | 79.51 | 320 |
| 1000 | 3543.25 | 61.79 | 1 | 69.76 | 2.72 | 154.18 | 587 |

Timing boundaries matter: edit composition excludes the model transaction and
renderer resynchronization. Serialization includes validation/reparse, but not
disk I/O. Raster timing draws the first page at point scale into a 900×1200
surface; it excludes window compositing and scrolling. Memory is cumulative
process high-water working set, including earlier cases, both retained layout
copies, undo snapshots and temporary persistence data.

Before sharing ICU grapheme-boundary tables and caching font-fallback choices,
the same 1,000-page initial layout took 26,483.54 ms. Afterward it took 3,543.25 ms
(about 7.5× faster). Existing Unicode/wrapping/regression tests still pass. This
is an engine optimization measured on a synthetic workload, not an FPS claim.

Next measurements must include end-to-end editing, long multi-line paragraphs,
mixed scripts, shaping/cache memory over many edits, save I/O, startup, scrolling,
zoom and recovery. Large tables and image workloads await their implementations.
