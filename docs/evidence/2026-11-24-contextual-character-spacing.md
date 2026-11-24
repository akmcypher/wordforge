# Contextual spacing and character spacing (2026-11-24)

## What changed

- **Contextual spacing** (Word's "Don't add space between paragraphs of the same
  style", `w:contextualSpacing`) is a paragraph property that follows the style cascade,
  with direct bit 10. A paragraph that has it leaves out its space before when the
  previous paragraph has the same style, and its space after when the next one does.
  Paragraphs count as alike when they have the same style and sit in the same container:
  the body or the same table cell.
- **Character spacing** (`w:rPr/w:spacing`) is a character property in points, -1584 to
  1584, with character bit 9. The shaper adds it after the last glyph of each cluster, and
  turns off `liga`/`clig` ligatures for spaced text, as Word does. Tab leaders and list
  markers are not spaced.
- **Native:** WFG18 adds paragraph/style attribute `contextual` and run/style attribute
  `spacing`. It is written only when a document uses either option. See `docs/WFG_SCHEMA.md`.
- **DOCX:** both options are read from document defaults, styles (including table styles)
  and direct formatting, and written back in schema order: `w:contextualSpacing` after
  `w:ind`, and run `w:spacing` after `w:color`. They are no longer kept-but-not-shown style
  properties.
- **Format panel:** a "No space between same-style paragraphs" checkbox and a "Character
  spacing" box (-20 to 100 pt).
- **Pagination keys:** each paragraph's key includes whether it drops its space before or
  after. Changing a neighbour's style therefore recomposes from the paragraph before it.

## Verification

- All six CTest suites pass on Windows ARM64 (Release, offscreen), including these new
  tests:
  - layout: `Spacing.ContextualSpacingDropsSpaceOnlyNextToTheSameStyle`,
    `Spacing.CharacterSpacingFollowsEachCharacterWithoutLigatures`
  - incremental layout: `IncrementalLayout.ContextualAndCharacterSpacingMatchFullLayout`
    (incremental layout is compared against a full layout)
  - native: `Spacing.NativeKeepsContextualAndCharacterSpacingInWfg18`
  - DOCX: `Docx.ReadsShowsAndSavesContextualAndCharacterSpacing`
  - desktop: `contextualAndCharacterSpacingApplyFromPanelAndPersist`, which covers undo and
    saving as WFG and DOCX
- **Word corpus (114 documents made by Word):**
  - 74 now open without the import-limitations dialog, up from 51 after PR #61 and none
    before it.
  - 112 page counts are unchanged.
  - Two documents with spaced lists are one page shorter: 086 (4 → 3 pages) and 096 (3 → 2).
  - The most common remaining warnings are custom list labels (18), even-page headers (12),
    adjacent list restarts (10), per-cell borders (10) and nested lists (8).
- **Not verified:**
  - Word's own page counts for these documents. The Word automation harness recorded 1 page
    for every file, so its page column can't be used.
  - Ubuntu, which runs in CI only.
  - Word's exact layout for spacing around tables and section breaks. Wordforge treats a
    table cell as its own container and ignores section boundaries.
