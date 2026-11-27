# First-page headers, border styles and diagonal borders (2026-11-27)

These three gaps were listed as known limitations of 0.6.14: first-page header text wasn't
shown, dashed, dotted and double borders were drawn as single lines, and diagonal cell
borders weren't drawn.

## What changed

- **First-page headers and footers (PR #69):**
  - `PageStyle` gains `firstHeader` and `firstFooter`. With `differentFirstPage` on, the
    document's first page shows them; without text it shows none, as before.
  - DOCX reads `w:headerReference w:type="first"` (and the footer). The bands are kept
    only when the section has `w:titlePg`, as in Word. They used to be dropped with a
    warning. Export writes them as `headerFirst.xml`/`footerFirst.xml`.
  - The Header and footer dialog has first-page rows, enabled by the renamed "Different
    header and footer on the first page" switch.
  - Wordforge's first page is the document's, which in Word is the first section's title
    page. Later sections' title pages aren't distinguished.
- **Dashed, dotted and double borders (PR #70):**
  - `TableBorder` gains a `BorderStyle`: single, dashed, dotted or double.
  - DOCX reads `dashed`, `dashSmallGap`, `dotted` and `double`; `single` and `thick` stay
    single. Other styles (wave, triple, 3-D...) still warn and draw as single lines.
  - A double border's width covers both lines and the gap, so it is three times Word's
    `w:sz` (each line's size). Export divides it back.
  - The renderer draws dashes three widths long (at least 3 points), dots one width
    apart, and a double line as two lines a third of the width each.
  - Table borders has a Style list.
- **Diagonal cell borders (PR #71):**
  - `CellSide` gains `DiagonalDown` (top left to bottom right) and `DiagonalUp`. Only the
    cell sets them; `borderAt` never takes them from the table's border or a neighbour.
  - DOCX reads `w:tl2br`/`w:tr2bl` from a cell's `tcBorders` and the style's region parts,
    and writes them after the four sides. They used to be dropped with a warning.
  - Layout gives each cell rectangle six lines; the renderer draws the diagonals corner to
    corner with butt ends. They can't be set in the editor yet.
- **Native format:** WFG20 adds `<firstHeader>`/`<firstFooter>`, `borderStyle` on `table`,
  `style` on `cellBorder` and the `diagonalDown`/`diagonalUp` sides. It is written only for
  documents that use them. See `docs/WFG_SCHEMA.md`.

## Verification

- **Tests:** all six CTest suites pass on Windows ARM64 (Release, offscreen), and CI passed
  on PRs #69–#71 (Ubuntu x64, Windows ARM64). New tests:
  - `FirstPage.WordFirstPageHeadersRoundTrip`
  - `NativeFormat.FirstPageHeadersNeedWfg20Only`
  - `Layout.FirstPageShowsItsOwnHeaderAndFooter`
  - `DesktopTest::headerFooterDialogSetsFirstPageBands`
  - `CellBorders.WordDashedDottedAndDoubleLinesRoundTrip`
  - `NativeFormat.BorderStylesNeedWfg20Only`
  - `Rendering.DashedAndDoubleBordersLeaveGaps` (pixel runs along and across the line)
  - `CellBorders.WordDiagonalCellBordersRoundTrip`
  - `NativeFormat.DiagonalCellBordersNeedWfg20Only`
  - `Rendering.DiagonalCellBordersCrossTheCell`
  - the Table borders dialog test now also sets the double style
- **Corpus:** all 115 Word documents (the 114 of 0.6.14 plus one) still open without the
  import-limitations dialog.
- **Benchmark:** 1000-page keystroke, three alternating runs each on a ~50% busy machine:
  0.6.14 median 2.74–3.16 ms (p95 3.5–4.2) against 0.6.13 3.13–3.74 ms (p95 4.4–12.9).
  The 0.6.15 changes don't touch the typing path or the benchmark document's features.
- **Not verified:** saved files weren't opened in Word, and the drawn dashes and double
  lines weren't compared with Word's rendering.
