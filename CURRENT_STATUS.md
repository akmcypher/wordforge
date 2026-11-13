# Current status

2026-11-13 — on the Snap Store edge channel: `sudo snap install wordforge --edge`
(amd64 revision 3, arm64 revision 2). See docs/SNAP.md → Publication record.
A real-desktop strict-confinement test (live USB) is still pending.

2026-11-13 — public repository. Development moved here from the private Meridian
repository (exported at 2dd5b05 as one commit). The first hosted CI run passed both
jobs (Ubuntu x64 build, tests and amd64 Snap artifact; Windows ARM64 tests), the
first amd64 build of the project. The amd64 Snap has not been installed yet.

2026-11-13 — appearance and Linux UI fixes (user-reported from the WSL window):

* View → Theme: Match system / Light / Dark, saved in the app config folder.
  "Match system" reads the Windows setting, Qt 6.5+ colour scheme, or the
  freedesktop portal on Linux (Ubuntu's Qt 6.4 does not); the portal path is not
  yet verified on a real GNOME desktop. Forced themes start from Fusion's palette.
* Disabled controls are greyed again (the palette had overwritten every colour
  group); spin boxes keep Fusion's arrows; the font box names an uninstalled
  family (e.g. Arial on Ubuntu) instead of showing another font; toolbar icons
  are redrawn on theme change; sample text no longer mentions a Layout tab.
* "Insert and Format menus open together" reproduced only under WSLg. On a plain
  X11 server (Xvfb + xdotool) one menu is open at a time. The toolbar's Insert
  button now has its own menu instead of sharing the menu bar's QMenu, a likely
  trigger for WSLg's popup handling. Real-desktop confirmation is pending.

2026-11-13 — release-blocking features for the open-source preview:

* Printing: File → Print (Ctrl+P) and Print preview rasterize each page through
  the shared Skia scene at up to 300 dpi; page ranges and current page work. Test
  prints to PDF and counts pages. The Snap bundles Qt's CUPS plugin and declares
  the `cups` plug; it did not auto-connect on WSL (no CUPS there), so printing from
  the installed Snap is unverified.
* Word count: status bar shows "N of M words" for selections; Tools → Word count
  (Ctrl+Shift+G) lists words, characters with/without spaces, paragraphs, pages.
* Tables (step 5 of the minimal UI): Insert → Table dialog, Format → Table and a
  Format panel Table section; Tab/Shift+Tab between cells, Tab in the last cell
  adds a row in one undo step; pictures fit the cell width. An exported DOCX was
  read by python-docx 1.1.0 as one 2×2 table with the typed text. No Word or
  LibreOffice visual check yet.
* Formatted copy/paste: HTML on copy; HTML paste via QTextDocument keeps fonts,
  sizes, bold/italic/underline/strike, colours, highlights, alignment, spacing,
  headings and lists as one undo step. Near-white colours from dark web themes
  are dropped. Pictures and tables in pasted HTML become plain paragraphs.
* Spell check: Hunspell with local dictionaries (system, Snap bundle, MSYS2, or
  WORDFORGE_DICTIONARIES), wavy underlines skipping the word being typed, right-
  click suggestions / Add to dictionary (personal list in app data) / Ignore,
  Tools → Check spelling as you type (F7) and Spelling language. Words in another
  script, with digits, in capitals or single letters are not checked.
* 100 cases: Ubuntu ARM64 100 pass; Windows ARM64 99 pass + one Ubuntu-only skip.
  Evidence: docs/evidence/2026-11-13-*-release-features-tests.txt. The arm64 Snap
  repacked with Hunspell, en_US dictionary and the print plugin, installs and runs
  on WSL (partial confinement).

2026-11-13 — all installed fonts:

* Windows now offers every font in %WINDIR%\Fonts and the per-user font folder
  (previously 13 hard-coded files). Linux also reads ~/.fonts, and inside a Snap
  the host's fonts (/var/lib/snapd/hostfs/usr/share/fonts) and the real home's
  fonts, not only the bundled DejaVu/Noto set. Faces are catalogued at startup
  and opened only when shaping needs them; a missing family falls back to Arial,
  Liberation Sans, DejaVu Sans, Noto Sans or Times New Roman, in that order.
* Ubuntu ARM64 all 5 suites pass; Windows ARM64 all pass (one Ubuntu-only skip).
  Evidence: docs/evidence/2026-11-13-*-all-fonts-tests.txt. The Snap host-font
  path is not yet tested in an installed Snap.

2026-11-13 — open-source release preparation:

* Public-facing README (what works, what does not, install, licence).
  `tools/export-standalone.py` exports wordforge/ as a new repository staged
  for a single commit with no enclosing history (the author identity is left to
  the publisher); an export of 6ba45f8 built and passed all five CTest suites on
  Ubuntu ARM64 on its own; `.github/workflows/ci.yml` is the
  standalone copy of the CI workflow (free runners on a public repository).
* Snap: fixed the bundled LICENSE path for a standalone checkout and the stale
  "single ribbon" description. The arm64 Snap installs and runs under snapd on
  WSL (partial confinement only); see docs/SNAP.md. Strict confinement on a real
  Ubuntu desktop and an amd64 build are still required before an edge upload.

2026-11-13 — ribbon removed (step 2 of the minimal interface):

* Deleted `apps/desktop/ribbon.*`, the View → Classic ribbon (legacy) toggle and its
  styles. The menu bar, compact toolbar, Format panel and Ctrl+K palette are the only
  interface. Ribbon-only tests now drive the same commands through toolbar buttons,
  the panel, menu actions and the style picker. Focus mode hides menu bar, toolbar
  and panel. Next: floating selection toolbar (3), slash commands (4), table
  controls in the panel and Insert menu (5).
* 92 cases: Ubuntu ARM64 92 pass; Windows ARM64 91 pass + one Ubuntu-only skip.
  Evidence: docs/evidence/2026-11-13-*-ribbon-removal-tests.txt.

2026-11-13 — minimal interface, step 1 of the ribbon replacement (user decision):

* New default shell: menu bar (File, Edit, Insert, Format, View, Help), one compact
  toolbar row, a right-hand Format panel (text, paragraph, picture and page settings)
  and a Ctrl+K command palette. All surfaces share one command registry
  (`apps/desktop/commands.*`); shell widgets live in `apps/desktop/shell.*`.
* Every former ribbon command is reachable from the menus (parity regression), and
  the Format panel adds a "Default · 2 cm" margin matching new documents.
  Focus mode hides menu bar, toolbar and panel and restores the panel's last state.
* The classic ribbon is hidden behind View → Classic ribbon (legacy) until step 2
  removes it. Floating selection toolbar and slash commands are steps 3–4.
* 92 cases: Ubuntu ARM64 92 pass; Windows ARM64 91 pass + one Ubuntu-only skip.
  Evidence: docs/evidence/2026-11-13-*-minimal-shell-tests.txt.

2026-11-13 — table navigation fixes (core only; desktop table controls still unfinished):

* insertTable at a paragraph start no longer leaves an empty leading paragraph,
  and still splits after an existing table so every table keeps its body paragraph
  (regression for table-directly-after-table insertion added).
* Backtab in the first cell stays inside the table; Tab/Backtab walk neighbouring
  paragraphs instead of scanning the document. `wordforge_transaction_benchmark
  --tables`: 20,000 cell moves in a 34,000-paragraph document took 0.11 ms;
  row/column insertion still copies the document (~20 ms) on Windows ARM64.
* 88 cases: Windows ARM64 87 pass + one Ubuntu-only skip; Ubuntu ARM64 88 pass.
  Evidence: docs/evidence/2026-11-13-*-table-navigation-tests.txt.

2026-11-13 — approved native UI polish: standardized ribbon spacing and icons, larger captions, quieter checked states, matching input/focus treatment. Existing group order and commands retained. All five Windows/Ubuntu suites pass. See docs/UNSLOP_POLISH.md. Table desktop controls remain unfinished.

2026-11-13 — saved table foundation, desktop feature unfinished:

* Typed Table/TableCell membership in the existing paragraph authority; complete
  row-major cells contain multiple editable paragraphs. Core insert/add/delete
  rows/columns/remove-table/Tab commands, grouped undo and bookmark repair tested.
* Layout uses equal-width columns, wrapped text, max-height rows and page breaks
  between rows. Borders render through Skia/PDF; hit testing chooses actual cells.
  Over-height rows reject. Per-cell decimal lists restart at1; review regression fixed.
* WFG5 ZIP preserves table metadata/cell membership plus images and bookmarks;
  WFG1–4 remain readable. DOCX exports real tables, paragraphs and pictures in cells.
* 85 cases excluding Qt setup/cleanup; Ubuntu85 pass, Windows84 pass +1 font skip.
  All five suites pass. Evidence: 2026-11-13-*-table-foundation-*.
* **No Insert Table dialog or ribbon controls yet.** User requested this handoff
  before completion. Pending desktop regression is saved in the table plan, not
  included as a passing or skipped feature test. See docs/TABLE_CHECKPOINT.md.
  DOCX table import, merge/split/custom widths/repeated headers/row splitting remain
  pending. This checkpoint does not complete tables, full Insert, or Store readiness.

2026-11-13 — 0.6 inline pictures:

* Insert → Picture opens a PNG/JPEG chooser; Picture size resizes with aspect lock.
  Typed text/image inlines share the existing editor, shaping, layout and renderer.
  Selection, deletion, undo, bookmarks, native save/reopen and recovery retain images.
* Immutable shared PNG resources avoid copying encoded bytes into each undo snapshot.
  Caller input is frozen on insertion. Deleted images are pruned; native/DOCX exports
  omit orphan resources. Renderer validates full pixel decode before publication.
* WFG4 ZIP carries document.xml and referenced PNG parts. WFG1/2/3 remain readable;
  documents without pictures still write WFG2/3 XML. PDF uses actual image pixels;
  DOCX export writes DrawingML, media relationships and physical dimensions.
* Twelve new regressions bring coverage to 76 cases across five executables.
  Windows ARM64: 75 pass, one Ubuntu-only skip; Ubuntu ARM64: all 76 pass.
  Evidence: docs/evidence/2026-11-13-*-pictures-*. Independent python-docx 1.1.0
  verified one exported inline shape at 144x72 points and exact PNG media bytes.
  Native Windows UI inserted the user's local screenshot and resized 360→300 pt
  with height following 74.8→62.3 pt. No Word/LibreOffice visual round trip claimed.
* Preview limits: 16 MiB per encoded PNG/package, 64 MiB expanded package/resource
  total, 25 million pixels per image, dimensions ≤16384 pixels; physical 1–2880 pt
  further constrained by page layout. UI normalizes JPEG/PNG through QImageReader.
* Remaining Insert work: tables; hyperlinks; headers/footers/page numbers; text
  boxes/shapes; comments; equations; cover pages and other advanced objects.
  DOCX image import, floating/wrap/crop, replacement and image clipboard are pending.
  Full typed blocks/sections, incremental pagination and Store readiness remain open.

2026-11-12 — 0.5 Phase 1B anchor/bookmark slice:

* Added bookmark metadata with globally unique document IDs, stable paragraph-ID
  anchors, grapheme-safe UTF-16 offsets and before/after affinity. Edits transform
  registered anchors; deletion collapses them at the replacement boundary; reorder
  and undo preserve identity. Detached Anchor values are not live tracking handles.
* Insert → Bookmark (Ctrl+Shift+F5) opens native add/go-to/delete controls.
  Navigation uses shared caret-style lookup and commits/clears IME state. Model
  commands, undo, dirty state, recovery and native save/open retain bookmarks.
* Writer emits WFG3 when bookmarks exist, otherwise WFG2; reads WFG1/2/3.
  Invalid/dangling/duplicate anchors are rejected. DOCX bookmark import warns;
  export of native bookmarks is refused before replacing an existing file.
* Per-operation boundary caches avoid scanning a long paragraph per bookmark.
  Model benchmark (100k characters, 1000 bookmarks): saved Windows single edit
  3.42 ms / grouped 5.32 ms; Ubuntu single 3.76 ms / grouped 4.86 ms. Isolated model observations
  exclude layout/render/disk; no percentile or general SLA claim.
* Six new cases bring coverage to 64 across five suites. Ubuntu ARM64 runs 64;
  Windows ARM64 runs 63 plus one Ubuntu-only skip. All suites pass on both.
  Evidence: docs/evidence/2026-11-12-*-bookmarks-*.
* Only the anchor/bookmark slice is complete. Typed blocks/inlines, resources,
  sections, semantic styles, tables/images and incremental pagination remain open.

2026-11-12 — 0.4.1 Phase 1A implemented:

* Shared document/style validation at import/reset/write and grouped transaction
  boundaries; checked allocation and editor high-water state prevent retired ID reuse.
* WFG2 persists exact uint64 paragraph IDs/nextId; WFG1 still migrates losslessly.
  New saves are not readable by older binaries. See docs/WFG_SCHEMA.md and legacy fixture.
* Editor::transact stages grouped commands, validates before one undo publication,
  preserves state/history on failure and returns changed/inserted/removed IDs plus
  geometry/order flags. insertPageBreak uses this boundary. Index shifts preserve
  untouched measurement-cache revisions. Stable index-independent selection awaits 1B.
* Six added regression cases bring the suite to 58 cases, across five suites.
  Windows ARM64 runs 57 with one Ubuntu-only skip; Ubuntu ARM64 runs 58. All suites pass.
  Evidence: docs/evidence/2026-11-12-*-phase1a-*.
* New model-only 34,000-paragraph benchmark: saved Windows observation single edit 6.72 ms,
  grouped 23.39 ms, undo 7.91 ms, redo 8.89 ms, peak 58.32 MiB; Ubuntu single edit 5.84 ms,
  grouped 34.19 ms, undo 11.32 ms, redo 8.56 ms, peak 56.79 MiB. No layout/render/disk;
  single observations, not latency distributions or improvement claims.
* Grouped editing still copies/full-validates/scans the document; undo still retains
  full snapshot contents. This is a tested identity/transaction foundation, not
  completion of typed model-v2, persistent storage or incremental pagination.
* Next milestone: Phase 1B typed document blocks/inlines/resources/anchors, then
  semantic styles 1C and page checkpoints 2A. Check Git log for the published checkpoint.

2026-11-12 — 0.4 DOCX increment and phased handoff:

* Added bounded in-memory OPC ZIP reading/writing with libzip, namespace-aware
  OOXML parsing, relationship-resolved main/styles/numbering parts, inherited
  formatting, supported run/paragraph properties, simple lists and page setup.
* File Open accepts DOCX as an unsaved copy. Unsupported content prompts before
  opening; cancellation/failed import preserves the current document. Save As
  offers DOCX; QSaveFile still provides the atomic publication boundary.
* Seven DOCX regressions plus a desktop open/export test bring the suite to 52
  cases (excluding Qt init/cleanup). All five suites pass on Windows ARM64 and
  Ubuntu ARM64; Windows skips the Ubuntu-only font fixture. Evidence filenames
  include `2026-11-12-*-docx-*`.
* Imported an independent python-docx 1.1.0 fixture. python-docx also opened an
  exported round trip and verified Unicode, bold/italic, numbering and margins.
  This is not an executed Microsoft Word/LibreOffice visual round trip.
* Review findings about default-style booleans, inherited numbering, style-toggle
  semantics and complex-script emphasis were reproduced and corrected.
* DOCX is partial: no unknown-part preservation registry; unsupported tables,
  drawings, fields, revisions and section features are omitted only after an
  explicit warning. Source files stay unchanged. Styles are flattened into the
  existing paragraph/run model, not retained as editable inheritance semantics.
* Document model v2, incremental pagination, resources, tables/images, professional
  review features and production distribution remain unimplemented. See
  COMPATIBILITY_MATRIX.md. New directive assessment does not mark these complete.
* Full directive saved in docs/ENGINEERING_DIRECTIVE.md; ENGINEERING_ROADMAP.md,
  AGENT_HANDOFF.md and PERFORMANCE_BASELINE.md document the phased work, exact
  environment, acceptance gates and next milestone. No model-v2 scaffold is
  advertised as implemented. Next execution starts at roadmap1A.

2026-11-12 — 0.3.1 Snap preview preparation:

* Warm brown/taupe palette, including active ribbon controls, selection and caret.
* Atomic 30-second recovery snapshots, per-window ownership locks, abandoned-draft
  restore as an unsaved document, cleanup after formatted save/explicit discard.
  Last 30 seconds may be lost. A killed-child-process test verifies stale-lock recovery.
* 44 cases across five suites: Ubuntu runs all 44; Windows runs 43 and skips the
  Ubuntu bitmap-font fixture. All five suites passed on both systems.
* Native arm64 Snap packed with Qt, fonts, XKB/Compose data and dependency notices;
  unpacked payload launched on X11/WSLg. Actual installed-Snap checks are blocked
  by Snap Store connectivity; WSL also has only partial confinement. See docs/SNAP.md.
* Not uploaded or published. Native amd64 packaging is configured in CI but unrun.

2026-11-11 — 0.3 single ribbon and native Ubuntu validation:

* Consolidated the menu/quick-toolbar stack into one Word-style ribbon: File,
  Home, Insert, Layout and View. Home has labeled Clipboard, Font, Paragraph,
  Styles and Editing groups; compact save/undo/redo share the tab row.
* Added bullets/contiguous decimal lists, indentation, line/paragraph spacing,
  Normal/Title/Heading presets, highlighting, strikethrough, symbols, current
  date insertion and Ctrl+Enter page breaks. Properties persist in `.wfg`;
  formatting and breaks participate in the existing undo transactions.
* Lists continue on Enter and exit on an empty item; headings return to Normal
  after Enter at their end. Applying presets preserves explicit page boundaries.
* Built Google Skia m143 and the actual Linux executable from source on Ubuntu
  24.04.5 ARM64 under WSL2, using Qt 6.4.2, GCC 13 and native Linux libraries.
  All five CTest suites passed, and the X11/WSLg window was launched/captured.
* Linux execution exposed and resolved the Skia FreeType scanner API and bitmap
  font-strike sizing. An installed Noto Color Emoji fixture exercises real bitmap
  shaping at requested point sizes. Its targeted test skips on Windows.
* Current suite: 40 cases excluding Qt init/cleanup; Ubuntu runs all 40, Windows
  runs 39 with the Ubuntu-only font fixture skipped. See TESTING.md and evidence.
* Ubuntu x86_64 CI is configured; it has not run remotely. Linux packaging, a
  standalone Ubuntu desktop session and other Unix variants still need validation.

Earlier executed milestones follow.

2026-11-11 — 0.2 responsiveness and writing-interface update:

* Viewport page raster cache and independent selection overlay; cursor blinks
  invalidate only the caret rectangle. Navigation no longer rerasterizes pages.
* Refined native toolbars, active formatting states, save status and F11 focus mode.
* Ctrl+F/Ctrl+H exact, case-sensitive search/replacement within paragraphs, wrap
  search, Unicode boundary filtering, undoable replacement and Escape-to-close.
* Release launcher default and Windows GUI executable without an extra console.
* 28 test cases across five suites; desktop interactions also exercised at 150%
  scaling. The core 1,000-page benchmark remains a separate workload.
* Observed native benchmark entry-to-first-widget-paint: 143.55 ms. Offscreen
  100-caret-frame workload: 36.92 ms with zero page rasterizations, down from
  67.74 ms and 100 rasterizations before this change. Timing scope is documented
  in docs/PERFORMANCE.md; these are not cold file-open or screen-presentation SLAs.

The original foundation milestone and remaining larger product work follow.

2026-11-11 — native foundation built and executed on Windows ARM64.

Implemented and exercised:

* Native Qt6 Widgets executable, Home/Layout/View controls, native file dialogs,
  menus, keyboard commands, mouse selection, clipboard and input-method preedit.
* Independent paragraph/run model, stable IDs, formatting, grapheme-aware edits,
  atomic candidate-model publication and shared immutable undo/redo snapshots.
* HarfBuzz shaping, FreeType font loading, ICU segmentation and bidirectional
  runs, fallback fonts, Latin/Arabic/Devanagari tests and Hebrew sample text.
* Physical page geometry, Unicode wrapping, half-inch tab stops, pagination and
  paragraph measurement reuse. Forward page composition still visits all blocks.
* Skia retained per-page pictures, exact scene keys, glyph/typeface caching and
  viewport-sized CPU surfaces. No whole-document raster bitmap.
* Versioned native `.wfg` XML and UTF-8 TXT import/export; atomic native saves.
  PDF export uses the same retained glyph scene and configured JPEG callbacks.
* Real Windows platform launch/capture at 125% scaling; offscreen desktop tests.
* Debug and Release CMake/Ninja builds. Five CTest executables cover 24 test cases
  excluding Qt setup/cleanup: model (9), layout/typography (4), formats (3),
  rendering (1), desktop (7). See TESTING.md for execution scope.
* Independent review found six correctness issues; regressions reproduce them,
  fixes pass, and the follow-up review confirmed all six resolved.
* 1/100/500/1000-page Release workloads executed; see docs/PERFORMANCE.md.
* A dedicated Windows ARM64 CI workflow is configured but has not run remotely.

Still required for the requested final product:

* Linux distribution packaging, native desktop integration beyond WSLg, x86_64
  execution evidence and validation of other Unix systems.
* GPU backend/context validation; rendering currently uses Skia CPU only.
* DOCX package parser/serializer and unsupported-OOXML preservation; ODT and RTF.
* Tables, images, sections, headers/footers, multilevel lists, widow/orphan/keep
  constraints, footnotes, references, comments, tracked changes and comparison.
* Cross-paragraph/regex/batch search, outline, document tabs, autosave/crash recovery, rich clipboard,
  visual bidi caret movement, arbitrary IME replacement ranges and accessibility.
* Fuzzing, sanitizers, interoperability/golden corpus and broad performance tests.

The 1,000-page corpus is synthetic text, not evidence of complex-document or
Word interoperability. Undo still snapshots document storage, and page composition
is O(document size). This is a working foundation, not a production-grade Word
replacement. No unsupported features are presented as working UI commands.
