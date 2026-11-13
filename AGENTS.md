# Wordforge agent entry point

Read in order: `AGENT_HANDOFF.md`, `CURRENT_STATUS.md`, `ENGINEERING_ROADMAP.md`,
`COMPATIBILITY_MATRIX.md`, then `docs/ENGINEERING_DIRECTIVE.md` for the full scope.
This directory is a native C++ application inside the Meridian repository;
Meridian's web/data platform is unrelated. Limit changes to `wordforge/` and
`.github/workflows/wordforge-native.yml` unless the task explicitly requires more.

- Work on `codex/wordforge-native`. Never rewrite published history or add
  `Co-authored-by` trailers. Preserve uncommitted work; inspect status/diffs first.
- Keep C++20, Qt6 Widgets, Skia m143, HarfBuzz, FreeType and ICU. Core model and
  formats must not depend on widgets; layout must not depend on the UI.
- The phase ledger distinguishes implemented, partial, planned and unverified.
  Do not treat a plan, a test count, or a packed Snap as production readiness.
- Finish a buildable vertical increment, verify it, update status/handoff and
  publish focused commits. Do not leave empty feature shells or duplicate models.
- Never close a user's running editor to unlock a build. Use a new build directory.
- Imports must preserve the active document on failure. Partial DOCX imports
  currently require a warning/consent and open as copies; this is not unknown-part
  preservation. Keep that distinction until a preservation registry is implemented.
- Test both actual Windows and Ubuntu paths where available. Offscreen tests
  alone do not prove desktop accessibility, installed packaging or Word fidelity.
- The user prefers autonomous phased implementation; routine engineering choices
  do not require repeated approval. Public product releases remain a separate task.
