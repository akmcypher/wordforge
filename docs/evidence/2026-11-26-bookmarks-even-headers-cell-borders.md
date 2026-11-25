# Bookmarks, even-page headers and cell borders (2026-11-26)

These were the last three warnings the Word corpus raised: even-page headers (12
documents), borders on single cells (10) and bookmarks (5).

## What changed

- **Bookmarks:**
  - A bookmark is a place, as before, or now a named range (`Bookmark::end`). Typing at
    a range's edges grows it, as with comments. Go to selects the range.
  - DOCX import reads `w:bookmarkStart`/`w:bookmarkEnd` inside paragraphs and between
    blocks, table rows and cells. A start between paragraphs goes to the next paragraph's
    start, and an end to the end of the paragraph before it. A start and end with nothing
    between them make a place.
  - Repeated names, unusable names and ends without a start are left out with a warning.
  - DOCX export writes bookmarks. Saving a document with bookmarks as DOCX used to fail.
    Names Word would reject (spaces, a leading digit, more than 40 characters, a clash)
    are renamed, and links to them follow.
  - The Bookmarks dialog hides Word's hidden `_` bookmarks (`_Toc…`, `_GoBack`) unless
    "Hidden bookmarks" is ticked, as Word does.
- **Even-page headers and footers:**
  - `PageStyle` gains even-page header and footer bands and Word's "different odd and
    even pages" switch. With the switch on, pages 2, 4, ... show the even-page bands.
  - DOCX reads `w:headerReference w:type="even"` (and the footer) and the settings'
    `w:evenAndOddHeaders`. That switch is no longer stripped from the kept settings; it's
    written back from the page settings, and a new settings part is created when needed.
    Even-page bands without the switch are ignored, as in Word.
  - The Header and footer dialog has even-page rows and the switch.
- **Cell borders:**
  - Each side of a cell can have its own line (`Table::cellBorders`; width 0 means no
    line). Otherwise the side uses the table's border.
  - `Table::borderAt` resolves the line on a side. Of the cell's line and its neighbour's
    line on the shared side, the wider wins, as Word settles conflicts.
  - Layout gives each cell rectangle four sides. The renderer draws each side as its own
    line.
  - Inserting or deleting rows and columns moves the borders with their cells. Merging
    keeps the top-left cell's borders. Choosing the table's border in the Table border
    dialog clears the cells' own borders, so the table looks as chosen.
  - DOCX import resolves each side in Word's layering order:
    1. the style's table borders
    2. the table's own borders
    3. the style's parts for the cell's region (whole table, row and column bands, first
       and last rows and columns, corners), with `insideH`/`insideV` on inner sides
    4. the cell's own `tcBorders`

    The most common line becomes the table's border, and the other sides are stored
    per cell. This replaces the single border width and colour per table, and its
    warnings for cell and style borders.
  - Border styles other than single lines are drawn as single lines, with a warning.
    Diagonal borders aren't drawn, also with a warning.
  - DOCX export writes the table's border as `tblBorders` and each cell's own sides as
    `tcBorders`.
- **Native format:** WFG19 (not yet released) adds the even-page bands and their switch,
  bookmark range ends and `<cellBorder>` elements. It is written only for documents that
  use them. See `docs/WFG_SCHEMA.md`.

## Verification

- **Tests:** all six CTest suites pass on Windows ARM64 (Release, offscreen). New tests:
  - `Bookmarks.WordRangesAndPlacesRoundTripWithWordNames`
  - `Bookmarks.NamesWordWouldRejectAreRenamedWithTheirLinks`
  - `EvenPages.WordEvenPageHeadersAndTheSwitchRoundTrip`
  - `Layout.EvenPagesShowTheirOwnHeaderAndFooter`
  - `CellBorders.WordCellAndStyleBordersShowSideBySideAndRoundTrip`
  - `CellBorders.EditingMovesThemWithTheirCellsAndTheTableBorderReplacesThem`
  - the desktop bookmark test, which now saves and reopens a DOCX with bookmarks
- **Word corpus (114 documents made by Word):** all 114 open without the
  import-limitations dialog, up from 88. No document's paragraph, table, word or page
  counts changed.
- **1000-page keystroke** (`wordforge_desktop_benchmark --long`, alternating runs against a
  build of `main` at 4ffabe2, the 0.6.13 code): not measured reliably. Other processes kept
  the machine at about 74% CPU, so both builds took a median of 5–11 ms, against the 3.5 ms
  measured for 0.6.13. Within that noise, no difference stood out between the two builds.
  The benchmark document has no tables, bookmarks or headers. On its keystroke path, this
  change only copies and hashes a few more empty header fields.
- **Not verified:**
  - Word's own rendering of the saved files, because Word automation still doesn't work
    here.
  - Ubuntu, which runs in CI only.
  - The drawn borders in the desktop app, beyond the layout and renderer tests.
