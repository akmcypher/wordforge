# Phase 2A: incremental pagination (2026-11-23)

Status: **implemented** (first slice of 2A). Evidence:
`docs/evidence/2026-11-23-incremental-pagination.md`.

## What it does

`LayoutEngine::compose(document, Layout &&previous)` gives the same result as
`compose(document)`. When `previous` is this engine's latest result, it re-paginates only
around the change:

1. **Keys.** Each paragraph gets a 64-bit pagination key from:
   - its id and revision (text and character formatting change the revision)
   - its paragraph format fields
   - its section geometry
   - its cell and table structure
   - the ids and text hashes of the notes it references

   A global key covers the note style and the document header and footer.
2. **Checkpoints.** Every step start (a paragraph, or the first paragraph of a table row
   group) records the pagination state and the output sizes so far:
   - state: page, y, column, section top/bottom, list and note numbers, pending
     footnotes
   - output sizes: lines, cells, note lines, separators

   A final checkpoint holds the state after the last paragraph, before endnotes.
3. **Resume.** Find the first changed key and the unchanged suffix. Resume at the
   checkpoint at or before the first change, stepping back over any keep-with-next chain
   that runs into it. The prefix output is reused in place.
4. **Converge.** From the second unchanged-suffix paragraph onwards, compare the state at
   each step start with the old checkpoint for the same paragraph. The page number may
   differ; pending footnotes are compared by id, text hash, number and height. On a
   match, the old tail is spliced in with these shifts:
   - paragraph index
   - page number
   - cell index

   Checkpoints and endnote ids are shifted the same way.
5. **Always recomposed:** endnotes. Headers and footers are reused while their key
   (text, style, page count, page geometry) is unchanged. They are tagged in
   `furniture` (`paragraph == noBox`) and merged back by page.
6. **Fallback.** Anything else does a full composition:
   - a foreign or stale `previous`
   - a changed global key
   - a composition that throws, which resets the engine's generation

`PageView::change` uses the incremental overload. Opening a document, refreshes and the
input-method preview stay full.

## Not done (later 2A/2B/2C work)

- Key computation and moving the reused output are still O(paragraphs) per edit. They are
  cheap (about 3 ms at 1000 pages) but not O(change).
- The model transaction copies the document (Phase 1 copy-on-write), and
  `PageRenderer::synchronize` rebuilds page keys for every page. Both are O(document).
- No background or visible-page-first layout, cancellation or stale-result fencing (2C).
- The cell index can't change within a row group. Table row groups stay one step, with
  no checkpoint inside a group.
