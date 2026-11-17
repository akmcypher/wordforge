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

2026-11-17, Microsoft Word 16.0 (build 16.0.20527), 114 documents:

| Measure | First run | Page breaks, fonts, colour | Indents, justification |
|---|---|---|---|
| Opened in Word after Wordforge saved them | 114/114 | 114/114 | 114/114 |
| Body text kept | 100% | 100% | 100% |
| Header, footer, note and comment text kept | 95.9% | 95.9% | 95.9% |
| Paragraph formatting the same | 90.6% | 90.7% | 100% |
| Character formatting the same | 0% | 89.5% | 89.5% |
| Same page count | 86.8% | 97.4% | 100% |
| Structure counts unchanged | 97.9% | 97.9% | 97.9% |
| Score | 79/100 | 95/100 | 97/100 |

Fixed from the first run: Word page breaks and next-page section breaks became plain
paragraph breaks; theme fonts (Aptos) became Arial; automatic text colour became #202631.
Then first-line, hanging and right indents (including each list level's indent from Word's
numbering definitions) and justified text, which had made lists match 52%, indents 64%,
multilevel lists 67% and justified paragraphs 80%.

Still lost: heading theme colours and some title fonts (character formatting 89.5%);
endnotes, fields (table of contents, page numbers inside text), hyperlink targets and
section settings (orientation, columns). Nested list levels keep their indent but show one
marker style.

Other formats and languages: see [FORMAT_COMPATIBILITY.md](FORMAT_COMPATIBILITY.md).
