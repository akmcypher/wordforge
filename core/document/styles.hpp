#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wf {
struct CharacterStyle;
struct ParagraphFormat;
enum class Alignment;
enum class ListKind;

// A Word theme colour (accent1, text1...) that a text colour was taken from, optionally
// darkened (shade) or lightened (tint) 0-255. `color` is the colour it stood for when read;
// the reference counts only while the text colour still equals it, so changing the colour
// drops it without further bookkeeping.
struct ThemeColor {
    std::uint8_t index = 0; // 0: none; 1-16 in themeColorName's order
    std::int16_t shade = -1, tint = -1;
    std::uint32_t color = 0;
    bool operator==(const ThemeColor &) const = default;
};
// "dark1", "light1", "dark2", "light2", "accent1".."accent6", "hyperlink",
// "followedHyperlink", "background1", "text1", "background2", "text2"; nullptr if out of range.
const char *themeColorName(std::uint8_t index);
std::uint8_t themeColorIndex(const std::string &name); // 0 if unknown
// The theme reference `theme` keeps for text coloured `color`, or nullopt if it lapsed.
std::optional<ThemeColor> activeTheme(const ThemeColor &theme, std::uint32_t color);
// Compact text form for the native format: "accent1", "accent1 s:BF", "text2 t:99".
std::string themeColorText(const ThemeColor &);
ThemeColor parseThemeColorText(const std::string &, std::uint32_t color); // throws if invalid

// Properties a style sets; unset ones come from the style it is based on, then defaults.
enum class TextBaseline { Normal, Superscript, Subscript };
struct CharacterProperties {
    std::optional<TextBaseline> baseline;
    std::optional<std::string> family;
    std::optional<double> size;
    std::optional<bool> bold, italic, underline, strike;
    std::optional<std::uint32_t> color, highlight;
    std::optional<ThemeColor> theme; // only with `color`, and only while it matches
    bool operator==(const CharacterProperties &) const = default;
};
enum class TabAlignment { Left, Center, Right };
enum class TabLeader { None, Dots };
struct TabStop {
    double position = 0;
    TabAlignment alignment = TabAlignment::Left;
    TabLeader leader = TabLeader::None;
    bool operator==(const TabStop &) const = default;
};
struct ParagraphProperties {
    std::optional<std::vector<TabStop>> tabs;
    std::optional<Alignment> alignment;
    std::optional<ListKind> list;
    std::optional<double> indentLeft, spaceBefore, spaceAfter, lineSpacing;
    std::optional<double> indentFirstLine, indentRight;
    std::optional<bool> keepWithNext, keepLines, widowControl;
    bool operator==(const ParagraphProperties &) const = default;
};

// A named paragraph style. `basedOn` is empty for a root style; `next` is the style of
// the paragraph that Enter creates at the end of this one (empty: the same style).
struct ParagraphStyle {
    std::string id, name, basedOn, next;
    ParagraphProperties paragraph;
    CharacterProperties character;
    bool operator==(const ParagraphStyle &) const = default;
};

// A named character style. Runs that use it inherit its properties on top of their
// paragraph style's character formatting; `basedOn` names another character style.
struct NamedCharacterStyle {
    std::string id, name, basedOn;
    CharacterProperties character;
    bool operator==(const NamedCharacterStyle &) const = default;
};

struct StyleSheet {
    std::vector<ParagraphStyle> paragraphStyles;
    std::vector<NamedCharacterStyle> characterStyles;
    const ParagraphStyle *find(const std::string &id) const;
    ParagraphStyle *find(const std::string &id);
    const NamedCharacterStyle *findCharacter(const std::string &id) const;
    NamedCharacterStyle *findCharacter(const std::string &id);
    bool operator==(const StyleSheet &) const = default;
};

// A character style's properties after following its based-on chain.
CharacterProperties resolveCharacterStyle(const StyleSheet &, const std::string &id);
// What a run inherits: its paragraph style's character formatting with its character
// style (empty: none) applied on top.
CharacterStyle inheritedCharacter(const StyleSheet &, CharacterStyle paragraphCharacter,
                                  const std::string &characterStyle);

// Sets each property `target` leaves unset to `fallback`'s value.
void fillUnset(CharacterProperties &target, const CharacterProperties &fallback);
void fillUnset(ParagraphProperties &target, const ParagraphProperties &fallback);
// A style identifier derived from `name` (ASCII letters and digits) that no paragraph or
// character style in `sheet` uses.
std::string uniqueStyleId(const StyleSheet &sheet, const std::string &name);

// Normal, Title, Heading1 and Heading2; every document has them and they cannot be removed.
StyleSheet builtinStyles();
bool isBuiltinStyle(const std::string &id);

// Bits naming the properties a paragraph or run sets directly rather than through its style.
enum CharacterField : std::uint16_t {
    FamilyField = 1 << 0,
    SizeField = 1 << 1,
    BoldField = 1 << 2,
    ItalicField = 1 << 3,
    UnderlineField = 1 << 4,
    StrikeField = 1 << 5,
    ColorField = 1 << 6,
    HighlightField = 1 << 7,
    BaselineField = 1 << 8,
    AllCharacterFields = 0x1ff,
};
enum ParagraphField : std::uint16_t {
    AlignmentField = 1 << 0,
    ListField = 1 << 1,
    IndentField = 1 << 2, // left, first-line and right indents together
    SpaceBeforeField = 1 << 3,
    SpaceAfterField = 1 << 4,
    LineSpacingField = 1 << 5,
    TabsField = 1 << 6,
    KeepWithNextField = 1 << 7,
    KeepLinesField = 1 << 8,
    WidowControlField = 1 << 9,
    AllParagraphFields = 0x3ff,
};
using CharacterMask = std::uint16_t;
using ParagraphMask = std::uint16_t;

CharacterMask differingFields(const CharacterStyle &, const CharacterStyle &);
ParagraphMask differingFields(const ParagraphFormat &, const ParagraphFormat &);
// Copies the fields NOT named in `keep` from `source` into `target`.
void inheritFields(CharacterStyle &target, const CharacterStyle &source, CharacterMask keep);
void inheritFields(ParagraphFormat &target, const ParagraphFormat &source, ParagraphMask keep);
// The properties a style must set so that, on top of `base`, it produces `value`.
CharacterProperties propertiesFor(const CharacterStyle &base, const CharacterStyle &value);
ParagraphProperties propertiesFor(const ParagraphFormat &base, const ParagraphFormat &value);
} // namespace wf
