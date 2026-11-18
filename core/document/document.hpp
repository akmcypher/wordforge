#pragma once
#include "core/document/styles.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace wf {
struct CharacterStyle {
    std::string family = "Arial";
    double size = 12;
    bool bold = false, italic = false, underline = false;
    bool strike = false;
    std::uint32_t color = 0xff202631;
    std::uint32_t highlight = 0;
    ThemeColor theme; // the Word theme colour `color` came from, if any
    bool operator==(const CharacterStyle &) const = default;
};
void validateCharacterStyle(const CharacterStyle &);
enum class Alignment { Left, Center, Right, Justify };
enum class ListKind { None, Bullet, Decimal };
enum class ParagraphPreset { Normal, Title, Heading1, Heading2 };
struct ParagraphFormat {
    Alignment alignment = Alignment::Left;
    ListKind list = ListKind::None;
    double indentLeft = 0, spaceBefore = 0, spaceAfter = 8, lineSpacing = 1.15;
    // Points: the first line starts this far right of indentLeft (negative: a hanging
    // indent, never left of the margin); the right edge sits indentRight inside the margin.
    // For a list paragraph the first-line indent moves the bullet or number.
    double indentFirstLine = 0, indentRight = 0;
    bool pageBreakBefore = false;
    bool operator==(const ParagraphFormat &) const = default;
};
void validateParagraphFormat(const ParagraphFormat &);
struct ImageResource {
    std::uint32_t pixelWidth = 0, pixelHeight = 0;
    std::string png;
    bool operator==(const ImageResource &) const = default;
};
struct InlineImage {
    std::uint64_t resourceId = 0;
    double width = 0, height = 0;
    std::string description;
    bool operator==(const InlineImage &) const = default;
};
struct TextContent {
    std::u16string value;
    bool operator==(const TextContent &) const = default;
};
void validateInlineImage(const InlineImage &);
// The superscript mark in the text that a footnote belongs to.
struct NoteReference {
    std::uint64_t id = 0; // a Footnote::id
    bool operator==(const NoteReference &) const = default;
};
class Run {
  public:
    Run(std::u16string text = {}, CharacterStyle style = {});
    CharacterStyle style;
    CharacterMask direct = 0; // fields of `style` set directly, not inherited from the styles
    std::string characterStyle; // named character style; empty for none
    // Hyperlink target: a web or mail address ("https://...", "mailto:...") or "#name" for a
    // bookmark in this document; empty for none. Adjacent runs with one target are one link.
    std::string link;
    // Tracked changes: the Revision that inserted or deleted this run (0 for none). Deleted
    // text stays in the document, shown struck through, until the change is accepted.
    std::uint64_t inserted = 0, deleted = 0;
    bool tracked() const {
        return inserted || deleted;
    }
    const std::u16string &text() const;
    const InlineImage *image() const;
    const NoteReference *note() const;
    // An indivisible object (picture or footnote reference) occupying one U+FFFC.
    bool atom() const;
    static Run fromImage(InlineImage, CharacterStyle = {});
    static Run fromNote(NoteReference, CharacterStyle = {});
    Run slice(std::size_t start, std::size_t end) const;
    void appendText(const std::u16string &);
    bool operator==(const Run &) const = default;

