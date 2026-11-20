# Wordforge handoff — start here

## Current state (2026-11-20) — authoritative checkpoint

Release remains v0.6.8. Main is dd2730a (sections merged); the historical a19fa8c
and unfinished feat/sections notes below are superseded. PR #39 (toolbar/caret font)
and draft #40 (tabs/TOC leaders, 40b05bf) remain unmerged. Their work is intact.

Active branch: `codex/wordforge-paragraph-pagination`, based independently on main.
Plan: `docs/superpowers/plans/2026-11-20-paragraph-pagination.md`. Implements
keep-with-next and keep-lines through paragraph styles/direct formatting, WFG15,
DOCX and the Format panel. Text-group preflight uses measured lines and footnote
reservations. Explicit breaks win; infeasible groups split; feasible individual
keep-lines survives oversized chains. Columns/continuous sections are covered.
Table-boundary keep-next is preserved with a DOCX warning, not laid out.

WFG14 and mask bit 6 belong to PR #40; this independent main-based branch reserves
them and uses WFG15 / bits 7 and 8. Resolve version-reader and mask integration
when those branches are merged, then rerun all suites. Do not assign conflicting
meanings to those versions or advertise this branch as a combined preview.

Fresh Windows ARM64 Release build and six suites passed after independent review
fixes. Ubuntu ARM64 final rebuild and six suites also passed (7.17 s).
See `docs/evidence/2026-11-20-paragraph-pagination.md` for results.
The new controls were inspected in a native snapshot; interactive/accessibility
and full Word visual parity are not certified. Background/incremental pagination,
widow/orphan control and table-row chaining remain future work.

Owner explicitly deferred promotion until more features are ready. No main merge,
Snap upload/promotion, public release or app version bump is authorized by this task.
Continue focused PR increments; preserve running editors and use fresh build dirs.

## Historical checkpoint (2026-11-18)

**Released and live: v0.6.8** (tag `v0.6.8`, `main` a19fa8c).
- Windows: <https://github.com/akmcypher/wordforge/releases/tag/v0.6.8>. The x64 and ARM64
  `-setup.exe` installers (Inno Setup, per-user, unsigned), portable ZIPs and
  `SHA256SUMS.txt`.
