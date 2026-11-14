# Wordforge phased implementation plan

Updated 2026-11-13 (Asia/Calcutta). The full product specification is
[ENGINEERING_DIRECTIVE.md](docs/ENGINEERING_DIRECTIVE.md). Start with
[AGENT_HANDOFF.md](AGENT_HANDOFF.md) for the current checkout and build commands.

**Goal:** A production-grade native word processor with reliable document
semantics, typography, pagination, interoperability and long-document editing.
**Architecture:** Evolve the existing model through versioned migrations; one
authoritative document, one layout pipeline and one renderer. Keep the current
application usable at each checkpoint. Do not add a second engine or a parallel
unconnected document tree.
**Stack:** C++20, Qt6 >=6.4, Skia m143, HarfBuzz, FreeType, ICU, pugixml,
libzip >=1.7, CMake/Ninja, GoogleTest and QtTest.

Status vocabulary: **implemented** = integrated with executed evidence;
**partial** = the named subset only; **planned** = no implementation claim;
**unverified** = code/configuration exists without the required execution.
Nothing below asserts a completed production-grade phase.

## Current checkpoint

- Published starting baseline: `32b1461` (native editor, brown UI, recovery,
  unsigned Snap preparation); `1116f20` added DOCX and the durable handoff.
- Core/desktop tests: 85 cases excluding Qt setup/cleanup, five suites. Ubuntu
  ARM64 runs all; Windows ARM64 skips one Ubuntu-specific font fixture.
- DOCX subset: bounded ZIP, OOXML paragraphs/runs, basic inherited formatting,
  simple numbering and page setup. Imports are editable copies with warnings.
- 0.6 adds typed text/image inlines, immutable image resources, WFG4, inline
  picture insertion/resize/undo, native recovery and PDF/DOCX image export.
- Full typed blocks/sections, retained semantic styles, a preservation registry,
  incremental page convergence, tables and production packages remain open.

## Five confirmed baseline weaknesses and current resolutions

1. `core/document/document.hpp`: paragraphs/runs are the only content nodes;
   resolved character properties are copied into runs. 0.6 adds typed text/image
   inlines and PNG resources; sections, tables and semantic styles remain absent.
2. `core/formats/native.cpp`: baseline WFG1 regenerated IDs. Phase1A resolved this
   with WFG2 identity/allocator persistence; 0.5 adds WFG3 bookmarks. Rich typed-node
   and resource migrations are still required for the canonical tree.
3. `core/layout/layout.cpp::compose`: cached measurements avoid reshaping unchanged
   paragraphs, but page composition still traverses the document. No convergence
   checkpoints, background cancellation or visible-page scheduling.
4. `Editor::checkpoint`, `PageView::change`: shared history entries still contain
   copied document content; candidate editing copies the current document. There
   are now grouped transactions/change sets, but no persistent text/run storage
   or retained-memory cap; batch validation/diff and content copies remain O(N).
5. `docx.cpp`, `shaper.cpp`: DOCX flattens styles/omits unsupported structures after
   warning; font-choice caching has no explicit capacity. Preservation, bounded
   memory and richer semantics remain necessary; they are not solved by the UI.

## Delivery rules

For every increment: write a failing behavioral regression, implement the
smallest integrated change, build, run targeted then full suites, measure affected
performance, review the diff, update the ledger and commit/push without rewriting
history. Keep application file workflows and old fixtures working. A phase exits
only after its acceptance criteria pass; do not infer completion from scaffolding.

All coordinates need explicit units: model/layout use points, text uses UTF-16
offsets constrained to ICU grapheme boundaries, raster uses device pixels. New
document anchors must use stable IDs rather than persisting vector indexes.

## Phase 0 — Baseline and handoff

**Status: implemented baseline, partial quality/performance coverage.**

Files: CMake/build helpers, `ARCHITECTURE.md`, `PERFORMANCE_BASELINE.md`,
`COMPATIBILITY_MATRIX.md`, status and evidence register. Preserve the complete
directive in the repository so attachment paths are not required for continuation.

