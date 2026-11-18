# Wordforge — a fast, free, offline word processor for Linux and Windows

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/wordforge)
[![Latest release](https://img.shields.io/github/v/release/akmcypher/wordforge?label=Windows%20download)](https://github.com/akmcypher/wordforge/releases/latest)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

Wordforge is an open-source word processor and DOCX document editor: write letters,
reports, essays and assignments, open and save Microsoft Word `.docx` files, and export
PDF. It is a lightweight, private alternative to Microsoft Word, LibreOffice Writer and
Google Docs for everyday writing: no account, no cloud, no telemetry, nothing sent
anywhere.

It is native code (C++20 with Qt6, Skia, HarfBuzz, FreeType and ICU), not a web page in
a browser wrapper and not LibreOffice underneath: the document model, page layout and
rendering are its own, so it starts fast and stays responsive.

**[Website](https://akmcypher.github.io/wordforge/)** ·
**[Download for Windows](https://github.com/akmcypher/wordforge/releases/latest)** ·
**[Install on Ubuntu](https://snapcraft.io/wordforge)** ·
**[Report a problem](https://github.com/akmcypher/wordforge/issues)**

**Status: developer preview (0.6).** It is usable for everyday writing, but it is not
yet a full replacement for Microsoft Word or LibreOffice. Keep copies of important
documents and report anything that loses data.

## What works today

* **Writing and formatting** — fonts, sizes, bold/italic/underline/strikethrough,
  highlight and text colour, alignment including justified text, first-line, hanging
  and right indents, bullets and numbering, line and paragraph spacing.
* **Styles** — Normal, Title, Headings and your own paragraph and character styles,
  with based-on inheritance. Update a style from your text and every paragraph using
  it follows.
* **Pages** — real page layout with margins, A4/Letter, portrait/landscape, page
  breaks, headers and footers with page numbers, and footnotes.
* **Review** — comments and tracked changes (accept or reject), saved to and from Word.
* **Tables** — insert, add and delete rows and columns, merged cells, column widths,
  borders and cell shading.
* **Pictures** — insert PNG/JPEG, resize with aspect lock; pictures in Word files open.
* **Microsoft Word files** — opens `.docx`, Strict Open XML, `.docm` (macros removed),
  `.dotx` and `.dotm` as editable copies and saves `.docx`. Measured against Word on 114
  test documents: text, paragraph formatting and page counts match; see
  [docs/WORD_FIDELITY.md](docs/WORD_FIDELITY.md).
* **Every language** — HarfBuzz shaping and font fallback for Hindi and other Indic
  scripts, Arabic, Hebrew, Thai, Chinese, Japanese, Korean and emoji; plain text in
  UTF-8/16/32 and legacy encodings (Shift_JIS, GB18030, Big5, EUC-KR, Windows code
  pages, KOI8-R) is detected automatically; see
  [docs/FORMAT_COMPATIBILITY.md](docs/FORMAT_COMPATIBILITY.md).
* **Spell check** — red underlines as you type and suggestions on right-click, using
  Hunspell dictionaries on your computer.
* **Copy and paste with formatting** — from web pages, Word or LibreOffice and back.
* **Find and replace, bookmarks, word count, printing and PDF export.**
* **Recovery** — unsaved work is snapshotted every 30 seconds and offered after a crash.
* **A quiet interface** — a menu bar, one toolbar, a Format panel, Ctrl+K to search
  every command, F11 focus mode, light and dark themes.

## Not yet

Word 97–2003 `.doc`, RTF and OpenDocument `.odt` files; endnotes, fields such as a
table of contents, hyperlink targets, sections and columns, floating or wrapped
pictures, equations and citations. Visual caret movement through mixed-direction text
and screen-reader accessibility are incomplete. Details are in
[COMPATIBILITY_MATRIX.md](COMPATIBILITY_MATRIX.md) and
[CURRENT_STATUS.md](CURRENT_STATUS.md).

## Install

**Ubuntu and other Linux distributions with snaps** (amd64, arm64):

```sh
sudo snap install wordforge
```

**Windows 10 (1903 or later) and Windows 11**: download the installer for your PC from
the **[latest release](https://github.com/akmcypher/wordforge/releases/latest)** —
`windows-x64-setup.exe` for most PCs, `windows-arm64-setup.exe` for Arm laptops such as
Snapdragon. It needs no administrator rights. Portable ZIPs are there too: unzip and
run `Wordforge.exe`. The builds are not code-signed yet, so Windows SmartScreen may ask
you to confirm the first launch (More info → Run anyway). `SHA256SUMS.txt` lists the
checksums.

To build from source, see [BUILD.md](BUILD.md), [docs/UBUNTU.md](docs/UBUNTU.md) and
[docs/WINDOWS.md](docs/WINDOWS.md).

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
