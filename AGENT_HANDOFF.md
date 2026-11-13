# Wordforge handoff — start here

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
