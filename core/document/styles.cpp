#include "core/document/styles.hpp"
#include "core/document/document.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wf {
namespace {
constexpr std::size_t maxStyles = 1024, maxStyleIdBytes = 128, maxStyleNameBytes = 256;

void apply(CharacterStyle &target, const CharacterProperties &properties) {
    if (properties.family)
        target.family = *properties.family;
    if (properties.size)
        target.size = *properties.size;
    if (properties.bold)
        target.bold = *properties.bold;
    if (properties.italic)
        target.italic = *properties.italic;
    if (properties.underline)
        target.underline = *properties.underline;
    if (properties.strike)
        target.strike = *properties.strike;
    if (properties.color)
        target.color = *properties.color;
    if (properties.highlight)
        target.highlight = *properties.highlight;
}

void apply(ParagraphFormat &target, const ParagraphProperties &properties) {
    if (properties.alignment)
        target.alignment = *properties.alignment;
    if (properties.list)
        target.list = *properties.list;
    if (properties.indentLeft)
        target.indentLeft = *properties.indentLeft;
    if (properties.spaceBefore)
        target.spaceBefore = *properties.spaceBefore;
    if (properties.spaceAfter)
        target.spaceAfter = *properties.spaceAfter;
    if (properties.lineSpacing)
        target.lineSpacing = *properties.lineSpacing;
}

void checkText(const std::string &value, std::size_t limit, bool allowEmpty, const char *what) {
    if ((!allowEmpty && value.empty()) || value.size() > limit)
        throw std::invalid_argument(std::string("Invalid style ") + what);
    for (auto ch : utf16(value))
        if (ch < 32)
            throw std::invalid_argument(std::string("Invalid style ") + what);
}
} // namespace

const ParagraphStyle *StyleSheet::find(const std::string &id) const {
    const auto found = std::find_if(paragraphStyles.begin(), paragraphStyles.end(),
                                    [&](const auto &style) { return style.id == id; });
    return found == paragraphStyles.end() ? nullptr : &*found;
}
ParagraphStyle *StyleSheet::find(const std::string &id) {
    return const_cast<ParagraphStyle *>(std::as_const(*this).find(id));
}
const NamedCharacterStyle *StyleSheet::findCharacter(const std::string &id) const {
    const auto found = std::find_if(characterStyles.begin(), characterStyles.end(),
                                    [&](const auto &style) { return style.id == id; });
    return found == characterStyles.end() ? nullptr : &*found;
}
NamedCharacterStyle *StyleSheet::findCharacter(const std::string &id) {
    return const_cast<NamedCharacterStyle *>(std::as_const(*this).findCharacter(id));
}

CharacterProperties resolveCharacterStyle(const StyleSheet &sheet, const std::string &id) {
    CharacterProperties result;
    std::unordered_set<std::string> seen;
    for (auto current = id; !current.empty();) {
        const auto *style = sheet.findCharacter(current);
        if (!style)
            throw std::invalid_argument("Unknown character style: " + current);
        if (!seen.insert(current).second)
            throw std::invalid_argument("Character styles are based on each other in a cycle");
        fillUnset(result, style->character); // the derived style's own values win
        current = style->basedOn;
    }
    return result;
}

CharacterStyle inheritedCharacter(const StyleSheet &sheet, CharacterStyle paragraphCharacter,
                                  const std::string &characterStyle) {
    if (!characterStyle.empty())
        apply(paragraphCharacter, resolveCharacterStyle(sheet, characterStyle));
    return paragraphCharacter;
}

StyleSheet builtinStyles() {
    // Same values as the 0.6 presets, so documents look identical after migration.
    ParagraphStyle normal{"Normal", "Normal", "", "", {}, {}};
    ParagraphStyle title{"Title", "Title", "Normal", "Normal", {}, {}};
    title.paragraph.spaceBefore = 0;
    title.paragraph.spaceAfter = 6;
    title.paragraph.lineSpacing = 1;
    title.character.size = 28;
    ParagraphStyle heading1{"Heading1", "Heading 1", "Normal", "Normal", {}, {}};
    heading1.paragraph.spaceBefore = 12;
    heading1.paragraph.spaceAfter = 6;
    heading1.paragraph.lineSpacing = 1;
    heading1.character.size = 20;
    heading1.character.bold = true;
    heading1.character.color = 0xff185abd;
    ParagraphStyle heading2{"Heading2", "Heading 2", "Heading1", "Normal", {}, {}};
    heading2.character.size = 16;
    return {{normal, title, heading1, heading2}};
}

void fillUnset(CharacterProperties &target, const CharacterProperties &fallback) {
    auto fill = [](auto &value, const auto &other) {
        if (!value)
            value = other;
    };
    fill(target.family, fallback.family);
    fill(target.size, fallback.size);
    fill(target.bold, fallback.bold);
    fill(target.italic, fallback.italic);
    fill(target.underline, fallback.underline);
    fill(target.strike, fallback.strike);
    fill(target.color, fallback.color);
    fill(target.highlight, fallback.highlight);
}

