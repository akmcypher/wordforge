#include "core/formats/docx.hpp"
#include "core/formats/zip_package.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace wf {
namespace {
constexpr auto wordNs = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr auto relNs = "http://schemas.openxmlformats.org/package/2006/relationships";
std::string local(std::string name) {
    auto colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}
std::string ns(pugi::xml_node node, const std::string &prefix) {
    const auto key = prefix.empty() ? "xmlns" : "xmlns:" + prefix;
    for (; node; node = node.parent())
        if (auto attr = node.attribute(key.c_str()))
            return attr.value();
    return {};
}
std::string namespaceOf(pugi::xml_node node, std::string name, bool attribute = false) {
    const auto colon = name.find(':');
    if (colon == std::string::npos)
        return attribute ? "" : ns(node, "");
    return ns(node, name.substr(0, colon));
}
bool wordNamespace(const std::string &uri) {
    return uri == wordNs || uri == "http://purl.oclc.org/ooxml/wordprocessingml/main";
}
bool is(pugi::xml_node node, const char *name) {
    return node.type() == pugi::node_element && local(node.name()) == name &&
           wordNamespace(namespaceOf(node, node.name()));
}
pugi::xml_node child(pugi::xml_node node, const char *name) {
    for (auto item : node.children())
        if (is(item, name))
            return item;
    return {};
}
std::string attr(pugi::xml_node node, const char *name) {
    for (auto item : node.attributes())
        if (local(item.name()) == name && wordNamespace(namespaceOf(node, item.name(), true)))
            return item.value();
    return {};
}
std::string val(pugi::xml_node node) {
    return attr(node, "val");
}
bool enabled(pugi::xml_node node) {
    const auto value = val(node);
    if (value.empty() || value == "true" || value == "1" || value == "on")
        return true;
    if (value == "false" || value == "0" || value == "off")
        return false;
    throw std::runtime_error("Invalid OOXML boolean");
}
double number(const std::string &text) {
    double value;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value))
        throw std::runtime_error("Invalid OOXML numeric value");
    return value;
}
std::uint32_t color(const std::string &text) {
    if (text.size() != 6)
        throw std::runtime_error("Invalid OOXML color");
    std::uint32_t value;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("Invalid OOXML color");
    return 0xff000000 | value;
}
std::string hex(std::uint32_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string text(6, '0');
    for (int index = 5; index >= 0; --index, value >>= 4)
        text[static_cast<std::size_t>(index)] = digits[value & 15];
    return text;
}
void warn(std::vector<std::string> &warnings, const std::string &message) {
    if (std::find(warnings.begin(), warnings.end(), message) == warnings.end())
        warnings.push_back(message);
}
pugi::xml_document xml(const std::string &bytes) {
    pugi::xml_document document;
    if (bytes.find('\0') != std::string::npos ||
        !document.load_buffer(bytes.data(), bytes.size(),
                              pugi::parse_default | pugi::parse_ws_pcdata | pugi::parse_doctype))
        throw std::runtime_error("Invalid DOCX XML");
    int roots = 0;
    for (auto node : document.children()) {
        if (node.type() == pugi::node_doctype)
            throw std::runtime_error("DOCX XML DTDs are not supported");
        roots += node.type() == pugi::node_element;
    }
    if (roots != 1)
        throw std::runtime_error("DOCX XML must have one root element");
    return document;
}
std::string saved(const pugi::xml_document &document) {
    std::ostringstream output;
    document.save(output, "", pugi::format_raw, pugi::encoding_utf8);
    return output.str();
}
pugi::xml_node element(pugi::xml_node parent, const char *name) {
    return parent.append_child(("w:" + std::string(name)).c_str());
}
void set(pugi::xml_node node, const char *name, const std::string &value) {
    node.append_attribute(("w:" + std::string(name)).c_str()) = value.c_str();
}
void set(pugi::xml_node node, const char *name, double value) {
    node.append_attribute(("w:" + std::string(name)).c_str()) =
        static_cast<long long>(std::llround(value));
}
void property(pugi::xml_node parent, const char *name, const std::string &value) {
    set(element(parent, name), "val", value);
}
void property(pugi::xml_node parent, const char *name, double value) {
    set(element(parent, name), "val", value);
}
// A header or footer part: one paragraph with centre and right tab stops at the text
// edge, "{PAGE}" and "{NUMPAGES}" written as Word fields.
std::string bandPart(bool header, const PageBand &band, double width) {
    pugi::xml_document part;
    auto root = part.append_child(header ? "w:hdr" : "w:ftr");
    root.append_attribute("xmlns:w") = wordNs;
    auto p = element(root, "p");
    auto ppr = element(p, "pPr");
    auto tabs = element(ppr, "tabs");
    for (auto [kind, position] : {std::pair{"center", width / 2}, std::pair{"right", width}}) {
        auto tab = element(tabs, "tab");
        set(tab, "val", std::string(kind));
        set(tab, "pos", position * 20);
    }
    set(element(ppr, "spacing"), "after", 0.0);
    auto run = [&](pugi::xml_node parent) {
        auto r = element(parent, "r");
        auto rpr = element(r, "rPr");
        property(rpr, "color", std::string("5F6673"));
        property(rpr, "sz", 20.0);
        return r;
    };
    auto text = [&](const std::u16string &value) {
        if (value.empty())
            return;
        auto t = element(run(p), "t");
        t.append_attribute("xml:space") = "preserve";
        t.text().set(utf8(value).c_str());
    };
    auto segment = [&](const std::u16string &value) {
        std::size_t start = 0;
        for (std::size_t i = 0; i < value.size();) {
            const bool page = value.compare(i, 6, u"{PAGE}") == 0;
            const bool count = value.compare(i, 10, u"{NUMPAGES}") == 0;
            if (!page && !count) {
                ++i;
                continue;
            }
            text(value.substr(start, i - start));
            auto field = element(p, "fldSimple");
            set(field, "instr", std::string(page ? " PAGE " : " NUMPAGES "));
            element(run(field), "t").text().set("1");
            i += page ? 6 : 10;
            start = i;
        }
        text(value.substr(start));
    };
    segment(band.left);
    if (!band.center.empty() || !band.right.empty()) {
        element(run(p), "tab");
        segment(band.center);
    }
    if (!band.right.empty()) {
        element(run(p), "tab");
        segment(band.right);
    }
    return saved(part);
}
void runProperties(pugi::xml_node properties, CharacterStyle &style,
                   std::vector<std::string> &warnings, bool styleDefinition = false) {
    auto toggle = [styleDefinition](bool previous, pugi::xml_node node) {
        return styleDefinition ? (previous != enabled(node)) : enabled(node);
    };
    for (auto node : properties.children()) {
        if (node.type() != pugi::node_element)
            continue;
        if (is(node, "rFonts")) {
            auto family = attr(node, "ascii");
            if (family.empty())
                family = attr(node, "hAnsi");
            if (!family.empty())
                style.family = family;
            if (!attr(node, "asciiTheme").empty())
                warn(warnings, "Theme fonts use a local fallback.");
        } else if (is(node, "b"))
            style.bold = toggle(style.bold, node);
        else if (is(node, "i"))
            style.italic = toggle(style.italic, node);
        else if (is(node, "strike"))
            style.strike = toggle(style.strike, node);
        else if (is(node, "sz"))
            style.size = number(val(node)) / 2;
        else if (is(node, "u")) {
            style.underline = val(node) != "none";
            if (val(node) != "single" && val(node) != "none" && !val(node).empty())
                warn(warnings, "Underline patterns are shown as a single line.");
        } else if (is(node, "color")) {
            if (val(node) != "auto")
                style.color = color(val(node));
            if (!attr(node, "themeColor").empty())
                warn(warnings, "Theme colors use their saved fallback values.");
        } else if (is(node, "shd")) {
            const auto fill = attr(node, "fill");
            if (!fill.empty() && fill != "auto")
                style.highlight = color(fill);
        } else if (is(node, "highlight")) {
            static const std::map<std::string, std::uint32_t> colors{{"yellow", 0xffffff00},
                                                                     {"green", 0xff00ff00},
                                                                     {"cyan", 0xff00ffff},
                                                                     {"magenta", 0xffff00ff},
                                                                     {"blue", 0xff0000ff},
                                                                     {"red", 0xffff0000},
                                                                     {"darkBlue", 0xff000080},
                                                                     {"darkCyan", 0xff008080},
                                                                     {"darkGreen", 0xff008000},
                                                                     {"darkMagenta", 0xff800080},
                                                                     {"darkRed", 0xff800000},
                                                                     {"darkYellow", 0xff808000},
                                                                     {"darkGray", 0xff808080},
                                                                     {"lightGray", 0xffc0c0c0},
                                                                     {"black", 0xff000000},
                                                                     {"white", 0xffffffff},
                                                                     {"none", 0}};
            if (auto found = colors.find(val(node)); found != colors.end())
                style.highlight = found->second;
            else
                warn(warnings, "An unknown highlight color was omitted.");
        } else if (!is(node, "rStyle") && !is(node, "szCs") && !is(node, "bCs") &&
                   !is(node, "iCs") && !is(node, "lang") && !is(node, "noProof"))
            warn(warnings, "Some character options are not supported: " + local(node.name()));
    }
    for (auto [simple, complex] : {std::pair{"b", "bCs"}, std::pair{"i", "iCs"}}) {
        const auto cs = child(properties, complex);
        if (!cs)
            continue;
        if (!child(properties, simple)) {
            auto &value = std::string_view(simple) == "b" ? style.bold : style.italic;
            value = toggle(value, cs);
            warn(warnings, "Complex-script-only emphasis is applied to the entire run.");
        } else if (enabled(cs) != enabled(child(properties, simple)))
            warn(warnings, "Different Latin and complex-script emphasis is approximated.");
    }
    if (auto cs = child(properties, "szCs")) {
        if (!child(properties, "sz")) {
            style.size = number(val(cs)) / 2;
            warn(warnings, "Complex-script-only font size is applied to the entire run.");
        } else if (val(cs) != val(child(properties, "sz")))
            warn(warnings, "Different Latin and complex-script font sizes are approximated.");
    }
}
void paragraphProperties(pugi::xml_node properties, Paragraph &paragraph,
                         std::vector<std::string> &warnings) {
    for (auto node : properties.children()) {
        if (node.type() != pugi::node_element)
            continue;
        if (is(node, "jc")) {
            const auto alignment = val(node);
            if (alignment == "center")
                paragraph.alignment = Alignment::Center;
            else if (alignment == "right" || alignment == "end")
                paragraph.alignment = Alignment::Right;
            else if (alignment == "left" || alignment == "start")
                paragraph.alignment = Alignment::Left;
            else
                warn(warnings, "Justified/distributed alignment is not supported yet.");
        } else if (is(node, "pageBreakBefore"))
            paragraph.pageBreakBefore = enabled(node);
        else if (is(node, "spacing")) {
            if (!attr(node, "before").empty())
                paragraph.spaceBefore = number(attr(node, "before")) / 20;
            if (!attr(node, "after").empty())
                paragraph.spaceAfter = number(attr(node, "after")) / 20;
            if (!attr(node, "line").empty()) {
                if (attr(node, "lineRule").empty() || attr(node, "lineRule") == "auto")
                    paragraph.lineSpacing = number(attr(node, "line")) / 240;
                else
                    warn(warnings, "Fixed-height line spacing is approximated.");
            }
        } else if (is(node, "ind")) {
            const auto left = attr(node, "left").empty() ? attr(node, "start") : attr(node, "left");
            if (!left.empty())
                paragraph.indentLeft = number(left) / 20;
            if (!attr(node, "hanging").empty() || !attr(node, "firstLine").empty() ||
                !attr(node, "right").empty())
                warn(warnings, "Hanging/first-line/right indents are not preserved.");
        } else if (!is(node, "pStyle") && !is(node, "numPr") && !is(node, "rPr"))
            warn(warnings, "Some paragraph options are not supported: " + local(node.name()));
    }
}
std::string relationship(const opc::Parts &parts, const std::string &owner, const char *type,
                         const std::string &id = {}) {
    const auto path = std::filesystem::path(owner);
    const auto rel = owner.empty()
                         ? std::string("_rels/.rels")
                         : (path.parent_path() / "_rels" / (path.filename().string() + ".rels"))
                               .generic_string();
    auto found = parts.find(rel);
    if (found == parts.end())
        return {};
    auto relationships = xml(found->second);
    for (auto node : relationships.document_element().children()) {
        if (local(node.name()) != "Relationship" || namespaceOf(node, node.name()) != relNs)
            continue;
        auto relation = std::string(node.attribute("Type").value());
        if (!relation.ends_with("/" + std::string(type)) ||
            (!id.empty() && id != node.attribute("Id").value()))
            continue;
        if (std::string_view(node.attribute("TargetMode").value()) == "External")
            throw std::runtime_error("External DOCX document parts are not loaded");
        auto target = std::string(node.attribute("Target").value());
        if (target.find('\\') != std::string::npos || target.find(':') != std::string::npos ||
            target.find('%') != std::string::npos || target.empty())
            throw std::runtime_error("Unsupported OPC relationship target");
        auto resolved = (target.front() == '/' ? std::filesystem::path(target.substr(1))
                                               : path.parent_path() / target)
                            .lexically_normal()
                            .generic_string();
        if (resolved.starts_with("../") || resolved == "..")
            throw std::runtime_error("OPC relationship escapes package");
        return resolved;
    }
    return {};
}
// Writes the character formatting `value` into rPr. With `base`, only what differs from
// it, as a style definition: b/i/strike there toggle the inherited value.
void writeCharacter(pugi::xml_node rpr, const CharacterStyle &value, const CharacterStyle *base) {
    auto differs = [&](auto member) { return !base || value.*member != base->*member; };
    auto opaque = [](std::uint32_t colour) {
        if ((colour >> 24) != 255)
            throw std::runtime_error(
                "DOCX export cannot preserve transparent text colors; save as .wfg");
    };
    if (differs(&CharacterStyle::family)) {
        auto fonts = element(rpr, "rFonts");
        for (auto script : {"ascii", "hAnsi", "cs", "eastAsia"})
            set(fonts, script, value.family);
    }
    auto toggle = [&](bool CharacterStyle::*member, const char *name, const char *complex) {
        if (base ? value.*member != base->*member : value.*member) {
            element(rpr, name);
            if (complex)
                element(rpr, complex);
        }
    };
    toggle(&CharacterStyle::bold, "b", "bCs");
    toggle(&CharacterStyle::italic, "i", "iCs");
    toggle(&CharacterStyle::strike, "strike", nullptr);
    if (differs(&CharacterStyle::color)) {
        opaque(value.color);
        property(rpr, "color", hex(value.color));
    }
    if (differs(&CharacterStyle::size)) {
        property(rpr, "sz", value.size * 2);
        property(rpr, "szCs", value.size * 2);
    }
    const bool highlight = base ? value.highlight != base->highlight : value.highlight != 0;
    if (highlight && !value.highlight)
        property(rpr, "highlight", std::string("none"));
    if (differs(&CharacterStyle::underline))
        property(rpr, "u", std::string(value.underline ? "single" : "none"));
    if (highlight && value.highlight) {
        opaque(value.highlight);
        auto shading = element(rpr, "shd");
        set(shading, "val", std::string("clear"));
        set(shading, "fill", hex(value.highlight));
    }
}
// Paragraph counterpart of writeCharacter (lists are written on paragraphs only).
void writeParagraph(pugi::xml_node ppr, const ParagraphFormat &value, const ParagraphFormat *base) {
    auto differs = [&](auto member) { return !base || value.*member != base->*member; };
    if (differs(&ParagraphFormat::spaceBefore) || differs(&ParagraphFormat::spaceAfter) ||
        differs(&ParagraphFormat::lineSpacing)) {
        auto spacing = element(ppr, "spacing");
        if (differs(&ParagraphFormat::spaceBefore))
            set(spacing, "before", value.spaceBefore * 20);
        if (differs(&ParagraphFormat::spaceAfter))
            set(spacing, "after", value.spaceAfter * 20);
        if (differs(&ParagraphFormat::lineSpacing)) {
            set(spacing, "line", value.lineSpacing * 240);
            set(spacing, "lineRule", std::string("auto"));
        }
    }
    if (differs(&ParagraphFormat::indentLeft))
        set(element(ppr, "ind"), "left", value.indentLeft * 20);
    if (differs(&ParagraphFormat::alignment))
        property(ppr, "jc", std::string(value.alignment == Alignment::Center  ? "center"
                                        : value.alignment == Alignment::Right ? "right"
                                                                              : "left"));
}
// Word recognises its built-in headings by these names (navigation pane, outline).
std::string wordStyleName(const ParagraphStyle &style) {
    if (style.id == "Heading1")
        return "heading 1";
    if (style.id == "Heading2")
        return "heading 2";
    return style.name;
}
constexpr const char *drawingNs = "http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr const char *placementNs =
    "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
