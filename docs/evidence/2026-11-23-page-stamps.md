# Page stamps for incremental rendering (2026-11-23)

Follow-up to incremental pagination. `Layout::pageStamps` gives every page a stamp:
- A page the incremental composition reuses unchanged keeps its old stamp, even when an
  edit moves it to another page number.
- Recomposed pages get new stamps.
- Everything is new after a full composition, and when header or footer text changes
  (for example after a page count change).
- Pages with endnotes are new, because endnotes are always recomposed.

`PageRenderer::synchronize` reuses a cached picture by stamp without building the page's
content key. It falls back to the existing content-key comparison for pages with new
stamps, so full compositions behave as before. Picture resources are part of the
layout's global key, so any resource change recomposes every page.

## Verification

- `incremental_layout_test.cpp`: every edit checks that each page keeping a stamp has the
  same drawn content as the page that had it. This covers positions, lines, notes, cells,
  separators and page geometry. A middle edit gives new stamps to at most the edited page
  and the page where pagination converged.
- `render_test.cpp`: after an incremental edit, a sync re-records one page. After a
  paragraph insertion and undo near the start, every page's pixels match a fresh renderer
  on a fresh full layout.

## Performance

`wordforge_benchmark 1000`, Windows ARM64 Release, one run, 1000 pages / 32,000
paragraphs, typing in the middle:

| Step | Before | After |
|---|---:|---:|
| Renderer sync after the edit | 56–60 ms (every page keyed) | 0.27 ms, 1 page re-recorded |
| Layout after the edit | 3.3 ms incremental | unchanged |

The remaining per-edit O(document) cost is the editor's document copy (Phase 1 copy-on-write).

## Test runs

- Windows ARM64 Release (`build/incr-1123`): all six CTest suites pass.
- Ubuntu ARM64 (WSL WordforgeUbuntu): all six CTest suites pass.
