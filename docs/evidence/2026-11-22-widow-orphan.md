# Widow/orphan control evidence, 2026-11-22

Branch `codex/wordforge-widow-orphan`, based on main d7afd47 (released v0.6.9).
This increment has no version bump, merge, public release or Snap promotion.

## Behavior and persistence

- One `widowControl` paragraph property follows the existing style cascade, direct
  formatting mask, selection edits, undo/redo and Format panel checkbox.
- Native defaults remain false. WFG16 adds boolean `widow` and bit 9 (mask 0x3ff);
  WFG1–15 keep their defaults and version-specific masks. Tests separately reject
  new attributes and a mask-only override when relabelled as WFG15.
- DOCX widowControl follows document defaults, styles and direct properties. Omission
  through the whole hierarchy defaults true, including documents with no Normal
  definition. Export explicitly represents the native default. XML assertions and
  changed-parent round trips cover explicit false equal to the base without pinning
  inherited paragraphs. Native styles retain optional true/false presence.
- Existing placement uses measured line/note reservations for opening/closing pairs,
  three-line paragraphs, columns and continuous sections. Full-width note fit checks
  protect earlier columns. Explicit breaks and feasible keep groups take priority;
  impossible constraints relax without blank-page retry loops.
- A three-line body that cannot fit with a kept heading preserves the feasible
  heading + opening pair. With a late footnote, an infeasible final three-line tail
  is reserved as two pairs instead. The regression reproduces the avoidable
  `4 + 2 + 1` split and verifies `3 + 2 + 2` after the fix.

## Verification

Fresh Windows ARM64 Release build in `build/widow-orphan`, using the existing
Clang/Qt6 toolchain. Baseline: all six suites, 17.88 s. Final build and
`ctest --test-dir build/widow-orphan --output-on-failure`: **6/6, 15.75 s**.
The Ubuntu bitmap-font fixture remains unavailable/skipped on Windows.

Actual Ubuntu ARM64 in WSL WordforgeUbuntu, GNU 13.3.0, fresh build directory
`/root/wordforge-widow-orphan-build`. Final build and
`QT_QPA_PLATFORM=offscreen ctest --test-dir /root/wordforge-widow-orphan-build --output-on-failure`:
**6/6, 5.83 s**. Platform suites include layout, model, formats, rendering, Qt workflows
and installation self-test against the build tree; they do not establish installed
packaging or strictly confined Snap behavior.

Focused Microsoft Word 16.0 build 16.0.20625 round trip after the final fix:
all six outputs (061, 062, 063, 102, 107, 111) reopened. Body/other text,
paragraph formatting, structure counts and Word page counts retained: 100%;
sampled character formatting: 99.9%. This six-document structural sample does not
certify visual parity, widow geometry in Word, or full DOCX preservation. Dedicated
unit tests assert the widow XML semantics separately.

Qt integration checks selection, undo/redo, accessible label and WFG/DOCX reopen.
A native Windows snapshot was inspected: all three paragraph-control labels fit
without clipping. This is not interactive keyboard or screen-reader acceptance.

Final synthetic Windows benchmark samples (flags off, no incremental-layout claim):

| Requested/actual pages | Paragraphs | Initial layout | Edit/layout | Remeasured | Peak MiB |
|---|---:|---:|---:|---:|---:|
| 100/100 | 3,200 | 174.98 ms | 5.49 ms | 1 | 85 |
| 1,000/1,000 | 32,000 | 1,798.06 ms | 70.23 ms | 1 | 643 |

These are single smoke samples, not controlled performance comparisons.

## Independent review and limits

One independent read-only review found the late-footnote concern. The failing
regression was confirmed before the reservation fix; it passes afterward on both
platforms. No additional important source defects were reported. No second review
cycle. The review declined visual Word parity, interactive accessibility, installed
packaging, exhaustive old-file corpus and performance acceptance; those remain
unverified beyond the explicitly scoped checks above.

WFG16 files require the newer reader; released 0.6.9 reads only through WFG15.
Native/new-native defaults remain off for compatibility; imported DOCX may now
paginate differently because Word's omitted default is honored. Oversized lines,
notes or impossible combinations cannot guarantee both constraints. Table rows
remain unsplit; keep-next across table boundaries is preserved with a warning,
not laid out. Incremental convergence, background pagination and full Word visual
parity remain open. Partial DOCX omission warnings and consent requirements remain.

Ignored local evidence: `out/widow-final-tests.txt`, `out/widow-ubuntu-final.txt`,
`out/widow-review-red.txt`, `out/widow-review-green.txt`, `out/widow-panel.png`,
`out/widow-benchmark-100.csv`, `out/widow-benchmark-1000.csv`,
`out/fidelity-widow/report.md`. No raw analytics or private credentials are committed.