  private:
    std::variant<TextContent, InlineImage, NoteReference> content_;
};
void validateImageResource(const ImageResource &);
// Throws unless `target` is a usable link: at most 2048 bytes, no spaces or control
// characters, and http(s), mailto, ftp or file address, or "#" plus a bookmark name.
void validateLink(const std::string &target);
// Whether a link may be opened outside Wordforge (http, https, mailto).
bool externalLink(const std::string &target);
using ImageResources = std::map<std::uint64_t, std::shared_ptr<const ImageResource>>;
struct TableCell {
    std::uint64_t tableId = 0;
    std::size_t row = 0, column = 0;
    bool operator==(const TableCell &) const = default;
};
// A rectangle of cells shown as one. The top-left cell holds the content; the covered
// cells keep their own paragraphs (normally one empty one), shown inside the merged cell.
struct CellMerge {
    std::size_t row = 0, column = 0, rows = 1, columns = 1;
    bool contains(std::size_t r, std::size_t c) const {
        return r >= row && r < row + rows && c >= column && c < column + columns;
    }
    bool operator==(const CellMerge &) const = default;
};
// The lines around and between a table's cells, all alike. Width 0: no borders.
struct TableBorder {
    double width = 0.5; // points, at most 6
    std::uint32_t color = 0xff805a3b; // opaque ARGB
    bool operator==(const TableBorder &) const = default;
};
// A cell's background colour (for a merged cell, set on its top-left cell).
struct CellShading {
    std::size_t row = 0, column = 0;
    std::uint32_t color = 0; // opaque ARGB
    bool operator==(const CellShading &) const = default;
};
struct Table {
    std::uint64_t id = 0;
    std::size_t rows = 0, columns = 0;
    // The background of the cell at (row, column), 0 for none.
    std::uint32_t fillAt(std::size_t row, std::size_t column) const;
    // Relative column widths in points (scaled to the text width); empty = equal.
    std::vector<double> widths;
    std::vector<CellMerge> merges;
    TableBorder border;
    std::vector<CellShading> shading;
    // The merge covering (row, column), if any.
    const CellMerge *mergeAt(std::size_t row, std::size_t column) const;
    bool operator==(const Table &) const = default;
};
// Smallest column width that leaves a cell 36 pt of text after its 4 pt paddings.
constexpr double minimumColumnWidth = 44;
// Left edges of the columns plus the right edge, fitting `width`; narrow columns are
// widened to the minimum at the expense of the others. Throws if they cannot fit.
std::vector<double> columnEdges(const Table &, double width);
struct Paragraph : ParagraphFormat {
    Paragraph(std::uint64_t identifier = 0) : id(identifier) {}
    std::uint64_t id = 0, revision = 0;
    std::string style = "Normal";
    ParagraphMask direct = 0; // ParagraphFormat fields set directly, not inherited from `style`
    std::vector<Run> runs{{u"", {}}};
    std::optional<TableCell> cell;
    std::u16string text() const;
    std::vector<std::size_t> boundaries() const;
    bool operator==(const Paragraph &) const = default;
};
// Text repeated at the top or bottom of every page, in three tab-like positions.
// "{PAGE}" and "{NUMPAGES}" are replaced by the page number and the page count.
struct PageBand {
    std::u16string left, center, right;
    bool empty() const {
        return left.empty() && center.empty() && right.empty();
    }
    bool operator==(const PageBand &) const = default;
};
struct PageStyle {
    double width = 595.28, height = 841.89, margin = 56.7;
    PageBand header, footer;
    bool differentFirstPage = false; // no header or footer on the first page
    bool operator==(const PageStyle &) const = default;
};
// The band's text on page `number` (1-based) of `count`, fields substituted.
std::u16string bandText(const std::u16string &, std::size_t number, std::size_t count);
void validatePage(const PageStyle &);
struct Position {
    std::size_t paragraph = 0, offset = 0;
    auto operator<=>(const Position &) const = default;
};
enum class Affinity { Before, After };
struct Anchor {
    std::uint64_t paragraphId = 0;
    std::size_t offset = 0;
    Affinity affinity = Affinity::After;
    bool operator==(const Anchor &) const = default;
};
struct Bookmark {
    std::uint64_t id = 0;
    std::string name;
    Anchor anchor;
    bool operator==(const Bookmark &) const = default;
};
void validateBookmarkName(const std::string &);
// A review comment on a range of text. The range grows when text is typed at its edges
// (start has Before affinity, end After) and collapses to a point when deleted.
struct Comment {
    std::uint64_t id = 0;
    std::string author; // UTF-8
    std::string date;   // ISO 8601 (as Word writes it), or empty
    std::u16string text; // one paragraph of plain text
    Anchor start, end;
    bool operator==(const Comment &) const = default;
};
void validateCommentText(const std::u16string &);
void validateCommentAuthor(const std::string &);
// DOCX package parts Wordforge does not edit (document properties, theme, settings, font
// table, web settings, custom XML) kept byte for byte from an imported file, so saving to
// DOCX writes them back. This is package-part preservation only: unknown content inside
// the document body is still omitted (with a warning) on import.
struct PreservedPart {
    std::string path, contentType, data; // relationship parts (.rels) have no content type
    bool operator==(const PreservedPart &) const = default;
};
struct PreservedRelationship {
    bool package = false; // from the package root; otherwise from the main document
    std::string type, target; // target: the path of a preserved part
    bool operator==(const PreservedRelationship &) const = default;
};
struct PreservedPackage {
    std::vector<PreservedPart> parts;
    std::vector<PreservedRelationship> relationships;
    bool operator==(const PreservedPackage &) const = default;
};
// Footnote text (one paragraph of plain text), shown at the bottom of the page that holds
// its reference. Footnotes are numbered in the order of their references.
struct Footnote {
    std::uint64_t id = 0;
    std::u16string text;
    bool operator==(const Footnote &) const = default;
};
void validateFootnoteText(const std::u16string &);
// Who made a tracked change and when (ISO 8601, or empty).
struct Revision {
    std::uint64_t id = 0;
    std::string author, date;
    bool operator==(const Revision &) const = default;
};
// How a run looks with its tracked change shown: insertions underlined, deletions struck
// through, both in the change colour.
CharacterStyle shownStyle(const Run &);
constexpr std::size_t maxPreservedBytes = 16 * 1024 * 1024, maxPreservedParts = 256;
void validatePreserved(const PreservedPackage &);
struct Document {
    std::vector<Paragraph> paragraphs{{1}};
    std::shared_ptr<const PreservedPackage> preserved; // set by DOCX import
    std::vector<Bookmark> bookmarks;
    ImageResources images;
    std::vector<Table> tables;
    std::vector<Footnote> footnotes; // each referenced exactly once in the text
    std::vector<Comment> comments;
    std::vector<Revision> revisions; // each used by at least one run
    bool trackChanges = false;       // record edits as tracked changes
    StyleSheet styles = builtinStyles();
    PageStyle page;
    std::uint64_t revision = 0, nextId = 2;
    std::u16string text() const;
    std::uint64_t allocateId();
    Anchor anchorAt(Position, Affinity = Affinity::After) const;
    std::optional<Position> resolve(const Anchor &) const;
    // Positions of every anchor (bookmarks, then each comment's start and end).
    std::vector<Position> bookmarkPositions() const;
    std::vector<Anchor *> anchors();
    std::vector<const Anchor *> anchors() const;
    const CharacterStyle &styleAt(Position) const;
    // The run that typing at `position` continues (the one before it at a boundary).
    const Run &runAt(Position) const;
    void pruneUnusedImages(); // and footnotes and revisions no longer referenced
    const Footnote *footnote(std::uint64_t id) const;
    const Revision *findRevision(std::uint64_t id) const;
};
void validateDocument(const Document &);
// Effective formatting of a paragraph style after following its based-on chain.
struct ResolvedStyle {
    ParagraphFormat paragraph;
    CharacterStyle character;
};
ResolvedStyle resolveStyle(const StyleSheet &, const std::string &id);
void validateStyles(const StyleSheet &);
// For content whose formatting was stored fully resolved (older files, DOCX, generated
// documents): unknown styles become Normal and every value that differs from the style is
// marked as set directly. Values equal to the style's become inherited.
void adoptStyles(Document &);
// A new empty paragraph carrying the resolved formatting of `style`, with nothing direct.
Paragraph styledParagraph(const Document &, std::uint64_t id, const std::string &style = "Normal");
std::string presetStyleId(ParagraphPreset);
std::optional<ParagraphPreset> presetFor(const std::string &styleId);
void validateTables(const Document &);
struct Selection {
    Position anchor, focus;
};
std::pair<std::size_t, std::size_t> selectedParagraphs(Selection);
// Counts for the word-count display. Paragraphs are non-empty lines; characters are
// user-perceived characters (graphemes), so an emoji or accented letter counts once.
struct TextCount {
    std::size_t words = 0, characters = 0, charactersWithoutSpaces = 0, paragraphs = 0;
    bool operator==(const TextCount &) const = default;
};
TextCount countText(const std::u16string &);
std::string utf8(const std::u16string &);
std::u16string utf16(const std::string &);
std::size_t previousGrapheme(const std::u16string &, std::size_t);
std::size_t nextGrapheme(const std::u16string &, std::size_t);
std::vector<std::size_t> graphemeBoundaries(const std::u16string &);
std::size_t previousGrapheme(const Paragraph &, std::size_t);
std::size_t nextGrapheme(const Paragraph &, std::size_t);
struct ChangeSet {
    std::uint64_t beforeRevision = 0, afterRevision = 0;
    std::vector<std::uint64_t> inserted, removed, changed;
    bool pageChanged = false;
    bool orderChanged = false;
    bool bookmarksChanged = false;
    bool resourcesChanged = false;
    bool tablesChanged = false;
    bool notesChanged = false; // footnote texts, additions or removals
    bool commentsChanged = false;
    bool revisionsChanged = false; // the revision list or the track-changes switch
};