Acceptance: fresh native builds and tests on available platforms; exact commands,
source state, measured boundaries and missing coverage recorded. Do not label CI
or installed platforms passed without checking their runs. Local DOCX increment
is a useful checkpoint, not completion of Phase 4.

## Phase 1 — Structured model, identity and native migration

**Status: 1A implemented; 1B anchors and typed text/image resources implemented;
full blocks/sections and 1C planned.**

Executable next-increment plan:
[2026-11-12-phase-1a.md](docs/superpowers/plans/2026-11-12-phase-1a.md).

### 1A — Durable identity and transaction baseline

Delivered: checked ID allocation/high-water, WFG2 exact IDs plus WFG1 reader,
shared validation, grouped ChangeSet/rollback/undo and insertPageBreak integration.
Schema and constraints: docs/WFG_SCHEMA.md. Six new regressions and native
model-only benchmarks executed on Windows/Ubuntu ARM64. Full typed model remains1B.

Files: `core/document/document.*`, proposed `validation.*`, `transactions.*`,
`core/formats/native.*`, unit/document/format tests and legacy fixtures.

- Introduce shared document validation: unique nonzero IDs, valid allocator,
  valid Unicode/styles/geometry. Separate full import validation from changed-node
  validation; do not add an O(document) pass to every keystroke.
- Define the native v2 schema with persisted node IDs and allocator state. Read
  v1 via deterministic migration; keep the v1 reader and golden fixtures. Reject
  malformed v2, duplicate IDs and dangling references without mutating the editor.
- Generalize compound editing into an atomic transaction with one undo entry,
  rollback on failure, selection restoration and a change set identifying changed,
  inserted and removed IDs. Keep existing single commands compatible.
- Preserve the existing stable-paragraph behavior; add missing guarantees rather
  than rewriting working grapheme/editing logic.

Tests: `LegacyV1MigratesWithoutTextOrFormatLoss`, `V2PreservesIdsAcrossSaveOpen`,
`DuplicateIdsAndInvalidAllocatorRejected`, `CompoundEditUndoesAsOneOperation`,
`FailedCompoundEditPreservesDocumentSelectionAndHistory`,
`UntouchedParagraphIdsRemainStable`, `UndoBranchDoesNotReusePublishedIds`.
Acceptance: legacy .wfg fixtures and all desktop/recovery/DOCX tests pass;
IDs survive persistence; the transaction is used by a real composite command;
measure model-edit/undo memory before and after. Do not claim the entire v2 tree.

### 1B — One typed document tree and resource ownership

Delivered first slice: registered paragraph-ID bookmark anchors with edit transforms,
native persistence, undo, navigation and a Qt dialog. Executed plan:
`docs/superpowers/plans/2026-11-12-phase-1b-anchors.md`. 0.6 integrates typed
text/image Run variants and immutable shared resources through editing, shaping,
layout, rendering and native/DOCX export. See the inline-images plan. Full typed
blocks/sections remain open; point bookmarks are not arbitrary range anchors.

Proposed files: `core/document/nodes.*`, `resources.*`, `positions.*` plus adapters
in editor/layout/formats. Introduce document metadata/settings, section nodes,
typed block and inline variants, stable resource references and document anchors.
Migrate existing paragraphs into this single authority; migrate readers/editor
incrementally and remove obsolete duplicate storage as each slice lands.

Acceptance: paragraph editing, layout, save/recovery and both formats consume the
same model. No inert table/image placeholders advertised as working. Resource
IDs resolve after serialization; missing/corrupt resources fail transactionally.

### 1C — Semantic styles and efficient document versions

