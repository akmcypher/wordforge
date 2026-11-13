# Engineering decisions

## 2026-11-12 — First semantic anchor owner

Start the next model slice with real point bookmarks, not an unconnected replacement
tree. Bookmark metadata owns paragraph-ID/offset/affinity anchors; editing transforms
these registered values, and undo/native persistence retain them. Detached Anchor
copies are not live tracking handles. General resource/comment/range anchors and
the canonical block/inline tree still need implementation.

Use WFG3 only when bookmarks exist, retaining WFG1/2 readers and WFG2 output for
documents without them. DOCX export refuses bookmark loss; unsupported import
warns. Native UI is backed by core commands and the existing candidate publication
boundary. Caret-style lookup is shared; bookmark navigation uses normal IME handling.
Cache Unicode boundaries per paragraph per operation: per-bookmark rescans created
a measured multi-second typing stall on a long paragraph. No new dependency.

## 2026-11-12 — Phase 1A identity and grouped commands

Keep document-scoped uint64 paragraph IDs, persist them exactly in WFG2 and retain
the WFG1 reader/migration. An editor-private allocator high-water survives undo
branches; checked allocation rejects exhaustion before command mutation. Reset
starts a new validated document scope. Do not serialize transient undo/revisions.

Grouped transactions stage a candidate, suppress internal checkpoints and publish
one history entry only after validation. ChangeSet separates order changes from
content revisions: insertion shifts must not reshape every following paragraph.
The page-break command uses this path; nested/history/reset operations are guarded.
This introduces O(N) batch validation/diff plus copies, measured independently from
layout. Persistent storage and index-independent anchors remain later work.

## 2026-11-12 — Limited DOCX checkpoint and phased handoff

Use libzip >=1.7 for bounded in-memory ZIP ownership and pugixml for OOXML. These
solve container/XML handling without replacing the native model/layout/renderer.
Resolve document/styles/numbering parts through OPC relationships and never fetch
external targets. Bound package/part bytes, expansion, entry count, style/inline
recursion and reject unsafe/duplicate names and DTDs.

The present flat model cannot retain Word's full semantics. DOCX import therefore
opens an unsaved copy, requires consent for detected unsupported content, and
never treats warning-only omission as unknown-part preservation. Save/export is
atomic. Proper semantic styles, structured content and preservation depend on
the model migration; do not expand a lossy adapter indefinitely as a substitute.

Next foundation milestone is durable identity/native migration plus grouped
transactions; retain working editing/Unicode paths and old native fixtures.
Publish the directive, phase gates, evidence and handoff in the repository. The
user requested takeover readiness before starting another architectural change.

## 2026-11-11 — Initial foundation

* C++20/CMake/Ninja, Qt6 Widgets, Skia, HarfBuzz, FreeType and ICU as requested.
* UTF-16 offsets allow direct ICU/HarfBuzz integration. Caret commands operate on
  ICU grapheme boundaries, not code units. The model has no desktop types.
* Page geometry uses points (72/inch). Device pixels enter only in presentation.
* Build the Skia CPU backend first on this Windows ARM64 host. A GPU backend
  must share retained page pictures; it cannot become a second layout engine.
  GPU support is not claimed until a context has been exercised.
* Use signed MSYS2 CLANGARM64 packages as a coherent initial dependency set.
  Record exact installed packages in build evidence. Linux builds consume the
  same pkg-config interfaces; they require a separately supplied Skia build.
* Undo entries are immutable document snapshots shared by editor copies. Desktop
  commands apply to a candidate model and publish it after layout/render success,
  so rejected formatting/imports preserve the open document and history. Run/text
  storage is still copied into each new snapshot; persistent text storage remains
  a large-document improvement, not an implemented capability.
* Keep the new native tree separate from Meridian's web product. No published
  history is rewritten; only Wordforge files are staged for milestone commits.

## 2026-11-11 — Interactive rendering and writing controls

Cache the rasterized viewport, retaining Skia page pictures as the resolution-
independent source. Content/geometry/scroll/zoom/resize invalidate that image;
selection has a separate transparent Skia layer, and the caret is a native UI
overlay. Memory remains bounded to the viewport rather than the document size.
This removes page text rendering from caret navigation and blink repaint paths.

Search initially uses exact, case-sensitive paragraph text. Candidate endpoints
must be ICU grapheme boundaries, and replacement uses the existing atomic editor
command. Broader search semantics are deferred explicitly. UI styling stays in
its own desktop module; the model/layout/typography libraries remain UI-independent.

## 2026-11-11 — Single ribbon, paragraph properties and Ubuntu

Replace stacked menu/quick/ribbon toolbars with one ribbon widget. File commands
use its File menu; save/undo/redo occupy a compact tab-row area. Group arrangement
follows the familiar [Word ribbon categories](https://support.microsoft.com/en-us/accessibility/word/keyboard-shortcuts-in-word).
Only connected features are presented; unsupported review/table/image tabs are absent.

ParagraphFormat is a typed property value separate from paragraph IDs and runs.
Lists are generated layout markers, rather than inserted characters, and decimal
ordinals participate in paragraph cache keys. Explicit page breaks remain structural
properties when a style preset is applied. Presets are concrete property applications,
not a claim of Word's cascading style inheritance or multilevel numbering support.

Ubuntu's packaged Qt6.4.2 supplies the APIs used here, so the earlier unsupported
6.5 minimum was lowered to 6.4 and verified by compiling/running the Linux application.
Linux Skia needs an explicit FreeType scanner. Bitmap-only font faces select a real
fixed strike and scale shaped metrics to requested points; they do not silently
substitute missing-glyph boxes or native bitmap dimensions for document geometry.

## 2026-11-13 — Inline pictures and resource ownership

Use typed text/image Run content in the authoritative model, with one atomic
UTF-16 object offset per picture. Image edges override combining-text grapheme
joining. Encoded PNG resources freeze at insertion and share immutable ownership
across undo/layout/render caches. Delete removes current references/resources;
export filters orphan bytes independently. Full pixel decode succeeds before UI
publication. WFG4 is a bounded ZIP transport; prior text/bookmark XML stays readable.
DOCX exports actual inline DrawingML and PNG media; image import still warns.
This increment delivers inline placement/size, not floating wrap/crop or full Word.