- Snap: `latest/stable` is 0.6.8 (amd64 rev 51, arm64 rev 52); edge follows `main`.
- Website: <https://akmcypher.github.io/wordforge/>, from `site/` via
  `.github/workflows/pages.yml` on `main` pushes that touch `site/`. Repo description,
  20 topics and the homepage link are set. Not done (they need the user's accounts): Google
  Search Console with the sitemap, and the repo's social preview image.

**In progress, not merged: branch `feat/sections` (off main a19fa8c).** Commit 9cb7c0b,
"Add sections and columns" (WFG13), plus uncommitted doc updates: WFG_SCHEMA, AGENTS.md,
README, COMPATIBILITY_MATRIX, CURRENT_STATUS and this file. All 6 suites passed on Windows
ARM64 in `build\theme-1`. To finish it:
1. Read the Word harness result in `out/fidelity/run9/report.md`. It was running against the
   frozen tool `build\theme-1\roundtrip-sections.exe`. Before this branch, sections changed
   in 3 documents (055–057) and the structure score was 99.7%. Add a "Sections" column to
   the table in `docs/WORD_FIDELITY.md` and update its "Still lost" text.
2. Commit the docs, push, and open a PR into `main`. Merge only when CI is green and the
   user approves (see Working rules).
3. Not yet checked interactively in the app: try Insert > Section break, Columns in the
   Format panel, and a landscape section. `wordforge.exe --snapshot out.png file.docx`
   captures the window (used for `out/showcase/`).
- What it does: `Paragraph::sectionBreak` (`SectionBreak{PageStyle page; bool continuous}`)
  ends a section. `PageStyle` gains `columns` (1–6) and `columnGap`. Header, footer and
  first-page settings stay on `document.page` and must be empty in breaks.
  `sectionPage(doc, i)` gives a paragraph's section settings. `Editor::setPage` applies
  size, margins and columns to the caret's section and bands to the whole document.
  `insertSectionBreak(continuous)` always splits at the caret. `removeSectionBreak()`.
  When paragraphs merge, the surviving (last) mark keeps its break.
- Layout: `Layout::pageStyles` / `pageStyle(i)` hold each page's geometry. Text flows down
  each column, then across (`advance()`). A continuous break stays on the same page when
  the paper size matches. Columns are not balanced. Endnotes and footnotes use the full
  text width.
- PageView: `pageLeft(page)`, `pageTop(page)` (cumulative heights), `pageAtY`,
  `widestPage`. Print sets each page's layout; PDF uses each page's size.
- DOCX: paragraph `sectPr` and body `sectPr` both ways (pgSz/orient, pgMar as one margin,
  cols num/space, `type continuous`). Word stores how a section *starts* in its own `w:type`,
  so break k is continuous when section k+1's type is. Header references are written in every
  `sectPr`; `titlePg` only in the first one.
- WFG13: root `columns`/`columnGap`; `p` `section` = page|continuous plus
  `sectionWidth`/`Height`/`Margin`/`Columns`/`Gap`. WFG12 was released in 0.6.8, which is why
  these attributes are not in WFG12.

**In 0.6.8** (#35, #36, release #37): Word heading styles (Normal/Title/Heading1/2 are
redefined from the DOCX) and theme colours (`ThemeColor` on `CharacterStyle`, kept while
the colour is unchanged); hyperlinks (`Run::link`, Insert > Link Ctrl+Shift+K, Ctrl+click,
`validateLink` refuses unsafe schemes); endnotes (`NoteReference::endnote`, roman numerals,
laid out after the text); fields (`Document::fields`, anchored ranges, nest without crossing)
and the table of contents (Insert/Update table of contents, page numbers from a scratch
layout). Word harness: 100/100 rounded (99.6).
**Earlier:** 0.6.7 borders/shading, indents/justify, Strict/.docm/.dotx, any-encoding text;
0.6.6 tracked changes; 0.6.5 package preservation, footnotes, comments; 0.6.4 DOCX
tables/pictures, headers/footers, merges and widths.

**Next work, in order:**
1. Finish and merge `feat/sections`. Release 0.6.9 only when the user asks (same flow as
   0.6.8).
2. Two small UI flaws seen in the 0.6.8 screenshot: the toolbar labels are cut off
   ("I…t", "Fo…at", "Com…nds"), and the font box shows Arial for Word text in another font
   (the typing style at the caret?).
3. Fidelity follow-ups: right-aligned TOC page numbers with dot leaders (needs tab stops;
   `tabs` paragraph option unsupported), formatted footnotes/comments, comment replies,
   nested list marker styles, per-cell borders, floating pictures, unequal margins and
   columns, per-section headers, column balancing.
4. Interop: `.doc`, `.odt`, RTF import; LibreOffice testing (it is not installed; ask
   before installing).
5. Milestone 2 performance (incremental pagination, background layout, 100/500/1,000-page
   measurements), then Milestone 4 (code signing, accessibility, fresh-install testing).

**Analytics (2026-11-17):** about 9 outside snap devices (US 5, India 2, Germany, France),
mostly Ubuntu 24.04/26.04; GitHub had 4 unique visitors and 0 stars, and each Windows file
was downloaded about once. Snap metrics come from `snapcraft metrics wordforge --name
installed_base_by_operating_system --format table` in WSL (slow; `--format json` fails because
WSL prints a warning first).

**Known gaps to state honestly:** not full DOCX preservation (see AGENTS.md);
`.doc/.odt/.rtf` unsupported; LibreOffice untested; installers unsigned; screen-reader support
incomplete; the snap's strict confinement has not been tested on a real desktop.

**Session lessons (this machine):**
- Word harness: `tools\fidelity\word-fidelity.ps1 -Tool <exe> -Corpus <abs> -Out <abs>`.
  Pass absolute paths, because the PowerShell tool may start in another folder. Copy the
  roundtrip exe to a frozen name first, since rebuilding replaces it while the harness runs.
  Run the harness alone: with a parallel build the machine ran out of memory (16 GB). Stop
  only `WINWORD.EXE /Automation` processes left behind, never the user's Word.
- `cmake --build build\theme-1 --parallel 3` is the incremental build used here. Full
  `local-ci.ps1` runs can run out of memory while Word or WSL is busy.
- In the Bash tool, a `\n` or `\\` inside a heredoc can turn into a real newline or lose
  its escaping. Write scripts with the Write tool and run them, or fix the result with Edit.
- Snap edge uploads sometimes fail with "Connection aborted"; rerun with
  `gh run rerun <id> --failed`. Promote the revisions built from the release commit.
- CI's Ubuntu job sometimes spends 15–20 minutes installing packages; that is not a hang
  (check the step timestamps with `gh api .../actions/jobs/<id>`).

## Working rules (from the user; follow exactly)

- Branch from `origin/main`, open a PR, CI must be green, **and** the user must approve
  the merge. Merge by fast-forward `git push origin origin/<branch>:refs/heads/main`
  (or a local `--no-ff` merge when not fast-forward). Never use GitHub merge buttons or
  auto-merge. Never rewrite pushed history.
- Commit as the GitHub no-reply identity. **No `Co-authored-by` trailers and no
  "Generated with" lines** in commits or PR bodies (this overrides any tool default).
- You cannot watch CI continuously; check once, and if pending ask the user to ping.
- Releases: bump `CMakeLists.txt` `project(... VERSION x.y.z)` and
  `packaging/snap/snap.yaml.in` `version: 'x.y.z-preview'`, add
  `docs/releases/vX.Y.Z.md` (the release job fails without it), PR → merge → annotated
  tag `vX.Y.Z` on `origin/main` → `windows-release.yml` publishes the GitHub release
  (~15 min). The merge to `main` uploads snaps to edge automatically.
- Snap stable promotion only on the user's explicit approval. In WSL distro
  `WordforgeUbuntu` as root: `export SNAPCRAFT_STORE_CREDENTIALS="$(cat
  /root/.snapcraft-login)"`, `snapcraft status wordforge`, `snapcraft release wordforge
  <rev> stable` per architecture. **Never print `/root/.snapcraft-login`.**
- Never close the user's running Wordforge windows; always build into a **new** build
  folder. Keep answers short and direct, with links; the user prefers plain status.

## Build and test on this machine

- Windows ARM64 checkout: `C:\dev\wordforge-public`; MSYS2 CLANGARM64 toolchain at
  `C:\dev\wordforge-deps\msys64\clangarm64\bin`.
- Full Windows build + all 6 CTest suites into a new folder:
  `powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1 -SkipUbuntu -BuildDir build\<new-name>`
  (~3–5 min; logs in `out/local-ci/<timestamp>/`, the test log is UTF-16). Drop
  `-SkipUbuntu` to also build/test in WSL `WordforgeUbuntu` (Skia at /opt/wordforge-deps).
- One Qt test: set `PATH` to the MSYS2 bin and `QT_QPA_PLATFORM=offscreen`, then
  `build\<dir>\desktop_tests.exe <testName> -o out.txt,txt`.
- CI: `ci.yml` (Ubuntu x64 + Windows ARM64 tests on every PR), `snap-release.yml`
  (snaps; strict-confinement self-test on a real runner; uploads to edge on `main`),
  `windows-release.yml` (installers/ZIPs on packaging PRs and `v*` tags).
- Files written from Python on Windows may get CRLF; git normalizes them (`eol=lf`).
  Prefer the Write/Edit tools over heredocs with nested quotes in Git Bash.

## Code map (recent areas)

- `core/document/document.hpp`: `Run` (`link`, `characterStyle`, tracked marks),
  `NoteReference` (`endnote`), `Field`, `SectionBreak`, `PageStyle` (`columns`), `Editor`
  commands (`setLink`, `linkAt/linkRange`, `insertTableOfContents`, `updateTablesOfContents`,
  `insertSectionBreak`, `sectionPage`). Their implementations are near the end of
  `document.cpp`; footnotes are in `images.cpp` and anchors (including fields) in
  `anchors.cpp`.
- `core/document/styles.*`: `ThemeColor` (`activeTheme`, `themeColorText`), style
  resolution and masks.
- `core/layout/layout.cpp` `compose`: sections and columns (`useSection`, `advance`,
  `columnLeft`), footnotes at the page bottom, endnotes after the text, headers and footers
  per page. `core/typography/shaper.cpp`: note labels (`romanNumeral`).
- `core/formats/docx.cpp`: import `parseDocx` (`readGeometry`, `loadNotes`, the field marks
  `"\1f" + N` keys sharing `commentMarks`, `linkTargets`, built-in style redefinition) and export
  `serializeDocx` (`writeSection`, `writeNotes`, field markers in `emitMarkers`,
  `w:hyperlink` grouping, `writeColor`).
- `core/formats/native.cpp`: WFG versions (11 indents; 12 theme/link/endnote/fields;
  13 sections) and the "written only when used" rules.
- `apps/desktop/page_view.*`: per-page geometry, link open/tooltip/context menu, TOC and
  section commands. `commands.cpp`: dialogs (link, footnote/endnote) and command
  registration. `main_window.cpp`: menus. `shell.cpp`: the Format panel (Columns).
- `tools/fidelity/`: `word-fidelity.ps1` (114-document Word corpus in `out/fidelity/corpus`,
  report and per-document `.diff.txt`), `format-compat.ps1` (formats × 23 languages),
  `roundtrip.cpp`.

## Code map for the style work (0.6.3)

- `core/document/styles.*`: `StyleSheet` (paragraph + character styles), resolution,
  masks, `adoptStyles`, `uniqueStyleId`, `inheritedCharacter`.
- `core/document/document.*`: `Editor` operations (`applyStyle`, `createStyleFromSelection`,
  `applyCharacterStyle`, `createCharacterStyleFromSelection`, `renameStyle`,
  `deleteStyle`, `restyle`); invariant: every value not marked `direct` equals what the
  styles resolve to (`validateDocument` enforces it).
- `core/formats/native.cpp` (WFG1–7), `core/formats/docx.cpp` (styles.xml both ways).
- `apps/desktop/commands.cpp` (Format → Styles commands), `shell.cpp` (style box),
  tests in `tests/unit/style_test.cpp`, `docx_test.cpp`, `tests/integration/desktop_test.cpp`.

---

## Historical notes (before 2026-11-14; superseded where they conflict with the above)

Latest UI direction (2026-11-13, user decision): replace the ribbon with a minimal
shell — menu bar, compact toolbar, Format panel, Ctrl+K palette (step 1 done),
ribbon deleted (step 2 done), table controls in the panel/Insert menu (step 5
done 2026-11-13, with printing, spell check, formatted paste and word count), then add a floating selection toolbar (3), slash commands
(4) and put table controls in the panel/Insert menu (5). Ask the user before
adding features beyond this list. See CURRENT_STATUS.md first entry.

Latest appearance checkpoint: user-approved native ribbon polish, documented in docs/UNSLOP_POLISH.md; verified Windows output build/polish-preview. All five Windows/Ubuntu suites pass. This does not change the unfinished table-UI takeover work below.

## Latest takeover checkpoint — 2026-11-13 table foundation (unfinished feature)

The user explicitly requested saving all code and context before another agent
continues. Pictures are delivered in ff8d5d3. Subsequent table changes are a
buildable foundation, **not a delivered Insert Table UI**. Read
[docs/TABLE_CHECKPOINT.md](docs/TABLE_CHECKPOINT.md) first, then the table plan.
85 actual regression cases: Ubuntu ARM64 85 pass; Windows ARM64 84 pass + one
Ubuntu-only skip. All five executables pass on both; logs contain table-foundation.

Next action: restore the pending Qt test from the table plan, implement PageView
wrappers and the Table dialog/context controls, Tab/Backtab, picture sizing within
cells, desktop rollback tests and independent DOCX table parsing. Re-run both
platform suites and review before marking tables delivered. Keep pushing tested
features as requested. Do not start the next Insert feature before this is complete.

Latest checkpoint: **0.6 inline pictures**, updated 2026-11-13 (Asia/Calcutta).
Read CURRENT_STATUS.md's first entry before the historical 0.5/0.4 notes below.
76 cases: Ubuntu ARM64 76 pass; Windows ARM64 75 pass + one Ubuntu-only skip.
Evidence: docs/evidence/2026-11-13-*-pictures-*. Independent review found/fixed
orphan-image export retention and RTL image caret edges; targeted regressions pass.

Read `core/document/images.cpp`, `apps/desktop/pictures.*`, WFG_SCHEMA.md and
`docs/superpowers/plans/2026-11-12-inline-images.md`. Run is a text/image variant;
`text()` exposes an image as U+FFFC, but literal U+FFFC text is not an image.
Resources are immutable shared PNG blobs; layout retains owners; Skia fully decodes
before PageView commits a candidate. Delete prunes live resources while undo retains
old snapshots. Native/DOCX serialize referenced resources only. WFG4 is ZIP when
pictures exist; WFG2/3 XML otherwise. DOCX image export works, import still warns.

Windows current verified output: `build/images-final`; `build/images-preview` is
open with a disposable local-picture demo. Earlier `build/docx-preview` contains
an unsaved user document: never close it to unlock builds. Native Linux verified
output remains `/root/wordforge-build` in WordforgeUbuntu. Older versions/counts
below are historical. The user requires a tested commit/push for each feature.

Next increment: real tables (5A) with typed cell content, layout, selection/undo,
native persistence and export; plan its supported subset before implementing.
Other Insert features remain listed in CURRENT_STATUS and the roadmap. Do not add
placeholder buttons or claim full Word parity; picture wrap/crop/import are pending.

Latest checkpoint: 0.5 implements the Phase 1B anchor/bookmark slice; 64 cases now
cover native bookmarks/edit transforms/navigation/persistence plus earlier features.
All suites pass on Windows/Ubuntu ARM64; logs are `2026-11-12-*-bookmarks-*`.
Read anchors.cpp, bookmarks_dialog.* and the 1B anchor plan. WFG3 is conditional
on bookmarks; WFG1/2 remain readable. DOCX bookmark export refuses loss; import
warns. Typed tree/resources are still next; 52/58-case counts below are historical.

Repository: https://github.com/akmcypher/wordforge (public, `main` via pull
requests). Until 2026-11-13 this project lived in the private Meridian repository's
`wordforge/` folder on branch `codex/wordforge-native`; older notes below refer to
those paths.

