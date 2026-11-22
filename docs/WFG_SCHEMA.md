# Native WFG schema and migration

**WFG17 (superscript/subscript)** adds character attribute `baseline`: integer 0
(normal), 1 (superscript), 2 (subscript). It applies to text/image/note runs and both
style kinds. Omitted run values are normal; absent style values inherit. Character
mask bit 8 records an explicit override, including normal equal to the base; the
combined character mask is 0x1ff. WFG1–16 keep normal defaults and 0xff character
masks and reject the new attribute/bit. WFG17 is selected for a non-normal value,
a direct baseline bit, or any optional style baseline (including normal). Font `size`
remains nominal, not the smaller drawing size. Earlier documents retain their
previous schema selection. WFG17 includes all WFG16 paragraph properties/masks.

**WFG16 (widow/orphan control)** is written when a paragraph enables
`widowControl`, sets direct bit 9 (including explicit false), or a paragraph style
contains that optional override. Paragraph/style attribute `widow` is boolean;
omitted paragraph values are false and omitted style values inherit. The paragraph
mask is 0x3ff. WFG1–15 retain their previous defaults and masks and reject this
attribute/bit. WFG16 includes all tabs, keeps and sections from WFG14/15.


**WFG15 (paragraph pagination)** is written when a paragraph uses
keep-with-next/keep-lines, sets their direct-formatting bits, or a paragraph style
contains either optional override (including false). Paragraph and paragraph-style
attributes `keepNext` and `keepLines` are booleans; omitted paragraph values are
false, while omitted style attributes inherit. Direct masks reserve bit 7 for
keep-with-next and bit 8 for keep-lines. Explicit false is retained by the mask.
Older versions reject the new attributes and bits; other documents retain the
previous version-selection rules and their old defaults.

WFG15 includes WFG14 tab stops and uses the combined paragraph mask 0x1ff.
WFG14 permits bits 0–6 (0x7f); earlier style formats permit bits 0–5 (0x3f).
The integrated reader accepts WFG1–17. Mixed tabs/pagination documents use WFG15;
tabs-only documents retain WFG14. Regression tests cover combined properties,
explicit clears, older readers and version-specific mask rejection.

**WFG13 (sections and columns)** is written only when a paragraph ends a section or the
last section has columns. The root may carry `columns` (1–6) and `columnGap` (0–144
points) for the last section. `p` may carry `section` (`page`: the next section starts on
a new page; `continuous`: on the same page) with `sectionWidth`, `sectionHeight`,
`sectionMargin`, `sectionColumns` and `sectionGap`: the page settings of the section that
ends with this paragraph. Header, footer and first-page settings stay on the root. Table
cell paragraphs never end a section. Columns must leave at least 36 points each.

**WFG12 (Word theme colours, links, endnotes, fields)** is written only when a run,
paragraph style or character style keeps a Word theme colour, a run is a link, a note is an
endnote or the document has fields. Root children `field` (`id`, `instruction`: 1-4096
bytes of UTF-8 without control characters, `startParagraph`, `startOffset`, `endParagraph`,
`endOffset`) mark a Word field whose result is the text between the two anchors; fields
nest or are apart, never cross.
`r` and `image` may carry `link`: `http://`, `https://`, `mailto:`, `ftp://` or `file:///`
followed by at least one character, or `#` and a bookmark name; at most 2048 bytes, no
spaces or control characters; other schemes are refused. Adjacent runs with the same
`link` form one link. `note` may carry `endnote="true"`: the reference is an endnote
(numbered separately, i, ii, iii, and listed after the text). `r`, `image`, `note`, `style`
and `charStyle` may carry
`theme` next to `color`: a theme colour name (`dark1`, `light1`, `dark2`, `light2`,
`accent1`…`accent6`, `hyperlink`, `followedHyperlink`, `background1`, `text1`,
`background2`, `text2`), optionally followed by ` s:XX` (shade) and/or ` t:XX` (tint), each
two upper-case hex digits, at most once. `color` stays the colour shown; the theme name is
what DOCX export writes as `w:themeColor`. A style's `theme` requires its `color`. Writers
drop the reference once the colour no longer equals the one it was read with.

