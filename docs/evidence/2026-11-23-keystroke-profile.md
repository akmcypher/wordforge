# Keystroke profile at 1000 pages (2026-11-23)

`wordforge_desktop_benchmark --long` types 50 keystrokes into a 32,000-paragraph
document through `PageView::insertText`, each followed by a viewport paint. Windows ARM64
Release, single runs.

## Where the time went (after the incremental word count, main 99ddf14)

The keystroke was timed in pieces with temporary instrumentation:
- **Edit, about 4.5 ms:**
  - model edit 0.6 ms, incremental layout 2.9 ms, renderer sync 0.2 ms
  - `contentChanged` and `selectionChanged` listeners about 0.8 ms
- **Viewport paint, about 20 ms:**
  - pages replayed from their recorded pictures 1.2 ms, blit 0.4 ms
  - **spell-check underlines 17.6 ms**

`paintSpelling` looped over every line of the layout and called `pageTop` for each line.
`pageTop` called `pageOffset`, which summed the heights of all earlier pages, so the loop
was O(lines × pages). `pageAtY` was quadratic in pages for the same reason.

## Fix

- `PageView::pageOffset` reads prefix sums of the page heights. They are rebuilt when the
  layout's generation or page count changes, in O(pages) once per layout.
- `PageView::pageAtY` binary-searches the page tops.
- `paintSpelling` skips lines outside the visible pages before computing positions.

## Result

| | Median keystroke ms | P95 ms |
|---|---:|---:|
| Before (99ddf14) | 24–27 | 25–31 |
| After | 5.4–5.8 | 6.7 |

Spell checking was on in these runs (its underlining is what took the 17.6 ms). The paint now costs
about 1.3 ms, and the edit itself about 3.6–4.5 ms. All six CTest suites pass on Windows
ARM64 and Ubuntu ARM64 (WSL).

Measured afterwards with the same benchmark copied onto tag v0.6.10, the 0.6.10 code
takes a median of 181–185 ms per keystroke (P95 225–235 ms). Five changes since that
release bring it to about 5.6 ms, about 33 times faster:
- incremental pagination and page stamps
- copy-on-write paragraphs and the incremental word count
- this fix

An earlier note gave 116 ms for 0.6.10. That figure was main at 7939d73, which already
had incremental pagination and page stamps. These are single runs on synthetic text, not
a P95 objective result.

## Follow-up: pagination keys and checkpoints (2026-11-24)

Timed inside `LayoutEngine::paginate` on the same benchmark, the incremental layout of one
keystroke spent about 1.9 ms hashing every paragraph's pagination key, about 0.3 ms finding
section settings, and about 0.4 ms copying the per-paragraph checkpoints into a new vector.
- Each paragraph's own key part is kept per paragraph and recomputed only where the
  paragraphs aren't shared with the previous composition's (copy-on-write pointers). The
  final key adds the section, table and note parts. Keys now take about 0.1 ms.
- The checkpoints vector is reused in place: only the changed range is resized, and the
  reused tail is shifted only when an edit moved lines or pages.

Median keystroke 3.1–3.7 ms in two runs (P95 4.0–5.4 ms); a third, noisier run gave
5.3 ms. Before: 5.4–5.8 ms. `IncrementalLayout.RandomEditsMatchFullLayout` fails if
checkpoints inside a new table row group aren't invalidated, so that case is covered.
