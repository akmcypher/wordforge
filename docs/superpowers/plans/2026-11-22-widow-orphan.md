# Widow/orphan control implementation plan

> Execute inline with executing-plans and test-driven-development; one independent
> final review. The owner requested immediate autonomous continuation, so routine
> design choices do not require another approval interview.

**Goal:** Avoid a paragraph's single first or last line being stranded at a page or
column boundary, with editable settings and faithful native/DOCX round trips.

**Spec:** Phase 2B of ENGINEERING_ROADMAP.md and docs/ENGINEERING_DIRECTIVE.md.

**Architecture:** Add one boolean to the existing paragraph/style cascade and use
measured line/footnote reservations in the existing placement loop. A shared fit
calculation must account for earlier columns and continuous-section depth. No new
document model, renderer or background-layout subsystem.

**Tech:** Preserve C++20, Qt6 Widgets, Skia m143, HarfBuzz, FreeType and ICU.

## Decisions and constraints

- Core/new-native defaults remain false to preserve pre-WFG16 rendering. DOCX
  omission means true per OOXML; imports establish that in Normal/defaults. DOCX
  export explicitly represents the native default so Word does not silently change it.
- Styles retain optional presence, including false equal to the base; direct masks
  retain explicit overrides. WFG16 and mask bit 9 add widowControl; versions 1–15
  remain readable with their old defaults and reject the new fields/bits.
- Reserve two leading/trailing lines where possible; a three-line paragraph must
  move intact if splitting would strand a line and it fits fresh. Explicit breaks
  and feasible keep groups take priority. Impossible constraints relax without
  blank-page loops. Keep-next lookahead includes the terminal paragraph's needs.
- Column/page footnotes share the page's full width. Fit checks must protect body
  content already placed in earlier columns, not just the current cursor.
- Format panel checkbox applies to selected paragraphs and follows undo/style changes.
- No app version bump, merge, Snap promotion or public release in this increment.
  Work starts from main d7afd47 on codex/wordforge-widow-orphan; PR #43 is retained.

## Work and verification

- [x] Fresh Windows baseline in build/widow-orphan: build and all six suites.
- [x] Model/style/native/DOCX failing regressions; wire default/inheritance/override
  semantics, WFG16/masks, old readers and independent OOXML assertions.
- [x] Layout regressions observed failing: first-line orphan, trailing widow,
  three-line paragraph, columns/sections, keeps, footnotes and impossible short areas.
  Reuse one placement path and shared writing-area reservations.
- [x] Format panel workflow: selection, keyboard-readable label, undo/redo,
  WFG/DOCX save/reopen. Native snapshot inspection is distinct from accessibility proof.
- [x] Final Windows and actual Ubuntu suites, existing 100/1000-page benchmarks,
  focused Word interoperability where available; one independent review/fix pass.
- [x] Update status/handoff/schema/compatibility, commit/push, open focused PR into main
  and inspect CI once. No release or promotion. (PR publication is the final step.)

## Review focus

Three-line paragraphs, paragraphs too tall to satisfy both rules, a kept heading
before a widow-controlled body, fresh columns below existing footnotes, continuous
section starts, explicit false equal to inherited/default values, old-version masks,
and cache equivalence after editing. Every case needs a regression or an explicit limit.

## Ledger

2026-11-22: clean main-based branch created in existing isolated worktree. Baseline
build started in a new directory; running editors and old release outputs untouched.
Primary OOXML reference confirms widowControl defaults on when absent from the
whole hierarchy: https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.widowcontrol

2026-11-22 final checks: Windows six suites 15.75 s; actual Ubuntu six suites
5.83 s. Six focused Word outputs reopened with text/structure/page counts retained;
sampled character formatting 99.9%. One review concern reproduced red then fixed
green: late footnote no longer leaves an avoidable final widow. Native snapshot
labels fit. See the evidence document for benchmark samples, review rulings and limits.
