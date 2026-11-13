# Testing

## Local CI (no hosted minutes)

From the wordforge folder on the Windows ARM64 machine:

```powershell
powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1          # Windows + Ubuntu tests
powershell -ExecutionPolicy Bypass -File tools\local-ci.ps1 -Snap    # also pack the arm64 Snap
```

It builds into `build\local-ci` with the MSYS2 CLANGARM64 toolchain, runs all CTest
suites, then runs `tools/local-ci-ubuntu.sh` in the WordforgeUbuntu WSL distro
(build, tests, optional `package-snap.py`). Logs go to `out\local-ci\<timestamp>\`;
the exit code is 0 only when every selected stage passed. Options: `-SkipWindows`,
`-SkipUbuntu`, `-BuildDir`, `-MsysBin`, `-Distro`. It never closes a running editor:
if `wordforge.exe` is running from the build folder, the Windows stage is skipped.
`local-ci-ubuntu.sh <log dir> [--snap]` also works alone on an Ubuntu 24.04 machine.
WSL gives only partial Snap confinement, and this machine cannot build amd64.

Current release-feature coverage (2026-11-13): 100 cases; see CURRENT_STATUS.md.

Table foundation checkpoint: 85 actual cases across five executables (Qt setup/
cleanup excluded). Windows84 pass plus1 Ubuntu-specific font skip; Ubuntu85 pass.
Nine added regressions cover structural editing/undo/boundaries/bookmarks, table
geometry/pagination/hit testing, rendered borders, per-cell numbering, WFG5 malformed
membership/exact IDs and DOCX table structure with picture cells. Review's numbering
bug was reproduced before correction. Logs: 2026-11-13-*-table-foundation-*.
The pending desktop table regression failed because tableButton is absent; it was
moved verbatim to the implementation plan for this user-requested checkpoint.
No table UI or independent external parser result is claimed as delivered.

0.6 picture coverage: twelve additional cases exercise typed atom boundaries,
immutable ownership and deletion, RTL caret edges, actual rendered PNG pixels,
WFG4 save/open and malformed references, orphan-resource omission, DrawingML
media/geometry, desktop file dialogs, aspect resize, PDF bytes and failed-command
rollback. All five suites pass on both native ARM64 platforms: 76 cases total
(excluding Qt init/cleanup), Windows one Ubuntu-only skip. Saved logs use
`2026-11-13-*-pictures-*`. The live Windows chooser and size dialog were exercised;
python-docx 1.1.0 verified exported image count, dimensions and PNG bytes. No real
Word/LibreOffice visual round trip, Snap installation or x86_64 proof is claimed.

Six anchor/bookmark cases cover stable resolution/reordering, insertion affinity,
multi-paragraph replacement/deletion, combining-character repair, typing-style
navigation, invalid native metadata, undo/save/reopen and actual Qt dialog creation.
IME preedit navigation and failed DOCX export preservation have regressions.
Evidence: `2026-11-12-*-bookmarks-*`. Benchmark `--anchors` measures a 100k-character
paragraph with 1000 registered bookmarks and per-paragraph boundary caches.

Run `ctest --preset dev`. GoogleTest covers real model operations, selection,
style preservation and ICU grapheme deletion. Integration tests will exercise
the actual layout, renderer and desktop command paths as they are introduced.

Use `QT_QPA_PLATFORM=offscreen` for unattended Qt tests. Offscreen tests do not
prove desktop integration, accessibility, GPU execution or Linux compatibility.
Keep screenshots, timings and exact dependency versions in ignored `out/`.

Initial command, layout, format and rendering tests were executed against
unimplemented bodies and failed before implementation. Review regressions were
also reproduced before fixes. Regressions cover whitespace and mixed CDATA text
loss, multiple XML roots, delete/undo caret restoration, joined emoji boundaries,
keyboard emphasis and atomic rejection of invalid imported/edited layouts.

The desktop test functions exercise actual Qt events, clipboard, save/open,
PDF bytes, IME preedit/commit, viewport cache reuse/invalidation, toolbar state,
focus mode and the find/replace bar including Escape and undo. Qt adds
init/cleanup cases to its count. Expected offscreen-plugin sizing warnings
occur when displaying or resizing the full shell.
The native Windows platform was also launched and captured; the headless test
suite alone is not claimed as platform integration proof.
The complete desktop suite was also run with `QT_SCALE_FACTOR=1.5`.

The suite additionally covers paragraph property undo, list continuation and
numbering cache invalidation, spacing/indent geometry, preset/page-break
interaction, persisted decorations and actual highlight/list pixels. The full
ribbon test exercises new commands and checks group captions remain visible.

Native Ubuntu ARM64 tests were executed using GCC13, Qt6.4.2 and source-built
Skia. All five CTest suites passed, including the actual Noto bitmap emoji fixture.
Windows skips that Ubuntu-path-specific fixture. There are 64 cases total,
excluding Qt's two setup/cleanup cases. A native X11/WSLg capture was also made.

The four recovery regressions exercise atomic preservation after invalid writes,
live-owner lock exclusion, abandonment, formatted-save versus text-export cleanup,
and a real killed subprocess followed by stale-lock acquisition and draft readback.
Test-mode data paths keep these fixtures separate from the user's recovery files.

Seven DOCX regressions cover Unicode/style/list/page round trips, alternate namespace
prefixes, inherited/default styles and numbering, style-toggle semantics, complex
script export flags, malformed packages/DTDs, duplicate ZIP names, cyclic styles,
deep inline content, an independent python-docx fixture and unsupported-content
warnings. A desktop regression verifies real ZIP output, import-as-copy, formatting
and failed-import preservation. Evidence: `2026-11-12-*-docx-*`. Independent
python-docx opening is a structural check, not Word/LibreOffice visual fidelity.

Six Phase 1A regressions exercise document/allocator invariants, exhausted
allocation before history mutation, legacy WFG1 migration and exact WFG2 sparse
IDs above 2^53, grouped undo/redo/rollback including selection and typing state,
genuine moves versus shifted paragraph caches, and retired-ID rewind protection.
The full desktop/recovery/DOCX suites exercise the new writer/reader unchanged.
Evidence: `2026-11-12-*-phase1a-*`. Model-only timing/memory comes from
`wordforge_transaction_benchmark`; no rendering/layout or percentile claim.

The root `.github/workflows/wordforge-native.yml` configures Windows ARM64 and
Ubuntu x86_64 builds/tests. Remote CI, sanitizers, fuzzing and external format
interoperability remain unexecuted and are not reported as passing.
