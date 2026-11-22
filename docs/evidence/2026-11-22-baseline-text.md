# Superscript/subscript evidence, 2026-11-22

Branch `codex/wordforge-baseline-text`, based on main bdf5012 (merged PR #44).
This increment has no app version bump, release, Snap promotion or signing purchase.

## Implemented

- One typed TextBaseline property (normal/superscript/subscript) participates in the
  paragraph and character style cascade, direct bit 8, nominal size, undo and typing.
- WFG17 serializes run and optional-style baseline values, including explicit normal.
  WFG1–16 retain normal defaults and strict 0xff character masks. WFG17's character
  mask is 0x1ff; WFG16 paragraph masks/properties are unchanged. New attributes and
  mask-only overrides relabelled as older versions are rejected. Invalid enum values
  are rejected during document validation.
- DOCX vertAlign maps all three values through defaults, paragraph/character styles
  and direct runs. Explicit normal equal to its base remains an override; inherited
  text follows later parent changes. Nominal font size is retained. XML assertions
  and changed-parent round trips independently exercise these distinctions.
- Shaping derives 65% size and -0.38 em superscript/+0.20 em subscript offsets from
  nominal size. Glyph y, ascent/descent and effective font size drive existing
  layout/rendering. Underline/strike use the same offset; page-record keys include it.
  Dotted tab leaders use the same effective size and baseline. Images remain baseline
  objects. Automatic footnote/endnote positioning is applied once, without changing
  subsequent typing into superscript.
- Format menu/panel and command palette expose mutually exclusive modes. Ctrl+=
  toggles subscript; Ctrl+Shift+= toggles superscript; toggling the active mode restores
  normal. Selection, undo, shortcut behavior, WFG/DOCX save/reopen and HTML-only
  clipboard round trips (including nominal size) are covered by Qt integration tests.

## Verification and review

Windows ARM64 Release build and `ctest --test-dir build/widow-orphan --output-on-failure`:
**6/6 suites, 22.05 s**. The build tree was reused without closing any running editor.
Actual Ubuntu ARM64 in WSL WordforgeUbuntu, build and
`QT_QPA_PLATFORM=offscreen ctest --test-dir /root/wordforge-widow-orphan-build --output-on-failure`:
**6/6 suites, 6.61 s**. Windows lacks the Ubuntu bitmap-font fixture, which remains
skipped there. These checks exercise build-tree applications, not installed packages.

One independent read-only review found three defects, all reproduced failing before
fixes and included in the final platform runs:

1. DOCX text splitting discarded direct masks. Reusing Run::slice retains metadata
   instead of rebuilding incomplete run copies.
2. Dotted leaders reshaped an already-scaled style and applied baseline scaling twice.
   Derived glyph styles are normalized after resolution; copied leader glyphs receive
   the existing resolved offset.
3. Imported automatic note vertAlign leaked into following typing. Note runs retain
   the paragraph's baseline; automatic reference placement stays in the shaper.

No second review cycle. The review declined full Word visual fidelity, interactive
accessibility, installed packaging and final platform acceptance; the scoped platform
checks above were performed by the implementer. Those broader claims remain unverified.

Native Windows snapshot inspected: x²/x₂ controls fit the Format panel alongside
strikethrough and clear formatting. Accessible names retain the full command labels.
This does not prove screen-reader navigation or interactive assistive-tech behavior.

An additional real-Word fixture-generation/round-trip attempt stalled before producing
the source document or a result. No visible Word dialog was exposed. The task's own
hidden Word automation worker was stopped; no user document/editor was closed.
Therefore **no new Microsoft Word runtime fidelity result is claimed for this change**.
Earlier six-document results concern previous increments and are not reused as proof.
DOCX source-level and XML round-trip tests pass, but exact Word visual matching is open.

## Limits and next work

Positioning is a deterministic approximation, not font-specific OpenType superior/
inferior glyph selection. Arbitrary numeric baseline shifts, character spacing,
language, OpenType features and variable fonts remain planned. WFG17 files require
the newer reader; released 0.6.9 cannot open them. Partial DOCX warnings/consent and
package-part preservation limits still apply. Incremental/background pagination,
numbering, table styles/row splitting, full Word fidelity and accessibility remain
engineering work; this feature checkpoint does not complete those phases.

Ignored local evidence: `out/baseline-text-red.txt`, `out/baseline-text-tests.txt`,
`out/baseline-review-red.txt`, `out/baseline-note-red.txt`, `out/baseline-final-tests.txt`,
`out/baseline-ubuntu-final.txt`, `out/baseline-panel.png`. The failed Word attempt's
script/output remain under ignored `out/`; no private paths or credentials are published.
