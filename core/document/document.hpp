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
    bool operator==(const CharacterStyle &) const = default;
};
void validateCharacterStyle(const CharacterStyle &);
enum class Alignment { Left, Center, Right };
enum class ListKind { None, Bullet, Decimal };
enum class ParagraphPreset { Normal, Title, Heading1, Heading2 };
struct ParagraphFormat {
    Alignment alignment = Alignment::Left;
    ListKind list = ListKind::None;
    double indentLeft = 0, spaceBefore = 0, spaceAfter = 8, lineSpacing = 1.15;
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
class Run {
  public:
    Run(std::u16string text = {}, CharacterStyle style = {});
    CharacterStyle style;
    CharacterMask direct = 0; // fields of `style` set directly, not inherited from the styles
    std::string characterStyle; // named character style; empty for none
    const std::u16string &text() const;
    const InlineImage *image() const;
    static Run fromImage(InlineImage, CharacterStyle = {});
    Run slice(std::size_t start, std::size_t end) const;
    void appendText(const std::u16string &);
    bool operator==(const Run &) const = default;

  private:
    std::variant<TextContent, InlineImage> content_;
};
void validateImageResource(const ImageResource &);
using ImageResources = std::map<std::uint64_t, std::shared_ptr<const ImageResource>>;
struct TableCell {
    std::uint64_t tableId = 0;
    std::size_t row = 0, column = 0;
    bool operator==(const TableCell &) const = default;
};
struct Table {
    std::uint64_t id = 0;
    std::size_t rows = 0, columns = 0;
    bool operator==(const Table &) const = default;
};
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
struct PageStyle {
    double width = 595.28, height = 841.89, margin = 56.7;
    bool operator==(const PageStyle &) const = default;
};
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
struct Document {
    std::vector<Paragraph> paragraphs{{1}};
    std::vector<Bookmark> bookmarks;
    ImageResources images;
    std::vector<Table> tables;
    StyleSheet styles = builtinStyles();
    PageStyle page;
    std::uint64_t revision = 0, nextId = 2;
    std::u16string text() const;
    std::uint64_t allocateId();
    Anchor anchorAt(Position, Affinity = Affinity::After) const;
    std::optional<Position> resolve(const Anchor &) const;
    std::vector<Position> bookmarkPositions() const;
    const CharacterStyle &styleAt(Position) const;
    // The run that typing at `position` continues (the one before it at a boundary).
    const Run &runAt(Position) const;
    void pruneUnusedImages();
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
    void resizeImage(double width, double height);
    std::optional<TableCell> selectedCell() const;
    void insertTable(std::size_t rows, std::size_t columns);
    void tableRow(bool insert, bool before = false);
    void tableColumn(bool insert, bool before = false);
    void removeTable();
    bool nextCell(bool backward = false);
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
    void checkpoint();
    void validate(Selection) const;
    void replaceRange(const std::u16string &, Selection, const Run *object = nullptr);
    void remapBookmarks(const std::vector<Position> &, Position start, Position end,
                        Position insertedEnd);
    State state() const;
    void restyle();
    void restore(State, bool preserveAllocator = true);
};
} // namespace wf
