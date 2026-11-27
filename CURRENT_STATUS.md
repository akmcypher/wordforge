# Current status

Condensed 2026-11-23. Dated entries before this, with their evidence, are in
`docs/history/status-to-2026-11-23.md`. `COMPATIBILITY_MATRIX.md` holds the
per-feature support table.

## Release state

| Channel | Version | Source | Notes |
|---|---|---|---|
| GitHub release (Windows x64/ARM64) | 0.6.14 | 38b5d00 | Released 2026-11-27, checksums verified |
| Snap stable (amd64 91 / arm64 90) | 0.6.14-preview | 38b5d00 | Promotion run 45882208032 |

Both channels read WFG1–19. Previous release: 0.6.13 (0baebd5). Release notes:
`docs/releases/v0.6.14.md`.

## In 0.6.15 (since 0.6.14; prepared, not yet published)

Release notes: `docs/releases/v0.6.15.md`. Version 0.6.15 / `0.6.15-preview`.

- **First-page headers and footers:** Word's "different first page" header and footer
  text is shown on the first page, kept when saving as DOCX, and can be edited in the
  Header and footer dialog. It used to be dropped with a warning.
- **Dashed, dotted and double table borders:** drawn as such (they used to be single
  lines with a warning), kept in DOCX and native files (WFG20), and chosen in Table
  borders. Other Word styles (wavy, triple, 3-D...) still warn and are drawn as single
  lines.
- **Diagonal cell borders:** Word's diagonal lines across a cell are drawn and kept in DOCX
  and native files (WFG20) instead of being dropped with a warning. They can't be set in
  Wordforge yet.
- Native format WFG20 is written only for documents using these; it needs this newer
  reader.
- Evidence: `docs/evidence/2026-11-27-first-page-headers-border-styles.md`.

## In 0.6.14 (since 0.6.13)

Release notes: `docs/releases/v0.6.14.md`. Version 0.6.14 / `0.6.14-preview`.

- **Bookmarks, even-page headers and cell borders:**
  - Word bookmarks are read and saved as places or named ranges. Saving a document with
    bookmarks as DOCX no longer fails.
  - Even-page headers and footers are shown with Word's "different odd and even pages"
    switch, and can be edited in the Header and footer dialog.
  - Each side of a table cell can have its own line, from the cell, the table and the
    style's regions.
  - All 114 Word documents in the corpus now open without the import-limitations dialog,
    up from 88.
  - Native format WFG19 is written only for documents using these; it needs this newer
    reader.
  - Evidence: `docs/evidence/2026-11-26-bookmarks-even-headers-cell-borders.md`.

## In 0.6.13 (since 0.6.12)

Release notes: `docs/releases/v0.6.13.md`. Version 0.6.13 / `0.6.13-preview`.

