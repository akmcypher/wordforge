# Wordforge final integration handoff

Updated 2026-11-23 IST. Read AGENTS.md and its ordered entry documents first.
This checkpoint supersedes older open/pending/old-channel snapshots. The owner
wants continued autonomous engineering, frequent saves and a usable handoff.
The remaining Wordforge roadmap is not complete.

## Recover the exact state

Repository: https://github.com/akmcypher/wordforge, Apache-2.0. Development happens
only here; the private Meridian export is frozen. The owner authorized merging
all remaining PRs. This integration preserves published history and contains:

| PR | Content | Evidence/source |
|---|---|---|
| #43 | Original v0.6.9 publication evidence | Historical docs/evidence/2026-11-20-release-0.6.9.md |
| #44 | Widow/orphan control, WFG16, DOCX and Format panel | bdf5012 |
| #45 | Superscript/subscript, WFG17, DOCX and clipboard | Feature 66ce15f; handoff 53f8fdc |
| #46 | Reviewed manual promotion of verified Snap revisions | 81ab0aa |
| #47 | Latest stable publication record | 56ab612 |

The current integration merges the older release-evidence branch into the latest
publication checkpoint and resolves only documentation overlaps. It does not
revert newer capabilities or replace the latest state with old release notes.
Obtain the final integration SHA with `git rev-parse HEAD`; do not infer it from
the branch name. Fetch main and inspect status/diffs/open PRs before new changes.
Preserve other worktrees and branches; no branch deletion is required.

## What users actually receive

**Snap stable:** version label 0.6.9-preview, amd64 revision **60**, ARM64 revision
**59**, clean source `53f8fdcefacaa8207438f97286e30ae304dd5266`. Both builds passed
installed strict-confinement self-tests in run 45058770992. Both Store archives
were checked for SHA3-384, architecture, clean embedded source and executable
SHA256 before and after stable promotion. Promotion run 45060433513 succeeded:
https://github.com/akmcypher/wordforge/actions/runs/45060433513

Do not repeat promotion. Main-triggered builds may advance edge independently;
stable's verified revision pair above is the publication result.

**Windows/GitHub:** public v0.6.9 tag remains d7afd475ff6510266d3328c859d77fe00ee427be.
Its x64/ARM64 installers and ZIPs were downloaded and checked against SHA256SUMS;
ZIP build IDs match the tag. They remain unsigned. No warning-free launch or new
Windows release was produced by the later Snap update. Released Windows 0.6.9
cannot read WFG16/17 native files. Never rewrite the tag or imply platform parity.

## Implemented behavior to preserve

- Widow/orphan control follows the paragraph/style/direct cascade, WFG16, DOCX,
  selection, undo and Format panel. Native defaults stay off; DOCX omission through
  the entire hierarchy defaults on. Explicit false stays explicit. Measured line/
  note reservations protect first/last pairs and three-line paragraphs where
  feasible; explicit breaks and feasible keep groups take priority. Impossible
  constraints relax. Late-footnote regression verifies 3+2+2 instead of 4+2+1.
- Superscript/subscript uses one TextBaseline enum through paragraph/character
  styles, direct bit 8, WFG17 and DOCX vertAlign. Nominal size remains unchanged;
  shaping derives 65% size and -0.38/+0.20 em offsets, a documented approximation.
  Dotted leaders and decorations share the derived geometry. Automatic footnote/
  endnote superscript is applied once and does not leak into subsequent typing.
  Format menu/panel, command palette, Ctrl+= and Ctrl+Shift+= toggle exclusive modes;
  HTML clipboard, typing, undo and WFG/DOCX save/reopen preserve them.
- WFG17 character mask is 0x1ff; WFG1–16 retain normal defaults/0xff character masks.
  WFG16/17 paragraph mask is 0x3ff; WFG15 uses 0x1ff, WFG14 0x7f, earlier 0x3f.
  New attributes/bits must not be silently accepted by older-version readers.
- Manual stable promotion is main-only and uses the existing CI publishing secret.
  Both exact edge archives and main ancestry must verify before either release.
  Only public pointer propagation retries (exit 3); checksum/source errors stop.
  Architecture publication is sequential, not atomic. Inspect both channels after
  a failure instead of assuming rollback. No credentials are committed.