**Slice 1 delivered (2026-11-13): semantic paragraph styles.** `core/document/styles.*`
holds named paragraph styles with based-on chains, next style, cycle/dangling
detection and the built-ins replacing the four presets. Paragraphs and runs record
directly set properties as masks; resolved values stay on the nodes so layout,
rendering and export are unchanged, and `validateDocument` rejects any inherited
value that disagrees with its style (one authority). Editor: `applyStyle`,
`updateStyleFromSelection` (Format → Styles → Update style to match selection),
style-aware Clear formatting and Enter's next style. WFG6 persists styles and
masks; WFG1–5 and DOCX migrate through `adoptStyles` (docs/WFG_SCHEMA.md). Tests:
`tests/unit/style_test.cpp` (9) plus a desktop end-to-end test. Measured on Windows
ARM64 versus main: single edit on 34,000 paragraphs 5.1 → 6.3 ms, grouped break
23.5 → 26.3 ms, peak +4 MiB; layout, save and first paint unchanged within noise.
**Slice 2 delivered (2026-11-14): custom paragraph styles.** `Editor::
createStyleFromSelection` (based on the caret paragraph's style, appearance kept,
only differences stay direct), `renameStyle` (custom only; identifiers never change)
and `deleteStyle` (paragraphs fall back to the base style keeping direct formatting;
derived styles absorb the deleted style's properties so they look the same; `next`
references are redirected). Format → Styles → New/Rename/Delete; the toolbar style
box lists built-ins then the document's styles. Undoable; persisted by WFG6.
Remaining 1C: named character and table styles, theme fonts/colours, DOCX style
export, numbering definitions, and delta-based undo storage.

Proposed files: `core/document/styles.*`, `numbering.*`, transaction storage.
Retain defaults, named paragraph/character/table styles, based-on links, theme
references and explicit property overrides. Resolve into a cached immutable
effective style at layout time. Detect cycles. Use measured persistent storage or
command deltas for undo instead of repeated complete text/run copies.

Acceptance: changing a base style updates inheriting content but retains direct
overrides; save/reopen preserves the distinction. Bound retained history/caches
with documented policies. Mixed edits, undo branches and migration have regressions.

## Phase 2 — Incremental layout and caret correctness

**Status: existing paragraph measurements and basic pages; v2 planned.**

Files: `core/layout/layout.*`, proposed `inline_layout.*`, `pagination.*`,
`checkpoints.*`, `hit_testing.*`; `page_view.*` presentation boundary.

2A: consume change sets, separate shaping/inline/paragraph/page stages, record
dependency signatures and page checkpoints; resume at the earliest affected
block and stop when downstream state converges. Reuse untouched page output.
2B: keep-with-next/keep-lines, widow/orphan, section/column/page breaks, distinct
section geometry and header/footer regions; prevent oscillation.
2C: immutable revision-tagged background layout, visible-page priority, cancellation
and stale-result fencing; no worker touches widgets or mutable live documents.
2D: validate visual/logical bidi navigation and cluster-derived caret/selection
geometry for ligatures, combining marks, emoji, Arabic and Indic scripts.

Acceptance: targeted early/middle/end edits match a full-layout oracle; untouched
prefixes do not recompose; stale work cannot publish. Repeatable 1/100/1000-page
end-to-end edit distributions and memory tests, not only composition microtimings.

## Phase 3 — Typography completion

**Status: working multilingual foundation; advanced controls planned.**

Files: `core/typography/shaper.*`, proposed `font_catalog.*`, `font_cache.*`, tests.
Bound fallback/glyph caches; add language, script, OpenType feature and variable
font configuration, superscript/subscript/baseline and spacing semantics. Preserve
deterministic fallback and bitmap-strike sizing. Run a real-script/font corpus.
Acceptance: glyph clusters and actual metrics drive layout; language/feature
changes invalidate correct caches; missing fonts fail/fallback explicitly;
stress edits do not grow caches indefinitely. Record font versions in goldens.

## Phase 4 — DOCX infrastructure, preservation and fidelity

**Status: partial text/formatting subset.**

Files: `core/formats/zip_package.*`, `docx.*`; split OPC relationships/content
types, styles/numbering and block/run codecs into focused modules as they grow.
Add proposed `preservation.*`, `ooxml/`, fixtures and compatibility tests.

