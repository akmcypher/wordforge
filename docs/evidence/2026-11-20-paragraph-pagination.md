# Paragraph pagination evidence, 2026-11-20

Branch `codex/wordforge-paragraph-pagination`, based on main dd2730a. No merge,
version bump, public release, Snap upload or channel promotion.

## Implemented and checked

- Keep-with-next and keep-lines participate in paragraph defaults, style cascade,
  direct masks, undo and Format panel selection edits. Defaults remain false.
- Native WFG15 retains flags, style optional true/false and direct false overrides.
  Mask-only tests reject new bits in WFG13 and reserved bit 6 in WFG15. WFG14/bit 6
  are reserved for independent tabs PR #40; reader integration remains a merge task.
- DOCX keepNext/keepLines handle omitted w:val, explicit false, defaults with no
  Normal definition, style inheritance and default-equal overrides. Export leaves
  inherited values inherited. An XML style redefinition test demonstrates that a
  direct paragraph and an explicit derived style retain their overrides while an
  inherited paragraph follows the changed parent.
- Text-group layout uses actual line and footnote measurements. Tests cover short
  paragraphs, kept headings, columns, explicit page/section breaks, continuous
  section entry, notes below prior columns, oversized groups, independent keep-lines
  within an oversized chain and changed-document comparison with a fresh engine.
- Qt integration toggles controls on selected paragraphs, undo/redo, WFG/DOCX saves
  and reopen. Native window snapshot shows both checkbox labels without clipping;
  this is not interactive keyboard/screen-reader acceptance.

## Verification

Fresh Windows ARM64 Release build in `build/paragraph-pagination`:
`cmake --build build/paragraph-pagination --parallel 3`, then
`ctest --test-dir build/paragraph-pagination --output-on-failure`.
Final result after review fixes: **6/6 suites, 18.99 s**. The bitmap emoji case
requires the Ubuntu fixture and is skipped on Windows.

Actual Ubuntu ARM64 path in WSL WordforgeUbuntu, GNU 13.3.0:
`cmake --build /root/wordforge-paragraph-pagination-build --parallel 3`, then
`QT_QPA_PLATFORM=offscreen ctest --test-dir /root/wordforge-paragraph-pagination-build --output-on-failure`.
Final result after review fixes: **6/6 suites, 7.17 s**. These are offscreen tests,
not an installed or strictly confined desktop Snap check.

Focused Word check after final fixes: Microsoft Word 16.0 build 16.0.20527 opened
all six outputs (corpus 061, 062, 063, 102, 107, 111). Body/other text, paragraph
formatting, structure counts and Word page counts retained: 100%; sampled character
formatting: 99.9%. This generated six-document structural sample does not certify
visual parity or full DOCX preservation. Report: `out/fidelity-pagination/report.md`.

Existing synthetic benchmark, single Windows samples before final review fixes
(keep flags off, no incremental-pagination claim):

| Requested/actual pages | Paragraphs | Initial layout | Edit/layout | Remeasured | Peak MiB |
|---|---:|---:|---:|---:|---:|
| 100/100 | 3,200 | 182.66 ms | 6.70 ms | 1 | 85 |
| 1,000/1,000 | 32,000 | 1,802.88 ms | 65.68 ms | 1 | 640 |

## Review and limits

Independent read-only review found four issues: skipped continuous-section entry,
footnote overlap over previous columns, independent keep-lines lost inside an
oversized chain, and DOCX cascade/presence loss. Every issue was reproduced as a
failing test before the fix. All final suites pass; native mask-only coverage was
also added. No second review cycle or deferred minor fixes.

Keep-next across table boundaries is preserved with an import warning, not laid out.
Widow/orphan control, keep-group table-row chaining, incremental convergence and
background layout remain outside this slice. Full Word visual parity, interactive
accessibility and installed packaging remain unverified. These limits can still
cause pagination/workflow differences; they are not certified by the test count.

Ignored local logs: `out/pagination-final-tests.txt`, `out/pagination-ubuntu-final.txt`,
`out/pagination-review-*-red.txt`, `out/pagination-ui-red.txt`,
`out/pagination-benchmark-100.csv`, `out/pagination-benchmark-1000.csv`,
and `out/pagination-panel.png`.
