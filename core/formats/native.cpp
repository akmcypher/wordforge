#include "core/formats/native.hpp"
#include "core/formats/zip_package.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace wf {
namespace {
constexpr std::size_t maxBytes = 16 * 1024 * 1024;
std::uint64_t identifier(pugi::xml_node node, const char *name, bool allowZero = false) {
    const std::string_view text = node.attribute(name).value();
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        (!result && !allowZero))
        throw std::runtime_error("Invalid native identifier or allocator");
    return result;
}
std::size_t index(pugi::xml_node node, const char *name, bool allowZero = false) {
    const auto value = identifier(node, name, allowZero);
    if (value > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Native table index exceeds index size");
    return static_cast<std::size_t>(value);
}
double number(pugi::xml_node node, const char *key, double fallback, double low, double high) {
    auto attr = node.attribute(key);
    if (!attr)
        return fallback;
    char *end = nullptr;
    const double value = std::strtod(attr.value(), &end);
    if (end == attr.value() || *end || !std::isfinite(value) || value < low || value > high)
        throw std::runtime_error(std::string("Invalid document value: ") + key);
    return value;
}
void attributes(pugi::xml_node node, std::initializer_list<std::string_view> known) {
    std::set<std::string_view> seen;
    for (auto attr : node.attributes()) {
        bool found = false;
        for (auto key : known)
            if (attr.name() == key)
                found = true;
        if (!found || !seen.insert(attr.name()).second)
            throw std::runtime_error("Unknown native-format attribute; refusing lossy import");
    }
}
void pageGeometry(const PageStyle &page) {
    try {
        validatePage(page);
    } catch (const std::invalid_argument &error) {
        throw std::runtime_error(error.what());
    }
}
bool boolean(pugi::xml_node node, const char *key) {
    auto attr = node.attribute(key);
    if (!attr)
        return false;
    if (std::string_view(attr.value()) == "true")
        return true;
    if (std::string_view(attr.value()) == "false")
        return false;
    throw std::runtime_error("Invalid boolean");
}
bool indentation(pugi::xml_node node) {
    return node.type() == pugi::node_pcdata &&
           std::string_view(node.value()).find_first_not_of(" \t\r\n") == std::string_view::npos;
}
void writeCharacterProperties(pugi::xml_node node, const CharacterProperties &c);
void writeStyle(pugi::xml_node root, const ParagraphStyle &style) {
    auto node = root.append_child("style");
    node.append_attribute("id") = style.id.c_str();
    node.append_attribute("name") = style.name.c_str();
    if (!style.basedOn.empty())
        node.append_attribute("basedOn") = style.basedOn.c_str();
    if (!style.next.empty())
        node.append_attribute("next") = style.next.c_str();
    const auto &p = style.paragraph;
    if (p.alignment)
        node.append_attribute("align") = static_cast<int>(*p.alignment);
    if (p.list)
        node.append_attribute("list") = static_cast<int>(*p.list);
    if (p.indentLeft)
        node.append_attribute("indent") = *p.indentLeft;
    if (p.indentFirstLine)
        node.append_attribute("firstLine") = *p.indentFirstLine;
    if (p.indentRight)
        node.append_attribute("rightIndent") = *p.indentRight;
    if (p.spaceBefore)
        node.append_attribute("before") = *p.spaceBefore;
    if (p.spaceAfter)
        node.append_attribute("after") = *p.spaceAfter;
    if (p.lineSpacing)
        node.append_attribute("line") = *p.lineSpacing;
    writeCharacterProperties(node, style.character);
}
void writeCharacterProperties(pugi::xml_node node, const CharacterProperties &c) {
    if (c.family)
        node.append_attribute("family") = c.family->c_str();
    if (c.size)
        node.append_attribute("size") = *c.size;
    if (c.bold)
        node.append_attribute("bold") = *c.bold;
    if (c.italic)
        node.append_attribute("italic") = *c.italic;
    if (c.underline)
        node.append_attribute("underline") = *c.underline;
    if (c.strike)
        node.append_attribute("strike") = *c.strike;
    if (c.color)
        node.append_attribute("color") = static_cast<unsigned long long>(*c.color);
    if (c.color && c.theme && activeTheme(*c.theme, *c.color))
        node.append_attribute("theme") = themeColorText(*c.theme).c_str();
    if (c.highlight)
        node.append_attribute("highlight") = static_cast<unsigned long long>(*c.highlight);
}
template <typename T> std::optional<T> whole(pugi::xml_node node, const char *key, double high) {
    if (!node.attribute(key))
        return std::nullopt;
    const auto value = number(node, key, 0, 0, high);
    if (std::floor(value) != value)
        throw std::runtime_error(std::string("Invalid style value: ") + key);
    return static_cast<T>(static_cast<long long>(value));
}
std::optional<double> real(pugi::xml_node node, const char *key, double low, double high) {
    if (!node.attribute(key))
        return std::nullopt;
    return number(node, key, 0, low, high);
}
std::optional<bool> flag(pugi::xml_node node, const char *key) {
    if (!node.attribute(key))
        return std::nullopt;
    return boolean(node, key);
}
CharacterProperties readCharacterProperties(pugi::xml_node node) {
    CharacterProperties character;
    if (node.attribute("family"))
        character.family = node.attribute("family").value();
    character.size = real(node, "size", 1, 200);
    character.bold = flag(node, "bold");
    character.italic = flag(node, "italic");
    character.underline = flag(node, "underline");
    character.strike = flag(node, "strike");
    character.color = whole<std::uint32_t>(node, "color", 4294967295.0);
    character.highlight = whole<std::uint32_t>(node, "highlight", 4294967295.0);
    if (node.attribute("theme")) {
        if (!character.color)
            throw std::runtime_error("A style theme colour needs a colour");
        character.theme = parseThemeColorText(node.attribute("theme").value(), *character.color);
    }
    return character;
}
// `indented`: WFG11, which adds first-line and right indents and justified alignment.
// `themed`: WFG12, which adds Word theme colours.
ParagraphStyle readStyle(pugi::xml_node node, bool indented, bool themed) {
    if (indented)
        attributes(node, {"id", "name", "basedOn", "next", "align", "list", "indent", "firstLine",
                          "rightIndent", "before", "after", "line", "family", "size", "bold",
                          "italic", "underline", "strike", "color", "highlight",
                          themed ? "theme" : "id"});
    else
        attributes(node, {"id", "name", "basedOn", "next", "align", "list", "indent", "before",
                          "after", "line", "family", "size", "bold", "italic", "underline",
                          "strike", "color", "highlight"});
    for (auto child : node.children())
        if (!indentation(child))
            throw std::runtime_error("Unexpected style content");
    ParagraphStyle style;
    style.id = node.attribute("id").value();
    style.name = node.attribute("name").value();
    style.basedOn = node.attribute("basedOn").value();
    style.next = node.attribute("next").value();
    style.paragraph.alignment = whole<Alignment>(node, "align", indented ? 3 : 2);
    style.paragraph.list = whole<ListKind>(node, "list", 2);
    style.paragraph.indentLeft = real(node, "indent", 0, 360);
    style.paragraph.indentFirstLine = real(node, "firstLine", -360, 360);
    style.paragraph.indentRight = real(node, "rightIndent", 0, 360);
    style.paragraph.spaceBefore = real(node, "before", 0, 200);
    style.paragraph.spaceAfter = real(node, "after", 0, 200);
    style.paragraph.lineSpacing = real(node, "line", 1, 3);
    style.character = readCharacterProperties(node);
    return style;
}
NamedCharacterStyle readCharacterStyle(pugi::xml_node node, bool themed) {
    attributes(node, {"id", "name", "basedOn", "family", "size", "bold", "italic", "underline",
                      "strike", "color", "highlight", themed ? "theme" : "id"});
    for (auto child : node.children())
        if (!indentation(child))
            throw std::runtime_error("Unexpected style content");
    NamedCharacterStyle style;
    style.id = node.attribute("id").value();
    style.name = node.attribute("name").value();
    style.basedOn = node.attribute("basedOn").value();
    style.character = readCharacterProperties(node);
    return style;
}
std::uint16_t mask(pugi::xml_node node, std::uint16_t all) {
    const auto value = identifier(node, "direct", true);
    if (value & ~static_cast<std::uint64_t>(all))
        throw std::runtime_error("Invalid direct-formatting mask");
    return static_cast<std::uint16_t>(value);
}
} // namespace
std::string serializeNative(const Document &document) {
    validateDocument(document);
    pageGeometry(document.page);
    pugi::xml_document xml;
    auto root = xml.append_child("wordforge");
    std::set<std::uint64_t> imageIds;
    for (const auto &paragraph : document.paragraphs)
        for (const auto &run : paragraph.runs)
            if (const auto *image = run.image())
                imageIds.insert(image->resourceId);
    const bool tables = !document.tables.empty();
    // WFG9 only for footnotes or kept DOCX package parts (which need the ZIP transport).
    const bool preserving = document.preserved != nullptr;
    const bool noted = !document.footnotes.empty() || !document.comments.empty();
    // WFG10 only for tracked changes, so other files stay readable by 0.6.5.
    const bool reviewed = document.trackChanges || !document.revisions.empty();
    // WFG11 only for table borders, cell shading, first-line or right indents or justified
    // text, so other files stay readable by 0.6.6.
    auto indented = [](const auto &p) {
        return p.indentFirstLine != decltype(p.indentFirstLine){} ||
               p.indentRight != decltype(p.indentRight){} ||
               p.alignment == decltype(p.alignment){Alignment::Justify};
    };
    const bool decorated =
        std::any_of(document.tables.begin(), document.tables.end(),
                    [](const Table &t) { return t.border != TableBorder{} || !t.shading.empty(); }) ||
        std::any_of(document.paragraphs.begin(), document.paragraphs.end(),
                    [&](const Paragraph &p) { return indented(p); }) ||
        std::any_of(document.styles.paragraphStyles.begin(), document.styles.paragraphStyles.end(),
                    [&](const ParagraphStyle &s) { return indented(s.paragraph); });
    const bool packaged = tables || !imageIds.empty() || preserving;
    // WFG6 only when styles were customized; built-in styles keep files readable by 0.6.0.
    const bool styled = document.styles != builtinStyles();
    // WFG7 only when character styles exist, so other styled files stay readable by 0.6.2.
    const bool characterStyled = !document.styles.characterStyles.empty();
    // WFG8 only for headers/footers or table widths/merges, so other files stay readable
    // by 0.6.3.
    const bool banded =
        !document.page.header.empty() || !document.page.footer.empty() ||
        document.page.differentFirstPage ||
        std::any_of(document.tables.begin(), document.tables.end(),
                    [](const Table &t) { return !t.widths.empty() || !t.merges.empty(); });
    // WFG6 and later store paragraph style names and direct-formatting masks.
    // WFG12 only for Word theme colours, links, endnotes or fields, so other files stay
    // readable by 0.6.7.
    auto themedCharacter = [](const CharacterProperties &c) {
        return c.color && c.theme && activeTheme(*c.theme, *c.color);
    };
    const bool themed =
        !document.fields.empty() ||
        std::any_of(document.paragraphs.begin(), document.paragraphs.end(),
                    [](const Paragraph &p) {
                        return std::any_of(p.runs.begin(), p.runs.end(), [](const Run &r) {
                            return activeTheme(r.style.theme, r.style.color).has_value() ||
                                   !r.link.empty() || (r.note() && r.note()->endnote);
                        });
                    }) ||
        std::any_of(document.styles.paragraphStyles.begin(), document.styles.paragraphStyles.end(),
                    [&](const ParagraphStyle &s) { return themedCharacter(s.character); }) ||
        std::any_of(document.styles.characterStyles.begin(), document.styles.characterStyles.end(),
                    [&](const NamedCharacterStyle &s) { return themedCharacter(s.character); });
    // WFG13 only for sections or columns, so other files stay readable by 0.6.8.
    const bool sectioned =
        document.page.columns != 1 || document.page.columnGap != 36 ||
        std::any_of(document.paragraphs.begin(), document.paragraphs.end(),
                    [](const Paragraph &p) { return p.sectionBreak.has_value(); });
    const bool semantic =
        styled || banded || preserving || noted || reviewed || decorated || themed || sectioned;
    root.append_attribute("version") = sectioned                    ? "13"
                                       : themed                     ? "12"
                                       : decorated                  ? "11"
                                       : reviewed                   ? "10"
                                       : preserving || noted        ? "9"
                                       : banded                     ? "8"
                                       : characterStyled            ? "7"
                                       : styled                     ? "6"
                                       : tables                     ? "5"
                                       : packaged                   ? "4"
                                       : document.bookmarks.empty() ? "2"
                                                                    : "3";
    opc::Parts parts;
    for (const auto id : imageIds) {
        const auto &resource = document.images.at(id);
        auto node = root.append_child("imageResource");
        const auto path = "images/" + std::to_string(id) + ".png";
        node.append_attribute("id") = static_cast<unsigned long long>(id);
        node.append_attribute("path") = path.c_str();
        node.append_attribute("pixelWidth") = resource->pixelWidth;
        node.append_attribute("pixelHeight") = resource->pixelHeight;
        parts.emplace(path, resource->png);
    }
    root.append_attribute("nextId") = static_cast<unsigned long long>(document.nextId);
    root.append_attribute("width") = document.page.width;
    root.append_attribute("height") = document.page.height;
    root.append_attribute("margin") = document.page.margin;
    if (document.page.columns != 1 || document.page.columnGap != 36) {
        root.append_attribute("columns") = static_cast<unsigned long long>(document.page.columns);
        root.append_attribute("columnGap") = document.page.columnGap;
    }
    if (document.page.differentFirstPage)
        root.append_attribute("differentFirstPage") = "true";
    if (document.trackChanges)
        root.append_attribute("trackChanges") = "true";
    for (const auto &revision : document.revisions) {
        auto node = root.append_child("revision");
        node.append_attribute("id") = static_cast<unsigned long long>(revision.id);
        node.append_attribute("author") = revision.author.c_str();
        if (!revision.date.empty())
            node.append_attribute("date") = revision.date.c_str();
    }
    for (auto [name, band] : {std::pair{"header", &document.page.header},
                              std::pair{"footer", &document.page.footer}}) {
        if (band->empty())
            continue;
        auto node = root.append_child(name);
        for (auto [key, text] : {std::pair{"left", &band->left}, std::pair{"center", &band->center},
                                 std::pair{"right", &band->right}})
            if (!text->empty())
                node.append_attribute(key) = utf8(*text).c_str();
    }
    if (semantic) // WFG6 and later always list the paragraph styles
        for (const auto &style : document.styles.paragraphStyles)
            writeStyle(root, style);
    for (const auto &style : document.styles.characterStyles) {
        auto node = root.append_child("charStyle");
        node.append_attribute("id") = style.id.c_str();
        node.append_attribute("name") = style.name.c_str();
        if (!style.basedOn.empty())
            node.append_attribute("basedOn") = style.basedOn.c_str();
        writeCharacterProperties(node, style.character);
    }
    for (const auto &comment : document.comments) {
        auto node = root.append_child("comment");
        node.append_attribute("id") = static_cast<unsigned long long>(comment.id);
        node.append_attribute("author") = comment.author.c_str();
        if (!comment.date.empty())
            node.append_attribute("date") = comment.date.c_str();
        node.append_attribute("text") = utf8(comment.text).c_str();
        node.append_attribute("startParagraph") =
            static_cast<unsigned long long>(comment.start.paragraphId);
        node.append_attribute("startOffset") = static_cast<unsigned long long>(comment.start.offset);
        node.append_attribute("endParagraph") =
            static_cast<unsigned long long>(comment.end.paragraphId);
        node.append_attribute("endOffset") = static_cast<unsigned long long>(comment.end.offset);
    }
    for (const auto &field : document.fields) {
        auto node = root.append_child("field");
        node.append_attribute("id") = static_cast<unsigned long long>(field.id);
        node.append_attribute("instruction") = field.instruction.c_str();
        node.append_attribute("startParagraph") =
            static_cast<unsigned long long>(field.start.paragraphId);
        node.append_attribute("startOffset") = static_cast<unsigned long long>(field.start.offset);
        node.append_attribute("endParagraph") =
            static_cast<unsigned long long>(field.end.paragraphId);
        node.append_attribute("endOffset") = static_cast<unsigned long long>(field.end.offset);
    }
    for (const auto &note : document.footnotes) {
        auto node = root.append_child("footnote");
        node.append_attribute("id") = static_cast<unsigned long long>(note.id);
        node.append_attribute("text") = utf8(note.text).c_str();
    }
    if (preserving) {
        std::size_t number = 0;
        for (const auto &part : document.preserved->parts) {
            const auto stored = "preserved/" + std::to_string(number++);
            auto node = root.append_child("preservedPart");
            node.append_attribute("path") = part.path.c_str();
            if (!part.contentType.empty())
                node.append_attribute("contentType") = part.contentType.c_str();
            node.append_attribute("part") = stored.c_str();
            parts.emplace(stored, part.data);
        }
        for (const auto &relationship : document.preserved->relationships) {
            auto node = root.append_child("preservedRelationship");
            if (relationship.package)
                node.append_attribute("package") = "true";
            node.append_attribute("type") = relationship.type.c_str();
            node.append_attribute("target") = relationship.target.c_str();
        }
    }
    for (const auto &table : document.tables) {
        auto node = root.append_child("table");
        node.append_attribute("id") = static_cast<unsigned long long>(table.id);
        node.append_attribute("rows") = static_cast<unsigned long long>(table.rows);
        node.append_attribute("columns") = static_cast<unsigned long long>(table.columns);
        if (table.border != TableBorder{}) {
            node.append_attribute("borderWidth") = table.border.width;
            node.append_attribute("borderColor") =
                static_cast<unsigned long long>(table.border.color);
        }
        for (const auto width : table.widths)
            node.append_child("column").append_attribute("width") = width;
        for (const auto &shade : table.shading) {
            auto item = node.append_child("shade");
            item.append_attribute("row") = static_cast<unsigned long long>(shade.row);
            item.append_attribute("column") = static_cast<unsigned long long>(shade.column);
            item.append_attribute("color") = static_cast<unsigned long long>(shade.color);
        }
        for (const auto &merge : table.merges) {
            auto item = node.append_child("merge");
            item.append_attribute("row") = static_cast<unsigned long long>(merge.row);
            item.append_attribute("column") = static_cast<unsigned long long>(merge.column);
            item.append_attribute("rows") = static_cast<unsigned long long>(merge.rows);
            item.append_attribute("columns") = static_cast<unsigned long long>(merge.columns);
        }
    }
    for (const auto &p : document.paragraphs) {
        auto paragraph = root.append_child("p");
        paragraph.append_attribute("id") = static_cast<unsigned long long>(p.id);
        if (p.cell) {
            paragraph.append_attribute("table") = static_cast<unsigned long long>(p.cell->tableId);
            paragraph.append_attribute("row") = static_cast<unsigned long long>(p.cell->row);
            paragraph.append_attribute("column") = static_cast<unsigned long long>(p.cell->column);
        }
        paragraph.append_attribute("align") = static_cast<int>(p.alignment);
        paragraph.append_attribute("after") = p.spaceAfter;
        if (p.list != ListKind::None)
            paragraph.append_attribute("list") = static_cast<int>(p.list);
        if (semantic) {
            if (p.style != "Normal")
                paragraph.append_attribute("style") = p.style.c_str();
            if (p.direct)
                paragraph.append_attribute("direct") = p.direct;
        } else if (const auto preset = presetFor(p.style); preset && *preset != ParagraphPreset::Normal)
            paragraph.append_attribute("preset") = static_cast<int>(*preset);
        if (p.indentLeft)
            paragraph.append_attribute("indent") = p.indentLeft;
        if (p.indentFirstLine)
            paragraph.append_attribute("firstLine") = p.indentFirstLine;
        if (p.indentRight)
            paragraph.append_attribute("rightIndent") = p.indentRight;
        if (p.spaceBefore)
            paragraph.append_attribute("before") = p.spaceBefore;
        if (p.lineSpacing != 1.15)
            paragraph.append_attribute("line") = p.lineSpacing;
        if (p.pageBreakBefore)
            paragraph.append_attribute("break") = true;
        if (const auto &end = p.sectionBreak) {
            paragraph.append_attribute("section") = end->continuous ? "continuous" : "page";
            paragraph.append_attribute("sectionWidth") = end->page.width;
            paragraph.append_attribute("sectionHeight") = end->page.height;
            paragraph.append_attribute("sectionMargin") = end->page.margin;
            paragraph.append_attribute("sectionColumns") =
                static_cast<unsigned long long>(end->page.columns);
            paragraph.append_attribute("sectionGap") = end->page.columnGap;
        }
        for (const auto &r : p.runs) {
            const auto *image = r.image();
            auto run = paragraph.append_child(image ? "image" : r.note() ? "note" : "r");
            if (r.note())
                run.append_attribute("id") = static_cast<unsigned long long>(r.note()->id);
            if (r.note() && r.note()->endnote)
                run.append_attribute("endnote") = true;
            if (image) {
                run.append_attribute("resource") =
                    static_cast<unsigned long long>(image->resourceId);
                run.append_attribute("width") = image->width;
                run.append_attribute("height") = image->height;
                run.append_attribute("description") = image->description.c_str();
            }
            run.append_attribute("family") = r.style.family.c_str();
            run.append_attribute("size") = r.style.size;
            run.append_attribute("bold") = r.style.bold;
            run.append_attribute("italic") = r.style.italic;
            run.append_attribute("underline") = r.style.underline;
            run.append_attribute("color") = static_cast<unsigned long long>(r.style.color);
            if (activeTheme(r.style.theme, r.style.color))
                run.append_attribute("theme") = themeColorText(r.style.theme).c_str();
            if (r.style.strike)
                run.append_attribute("strike") = true;
            if (r.style.highlight)
                run.append_attribute("highlight") =
                    static_cast<unsigned long long>(r.style.highlight);
            if (semantic && r.direct)
                run.append_attribute("direct") = r.direct;
            if (!r.characterStyle.empty())
                run.append_attribute("cstyle") = r.characterStyle.c_str();
            if (!r.link.empty())
                run.append_attribute("link") = r.link.c_str();
            if (r.inserted)
                run.append_attribute("ins") = static_cast<unsigned long long>(r.inserted);
            if (r.deleted)
                run.append_attribute("del") = static_cast<unsigned long long>(r.deleted);
            if (!image && !r.note())
                run.append_child(pugi::node_pcdata).set_value(utf8(r.text()).c_str());
        }
    }
    for (const auto &bookmark : document.bookmarks) {
        auto node = root.append_child("bookmark");
        node.append_attribute("id") = static_cast<unsigned long long>(bookmark.id);
        node.append_attribute("name") = bookmark.name.c_str();
        node.append_attribute("paragraph") =
            static_cast<unsigned long long>(bookmark.anchor.paragraphId);
        node.append_attribute("offset") = static_cast<unsigned long long>(bookmark.anchor.offset);
        node.append_attribute("affinity") =
            bookmark.anchor.affinity == Affinity::Before ? "before" : "after";
    }
    std::ostringstream output;
    xml.save(output, "  ", pugi::format_default, pugi::encoding_utf8);
    auto result = output.str();
    if (packaged) {
        parts.emplace("document.xml", std::move(result));
        result = opc::write(parts);
    }
    // Validate both directions at the persistence boundary.
    parseNative(result);
    return result;
}
Document parseNative(const std::string &input) {
    const bool packaged = input.starts_with("PK");
    opc::Parts parts;
    if (packaged) {
        parts = opc::read(input);
        if (!parts.contains("document.xml"))
            throw std::runtime_error("Native package has no document.xml");
    }
    const auto &bytes = packaged ? parts.at("document.xml") : input;
    std::set<std::string> consumed{"document.xml"};
    if (bytes.size() > maxBytes)
        throw std::runtime_error("Native document exceeds 16 MiB limit");
    utf16(bytes);
    if (bytes.find("<!DOCTYPE") != std::string::npos || bytes.find("<!ENTITY") != std::string::npos)
        throw std::runtime_error("DTD and entity declarations are not accepted");
    pugi::xml_document xml;
    auto parsed = xml.load_buffer(bytes.data(), bytes.size(),
                                  pugi::parse_default | pugi::parse_ws_pcdata, pugi::encoding_utf8);
    if (!parsed)
        throw std::runtime_error(std::string("Invalid native document: ") + parsed.description());
    auto root = xml.document_element();
    for (auto child : xml.children())
        if (child != root && !indentation(child))
            throw std::runtime_error("Expected exactly one document root");
    const std::string_view version = root.attribute("version").value();
    if (std::string_view(root.name()) != "wordforge" ||
        (version != "1" && version != "2" && version != "3" && version != "4" && version != "5" &&
         version != "6" && version != "7" && version != "8" && version != "9" &&
         version != "10" && version != "11" && version != "12" && version != "13"))
        throw std::runtime_error("Unsupported native document version");
    // WFG7 is WFG6 plus character styles; WFG8 adds headers and footers.
    // WFG9 adds DOCX package parts kept from an imported file.
    const bool v13 = version == "13";
    const bool v12 = version == "12" || v13;
    const bool v11 = version == "11" || v12;
    const bool v10 = version == "10" || v11;
    const bool v9 = version == "9" || v10;
    const bool v8 = version == "8" || v9;
    const bool v7 = version == "7" || v8;
    const bool v6 = version == "6" || v7;
    if (!v6 && packaged != (version == "4" || version == "5"))
        throw std::runtime_error("WFG4/5 require ZIP transport; legacy versions require XML");
    if (version == "1")
        attributes(root, {"version", "width", "height", "margin"});
    else if (v13)
        attributes(root, {"version", "width", "height", "margin", "nextId", "differentFirstPage",
                          "trackChanges", "columns", "columnGap"});
    else if (v10)
        attributes(root, {"version", "width", "height", "margin", "nextId", "differentFirstPage",
                          "trackChanges"});
    else if (v8)
        attributes(root, {"version", "width", "height", "margin", "nextId", "differentFirstPage"});
    else
        attributes(root, {"version", "width", "height", "margin", "nextId"});
    Document document;
    document.paragraphs.clear();
    document.nextId = 1;
    std::shared_ptr<PreservedPackage> preserved;
    if (v6)
        document.styles.paragraphStyles.clear();
    document.page = {number(root, "width", 595.28, 72, 2880),
                     number(root, "height", 841.89, 72, 2880),
                     number(root, "margin", 56.7, 0, 1400)};
    pageGeometry(document.page);
    if (v8)
        document.page.differentFirstPage = boolean(root, "differentFirstPage");
    if (v10)
        document.trackChanges = boolean(root, "trackChanges");
    if (v13) {
        const auto columns = number(root, "columns", 1, 1, 6);
        if (std::floor(columns) != columns)
            throw std::runtime_error("Invalid column count");
        document.page.columns = static_cast<std::size_t>(columns);
        document.page.columnGap = number(root, "columnGap", 36, 0, 144);
    }
    for (auto p : root.children()) {
        if (indentation(p))
            continue;
        if (v10 && std::string_view(p.name()) == "revision") {
            attributes(p, {"id", "author", "date"});
            if (p.first_child())
                throw std::runtime_error("Unexpected revision content");
            document.revisions.push_back(
                {identifier(p, "id"), p.attribute("author").value(), p.attribute("date").value()});
            continue;
        }
        if (v9 && std::string_view(p.name()) == "comment") {
            attributes(p, {"id", "author", "date", "text", "startParagraph", "startOffset",
                           "endParagraph", "endOffset"});
            if (p.first_child())
                throw std::runtime_error("Unexpected comment content");
            Comment comment{identifier(p, "id"),
                            p.attribute("author").value(),
                            p.attribute("date").value(),
                            utf16(p.attribute("text").value()),
                            {identifier(p, "startParagraph"), index(p, "startOffset", true),
                             Affinity::Before},
                            {identifier(p, "endParagraph"), index(p, "endOffset", true),
                             Affinity::After}};
            try {
                validateCommentText(comment.text);
                validateCommentAuthor(comment.author);
            } catch (const std::invalid_argument &error) {
                throw std::runtime_error(error.what());
            }
            document.comments.push_back(std::move(comment));
            continue;
        }
        if (v12 && std::string_view(p.name()) == "field") {
            attributes(p, {"id", "instruction", "startParagraph", "startOffset", "endParagraph",
                           "endOffset"});
            if (p.first_child())
                throw std::runtime_error("Unexpected field content");
            Field field{identifier(p, "id"),
                        p.attribute("instruction").value(),
                        {identifier(p, "startParagraph"), index(p, "startOffset", true),
                         Affinity::Before},
                        {identifier(p, "endParagraph"), index(p, "endOffset", true),
                         Affinity::After}};
            try {
                validateFieldInstruction(field.instruction);
            } catch (const std::invalid_argument &error) {
                throw std::runtime_error(error.what());
            }
            document.fields.push_back(std::move(field));
            continue;
        }
        if (v9 && std::string_view(p.name()) == "footnote") {
            attributes(p, {"id", "text"});
            if (p.first_child())
                throw std::runtime_error("Unexpected footnote content");
            const auto text = utf16(p.attribute("text").value());
            try {
                validateFootnoteText(text);
            } catch (const std::invalid_argument &error) {
                throw std::runtime_error(error.what());
            }
            document.footnotes.push_back({identifier(p, "id"), text});
            continue;
        }
        if (v9 && std::string_view(p.name()) == "preservedPart") {
            attributes(p, {"path", "contentType", "part"});
            if (p.first_child() || !packaged)
                throw std::runtime_error("Invalid preserved document part");
            const std::string stored = p.attribute("part").value();
            if (!stored.starts_with("preserved/") || !parts.contains(stored) ||
                !consumed.insert(stored).second)
                throw std::runtime_error("Invalid or duplicate preserved document part");
            if (!preserved)
                preserved = std::make_shared<PreservedPackage>();
            preserved->parts.push_back(
                {p.attribute("path").value(), p.attribute("contentType").value(), parts.at(stored)});
            continue;
        }
        if (v9 && std::string_view(p.name()) == "preservedRelationship") {
            attributes(p, {"package", "type", "target"});
            if (p.first_child())
                throw std::runtime_error("Invalid preserved document relationship");
            if (!preserved)
                preserved = std::make_shared<PreservedPackage>();
            preserved->relationships.push_back(
                {boolean(p, "package"), p.attribute("type").value(), p.attribute("target").value()});
            continue;
        }
        if (v8 && (std::string_view(p.name()) == "header" || std::string_view(p.name()) == "footer")) {
            attributes(p, {"left", "center", "right"});
            for (auto nested : p.children())
                if (!indentation(nested))
                    throw std::runtime_error("Unexpected header or footer content");
            auto &band =
                std::string_view(p.name()) == "header" ? document.page.header : document.page.footer;
            if (!band.empty())
                throw std::runtime_error("Duplicate native header or footer");
            band = {utf16(p.attribute("left").value()), utf16(p.attribute("center").value()),
                    utf16(p.attribute("right").value())};
            if (band.empty())
                throw std::runtime_error("Empty native header or footer");
            continue;
        }
        if (v6 && std::string_view(p.name()) == "style") {
            document.styles.paragraphStyles.push_back(readStyle(p, v11, v12));
            continue;
        }
        if (v7 && std::string_view(p.name()) == "charStyle") {
            document.styles.characterStyles.push_back(readCharacterStyle(p, v12));
            continue;
        }
        if ((version == "5" || v6) && std::string_view(p.name()) == "table") {
            if (v11)
                attributes(p, {"id", "rows", "columns", "borderWidth", "borderColor"});
            else
                attributes(p, {"id", "rows", "columns"});
            Table table{identifier(p, "id"), index(p, "rows"), index(p, "columns")};
            if (p.attribute("borderWidth") || p.attribute("borderColor")) {
                table.border.width = number(p, "borderWidth", 0.5, 0, 6);
                table.border.color = static_cast<std::uint32_t>(
                    number(p, "borderColor", 0xff805a3b, 0, 4294967295.0));
            }
            for (auto nested : p.children()) {
                if (indentation(nested))
                    continue;
                const std::string_view name = nested.name();
                if (v11 && name == "shade") {
                    attributes(nested, {"row", "column", "color"});
                    if (nested.first_child() || table.shading.size() >= 10000)
                        throw std::runtime_error("Invalid native table shading");
                    table.shading.push_back(
                        {index(nested, "row", true), index(nested, "column", true),
                         static_cast<std::uint32_t>(number(nested, "color", 0, 0, 4294967295.0))});
                } else if (v8 && name == "column" && table.merges.empty() &&
                           table.shading.empty()) {
                    attributes(nested, {"width"});
                    if (!nested.attribute("width") || nested.first_child())
                        throw std::runtime_error("Invalid native table column");
                    table.widths.push_back(number(nested, "width", 0, 0.01, 2880));
                } else if (v8 && name == "merge") {
                    attributes(nested, {"row", "column", "rows", "columns"});
                    if (nested.first_child() || table.merges.size() >= 10000)
                        throw std::runtime_error("Invalid native table merge");
                    table.merges.push_back({index(nested, "row", true), index(nested, "column", true),
                                            index(nested, "rows"), index(nested, "columns")});
                } else
                    throw std::runtime_error("Unexpected native table metadata content");
            }
            document.tables.push_back(std::move(table));
            continue;
        }
        if ((version == "4" || version == "5" || v6) &&
            std::string_view(p.name()) == "imageResource") {
            attributes(p, {"id", "path", "pixelWidth", "pixelHeight"});
            for (auto nested : p.children())
                if (!indentation(nested))
                    throw std::runtime_error("Unexpected image resource content");
            const auto id = identifier(p, "id");
            const auto path = "images/" + std::to_string(id) + ".png";
            if (p.attribute("path").value() != path || !parts.contains(path) ||
                !consumed.insert(path).second)
                throw std::runtime_error("Invalid or duplicate native image part");
            const auto width = identifier(p, "pixelWidth"), height = identifier(p, "pixelHeight");
            if (width > std::numeric_limits<std::uint32_t>::max() ||
                height > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Invalid native image pixel dimensions");
            auto resource = std::make_shared<const ImageResource>(
                ImageResource{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                              parts.at(path)});
            validateImageResource(*resource);
            document.images.emplace(id, std::move(resource));
            continue;
        }
        if ((version == "3" || version == "4" || version == "5" || v6) &&
            std::string_view(p.name()) == "bookmark") {
            attributes(p, {"id", "name", "paragraph", "offset", "affinity"});
            for (auto nested : p.children())
                if (!indentation(nested))
                    throw std::runtime_error("Unexpected bookmark content");
            const auto offset = identifier(p, "offset", true);
            if (offset > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Bookmark offset exceeds native index size");
            const std::string_view affinity = p.attribute("affinity").value();
            if (affinity != "before" && affinity != "after")
                throw std::runtime_error("Invalid bookmark affinity");
            document.bookmarks.push_back(
                {identifier(p, "id"),
                 p.attribute("name").value(),
                 {identifier(p, "paragraph"), static_cast<std::size_t>(offset),
                  affinity == "before" ? Affinity::Before : Affinity::After}});
            continue;
        }
        if (std::string_view(p.name()) != "p")
            throw std::runtime_error("Unknown native block; refusing lossy import");
        if (version == "1")
            attributes(p,
                       {"align", "after", "list", "preset", "indent", "before", "line", "break"});
        else if (v11)
            attributes(p, {"align", "after", "list", "style", "direct", "indent", "firstLine",
                           "rightIndent", "before", "line", "break", "id", "table", "row",
                           "column", v13 ? "section" : "id", v13 ? "sectionWidth" : "id",
                           v13 ? "sectionHeight" : "id", v13 ? "sectionMargin" : "id",
                           v13 ? "sectionColumns" : "id", v13 ? "sectionGap" : "id"});
        else if (v6)
            attributes(p, {"align", "after", "list", "style", "direct", "indent", "before", "line",
                           "break", "id", "table", "row", "column"});
        else if (version == "5")
            attributes(p, {"align", "after", "list", "preset", "indent", "before", "line", "break",
                           "id", "table", "row", "column"});
        else
            attributes(
                p, {"align", "after", "list", "preset", "indent", "before", "line", "break", "id"});
        Paragraph paragraph;
        paragraph.id = version == "1" ? document.allocateId() : identifier(p, "id");
        if (p.attribute("table") || p.attribute("row") || p.attribute("column"))
            paragraph.cell =
                TableCell{identifier(p, "table"), index(p, "row", true), index(p, "column", true)};
        auto alignment = number(p, "align", 0, 0, v11 ? 3 : 2);
        if (std::floor(alignment) != alignment)
            throw std::runtime_error("Invalid alignment");
        paragraph.alignment = static_cast<Alignment>(static_cast<int>(alignment));
        paragraph.spaceAfter = number(p, "after", 8, 0, 200);
        const auto list = number(p, "list", 0, 0, 2), preset = number(p, "preset", 0, 0, 3);
        if (std::floor(list) != list || std::floor(preset) != preset)
            throw std::runtime_error("Invalid paragraph enum");
        paragraph.list = static_cast<ListKind>(static_cast<int>(list));
        if (v6) {
            if (p.attribute("style"))
                paragraph.style = p.attribute("style").value();
            if (p.attribute("direct"))
                paragraph.direct = mask(p, AllParagraphFields);
        } else
            paragraph.style = presetStyleId(static_cast<ParagraphPreset>(static_cast<int>(preset)));
        paragraph.indentLeft = number(p, "indent", 0, 0, 360);
        paragraph.indentFirstLine = number(p, "firstLine", 0, -360, 360);
        paragraph.indentRight = number(p, "rightIndent", 0, 0, 360);
        paragraph.spaceBefore = number(p, "before", 0, 0, 200);
        paragraph.lineSpacing = number(p, "line", 1.15, 1, 3);
        paragraph.pageBreakBefore = boolean(p, "break");
        if (p.attribute("section")) {
            const std::string_view kind = p.attribute("section").value();
            if (kind != "page" && kind != "continuous")
                throw std::runtime_error("Invalid section break");
            SectionBreak end;
            end.continuous = kind == "continuous";
            end.page.width = number(p, "sectionWidth", 595.28, 72, 2880);
            end.page.height = number(p, "sectionHeight", 841.89, 72, 2880);
            end.page.margin = number(p, "sectionMargin", 56.7, 0, 1400);
            const auto columns = number(p, "sectionColumns", 1, 1, 6);
            if (std::floor(columns) != columns)
                throw std::runtime_error("Invalid column count");
            end.page.columns = static_cast<std::size_t>(columns);
            end.page.columnGap = number(p, "sectionGap", 36, 0, 144);
            paragraph.sectionBreak = end;
        }
        paragraph.runs.clear();
        for (auto r : p.children()) {
            if (indentation(r))
                continue;
            const bool image = (version == "4" || version == "5" || v6) &&
                               std::string_view(r.name()) == "image";
            const bool note = v9 && std::string_view(r.name()) == "note";
            if (std::string_view(r.name()) != "r" && !image && !note)
                throw std::runtime_error("Unknown inline content; refusing lossy import");
            if (note)
                attributes(r, {"id", "family", "size", "bold", "italic", "underline", "color",
                               "strike", "highlight", "direct", "cstyle", v10 ? "ins" : "id",
                               v10 ? "del" : "id", v12 ? "theme" : "id",
                               v12 ? "endnote" : "id"});
            else if (image)
                attributes(r, {"resource", "width", "height", "description", "family", "size",
                               "bold", "italic", "underline", "color", "strike", "highlight",
                               v6 ? "direct" : "resource", v7 ? "cstyle" : "resource",
                               v10 ? "ins" : "resource", v10 ? "del" : "resource",
                               v12 ? "theme" : "resource", v12 ? "link" : "resource"});
            else
                attributes(r, {"family", "size", "bold", "italic", "underline", "color", "strike",
                               "highlight", v6 ? "direct" : "family", v7 ? "cstyle" : "family",
                               v10 ? "ins" : "family", v10 ? "del" : "family",
                               v12 ? "theme" : "family", v12 ? "link" : "family"});
            for (auto child : r.children())
                if (image || note ? !indentation(child)
                          : child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata)
                    throw std::runtime_error("Unexpected nested inline content");
            Run run;
            if (!image && !note)
                for (auto child : r.children())
                    run.appendText(utf16(child.value()));
            for (auto ch : run.text())
                if (ch < 32 && ch != u'\t')
                    throw std::runtime_error("Invalid character in paragraph");
            run.style.family = r.attribute("family").as_string("Arial");
            if (run.style.family.size() > 256)
                throw std::runtime_error("Font family too long");
            run.style.size = number(r, "size", 12, 1, 200);
            run.style.bold = boolean(r, "bold");
            run.style.italic = boolean(r, "italic");
            run.style.underline = boolean(r, "underline");
            run.style.strike = boolean(r, "strike");
            run.style.highlight =
                static_cast<std::uint32_t>(number(r, "highlight", 0, 0, 4294967295.0));
            run.style.color =
                static_cast<std::uint32_t>(number(r, "color", 0xff202631, 0, 4294967295.0));
            if (r.attribute("theme"))
                run.style.theme = parseThemeColorText(r.attribute("theme").value(), run.style.color);
            if (note)
                run = Run::fromNote({identifier(r, "id"), boolean(r, "endnote")}, run.style);
            if (image)
                run = Run::fromImage({identifier(r, "resource"), number(r, "width", 0, 0, 2880),
                                      number(r, "height", 0, 0, 2880),
                                      r.attribute("description").value()},
                                     run.style);
            if (v6 && r.attribute("direct"))
                run.direct = mask(r, AllCharacterFields);
            if (r.attribute("link")) {
                run.link = r.attribute("link").value();
                validateLink(run.link);
            }
            if (r.attribute("ins"))
                run.inserted = identifier(r, "ins");
            if (r.attribute("del"))
                run.deleted = identifier(r, "del");
            if (v7 && r.attribute("cstyle")) {
                run.characterStyle = r.attribute("cstyle").value();
                if (run.characterStyle.empty())
                    throw std::runtime_error("Empty character style reference");
            }
            paragraph.runs.push_back(std::move(run));
        }
        if (paragraph.runs.empty())
            paragraph.runs.push_back({u"", {}});
        document.paragraphs.push_back(std::move(paragraph));
    }
    if (document.paragraphs.empty())
        throw std::runtime_error("Native document has no paragraphs");
    pageGeometry(document.page); // header and footer text
    if (version == "5" && document.tables.empty())
        throw std::runtime_error("WFG5 requires table metadata");
    try {
        // Older versions stored resolved values only; WFG6 must already agree with its styles.
        if (!v6)
            adoptStyles(document);
    } catch (const std::invalid_argument &error) {
        throw std::runtime_error(error.what());
    }
    if (version != "1")
        document.nextId = identifier(root, "nextId");
    if (preserved) {
        if (preserved->parts.empty())
            throw std::runtime_error("Preserved relationships without parts");
        try {
            validatePreserved(*preserved);
        } catch (const std::invalid_argument &error) {
            throw std::runtime_error(error.what());
        }
        document.preserved = std::move(preserved);
    }
    if (packaged && consumed.size() != parts.size())
        throw std::runtime_error("Unknown native package part; refusing lossy import");
    validateDocument(document);
    return document;
}
} // namespace wf