4A: validate content types and relationship types/targets, preserve unknown parts
and relationships in a registry tied to stable document IDs/context. Extend
configurable resource limits and corrupt/expansion/encrypted-package tests.
4B: map semantic styles/themes/defaults, numbering/restarts/levels, sections,
metadata/settings, hyperlinks/bookmarks and soft/page/column breaks faithfully.
4C: add tables/media/headers/footers/notes/comments/revisions only with the
corresponding model and layout support. Preserve uneditable content meanwhile.
4D: corpus of letters, resumes, reports, academic, multilingual, table/image-rich,
tracked-change and long documents; Wordforge -> Word/LibreOffice -> Wordforge
structure and rendered-page comparisons. Unknown parts must survive edits where
their context remains valid; unsafe invalidation must be explicit to the user.

Acceptance: explicit full/partial/preserved/unpreservable compatibility matrix,
no silent loss, no original-file overwrite on failed/partial import, tested real
external-app round trips. ZIP creation or python-docx parsing alone is insufficient.

## Phase 5 — Professional content and editing

**Status: lists/pictures delivered; table model/layout/export foundation tested,
desktop table controls unfinished (user-requested takeover checkpoint).**

Table checkpoint: docs/TABLE_CHECKPOINT.md and
`docs/superpowers/plans/2026-11-13-tables.md` record the exact completed foundation,
next desktop tests and remaining scope. Do not mark tables delivered yet.

5A tables: `core/document/tables.*`, `core/layout/table_layout.*`, editing commands
and desktop controls. Rows/columns, widths, margins, borders/shading, merge/split,
cell selection/navigation, repeating headers and page/row-split rules. Add styles,
vertical alignment and nested tables after stable first-level pagination.
5B images: managed resources, decode limits, inline placement, scaling/aspect,
replacement; then floating anchors, wrap geometry, cropping and compression.
Delivered in 0.6: PNG/JPEG insertion normalized to immutable PNG, selected-picture
size dialog with aspect lock, inline pagination, deletion/undo, WFG4/recovery,
PDF/DOCX export. Image import from DOCX, replacement, floating/wrap/crop and rich
clipboard remain open. This does not complete the full 5B acceptance criterion.
5C lists: semantic multilevel definitions, custom formats, restarts/continuation.
5D headers/footers: section/first/odd-even variants, separate editing and fields.
5E navigation/clipboard: outline/heading pane, go-to page/section/bookmark,
transactional internal fragments, rich/HTML interchange, images and tables.

Acceptance: insert/edit/undo/save/reopen/DOCX round trip for each supported object,
including cross-page cases; geometry and selection regressions. UI controls only
appear when connected to working document commands.

## Phase 6 — Advanced semantics

**Status: planned.** Proposed model/commands/codecs for revisions, comments,
comparison, notes, fields, TOC, cross-references, citations/bibliographies, equations,
captions/indexes, content controls, protection and forms. Deliver separately with
undo, persistence and DOCX preservation. Field evaluation must be deterministic
and must not execute untrusted external content. No UI-only semantic overlays.

## Phase 7 — Rendering, printing and visual quality

**Status: retained CPU pages/overlays/PDF implemented; remainder planned.**
Files: `core/rendering/page_renderer.*`, `page_view.*`, proposed print service.
Consume immutable layout, bound resources, schedule visible/adjacent pages,
background raster safely, add printing/preview and visual regression goldens at
multiple DPIs/zooms. GPU only after a measured CPU bottleneck and parity tests.
Acceptance: shared print/PDF/screen geometry, no duplicate layout, deterministic
selection/caret output, DPR/zoom/image/table corpus and measured latency.

## Phase 8 — Native application and accessibility

