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

// Properties a style sets; unset ones come from the style it is based on, then defaults.
struct CharacterProperties {
    std::optional<std::string> family;
    std::optional<double> size;
    std::optional<bool> bold, italic, underline, strike;
    std::optional<std::uint32_t> color, highlight;
    bool operator==(const CharacterProperties &) const = default;
};
struct ParagraphProperties {
    std::optional<Alignment> alignment;
    std::optional<ListKind> list;
    std::optional<double> indentLeft, spaceBefore, spaceAfter, lineSpacing;
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

struct StyleSheet {
    std::vector<ParagraphStyle> paragraphStyles;
    const ParagraphStyle *find(const std::string &id) const;
    ParagraphStyle *find(const std::string &id);
    bool operator==(const StyleSheet &) const = default;
};

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
    AllCharacterFields = 0xff,
};
enum ParagraphField : std::uint16_t {
    AlignmentField = 1 << 0,
    ListField = 1 << 1,
    IndentField = 1 << 2,
    SpaceBeforeField = 1 << 3,
    SpaceAfterField = 1 << 4,
    LineSpacingField = 1 << 5,
    AllParagraphFields = 0x3f,
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
