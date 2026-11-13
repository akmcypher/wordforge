# Insert Pictures: typed inline images

**Goal:** Real picture insertion, layout, resize, deletion, undo, native recovery,
PDF and DOCX export. A ribbon button alone does not count.
**Architecture:** Run content becomes a TextContent/InlineImage variant, sharing
character style. An image occupies one U+FFFC model atom with hard grapheme edges.
Document image resources hold immutable shared PNG bytes outside run content.
Layout carries resource ownership; Skia records images from explicit PNG codecs.
**Spec:** User's Word Insert screenshot and ENGINEERING_ROADMAP phase1B/5B.

## Contract

`ImageResource { uint32_t pixelWidth,pixelHeight; string png; }`.
`InlineImage { uint64_t resourceId; double width,height; string description; }`.
`Run::text() const`, `Run::image() const`, `Run::fromImage`, `Run::slice`, `appendText`.
`Document::images` is map<uint64_t,shared_ptr<const ImageResource>>; IDs share the
checked allocator. Core validates PNG header dimensions/size and references.
`Editor::insertImage(shared_ptr<const ImageResource>,width,height,description)`
returns resource ID and selects the inserted atom as one grouped undo. Resize
preserves aspect via UI, core accepts validated dimensions; deletion removes atom.

Resources: max16MiB per PNG,64MiB total,25million decoded pixels; these are explicit
preview limits, configurable-limit work remains pending. Native WFG4 uses bounded
ZIP with document.xml and images/<id>.png, keeping WFG1/2/3 readers. External URLs
are never fetched. DOCX writes real DrawingML image relationships; import may warn
until corresponding decoder is implemented, never silently discard source content.

## Work ownership and acceptance

- [x] Root: typed core/resource API and grapheme-safe editing, tests first.
- [x] Format implementer: native WFG4 transport and image refs plus DOCX media/
  DrawingML export; tests and old fixtures. Own native.cpp/docx.cpp/format tests only.
- [x] Root: image measurement/pagination, caret stops, Skia rendering/PDF with
  bounded validated codec decode and immutable resource cache ownership.
- [x] Root: Insert â†’ Picture file dialog, resize controls with aspect lock, safe
  input size/decode limits and user-visible errors; desktop/render regressions.
- [x] Independent review, full native Windows/Ubuntu suites, image save/open/PDF
  pixel evidence, updated status/handoff and a feature commit pushed to GitHub.

No floating images/wrap/crop, tables, shapes or placeholders are claimed complete.