**Status: minimal menu bar/compact toolbar/Format panel/Ctrl+K palette over one
command registry (ribbon removed), dialogs, IME/shortcuts/focus/zoom.**
Files: `apps/desktop/` commands/services, ribbon, navigation, preferences and
document session management. Add recent files, tabs, context menus, settings,
navigation and print preview. Keep business rules in commands/services.
Implement document accessibility objects with meaningful text/ranges/actions;
test real screen readers and keyboard-only workflows, not widget labels alone.
Acceptance: assistive technology, IME, multi-click/drag selection, shortcut focus,
high-DPI and file lifecycle tested on each claimed platform.

## Phase 9 — Reliability and recovery hardening

**Status: atomic save, 30s snapshots, recovery locks/dirty state implemented.**
Files: `apps/desktop/recovery_file.*`, file lifecycle, model validation/resources.
Add user-document locking/conflict detection, configurable limits, rotating
backups, integrity checks, recovery/version migration and explicit error reporting.
Fault-inject write, commit, disk-full, permission, partial-write, process kill,
duplicate instance and interrupted refresh cases. Move expensive snapshots off
the UI only with revision fencing. Define safe privacy/crash-reporting policy.
Acceptance: existing document and last durable snapshot survive failures;
successful-save claims follow durable commit; updates retain recoverable drafts.

## Phase 10 — Measured performance

**Status: synthetic benchmarks only; see PERFORMANCE_BASELINE.md.**
Extend `benchmarks/` with end-to-end open/edit/delete/format/search/selection/
scroll/save/DOCX/PDF workloads and real multilingual/table/image corpora. Report
median/P95/P99, memory, cache hits and invalidation/recomposition scope. Warm
caret <5ms, normal edit <50ms, large edit <100ms, cached navigation/repaint <16ms,
1000-page search <500ms and typical 100-page save <500ms are targets, not claims.
Acceptance: reproducible corpus/hardware/build metadata, distributions and CI
regression thresholds; improvements include model/storage and presentation costs.

## Phase 11 — Quality/security corpus

**Status: unit/integration regressions exist; full program planned.**
Separate model/transaction/Unicode/layout/pagination/render/formats/clipboard/UI/
recovery/security/performance coverage as meaningful suites grow. Add sanitizers,
parser fuzzing, property-based edits and corrupt-file fixtures, visual comparisons,
interoperability and cache/history stress. Retain minimized failures. Tests for
absent table/image features are not passing feature evidence.
Acceptance: relevant corpus runs on claimed platforms, limits are exercised with
adversarial input, regressions are reproducible and performance failures actionable.

## Phase 12 — Installed distribution

**Status: preview packaging only; publication deferred.**
Files: `packaging/`, CI and build helpers. Validate Windows x64/ARM64 and Ubuntu
amd64/ARM64 installers, Snap and AppImage/native package, associations, icons,
uninstall, dependencies/notices, source/relinking obligations, signing, updates
and recovery across upgrades. Document macOS feasibility separately.
Acceptance: installed builds launch and pass real desktop smoke tests; strict
Snap confinement verified on standalone Ubuntu; signed/versioned artifacts and
rollback proven. Only then consider a separately authorized Store release.

## Review focus / cross-phase hazards

- Legacy native files and old recovery snapshots: Phase 1/9 golden migrations.
- Unknown OOXML tied to moved/deleted anchors: Phase 4 preservation invalidation.
- Style inheritance versus direct/toggle overrides: Phase 1C/4B cascade tests.
- Undo identity and obsolete background results: Phase 1A/2C branch/revision tests.
- Tables/images spanning pages and fallback fonts: Phase 2/3/5 geometry corpus.

## Next-agent execution order

1. Read handoff, inspect Git state, rebuild/test the actual checkout.
2. Verify **1A** regressions/schema on the checkout; do not reimplement its tested
   identity/transaction migration. Check recorded O(N) batch/history limitations.
3. Start **1B/1C**, then **2A**. Expand DOCX preservation alongside the model;
   do not expand lossy DOCX mappings indefinitely on the old flat representation.
4. Keep this ledger, status, compatibility and handoff synchronized after every
   published milestone. Detailed task plans live under `docs/superpowers/plans/`.
