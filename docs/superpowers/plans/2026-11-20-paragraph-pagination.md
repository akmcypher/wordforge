# Paragraph pagination implementation plan

> Execute inline with the executing-plans skill; use failing regressions before
> implementation and one independent final review. Routine design choices use the
> owner's standing autonomous implementation authorization.

**Goal:** Keep a heading with the following text paragraph and keep a short paragraph
on one page/column, retaining the corresponding Word settings through editing and saves.

**Architecture:** Add keep-with-next and keep-lines to the existing paragraph/style
cascade. Preflight the minimum unbreakable text group using actual measured lines and
footnote reservation before the current placement loop. Keep the existing line placement
and explicit page/section breaks as the authority; no second pagination engine.

**Spec:** `docs/ENGINEERING_DIRECTIVE.md`, Phase 2B in `ENGINEERING_ROADMAP.md`.
These pagination dependencies precede convergence checkpoints so later checkpoints can
account for groups. This does not implement incremental/background layout or widows/orphans.

## Design choices

- Flags default false, preserving old documents. Keep-next chains include whole linking
  paragraphs and the first line of their terminal paragraph (all lines if keep-lines).
- Explicit page/section breaks win. Groups too tall for a fresh writing area relax
  keeps and use existing splitting; never create blank pages or loop indefinitely.
- Columns and footnote reservations participate. Existing table rows are already
  unbreakable; keep-next across a text/table boundary stays preserved but warns on DOCX
  import because row grouping is outside this increment.
- Format panel checkboxes apply to selected paragraphs and follow undo/style changes.
- WFG15 only when these flags or style overrides are used. WFG14 and paragraph mask
  bit 6 are reserved for unmerged PR #40's tab stops; this independent branch is based
  on main and does not add its reader. Keep mask bits 7/8 avoid a collision at integration.
- DOCX keepNext/keepLines read/write defaults, styles and direct overrides, including
  explicit false. Older native versions reject the new attributes/mask bits.
- Same C++20/Qt6 Widgets/Skia/HarfBuzz/FreeType/ICU stack; core/layout stay widget-free.

## Tasks

- [x] Record baseline Windows Release build and all six suites in fresh
  `build/paragraph-pagination`; preserve other running editors/builds.
- [x] Add model/style/native/DOCX regressions and observe missing fields fail to build;
  integrate flags through existing cascade and WFG15 serialization. Test inheritance,
  direct false, undo, old native schema rejection and independent DOCX XML.
- [x] Add layout regressions: keep heading with next line, keep short paragraphs,
  chain/oversized fallback, explicit breaks, columns, notes, and reuse after edits.
  Observe failures, then add group preflight to the existing placement loop.
- [x] Add Format panel controls and Qt workflow regression for selected paragraphs,
  undo and save/reopen. Run targeted and full suites; benchmark existing 100-page case.
- [x] Independent review, fix important findings with red/green tests, update ledger,
  handoff/status/compatibility/schema. Publish the verified change through a focused
  commit and PR into main; check CI once. No merge, Snap promotion or release.

## Review focus

Keep chains near a column bottom; long groups exceeding a page; page/section breaks
within a chain; footnotes moving with their references; inherited false overrides and
old-version/mask rejection. Each requires a corresponding regression before completion.

## Execution ledger

2026-11-20: branched `codex/wordforge-paragraph-pagination` from main dd2730a in the
existing isolated worktree. PR #39/#40 remain intact and unmerged. New baseline running.

Baseline: Windows Release build and six suites passed (19.37 s). Model tests first
failed to compile without fields, then layout/native/DOCX assertions failed before
their implementations. UI test failed for absent controls before they were added.
Missing table-boundary warnings and default-equal direct false persistence were
also observed failing before fixes. Native mask-only rejection cases passed.

Final review identified continuous-section entry, full-width footnotes over earlier
columns, oversized-chain independent keep-lines, and DOCX inherited/explicit/default
property presence defects. All reproduced as failing regressions and were fixed in
one pass, with final Windows 6/6 suites green (18.99 s). An earlier all-single-line
oversized-chain case caught loss of fresh-page start during this pass; retain the
original oversized group decision before relaxing only its links.

Ruling: table-row chaining, widow/orphan and incremental layout remain outside this
bounded slice; table-boundary keeps survive export and warn on import. Cost: some
Word pagination remains different until those features are implemented.
Ruling: Word visual parity, interactive accessibility and installed-platform proof
need application-level evidence beyond source/offscreen tests and are unverified.
Cost: those workflows may still contain defects; do not promote this as certification.
No deferred review minors. Native mask-only coverage requested by review was added.

Final verification: Windows 6/6 suites (18.99 s); Ubuntu ARM64 6/6 (7.17 s), after
the review fixes. Microsoft Word opened six focused round-trip outputs, retaining
text, structure counts and Word page counts; visual parity is not inferred.
