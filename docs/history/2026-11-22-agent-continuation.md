# Continue Wordforge engineering from this checkpoint

## Superseding publication checkpoint, 2026-11-23 IST

PR #45 is merged at 53f8fdc. Owner-authorized Snap stable publication is complete:
`0.6.9-preview`, amd64 revision 60 and ARM64 revision 59. Both embed clean source
53f8fdcefacaa8207438f97286e30ae304dd5266 and passed strict-confinement tests in
run 45058770992. Store archives and executable hashes were independently verified
before/after promotion. Do not repeat it. PR #46 added the reviewed manual promotion
workflow, passed native CI 45059784254, and merged at 81ab0aa. Promotion run
45060433513 succeeded, including public stable verification. Older open/pending/
revision 56/55 statements below describe the previous checkpoint. Version label and
Windows downloads remain unchanged; WFG16/17 files need the newer reader. Continue
the unfinished engineering roadmap and frequent verified saves.

Saved 2026-11-22, approximately 20:50 IST. This is an engineering handoff, not a
claim that the Word alternative is complete. Read AGENTS.md and its ordered entry
documents first; this file supplies the current continuation point.

## Owner intent and saving policy

The owner wants continued autonomous feature implementation and explicitly asked to
keep saving and leave a handoff for another agent. Do not stop at plans, a merge
announcement, or a capability list. Make a complete buildable increment, verify it,
update status/handoff, and save focused commits to a feature branch with a PR.
Save at each verified engineering checkpoint; keep unverified work clearly labelled.
Do not promise an unattended background process or claim unfinished roadmap work done.
Routine engineering choices do not require another approval interview. Public releases
are a separate task. The previous 'PUSH IT' integrated PR #44, not a blanket instruction
to publish another release or merge every future PR.

## Exact state to recover

- Public repository: https://github.com/akmcypher/wordforge (Apache-2.0).
- Active branch: `codex/wordforge-baseline-text`.
- Feature code commit: `66ce15fcdc94e5d4b5beefc13b0cd1ea2dd82e82`.
- PR #45: https://github.com/akmcypher/wordforge/pull/45, open, targeting main.
  Live check during this save: mergeable, unmerged. Native CI run 45037411672
  was in progress for 66ce15f; do not call that green. This handoff can be a later
  documentation commit and can trigger another run; always query the actual PR head.
- Main contains merged PR #44, `bdf50125b18778e04f6686feafbae5c253198a20`
  (widow/orphan control). The local origin/main ref may be stale because the merge
  was completed through the GitHub update_ref connector after a Git transport stall.
- The worktree was clean before this documentation save. No unfinished production
  code changes remain outside Git. Inspect status/diffs again before doing anything.
- PR #43 contains separate v0.6.9 publication evidence and remains a separate branch;
  preserve it. Do not casually merge/revert other branches or delete worktrees.

## Completed feature code in PR #45

Superscript/subscript uses one TextBaseline enum: Normal, Superscript, Subscript.
It follows CharacterStyle, optional paragraph/character style properties, direct
mask bit 8, typing, undo, WFG17, DOCX vertAlign and HTML clipboard. Nominal model
font size stays intact; shaping derives 65% size, -0.38 em/+0.20 em offsets.
This is an approximation, not exact Word/font-specific metrics. Images stay on
baseline. Effective glyph styles are normalized after resolution to prevent a
second application when shaping dotted leaders. Glyph metrics and y offsets flow
through the existing renderer and underline/strike geometry; cache keys include offset.

Format menu/panel and command palette expose both modes. Ctrl+= toggles subscript;
Ctrl+Shift+= toggles superscript. Modes are exclusive; selecting the active mode
restores normal. HTML-only clipboard preserves the mode and nominal size.

WFG17 adds character attribute baseline=0/1/2 and character mask 0x1ff. WFG1–16 keep
normal defaults and strict 0xff character masks; paragraph masks stay as WFG16.
Explicit normal equal to the base must remain an override. All run kinds and both
style kinds use the same model. Released 0.6.9 cannot read WFG16/17 feature files.

Three independent-review findings were reproduced failing before fixes:

1. DOCX splitting discarded direct masks. Use Run::slice to retain run metadata;
   rebuilding text/style-only copies loses explicit equal-base overrides.
2. Dotted leaders scaled baseline text twice. Resolve drawing properties once,
   then give leader glyphs the already resolved baseline offset.
3. Automatic note superscript leaked into following typing. Imported automatic
   note runs retain paragraph baseline; shaping applies reference positioning once.

One independent review/fix pass was completed. Do not repeat a full review just
because a new agent took over. New changes need appropriately scoped verification.

## Verification and reproducible commands

Final checks apply to feature code 66ce15f, before this documentation-only save:

- Windows ARM64 Release: all six CTest suites pass, 22.05 s.
- Actual Ubuntu ARM64 in WSL WordforgeUbuntu: all six suites pass, 6.61 s.
- Tests cover geometry, automatic notes, dotted leaders, old native masks, nominal
  size, DOCX changed-parent/direct-normal semantics, UI exclusivity/shortcuts/undo,
  WFG/DOCX reopen and HTML-only clipboard.
- Native Windows snapshot inspected: x²/x₂ controls fit, full accessible names set.
  Offscreen tests/snapshots do not prove screen-reader or installed desktop behavior.
- Additional Word COM fixture generation stalled before a source/result existed.
  No visible Word dialog was exposed. Only the task-owned hidden worker was stopped.
  No new real-Word runtime/visual fidelity result is claimed for this feature.

Windows build notes (do not close the user's editor; use a fresh directory if locked):

```powershell
$env:PATH='C:/dev/wordforge-deps/msys64/clangarm64/bin;'+$env:PATH
cmake --build build/widow-orphan --parallel 3
ctest --test-dir build/widow-orphan --output-on-failure
```

The directory name is historical; it contains the latest baseline-text build.
The compiler is the toolchain's c++.exe, C++20/Qt6/Skia m143/HarfBuzz/FreeType/ICU.

Ubuntu build notes:

```powershell
wsl -d WordforgeUbuntu -u root -- bash -lc 'cmake --build /root/wordforge-widow-orphan-build --parallel 2 && QT_QPA_PLATFORM=offscreen ctest --test-dir /root/wordforge-widow-orphan-build --output-on-failure'
```

Fresh Ubuntu configure needs PKG_CONFIG_PATH=/opt/wordforge-deps/pkgconfig. Keep
Windows and Ubuntu paths distinct. Run Word automation without parallel heavy builds.
A stalled optional Word check is not a reason to kill a user Word process.

Evidence: `docs/evidence/2026-11-22-baseline-text.md`.
Plan: `docs/superpowers/plans/2026-11-22-baseline-text.md`.
Ignored raw logs/snapshots: out/baseline-final-tests.txt, out/baseline-ubuntu-final.txt,
out/baseline-review-red.txt, out/baseline-note-red.txt, out/baseline-panel.png.
Those ignored files are local evidence, not guaranteed to exist in another checkout.

## Remaining work in order

1. Open. Inspect the actual head/status of PR #45 and its current CI. The save-time
   check was in progress for 66ce15f, not success. Check once; report pending and
   continue useful independent engineering rather than repeatedly polling. (path run)
2. Open. Keep this checkpoint and AGENT_HANDOFF.md current when changing branch or
   publishing the next verified increment; preserve feature and documentation commits.
   Save-time PR #45 was open and the tree clean before documentation edits. (target state checked)
3. Open. Implement the next bounded roadmap increment. Suggested priority is Phase 2A
   incremental pagination/checkpoints; inspect ChangeSet and LayoutEngine/PageView first.
   This is a recommendation, not an already written/approved implementation plan.
   Require full-layout-oracle comparisons for early/middle/end edits and dependency
   signatures including styles, sections, keeps, widow/orphan, lists, tables and notes.
   Do not equate measurement-cache reuse with incremental page composition. (path read)
4. Open. Continue Phase 1C/3/4 work from ENGINEERING_ROADMAP.md: numbering definitions,
   table styles/row splitting, theme fonts, character spacing/language/OpenType controls,
   remaining DOCX fidelity and accessibility. Choose a buildable vertical increment
   instead of empty feature shells or simultaneous unverified subsystem rewrites. (path read)
5. Open. Revisit the new Word-created baseline fixture/runtime check when COM is healthy;
   the attempted fixture script stalled and produced no result. Do not reuse old Word
   scores as proof for changed code. (path run)

## Distribution is separate

Published release is v0.6.9 at d7afd475ff6510266d3328c859d77fe00ee427be.
Its four Windows artifacts were checked against public SHA256SUMS. Snap stable is
0.6.9-preview, amd64 revision 56 / ARM64 revision 55, verified before promotion.
Do not republish those assets or assume PR #45 has shipped to users. Do not bump the
version, tag, promote stable or buy signing services under this handoff alone.

## Guardrails and next-agent prompt

Use a feature branch/PR, no rewritten published history, no co-author trailers;
commit only with 350822086+akmcypher@users.noreply.github.com. No secrets/personal
emails/private paths outside build notes. Never add a self-hosted public runner.
Preserve active documents on import failure; partial DOCX needs warning/consent
and opens as a copy. Package-part preservation is not full body-content fidelity.
Core model/formats must remain widget-independent; layout stays UI-independent.

Give the next agent this instruction:

> Read AGENTS.md, its ordered entry documents, then
> docs/handoffs/2026-11-22-agent-continuation.md. Inspect Git/PR #45 before changes.
> Continue the unfinished engineering roadmap autonomously, saving focused verified
> commits and updating the handoff at each checkpoint. Do not call the product done
> after one PR. Keep release/promotion separate and preserve all existing work.