## User intent and authorization

Build the native word processor in phases and publish current code and a durable
implementation plan so another agent can continue. Keep the C++/Qt/Skia stack,
a minimal menu/toolbar/panel interface (superseding the earlier single ribbon),
warm brown/taupe styling, real features and measured
responsiveness. The user rejected describing this prototype as a Word competitor
or Store-ready product. No `Co-authored-by` trailers. No rewriting published Git
history. Ordinary implementation and pushing this branch are authorized.

The user subsequently resumed autonomous phased implementation. The DOCX/handoff
checkpoint is published as 1116f20;0.4.1 now implements Phase 1A. Continue with 1B
after verifying this checkout. Do not infer that the full typed model v2 is done.

## Read in order

1. `CURRENT_STATUS.md` — implemented behavior and dated evidence.
2. `ENGINEERING_ROADMAP.md` — phase dependencies, files and acceptance criteria.
3. `COMPATIBILITY_MATRIX.md`, `ARCHITECTURE.md`, `PERFORMANCE_BASELINE.md`.
4. `docs/ENGINEERING_DIRECTIVE.md` — user's complete product directive, retained
   verbatim; no dependency on a chat attachment.
5. `DECISIONS.md`, `BUILD.md`, `TESTING.md`, `docs/UBUNTU.md`, `docs/SNAP.md`.

