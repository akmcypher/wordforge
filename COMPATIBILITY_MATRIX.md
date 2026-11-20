# Actual capability and compatibility

2026-11-13. Starting published baseline: 32b1461. The 0.4 DOCX/handoff commits extend it.
These are capability statements, not production-readiness checkmarks.

2026-11-19 branch addition (PR #40, unmerged): left/centre/right paragraph tab stops
and dots through styles, WFG14 and DOCX; TOCs use right-aligned page numbers.
Custom automatic intervals stay in DOCX settings but display at 36 pt with a warning.
Decimal/bar tabs, other leaders, RTL-specific geometry and a ruler are unsupported.
Windows six-suite and six-document Word structural checks passed; interactive/visual
parity remains unverified. See `docs/evidence/2026-11-19-tabs-acceptance.md`.

| Area | Starting baseline | Current additions through 0.6 | Major remaining requirement |
|---|---|---|---|
| Native stack | C++20, Qt6, Skia, HarfBuzz, FreeType, ICU | libzip for DOCX | Preserve architecture while extending semantics |
| Model | Paragraphs/runs, in-session IDs, revisions, undo | Durable IDs, grouped transactions, bookmarks, typed text/image runs and shared PNG resources | Typed sections/blocks, remaining inline kinds, style cascade |
| Layout | Wrapping, pages, measurement cache, caret mapping | No layout v2 | Incremental checkpoints/pagination, tables, sections, background cancellation |
| Typography | Real multilingual shaping and fallback | OOXML formatting mapping | Language/feature controls, variable fonts, full visual navigation validation |
| Native format | WFG1, Unicode/style round trips, atomic save | WFG11 indents/justification; WFG2 IDs; WFG3 bookmarks; WFG4 PNG ZIP; WFG1/2/3/4/5 readers; WFG5 table foundation | Rich-tree schema and resource integrity |
| DOCX | Absent | Transitional and Strict OOXML, .docm/.dotx/.dotm; left, first-line/hanging and right indents (list levels from numbering) and justified text both ways; text/styles (custom paragraph and character styles both ways)/lists/page setup, editable-copy import; DrawingML PNG and table export; table import and export with merged cells, column widths, one border width/colour per table and cell shading (nested flattened, ≤10 columns) and picture import (any Qt format → PNG, floating placed inline) ; default header/footer text with PAGE/NUMPAGES fields both ways; document properties, theme, settings, font table and custom XML kept byte for byte; footnotes, endnotes and comments both ways (plain text); hyperlinks (web/mail/bookmark targets) both ways; Word theme colours (themeColor/Shade/Tint) and Word's own Normal/Title/Heading definitions kept; tracked insertions and deletions of text both ways (w:ins/w:del, moves as insert/delete) with the Track Changes setting fields (TOC, PAGEREF, PAGE, HYPERLINK...) kept as anchored ranges with their result text and a TOC that Wordforge can rebuild; sections with their own size, orientation, margin and equal columns (next-page and continuous starts) both ways | Body-level unknown content, tracked paragraph-mark, formatting and table changes, formatted footnotes/comments, comment replies, per-cell and styled borders, table-style formatting, row heights, floating pictures, sections, even/first-page and formatted headers/headers/fields/revisions, real Word/LO visual corpus |
| Editing | Ribbon, basic formatting, simple lists, search/replace, plain clipboard | DOCX workflow; bookmarks; PNG/JPEG insert, aspect resize, delete/undo, save/reopen; table controls, merged cells, column widths; HTML copy/paste; Hunspell spell check; printing; word count | Drag-resize columns, row heights, floating/wrap/crop, grammar |
| Advanced content | Absent | Absent | Comments, track changes, TOC, footnotes, equations, citations |
| Rendering | Retained CPU pages, raster cache, overlays, PDF | Existing renderer reused | Printing, background rendering, richer document objects |
| Reliability | Atomic saves, basic 30s snapshots, recovery locks | Import-copy and failed-open tests | User-document locking, backup retention, configurable limits, fault campaigns |
| Accessibility | Native widgets and basic keyboard/IME | No new accessibility subsystem | Screen-reader document semantics and assistive-tech testing |
| Performance | Synthetic core/layout/render/desktop benchmarks | No DOCX benchmark yet | Real corpus, P95/P99, full editing/open/save targets |
| Tests | Five suites; 44 cases | 85 cases total, DOCX/identity/transaction/anchor regressions | Fuzz/property/visual/performance regression corpus |
| Distribution | ARM64 dev builds; unsigned ARM64 Snap packed, unpacked launch | Packaging recipe updated, not rebuilt as 0.4 | Installed/full-confinement proof, x64 platforms, signed installers, updates |

The core contains useful building blocks. It does not implement the master
directive's production-grade model, layout, interoperability or feature scope.
