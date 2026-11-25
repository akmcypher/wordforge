# Wordforge handoff — start here

Condensed on 2026-11-23. The full earlier notes are kept verbatim in
`docs/history/` (`agent-handoff-to-2026-11-23.md`, `status-to-2026-11-23.md`,
`2026-11-22-agent-continuation.md`). Where they disagree with this file, this file wins.

## Current state

- `main` holds everything through PR #64 (release 0.6.13):
  - features: sections/columns, tab stops and TOC dot leaders, keep-with-next and
    keep-lines, widow/orphan control, superscript/subscript, and in 0.6.13 Word table
    styles (PR #59), Word's style defaults kept (PR #61), contextual and character
    spacing (PR #62), and list levels with Word numbering definitions (PR #63)
  - the long-document performance work of PRs #50–#54: incremental pagination, page
    stamps, copy-on-write paragraphs, incremental word count, O(1) page positions,
    and PR #57's per-paragraph pagination keys and in-place checkpoints

  Native format WFG18; the reader supports WFG1–18. WFG18 (contextual and character
  spacing, list levels and definitions) shipped in 0.6.13, so further format changes
  need WFG19.
- Last published release: **v0.6.13** (tag at 0baebd5, 2026-11-24).
  - Windows installers and ZIPs are checksum-verified.
  - Snap stable is `0.6.13-preview`, amd64 rev 87 / arm64 rev 86, promoted by verified
    run 45589594999.
  - Both channels read WFG1–18.
- The Snap promotion's Store download can fail with TLS errors while the Store is
  throttling (HTTP 429). Rerun the failed job once the Store's info endpoint answers 200
  again; nothing is promoted until both archives verify.
- PR #43 (0.6.9 publication evidence) is open on `codex/release-0.6.9-handoff`.
  Preserve it, and don't delete other branches or worktrees without asking.
- Latest verification (2026-11-24):
  - Main 0baebd5 (0.6.13) passed CI on PR #64, including both Windows installer builds and
    both strict-confinement Snap builds, and all six CTest suites locally on Windows ARM64.
  - Ubuntu ran in CI only for 0.6.13. No new Word automation result exists for WFG16/17 features; the last
  attempt stalled.

## Next work (in order)

1. **Rest of M2 performance.** The first slice of 2A, incremental pagination, is done.
   See `docs/superpowers/plans/2026-11-23-incremental-pagination.md`. Next:
   - a 1000-page keystroke takes about 3.1–3.7 ms (`wordforge_desktop_benchmark --long`).
     What remains grows with paragraphs: moving and shifting reused lines after the edit, the per-edit
     list-of-pointers copy, and the full scans in `validateDocument`
   - 2C background, visible-page-first layout

   Write paragraphs only through `ParagraphList::edit`/`editEach`, never before
   `checkpoint()`. Keep `tests/unit/incremental_layout_test.cpp` and
   `tests/unit/shared_paragraphs_test.cpp` green. Any new pagination input must go
   into the paragraph or global key in `layout.cpp`.
2. Phase 1C/3/4 from `ENGINEERING_ROADMAP.md`:
   - numbering through styles (Word heading numbering), and Word's list continuation
     past other paragraphs for Wordforge's own lists (list levels and definitions are done)
   - table styles and row splitting
   - theme fonts
   - language and OpenType controls (character spacing is done)
   - DOCX fidelity
   - accessibility
3. Rerun the Word fidelity harness for widow/orphan and superscript/subscript once Word
   COM works.
4. Housekeeping: about 35 local / 48 remote stale branches and old `build/` folders.
   Ask before deleting.

Known debt:
- `layout.cpp` `compose()` keeps collecting pagination rules.
- `docx.cpp` (about 3.3k lines) and `document.cpp` (about 1.8k lines) are large.
- The code has no enforced formatting.
- Each feature has bumped the WFG version. Group format changes per release where possible.

## Working rules (from the user; follow exactly)

- **Branch and merge:**
  - Branch from `origin/main` and open a PR.
  - Merging needs green CI **and** the user's approval.
  - Merge by fast-forward: `git push origin origin/<branch>:refs/heads/main`.
  - Never use GitHub merge buttons or auto-merge. Never rewrite pushed history.
- **Commit identity:** commit as `350822086+akmcypher@users.noreply.github.com`.
  **No `Co-authored-by` trailers and no "Generated with" lines** in commits or PR
  bodies; this overrides tool defaults.
- **CI:** you can't watch CI continuously. Check once; if it's pending, ask the user to ping.
- **Releases:**
  - Bump `CMakeLists.txt` `project(... VERSION x.y.z)` and `packaging/snap/snap.yaml.in`
    `version: 'x.y.z-preview'`.
  - Add `docs/releases/vX.Y.Z.md`; the release job fails without it.
  - PR, then merge, then an annotated tag `vX.Y.Z` on `origin/main`.
  - `windows-release.yml` then publishes the GitHub release (about 15 min).
  - Merging to `main` uploads snaps to edge.
- **Snap stable:** promote only on the user's explicit approval. Either:
  - run `snap-promote.yml` (workflow_dispatch, main only, with the source commit and
    both revisions; it verifies Store archives before promoting), or
  - in WSL `WordforgeUbuntu` as root, run
    `export SNAPCRAFT_STORE_CREDENTIALS="$(cat /root/.snapcraft-login)"`, then
    `snapcraft release wordforge <rev> stable`.

  **Never print `/root/.snapcraft-login`.**
- **Editor and Word:** never close the user's running Wordforge windows; always build
  into a **new** build folder. Only stop `WINWORD.EXE /Automation` processes.
- **DOCX claims:** don't describe DOCX support as full preservation.
- **Answers:** keep them short and plain, with links.

## Build and test on this machine

- Windows ARM64 checkout `C:\dev\wordforge-public`; MSYS2 CLANGARM64 toolchain at
  `C:\dev\wordforge-deps\msys64\clangarm64\bin`.
- Full build plus all six suites:
  `powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1 -SkipUbuntu -BuildDir build\<new>`.
  - The BuildDir **must be relative**.
  - Logs go to `out/local-ci/<ts>/`; the test log is UTF-16.
  - Drop `-SkipUbuntu` to also run in WSL `WordforgeUbuntu`.
  - For a fresh Ubuntu configure, set `PKG_CONFIG_PATH=/opt/wordforge-deps/pkgconfig`.
- Incremental build: put the MSYS2 bin on `PATH`, then run
  `cmake --build build/<dir> --parallel 3` and `ctest --test-dir build/<dir>`.
- One Qt test: set `QT_QPA_PLATFORM=offscreen`, then run
  `build\<dir>\desktop_tests.exe <name> -o out.txt,txt`.
- **Word fidelity:**
  - Command:
    `tools/fidelity/word-fidelity.ps1 -Tool <frozen exe copy> -Corpus <abs> -Out <abs>`.
  - Corpus: `out/fidelity/corpus`.
  - Run it alone; it runs out of memory next to builds.
  - Copy the exe first so a rebuild can't replace it mid-run.
- **CI workflows:**
  - `ci.yml`: Ubuntu x64 and Windows ARM64 tests.
  - `snap-release.yml`: strict-confinement self-test; uploads to edge on main.
  - `windows-release.yml`: installers on packaging PRs and `v*` tags.
  - `snap-promote.yml`: manual stable promotion.
  - `pages.yml`: website.
- **Editing gotcha:** in Git Bash, use the Write/Edit tools or Python script files
  rather than heredocs with escapes.

## Code map

- **`core/document/document.hpp`:**
  - Model types: `Run` (link, character style, tracked marks, baseline),
    `NoteReference` (endnote), `Field`, `SectionBreak`, `PageStyle` (columns), and
    paragraph tabs/keeps/widow.
  - `Editor` commands. Footnotes live in `images.cpp`, anchors and fields in `anchors.cpp`.
  - Invariant: every value not marked `direct` equals what the styles resolve to.
    `validateDocument` enforces it.
- **`core/document/styles.*`:** `StyleSheet`, resolution and masks, `ThemeColor`.
- **`core/layout/layout.cpp` `compose`:**
  - sections and columns, keeps and widow/orphan reservations
  - footnotes at the page bottom, endnotes after the text
  - headers and footers
- **`core/typography/shaper.cpp`:** shaping, the baseline offset, note labels.
- **`core/formats/docx.cpp`:**
  - import: `parseDocx`, `readGeometry`, `loadNotes`, fields, links
  - export: `serializeDocx`, `writeSection`, `writeNotes`, `writeColor`
- **`core/formats/native.cpp`:** WFG versions. Format changes are documented in
  `docs/WFG_SCHEMA.md`.
- **Desktop app (`apps/desktop/`):**
  - `page_view.*`: page geometry, links, TOC and sections.
  - `commands.cpp`: dialogs and commands.
  - `main_window.cpp`: menus.
  - `shell.cpp`: the Format panel.
  - `clipboard.cpp`: HTML copy and paste.
- **`tools/fidelity/`:** the Word harness, format compatibility, `roundtrip.cpp`.
- Evidence for each feature is in `docs/evidence/`; plans are in `docs/superpowers/plans/`.
