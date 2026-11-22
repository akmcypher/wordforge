# Microsoft Word fidelity

How Wordforge's DOCX support is measured against real Microsoft Word, and the latest
results. The tools are in `tools/fidelity`; they need Windows with Microsoft Word.

## Method

1. `make-word-corpus.ps1` drives Word through COM automation to build a reproducible
   corpus (seeded). Every file is written by Word itself. The 114 documents cover each
   feature alone in three variants plus mixed documents: text, headings and title, character
   formatting, bullet/numbered and multilevel lists, alignment, indents, spacing, page
   breaks, tables (plain, merged, shaded, borderless), PNG and JPEG pictures, footnotes,
   endnotes, comments, tracked changes, headers/footers with page numbers, section breaks,
   two columns, a table of contents, hyperlinks and 12-chapter documents.
2. `wordforge_roundtrip` (built with the benchmarks) opens each DOCX exactly as the
   application does (pictures converted with Qt), saves it back to DOCX and reports what it
   understood, its warnings and its own page count.
3. `word-fidelity.ps1` opens the original and Wordforge's file in Word and compares:
   whether Word opened it; body text and header/footer/note/comment text kept (word
   multisets); counts of tables, pictures, footnotes, endnotes, comments, revisions,
   sections, fields and hyperlinks; per paragraph the style, alignment, left/first-line/right
   indents, spacing and line spacing; for up to six words per paragraph the font, size,
   bold, italic, underline, strikethrough, colour and highlight; and the page count. It
   writes `results.csv`, `report.md` and per-document `.diff.txt` files listing every
   difference.

```powershell
powershell -ExecutionPolicy Bypass -File tools\fidelity\make-word-corpus.ps1
powershell -ExecutionPolicy Bypass -File tools\fidelity\word-fidelity.ps1 -Tool build\<dir>\wordforge_roundtrip.exe
```

The score is the share of files Word opened times the mean of six measures (body text,
other text, paragraph formatting, character formatting, structure counts, page count). It
compares Word's view of both files, not pixels; visual comparison of exported PDFs is still
to do. The corpus is generated, not collected: it shows how Wordforge handles what Word
writes, not every document people have.

## Results

Release 0.6.9 was rechecked on 2026-11-20 with Microsoft Word 16.0 (build
16.0.20527), using all 114 generated documents. Word opened all outputs; body/other
text, paragraph formatting, structural counts and Word page counts matched at
100%; sampled character formatting matched at 99.5%. The rounded score remains
100/100. These structural samples do not establish pixel parity or full DOCX
preservation. See `docs/evidence/2026-11-20-release-0.6.9.md`.

Earlier runs, 2026-11-17–18, Microsoft Word 16.0 (build 16.0.20527), 114 documents:

| Measure | First run | Page breaks, fonts, colour | Indents, justification | Styles, theme colours, links, endnotes | Fields | Sections |
|---|---|---|---|---|---|---|
| Opened in Word after Wordforge saved them | 114/114 | 114/114 | 114/114 | 114/114 | 114/114 | 114/114 |
| Body text kept | 100% | 100% | 100% | 100% | 100% | 100% |
| Header, footer, note and comment text kept | 95.9% | 95.9% | 95.9% | 100% | 100% | 100% |
| Paragraph formatting the same | 90.6% | 90.7% | 100% | 100% | 100% | 100% |
| Character formatting the same | 0% | 89.5% | 89.5% | 99.5% | 99.5% | 99.5% |
| Same page count | 86.8% | 97.4% | 100% | 100% | 100% | 100% |
| Structure counts unchanged | 97.9% | 97.9% | 97.9% | 99.2% | 99.7% | 100% |
| Score | 79/100 | 95/100 | 97/100 | 100/100 (rounded) | 100/100 (rounded) | 100/100 (rounded) |

Fixed from the first run: Word page breaks and next-page section breaks became plain
paragraph breaks; theme fonts (Aptos) became Arial; automatic text colour became #202631.
Then first-line, hanging and right indents (including each list level's indent from Word's
numbering definitions) and justified text, which had made lists match 52%, indents 64%,
multilevel lists 67% and justified paragraphs 80%.

Then Word's own Normal, Title and Heading definitions (Wordforge had kept its own, so
paragraph marks turned Arial bold) and theme colours (accent1 with a shade became a plain
colour), which together made character formatting match 89.5%; hyperlink targets; and
endnotes (dropped before).

Then fields (tables of contents, PAGEREF, PAGE), which were dropped in 9 documents.

Then sections (orientation and equal columns), checked on 2026-11-18 in the existing
`out/fidelity/run9/report.md`: all structure counts and Word page counts match.
This checks Word's round-trip interpretation, not Wordforge's visual pagination.

Still limited: per-section headers, unequal margins and columns, column balancing, and
nested list marker styles. A few sampled words still differ, so "100/100" remains rounded.

Other formats and languages: see [FORMAT_COMPATIBILITY.md](FORMAT_COMPATIBILITY.md).