- **Word table styles** applied on import (PR #59).
- **Word's style defaults kept** instead of warning on every DOCX (PR #61).
- **Contextual spacing and character spacing:**
  - Covered through styles, direct formatting, WFG18, DOCX both ways, layout and the
    Format panel.
  - 74 of 114 Word documents now open without the import-limitations dialog, up from 51.
  - Evidence: `docs/evidence/2026-11-24-contextual-character-spacing.md`.
  - WFG18 needs this newer reader.
- **List levels and numbering definitions:**
  - Nine list levels, each with its own format (1, a, A, i, I, bullets, none), label template
    (`%1)`, `%1.%2.`) and start.
  - Separate Word lists number separately, and nested levels restart under each parent.
  - Increase and Decrease indent move a list item between levels.
  - 88 of 114 Word documents now open without the import-limitations dialog.
  - Evidence: `docs/evidence/2026-11-24-list-levels.md`.

## In 0.6.12 (since 0.6.11; performance only)

- **Per-paragraph pagination keys and in-place checkpoints (PR #57):** a keystroke's
  layout no longer hashes every paragraph or copies every checkpoint. A 1000-page
  keystroke takes a median of 3.1–3.7 ms instead of about 5.6 ms.
  - Evidence: `docs/evidence/2026-11-23-keystroke-profile.md` (follow-up section).

## In 0.6.10 (since 0.6.9)

- **Widow/orphan control** (PR #44):
  - Covers styles, direct formatting, WFG16, DOCX and the Format panel.
  - Measured reservations work across columns, continuous sections and notes.
  - Evidence: `docs/evidence/2026-11-22-widow-orphan.md`.
- **Superscript/subscript** (PR #45):
  - Covers styles, direct formatting, WFG17, DOCX `vertAlign`, HTML clipboard, menu,
    panel, palette and shortcuts.
  - Drawn at 65% size with offsets that approximate Word's.
  - Evidence: `docs/evidence/2026-11-22-baseline-text.md`.
- **Manual verified Snap promotion workflow** (PR #46). Records: PR #47, `docs/SNAP.md`.

## In 0.6.11 (since 0.6.10; performance only)

- **Incremental pagination (Phase 2A, first slice; PRs #50, #51):** editing re-paginates from the
  checkpoint before the change and reuses the rest once pagination converges.
  - It matches a full-layout oracle across edits, keeps, widow/orphan control, notes,
    tables, sections and random edits.
  - At 1000 pages, layout after an edit takes 3.4 ms instead of 75 ms.
  - Page stamps let the renderer redraw only changed pages: sync after an edit takes
    0.27 ms instead of about 58 ms (`docs/evidence/2026-11-23-page-stamps.md`).
  - Evidence: `docs/evidence/2026-11-23-incremental-pagination.md`.
- **Copy-on-write paragraphs (PR #52):**
  - Copying a document or undo state shares its paragraphs; an edit clones only what it
    changes.
  - At 34,000 paragraphs, a model edit takes about 1 ms instead of 9–13 ms. Undo and redo
    take under 0.01 ms instead of about 20 ms, and peak memory is 36 MiB instead of 126 MiB.
  - Typing in a 1000-page document now spends about 3.7 ms in the editor.
  - Evidence: `docs/evidence/2026-11-23-shared-paragraphs.md`.
- **Incremental word count (PR #53):**
  - `DocumentCounter` recounts only the paragraphs an edit changed, for the status bar
    and the Word Count dialog.
  - A keystroke in a 1000-page document, including the viewport paint, takes about 25 ms
    instead of 95 ms.
- **Page positions in O(1) (PR #54):**
  - Spell-check underlining had been summing every earlier page's height for each line
    (17.6 ms of a 1000-page keystroke).
  - The keystroke now takes about 5.6 ms. The same keystroke takes a median of 181–185 ms in 0.6.10.
  - Evidence: `docs/evidence/2026-11-23-keystroke-profile.md`.

## Verification

- Main 76e32e6 (the 0.6.11 code): all six CTest suites pass on Windows ARM64 Release and
  Ubuntu ARM64 (WSL), and CI passed on PR #54 (2026-11-23).
- Main 56ab612, fresh Windows ARM64 Release build: all six CTest suites pass (2026-11-23).
- Feature code 66ce15f: all six suites pass on real Ubuntu ARM64 (WSL).
- Snap revisions 60/59 passed installed strict-confinement self-tests.
- **Not verified:**
  - No Word automation result for widow/orphan or superscript/subscript.
  - Offscreen tests don't prove screen-reader behaviour or installed-desktop behaviour.

## Not done

- Background pagination, renderer and model copy costs per edit (rest of M2).
- Numbering through styles, row splitting, theme fonts, language and OpenType controls.
- Border styles other than single, dashed, dotted and double lines; setting a cell's own
  borders in the editor.
- Fuller DOCX fidelity (formatting revisions, unknown body content is omitted with
  a warning, notes and comments are plain text).
- Balanced columns and per-section headers.
- Accessibility proof and code signing.

This is a developer preview, not a production Word replacement.
