# Performance evidence register

## Anchor-heavy model workload (0.5)

`wordforge_transaction_benchmark --anchors`: one 100,000-character paragraph,
1000 point bookmarks; same model-only operation sequence as the Phase 1A benchmark.
Per-operation per-paragraph boundary caches prevent repeated scans for each anchor.

| Platform | Single edit ms | Grouped break ms | Undo ms | Redo ms | Final peak MiB |
|---|---:|---:|---:|---:|---:|
| Windows ARM64 | 3.42 | 5.32 | 0.30 | 0.20 | 9.94 |
| Ubuntu ARM64 | 3.76 | 4.86 | 0.08 | 0.02 | 7.50 |

Raw observations: `docs/evidence/2026-11-12-*-bookmarks-model.csv`. Single Release
runs, not distributions or end-to-end UI latency; no rendering/layout/disk costs.

Updated 2026-11-12. Targets in the directive are not current performance claims.
Full benchmark definitions and raw evidence links: `docs/PERFORMANCE.md` and
`docs/evidence/`. Latest DOCX code has functional tests but no DOCX latency baseline.

## Phase 1A model-only workload

34,000 short paragraphs, Release; one recorded observation per platform. No layout,
rendering, disk I/O or Qt. Grouped operation is the actual `insertPageBreak` path;
memory is cumulative process high-water, not exact per-history-entry accounting.

| Platform | Single edit ms | Grouped break ms | Undo ms | Redo ms | Peak after single MiB | After grouped MiB | Final peak MiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| Windows ARM64 | 6.72 | 23.39 | 7.91 | 8.89 | 30.36 | 51.20 | 58.32 |
| Ubuntu ARM64 | 5.84 | 34.19 | 11.32 | 8.56 | 25.38 | 49.28 | 56.79 |

Raw: `docs/evidence/2026-11-12-*-phase1a-model.csv`. This is a new baseline,
not a before/after speedup or P95 measurement. The grouped scan/validation and
document/history copying are still O(N). Next optimization must retain the
rollback/identity tests while removing copies and introducing page convergence.

## Earlier layout and presentation workloads

| Executed workload | Observation | Important limit |
|---|---|---|
| 0.3 native Windows first widget paint | 419.61–817.25 ms across recorded runs | Starts inside main; excludes DLL/cold-file/compositor costs |
| 0.3 Ubuntu ARM64 X11/WSLg first paint | 233.14 ms | One recorded environment/run, not P95 |
| Windows 100 caret frames | ~40 ms total, zero page rasters | Synthetic forced paints, not every input path |
| Ubuntu 100 caret frames | 52.72 ms total, zero page rasters | Same narrow workload |
| 1000 pages/34000 short paragraphs, 0.3 | initial3011.18 ms; edit composition88.85 ms | Composition excludes model transaction/copy and presentation |
| Same workload native serialization/validation | 240.09 ms | Excludes disk I/O; no DOCX |
| Same workload peak process memory | 621 MiB | Synthetic text; retained layout/history/caches included |

No end-to-end P95/P99 objective has been certified. Missing: actual editing/undo
memory distributions, DOCX import/export, disk save, full search, long paragraphs,
real mixed-script corpus, images/tables and installed-platform measurements.
Do not reuse an old version's startup result as evidence for a newer UI.

Next measurements (roadmap1A/2A): run `wordforge_benchmark` and
`wordforge_desktop_benchmark` in Release before/after; add model-transaction time,
history bytes and recomposition scope. Later use repeated warm/cold runs and
median/P95/P99 with hardware, fonts, compiler, corpus and source SHA recorded.