## Verification and limitations

Superscript/subscript final Windows ARM64 six suites passed in 22.05 s; actual
Ubuntu ARM64 six passed in 6.61 s. Native CI 45038025807 passed Windows ARM64 and
Ubuntu x64 for 53f8fdc. Promotion-infrastructure CI 45059784254 passed both jobs.
The final integration changes Markdown only; no new application test result is
claimed solely from a documentation merge. Check the actual integration CI state.

Independent review defects were reproduced failing before fixes: DOCX splitting
lost direct masks (fixed with Run::slice), dotted leaders double-scaled, and
automatic note formatting leaked into typing. One review/fix pass was completed;
do not repeat it just because a different agent took over. New code needs its own
appropriately scoped verification. Existing native snapshots show controls fit,
not interactive screen-reader acceptance. Windows skips an Ubuntu-only bitmap-font
fixture. WSL does not establish strict Snap confinement; hosted installed tests did.

The original d7afd47 release passed 114-document Word structural checks, not visual
parity. The later widow increment passed six focused Word round trips. The extra
real-Word fixture for superscript/subscript stalled without a result; do not reuse
old Word scores as proof for that newer behavior. DOCX XML/inheritance tests pass;
exact Word visual fidelity remains open. See the dated evidence files.

## First next steps for the new agent

1. Fetch main, inspect status/diffs and open PRs, then work on a new codex/ branch.
   Preserve user changes and worktrees. Do not recreate the release or promotion.
2. Continue ENGINEERING_ROADMAP.md. Recommended next substantial increment is
   Phase 2A incremental pagination/checkpoints: inspect ChangeSet in document.hpp,
   LayoutEngine in layout.hpp/layout.cpp and PageView publication paths. Existing
   paragraph measurement caching is not incremental page composition.
3. Define dependency signatures and full-layout-oracle comparisons for early,
   middle and end edits, including style/section/keep/widow/list/table/note effects.
   Ship a buildable slice; no empty checkpoint/background-worker shells. Background
   work later needs immutable revisions, cancellation and stale-result fencing.
4. Remaining phases also include numbering definitions, table styles and row
   splitting, theme fonts, character spacing/language/OpenType/variable-font
   controls, remaining DOCX fidelity, accessibility and broader installed desktop
   testing. These are planned, not secretly implemented by the recent PRs.
5. Save focused verified commits and update the handoff at every checkpoint. Do not
   stop at a feature list, merge announcement or plan. Routine engineering choices
   do not need repeated approval. New public releases remain separate tasks.

## Build notes and guardrails

Windows: prepend C:/dev/wordforge-deps/msys64/clangarm64/bin to PATH, then build
`build/widow-orphan` with CMake --parallel 3 and run CTest --output-on-failure.
That directory name is historical and contains the latest feature build. Never
close the user's editor to unlock a build; use a new build directory instead.

Actual Ubuntu ARM64: WSL WordforgeUbuntu, build directory
/root/wordforge-widow-orphan-build, CMake --parallel 2, then
QT_QPA_PLATFORM=offscreen ctest --test-dir /root/wordforge-widow-orphan-build
--output-on-failure. Fresh configure uses PKG_CONFIG_PATH=/opt/wordforge-deps/pkgconfig.
Run Word automation without parallel heavy builds. Never kill a user Word process.

Keep C++20, Qt6 Widgets, Skia m143, HarfBuzz, FreeType and ICU. Core model/formats
must not depend on widgets; layout must not depend on UI. Imports preserve the
active document on failure; partial DOCX warns, requires consent and opens a copy.
Package-part preservation is not full document-body fidelity. Keep unknown-content
warnings honest. Never attach a self-hosted runner to this public repository.

Commit only with 350822086+akmcypher@users.noreply.github.com; no co-author trailers,
personal emails, secrets or rewritten published history. Local ignored out/ logs
may not exist in another checkout. The repository evidence and this handoff must
be sufficient to resume. HTTPS Git stalls were avoided using http.version=HTTP/1.1
with lowSpeedLimit=1000 and lowSpeedTime=20. Do not expose authentication output.
