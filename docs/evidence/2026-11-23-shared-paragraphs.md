# Copy-on-write paragraphs evidence (2026-11-23)

Phase 1 follow-up to incremental pagination: an edit no longer copies the whole document.

## What changed

- `Document::paragraphs` is a `ParagraphList` (`core/document/document.hpp`), not a
  `std::vector<Paragraph>`.
  - Copying a document, an editor or an undo state shares the paragraphs.
  - Reading (`operator[]`, iteration, `front`/`back`) is const and never copies.
  - Changes go through `edit(i)`, which clones paragraph `i` only while it is shared, or
    through `editEach`, which keeps every paragraph it leaves unchanged shared.
  - Insert and erase copy the list of pointers only when it is shared.
- `Editor::transact` compares and stamps only the paragraphs between the shared prefix and
  the shared suffix.
- `validateDocument(document, validated)` skips each paragraph's own checks for
  paragraphs still shared with the document that was validated before. This applies only
  while the styles are unchanged.
  - References from those paragraphs are still checked: pictures, tracked-change revisions
    and unique footnotes.
  - Their ids are checked against everything else.
- Undo and redo keep the restored paragraphs' revisions, so incremental layout reuses
  everything the undo doesn't change. A newly opened document is still stamped afresh.

## Correctness

- `tests/unit/shared_paragraphs_test.cpp` (in `core_tests`) runs each of the following
  edits on a 300-paragraph document, then its undo and its redo:
  - typing and both erases, a cross-paragraph replace, paragraph and page breaks
  - character, paragraph, alignment, heading and link formatting
  - a bookmark, a comment, a footnote and tracked typing
  - a table, a section break and a page header

  For each edit it bounds how many paragraphs stop being shared, and it checks that redo
  restores the edited paragraphs. A second test undoes 27 mixed edits back to the
  original document.
- No `edit()` reference is taken before `checkpoint()` in the same function, which would
  write into the saved undo state; checked with a scan of `core/document`.
- All six CTest suites pass on Windows ARM64 Release (`build/incr-1123`) and on Ubuntu
  ARM64 (WSL WordforgeUbuntu). The incremental-layout oracle, including its undo/redo and
  150 random edits, passes on both.

## Performance

Windows ARM64 Release. "Before" is main 7939d73 built in a separate worktree on the same
machine; single runs.

`wordforge_transaction_benchmark` (34,000 short paragraphs, model only):

| | Single edit ms | Grouped page break ms | Undo ms | Redo ms | Peak MiB |
|---|---:|---:|---:|---:|---:|
| Before | 8.9–13.4 | 41 | 19–21 | 20–21 | 126 |
| After | 0.9–1.9 | 2.7–9 | <0.01 | <0.01 | 36 |

`--tables` (34,000 paragraphs and a table): inserting a row takes 4.7 ms instead of
42 ms, and a column 4.3 ms instead of 42 ms. `--anchors` (one 100,000-character
paragraph, 1000 bookmarks) is unchanged at about 10–11 ms per single edit.

`wordforge_desktop_benchmark --long` (new): 50 keystrokes in a 32,000-paragraph
document through `PageView::insertText` plus a viewport paint.

| | Median keystroke ms | P95 ms |
|---|---:|---:|
| Before | 115.5 | 125–137 |
| After | 94–95 | 99–104 |

Timed separately, the editor's own work per keystroke after this change is:
- document copy: 0.0 ms
- edit: 0.6 ms
- incremental layout: 2.9 ms
- renderer sync: 0.2 ms

The remaining time per keystroke is outside the editor:
- **About 73 ms** in `contentChanged` listeners. The status bar recounts the words of the
  whole document on every edit; that count alone measures 70 ms at this size.
- The rest is the viewport paint.

This is not a P95 objective result: single runs on one machine, synthetic text.