void fillUnset(ParagraphProperties &target, const ParagraphProperties &fallback) {
    auto fill = [](auto &value, const auto &other) {
        if (!value)
            value = other;
    };
    fill(target.alignment, fallback.alignment);
    fill(target.list, fallback.list);
    fill(target.indentLeft, fallback.indentLeft);
    fill(target.spaceBefore, fallback.spaceBefore);
    fill(target.spaceAfter, fallback.spaceAfter);
    fill(target.lineSpacing, fallback.lineSpacing);
}

std::string uniqueStyleId(const StyleSheet &sheet, const std::string &name) {
    std::string base;
    for (unsigned char ch : name)
        if (ch < 128 && std::isalnum(ch) && base.size() < 64)
            base += static_cast<char>(ch);
    if (base.empty() || std::isdigit(static_cast<unsigned char>(base.front())))
        base = "Style" + base;
    auto id = base;
    for (std::size_t n = 2; sheet.find(id) || sheet.findCharacter(id) || isBuiltinStyle(id); ++n)
        id = base + std::to_string(n);
    return id;
}

bool isBuiltinStyle(const std::string &id) {
    return id == "Normal" || id == "Title" || id == "Heading1" || id == "Heading2";
}

std::string presetStyleId(ParagraphPreset preset) {
    switch (preset) {
    case ParagraphPreset::Normal:
        return "Normal";
    case ParagraphPreset::Title:
        return "Title";
    case ParagraphPreset::Heading1:
        return "Heading1";
    case ParagraphPreset::Heading2:
        return "Heading2";
    }
    throw std::invalid_argument("Unknown paragraph preset");
}

std::optional<ParagraphPreset> presetFor(const std::string &id) {
    for (auto preset : {ParagraphPreset::Normal, ParagraphPreset::Title, ParagraphPreset::Heading1,
                        ParagraphPreset::Heading2})
        if (presetStyleId(preset) == id)
            return preset;
    return std::nullopt;
}

ResolvedStyle resolveStyle(const StyleSheet &sheet, const std::string &id) {
    std::vector<const ParagraphStyle *> chain;
    std::unordered_set<std::string> seen;
    for (auto current = id; !current.empty();) {
        const auto *style = sheet.find(current);
        if (!style)
            throw std::invalid_argument("Unknown paragraph style: " + current);
        if (!seen.insert(current).second)
            throw std::invalid_argument("Paragraph styles are based on each other in a cycle");
        chain.push_back(style);
        current = style->basedOn;
    }
    ResolvedStyle result;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        apply(result.paragraph, (*it)->paragraph);
        apply(result.character, (*it)->character);
    }
    return result;
}

void validateStyles(const StyleSheet &sheet) {
    if (sheet.paragraphStyles.size() > maxStyles)
        throw std::invalid_argument("Too many paragraph styles");
    std::unordered_set<std::string> ids, names;
    for (const auto &style : sheet.paragraphStyles) {
        checkText(style.id, maxStyleIdBytes, false, "identifier");
        checkText(style.name, maxStyleNameBytes, false, "name");
        checkText(style.basedOn, maxStyleIdBytes, true, "base");
        checkText(style.next, maxStyleIdBytes, true, "next style");
        if (!ids.insert(style.id).second || !names.insert(style.name).second)
            throw std::invalid_argument("Duplicate paragraph style");
    }
    if (sheet.paragraphStyles.size() + sheet.characterStyles.size() > maxStyles)
        throw std::invalid_argument("Too many styles");
    for (const auto &style : sheet.characterStyles) {
        checkText(style.id, maxStyleIdBytes, false, "identifier");
        checkText(style.name, maxStyleNameBytes, false, "name");
        checkText(style.basedOn, maxStyleIdBytes, true, "base");
        if (!ids.insert(style.id).second || !names.insert(style.name).second)
            throw std::invalid_argument("Duplicate style identifier or name");
    }
    for (const auto &style : sheet.characterStyles)
        validateCharacterStyle(inheritedCharacter(sheet, {}, style.id)); // cycles, dangling bases
    for (const auto *id : {"Normal", "Title", "Heading1", "Heading2"})
        if (!ids.contains(id))
            throw std::invalid_argument(std::string("Missing built-in style ") + id);
    if (!sheet.find("Normal")->basedOn.empty())
        throw std::invalid_argument("Normal cannot be based on another style");
    for (const auto &style : sheet.paragraphStyles) {
        if (!style.next.empty() && !sheet.find(style.next))
            throw std::invalid_argument("Unknown next style: " + style.next);
        const auto resolved = resolveStyle(sheet, style.id); // detects cycles and dangling bases
        validateParagraphFormat(resolved.paragraph);
        validateCharacterStyle(resolved.character);
    }
}