class Editor {
  public:
    Document document;
    Selection selection;
    CharacterStyle typingStyle;
    void replace(const std::u16string &);
    void eraseBackward();
    void eraseForward();
    void format(const std::function<void(CharacterStyle &)> &);
    void align(Alignment);
    void formatParagraph(const std::function<void(ParagraphFormat &)> &);
    void applyPreset(ParagraphPreset);
    // Gives the selected paragraphs `style`, clearing their direct formatting.
    void applyStyle(const std::string &style);
    // Redefines the caret paragraph's style from its current formatting (paragraph and the
    // character formatting at the caret); every paragraph using or inheriting from the
    // style follows, except for properties they set directly.
    void updateStyleFromSelection();
    // Defines a new paragraph style named `name` from the caret paragraph (based on its
    // current style) and gives it to the selected paragraphs without changing how they
    // look. Returns the new style's identifier.
    std::string createStyleFromSelection(const std::string &name);
    // Gives the selected text the character style `id` (empty: none), clearing its direct
    // character formatting. With no selection it sets the typing formatting.
    void applyCharacterStyle(const std::string &id);
    // Makes the selected text a link to `target` (empty: removes links from it). With no
    // selection it inserts `text` (the target itself when empty) as a link at the caret, or,
    // with an empty target, removes the whole link around the caret.
    void setLink(const std::string &target, const std::u16string &text = {});
    // The link at the caret (the selection's start), or empty.
    std::string linkAt() const;
    // The text range of the link around the caret, or nullopt.
    std::optional<Selection> linkRange() const;
    // Defines a character style from the selection's formatting (where the selection ends)
    // relative to its paragraph style, and gives it to the selected text without changing
    // how it looks. Returns the new style's identifier.
    std::string createCharacterStyleFromSelection(const std::string &name);
    // Renames a custom paragraph or character style; built-in styles keep their names.
    void renameStyle(const std::string &id, const std::string &name);
    // Removes a custom style. Its paragraphs (or text, for a character style) take the
    // style it was based on, keeping their direct formatting; styles based on it absorb
    // its properties so they look the same.
    void deleteStyle(const std::string &id);
    // Removes direct character formatting from the selection (or the typing style).
    void clearCharacterFormatting();
    void insertParagraphBreak();
    void insertPageBreak();
    void setPage(PageStyle);
    bool undo();
    bool redo();
    void reset(Document);
    ChangeSet transact(const std::function<void(Editor &)> &);
    std::uint64_t addBookmark(const std::string &, Affinity = Affinity::After);
    void removeBookmark(std::uint64_t);
    bool goToBookmark(std::uint64_t);
    std::uint64_t insertImage(std::shared_ptr<const ImageResource>, double width, double height,
                              const std::string &description = {});
    std::optional<InlineImage> selectedImage() const;
    // Inserts a footnote reference at the caret (replacing the selection) with `text`.
    std::uint64_t insertFootnote(const std::u16string &text);
    // The footnote whose reference is next to the caret or selected, if any.
    std::optional<std::uint64_t> selectedFootnote() const;
    void setFootnoteText(std::uint64_t id, const std::u16string &text);
    // Comments the selection (or the caret position when nothing is selected).
    std::uint64_t addComment(const std::u16string &text, const std::string &author,
                             const std::string &date);
    void setCommentText(std::uint64_t id, const std::u16string &text);
    void deleteComment(std::uint64_t id);
    // The current text range of a comment.
    std::optional<Selection> commentRange(std::uint64_t id) const;
    // Tracked changes. While document.trackChanges is set, typing and deleting text are
    // recorded under `reviewer` and `reviewDate` (set by the application) instead of being
    // applied: deleted text stays, marked, until accepted. Paragraph breaks, pictures,
    // footnotes, formatting and table structure are not tracked.
    std::string reviewer = "Author", reviewDate;
    void setTrackChanges(bool);
    // Accept or reject the tracked changes in the selection, or the change at the caret.
    // Returns false when there is none.
    bool acceptChanges();
    bool rejectChanges();
    void acceptAllChanges();
    void rejectAllChanges();
    // Selects the next tracked change after the caret, wrapping; false when there is none.
    bool nextChange();
    // The tracked change the caret is in, or whether the selection holds one.
    std::optional<Selection> changeAtCaret() const;
    bool selectionHasChanges() const;
    void resizeImage(double width, double height);
    std::optional<TableCell> selectedCell() const;
    void insertTable(std::size_t rows, std::size_t columns);
    void tableRow(bool insert, bool before = false);
    void tableColumn(bool insert, bool before = false);
    void removeTable();
    bool nextCell(bool backward = false);
    // Merge the cells spanned by the selection (two different cells of one table).
    void mergeCells();
    // Undo the merge holding the caret.
    void splitCell();
    // Set the caret column's width in points; the others shrink or grow to fit the page.
    void setColumnWidth(double points);
    void distributeColumns();
    // Borders of the caret's table.
    void setTableBorder(TableBorder);
    // Fills the selected cells (or the caret's cell) with `color`; 0 removes the fill.
    void shadeCells(std::uint32_t color);
    // Formatted clipboard support. copySelection returns the selected text runs with
    // their paragraph formatting (no table membership or pictures). insertFragment
    // replaces the selection with such paragraphs as one undoable step; the first
    // paragraph keeps the target paragraph's formatting unless that paragraph was empty.
    std::vector<Paragraph> copySelection() const;
    void insertFragment(const std::vector<Paragraph> &);

