# WORDFORGE — MASTER ENGINEERING AND IMPLEMENTATION DIRECTIVE

## 1. Mission

You are the principal architect and lead systems engineer responsible for turning Wordforge into a production-grade, native desktop word processor capable of competing with Microsoft Word in document fidelity, editing performance, typography, pagination, and reliability.

This is an implementation assignment, not a design exercise.

You must inspect the existing repository, understand its architecture, identify the actual implementation gaps, establish a technically sound execution plan, and start implementing immediately.

Do not stop after producing an assessment, architecture document, roadmap, prototype, mock-up, or collection of recommendations.

Your responsibility is to build the product.

**Primary repository:** `https://github.com/akmcypher/meridian`

**Working branch:** `codex/wordforge-native`

**Project directory:** `wordforge/`

**Published baseline:** commit `32b1461`

Read `wordforge/CURRENT_STATUS.md` and inspect the current working tree before making changes. The published commit may not contain all ongoing local work. Do not overwrite, discard, or revert existing uncommitted work.

---

## 2. Product vision

Wordforge must become a serious native word processor with:

- Extremely responsive editing.
- High-quality typography and text shaping.
- Accurate page layout and pagination.
- Faithful DOCX import and export.
- Robust support for long and complex documents.
- Professional document formatting.
- Reliable save, recovery, and crash handling.
- Native desktop interaction and accessibility.
- A clean, fast, polished interface.
- A maintainable architecture that can support years of development.

The objective is not to build a Word-like interface around a web editor.

The objective is to build a real document engine and a native desktop application.

The application must remain responsive while handling large documents, complex formatting, tables, images, headers, footers, sections, and multilingual content.

Do not sacrifice document correctness for superficial visual similarity.

---

## 3. Non-negotiable engineering principles

### 3.1 Preserve the existing technical foundation

The existing technology choices are fundamentally sound.

Retain and improve:

- C++20 or the repository's established compatible C++ standard.
- Qt 6 for the native application and desktop interaction.
- Skia for rendering.
- HarfBuzz for shaping.
- FreeType for font access and rasterization.
- ICU for Unicode, grapheme segmentation, bidirectional text, and internationalization.

Do not replace these with Electron, Chromium, HTML/CSS document rendering, browser-based editors, or an unrelated framework.

Do not introduce a new rendering engine simply because it is fashionable.

Evaluate additional libraries only where they solve a demonstrated engineering problem.

Use native platform capabilities where they provide a meaningful benefit, without compromising cross-platform consistency.

### 3.2 Engineering quality

Every implementation must have:

- Explicit ownership and lifetime rules.
- Well-defined interfaces.
- Strong error handling.
- Deterministic behavior.
- Automated tests.
- Performance measurements.
- Backward compatibility where applicable.
- Clear separation of model, layout, rendering, interaction, and persistence.

Avoid:

- Monolithic classes.
- Giant functions.
- UI-driven document state.
- Global mutable state.
- Unbounded caches.
- Repeated full-document recomputation.
- Unnecessary copies of large document structures.
- Silent data loss.
- Fake functionality and placeholder implementations.
- Hardcoded demo behavior.
- Premature GPU optimization.
- Unnecessary dependencies.
- Architectural rewrites without migration plans.

Do not optimize for the fewest lines of code. Optimize for correctness, performance, maintainability, and long-term extensibility.

### 3.3 Execution discipline

Work in vertical, testable implementation increments.

For each increment:

1. Inspect the existing implementation.
2. Identify the exact files and dependencies involved.
3. State the engineering decision briefly.
4. Implement the change.
5. Build the application.
6. Run relevant tests.
7. Add missing tests.
8. Measure performance where relevant.
9. Fix regressions before proceeding.
10. Update engineering documentation and status.

Do not create a large architectural plan and leave it unimplemented.

Do not repeatedly ask for permission to make ordinary engineering decisions.

Make reasonable decisions independently. Ask only when an irreversible product decision genuinely requires clarification.

Do not claim a feature is complete unless it is implemented, tested, and integrated.

---

# 4. Phase 0 — Repository audit and baseline

