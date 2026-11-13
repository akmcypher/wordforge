# Roadmap

The detailed, current phased plan is [ENGINEERING_ROADMAP.md](ENGINEERING_ROADMAP.md).
New agents start with [AGENT_HANDOFF.md](AGENT_HANDOFF.md). The list below is the
original high-level outline; it is not the implementation ledger.

1. Native foundation: verified dependencies, shaped page rendering, editable
   paragraph/run model, undo/redo, selection, plain text persistence and tests.
2. Layout correctness: Unicode line fitting, bidi caret navigation, paragraph
   styles, section geometry, widow/orphan and keep constraints, incremental page
   convergence, tables and headers/footers.
3. OOXML: bounded ZIP/XML reader, relationships/styles, preservation inventory,
   round-trip corpus, tables/media/sections and transactional serialization.
4. Professional editor: ribbon groups backed by commands, find/replace,
   outline, printing/PDF, autosave and recoverable sessions.
5. Advanced formats/features: ODT/RTF, annotations, tracked changes, references.
6. Hardening: parser fuzzing, sanitizers, interoperability corpus, accessibility,
   Linux packaging and measured 1/100/500/1000-page performance.

Progress is recorded with execution evidence in CURRENT_STATUS.md. A roadmap
entry is not evidence of implementation.