**WFG11 (table borders and shading; indents and justification)** is written only when a
table has non-default borders or shaded cells, or a paragraph or paragraph style uses a
first-line or right indent or justified alignment. `table` may carry `borderWidth` (0–6
points; 0 = no borders) and `borderColor` (opaque ARGB); children `shade` (`row`, `column`,
`color`: opaque ARGB) fill one cell each (a merged cell's top-left), at most once per cell.
`p` and `style` may carry `firstLine` (−360…360 points; negative is a hanging indent, never
left of the margin: `indent + firstLine ≥ 0`) and `rightIndent` (0–360 points); `align` may
be 3 (justified). For a list paragraph `firstLine` moves the bullet or number.

**WFG10 (tracked changes)** is written only when Track Changes is on or the document holds
tracked changes; other documents keep their earlier version. The root may carry
`trackChanges="true"`. Root elements `revision` (`id`, `author`: at most 256 bytes without
control characters, optional `date`: printable ASCII, at most 64 bytes) name who made a
change; text, picture and footnote-reference runs may carry `ins` and/or `del` naming a
revision. Every revision must be used by a run, every mark must name a revision, and
marked runs must not be empty. Deleted text stays in the paragraph until accepted.

**WFG9 (footnotes, kept DOCX parts)** is written only when a document has footnotes or
carries kept DOCX package parts. Footnotes are root elements `footnote` (`id`, `text`: one
paragraph of plain text, at most 2000 characters, no control characters) and inline
elements `note` (`id` naming a footnote, plus the run attributes) inside paragraphs; every
footnote must be referenced exactly once. Comments are root elements `comment` (`id`,
`author`, optional `date`, `text`: plain, at most 4000 characters, and the range
`startParagraph`/`startOffset`, `endParagraph`/`endOffset` as paragraph ids and offsets on
grapheme boundaries; the start must not come after the end). Kept parts come from a document imported from
DOCX that carries package parts Wordforge keeps (document properties, theme, settings, font table, web
settings, custom XML). It is WFG8 in ZIP transport plus root elements `preservedPart`
(`path` in the DOCX package, `contentType` — absent for `.rels` parts — and `part`, the
ZIP entry `preserved/N` holding the bytes) and `preservedRelationship` (`type`, `target`
naming a kept part, optional `package="true"` for package-level relationships, otherwise
from the main document). Paths must be relative, without `.`/`..` segments, limited to
letters, digits, `.`, `_`, `-`, `/`, unique, and not paths Wordforge writes itself; at most
256 parts, 64 relationships and 16 MiB. WFG8 and older files with these are rejected.

**WFG8 (headers and footers)** is written only when the page has a header, a footer or
the first-page option, so other files keep WFG7 or older. It is WFG7 plus optional root
elements `header` and `footer` (at most one each, attributes `left`, `center`, `right`,
at least one non-empty, no other content) and an optional root attribute
`differentFirstPage="true"`. Text is at most 500 characters per position without control
characters; `{PAGE}` and `{NUMPAGES}` are fields. WFG8 tables may also contain `column`
children (`width` in points, relative, one per column or none; before any merge) and
`merge` children (`row`, `column`, `rows`, `columns`; rectangles of at least two cells,
inside the table, not overlapping). The top-left cell of a merge holds its content;
covered cells keep their paragraphs. WFG7 and older files with any of these are rejected.

**WFG7 (character styles)** is written only when the document has character styles, so
other files keep WFG6 or older. It is WFG6 plus root-level `charStyle` elements (`id`,
`name`, optional `basedOn` naming another character style, and only the character
properties the style sets) and an optional run/image attribute `cstyle` naming the run's
character style. A run inherits its paragraph style's character formatting with its
character style on top; `direct` marks what differs from that. Identifiers and names are
unique across paragraph and character styles. Unknown references, cycles and `cstyle` or
`charStyle` in WFG6 are rejected. DOCX exports them as `w:type="character"` styles and
`rStyle`; import keeps the character styles runs use, with the properties their own
definition sets.

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
DOCX export writes every paragraph style to `styles.xml` (name, `basedOn`, `next`, and the
properties that differ from the base; b/i/strike as toggles) over `docDefaults` set to
Wordforge's defaults, and each paragraph's real style as `pStyle`. Built-in headings are
named `heading 1`/`heading 2` for Word. Paragraphs and runs still carry complete values.
DOCX import turns each custom paragraph style that a paragraph uses (and its bases) into a
Wordforge style holding what differs from its base, so it resolves to what Word shows;
duplicate names get " (2)", unused styles are skipped, and list numbering in styles is
still applied directly to paragraphs.

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


## WFG14 — paragraph tab stops (2026-11-19)

Written only when tab stops or a paragraph style tab override are used. WFG1–13
remain readable. Paragraphs and paragraph styles may carry a `tabs` attribute:
space-separated `position/alignment/leader` triples in points. Alignments are 0 left,
1 centre, 2 right; leaders 0 none, 1 dots. Stops must be strictly increasing, finite,
within [0,2880], maximum 64. An empty style attribute explicitly clears inherited tabs.
Paragraph direct mask bit 6 denotes tabs. Older versions reject the new attribute.
