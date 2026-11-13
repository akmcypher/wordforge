# Table checkpoint — takeover guide

**2026-11-13 update: the desktop table controls are delivered** (Insert → Table,
Format → Table, Format panel, Tab/Shift+Tab, cell-width pictures, desktop
regression, python-docx check). The steps below are kept for history; the
"Scope still pending" list remains open.

Saved 2026-11-13 (Asia/Calcutta) at the user's explicit request to commit all code
and context before another agent continues. Branch: codex/wordforge-native.
Remote: https://github.com/akmcypher/meridian.git ; project: wordforge/.
Base delivered picture commit: ff8d5d3. Read git log for this checkpoint's SHA.

## What works in code and what does not exist yet

The ribbon was replaced by the menu bar/toolbar/Format panel shell (2026-11-13);
Insert Table goes in the Insert menu and Format panel. Table foundation is integrated
below the desktop shell, with no exposed Table dialog/button yet. Do not tell the
user Insert is complete. Their continuing requirement is real Word-like Insert
features, warm brown/taupe design, fast native behavior and per-feature GitHub pushes.
No Co-authored-by trailers; never rewrite pushed history. Routine work authorized.

- document.hpp: Table{id,rows,columns}, TableCell{tableId,row,column}, optional
  Paragraph.cell, Document.tables, ChangeSet.tablesChanged.
- tables.cpp: validateTables; Editor::insertTable, selectedCell, tableRow(insert,
  before), tableColumn(insert,before), removeTable, nextCell(backward).
- Structural commands reuse Editor::transact. Paragraphs are the sole content
  authority; membership is typed metadata, not duplicate cell text. Multiple
  paragraphs per cell; globally allocated table/paragraph/image/bookmark IDs.
- Validation requires complete contiguous row-major cells,1..1000 rows,1..10
  columns, following body paragraph and no page breaks inside cells. Existing
  replaceRange stays within one body region/cell. Boundary delete refuses merges.
- Row/column deletion repairs bookmarks whose paragraph disappeared; transact
  prunes orphan images. Undo retains immutable image resources and full snapshots.
- Layout: equal page-body-width columns,4pt padding, min36pt text width, whole-row
  pagination, taller-than-page row rejection, per-cell lists, actual cell hit tests.
  Layout::cells stores rectangles and paragraph spans. Renderer includes borders
  in retained page keys and paints warm brown strokes; PDF uses the same scene.
- Native adapter: WFG5 ZIP with table metadata and paragraph membership, optional
  PNG resources/bookmarks. WFG1..4 unchanged readable. Table-free writes stay2/3/4.
- DOCX: real tbl/tr/tc, fixed equal grid,4pt margins,borders,cantSplit, paragraphs
  and images per cell. Table import still produces explicit unsupported warnings.

## Exact next implementation steps

1. Restore tableDialogEditingStructureAndFileWorkflow Qt slot from the fenced code
   in docs/superpowers/plans/2026-11-13-tables.md (and include QSpinBox). It already
   failed for missing tableButton; it now targets tableAction in the Insert menu. It was moved out of compiled tests for this
   unfinished checkpoint, not disguised as a pass or skip.
2. Add PageView wrappers through change() for insertTable/row/column/removeTable.
   Expose selectedCell. Use the existing candidate→layout→renderer→commit boundary.
3. Implement native Insert Table dialog (row/column spin boxes) and one Tables
   entry in the Insert menu (command id "table"), plus a Table section in the Format
   panel. Add context-enabled add/delete row/column
   and delete-table menu actions. Match names expected by pending test: tableAction,
   tableRows,tableColumns,insertRowAfter. No disabled placeholder feature shells.
4. Tab and Backtab in a cell call nextCell; at last cell Tab adds one row with one
   undo step. Preserve ordinary body Tab. Commit/clear IME correctly before navigation.
5. Picture insertion currently caps against full page width; calculate selected
   cell text width before fitting a new picture. Size dialog should use cell bounds.
   Layout already rejects too-wide pictures atomically; avoid routine UI rejection.
6. Add desktop regressions for tall-row rejection/undo preserving the active doc,
   Enter and boundary delete, native save/open/recovery/PDF and context control state.
7. Export a table DOCX and check with python-docx1.1.0 (installed in WSL): table/row/
   cell counts, multi-paragraph cell text and picture media. This independent table
   check has NOT been executed. Real Word/LibreOffice visual checks remain pending.
8. Review, run all suites on both platforms, update ledger/version/status, commit
   and push. Only then mark the table subset delivered. Continue remaining Insert
   roadmap (links,headers/footers/page numbers,shapes/text boxes,comments,equations).

Scope still pending: merge/split, custom widths/borders/shading, repeated header
rows, row splitting, nested tables, cell-range selection, DOCX table import.
Full model-v2 blocks/sections and incremental pagination are not complete.

## Verification and machine context

85 actual cases: Windows ARM64 84 pass +1 Ubuntu font skip; Ubuntu ARM6485 pass.
Five executables pass on both. Nine new table cases. Qt25 totals include setup/
cleanup;23 actual desktop cases. Logs: docs/evidence/2026-11-13-*-table-foundation-*.
Independent read-only review found per-cell numbering incorrectly0; reproduced
before fixing per-cell counters. No other material issue confirmed in that review.

Windows verified build: wordforge/build/tables-preview (not launched).
Prepend C:\dev\wordforge-deps\msys64\clangarm64\bin to PATH; CMake/Ninja Release.
Never close old running editors: docx-preview may hold unsaved user work, and
images-preview holds the disposable picture demonstration. Use a fresh build output
if an exe is locked. Qt6.12/Clang23/Skia143 on native Windows ARM64.

Ubuntu24.04 ARM64 WSL distro: WordforgeUbuntu. Source /mnt/c/dev/meridian/wordforge;
root build /root/wordforge-build; format-agent build /root/wordforge-table-formats.
PKG_CONFIG_PATH=/opt/wordforge-deps/pkgconfig; XDG_RUNTIME_DIR=/tmp/wordforge-runtime
(mode0700). cmake --build /root/wordforge-build --parallel4; ctest -V. GCC13/Qt6.4.2.
WSL briefly timed out during this session, then /bin/true and full tests succeeded;
no distro/Docker shutdown or destructive recovery was performed.

Build/out files are ignored and are NOT portable or committed. All source, tests,
plans, status and textual evidence are committed. Native API/runtime stack remains
C++20/Qt6/Skia/HarfBuzz/FreeType/ICU/pugixml/libzip. No browser document engine.
