# Actual capability and compatibility

2026-11-13. Starting published baseline: 32b1461. The 0.4 DOCX/handoff commits extend it.
These are capability statements, not production-readiness checkmarks.

2026-11-19 addition (PR #40, merged in v0.6.9): left/centre/right paragraph tab stops
and dots through styles, WFG14 and DOCX; TOCs use right-aligned page numbers.
Custom automatic intervals stay in DOCX settings but display at 36 pt with a warning.
Decimal/bar tabs, other leaders, RTL-specific geometry and a ruler are unsupported.
Windows six-suite and six-document Word structural checks passed; interactive/visual
parity remains unverified. See `docs/evidence/2026-11-19-tabs-acceptance.md`.
2026-11-20 branch addition: paragraph keep-with-next and keep-lines in the style
cascade, native WFG15, DOCX and Format panel. Explicit style/direct overrides retain
their presence, including false; inherited paragraphs remain inherited after DOCX
export. Text grouping handles columns, continuous sections and footnote reservation.
Table-boundary keep-next stays preserved with an import warning; it is not laid out.
Incremental/background pagination remains unsupported. The tabs/keeps integration
shipped in v0.6.9. See the pagination evidence.

2026-11-22 feature branch: widow/orphan control follows paragraph styles, direct
formatting, WFG16, DOCX and the Format panel. Native defaults stay off; DOCX omission
through the entire hierarchy defaults on, and export explicitly represents the native
default. Measured line/note reservations protect opening and closing pairs where
feasible; explicit breaks and feasible keep groups take priority. Three-line paragraphs
stay whole when possible. Impossible constraints relax. Rows remain unsplit; table-boundary
keeps, incremental/background layout and full Word visual parity remain open.
This feature has not been publicly released. See `docs/evidence/2026-11-22-widow-orphan.md`.

2026-11-22 typography feature: normal/superscript/subscript through paragraph and
character styles, direct formatting, WFG17 and DOCX vertAlign; Format menu/panel,
shortcuts, undo and HTML clipboard. Stored font size stays nominal; display uses
65% scaling with a -0.38 em/+0.20 em baseline offset. This is an approximation,
not Word visual certification. Arbitrary numeric baseline shifts, language/feature
controls and variable-font configuration remain open. WFG17 needs the newer reader.
See `docs/evidence/2026-11-22-baseline-text.md`. Not publicly released.

| Area | Starting baseline | Current additions through 0.6 | Major remaining requirement |
|---|---|---|---|
| Native stack | C++20, Qt6, Skia, HarfBuzz, FreeType, ICU | libzip for DOCX | Preserve architecture while extending semantics |
| Model | Paragraphs/runs, in-session IDs, revisions, undo | Durable IDs, grouped transactions, bookmarks, typed text/image runs and shared PNG resources | Typed sections/blocks, remaining inline kinds, style cascade |
| Layout | Wrapping, pages, measurement cache, caret mapping | No layout v2 | Incremental checkpoints/pagination, tables, sections, background cancellation |
| Typography | Real multilingual shaping and fallback | OOXML formatting mapping | Language/feature controls, variable fonts, full visual navigation validation |
| Native format | WFG1, Unicode/style round trips, atomic save | WFG11 indents/justification; WFG2 IDs; WFG3 bookmarks (WFG19: bookmark ranges, even-page headers, cell borders; WFG20: first-page headers); WFG4 PNG ZIP; WFG1/2/3/4/5 readers; WFG5 table foundation | Rich-tree schema and resource integrity |
| DOCX | Absent | Transitional and Strict OOXML, .docm/.dotx/.dotm; left, first-line/hanging and right indents (list levels from numbering); list numbering definitions both ways (nine levels with decimal, letter, Roman, bullet and none formats, label templates such as `%1.%2.`, starts and start overrides, separate lists numbered separately, Symbol/Wingdings bullets shown as Unicode; other level options warn) and justified text both ways; text/styles (custom paragraph and character styles both ways)/lists/page setup, editable-copy import; DrawingML PNG and table export; table import and export with merged cells, column widths, a table border plus each cell side's own (from the cell, the table's borders and its style's regions; the wider line wins on a shared side) and cell shading (nested flattened, ≤10 columns); Word table styles applied on import (whole-table, banded, first/last row and column and corner shading, text and paragraph formatting, style borders, including those of its regions; saved back as direct formatting, the style name isn't kept); contextual spacing (no space between same-style paragraphs) and character spacing both ways; style and default options Wordforge doesn't read (kerning, ligatures, outline levels, borders and others in `pPr`/`rPr`) kept and written back into the same styles, warning only for those that change what is shown; a document grid without a type is ignored, as it changes nothing and picture import (any Qt format → PNG, floating placed inline) ; default, even-page and first-page header/footer text with PAGE/NUMPAGES fields both ways (with Word's "different odd and even pages" and "different first page" settings; the first page is the document's, Word's first section's title page); bookmarks (places and ranges, hidden `_` bookmarks hidden in the dialog) both ways, names Word would reject renamed with their links; document properties, theme, settings, font table and custom XML kept byte for byte; footnotes, endnotes and comments both ways (plain text); hyperlinks (web/mail/bookmark targets) both ways; Word theme colours (themeColor/Shade/Tint) and Word's own Normal/Title/Heading definitions kept; tracked insertions and deletions of text both ways (w:ins/w:del, moves as insert/delete) with the Track Changes setting fields (TOC, PAGEREF, PAGE, HYPERLINK...) kept as anchored ranges with their result text and a TOC that Wordforge can rebuild; sections with their own size, orientation, margin and equal columns (next-page and continuous starts) both ways | Body-level unknown content, tracked paragraph-mark, formatting and table changes, formatted footnotes/comments, comment replies, border styles other than single lines, diagonal cell borders, the table style name, row heights, floating pictures, sections, formatted headers, per-section headers/fields/revisions, real Word/LO visual corpus |
| Editing | Ribbon, basic formatting, multilevel lists (indent changes the level), search/replace, plain clipboard | DOCX workflow; bookmarks; PNG/JPEG insert, aspect resize, delete/undo, save/reopen; table controls, merged cells, column widths; HTML copy/paste; Hunspell spell check; printing; word count | Drag-resize columns, row heights, floating/wrap/crop, grammar |
| Advanced content | Absent | Absent | Comments, track changes, TOC, footnotes, equations, citations |
| Rendering | Retained CPU pages, raster cache, overlays, PDF | Existing renderer reused | Printing, background rendering, richer document objects |
| Reliability | Atomic saves, basic 30s snapshots, recovery locks | Import-copy and failed-open tests | User-document locking, backup retention, configurable limits, fault campaigns |
| Accessibility | Native widgets and basic keyboard/IME | No new accessibility subsystem | Screen-reader document semantics and assistive-tech testing |
| Performance | Synthetic core/layout/render/desktop benchmarks | No DOCX benchmark yet | Real corpus, P95/P99, full editing/open/save targets |
| Tests | Five suites; 44 cases | 85 cases total, DOCX/identity/transaction/anchor regressions | Fuzz/property/visual/performance regression corpus |
| Distribution | ARM64 dev builds; unsigned ARM64 Snap packed, unpacked launch | Packaging recipe updated, not rebuilt as 0.4 | Installed/full-confinement proof, x64 platforms, signed installers, updates |

The core contains useful building blocks. It does not implement the master
directive's production-grade model, layout, interoperability or feature scope.
