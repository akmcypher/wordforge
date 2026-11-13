# Native WFG schema and migration

**WFG6 (semantic paragraph styles)** is written only when the style sheet differs
from the built-in one, so documents using built-in styles stay readable by 0.6.0.
WFG6 uses XML, or ZIP when pictures or tables exist, and may contain every WFG2–5
element. Root-level `style` elements declare `id`, `name`, optional `basedOn` and
`next`, and only the properties the style sets (`align`, `list`, `indent`, `before`,
`after`, `line`, `family`, `size`, `bold`, `italic`, `underline`, `strike`, `color`,
`highlight`). Paragraphs carry `style` (absent = Normal) instead of `preset`, and
paragraphs, runs and images may carry `direct`, a bit mask of the properties set
directly (core/document/styles.hpp: paragraph bits alignment, list, indent, before,
after, line; character bits family, size, bold, italic, underline, strike, color,
highlight). Stored values are still complete, but every value not marked direct
must equal what the style resolves to; otherwise the file is rejected, as are
unknown, duplicate or missing built-in styles (Normal, Title, Heading1, Heading2),
a based-on Normal, dangling bases or next styles, cycles, over 1024 styles and
masks with unknown bits. Resolution follows `basedOn` from the root, applying each
style's properties over the defaults.

WFG1–5 readers map `preset` 0–3 to the built-in styles and then mark every value
that differs from its style as direct (`adoptStyles`); values equal to the style
become inherited, which preserves appearance exactly. DOCX import does the same.
DOCX export writes the nearest built-in ancestor as `pStyle` with fully resolved run
properties, so custom style definitions are not yet exported (Phase 4).

Table foundation adds **WFG5 ZIP whenever Document.tables is nonempty**; readers
accept WFG1–5. Root `table` declares exact id/rows/columns; paragraph attributes
`table`, `row`, `column` declare membership. Complete rectangular row-major cells
contain one or more consecutive paragraphs. Each table has a following body
paragraph. Table IDs share the existing allocator. Malformed/incomplete/noncontiguous
membership or orphan metadata rejects through validateTables/validateDocument.
Tables and pictures coexist in the same ZIP; table-free documents keep WFG2/3/4.
This schema is implemented, while the desktop Insert Table controls are pending.

Version 0.6 writes **WFG4 ZIP when inline pictures exist**, otherwise WFG2/3 XML.
Readers accept WFG1/2/3/4. WFG4 contains `document.xml` with root version=4, existing
page/ID/bookmark semantics, and `images/<id>.png` parts. `imageResource` declares
id/path/pixelWidth/pixelHeight; paragraph `image` children retain character style
plus resource/width/height/description. Physical sizes are points. Image runs occupy
one atomic UTF-16 object offset with hard caret boundaries. Literal U+FFFC in a text
run remains text. IDs share the existing allocator namespace. Dangling/duplicate
resources, invalid dimensions, unexpected nodes/parts and unsafe ZIP paths reject.
Only referenced resources are exported; a map with orphan assets alone writes XML.

PNG resources are immutable shared owners; input bytes freeze on editor insertion.
Core validates PNG header/dimensions; desktop/Skia validates the full decode before
publication. Preview caps: 16 MiB encoded PNG/package, 64 MiB expanded ZIP/resource
total, 4096 ZIP entries, 25 million pixels/image, 16384 pixels/axis, physical 1–2880
points subject to layout fit. Older readers reject WFG4. Recovery uses this adapter.

Version 0.5 writes **WFG3 when bookmarks exist**, otherwise WFG2, and reads WFG1/2/3.
WFG3 adds empty `bookmark` children with required id, name, paragraph, offset and
affinity attributes. Offset may be zero; IDs/references may not. Affinity is before
or after. Names are unique nonempty UTF-8, at most 256 bytes, without C0 controls.
IDs share the paragraph allocator/uniqueness namespace. Anchors must resolve to
an existing paragraph/grapheme boundary. Old readers reject WFG3 rather than lose
metadata. WFG1/2 migrate with no bookmarks. Point bookmarks, not ranges, are supported.

Registered bookmarks transform through core replacement commands and snapshots;
ChangeSet.bookmarksChanged reports metadata edits. A detached Anchor is a value,
not a live tracking subscription. Other semantic anchor owners await the typed tree.

Version0.4.1 writes **WFG2** and reads **WFG1 and WFG2**. Older applications only
understand WFG1 and will reject newly saved WFG2 documents. Keep originals/backups
when moving between versions; do not disguise a WFG2 document as version1.

The XML root is `wordforge`, with `version`, width/height/margin in points. WFG2
adds required `nextId`; each `p` adds required `id`. IDs/allocator are exact
nonzero uint64 decimal integers, parsed without floating-point conversion.
`nextId` must exceed every live ID; paragraph IDs must be unique. A nextId at
uint64 maximum represents an exhausted allocator: text edits can continue but
new paragraph allocation is rejected before document/history mutation.

Paragraph attributes remain align, after, list, preset, indent, before, line and
break. Runs retain family, size, bold, italic, underline, color, strike and
highlight with Unicode text/CDATA. Existing enum/range/geometry rules remain.
Unknown nodes/attributes, malformed numbers/Unicode, DTDs/entities, invalid node
identity, missing paragraphs and unsupported versions are rejected. Current
16MiB cap is fixed; configurable limits remain Phase9 work.

WFG1 migration generates IDs in document order1..N and nextIdN+1. Other persisted
content is retained. The v1 reader stays tested through `tests/fixtures/legacy-v1.wfg`
and older parser regressions. Opening doesn't overwrite the source; a successful
native save writes WFG2 atomically through QSaveFile. Recovery uses the same adapter
and can read old snapshots. Runtime revision counters and undo stacks are not
serialized. IDs are document-scoped; future cross-document resources/anchors are
not implemented by this migration.

`validateDocument` applies shared import/reset/native-write invariants.
`Document::allocateId` checks exhaustion. Editor also retains a private allocator
high-water mark across undo/redo branches; reset intentionally starts a validated
new document scope. Normal editing/transactions cannot rewind published allocation.

## Grouped command boundary

`Editor::transact(callback)` stages an Editor candidate. Internal checkpoints are
suppressed; full batch validation precedes one history publication. Exceptions
preserve original content, selection, typing style, revisions and history.
Nested transactions, undo/redo and reset inside a callback are rejected; composite
`insertPageBreak` joins an existing transaction instead of nesting one.

The returned ChangeSet records before/after revision, inserted/removed/content-
changed IDs, pageChanged and orderChanged. A shift caused by inserted/removed
paragraphs is not a content change to every following node; reordering compares
the order of surviving IDs. Only changed/inserted paragraphs receive new content
revisions, preserving unaffected measurement caches. Selection remains index-based
until Phase 1B anchors; a callback that explicitly reorders content must supply a
valid resulting selection.

Grouped transactions still copy the current document, fully validate it and scan
IDs. This is O(N), not persistent/COW storage or incremental page layout. Single
ordinary keystrokes do not acquire this new full batch-validation pass. The model
benchmark measures this cost separately from layout and presentation.