CharacterMask differingFields(const CharacterStyle &a, const CharacterStyle &b) {
    CharacterMask mask = 0;
    if (a.family != b.family)
        mask |= FamilyField;
    if (a.size != b.size)
        mask |= SizeField;
    if (a.bold != b.bold)
        mask |= BoldField;
    if (a.italic != b.italic)
        mask |= ItalicField;
    if (a.underline != b.underline)
        mask |= UnderlineField;
    if (a.strike != b.strike)
        mask |= StrikeField;
    if (a.color != b.color)
        mask |= ColorField;
    if (a.highlight != b.highlight)
        mask |= HighlightField;
    return mask;
}

ParagraphMask differingFields(const ParagraphFormat &a, const ParagraphFormat &b) {
    ParagraphMask mask = 0;
    if (a.alignment != b.alignment)
        mask |= AlignmentField;
    if (a.list != b.list)
        mask |= ListField;
    if (a.indentLeft != b.indentLeft)
        mask |= IndentField;
    if (a.spaceBefore != b.spaceBefore)
        mask |= SpaceBeforeField;
    if (a.spaceAfter != b.spaceAfter)
        mask |= SpaceAfterField;
    if (a.lineSpacing != b.lineSpacing)
        mask |= LineSpacingField;
    return mask;
}

void inheritFields(CharacterStyle &target, const CharacterStyle &source, CharacterMask keep) {
    if (!(keep & FamilyField))
        target.family = source.family;
    if (!(keep & SizeField))
        target.size = source.size;
    if (!(keep & BoldField))
        target.bold = source.bold;
    if (!(keep & ItalicField))
        target.italic = source.italic;
    if (!(keep & UnderlineField))
        target.underline = source.underline;
    if (!(keep & StrikeField))
        target.strike = source.strike;
    if (!(keep & ColorField))
        target.color = source.color;
    if (!(keep & HighlightField))
        target.highlight = source.highlight;
}

void inheritFields(ParagraphFormat &target, const ParagraphFormat &source, ParagraphMask keep) {
    if (!(keep & AlignmentField))
        target.alignment = source.alignment;
    if (!(keep & ListField))
        target.list = source.list;
    if (!(keep & IndentField))
        target.indentLeft = source.indentLeft;
    if (!(keep & SpaceBeforeField))
        target.spaceBefore = source.spaceBefore;
    if (!(keep & SpaceAfterField))
        target.spaceAfter = source.spaceAfter;
    if (!(keep & LineSpacingField))
        target.lineSpacing = source.lineSpacing;
}

CharacterProperties propertiesFor(const CharacterStyle &base, const CharacterStyle &value) {
    CharacterProperties result;
    if (base.family != value.family)
        result.family = value.family;
    if (base.size != value.size)
        result.size = value.size;
    if (base.bold != value.bold)
        result.bold = value.bold;
    if (base.italic != value.italic)
        result.italic = value.italic;
    if (base.underline != value.underline)
        result.underline = value.underline;
    if (base.strike != value.strike)
        result.strike = value.strike;
    if (base.color != value.color)
        result.color = value.color;
    if (base.highlight != value.highlight)
        result.highlight = value.highlight;
    return result;
}

ParagraphProperties propertiesFor(const ParagraphFormat &base, const ParagraphFormat &value) {
    ParagraphProperties result;
    if (base.alignment != value.alignment)
        result.alignment = value.alignment;
    if (base.list != value.list)
        result.list = value.list;
    if (base.indentLeft != value.indentLeft)
        result.indentLeft = value.indentLeft;
    if (base.spaceBefore != value.spaceBefore)
        result.spaceBefore = value.spaceBefore;
    if (base.spaceAfter != value.spaceAfter)
        result.spaceAfter = value.spaceAfter;
    if (base.lineSpacing != value.lineSpacing)
        result.lineSpacing = value.lineSpacing;
    return result;
}

void adoptStyles(Document &document) {
    validateStyles(document.styles);
    std::unordered_map<std::string, ResolvedStyle> resolved;
    for (auto &paragraph : document.paragraphs) {
        if (!document.styles.find(paragraph.style))
            paragraph.style = "Normal";
        auto found = resolved.find(paragraph.style);
        if (found == resolved.end())
            found = resolved.emplace(paragraph.style, resolveStyle(document.styles, paragraph.style)).first;
        paragraph.direct |= differingFields(static_cast<const ParagraphFormat &>(paragraph),
                                            found->second.paragraph);
        for (auto &run : paragraph.runs) {
            if (!run.characterStyle.empty() && !document.styles.findCharacter(run.characterStyle))
                run.characterStyle.clear();
            run.direct |= differingFields(
                run.style,
                inheritedCharacter(document.styles, found->second.character, run.characterStyle));
        }
    }
}

Paragraph styledParagraph(const Document &document, std::uint64_t id, const std::string &style) {
    const auto resolved = resolveStyle(document.styles, style);
    Paragraph paragraph(id);
    static_cast<ParagraphFormat &>(paragraph) = resolved.paragraph;
    paragraph.style = style;
    paragraph.runs = {{u"", resolved.character}};
    return paragraph;
}
} // namespace wf
