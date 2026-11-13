# Phase 1B slice: stable anchors and bookmarks

**Goal:** Deliver navigable bookmarks as the first integrated stable-anchor feature;
the typed block/resource tree remains subsequent Phase 1B work.
**Architecture:** Document-scoped bookmark IDs share the checked node allocator.
Bookmark anchors store paragraph ID, grapheme-safe UTF-16 offset and insertion
affinity. Editor replacements transform anchors; reorder keeps IDs. WFG3 persists
bookmarks, with WFG1/2 migration retained. UI uses core commands and native dialogs.
**Stack:** Existing C++20 core/Qt/pugixml; no new dependency.
**Spec:** ENGINEERING_ROADMAP.md Phase 1B and full directive navigation/bookmarks.

## Constraints and decisions

- A bookmark is real model state participating in validation, transactions and undo.
- Unresolvable anchors fail explicitly; never jump to an unrelated vector index.
- Before/after affinity defines insertion at a bookmark; deletion collapses affected
  anchors to the replacement boundary. Recheck grapheme boundaries after joins.
- Names are unique, nonempty UTF-8, at most 256 bytes, no controls. IDs are nonzero,
  globally unique within the document (paragraph and bookmark namespace).
- Native save/recovery preserves bookmarks. DOCX currently has no bookmark adapter:
  export must refuse bookmark loss until mapping exists, rather than silently omit.
- No placeholders for tables/sections/resources are advertised as implemented.

## Tasks

- [x] Core tests first: stable ID resolution/reorder, before/after insertion,
  multi-paragraph replacement/deletion, grapheme joins, duplicate/dangling rejection,
  add/delete/undo/redo and grouped rollback.
- [x] Implement Anchor/Bookmark/resolve in document.*, editor add/remove/go-to and
  replacement anchor transforms; expose safe PageView navigation/commands.
- [x] Native tests first: WFG3 round trip, invalid IDs/offset/affinity/name, WFG1/2
  fixtures still readable. Implement version-dispatched serialization and validation.
- [x] Integrate Bookmark command on Insert and navigation dialog, keyboard shortcut,
  UI regression for create/navigate/delete and failed operation preservation.
- [x] Verify both native builds and all suites, review, update schema/status/roadmap/
  handoff, publish a focused working checkpoint. Record remaining typed-tree and
  resource work explicitly.

Execution record: this vertical slice is implemented in0.5; typed tree/resources remain pending. Core/UI navigation style and long-paragraph rescan review findings were reproduced and corrected. All five native suites pass on Windows/Ubuntu ARM64 with64 cases. Raw bookmark-specific evidence and model timing are in docs/evidence/.
