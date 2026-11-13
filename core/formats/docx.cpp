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
std::string relationship(const opc::Parts &parts, const std::string &owner, const char *type) {
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
        if (!relation.ends_with("/" + std::string(type)))
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
constexpr const char *presetNames[] = {"Normal", "Title", "Heading1", "Heading2"};
} // namespace
DocxImport parseDocx(const std::string &bytes) {
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
            for (auto propertyNode : node.children())
                if (propertyNode.type() == pugi::node_element && !is(propertyNode, "pgSz") &&
                    !is(propertyNode, "pgMar"))
                    warn(warnings,
                         "Section features are not preserved: " + local(propertyNode.name()));
            continue;
        }
        if (!is(node, "p")) {
            warn(warnings, "Unsupported document content was omitted: " + local(node.name()));
            continue;
        }
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
        for (int index = 0; index < 4; ++index)
            if (styleId == presetNames[index])
                paragraph.preset = static_cast<ParagraphPreset>(index);
        paragraphProperties(properties, paragraph, warnings);
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
                    for (auto item : run.children()) {
                        if (is(item, "t")) {
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
                    paragraph.runs.push_back({std::move(text), std::move(style)});
                } else
                    warn(warnings,
                         "Unsupported paragraph content was omitted: " + local(run.name()));
            }
        };
        readRuns(node, 0);
        Paragraph split = paragraph;
        split.runs.clear();
        for (const auto &run : paragraph.runs) {
            std::size_t start = 0;
            for (;;) {
                const auto end = run.text().find(u'\n', start);
                split.runs.push_back(
                    {run.text().substr(start, end == std::u16string::npos ? end : end - start),
                     run.style});
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
    }
    if (document.paragraphs.empty())
        document.paragraphs.emplace_back(document.allocateId());
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
    double cellWidth = 0;
    for (const auto &paragraph : document.paragraphs) {
        auto parent = body;
        if (paragraph.cell) {
            const auto &cell = *paragraph.cell;
            if (!previousCell || previousCell->tableId != cell.tableId) {
                const auto &table = *tables.at(cell.tableId);
                tableNode = element(body, "tbl");
                auto properties = element(tableNode, "tblPr");
                const auto width = (document.page.width - 2 * document.page.margin) * 20;
                cellWidth = width / table.columns;
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
                    set(element(grid, "gridCol"), "w", cellWidth);
            }
            if (previousCell != paragraph.cell) {
                if (!cell.column) {
                    rowNode = element(tableNode, "tr");
                    element(element(rowNode, "trPr"), "cantSplit");
                }
                cellNode = element(rowNode, "tc");
                auto width = element(element(cellNode, "tcPr"), "tcW");
                set(width, "w", cellWidth);
                set(width, "type", std::string("dxa"));
                previous = ListKind::None;
            }
            parent = cellNode;
        } else if (previousCell) {
            previous = ListKind::None;
        }
        previousCell = paragraph.cell;
        auto p = element(parent, "p");
        auto ppr = element(p, "pPr");
        property(ppr, "pStyle", std::string(presetNames[static_cast<int>(paragraph.preset)]));
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
    auto size = element(section, "pgSz");
    set(size, "w", document.page.width * 20);
    set(size, "h", document.page.height * 20);
    if (document.page.width > document.page.height)
        set(size, "orient", std::string("landscape"));
    auto margin = element(section, "pgMar");
    for (auto side : {"top", "bottom", "left", "right"})
        set(margin, side, document.page.margin * 20);
    pugi::xml_document styles;
    auto styleRoot = styles.append_child("w:styles");
    styleRoot.append_attribute("xmlns:w") = wordNs;
    for (int index = 0; index < 4; ++index) {
        auto style = element(styleRoot, "style");
        set(style, "type", std::string("paragraph"));
        set(style, "styleId", std::string(presetNames[index]));
        if (!index)
            set(style, "default", std::string("1"));
        property(style, "name", std::string(presetNames[index]));
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
    return opc::write(parts);
}
} // namespace wf
