# Superscript and subscript implementation

Owner requested autonomous continued feature implementation. Execute inline with
planning/test-first skills and one independent final review; no repeated design interview.

Goal: normal/superscript/subscript as one typed character property, inherited through
paragraph/character styles and explicit direct overrides. Preserve nominal size in
model/OOXML; derive displayed size and baseline only during shaping. Reuse glyph y
placement and measured ascent/descent for renderer, wrapping and caret geometry.
Footnote numbers remain one automatic superscript, without double scaling.

WFG17 adds character `baseline` enum 0/1/2 and character mask bit 8. Versions1–16
retain normal defaults and strict 0xff character masks. All run kinds and both style
kinds preserve presence. DOCX vertAlign maps baseline/superscript/subscript; explicit
baseline equal to the parent must remain an override. Invalid modes reject or warn
according to existing format contracts. Images remain baseline objects.

Format menu/command palette and shortcuts toggle super/sub exclusively; selecting the
active mode returns to normal. Selection, typing, undo/redo, WFG/DOCX reopen must work.
No app version bump, signing purchase or release. Branch starts at main bdf5012.

Work: add failing model/style/native/DOCX/shaping/UI regressions; implement through
existing helpers; verify Windows and actual Ubuntu, focused Word interoperability,
independent review/fix pass; update handoff/status/schema/capability and open PR.
Physical font metrics determine a documented approximation; do not claim exact Word
visual fidelity or arbitrary numeric baseline shifts/OpenType superior glyph features.
Primary semantics: https://learn.microsoft.com/en-us/dotnet/api/documentformat.openxml.wordprocessing.verticaltextalignment

Final ledger: implemented and independently reviewed. All three review defects were
reproduced failing before fixes. Final Windows six suites 22.05 s; actual Ubuntu
six suites 6.61 s. Native snapshot inspected; controls fit. Extra Word automation
stalled without a result, so Word runtime/visual fidelity remains unverified.
See docs/evidence/2026-11-22-baseline-text.md for exact scope and remaining work.