## Current code checkpoint

`32b1461` is the earlier published native/recovery baseline. The DOCX and handoff
commits following it contain this checkpoint; inspect `git log -5` and `git status`
instead of assuming an old SHA is latest. Check the remote branch before work.

0.4 adds `core/formats/zip_package.*` and `docx.*`, requiring libzip >=1.7.
Bounded in-memory OPC ZIP; namespace-aware XML; main/styles/numbering relationships;
Unicode/whitespace, common character/paragraph styles, simple numbering and page
geometry. Qt File Open imports a copy; unsupported-content warnings require consent.
Save As supports DOCX with atomic QSaveFile writes. It does not preserve unknown
parts, semantic style definitions, tables/images or advanced Word structures.

Seven DOCX regressions and a desktop workflow test were added. Review fixes:
legal default-style booleans, inherited numbering, custom label warnings,
style toggle semantics, and complex-script emphasis/size handling. ZIP duplicate
names/limits, DTDs, cyclic styles and deep inline nesting are tested. Independent
`python-docx` 1.1.0 fixture generation recipe is in `tests/fixtures/`.

0.4.1 adds WFG2 identity persistence/WFG1 migration and Editor::transact grouped
rollback/undo/change sets; docs/WFG_SCHEMA.md defines the implemented contract.
Review corrected allocator rewind and unnecessary shifted-node cache invalidation.
Six added regressions bring total coverage to 58 cases;0.4's52 remain historical.
Grouped operations still copy/full-validate/scan the document; they are not COW.

