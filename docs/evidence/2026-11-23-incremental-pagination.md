# Incremental pagination evidence (2026-11-23)

Phase 2A first slice. The design is in
`docs/superpowers/plans/2026-11-23-incremental-pagination.md`.

## Correctness: full-layout oracle

`tests/unit/incremental_layout_test.cpp` (in `layout_tests`) recomposes incrementally
after each edit. It compares every line, cell, note line, separator, page style and the
page count against a fresh engine's full composition. The scenarios are:

- typing and deleting at the start, middle and end of a 400-paragraph document
- an edit in the middle recomposing fewer than 10 steps; a line-adding edit stays bounded
- paragraph insertion, multi-paragraph deletion, undo, redo and page breaks; the reused
  tail shifts by paragraph and page
- widow/orphan control, keep-with-next headings, numbered lists and keep-lines
- footnotes (numbering and text changes), endnotes, and headers/footers with
  `{PAGE}`/`{NUMPAGES}`
- a table, continuous and next-page section breaks, two columns, and a landscape section
- 150 seeded random edits on a document with a footnote, a table and a keep-with-next
  paragraph. The edits are breaks, erases, undo, spacing changes and insertions.
- a layout from another engine falls back to a full composition

## Performance

`wordforge_benchmark`, Windows ARM64 Release, single run. The edit is typing in the
middle paragraph. "Full" recomposes with a warm measurement cache; "incremental" is the
editor's path.

| Pages | Paragraphs | Full edit layout ms | Incremental edit layout ms | Steps recomposed |
|---:|---:|---:|---:|---:|
| 1 | 32 | 0.22 | 0.09 | 2 |
| 100 | 3,200 | 5.41 | 0.46 | 2 |
| 500 | 16,000 | 40.24 | 2.28 | 2 |
| 1000 | 32,000 | 75.33 | 3.36 | 2 |

Before this change, the 0.6.10 build measured 116.7 ms for the same full edit layout at
1000 pages, in a separate run.

These numbers cover layout composition only:
- They don't include the editor's document copy, `PageRenderer::synchronize` (about
  94 ms to record 1000 pages; it re-keys every page on each edit) or painting.
- They're single runs, not P95 figures.

## Test runs

- Windows ARM64 Release: all six CTest suites pass (`build/incr-1123`); layout_tests
  has 47 passing tests and 1 Ubuntu-only skip.
- Ubuntu ARM64 (WSL WordforgeUbuntu): layout_tests passes all 48 tests. Before the fix,
  a full ctest run had all six suites building, with one layout failure: the bound on how
  far an added line ripples depends on the fonts (126 steps against a fixed limit of
  120). The bound is now a third of the document. The oracle comparisons passed on both
  platforms.
