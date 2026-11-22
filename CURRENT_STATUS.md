# Current status

Condensed 2026-11-23. Dated entries before this, with their evidence, are in
`docs/history/status-to-2026-11-23.md`. `COMPATIBILITY_MATRIX.md` holds the
per-feature support table.

## Release state

| Channel | Version | Source | Notes |
|---|---|---|---|
| GitHub release (Windows x64/ARM64) | 0.6.9 | d7afd47 | Reads WFG1–15 |
| Snap stable (amd64 60 / arm64 59) | 0.6.9-preview label | 53f8fdc | Includes WFG16/17 features |
| In preparation | 0.6.10 | `release/v0.6.10` | Realigns both channels |

## Implemented since 0.6.9 (on main)

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

## Verification

- Main 56ab612, fresh Windows ARM64 Release build: all six CTest suites pass (2026-11-23).
- Feature code 66ce15f: all six suites pass on real Ubuntu ARM64 (WSL).
- Snap revisions 60/59 passed installed strict-confinement self-tests.
- **Not verified:**
  - No Word automation result for widow/orphan or superscript/subscript.
  - Offscreen tests don't prove screen-reader behaviour or installed-desktop behaviour.

## Not done

- Incremental/background pagination (M2).
- Numbering definitions, table styles/row splitting, theme fonts, typography controls.
- Fuller DOCX fidelity (formatting revisions, unknown body content is omitted with
  a warning, notes and comments are plain text).
- Balanced columns and per-section headers.
- Accessibility proof and code signing.

This is a developer preview, not a production Word replacement.