Total 58 cases (Qt init/cleanup excluded), five CTest executables. Latest full
Windows ARM64 and Ubuntu ARM64 suites pass; one Ubuntu-path emoji fixture skips
on Windows. Evidence: `docs/evidence/2026-11-12-*-phase1a-*` plus earlier DOCX logs. `python-docx` opened an
export and checked text/emphasis/numbering/margins; no real Word/LibreOffice visual
round trip, fuzz/sanitizer campaign or production accessibility proof exists.

## Machine-specific build and runtime context

Windows workspace: `C:\dev\wordforge-public` (earlier `C:\dev\meridian\wordforge`);
dependency binaries:
`C:\dev\wordforge-deps\msys64\clangarm64\bin`. Native Windows ARM64 output.

```powershell
$env:PATH='C:\dev\wordforge-deps\msys64\clangarm64\bin;'+$env:PATH
cmake -S wordforge -B wordforge/build/handoff -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build wordforge/build/handoff --parallel 6
ctest --test-dir wordforge/build/handoff --output-on-failure
```

Use a fresh output directory if an executable is open. Several earlier app windows
may contain user edits; do not kill them. Latest local outputs before this handoff:
`wordforge/build/docx-preview` (0.4) and `build/snap-preview` (0.3.1). Build output
and `out/` are ignored, not portable artifacts. Rebuild on another machine.

