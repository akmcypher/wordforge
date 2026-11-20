# Tabs and TOC acceptance, 2026-11-19

Branch: `codex/wordforge-tabs`, PR #40. These checks extend 2453fac; no merge,
version bump, public release or signing was performed.

- Windows ARM64 Release: `cmake --build build/tabs --parallel 3`, then
  `ctest --test-dir build/tabs --output-on-failure`: 6/6 suites in 15.73 s.
  The Ubuntu-only bitmap emoji case remains skipped on Windows.
- Layout: left/centre/right anchors use real shaped widths with mixed font size
  and weight, first-line/left indents, with and without bullets.
- Qt workflow: menu actions insert/update a TOC; changed margins move the right
  stop; undo/redo restores it; WFG/DOCX save/reopen retain text, fields, alignment
  and leaders. DOCX positions allow half a twip for rounding. This offscreen
  integration test does not prove interactive accessibility.
- DOCX custom automatic interval: missing-warning regression failed before the
  fix. A 72 pt interval now warns that rendering uses 36 pt, with settings bytes
  preserved. The standard 36 pt interval produces no warning.
- Hosted `Wordforge native` run 44472935635 for 2453fac: success on Ubuntu x64
  and Windows ARM64. Local WSL ARM64 remains unverified. Follow-up commits need
  their own hosted run.
- Word 16.0 build 16.0.20527: frozen roundtrip tool; focused corpus documents
  061, 062, 063, 102, 107, 111. Word opened 6/6 outputs. Body/other text,
  paragraph formatting, structure counts and Word page counts: 100%; sampled
  character formatting: 99.9%. This six-file structural sample does not measure
  pixels or certify general fidelity. Unsupported-content warnings remain.
- Portable ARM64: 138 files, 96.5 MB unpacked, 37.0 MB ZIP. SHA256 checked;
  extracted-package self-test passed with only Windows system paths on PATH and
  Qt/Wordforge overrides removed. Includes fonts, spelling, image formats,
  native/DOCX persistence, rendering, PDF, print and clipboard. No installer or
  signature verification. Internal version remains 0.6.8 with a dev build ID.
- Existing 100-page synthetic benchmark: 3,200 paragraphs, initial layout
  249.48 ms, edit/layout 6.08 ms, one paragraph remeasured, save 19.15 ms,
  peak 85 MiB. Single sample, no comparison; not a tab-heavy latency claim.

Native preview launched. Computer-use window state/screenshots disagreed, so
coordinate input was stopped. Interactive TOC acceptance and Word visual comparison
remain open. Decimal/bar tabs, other leaders, custom automatic interval rendering,
RTL-specific geometry and a ruler remain unsupported.

Ignored local evidence under `out/`: `resume-tests.txt`,
`fidelity-tabs-resume/report.md`, `tabs-benchmark.csv`, `tabs-preview-package/`
and `tabs-package-check/self-test.txt`.
