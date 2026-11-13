# Wordforge

A fast, native word processor for Linux, written in C++20 with Qt6, Skia,
HarfBuzz, FreeType and ICU. No browser engine, no HTML editor, no LibreOffice
underneath: the document model, page layout and rendering are its own.

**Status: developer preview (0.6).** It is usable for everyday writing, but it
is not yet a replacement for Microsoft Word or LibreOffice. Keep copies of
important documents and report anything that loses data.

## What works today

* **Writing and formatting** — fonts, sizes, bold/italic/underline/strikethrough,
  highlight and text colour, Normal/Title/Heading styles, alignment, bullets and
  numbering, indentation, line and paragraph spacing.
* **Pages** — real page layout with margins, A4/Letter, portrait/landscape and
  undoable page breaks (Ctrl+Enter). Zoom to 100% or page width.
* **Multilingual text** — HarfBuzz shaping and font fallback for scripts such as
  Arabic, Hebrew and Devanagari, input-method composition, and editing that never
  splits a grapheme.
* **Tables** — Insert → Table, add or delete rows and columns from the Format
  menu or panel, Tab/Shift+Tab between cells; saved natively and exported to DOCX
  and PDF.
* **Pictures** — insert PNG/JPEG, resize with aspect lock, save, recover and export.
* **Bookmarks** — named positions that follow edits (Ctrl+Shift+F5).
* **Find and replace** — Ctrl+F and Ctrl+H, fully undoable.
* **Spell check** — red underlines as you type, suggestions and "Add to dictionary"
  on right-click, using Hunspell and dictionaries on your computer (nothing is sent
  anywhere). Tools → Spelling language picks the dictionary.
* **Copy and paste with formatting** — fonts, styles, colours, headings and lists
  survive pasting from web pages, Word or LibreOffice, and copying out of Wordforge.
* **Printing** — File → Print (Ctrl+P) and Print preview, including page ranges.
* **Word count** — in the status bar (also for a selection) and Tools → Word count.
* **Files** — native `.wfg` documents, UTF-8 text, PDF export, and partial DOCX
  import/export (imports open as copies and report what was not understood; see
  [COMPATIBILITY_MATRIX.md](COMPATIBILITY_MATRIX.md)).
* **Recovery** — unsaved work is snapshotted every 30 seconds; after a crash,
  File → Recover drafts opens it. Up to 30 seconds of edits can be lost.
* **A quiet interface** — a menu bar, one compact toolbar, a Format panel for
  paragraph, picture and page settings, Ctrl+K to search every command, and F11
  focus mode that hides everything but the page. Light and dark themes.

## Not yet

Merged cells, custom column widths and table import from DOCX; headers and
footers, footnotes, comments, tracked changes, floating or wrapped pictures,
DOCX picture import, and preserving Word features Wordforge does not understand.
Visual caret movement through mixed-direction text and screen-reader
accessibility are incomplete. DOCX export refuses documents with bookmarks rather
than lose them.

## Install

Wordforge is not on the Snap Store yet. To try it, build from source
([BUILD.md](BUILD.md), and [docs/UBUNTU.md](docs/UBUNTU.md) for Ubuntu 24.04)
or build an unsigned local snap with [docs/SNAP.md](docs/SNAP.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Design notes are in
[ARCHITECTURE.md](ARCHITECTURE.md) and [DECISIONS.md](DECISIONS.md), testing in
[TESTING.md](TESTING.md), verified capability and known limits in
[CURRENT_STATUS.md](CURRENT_STATUS.md), and plans in [ROADMAP.md](ROADMAP.md)
and [ENGINEERING_ROADMAP.md](ENGINEERING_ROADMAP.md). Contributors picking up
in-progress work should start with [AGENT_HANDOFF.md](AGENT_HANDOFF.md).

## Licence

Apache License 2.0 — see [LICENSE](LICENSE). Bundled dependencies keep their own
licences; see [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
