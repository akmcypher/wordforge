# List levels and numbering definitions (2026-11-24)

## What changed

- **Model** (`core/document/lists.*`):
  - Every list paragraph has a level (0–8).
  - It may also name a `ListDefinition`, a document-wide list like a Word numbering
    instance. Each of the nine levels has a format (decimal, lower/upper letter,
    lower/upper Roman, bullet, none), a label template of at most 32 characters, where
    `%1`–`%9` stand for level numbers, and a start (0–32767).
  - A paragraph without a definition uses Wordforge's own list of its kind: bullets • ◦ ▪,
    or numbers 1. a. i. repeating by level.
  - Neither field is part of styles.
- **Numbering** (`ListNumbering`) runs in document order:
  - A definition's paragraphs are numbered together through the whole document.
  - Wordforge's own lists restart after a paragraph outside a list and in each table cell.
  - A level restarts after a paragraph at a shallower level of the same list.
  - Two Word lists next to each other therefore number separately.
- **Layout:**
  - Labels are computed in one pass before pagination, since they never depend on page
    breaks.
  - The running list number in the pagination state is gone. Each paragraph's pagination
    key includes its label, so a renumbered paragraph is recomposed.
  - The pass is skipped for documents without lists.
- **Editing:**
  - Increase and Decrease indent on a list item also move it a level down or up, as in Word.
  - The bulleted and numbered list commands switch a paragraph to Wordforge's own list.
  - Pasting a list item into a document without its definition does the same.
  - Enter on an empty item leaves the list.
- **Native format:** WFG18 (not yet released) gains top-level `<list>` elements and the
  paragraph attributes `listId` and `level`.
- **DOCX import:**
  - Reads every level of every abstract list, `w:num` instances with `startOverride` and
    `lvl` overrides, and each paragraph's `ilvl`.
  - Symbol and Wingdings private-use bullets become their Unicode look; unknown ones show
    as • with a warning.
  - Other level options (alignment, number spacing, unknown formats, level fonts beyond
    `rFonts`, level paragraph options beyond indents and tabs) warn by name.
  - The level's `pStyle` back-reference is ignored silently.
- **DOCX export:** writes Wordforge's own bullet and number lists with nine levels, and
  one abstract list with a single instance per definition. All abstract lists come before
  the first instance, as the schema requires.

## Verification

- **Tests:** all six CTest suites pass on Windows ARM64 (Release, offscreen). New tests:
  - `Lists.NumbersLevelsFormatsAndRestartsLikeWord`
  - `Lists.ValidatesLevelsDefinitionsAndPastedReferences`
  - `Lists.NativeKeepsDefinitionsAndLevelsInWfg18`
  - `Lists.LayoutShowsEachLevelsLabel`
  - `IncrementalLayout.ListLevelsAndDefinitionsMatchFullLayout` (compared against a full
    layout)
  - `Docx.ReadsWordListLevelsLabelsBulletsAndRestarts`, covering nested lettered levels,
    `%1)` labels, a start override, Symbol bullets and a round trip
  - `Docx.WarnsOnlyForListOptionsThatAreApproximated`
  - the desktop test `indentingAListItemMovesItALevelAndPersists`, covering WFG and DOCX
    saves and undo
- **Word corpus (114 documents made by Word):**
  - 88 open without the import-limitations dialog, up from 74.
  - No list warnings remain, and no page counts changed.
  - The remaining warnings are even-page headers (12 documents), per-cell borders (10) and
    bookmarks (5).
- **1000-page keystroke** (`wordforge_desktop_benchmark --long`, alternating runs against a
  build of `main` at 40f7221 on the same machine): a median of 3.45–3.50 ms, against
  3.34–3.37 ms for `main`.
- **Not verified:**
  - Word's own rendering of the saved lists. Word automation still doesn't work here.
  - Ubuntu, which runs in CI only.
- **Still not done:**
  - Numbering attached to styles (heading numbering) is read through the style's `numPr`,
    but styles themselves don't carry a list definition.
  - Wordforge's own lists still restart after a paragraph outside the list.