Start by inspecting:

- Complete directory structure.
- CMake and build configuration.
- Application entry points.
- Document model.
- Editing and selection logic.
- Layout and pagination.
- Text shaping.
- Rendering.
- Persistence.
- Undo and redo.
- Clipboard.
- Search and replace.
- Recovery.
- Existing DOCX implementation, including uncommitted changes.
- UI components.
- Test infrastructure.
- CI and packaging.
- Existing performance benchmarks.

Build the existing project before changing it.

Establish:

- Current build status.
- Existing test results.
- Platform-specific differences.
- Known failures.
- Existing performance baselines.
- Existing document-format compatibility.

Do not assume the published status document represents the complete working tree.

Create or update:

- `ARCHITECTURE.md`
- `ENGINEERING_ROADMAP.md`
- `PERFORMANCE_BASELINE.md`
- `COMPATIBILITY_MATRIX.md`

These documents must describe the actual code, not an imagined architecture.

After completing the audit, immediately begin Phase 1.

---

# 5. Phase 1 — Document model v2

This is the most important architectural improvement.

The existing paragraph/run-oriented model must evolve into a structured document representation capable of supporting professional word-processing features without repeatedly redesigning the core.

## 5.1 Structured document tree

Implement a typed document model.

At minimum, support:

**Document**
- Document metadata.
- Sections.
- Default styles.
- Named styles.
- Numbering definitions.
- Theme information.
- Document settings.
- Relationships and resource references.

**Block elements**
- Paragraph.
- Table.
- Image.
- Section break.
- Page break.
- Column break.
- Content control, where appropriate.
- Future extensible block types.

**Inline elements**
- Text runs.
- Hyperlinks.
- Inline images.
- Bookmarks.
- Fields.
- Footnote references.
- Endnote references.
- Comment anchors.
- Future extensible inline types.

**Paragraph properties**
- Alignment.
- Indentation.
- Spacing before and after.
- Line spacing.
- Tabs.
- Borders.
- Shading.
- Keep-with-next.
- Keep-lines-together.
- Page-break-before.
- Widow/orphan control.
- Numbering reference.
- Style reference.

**Character properties**
- Font family.
- Font size.
- Weight.
- Italic.
- Underline.
- Strike-through.
- Color.
- Highlight.
- Baseline position.
- Character spacing.
- Language.
- OpenType features where supported.

**Section properties**
- Page size.
- Page orientation.
- Margins.
- Gutter.
- Columns.
- Header and footer references.
- Page numbering configuration.
- Section-specific layout settings.

## 5.2 Style inheritance

Implement a proper style system.

Support:

- Named paragraph styles.
- Character styles.
- Table styles.
- Base-style inheritance.
- Default document styles.
- Direct formatting.
- Theme-aware font and color references.

Resolve effective formatting through a deterministic cascade.

Do not duplicate fully resolved formatting on every run.

Formatting changes must preserve the distinction between inherited properties and explicit overrides.

## 5.3 Stable identity and editing transactions

Introduce stable identifiers for document nodes and resources.

Implement a transaction-oriented editing interface.

Requirements:

- Atomic edits.
- Grouped edits.
- Undo and redo.
- Transaction validation.
- Change notifications.
- Selection preservation.
- Efficient localized updates.
- Safe background processing.
- Document revision tracking.

The editing layer must not directly mutate renderer internals.

The renderer must not own document content.

## 5.4 Migration and compatibility

Preserve existing `.wfg` documents.

Implement versioned migrations from the current document format to the new representation.

Requirements:

- Backward compatibility.
- Schema validation.
- Safe migration.
- Recovery from incomplete or malformed files.
- No silent loss of unsupported content.
- Round-trip tests.

Do not delete the old serialization path until migration and compatibility tests pass.

**Deliverable:** A production-ready document model with tests, migration support, and a documented schema.

---

# 6. Phase 2 — Layout and pagination engine v2

Build a real incremental layout engine.

The layout engine must be independent of the UI and renderer.

## 6.1 Layout architecture

Separate:

1. Text shaping.
2. Inline layout.
3. Paragraph layout.
4. Table layout.
5. Block layout.
6. Page composition.
7. Section composition.
8. Pagination.
9. Hit testing and caret mapping.

Create immutable or revision-scoped layout results wherever practical.

Define explicit coordinate systems and unit conversions.

Use a consistent internal measurement system with adequate precision.

Avoid repeated conversions between pixels, points, and device-independent units.

## 6.2 Incremental layout

The current implementation must evolve beyond full-document recomposition.

Implement:

- Dirty paragraph tracking.
- Dirty block tracking.
- Dirty page tracking.
- Dependency-aware invalidation.
- Paragraph layout caching.
- Shaping caching.
- Page checkpoints.
- Incremental pagination.
- Recomposition from the earliest affected position.
- Reuse of unaffected downstream layout where valid.

An edit near the end of a large document must not require reshaping every preceding paragraph.

A local formatting change must invalidate only the affected layout dependencies.

Use stable document IDs and revision numbers to avoid stale cache reuse.

## 6.3 Pagination correctness

Implement:

- Paragraph wrapping.
- Page breaks.
- Section breaks.
- Column breaks.
- Widow and orphan control.
- Keep-with-next.
- Keep-lines-together.
- Paragraph spacing.
- Page margins.
- Header and footer regions.
- Different first-page headers.
- Odd/even page headers.
- Section-specific page geometry.
- Page numbering.
- Table pagination.
- Image pagination and anchoring.

Ensure that layout behavior remains deterministic.

Avoid page oscillation and infinite recomposition loops.

## 6.4 Long-document performance

Implement efficient measurement of candidate line breaks.

Do not repeatedly shape the same text range.

Reuse glyph runs and measurement results where valid.

Support background layout for non-visible pages while maintaining safe document revisions.

Prioritize the visible page and adjacent pages.

Avoid blocking the UI thread on full-document pagination.

Implement cancellation of obsolete background layout work.

## 6.5 Caret and selection geometry

This is a critical requirement.

Support accurate mapping between:

- UTF-8 offsets.
- UTF-16 offsets where needed.
- Unicode code points.
- Grapheme clusters.
- Shaping clusters.
- Glyph positions.
- Visual cursor positions.
- Logical document positions.

Handle:

- Ligatures.
- Combining characters.
- Emoji sequences.
- Right-to-left text.
- Mixed bidirectional paragraphs.
- Complex Indic scripts.
- Arabic shaping.
- Font fallback.

Caret movement must follow visual and logical navigation rules correctly.

Selection geometry must be derived from actual shaped glyph clusters, not approximated character widths.

**Deliverable:** An incremental, deterministic, high-performance layout engine with accurate caret and selection mapping.

---

# 7. Phase 3 — Typography and text shaping

Preserve the existing HarfBuzz, FreeType, and ICU foundation.

Build a complete typography subsystem.

## Requirements

- Font discovery and registration.
- Font fallback.
- Font substitution.
- Font metrics.
- Font caching.
- Glyph-run caching.
- OpenType shaping.
- Script-specific shaping.
- Language-specific shaping.
- Bidirectional text.
- Ligatures.
- Kerning.
- Variable fonts where supported.
- OpenType feature configuration.
- Baseline alignment.
- Superscript and subscript.
- Underline and strike-through geometry.
- Accurate glyph advances.
- Font loading failure handling.

Support complex scripts, particularly Devanagari and other Indic scripts, as first-class requirements rather than optional compatibility work.

Test with real multilingual documents.

Use actual font metrics and shaping output for layout. Do not estimate glyph width using character counts or simplistic average-width formulas.

Ensure font fallback is deterministic and cacheable.

**Deliverable:** A robust typography subsystem with multilingual tests and repeatable rendering.

---

# 8. Phase 4 — DOCX import and export

DOCX interoperability is a core product requirement, not a secondary feature.

Implement a proper Office Open XML package reader and writer.

## 8.1 Package architecture

DOCX is an OPC ZIP package.

Implement:

- ZIP package reading and writing.
- `[Content_Types].xml`.
- Package relationships.
- Document relationships.
- Styles.
- Numbering.
- Settings.
- Theme.
- Headers.
- Footers.
- Media.
- Footnotes.
- Endnotes.
- Comments.
- Hyperlinks.
- Core properties.
- Custom properties.

Use a structured XML parser.

Validate relationship targets and content types.

Avoid unsafe path resolution and ZIP extraction vulnerabilities.

## 8.2 Import

Support:

- Paragraphs.
- Runs.
- Character formatting.
- Paragraph formatting.
- Named styles.
- Style inheritance.
- Numbering.
- Bullets.
- Multilevel lists.
- Sections.
- Page setup.
- Tables.
- Cell formatting.
- Merged cells.
- Images.
- Hyperlinks.
- Headers.
- Footers.
- Page numbering.
- Bookmarks.
- Fields.
- Footnotes.
- Endnotes.
- Comments.
- Track changes, where supported.

Preserve unsupported XML and package components wherever possible.

Never silently discard content.

## 8.3 Export

Generate valid DOCX packages.

Requirements:

- Correct namespaces.
- Correct relationship references.
- Valid content types.
- Correct style definitions.
- Correct numbering definitions.
- Valid section properties.
- Correct image relationships.
- Valid headers and footers.
- Correct hyperlinks.
- Correct tables.
- Correct metadata.

Use stable identifiers and deterministic serialization.

## 8.4 Unknown-part preservation

This is mandatory.

A document editor must not destroy content it does not understand.

Preserve unknown package parts and relationships across an import/edit/export cycle wherever technically possible.

Preserve unsupported OOXML fragments when their relationships and context remain valid.

Maintain a preservation registry for unsupported features.

Document which features are:

- Fully supported.
- Partially supported.
- Preserved but not editable.
- Unsupported and not safely preservable.

## 8.5 DOCX compatibility testing

Create an interoperability corpus containing real-world document patterns:

- Simple letters.
- Resumes.
- Reports.
- Large documents.
- Academic papers.
- Complex tables.
- Image-heavy documents.
- Multilingual documents.
- Headers and footers.
- Multilevel numbering.
- Tracked changes.
- Comments.
- Footnotes.
- Complex page layouts.

Test:

1. Import into Wordforge.
2. Render in Wordforge.
3. Save as DOCX.
4. Open in Microsoft Word or LibreOffice.
5. Save again.
6. Reopen in Wordforge.
7. Compare structure and visual output.

Use LibreOffice headless conversion where available for automated interoperability checks.

Never claim full Word compatibility based only on successful ZIP creation or XML parsing.

**Deliverable:** A robust DOCX reader/writer with preservation, round-trip testing, and an explicit compatibility matrix.

---

# 9. Phase 5 — Professional editing features

Implement features on top of the structured document model.

## 9.1 Tables

Support:

- Table insertion.
- Rows and columns.
- Cell merging.
- Cell splitting.
- Cell margins.
- Borders.
- Shading.
- Vertical alignment.
- Column widths.
- Row heights.
- Repeating header rows.
- Table styles.
- Nested tables where feasible.
- Table pagination.
- Row-splitting controls.

Implement proper table selection and navigation.

## 9.2 Images

Support:

- Inline images.
- Floating images.
- Text wrapping.
- Anchoring.
- Alignment.
- Scaling.
- Cropping.
- Aspect-ratio preservation.
- Image replacement.
- Image compression where appropriate.
- Image resource management.

Do not embed large image binaries directly in the core document model.

Use resource references and a managed resource store.

## 9.3 Lists and numbering

Support:

- Bullets.
- Numbered lists.
- Multilevel numbering.
- Custom numbering formats.
- Restart numbering.
- Continue numbering.
- Indentation.
- List styles.
- Nested lists.

Implement numbering as document semantics, not as typed text prefixes.

## 9.4 Headers and footers

Support:

- First-page variants.
- Odd/even page variants.
- Section-specific headers.
- Section-specific footers.
- Page numbers.
- Total page counts.
- Fields.
- Independent header/footer editing.

## 9.5 Navigation

Implement:

- Navigation pane.
- Document outline.
- Heading hierarchy.
- Search.
- Replace.
- Go to page.
- Go to section.
- Bookmarks.
- Selection navigation.

## 9.6 Clipboard

Support:

- Plain text.
- Rich text.
- HTML clipboard interoperability where appropriate.
- Internal document fragments.
- DOCX-compatible clipboard interchange where feasible.
- Images.
- Tables.

Ensure paste operations are safe, transactional, and undoable.

---

# 10. Phase 6 — Advanced document functionality

Once the core engine and DOCX foundation are stable, implement:

- Track changes.
- Comments.
- Document comparison.
- Footnotes.
- Endnotes.
- Table of contents.
- Cross-references.
- Fields.
- Page references.
- Citations.
- Bibliographies.
- Equations.
- Captions.
- Indexes.
- Content controls.
- Document protection.
- Form fields.

Each feature must be represented in the document model and serialization layer.

Do not implement advanced functionality as visual overlays or temporary UI-only state.

Where a feature cannot yet be edited, preserve its underlying structure.

---

# 11. Phase 7 — Rendering and visual quality

Keep Skia as the rendering foundation.

Improve the retained rendering architecture.

Implement:

- Page-level retained display lists.
- Glyph-run caching.
- Resource caching.
- Invalidation regions.
- Visible-page prioritization.
- Background rendering.
- Device-pixel-ratio correctness.
- Zoom correctness.
- High-DPI support.
- Selection overlays.
- Caret rendering.
- Printing.
- PDF export.

The renderer must consume immutable layout results.

It must not independently calculate document layout.

Ensure rendering is visually consistent across supported platforms.

Introduce GPU acceleration only when profiling demonstrates a measurable benefit over the existing CPU path.

Do not trade deterministic rendering for an unmeasured GPU implementation.

---

# 12. Phase 8 — Native application and UI

Wordforge must feel like a professional native desktop application.

Retain Qt Widgets unless a demonstrated limitation justifies a change.

Improve:

- Ribbon and toolbar.
- Quick-access controls.
- Font and paragraph controls.
- Styles gallery.
- Page navigation.
- Zoom.
- Status bar.
- Context menus.
- Keyboard shortcuts.
- Dialogs.
- Preferences.
- Focus mode.
- Print preview.
- Document tabs.
- Recent documents.
- Recovery notifications.

The interface must be responsive and consistent.

Do not put document business logic in widgets.

Use commands and application services between the UI and document engine.

## Native interaction

Support:

- Keyboard navigation.
- Mouse selection.
- Drag selection.
- Multi-click selection.
- Contextual menus.
- IME.
- Native clipboard.
- Accessibility APIs.
- Screen-reader-friendly controls.
- High-DPI displays.
- Native file dialogs.
- Printing.

Provide a coherent keyboard shortcut system.

Make all important operations available without a mouse.

---

# 13. Phase 9 — Persistence, recovery, and reliability

Improve the existing `.wfg` persistence and recovery mechanisms.

Implement:

- Versioned serialization.
- Atomic saves.
- Safe replacement.
- File locking.
- Recovery snapshots.
- Crash recovery.
- Autosave.
- Dirty-state tracking.
- Resource integrity checks.
- Corruption detection.
- Recovery from partial writes.
- Backup retention.
- Document validation.

Use configurable resource limits rather than arbitrary fixed file-size caps.

Handle malformed and adversarial input safely.

A failed save must never silently report success.

A failed import must never corrupt the existing document.

Add fault-injection tests for persistence and recovery.

---

# 14. Phase 10 — Performance engineering

Performance must be measured continuously.

Create repeatable benchmarks for:

- Document opening.
- Initial layout.
- Incremental layout.
- Typing.
- Deletion.
- Formatting.
- Selection.
- Scrolling.
- Page navigation.
- Search.
- Save.
- DOCX import.
- DOCX export.
- PDF export.
- Image-heavy documents.
- Long tables.
- Complex multilingual documents.

Use synthetic and real-world document fixtures.

## Initial performance objectives

These are engineering targets, not claims about current performance.

