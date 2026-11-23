# Current status

Condensed 2026-11-23. Dated entries before this, with their evidence, are in
`docs/history/status-to-2026-11-23.md`. `COMPATIBILITY_MATRIX.md` holds the
per-feature support table.

## Release state

| Channel | Version | Source | Notes |
|---|---|---|---|
| GitHub release (Windows x64/ARM64) | 0.6.10 | 7eedc2b | Released 2026-11-23, checksums verified |
| Snap stable (amd64 64 / arm64 63) | 0.6.10-preview | 7eedc2b | Promotion run 45070162245 |

Both channels read WFG1–17. Previous release: 0.6.9 (d7afd47).

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

## Since 0.6.10 (branch `feat/incremental-pagination`)

- **Incremental pagination (Phase 2A, first slice):** editing re-paginates from the
  checkpoint before the change and reuses the rest once pagination converges.
  - It matches a full-layout oracle across edits, keeps, widow/orphan control, notes,
    tables, sections and random edits.
  - At 1000 pages, layout after an edit takes 3.4 ms instead of 75 ms.
  - Page stamps let the renderer redraw only changed pages: sync after an edit takes
    0.27 ms instead of about 58 ms (`docs/evidence/2026-11-23-page-stamps.md`).
  - Evidence: `docs/evidence/2026-11-23-incremental-pagination.md`.
- **Copy-on-write paragraphs (branch `feat/cow-document`):**
  - Copying a document or undo state shares its paragraphs; an edit clones only what it
    changes.
  - At 34,000 paragraphs, a model edit takes about 1 ms instead of 9–13 ms. Undo and redo
    take under 0.01 ms instead of about 20 ms, and peak memory is 36 MiB instead of 126 MiB.
  - Typing in a 1000-page document now spends about 3.7 ms in the editor.
  - Evidence: `docs/evidence/2026-11-23-shared-paragraphs.md`.
- **Incremental word count (branch `feat/incremental-word-count`):**
  - `DocumentCounter` recounts only the paragraphs an edit changed, for the status bar
    and the Word Count dialog.
  - A keystroke in a 1000-page document, including the viewport paint, takes about 25 ms
    instead of 95 ms.

## Verification

- Main 56ab612, fresh Windows ARM64 Release build: all six CTest suites pass (2026-11-23).
- Feature code 66ce15f: all six suites pass on real Ubuntu ARM64 (WSL).
- Snap revisions 60/59 passed installed strict-confinement self-tests.
- **Not verified:**
  - No Word automation result for widow/orphan or superscript/subscript.
  - Offscreen tests don't prove screen-reader behaviour or installed-desktop behaviour.

## Not done

- Background pagination, renderer and model copy costs per edit (rest of M2).
- Numbering definitions, table styles/row splitting, theme fonts, typography controls.
- Fuller DOCX fidelity (formatting revisions, unknown body content is omitted with
  a warning, notes and comments are plain text).
- Balanced columns and per-section headers.
- Accessibility proof and code signing.

This is a developer preview, not a production Word replacement.