Dedicated WSL2 distro: **WordforgeUbuntu**, Ubuntu 24.04.5 ARM64 with systemd.
Other/default/Docker distros were not changed. Native source-built Skia m143 lives
at `/opt/wordforge-deps/skia`, pkg-config file `/opt/wordforge-deps/pkgconfig`.
Linux app/test build: `tools/local-ci-ubuntu.sh` picks a build folder per checkout.
The quickest full check is `tools/local-ci.ps1` (see TESTING.md).

```powershell
wsl -d WordforgeUbuntu -- bash -lc 'export PKG_CONFIG_PATH=/opt/wordforge-deps/pkgconfig; cmake -S /mnt/c/dev/meridian/wordforge -B /root/wordforge-build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /root/wordforge-build --parallel 6 && XDG_RUNTIME_DIR=/tmp/wordforge-runtime ctest --test-dir /root/wordforge-build --output-on-failure'
```

`/tmp/wordforge-runtime` must exist, be owned by the current user and mode0700.
Native Linux GUI: `QT_QPA_PLATFORM=xcb` with WSLg. This is actual ARM64 Linux
execution, not standalone Ubuntu/x86_64 evidence. On another machine use
`docs/UBUNTU.md` and `tools/build-linux.sh`; don't assume these local paths exist.

## Important implementation boundaries

- Model: `Editor` public document/selection/style, commands and shared undo State
  snapshots. Snapshot contents and candidate documents still copy text/runs.
- `PageView::change` composes candidate layout and synchronizes rendering before
  publishing model state; failure refreshes the original. Preserve this behavior.
- Paragraph IDs/revisions enable measurement caching. WFG1 migration assigns IDs;
  new WFG2 files persist them. The private Editor high-water survives undo branches.
- ICU/HarfBuzz glyph clusters drive layout. Lists are generated glyph markers,
  not text prefixes. Renderer retains page pictures and separate selection pixels.
- DOCX remains a bounded import/export subset and flattens styles. Unknown content
  is not retained for round-trip export; an editable-copy warning is not preservation.
- Recovery writes native snapshots every30s, per-window UUID path and QLockFile;
  clean formatted save/explicit discard clears snapshots. Original files are not
  overwritten by recovery. Last30s, backups and upgrade recovery remain limitations.

## Distribution and CI

Unsigned ARM64 Snap 0.3.1 was packed (~61MiB) and its unpacked wrapper launched.
snapd/systemd were installed only in the dedicated distro. Store API HTTPS timed
out from Windows and Ubuntu; `snap install core24` failed, and confinement is
`partial` (no AppArmor). Installed strict Snap, Store acceptance and publication
are unverified. Current 0.4 recipe is not the old tested 0.3.1 artifact.

`.github/workflows/wordforge-native.yml` configures Windows ARM64 and Ubuntu amd64
build/tests and an unsigned amd64 Snap artifact. Check actual Actions runs; a
configured workflow or empty combined-status response is not passing CI evidence.
No Store upload/authentication is part of this handoff. Name availability unknown.

## Exact next milestone

(Historical — superseded.) First finish the table desktop task in docs/TABLE_CHECKPOINT.md;
then roadmap **1B: typed document tree/resources/anchors**, 1C and 2A.
Phase 1A's executed plan is `docs/superpowers/plans/2026-11-12-phase-1a.md`.
Verify its persisted-ID/migration/rollback regressions instead of rewriting them.
Create the 1B file-level design and failing tests against one authoritative tree;
keep existing editor/layout/native/DOCX paths working while removing replaced storage.
Do not build tables/media/review features on a second unconnected model.

After each increment update status, roadmap, compatibility and this handoff;
record exact executed checks and remaining failures; commit and push focused
changes. Preserve the branch in a working state. The full product is still early.