| Operation | Target |
|---|---:|
| Warm caret movement | Under 5 ms |
| Typical local edit | Under 50 ms |
| Large-document local edit | Under 100 ms |
| Cached page navigation | Under 16 ms |
| Visible-page repaint | Under 16 ms |
| Search across 1,000 pages | Under 500 ms |
| Save a typical 100-page document | Under 500 ms |

Track:

- Median latency.
- P95 latency.
- P99 latency where meaningful.
- Memory usage.
- Cache hit rate.
- Layout invalidation count.
- Recomposition scope.
- Rendering time.

Do not optimize based on intuition alone.

Identify and fix the actual bottleneck.

Avoid memory growth proportional to every historical document revision.

---

# 15. Phase 11 — Testing and quality engineering

Expand the existing test infrastructure.

Maintain separate test suites for:

1. Document model.
2. Transactions and undo/redo.
3. Typography.
4. Unicode and bidirectional text.
5. Layout.
6. Pagination.
7. Rendering.
8. Persistence.
9. DOCX import/export.
10. Clipboard.
11. UI interaction.
12. Recovery.
13. Performance.
14. Security.

## Required testing practices

- Unit tests.
- Integration tests.
- Regression tests.
- Property-based tests where useful.
- Fuzz testing for document parsers.
- Corrupt-file testing.
- DOCX interoperability tests.
- Visual regression tests.
- Cross-platform tests.
- Performance regression tests.

Build a corpus of representative documents and retain it as a regression suite.

Test at least:

- Latin text.
- Devanagari.
- Arabic.
- Mixed RTL/LTR text.
- Combining characters.
- Emoji.
- Ligatures.
- Large paragraphs.
- Very long documents.
- Tables spanning pages.
- Floating images.
- Complex styles.

A test passing on one operating system must not be assumed to pass everywhere.

---

# 16. Phase 12 — Packaging and distribution

Prepare production-grade distribution.

Target:

- Windows x64.
- Windows ARM64.
- Ubuntu Linux x86_64.
- Ubuntu Linux ARM64.
- Snap.
- AppImage or an appropriate native Linux package.
- macOS, subject to a documented feasibility assessment.

Implement:

- Installer and uninstaller.
- Application icons.
- File associations.
- Default `.wfg` association.
- DOCX opening association where appropriate.
- Version information.
- Update strategy.
- Crash reporting policy.
- Dependency packaging.
- Signing where available.
- Release artifacts.

Validate the installed application, not just the development build.

Do not call a packaging target complete until an installed build launches and passes smoke tests.

---

# 17. Architecture boundaries

Enforce clear subsystem boundaries.

Suggested logical structure:

**Core**
- Document model.
- Document transactions.
- Styles.
- Resources.
- Metadata.
- Commands.

**Typography**
- Font management.
- Font fallback.
- Text shaping.
- Glyph runs.
- Unicode services.

**Layout**
- Inline layout.
- Paragraph layout.
- Table layout.
- Block layout.
- Pagination.
- Sections.
- Hit testing.

**Rendering**
- Skia integration.
- Display lists.
- Glyph rendering.
- Page rendering.
- Selection and caret overlays.
- PDF and print output.

**Formats**
- WFG serialization.
- DOCX OPC package handling.
- OOXML import.
- OOXML export.
- Plain text.
- Future formats.

**Application**
- Main window.
- Editor surface.
- Commands.
- Ribbon.
- Navigation.
- Preferences.
- File lifecycle.

**Infrastructure**
- Autosave.
- Recovery.
- File locking.
- Diagnostics.
- Performance instrumentation.

Adapt these boundaries to the actual repository. Do not create artificial projects or layers merely to match this list.

Keep dependencies directional.

The document model must not depend on Qt widgets or Skia.

The layout engine must not depend on application UI.

The renderer must not own document semantics.

The format layer must translate between external representations and the internal document model.

---

# 18. Implementation sequencing

Execute the work in this order, adjusting only when an actual dependency requires it.