  private:
    struct State {
        Document document;
        Selection selection;
        CharacterStyle typingStyle;
    };
    std::vector<std::shared_ptr<const State>> undo_, redo_;
    std::uint64_t serial_ = 0;
    std::uint64_t idHighWater_ = 2;
    bool inTransaction_ = false;
    // When set, text that replace() inserts takes this character style.
    const std::string *forcedCharacterStyle_ = nullptr;
    const std::string *forcedLink_ = nullptr; // the link of pasted text, while pasting
    bool untracked_ = false; // set while tracked-change machinery edits directly
    void checkpoint();
    void validate(Selection) const;
    void tracked(const std::function<void(Editor &)> &);
    void trackedReplace(const std::u16string &, Selection);
    Position trackedDelete(Position start, Position end);
    void trackedErase(bool backward);
    std::uint64_t currentRevision();
    // Rewrites the runs of `paragraph` in [from, to) through `change`.
    void markRuns(std::size_t paragraph, std::size_t from, std::size_t to,
                  const std::function<void(Run &)> &change);
    // Drops or rewrites each run piece of [start, end): `decide` returns true to drop it.
    // Returns how much text was dropped from the last paragraph.
    std::size_t resolveRange(Position start, Position end,
                             const std::function<bool(Run &)> &decide);
    bool resolveChanges(bool accept, bool all);
    void replaceRange(const std::u16string &, Selection, const Run *object = nullptr);
    void remapBookmarks(const std::vector<Position> &, Position start, Position end,
                        Position insertedEnd);
    State state() const;
    void restyle();
    void restore(State, bool preserveAllocator = true);
};
} // namespace wf
