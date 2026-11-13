# Wordforge agent entry point

Read in order: `AGENT_HANDOFF.md`, `CURRENT_STATUS.md`, `ENGINEERING_ROADMAP.md`,
`COMPATIBILITY_MATRIX.md`, then `docs/ENGINEERING_DIRECTIVE.md` for the full scope.
This is the public repository (https://github.com/akmcypher/wordforge, Apache-2.0).
It was exported from the private Meridian repository on 2026-11-13; that copy is
frozen. Development happens here only.

- Work on a branch and open a pull request into `main`; CI runs on every PR.
  Never rewrite published history or add `Co-authored-by` trailers. Preserve
  uncommitted work; inspect status/diffs first.
- This repository is public: commit only with the GitHub no-reply address, never
  personal emails, machine-specific secrets or private paths beyond build notes.
  Never attach a self-hosted runner (pull requests from forks would run on it).
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