| Priority | Workstream | Completion criterion |
|---|---|---|
| P0 | Repository audit | Verified build, tests, architecture and baseline |
| P0 | Document model v2 | Structured model, styles, stable IDs, transactions |
| P0 | WFG migration | Existing documents open and round-trip |
| P0 | Layout v2 | Incremental pagination and accurate caret mapping |
| P0 | Typography | Complex-script correctness and shaping cache |
| P0 | DOCX package infrastructure | Valid OPC reader/writer and preservation |
| P0 | DOCX core interoperability | Paragraphs, styles, numbering, sections |
| P0 | DOCX tables and images | Correct import/export and layout |
| P1 | Professional editing | Tables, images, lists, headers, footers |
| P1 | Navigation and clipboard | Production-grade editing workflow |
| P1 | Reliability | Recovery, fault testing, corruption handling |
| P1 | Accessibility | Keyboard and screen-reader support |
| P1 | Advanced document features | Comments, revisions, fields, TOC |
| P2 | Distribution | Validated platform installers |
| P2 | Optimization | Benchmarked performance improvements |

Do not spend months building advanced features on top of an unstable document model.

However, do not turn foundational engineering into an endless rewrite. Deliver usable functionality continuously.

---

# 19. Working method and repository hygiene

Use the current branch.

Before editing:

- Check `git status`.
- Inspect existing changes.
- Identify files owned by ongoing work.
- Preserve local changes.

Do not force-reset the branch.

Do not run destructive cleanup commands.

Do not silently revert another agent's implementation.

Make focused commits when appropriate.

Use clear commit messages.

Keep the application buildable at each meaningful milestone.

Do not leave the repository in a broken intermediate state at the end of a work session.

Update `CURRENT_STATUS.md` with:

- Implemented features.
- Files changed.
- Tests added.
- Build and test results.
- Known limitations.
- Remaining work.
- Next execution milestone.

Maintain a technical decision record for consequential architecture choices.

Do not produce documentation that merely repeats the roadmap. Documentation must explain actual implemented behavior and constraints.

---

# 20. Definition of done

Wordforge is not complete merely because it opens a document and displays text.

The product must demonstrate:

1. Native, responsive editing.
2. Accurate typography.
3. Reliable pagination.
4. Correct Unicode and complex-script behavior.
5. Structured document semantics.
6. Professional formatting.
7. Robust tables and images.
8. Reliable DOCX import and export.
9. Preservation of unsupported DOCX content where possible.
10. Correct undo and redo.
11. Reliable autosave and crash recovery.
12. Long-document performance.
13. Accessibility.
14. Stable cross-platform packaging.
15. Comprehensive automated tests.
16. Repeatable performance benchmarks.

The compatibility matrix must honestly describe any remaining limitations.

Never advertise compatibility that has not been tested.

---

# 21. Immediate execution instructions

Start now.

**Step 1:** Inspect the complete working tree, including all local DOCX implementation work.

**Step 2:** Build the existing application and run the current test suite. Record the actual baseline.

**Step 3:** Identify the five most consequential architectural weaknesses in the current implementation. Distinguish confirmed code problems from future design requirements.

**Step 4:** Create the implementation plan with concrete milestones and file-level changes.

**Step 5:** Begin implementing the first foundational milestone immediately.

Do not stop after Step 4.

The first implementation milestone must include:

- Document model v2 design translated into actual code.
- A migration strategy for existing `.wfg` documents.
- Stable document node identity.
- A transaction boundary for editing.
- Tests proving old documents remain readable.

If the existing code already implements any of these correctly, do not rewrite it. Verify it, add missing tests, and move to the next gap.

After the first milestone, continue into incremental layout and DOCX infrastructure without waiting for another prompt, provided the environment and available execution budget permit.

## Final instruction

Think like the architect of a commercial desktop application that must be maintained for the next decade.

Do not choose a shortcut because it is easier to generate.

Do not replace difficult engineering with a mock implementation.

Do not introduce a fashionable stack without a technical justification.

Do not spend the entire session explaining what should be built.

**Inspect the code. Make the architectural decisions. Implement the product. Build it. Test it. Measure it. Fix it. Continue.**