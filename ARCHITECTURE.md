# Wordforge architecture

Wordforge is an independent C++20 desktop application. It began inside the
private Meridian repository but never depended on Meridian's code or runtime.

* `core/document`: UTF-16 paragraphs, styled runs, stable paragraph IDs, revisions,
  selection and atomic editing transactions. No Qt or Skia types.
  `anchors.cpp` owns registered bookmarks, paragraph-ID resolution, grapheme-safe
  edit transforms and shared caret-style lookup. `images.cpp` owns typed text/image
  inline variants and immutable shared PNG resources. Image atoms have hard caret
  edges; ordinary text containing U+FFFC remains text. Blocks/sections remain open.
* `core/typography`: ICU segmentation/bidi, FreeType faces and HarfBuzz shaping.
* `core/layout`: retained lines and page geometry in physical points; paragraph
  shaping is cached by ID/revision and layout constraints.
* `core/rendering`: Skia display lists and viewport-sized raster surfaces. Qt
  presents pixels; it does not lay out or paint document text.
* `core/formats`: WFG1/2/3 XML, WFG4 image ZIP and partial DOCX adapters, independent of UI. libzip
  handles bounded in-memory OPC containers; pugixml handles native/OOXML nodes.
  DOCX resolves main/styles/numbering relationships and flattens supported content
  into the existing model. Unsupported parts are not retained for export.
* `apps/desktop`: Qt Widgets shell, input methods, clipboard and file dialogs.

`PageView::change` applies a command to a candidate Editor, composes layout and
synchronizes the renderer before committing the candidate. The current snapshot
history shares immutable State entries but copies their document contents when
new edits are checkpointed. Grouped Editor::transact operations now publish one
history entry and a ChangeSet, with full validation and rollback before commit.

Legacy WFG1 reconstructs paragraph IDs during migration; WFG2 persists exact IDs
and allocator state. Editor high-water state survives undo branches. Layout caches
use IDs/revisions and retain only live paragraphs;
font choice/typeface caches have no explicit capacity. Measurement caching does
not make whole-document forward page composition incremental.

Atomic QSaveFile writes live in the desktop file lifecycle. Recovery uses native
snapshots, a unique path per window and a lifetime QLockFile. DOCX imports open
as unsaved copies and ask consent if unsupported content was detected.

The planned evolution is in [ENGINEERING_ROADMAP.md](ENGINEERING_ROADMAP.md).
Typed text/image inlines and image references are integrated into the existing
model. Typed blocks/sections, semantic style cascades and incremental page
convergence remain planned; there is no parallel document model.

First milestone: compile and launch a native Qt/Skia editor with shaped text,
page layout and tested basic editing. This is the foundation of the requested
product, not a claim of Word feature parity or production readiness.