constexpr const char *relationshipsNs =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
bool isIn(pugi::xml_node node, const char *name, const char *uri) {
    return node.type() == pugi::node_element && local(node.name()) == name &&
           namespaceOf(node, node.name()) == uri;
}
pugi::xml_node childIn(pugi::xml_node node, const char *name, const char *uri) {
    for (auto item : node.children())
        if (isIn(item, name, uri))
            return item;
    return {};
}
// First element named `name` in `uri` below `node`, at most `depth` levels down.
pugi::xml_node findIn(pugi::xml_node node, const char *name, const char *uri, int depth = 8) {
    for (auto item : node.children()) {
        if (isIn(item, name, uri))
            return item;
        if (depth > 0)
            if (auto found = findIn(item, name, uri, depth - 1))
                return found;
    }
    return {};
}
std::string attrIn(pugi::xml_node node, const char *name, const char *uri) {
    for (auto item : node.attributes())
        if (local(item.name()) == name && namespaceOf(node, item.name(), true) == uri)
            return item.value();
    return {};
}
} // namespace
DocxImport parseDocx(const std::string &bytes, const PictureConverter &convertPicture) {
    auto parts = opc::read(bytes);
    if (!parts.contains("[Content_Types].xml"))
        throw std::runtime_error("Missing DOCX content types");
    const auto main = relationship(parts, "", "officeDocument");
    if (main.empty() || !parts.contains(main))
        throw std::runtime_error("DOCX main document relationship is missing");
    auto content = xml(parts.at(main));
    if (!is(content.document_element(), "document"))
        throw std::runtime_error("Unsupported DOCX document namespace");
    auto body = child(content.document_element(), "body");
    if (!body)
        throw std::runtime_error("DOCX document has no body");
    DocxImport result;
    auto &warnings = result.warnings;
    auto &document = result.document;
    document.paragraphs.clear();
    CharacterStyle defaultRun;
    Paragraph defaultParagraph;
    defaultParagraph.spaceAfter = 0;
    defaultParagraph.lineSpacing = 1;
    pugi::xml_document styles;
    std::map<std::string, pugi::xml_node> styleNodes;
    const auto stylesPart = relationship(parts, main, "styles");
    std::string defaultStyle;
    if (!stylesPart.empty()) {
        if (!parts.contains(stylesPart))
            throw std::runtime_error("Missing DOCX styles part");
        styles = xml(parts.at(stylesPart));
        auto root = styles.document_element();
        auto defaults = child(root, "docDefaults");
        runProperties(child(child(defaults, "rPrDefault"), "rPr"), defaultRun, warnings);
        paragraphProperties(child(child(defaults, "pPrDefault"), "pPr"), defaultParagraph,
                            warnings);
        for (auto node : root.children())
            if (is(node, "style")) {
                const auto id = attr(node, "styleId");
                if (!styleNodes.emplace(id, node).second)
                    throw std::runtime_error("Duplicate DOCX style identifier");
                const auto defaultValue = attr(node, "default");
                if (attr(node, "type") == "paragraph" &&
                    (defaultValue == "1" || defaultValue == "true" || defaultValue == "on"))
                    defaultStyle = id;
            }
    }
    std::function<void(const std::string &, Paragraph &, CharacterStyle &, std::set<std::string> &)>
        applyStyle;
    applyStyle = [&](const std::string &id, Paragraph &p, CharacterStyle &s,
                     std::set<std::string> &seen) {
        if (id.empty())
            return;
        if (seen.size() >= 64 || !seen.insert(id).second)
            throw std::runtime_error("Cyclic or excessively nested DOCX styles");
        if (auto found = styleNodes.find(id); found != styleNodes.end()) {
            auto node = found->second;
            applyStyle(val(child(node, "basedOn")), p, s, seen);
            paragraphProperties(child(node, "pPr"), p, warnings);
            runProperties(child(node, "rPr"), s, warnings, true);
        } else
            warn(warnings, "A referenced document style is missing; local defaults are used.");
        seen.erase(id);
    };
    std::map<std::string, ListKind> numbering;
    std::map<std::string, bool> approximateNumbering;
    const auto numberingPart = relationship(parts, main, "numbering");
    if (!numberingPart.empty()) {
        if (!parts.contains(numberingPart))
            throw std::runtime_error("Missing DOCX numbering part");
        auto definitions = xml(parts.at(numberingPart));
        std::map<std::string, ListKind> abstracts;
        std::map<std::string, bool> approximate;
        for (auto node : definitions.document_element().children())
            if (is(node, "abstractNum")) {
                auto level = child(node, "lvl");
                auto format = val(child(level, "numFmt"));
                abstracts[attr(node, "abstractNumId")] = format == "bullet"    ? ListKind::Bullet
                                                         : format == "decimal" ? ListKind::Decimal
                                                                               : ListKind::None;
                const auto expectedLabel = format == "bullet" ? "•" : "%1.";
                approximate[attr(node, "abstractNumId")] =
                    val(child(level, "start")) != "1" ||
                    val(child(level, "lvlText")) != expectedLabel || bool(child(level, "pPr")) ||
                    bool(child(level, "rPr"));
            }
        for (auto node : definitions.document_element().children())
            if (is(node, "num")) {
                const auto id = attr(node, "numId");
                const auto abstract = val(child(node, "abstractNumId"));
                numbering[id] = abstracts[abstract];
                approximateNumbering[id] =
                    approximate[abstract] || bool(child(node, "lvlOverride"));
            }
    }
    std::string previousNum;
    // Custom paragraph styles become Wordforge styles: each records what differs from the
    // style it is based on, so its paragraphs resolve to exactly what Word shows.
    auto styleNameTaken = [&](const std::string &candidate) {
        const auto &sheet = document.styles;
        return std::any_of(sheet.paragraphStyles.begin(), sheet.paragraphStyles.end(),
                           [&](const auto &other) { return other.name == candidate; }) ||
               std::any_of(sheet.characterStyles.begin(), sheet.characterStyles.end(),
                           [&](const auto &other) { return other.name == candidate; });
    };
    // Character styles keep only the properties their own definition sets.
    std::map<std::string, std::string> importedCharacterStyles; // DOCX id -> Wordforge id
    std::function<std::string(const std::string &, unsigned)> importCharacterStyle;
    importCharacterStyle = [&](const std::string &id, unsigned depth) -> std::string {
        if (id.empty())
            return {};
        if (auto done = importedCharacterStyles.find(id); done != importedCharacterStyles.end())
            return done->second;
        const auto found = styleNodes.find(id);
        importedCharacterStyles[id] = {}; // until defined; also ends any base cycle
        if (found == styleNodes.end() || attr(found->second, "type") != "character" || depth > 64 ||
            document.styles.paragraphStyles.size() + document.styles.characterStyles.size() >= 1024)
            return {};
        const auto node = found->second;
        NamedCharacterStyle style;
        style.basedOn = importCharacterStyle(val(child(node, "basedOn")), depth + 1);
        style.id = id;
        if (id.size() > 128 || document.styles.find(id) || document.styles.findCharacter(id) ||
            std::any_of(id.begin(), id.end(), [](char ch) { return static_cast<unsigned char>(ch) < 32; }))
            style.id = uniqueStyleId(document.styles, id);
        auto name = val(child(node, "name"));
        if (name.empty() || name.size() > 200)
            name = style.id;
        style.name = name;
        for (int n = 2; styleNameTaken(style.name); ++n)
            style.name = name + " (" + std::to_string(n) + ")";
        CharacterStyle own;
        std::vector<std::string> ignored;
        runProperties(child(node, "rPr"), own, ignored);
        style.character = propertiesFor(CharacterStyle{}, own);
        auto trial = document.styles;
        trial.characterStyles.push_back(style);
        try {
            validateStyles(trial);
        } catch (const std::exception &) {
            warn(warnings, "A character style could not be kept; its formatting is applied directly.");
            return {};
        }
        document.styles = std::move(trial);
        return importedCharacterStyles[id] = style.id;
    };
    std::map<std::string, std::string> importedStyles; // DOCX id -> Wordforge id
    std::vector<std::pair<std::string, pugi::xml_node>> createdStyles;
    std::function<std::string(const std::string &)> importStyle;
    importStyle = [&](const std::string &id) -> std::string {
        if (isBuiltinStyle(id))
            return id;
        if (auto done = importedStyles.find(id); done != importedStyles.end())
            return done->second;
        const auto found = styleNodes.find(id);
        if (found == styleNodes.end() || attr(found->second, "type") != "paragraph" ||
            document.styles.paragraphStyles.size() >= 1024)
            return "Normal";
        importedStyles[id] = "Normal"; // until defined; also ends any base cycle
        const auto node = found->second;
        const auto basedOn = val(child(node, "basedOn"));
        const auto parent = basedOn.empty() ? std::string() : importStyle(basedOn);
        Paragraph format = defaultParagraph;
        auto character = defaultRun;
        std::set<std::string> seen;
        std::vector<std::string> ignored; // reported once, by the paragraphs using the style
        std::swap(ignored, warnings);
        applyStyle(id, format, character, seen);
        std::swap(ignored, warnings);
        const auto base = parent.empty() ? ResolvedStyle{} : resolveStyle(document.styles, parent);
        ParagraphStyle style;
        style.id = id;
        if (id.empty() || id.size() > 128 ||
            std::any_of(id.begin(), id.end(), [](char ch) { return static_cast<unsigned char>(ch) < 32; }))
            style.id = uniqueStyleId(document.styles, id);
        style.basedOn = parent;
        auto name = val(child(node, "name"));
        if (name.empty() || name.size() > 200)
            name = style.id;
        auto taken = [&](const std::string &candidate) { return styleNameTaken(candidate); };
        style.name = name;
        for (int n = 2; taken(style.name); ++n)
            style.name = name + " (" + std::to_string(n) + ")";
        style.paragraph = propertiesFor(base.paragraph, format);
        style.character = propertiesFor(base.character, character);
        auto trial = document.styles;
        trial.paragraphStyles.push_back(style);
        try {
            validateStyles(trial);
        } catch (const std::exception &) {
            warn(warnings, "A document style could not be kept; its formatting is applied directly.");
            return importedStyles[id] = parent.empty() ? std::string("Normal") : parent;
        }
        document.styles = std::move(trial);
        createdStyles.emplace_back(style.id, node);
        return importedStyles[id] = style.id;
    };
    // Inline (and floating, placed inline) pictures: PNG directly, other formats through
    // the converter. Sizes come from the drawing's extent and are fitted to the page later.
    std::size_t pictureBytes = 0;
    auto readPicture = [&](pugi::xml_node drawing, const CharacterStyle &style) -> std::optional<Run> {
        auto frame = childIn(drawing, "inline", placementNs);
        if (!frame) {
            frame = childIn(drawing, "anchor", placementNs);
            if (frame)
                warn(warnings, "Floating pictures are placed inline with the text.");
        }
        const auto blip = findIn(frame, "blip", drawingNs);
        const auto embed = attrIn(blip, "embed", relationshipsNs);
        if (!frame || embed.empty()) {
            warn(warnings, "A drawing that is not a picture was omitted.");
            return std::nullopt;
        }
        const auto target = relationship(parts, main, "image", embed);
        if (target.empty() || !parts.contains(target)) {
            warn(warnings, "A picture whose data is missing was omitted.");
            return std::nullopt;
        }
        std::string png = parts.at(target);
        if (png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0) {
            std::optional<std::string> converted;
            if (convertPicture)
                converted = convertPicture(png);
            if (!converted) {
                warn(warnings, "A picture in an unsupported format was omitted.");
                return std::nullopt;
            }
            png = std::move(*converted);
        }
        if (png.size() < 24) {
            warn(warnings, "An unreadable picture was omitted.");
            return std::nullopt;
        }
        auto dimension = [&](std::size_t offset) {
            std::uint32_t value = 0;
            for (std::size_t index = offset; index < offset + 4; ++index)
                value = (value << 8) | static_cast<unsigned char>(png[index]);
            return value;
        };
        auto resource = std::make_shared<ImageResource>(
            ImageResource{dimension(16), dimension(20), std::move(png)});
        try {
            validateImageResource(*resource);
        } catch (const std::exception &) {
            warn(warnings, "A picture larger than the preview limits was omitted.");
            return std::nullopt;
        }
        if (resource->png.size() > 64 * 1024 * 1024 - pictureBytes) {
            warn(warnings, "Pictures beyond the 64 MiB document limit were omitted.");
            return std::nullopt;
        }
        pictureBytes += resource->png.size();
        const auto extent = childIn(frame, "extent", placementNs);
        double width = extent.attribute("cx").as_double() / 12700,
               height = extent.attribute("cy").as_double() / 12700;
        if (!(width >= 1 && height >= 1 && std::isfinite(width) && std::isfinite(height))) {
            width = resource->pixelWidth * 0.75;
            height = resource->pixelHeight * 0.75;
        }
        const auto scale = std::min({1.0, 2880 / width, 2880 / height});
        const auto properties = childIn(frame, "docPr", placementNs);
        std::string description = properties.attribute("descr").value();
        if (description.empty())
            description = properties.attribute("name").value();
        if (description.size() > 4096)
            description.resize(4096);
        for (auto &ch : description)
            if (static_cast<unsigned char>(ch) < 32)
                ch = ' ';
        const auto id = document.allocateId();
        document.images[id] = std::move(resource);
        return Run::fromImage({id, std::max(1.0, width * scale), std::max(1.0, height * scale),
                               description},
                              style);
    };
    std::function<void(pugi::xml_node, const std::optional<TableCell> &)> readParagraph =
        [&](pugi::xml_node node, const std::optional<TableCell> &cell) {
        Paragraph paragraph = defaultParagraph;
        paragraph.id = document.allocateId();
        paragraph.runs.clear();
        auto base = defaultRun;
        auto properties = child(node, "pPr");
        auto styleId = val(child(properties, "pStyle"));
        if (styleId.empty())
            styleId = defaultStyle;
        std::set<std::string> seen;
        applyStyle(styleId, paragraph, base, seen);
        paragraph.style = styleId.empty() ? std::string("Normal") : importStyle(styleId);
        paragraphProperties(properties, paragraph, warnings);
        if (cell) {
            paragraph.cell = cell;
            paragraph.pageBreakBefore = false; // not allowed inside a table
        }
        // Resolve numbering through the same base -> derived -> direct cascade as styles.
        std::vector<pugi::xml_node> propertyChain{properties};
        auto numberingStyle = styleId;
        std::set<std::string> numberingSeen;
        while (!numberingStyle.empty()) {
            if (numberingSeen.size() >= 64 || !numberingSeen.insert(numberingStyle).second)
                throw std::runtime_error("Cyclic DOCX numbering style inheritance");
            auto found = styleNodes.find(numberingStyle);
            if (found == styleNodes.end())
                break;
            propertyChain.push_back(child(found->second, "pPr"));
            numberingStyle = val(child(found->second, "basedOn"));
        }
        std::string numberId, numberLevel;
        bool hasNumbering = false;
        for (auto item = propertyChain.rbegin(); item != propertyChain.rend(); ++item)
            if (auto num = child(*item, "numPr")) {
                hasNumbering = true;
                if (child(num, "numId"))
                    numberId = val(child(num, "numId"));
                if (child(num, "ilvl"))
                    numberLevel = val(child(num, "ilvl"));
            }
        if (hasNumbering) {
            const auto &id = numberId;
            if (id != "0") {
                paragraph.list = numbering[id];
                if (paragraph.list == ListKind::None)
                    warn(warnings, "An unsupported list format was omitted.");
                if (approximateNumbering[id])
                    warn(warnings, "Custom list labels, starts, marker styles or indentation are "
                                   "approximated.");
                if (!previousNum.empty() && previousNum != id)
                    warn(warnings, "Adjacent list restarts may be renumbered.");
            }
            const auto &level = numberLevel;
            if (!level.empty() && level != "0")
                warn(warnings, "Nested lists are shown at a single level.");
            previousNum = id;
        } else
            previousNum.clear();
        std::function<void(pugi::xml_node, unsigned)> readRuns = [&](pugi::xml_node parent,
                                                                     unsigned depth) {
            if (depth > 64)
                throw std::runtime_error("Excessively nested DOCX inline content");
            for (auto run : parent.children()) {
                if (run.type() != pugi::node_element || is(run, "pPr"))
                    continue;
                if (is(run, "bookmarkStart") || is(run, "bookmarkEnd")) {
                    warn(warnings, "DOCX bookmarks are not imported yet.");
                    continue;
                }
                if (is(run, "hyperlink")) {
                    warn(warnings, "Hyperlink text is preserved; link targets are not.");
                    readRuns(run, depth + 1);
                } else if (is(run, "r")) {
                    auto style = base;
                    std::set<std::string> runSeen;
                    applyStyle(val(child(child(run, "rPr"), "rStyle")), paragraph, style, runSeen);
                    runProperties(child(run, "rPr"), style, warnings);
                    std::u16string text;
                    const auto characterStyle =
                        importCharacterStyle(val(child(child(run, "rPr"), "rStyle")), 0);
                    bool pictures = false;
                    for (auto item : run.children()) {
                        if (is(item, "drawing")) {
                            pictures = true; // even when omitted: no empty run is left behind
                            if (auto picture = readPicture(item, style)) {
                                if (!text.empty()) {
                                    paragraph.runs.push_back({std::move(text), style});
                                    paragraph.runs.back().characterStyle = characterStyle;
                                    text.clear();
                                }
                                picture->characterStyle = characterStyle;
                                paragraph.runs.push_back(std::move(*picture));
                            }
                        } else if (is(item, "t")) {
                            for (auto chunk : item.children())
                                if (chunk.type() == pugi::node_pcdata ||
                                    chunk.type() == pugi::node_cdata)
                                    text += utf16(chunk.value());
                        } else if (is(item, "tab"))
                            text += u'\t';
                        else if (is(item, "br") || is(item, "cr")) {
                            text += u'\n';
                            warn(warnings, "Inline breaks are converted to paragraph breaks; "
                                           "page/column break types are not preserved.");
                        } else if (!is(item, "rPr") && !is(item, "lastRenderedPageBreak"))
                            warn(warnings,
                                 "Unsupported inline content was omitted: " + local(item.name()));
                    }
                    if (!text.empty() || !pictures) {
                        paragraph.runs.push_back({std::move(text), std::move(style)});
                        paragraph.runs.back().characterStyle = characterStyle;
                    }
                } else
                    warn(warnings,
                         "Unsupported paragraph content was omitted: " + local(run.name()));
            }
        };
        readRuns(node, 0);
        Paragraph split = paragraph;
        split.runs.clear();
        for (const auto &run : paragraph.runs) {
            if (run.image()) {
                split.runs.push_back(run);
                continue;
            }
            std::size_t start = 0;
            for (;;) {
                const auto end = run.text().find(u'\n', start);
                split.runs.push_back(
                    {run.text().substr(start, end == std::u16string::npos ? end : end - start),
                     run.style});
                split.runs.back().characterStyle = run.characterStyle;
                if (end == std::u16string::npos)
                    break;
                document.paragraphs.push_back(split);
                split.id = document.allocateId();
                split.pageBreakBefore = false;
                split.runs.clear();
                start = end + 1;
            }
        }
        if (split.runs.empty())
            split.runs.push_back({u"", base});
        document.paragraphs.push_back(std::move(split));
        };
    auto emptyParagraph = [&](const std::optional<TableCell> &cell) {
        Paragraph paragraph = defaultParagraph;
        paragraph.id = document.allocateId();
        paragraph.style = defaultStyle.empty() ? std::string("Normal") : importStyle(defaultStyle);
        paragraph.cell = cell;
        paragraph.pageBreakBefore = false;
        paragraph.runs = {{u"", defaultRun}};
        return paragraph;
    };
    // Tables: Wordforge tables are a complete grid of 1-1000 rows and 1-10 columns whose
    // cells hold paragraphs. Merged cells are split; nested tables are flattened into the
    // cell that holds them; larger tables become plain paragraphs.
    bool nestedWarned = false;
    std::function<void(pugi::xml_node, const std::optional<TableCell> &, unsigned)> readCell =
        [&](pugi::xml_node cellNode, const std::optional<TableCell> &cell, unsigned depth) {
            if (depth > 16)
                throw std::runtime_error("Excessively nested DOCX tables");
            for (auto item : cellNode.children()) {
                if (item.type() != pugi::node_element || is(item, "tcPr"))
                    continue;
                if (is(item, "p"))
                    readParagraph(item, cell);
                else if (is(item, "tbl")) {
                    if (!nestedWarned)
                        warn(warnings, "Nested tables are flattened into the cell that holds them.");
                    nestedWarned = true;
                    for (auto row : item.children())
                        if (is(row, "tr"))
                            for (auto inner : row.children())
                                if (is(inner, "tc"))
                                    readCell(inner, cell, depth + 1);
                } else
                    warn(warnings, "Unsupported table content was omitted: " + local(item.name()));
            }
        };
    std::function<void(pugi::xml_node)> readTable = [&](pugi::xml_node table) {
        std::vector<std::vector<pugi::xml_node>> rows; // null nodes are cells covered by a span
        std::size_t columns = 0;
        bool shaded = false;
        // Spans: (row, column, columns, vMerge state: 0 none, 1 restart, 2 continue).
        struct Span {
            std::size_t row, column, columns;
            int vertical;
        };
        std::vector<Span> spans;
        for (auto row : table.children()) {
            if (row.type() != pugi::node_element || is(row, "tblPr") || is(row, "tblGrid"))
                continue;
            if (!is(row, "tr")) {
                warn(warnings, "Unsupported table content was omitted: " + local(row.name()));
                continue;
            }
            std::vector<pugi::xml_node> cells;
            for (auto cell : row.children()) {
                if (!is(cell, "tc")) {
                    if (cell.type() == pugi::node_element && !is(cell, "trPr") && !is(cell, "tblPrEx"))
                        warn(warnings, "Unsupported table content was omitted: " + local(cell.name()));
                    continue;
                }
                const auto properties = child(cell, "tcPr");
                if (child(properties, "shd"))
                    shaded = true;
                const auto span = val(child(properties, "gridSpan"));
                const auto count = span.empty() ? 1.0 : std::clamp(number(span), 1.0, 64.0);
                const auto vMerge = child(properties, "vMerge");
                const auto vMergeValue = attr(vMerge, "val");
                spans.push_back({rows.size(), cells.size(), static_cast<std::size_t>(count),
                                 !vMerge ? 0 : vMergeValue == "restart" ? 1 : 2});
                cells.push_back(cell);
                for (int extra = 1; extra < static_cast<int>(count); ++extra)
                    cells.emplace_back();
            }
            columns = std::max(columns, cells.size());
            rows.push_back(std::move(cells));
        }
        if (rows.empty() || !columns)
            return;
        if (columns > 10 || rows.size() > 1000) {
            warn(warnings, "A table with more than 10 columns or 1000 rows was imported as paragraphs.");
            for (const auto &row : rows)
                for (auto cell : row)
                    if (cell)
                        readCell(cell, std::nullopt, 0);
            return;
        }
        if (shaded)
            warn(warnings, "Table cell shading is not preserved.");
        // Two tables in a row need a body paragraph between them.
        if (!document.paragraphs.empty() && document.paragraphs.back().cell)
            document.paragraphs.push_back(emptyParagraph(std::nullopt));
        Table created{document.allocateId(), rows.size(), columns};
        // Merges: gridSpan across, vMerge continuing the cell of the same columns above.
        std::map<std::size_t, std::size_t> open; // first column -> index in merges
        std::vector<CellMerge> merges;
        for (const auto &span : spans) {
            if (span.column >= columns)
                continue;
            const auto width = std::min(span.columns, columns - span.column);
            if (span.vertical == 2) {
                const auto found = open.find(span.column);
                if (found != open.end() && merges[found->second].columns == width &&
                    merges[found->second].row + merges[found->second].rows == span.row) {
                    ++merges[found->second].rows;
                    continue;
                }
            }
            for (auto it = open.begin(); it != open.end();)
                it = it->first >= span.column && it->first < span.column + width ? open.erase(it)
                                                                                  : std::next(it);
            merges.push_back({span.row, span.column, 1, width});
            if (span.vertical)
                open[span.column] = merges.size() - 1;
        }
        std::vector<bool> covered(rows.size() * columns, false);
        bool overlapping = false;
        for (const auto &merge : merges) {
            if (merge.rows * merge.columns < 2)
                continue;
            bool free = true;
            for (auto r = merge.row; r < merge.row + merge.rows; ++r)
                for (auto c = merge.column; c < merge.column + merge.columns; ++c)
                    free = free && !covered[r * columns + c];
            if (!free) {
                overlapping = true;
                continue;
            }
            for (auto r = merge.row; r < merge.row + merge.rows; ++r)
                for (auto c = merge.column; c < merge.column + merge.columns; ++c)
                    covered[r * columns + c] = true;
            created.merges.push_back(merge);
        }
        if (overlapping)
            warn(warnings, "Overlapping merged table cells are split into separate cells.");
        // Column widths from the grid, when it matches.
        std::vector<double> widths;
        for (auto column : child(table, "tblGrid").children())
            if (is(column, "gridCol")) {
                const auto w = attr(column, "w");
                widths.push_back(w.empty() ? 0 : number(w) / 20);
            }
        if (widths.size() == columns &&
            std::all_of(widths.begin(), widths.end(),
                        [](double w) { return std::isfinite(w) && w > 0 && w <= 2880; }))
            created.widths = std::move(widths);
        document.tables.push_back(created);
        for (std::size_t r = 0; r < rows.size(); ++r)
            for (std::size_t c = 0; c < columns; ++c) {
                const TableCell cell{created.id, r, c};
                const auto before = document.paragraphs.size();
                if (c < rows[r].size() && rows[r][c])
                    readCell(rows[r][c], cell, 0);
                if (document.paragraphs.size() == before)
                    document.paragraphs.push_back(emptyParagraph(cell));
            }
    };
    // Headers and footers: the default one of each, as left/centre/right text split at
    // tabs, with PAGE and NUMPAGES fields kept. Everything else is simplified with a warning.
    auto readBand = [&](const std::string &id, bool header) {
        PageBand band;
        const auto path = relationship(parts, main, header ? "header" : "footer", id);
        if (path.empty() || !parts.contains(path)) {
            warn(warnings, "A missing header or footer part was ignored.");
            return band;
        }
        auto part = xml(parts.at(path));
        std::vector<std::u16string> segments{u""};
        std::optional<Alignment> alignment;
        bool joined = false, omitted = false, skipResult = false;
        std::string instruction;
        int fieldState = 0; // 0 outside a complex field, 1 in its code, 2 in its result
        auto field = [&](const std::string &code) {
            std::istringstream words(code);
            std::string word;
            words >> word;
            if (word == "PAGE")
                segments.back() += u"{PAGE}";
            else if (word == "NUMPAGES")
                segments.back() += u"{NUMPAGES}";
            else
                return false;
            return true;
        };
        std::function<void(pugi::xml_node)> walk = [&](pugi::xml_node node) {
            for (auto item : node.children()) {
                if (item.type() != pugi::node_element)
                    continue;
                if (is(item, "p")) {
                    if (segments.size() > 1 || !segments.back().empty()) {
                        // A further paragraph continues the current segment.
                        const auto count = segments.size();
                        const auto size = segments.back().size();
                        segments.back() += u"  ";
                        walk(item);
                        if (segments.size() == count && segments.back().size() == size + 2)
                            segments.back().resize(size);
                        else
                            joined = true;
                    } else {
                        const auto jc = val(child(child(item, "pPr"), "jc"));
                        alignment = jc == "center"                 ? Alignment::Center
                                    : jc == "right" || jc == "end" ? Alignment::Right
                                                                   : Alignment::Left;
                        walk(item);
                    }
                } else if (is(item, "fldSimple")) {
                    if (!field(attr(item, "instr")))
                        walk(item);
                } else if (is(item, "fldChar")) {
                    const auto type = attr(item, "fldCharType");
                    if (type == "begin") {
                        fieldState = 1;
                        instruction.clear();
                    } else if (type == "separate") {
                        fieldState = 2;
                        skipResult = field(instruction);
                    } else if (type == "end") {
                        if (fieldState == 1)
                            field(instruction);
                        fieldState = 0;
                        skipResult = false;
                    }
                } else if (is(item, "instrText")) {
                    if (fieldState == 1)
                        instruction += item.text().get();
                } else if (is(item, "t")) {
                    if (!(fieldState == 2 && skipResult))
                        segments.back() += utf16(item.text().get());
                } else if (is(item, "tab")) {
                    segments.emplace_back();
                } else if (is(item, "r") || is(item, "hyperlink") || is(item, "smartTag") ||
                           is(item, "sdt") || is(item, "sdtContent") || is(item, "ins")) {
                    walk(item);
                } else if (is(item, "drawing") || is(item, "pict") || is(item, "tbl") ||
                           is(item, "object")) {
                    omitted = true;
                }
            }
        };
        walk(part.document_element());
        if (joined)
            warn(warnings, "Header and footer paragraphs are joined into one line.");
        if (omitted)
            warn(warnings, "Pictures and tables in headers and footers are omitted.");
        for (auto &segment : segments) {
            for (auto &c : segment)
                if (c < 0x20 || c == 0x7f || c == 0xfffc)
                    c = u' ';
            if (segment.size() > 500)
                segment.resize(500);
        }
        if (segments.size() == 1) {
            const auto where = alignment.value_or(Alignment::Left);
            (where == Alignment::Center  ? band.center
             : where == Alignment::Right ? band.right
                                         : band.left) = segments[0];
        } else {
            band.left = segments[0];
            band.center = segments[1];
            for (std::size_t i = 2; i < segments.size(); ++i)
                band.right += (i > 2 ? u" " : u"") + segments[i];
            if (band.right.size() > 500)
                band.right.resize(500);
        }
        // Whitespace-only text shows nothing.
        for (auto *text : {&band.left, &band.center, &band.right})
            if (text->find_first_not_of(u' ') == std::u16string::npos)
                text->clear();
        return band;
    };
    auto readBands = [&](pugi::xml_node section) {
        const auto titlePage = child(section, "titlePg");
        document.page.differentFirstPage = titlePage && enabled(titlePage);
        for (auto reference : section.children()) {
            const bool header = is(reference, "headerReference");
            if (!header && !is(reference, "footerReference"))
                continue;
            const auto type = attr(reference, "type");
            const auto id = attrIn(reference, "id", relationshipsNs);
            if (type == "even") {
                warn(warnings, "Separate even-page headers and footers are not preserved.");
                continue;
            }
            if (type == "first") {
                if (document.page.differentFirstPage && !readBand(id, header).empty())
                    warn(warnings, "A first-page header or footer is not preserved; the first "
                                   "page shows none.");
                continue;
            }
            (header ? document.page.header : document.page.footer) = readBand(id, header);
        }
    };
    for (auto node : body.children()) {
        if (node.type() != pugi::node_element)
            continue;
        if (is(node, "sectPr")) {
            if (auto size = child(node, "pgSz")) {
                document.page.width = number(attr(size, "w")) / 20;
                document.page.height = number(attr(size, "h")) / 20;
            }
            if (auto margin = child(node, "pgMar")) {
                const auto top = number(attr(margin, "top")) / 20;
                document.page.margin = top;
                for (auto side : {"left", "right", "bottom"})
                    if (number(attr(margin, side)) / 20 != top)
                        warn(warnings, "Unequal page margins are approximated by the top margin.");
            }
            readBands(node);
            for (auto propertyNode : node.children())
                if (propertyNode.type() == pugi::node_element && !is(propertyNode, "pgSz") &&
                    !is(propertyNode, "pgMar") && !is(propertyNode, "headerReference") &&
                    !is(propertyNode, "footerReference") && !is(propertyNode, "titlePg"))
                    warn(warnings,
                         "Section features are not preserved: " + local(propertyNode.name()));
            continue;
        }
        if (is(node, "tbl")) {
            readTable(node);
            continue;
        }
        if (!is(node, "p")) {
            warn(warnings, "Unsupported document content was omitted: " + local(node.name()));
            continue;
        }
        readParagraph(node, std::nullopt);
    }
    // A table must be followed by a body paragraph.
    if (!document.paragraphs.empty() && document.paragraphs.back().cell)
        document.paragraphs.push_back(emptyParagraph(std::nullopt));
    if (document.paragraphs.empty())
        document.paragraphs.emplace_back(document.allocateId());
    // Pictures larger than their column or the page would not lay out: scale them down.
    if (!document.images.empty()) {
        const double content = document.page.width - 2 * document.page.margin;
        std::map<std::uint64_t, std::size_t> tableColumns;
        for (const auto &table : document.tables)
            tableColumns[table.id] = table.columns;
        for (auto &paragraph : document.paragraphs) {
            double available = paragraph.cell ? content / tableColumns.at(paragraph.cell->tableId) - 8
                                              : content;
            available -= paragraph.indentLeft + (paragraph.list != ListKind::None ? 40 : 0) + 1;
            double tallest = 0;
            for (const auto &run : paragraph.runs)
                tallest = std::max(tallest, run.style.size);
            const double maxHeight = (document.page.height - 2 * document.page.margin) /
                                         std::max(1.0, paragraph.lineSpacing) -
                                     tallest - 1;
            for (auto &run : paragraph.runs)
                if (const auto *image = run.image()) {
                    const auto scale = std::min(
                        {1.0, available / image->width, maxHeight / image->height});
                    if (scale >= 1)
                        continue;
                    auto fitted = *image;
                    fitted.width = std::max(1.0, image->width * scale);
                    fitted.height = std::max(1.0, image->height * scale);
                    auto characterStyle = run.characterStyle;
                    run = Run::fromImage(std::move(fitted), run.style);
                    run.characterStyle = std::move(characterStyle);
                }
        }
    }
    // Enter after an imported style continues with its DOCX "next" style when that was kept.
    for (const auto &[id, node] : createdStyles) {
        const auto next = val(child(node, "next"));
        const auto target = isBuiltinStyle(next) ? next : importedStyles.contains(next)
                                                              ? importedStyles.at(next)
                                                              : std::string();
        if (!target.empty() && target != id)
            document.styles.find(id)->next = target;
    }
    adoptStyles(document); // DOCX values arrive resolved; mark those differing from styles direct
    validateDocument(document);
    return result;
}
std::string serializeDocx(const Document &document) {
    if (!document.bookmarks.empty())
        throw std::runtime_error(
            "DOCX bookmark export is not implemented yet; save as .wfg to preserve bookmarks");
    validateDocument(document);
    pugi::xml_document content;
    auto root = content.append_child("w:document");
    root.append_attribute("xmlns:w") = wordNs;
    root.append_attribute("xmlns:r") =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    root.append_attribute("xmlns:wp") =
        "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
    root.append_attribute("xmlns:a") = "http://schemas.openxmlformats.org/drawingml/2006/main";
    root.append_attribute("xmlns:pic") = "http://schemas.openxmlformats.org/drawingml/2006/picture";
    unsigned long long drawingId = 0;
    std::set<std::uint64_t> imageIds;
    auto body = element(root, "body");
    pugi::xml_document numbers;
    auto numRoot = numbers.append_child("w:numbering");
    numRoot.append_attribute("xmlns:w") = wordNs;
    for (int kind = 0; kind < 2; ++kind) {
        auto abstract = element(numRoot, "abstractNum");
        set(abstract, "abstractNumId", kind);
        auto level = element(abstract, "lvl");
        set(level, "ilvl", 0);
        property(level, "start", 1);
        property(level, "numFmt", std::string(kind ? "decimal" : "bullet"));
        property(level, "lvlText", std::string(kind ? "%1." : "•"));
        property(level, "lvlJc", std::string("left"));
    }
    ListKind previous = ListKind::None;
    int numId = 0;
    std::map<std::uint64_t, const Table *> tables;
    for (const auto &table : document.tables)
        tables.emplace(table.id, &table);
    std::optional<TableCell> previousCell;
    pugi::xml_node tableNode, rowNode, cellNode;
    std::vector<double> edges;
    std::map<std::pair<std::size_t, std::size_t>, pugi::xml_node> anchors;
    for (const auto &paragraph : document.paragraphs) {
        auto parent = body;
        if (paragraph.cell) {
            const auto &cell = *paragraph.cell;
            if (!previousCell || previousCell->tableId != cell.tableId) {
                const auto &table = *tables.at(cell.tableId);
                tableNode = element(body, "tbl");
                auto properties = element(tableNode, "tblPr");
                const auto width = (document.page.width - 2 * document.page.margin) * 20;
                edges = columnEdges(table, document.page.width - 2 * document.page.margin);
                anchors.clear();
                auto tableWidth = element(properties, "tblW");
                set(tableWidth, "w", width);
                set(tableWidth, "type", std::string("dxa"));
                auto borders = element(properties, "tblBorders");
                for (const auto *side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
                    auto border = element(borders, side);
                    set(border, "val", std::string("single"));
                    set(border, "sz", 4);
                    set(border, "color", std::string("805A3B"));
                }
                set(element(properties, "tblLayout"), "type", std::string("fixed"));
                auto margins = element(properties, "tblCellMar");
                for (const auto *side : {"top", "left", "bottom", "right"}) {
                    auto margin = element(margins, side);
                    set(margin, "w", 80);
                    set(margin, "type", std::string("dxa"));
                }
                auto grid = element(tableNode, "tblGrid");
                for (std::size_t column = 0; column < table.columns; ++column)
                    set(element(grid, "gridCol"), "w", (edges[column + 1] - edges[column]) * 20);
            }
            const auto &table = *tables.at(cell.tableId);
            const auto *merge = table.mergeAt(cell.row, cell.column);
            const bool anchor = !merge || (merge->row == cell.row && merge->column == cell.column);
            if (previousCell != paragraph.cell) {
                if (!cell.column) {
                    rowNode = element(tableNode, "tr");
                    element(element(rowNode, "trPr"), "cantSplit");
                }
                // A tc for each anchor and for the first column of a merge's later rows
                // (a vMerge continuation); cells covered across are spanned by gridSpan.
                if (anchor || merge->column == cell.column) {
                    cellNode = element(rowNode, "tc");
                    auto properties = element(cellNode, "tcPr");
                    const auto span = merge ? merge->columns : 1;
                    auto width = element(properties, "tcW");
                    set(width, "w", (edges[cell.column + span] - edges[cell.column]) * 20);
                    set(width, "type", std::string("dxa"));
                    if (span > 1)
                        property(properties, "gridSpan", static_cast<double>(span));
                    if (merge && merge->rows > 1) {
                        auto vertical = element(properties, "vMerge");
                        if (anchor)
                            set(vertical, "val", std::string("restart"));
                    }
                    if (anchor)
                        anchors[{cell.row, cell.column}] = cellNode;
                    else
                        element(cellNode, "p"); // Word requires a paragraph in every cell
                }
                previous = ListKind::None;
            }
            if (!anchor) {
                // Content typed into a covered cell is shown in, and saved to, the merged cell.
                previousCell = paragraph.cell;
                if (paragraph.text().empty())
                    continue;
                parent = anchors.at({merge->row, merge->column});
            } else
                parent = cellNode;
        } else if (previousCell) {
            previous = ListKind::None;
        }
        previousCell = paragraph.cell;
        auto p = element(parent, "p");
        auto ppr = element(p, "pPr");
        // Paragraphs and runs still carry complete formatting, so a reader that ignores
        // styles shows the same result.
        property(ppr, "pStyle", paragraph.style);
        if (paragraph.pageBreakBefore)
            element(ppr, "pageBreakBefore");
        if (paragraph.list != ListKind::None) {
            if (paragraph.list != previous) {
                ++numId;
                auto num = element(numRoot, "num");
                set(num, "numId", numId);
                property(num, "abstractNumId", paragraph.list == ListKind::Bullet ? 0 : 1);
            }
            auto num = element(ppr, "numPr");
            property(num, "ilvl", 0);
            property(num, "numId", numId);
        }
        previous = paragraph.list;
        auto spacing = element(ppr, "spacing");
        set(spacing, "before", paragraph.spaceBefore * 20);
        set(spacing, "after", paragraph.spaceAfter * 20);
        set(spacing, "line", paragraph.lineSpacing * 240);
        set(spacing, "lineRule", std::string("auto"));
        set(element(ppr, "ind"), "left", paragraph.indentLeft * 20);
        property(ppr, "jc",
                 std::string(paragraph.alignment == Alignment::Center  ? "center"
                             : paragraph.alignment == Alignment::Right ? "right"
                                                                       : "left"));
        for (const auto &run : paragraph.runs) {
            auto r = element(p, "r");
            auto rpr = element(r, "rPr");
            if (!run.characterStyle.empty())
                property(rpr, "rStyle", run.characterStyle);
            auto fonts = element(rpr, "rFonts");
            for (auto script : {"ascii", "hAnsi", "cs", "eastAsia"})
                set(fonts, script, run.style.family);
            property(rpr, "b", std::string(run.style.bold ? "1" : "0"));
            property(rpr, "bCs", std::string(run.style.bold ? "1" : "0"));
            property(rpr, "i", std::string(run.style.italic ? "1" : "0"));
            property(rpr, "iCs", std::string(run.style.italic ? "1" : "0"));
            property(rpr, "strike", std::string(run.style.strike ? "1" : "0"));
            if ((run.style.color >> 24) != 255 ||
                (run.style.highlight && (run.style.highlight >> 24) != 255))
                throw std::runtime_error(
                    "DOCX export cannot preserve transparent text colors; save as .wfg");
            property(rpr, "color", hex(run.style.color));
            property(rpr, "sz", run.style.size * 2);
            property(rpr, "szCs", run.style.size * 2);
            if (!run.style.highlight) // override a highlight the paragraph style may set
                property(rpr, "highlight", std::string("none"));
            property(rpr, "u", std::string(run.style.underline ? "single" : "none"));
            if (run.style.highlight) {
                auto shading = element(rpr, "shd");
                set(shading, "val", std::string("clear"));
                set(shading, "fill", hex(run.style.highlight));
            }
            if (const auto *image = run.image()) {
                imageIds.insert(image->resourceId);
                const auto width =
                    static_cast<unsigned long long>(std::llround(image->width * 12700));
                const auto height =
                    static_cast<unsigned long long>(std::llround(image->height * 12700));
                const auto name = "Picture " + std::to_string(++drawingId);
                auto inlinePicture = element(r, "drawing").append_child("wp:inline");
                for (const auto *side : {"distT", "distB", "distL", "distR"})
                    inlinePicture.append_attribute(side) = 0;
                auto extent = inlinePicture.append_child("wp:extent");
                extent.append_attribute("cx") = width;
                extent.append_attribute("cy") = height;
                auto properties = inlinePicture.append_child("wp:docPr");
                properties.append_attribute("id") = drawingId;
                properties.append_attribute("name") = name.c_str();
                properties.append_attribute("descr") = image->description.c_str();
                auto locks = inlinePicture.append_child("wp:cNvGraphicFramePr")
                                 .append_child("a:graphicFrameLocks");
                locks.append_attribute("noChangeAspect") = 1;
                auto data = inlinePicture.append_child("a:graphic").append_child("a:graphicData");
                data.append_attribute("uri") =
                    "http://schemas.openxmlformats.org/drawingml/2006/picture";
                auto picture = data.append_child("pic:pic");
                auto nonVisual = picture.append_child("pic:nvPicPr");
                auto pictureProperties = nonVisual.append_child("pic:cNvPr");
                pictureProperties.append_attribute("id") = drawingId;
                pictureProperties.append_attribute("name") = name.c_str();
                pictureProperties.append_attribute("descr") = image->description.c_str();
                nonVisual.append_child("pic:cNvPicPr");
                auto fill = picture.append_child("pic:blipFill");
                fill.append_child("a:blip").append_attribute("r:embed") =
                    ("image" + std::to_string(image->resourceId)).c_str();
                fill.append_child("a:stretch").append_child("a:fillRect");
                auto shape = picture.append_child("pic:spPr");
                auto transform = shape.append_child("a:xfrm");
                auto offset = transform.append_child("a:off");
                offset.append_attribute("x") = 0;
                offset.append_attribute("y") = 0;
                auto dimensions = transform.append_child("a:ext");
                dimensions.append_attribute("cx") = width;
                dimensions.append_attribute("cy") = height;
                auto geometry = shape.append_child("a:prstGeom");
                geometry.append_attribute("prst") = "rect";
                geometry.append_child("a:avLst");
                continue;
            }
            std::size_t begin = 0;
            for (;;) {
                auto end = run.text().find(u'\t', begin);
                auto t = element(r, "t");
                t.append_attribute("xml:space") = "preserve";
                t.text().set(
                    utf8(run.text().substr(begin, end == std::u16string::npos ? end : end - begin))
                        .c_str());
                if (end == std::u16string::npos)
                    break;
                element(r, "tab");
                begin = end + 1;
            }
        }
    }
    auto section = element(body, "sectPr");
    // Header and footer references come first in sectPr.
    std::vector<std::pair<std::string, std::string>> bandParts; // name, XML
    if (!document.page.header.empty() || !document.page.footer.empty()) {
        const double textWidth = document.page.width - 2 * document.page.margin;
        auto reference = [&](bool header, bool first, const PageBand &band) {
            const auto name = std::string(header ? "header" : "footer") + (first ? "First" : "");
            auto node = element(section, header ? "headerReference" : "footerReference");
            set(node, "type", std::string(first ? "first" : "default"));
            node.append_attribute("r:id") = name.c_str();
            bandParts.emplace_back(name, bandPart(header, band, textWidth));
        };
        reference(true, false, document.page.header);
        reference(false, false, document.page.footer);
        if (document.page.differentFirstPage) {
            reference(true, true, {});
            reference(false, true, {});
        }
    }
    auto size = element(section, "pgSz");
    set(size, "w", document.page.width * 20);
    set(size, "h", document.page.height * 20);
    if (document.page.width > document.page.height)
        set(size, "orient", std::string("landscape"));
    auto margin = element(section, "pgMar");
    for (auto side : {"top", "bottom", "left", "right"})
        set(margin, side, document.page.margin * 20);
    for (auto side : {"header", "footer"}) // where Wordforge draws them: mid-margin
        set(margin, side, document.page.margin * 10);
    if (document.page.differentFirstPage)
        element(section, "titlePg");
    pugi::xml_document styles;
    auto styleRoot = styles.append_child("w:styles");
    styleRoot.append_attribute("xmlns:w") = wordNs;
    // Wordforge's defaults as document defaults, so styles resolve in Word as they do here.
    const ResolvedStyle defaults;
    auto docDefaults = element(styleRoot, "docDefaults");
    writeCharacter(element(element(docDefaults, "rPrDefault"), "rPr"), defaults.character, nullptr);
    writeParagraph(element(element(docDefaults, "pPrDefault"), "pPr"), defaults.paragraph, nullptr);
    for (const auto &definition : document.styles.paragraphStyles) {
        auto style = element(styleRoot, "style");
        set(style, "type", std::string("paragraph"));
        set(style, "styleId", definition.id);
        if (definition.id == "Normal")
            set(style, "default", std::string("1"));
        property(style, "name", wordStyleName(definition));
        if (!definition.basedOn.empty())
            property(style, "basedOn", definition.basedOn);
        if (!definition.next.empty())
            property(style, "next", definition.next);
        element(style, "qFormat");
        const auto self = resolveStyle(document.styles, definition.id);
        const auto base = definition.basedOn.empty()
                              ? defaults
                              : resolveStyle(document.styles, definition.basedOn);
        writeParagraph(element(style, "pPr"), self.paragraph, &base.paragraph);
        writeCharacter(element(style, "rPr"), self.character, &base.character);
    }
    for (const auto &definition : document.styles.characterStyles) {
        auto style = element(styleRoot, "style");
        set(style, "type", std::string("character"));
        set(style, "styleId", definition.id);
        property(style, "name", definition.name);
        if (!definition.basedOn.empty())
            property(style, "basedOn", definition.basedOn);
        element(style, "qFormat");
        // Only the properties the style sets, with explicit values.
        const auto &c = definition.character;
        auto rpr = element(style, "rPr");
        if (c.family) {
            auto fonts = element(rpr, "rFonts");
            for (auto script : {"ascii", "hAnsi", "cs", "eastAsia"})
                set(fonts, script, *c.family);
        }
        if (c.bold)
            property(rpr, "b", std::string(*c.bold ? "1" : "0"));
        if (c.italic)
            property(rpr, "i", std::string(*c.italic ? "1" : "0"));
        if (c.strike)
            property(rpr, "strike", std::string(*c.strike ? "1" : "0"));
        if (c.color) {
            if ((*c.color >> 24) != 255)
                throw std::runtime_error(
                    "DOCX export cannot preserve transparent text colors; save as .wfg");
            property(rpr, "color", hex(*c.color));
        }
        if (c.size)
            property(rpr, "sz", *c.size * 2);
        if (c.highlight && !*c.highlight)
            property(rpr, "highlight", std::string("none"));
        if (c.underline)
            property(rpr, "u", std::string(*c.underline ? "single" : "none"));
        if (c.highlight && *c.highlight) {
            if ((*c.highlight >> 24) != 255)
                throw std::runtime_error(
                    "DOCX export cannot preserve transparent text colors; save as .wfg");
            auto shading = element(rpr, "shd");
            set(shading, "val", std::string("clear"));
            set(shading, "fill", hex(*c.highlight));
        }
    }
    opc::Parts parts{
        {"[Content_Types].xml",
         "<Types xmlns='http://schemas.openxmlformats.org/package/2006/content-types'><Default "
         "Extension='rels' "
         "ContentType='application/vnd.openxmlformats-package.relationships+xml'/><Default "
         "Extension='xml' ContentType='application/xml'/><Override PartName='/word/document.xml' "
         "ContentType='application/"
         "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml'/><Override "
         "PartName='/word/styles.xml' "
         "ContentType='application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml'/"
         "><Override PartName='/word/numbering.xml' "
         "ContentType='application/"
         "vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml'/></Types>"},
        {"_rels/.rels",
         "<Relationships "
         "xmlns='http://schemas.openxmlformats.org/package/2006/relationships'><Relationship "
         "Id='document' "
         "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
         "officeDocument' Target='word/document.xml'/></Relationships>"},
        {"word/_rels/document.xml.rels",
         "<Relationships "
         "xmlns='http://schemas.openxmlformats.org/package/2006/relationships'><Relationship "
         "Id='styles' "
         "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles' "
         "Target='styles.xml'/><Relationship Id='numbering' "
         "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering' "
         "Target='numbering.xml'/></Relationships>"},
        {"word/document.xml", saved(content)},
        {"word/styles.xml", saved(styles)},
        {"word/numbering.xml", saved(numbers)}};
    if (!imageIds.empty()) {
        pugi::xml_document types, relationships;
        types.load_string(parts.at("[Content_Types].xml").c_str());
        auto png = types.document_element().append_child("Default");
        png.append_attribute("Extension") = "png";
        png.append_attribute("ContentType") = "image/png";
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        for (const auto id : imageIds) {
            const auto &resource = document.images.at(id);
            const auto filename = "image" + std::to_string(id) + ".png";
            parts.emplace("word/media/" + filename, resource->png);
            auto relation = relationships.document_element().append_child("Relationship");
            relation.append_attribute("Id") = ("image" + std::to_string(id)).c_str();
            relation.append_attribute("Type") =
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
            relation.append_attribute("Target") = ("media/" + filename).c_str();
        }
        parts["[Content_Types].xml"] = saved(types);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
    if (!bandParts.empty()) {
        pugi::xml_document types, relationships;
        types.load_string(parts.at("[Content_Types].xml").c_str());
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        for (auto &[name, xmlText] : bandParts) {
            const bool header = name.starts_with("header");
            auto entry = types.document_element().append_child("Override");
            entry.append_attribute("PartName") = ("/word/" + name + ".xml").c_str();
            entry.append_attribute("ContentType") =
                header ? "application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"
                       : "application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml";
            auto relation = relationships.document_element().append_child("Relationship");
            relation.append_attribute("Id") = name.c_str();
            relation.append_attribute("Type") =
                header ? "http://schemas.openxmlformats.org/officeDocument/2006/relationships/header"
                       : "http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer";
            relation.append_attribute("Target") = (name + ".xml").c_str();
            parts.emplace("word/" + name + ".xml", std::move(xmlText));
        }
        parts["[Content_Types].xml"] = saved(types);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
    return opc::write(parts);
}
} // namespace wf
