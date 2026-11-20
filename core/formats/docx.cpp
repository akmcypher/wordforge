#include "core/formats/docx.hpp"
#include "core/formats/zip_package.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
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
// A length in `perPoint` units per point (twips: 20, half-points: 2), or a Strict OOXML
// universal measure with its own unit ("36pt", "0.5in", "2.54cm", "12mm", "3pc").
double measure(const std::string &text, double perPoint) {
    static const std::pair<std::string_view, double> units[] = {
        {"pt", 1}, {"in", 72}, {"cm", 72 / 2.54}, {"mm", 72 / 25.4}, {"pc", 12}, {"pi", 12}};
    for (const auto &[suffix, points] : units)
        if (text.size() > suffix.size() && text.ends_with(suffix))
            return number(text.substr(0, text.size() - suffix.size())) * points * perPoint;
    return number(text);
}
double twips(const std::string &text) {
    return measure(text, 20);
}
double halfPoints(const std::string &text) {
    return measure(text, 2);
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
// The document theme's heading (major) and body (minor) Latin fonts, when it names them.
struct ThemeFonts {
    std::string major, minor;
};
thread_local ThemeFonts themeFonts; // set for the duration of one parseDocx
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
            if (const auto theme = attr(node, "asciiTheme"); !theme.empty()) {
                // majorHAnsi / minorHAnsi... name the theme's heading or body font.
                const auto &named =
                    theme.starts_with("major") ? themeFonts.major : themeFonts.minor;
                if (!named.empty())
                    style.family = named;
                else
                    warn(warnings, "Theme fonts use a local fallback.");
            }
        } else if (is(node, "b"))
            style.bold = toggle(style.bold, node);
        else if (is(node, "i"))
            style.italic = toggle(style.italic, node);
        else if (is(node, "strike"))
            style.strike = toggle(style.strike, node);
        else if (is(node, "sz"))
            style.size = halfPoints(val(node)) / 2;
        else if (is(node, "u")) {
            style.underline = val(node) != "none";
            if (val(node) != "single" && val(node) != "none" && !val(node).empty())
                warn(warnings, "Underline patterns are shown as a single line.");
        } else if (is(node, "color")) {
            // "auto" (Word's automatic colour) is Wordforge's default ink.
            style.color = val(node) == "auto" ? CharacterStyle{}.color : color(val(node));
            // A theme colour is shown as the value Word saved with it and kept for saving.
            style.theme = {};
            if (const auto theme = attr(node, "themeColor"); !theme.empty()) {
                auto level = [](const std::string &text) -> std::int16_t {
                    if (text.size() != 2 || !std::isxdigit(static_cast<unsigned char>(text[0])) ||
                        !std::isxdigit(static_cast<unsigned char>(text[1])))
                        return -1;
                    return static_cast<std::int16_t>(std::stoi(text, nullptr, 16));
                };
                style.theme = {themeColorIndex(theme), level(attr(node, "themeShade")),
                               level(attr(node, "themeTint")), style.color};
                if (!style.theme.index) {
                    style.theme = {};
                    warn(warnings, "Theme colors use their saved fallback values.");
                }
            }
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
            style.size = halfPoints(val(cs)) / 2;
            warn(warnings, "Complex-script-only font size is applied to the entire run.");
        } else if (val(cs) != val(child(properties, "sz")))
            warn(warnings, "Different Latin and complex-script font sizes are approximated.");
    }
}
// Word's w:ind in Word's terms (left edge, first-line offset, right edge; points). List
// paragraphs are converted to Wordforge's list geometry once the paragraph is complete.
void readIndent(pugi::xml_node node, ParagraphFormat &paragraph,
                std::vector<std::string> &warnings) {
    auto either = [&](const char *a, const char *b) {
        return attr(node, a).empty() ? attr(node, b) : attr(node, a);
    };
    if (const auto left = either("left", "start"); !left.empty())
        paragraph.indentLeft = twips(left) / 20;
    if (const auto right = either("right", "end"); !right.empty())
        paragraph.indentRight = twips(right) / 20;
    if (!attr(node, "hanging").empty())
        paragraph.indentFirstLine = -twips(attr(node, "hanging")) / 20;
    else if (!attr(node, "firstLine").empty())
        paragraph.indentFirstLine = twips(attr(node, "firstLine")) / 20;
    for (const char *chars :
         {"leftChars", "startChars", "rightChars", "endChars", "hangingChars", "firstLineChars"})
        if (!attr(node, chars).empty() && attr(node, chars) != "0") {
            warn(warnings, "Indents measured in characters are approximated in points.");
            break;
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
            else if (alignment == "both")
                paragraph.alignment = Alignment::Justify;
            else if (alignment == "distribute" || alignment == "thaiDistribute" ||
                     alignment.ends_with("Kashida")) {
                paragraph.alignment = Alignment::Justify;
                warn(warnings, "Distributed and kashida justification are shown as justified.");
            } else
                warn(warnings, "Some paragraph alignments are not supported: " + alignment);
        } else if (is(node, "pageBreakBefore"))
            paragraph.pageBreakBefore = enabled(node);
        else if (is(node, "keepNext"))
            paragraph.keepWithNext = enabled(node);
        else if (is(node, "keepLines"))
            paragraph.keepLines = enabled(node);
        else if (is(node, "spacing")) {
            if (!attr(node, "before").empty())
                paragraph.spaceBefore = twips(attr(node, "before")) / 20;
            if (!attr(node, "after").empty())
                paragraph.spaceAfter = twips(attr(node, "after")) / 20;
            if (!attr(node, "line").empty()) {
                if (attr(node, "lineRule").empty() || attr(node, "lineRule") == "auto")
                    paragraph.lineSpacing = twips(attr(node, "line")) / 240;
                else
                    warn(warnings, "Fixed-height line spacing is approximated.");
            }
        } else if (is(node, "ind"))
            readIndent(node, paragraph, warnings);
        else if (is(node, "tabs")) {
            for (auto item : node.children()) {
                if (!is(item, "tab"))
                    continue;
                const double pos = twips(attr(item, "pos")) / 20;
                const auto kind = attr(item, "val"), leader = attr(item, "leader");
                if (kind == "start" || kind == "end") warn(warnings, "Directional tabs are approximated as physical left/right stops.");
                std::erase_if(paragraph.tabs,
                              [&](const auto &t) { return std::abs(t.position - pos) < 0.001; });
                if (kind == "clear")
                    continue;
                if (pos < 0 || pos > 2880 || paragraph.tabs.size() >= 64 ||
                    (kind != "left" && kind != "right" && kind != "center" && kind != "start" &&
                     kind != "end")) {
                    warn(warnings, "Unsupported tab stops are omitted.");
                    continue;
                }
                if (!leader.empty() && leader != "none" && leader != "dot")
                    warn(warnings, "Unsupported tab leaders are shown without leaders.");
                paragraph.tabs.push_back({pos,
                                          kind == "right" || kind == "end" ? TabAlignment::Right
                                          : kind == "center"               ? TabAlignment::Center
                                                                           : TabAlignment::Left,
                                          leader == "dot" ? TabLeader::Dots : TabLeader::None});
            }
            std::sort(paragraph.tabs.begin(), paragraph.tabs.end(),
                      [](const auto &a, const auto &b) { return a.position < b.position; });
        } else if (!is(node, "pStyle") && !is(node, "numPr") && !is(node, "rPr") &&
                   !is(node, "sectPr"))
            warn(warnings, "Some paragraph options are not supported: " + local(node.name()));
    }
}
// Keep properties retain XML presence as well as their resolved value, so an explicit
// true/false equal to the base continues to override later changes to that base.
void readKeeps(pugi::xml_node ppr, ParagraphProperties &properties) {
    if (auto node = child(ppr, "keepNext"))
        properties.keepWithNext = enabled(node);
    if (auto node = child(ppr, "keepLines"))
        properties.keepLines = enabled(node);
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
std::string relationshipsPath(const std::string &owner) {
    if (owner.empty())
        return "_rels/.rels";
    const auto path = std::filesystem::path(owner);
    return (path.parent_path() / "_rels" / (path.filename().string() + ".rels")).generic_string();
}
struct OwnedRelationship {
    std::string type, target; // target: a resolved part path, or empty when external
};
// Every relationship of `owner`; internal targets resolved to part paths.
std::vector<OwnedRelationship> relationshipsOf(const opc::Parts &parts, const std::string &owner) {
    std::vector<OwnedRelationship> result;
    const auto found = parts.find(relationshipsPath(owner));
    if (found == parts.end())
        return result;
    auto relationships = xml(found->second);
    const auto directory = std::filesystem::path(owner).parent_path();
    for (auto node : relationships.document_element().children()) {
        if (local(node.name()) != "Relationship" || namespaceOf(node, node.name()) != relNs)
            continue;
        OwnedRelationship relation{node.attribute("Type").value(), {}};
        if (std::string_view(node.attribute("TargetMode").value()) != "External") {
            const std::string target = node.attribute("Target").value();
            if (target.empty() || target.find('\\') != std::string::npos ||
                target.find(':') != std::string::npos || target.find('%') != std::string::npos)
                continue;
            relation.target = (target.front() == '/' ? std::filesystem::path(target.substr(1))
                                                     : directory / target)
                                  .lexically_normal()
                                  .generic_string();
            if (relation.target.starts_with("../") || relation.target == "..")
                continue;
        }
        result.push_back(std::move(relation));
    }
    return result;
}
std::string relationshipKind(const std::string &type) {
    return type.substr(type.rfind('/') + 1);
}
// Keeps the package parts Wordforge does not edit: document properties, theme, settings,
// font table, web settings and custom XML, with the parts they reference.
// Word settings XML with its Track Changes switch set to `on`; unchanged bytes when it
// already is. The switch goes before the settings that follow it in the schema.
std::string withTrackRevisions(const std::string &data, bool on) {
    pugi::xml_document settings;
    if (!settings.load_buffer(data.data(), data.size()))
        return data;
    auto root = settings.document_element();
    std::vector<pugi::xml_node> existing;
    for (auto node : root.children())
        if (is(node, "trackRevisions"))
            existing.push_back(node);
    if (on == !existing.empty())
        return data;
    for (auto node : existing)
        root.remove_child(node);
    if (on) {
        static const std::set<std::string> later{"doNotTrackMoves",
                                                 "doNotTrackFormatting",
                                                 "documentProtection",
                                                 "autoFormatOverride",
                                                 "styleLockTheme",
                                                 "styleLockQFSet",
                                                 "defaultTabStop",
                                                 "autoHyphenation",
                                                 "consecutiveHyphenLimit",
                                                 "hyphenationZone",
                                                 "doNotHyphenateCaps",
                                                 "showEnvelope",
                                                 "summaryLength",
                                                 "clickAndTypeStyle",
                                                 "defaultTableStyle",
                                                 "evenAndOddHeaders",
                                                 "bookFoldRevPrinting",
                                                 "bookFoldPrinting",
                                                 "bookFoldPrintingSheets",
                                                 "drawingGridHorizontalSpacing",
                                                 "drawingGridVerticalSpacing",
                                                 "displayHorizontalDrawingGridEvery",
                                                 "displayVerticalDrawingGridEvery",
                                                 "doNotUseMarginsForDrawingGridOrigin",
                                                 "drawingGridHorizontalOrigin",
                                                 "drawingGridVerticalOrigin",
                                                 "doNotShadeFormData",
                                                 "noPunctuationKerning",
                                                 "characterSpacingControl",
                                                 "printTwoOnOne",
                                                 "strictFirstAndLastChars",
                                                 "noLineBreaksAfter",
                                                 "noLineBreaksBefore",
                                                 "savePreviewPicture",
                                                 "doNotValidateAgainstSchema",
                                                 "saveInvalidXml",
                                                 "ignoreMixedContent",
                                                 "alwaysShowPlaceholderText",
                                                 "doNotDemarcateInvalidXml",
                                                 "saveXmlDataOnly",
                                                 "useXSLTWhenSaving",
                                                 "saveThroughXslt",
                                                 "showXMLTags",
                                                 "alwaysMergeEmptyNamespace",
                                                 "updateFields",
                                                 "hdrShapeDefaults",
                                                 "footnotePr",
                                                 "endnotePr",
                                                 "compat",
                                                 "docVars",
                                                 "rsids",
                                                 "mathPr",
                                                 "attachedSchema",
                                                 "themeFontLang",
                                                 "clrSchemeMapping",
                                                 "doNotIncludeSubdocsInStats",
                                                 "doNotAutoCompressPictures",
                                                 "forceUpgrade",
                                                 "captions",
                                                 "readModeInkLockDown",
                                                 "smartTagType",
                                                 "schemaLibrary",
                                                 "shapeDefaults",
                                                 "doNotEmbedSmartTags",
                                                 "decimalSymbol",
                                                 "listSeparator"};
        const std::string rootName = root.name();
        const auto colon = rootName.find(':');
        const auto prefix =
            colon == std::string::npos ? std::string() : rootName.substr(0, colon + 1);
        pugi::xml_node before;
        for (auto node : root.children()) {
            if (node.type() != pugi::node_element)
                continue;
            const std::string name = node.name();
            if (!name.starts_with(prefix) || later.contains(name.substr(prefix.size()))) {
                before = node;
                break;
            }
        }
        const auto name = prefix + "trackRevisions";
        if (before)
            root.insert_child_before(name.c_str(), before);
        else
            root.append_child(name.c_str());
    }
    std::ostringstream output;
    settings.save(output, "", pugi::format_raw, pugi::encoding_utf8);
    return output.str();
}
std::shared_ptr<const PreservedPackage> preservePackage(const opc::Parts &parts,
                                                        const std::string &main,
                                                        std::vector<std::string> &warnings) {
    std::map<std::string, std::string> overrides, defaults;
    auto types = xml(parts.at("[Content_Types].xml"));
    for (auto node : types.document_element().children()) {
        if (local(node.name()) == "Override")
            overrides[node.attribute("PartName").value()] = node.attribute("ContentType").value();
        else if (local(node.name()) == "Default") {
            std::string extension = node.attribute("Extension").value();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            defaults[extension] = node.attribute("ContentType").value();
        }
    }
    auto contentType = [&](const std::string &path) -> std::string {
        if (const auto found = overrides.find("/" + path); found != overrides.end())
            return found->second;
        auto extension = std::filesystem::path(path).extension().string();
        if (extension.empty())
            return {};
        extension.erase(0, 1);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto found = defaults.find(extension);
        return found == defaults.end() ? std::string{} : found->second;
    };
    auto package = std::make_shared<PreservedPackage>();
    std::set<std::string> included, visiting;
    std::size_t total = 0;
    bool tooLarge = false;
    auto acceptable = [&](const PreservedPart &part) {
        PreservedPackage single{{part}, {}};
        try {
            validatePreserved(single);
            return true;
        } catch (const std::invalid_argument &) {
            return false;
        }
    };
    std::function<bool(const std::string &)> include = [&](const std::string &path) -> bool {
        if (included.contains(path) || visiting.contains(path))
            return true;
        const auto found = parts.find(path);
        PreservedPart part{path, contentType(path), {}};
        if (found == parts.end() || part.contentType.empty() || !acceptable(part))
            return false;
        visiting.insert(path);
        // The parts it references come along; a part whose references cannot be kept is not
        // kept either, so no dangling relationship is written.
        bool complete = true;
        for (const auto &relation : relationshipsOf(parts, path))
            if (!relation.target.empty())
                complete = include(relation.target) && complete;
        visiting.erase(path);
        if (!complete)
            return false;
        const auto rels = relationshipsPath(path);
        const auto relsFound = parts.find(rels);
        const auto size =
            found->second.size() + (relsFound == parts.end() ? 0 : relsFound->second.size());
        if (total + size > maxPreservedBytes || package->parts.size() + 2 > maxPreservedParts) {
            tooLarge = true;
            return false;
        }
        total += size;
        part.data = found->second;
        package->parts.push_back(std::move(part));
        if (relsFound != parts.end())
            package->parts.push_back({rels, {}, relsFound->second});
        included.insert(path);
        return true;
    };
    auto roots = [&](const std::string &owner, bool fromPackage,
                     std::initializer_list<std::string_view> kinds) {
        for (const auto &relation : relationshipsOf(parts, owner)) {
            const auto kind = relationshipKind(relation.type);
            if (relation.target.empty() ||
                std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
                continue;
            if (include(relation.target))
                package->relationships.push_back({fromPackage, relation.type, relation.target});
        }
    };
    roots("", true, {"core-properties", "extended-properties", "custom-properties"});
    roots(main, false, {"theme", "settings", "fontTable", "webSettings", "customXml"});
    if (tooLarge)
        warn(warnings, "Some document parts (theme, fonts or custom data) are too large to keep.");
    // Settings name footnote separators and even-page headers that are not kept.
    for (auto &part : package->parts) {
        if (part.contentType !=
            "application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml")
            continue;
        auto settings = xml(part.data);
        std::vector<pugi::xml_node> removed;
        for (auto node : settings.document_element().children())
            if (is(node, "footnotePr") || is(node, "endnotePr") || is(node, "evenAndOddHeaders"))
                removed.push_back(node);
        if (!removed.empty()) {
            for (auto node : removed)
                settings.document_element().remove_child(node);
            std::ostringstream output;
            settings.save(output, "", pugi::format_raw, pugi::encoding_utf8);
            part.data = output.str();
        }
    }
    // Name what is still dropped, ignoring parts that hold nothing a reader would miss.
    std::set<std::string> lost;
    for (const auto &relation : relationshipsOf(parts, main)) {
        const auto kind = relationshipKind(relation.type);
        static const std::set<std::string> handled{"styles",      "numbering",
                                                   "image",       "header",
                                                   "footer",      "theme",
                                                   "settings",    "fontTable",
                                                   "webSettings", "customXml",
                                                   "hyperlink",   "stylesWithEffects",
                                                   "footnotes",   "endnotes",
                                                   "comments",    "commentsExtended",
                                                   "commentsIds", "commentsExtensible",
                                                   "people"};
        if (relation.target.empty() || handled.contains(kind))
            continue;
        const auto found = parts.find(relation.target);
        if (found != parts.end() &&
            (kind == "footnotes" || kind == "endnotes" || kind == "comments")) {
            bool content = false;
            try {
                auto notes = xml(found->second);
                for (auto node : notes.document_element().children())
                    if (node.type() == pugi::node_element && attr(node, "type").empty())
                        content = true;
            } catch (const std::exception &) {
                content = true;
            }
            if (!content)
                continue;
        }
        lost.insert(kind);
    }
    if (!lost.empty()) {
        std::string names;
        for (const auto &kind : lost)
            names += (names.empty() ? "" : ", ") + kind;
        warn(warnings, "These document parts are not kept when saving: " + names + ".");
    }
    if (package->parts.empty())
        return nullptr;
    return package;
}
// w:color: hex, or "auto" for Wordforge's default ink when `automatic`, plus the Word theme
// colour it stands for.
void writeColor(pugi::xml_node rpr, std::uint32_t color, const std::optional<ThemeColor> &theme,
                bool automatic) {
    auto node = element(rpr, "color");
    set(node, "val",
        automatic && color == CharacterStyle{}.color && !theme ? std::string("auto") : hex(color));
    if (!theme)
        return;
    static const char *digits = "0123456789ABCDEF";
    auto level = [](std::int16_t v) { return std::string{digits[v >> 4], digits[v & 15]}; };
    set(node, "themeColor", std::string(themeColorName(theme->index)));
    if (theme->shade >= 0)
        set(node, "themeShade", level(theme->shade));
    if (theme->tint >= 0)
        set(node, "themeTint", level(theme->tint));
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
    const auto theme = activeTheme(value.theme, value.color);
    if (differs(&CharacterStyle::color) ||
        (base && theme != activeTheme(base->theme, base->color))) {
        opaque(value.color);
        writeColor(rpr, value.color, theme, true);
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
std::string jcValue(Alignment alignment) {
    return alignment == Alignment::Center    ? "center"
           : alignment == Alignment::Right   ? "right"
           : alignment == Alignment::Justify ? "both"
                                             : "left";
}
// w:ind in Word's terms. A list paragraph's number sits at indentLeft + indentFirstLine and
// its text 18 pt after indentLeft (see the import).
void writeIndent(pugi::xml_node ind, const ParagraphFormat &value) {
    const bool listed = value.list != ListKind::None;
    const double left = value.indentLeft + (listed ? 18 : 0);
    const double first = value.indentFirstLine - (listed ? 18 : 0);
    set(ind, "left", left * 20);
    if (value.indentRight)
        set(ind, "right", value.indentRight * 20);
    if (first < 0)
        set(ind, "hanging", -first * 20);
    else if (first > 0)
        set(ind, "firstLine", first * 20);
}
void writeTabs(pugi::xml_node ppr, const std::vector<TabStop> &value,
               const std::vector<TabStop> &base = {}) {
    if (value == base)
        return;
    auto tabs = element(ppr, "tabs");
    for (const auto &old : base) {
        if (std::none_of(value.begin(), value.end(),
                         [&](const auto &t) { return t.position == old.position; })) {
            auto tab = element(tabs, "tab");
            set(tab, "val", std::string("clear"));
            set(tab, "pos", old.position * 20);
        }
    }
    for (const auto &t : value) {
        auto tab = element(tabs, "tab");
        set(tab, "val",
            std::string(t.alignment == TabAlignment::Right    ? "right"
                        : t.alignment == TabAlignment::Center ? "center"
                                                              : "left"));
        set(tab, "pos", t.position * 20);
        if (t.leader == TabLeader::Dots)
            set(tab, "leader", std::string("dot"));
    }
}
// Paragraph counterpart of writeCharacter (lists are written on paragraphs only).
void writeParagraph(pugi::xml_node ppr, const ParagraphFormat &value, const ParagraphFormat *base) {
    writeTabs(ppr, value.tabs, base ? base->tabs : std::vector<TabStop>{});
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
    if (differs(&ParagraphFormat::indentLeft) || differs(&ParagraphFormat::indentFirstLine) ||
        differs(&ParagraphFormat::indentRight))
        writeIndent(element(ppr, "ind"), value);
    if (differs(&ParagraphFormat::alignment))
        property(ppr, "jc", jcValue(value.alignment));
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
    // Theme fonts (Word's default body font, Aptos or Calibri, comes from the theme).
    themeFonts = {};
    if (const auto themePath = relationship(parts, main, "theme");
        !themePath.empty() && parts.contains(themePath)) {
        pugi::xml_document theme;
        if (theme.load_buffer(parts.at(themePath).data(), parts.at(themePath).size()))
            for (auto scheme : theme.select_nodes("//*[local-name()='fontScheme']")) {
                auto font = [&](const char *which) {
                    auto latin = scheme.node().select_node(
                        (std::string("*[local-name()='") + which + "']/*[local-name()='latin']")
                            .c_str());
                    std::string name = latin.node().attribute("typeface").value();
                    return name.size() <= 256 ? name : std::string();
                };
                themeFonts = {font("majorFont"), font("minorFont")};
            }
    }
    // Footnote and endnote texts by id, as plain text (paragraphs joined by spaces).
    bool simplifiedNotes = false;
    auto loadNotes = [&](const char *kind) {
    std::map<std::string, std::u16string> footnoteTexts;
    if (const auto notesPath = relationship(parts, main, kind);
        !notesPath.empty() && parts.contains(notesPath)) {
        auto notes = xml(parts.at(notesPath));
        for (auto note : notes.document_element().children()) {
            if (note.type() != pugi::node_element || !attr(note, "type").empty())
                continue;
            std::u16string text;
            int paragraphs = 0;
            std::function<void(pugi::xml_node)> collect = [&](pugi::xml_node node) {
                for (auto item : node.children()) {
                    if (item.type() != pugi::node_element)
                        continue;
                    if (is(item, "p")) {
                        if (paragraphs++ && !text.empty())
                            text += u' ';
                        collect(item);
                    } else if (is(item, "t"))
                        text += utf16(item.text().get());
                    else if (is(item, "tab"))
                        text += u' ';
                    else if (is(item, "drawing") || is(item, "tbl") || is(item, "pict"))
                        simplifiedNotes = true;
                    else if (!is(item, "pPr") && !is(item, "rPr") && !is(item, "footnoteRef") &&
                             !is(item, "endnoteRef"))
                        collect(item);
                }
            };
            collect(note);
            if (paragraphs > 1)
                simplifiedNotes = true;
            for (auto &ch : text)
                if (ch < 32 || ch == 0x7f || ch == 0xfffc)
                    ch = u' ';
            const auto first = text.find_first_not_of(u' ');
            text = first == std::u16string::npos ? std::u16string() : text.substr(first);
            if (text.size() > 2000) {
                text.resize(2000);
                simplifiedNotes = true;
            }
            footnoteTexts[attr(note, "id")] = text;
        }
    }
    return footnoteTexts;
    };
    const auto footnoteTexts = loadNotes("footnotes");
    const auto endnoteTexts = loadNotes("endnotes");
    // Comments by id: author, date and plain text (paragraphs joined by spaces).
    struct ImportedComment {
        std::string author, date;
        std::u16string text;
    };
    std::map<std::string, ImportedComment> commentTexts;
    bool simplifiedComments = false;
    if (const auto commentsPath = relationship(parts, main, "comments");
        !commentsPath.empty() && parts.contains(commentsPath)) {
        auto comments = xml(parts.at(commentsPath));
        for (auto comment : comments.document_element().children()) {
            if (!is(comment, "comment"))
                continue;
            std::u16string text;
            int paragraphs = 0;
            std::function<void(pugi::xml_node)> collect = [&](pugi::xml_node node) {
                for (auto item : node.children()) {
                    if (item.type() != pugi::node_element)
                        continue;
                    if (is(item, "p")) {
                        if (paragraphs++ && !text.empty())
                            text += u' ';
                        collect(item);
                    } else if (is(item, "t"))
                        text += utf16(item.text().get());
                    else if (is(item, "tab"))
                        text += u' ';
                    else if (is(item, "drawing") || is(item, "tbl") || is(item, "pict"))
                        simplifiedComments = true;
                    else if (!is(item, "pPr") && !is(item, "rPr") && !is(item, "annotationRef"))
                        collect(item);
                }
            };
            collect(comment);
            if (paragraphs > 1)
                simplifiedComments = true;
            for (auto &ch : text)
                if (ch < 32 || ch == 0x7f || ch == 0xfffc)
                    ch = u' ';
            if (text.size() > 4000) {
                text.resize(4000);
                simplifiedComments = true;
            }
            auto author = utf16(attr(comment, "author"));
            for (auto &ch : author)
                if (ch < 32)
                    ch = u' ';
            if (author.size() > 80)
                author.resize(80);
            auto date = attr(comment, "date");
            if (date.size() > 64 ||
                std::any_of(date.begin(), date.end(), [](char c) { return c < 0x20 || c > 0x7e; }))
                date.clear();
            commentTexts[attr(comment, "id")] = {utf8(author), date, text};
        }
    }
    struct CommentMark {
        std::string id;
        bool start;
        std::uint64_t paragraph;
        std::size_t offset;
    };
    std::vector<CommentMark> commentMarks;
    // Fields: their range marks join commentMarks under keys that start with "\1f".
    struct OpenField {
        std::string key, instruction;
        bool separated = false;
    };
    std::vector<OpenField> openFields;
    std::map<std::string, std::string> fieldInstructions;
    std::size_t fieldCount = 0;
    if (!body)
        throw std::runtime_error("DOCX document has no body");
    DocxImport result;
    auto &warnings = result.warnings;
    auto &document = result.document;
    // Tracked changes: one Revision per author and date; the marks of the w:ins / w:del
    // being read.
    std::map<std::pair<std::string, std::string>, std::uint64_t> revisionIds;
    std::uint64_t insertedMark = 0, deletedMark = 0;
    // External hyperlink targets by relationship id.
    std::map<std::string, std::string> linkTargets;
    if (const auto found = parts.find(relationshipsPath(main)); found != parts.end()) {
        auto relationships = xml(found->second);
        for (auto node : relationships.document_element().children())
            if (local(node.name()) == "Relationship" &&
                relationshipKind(node.attribute("Type").value()) == "hyperlink")
                linkTargets[node.attribute("Id").value()] = node.attribute("Target").value();
    }
    auto revisionFor = [&](pugi::xml_node change) {
        auto author = attr(change, "author");
        auto date = attr(change, "date");
        try {
            validateCommentAuthor(author);
        } catch (const std::invalid_argument &) {
            author = "Unknown";
        }
        if (date.size() > 64 ||
            std::any_of(date.begin(), date.end(), [](char c) { return c < 0x20 || c > 0x7e; }))
            date.clear();
        const auto found = revisionIds.find({author, date});
        if (found != revisionIds.end())
            return found->second;
        if (document.revisions.size() >= 100000)
            throw std::runtime_error("Too many tracked changes");
        const auto id = document.allocateId();
        document.revisions.push_back({id, author, date});
        revisionIds.emplace(std::pair{author, date}, id);
        return id;
    };
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
        readKeeps(child(child(defaults, "pPrDefault"), "pPr"),
                  document.styles.find("Normal")->paragraph);
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
    // Each list level's w:ind by numId then ilvl (copies kept in `levelIndentStore`).
    pugi::xml_document levelIndentStore;
    std::map<std::string, std::map<std::string, pugi::xml_node>> levelIndents;
    const auto numberingPart = relationship(parts, main, "numbering");
    if (!numberingPart.empty()) {
        if (!parts.contains(numberingPart))
            throw std::runtime_error("Missing DOCX numbering part");
        auto definitions = xml(parts.at(numberingPart));
        std::map<std::string, ListKind> abstracts;
        std::map<std::string, bool> approximate;
        std::map<std::string, std::map<std::string, pugi::xml_node>> abstractIndents;
        // Copies keep the numbering part's namespace declarations.
        auto indentHolder = levelIndentStore.append_child(definitions.document_element().name());
        for (auto attribute : definitions.document_element().attributes())
            if (std::string_view(attribute.name()).starts_with("xmlns"))
                indentHolder.append_attribute(attribute.name()) = attribute.value();
        for (auto node : definitions.document_element().children())
            if (is(node, "abstractNum")) {
                auto level = child(node, "lvl");
                auto format = val(child(level, "numFmt"));
                abstracts[attr(node, "abstractNumId")] = format == "bullet"    ? ListKind::Bullet
                                                         : format == "decimal" ? ListKind::Decimal
                                                                               : ListKind::None;
                const auto expectedLabel = format == "bullet" ? "•" : "%1.";
                // Level indents are kept; other level paragraph options are not.
                bool otherOptions = false;
                for (auto option : child(level, "pPr").children())
                    if (option.type() == pugi::node_element && !is(option, "ind") &&
                        !is(option, "tabs"))
                        otherOptions = true;
                approximate[attr(node, "abstractNumId")] =
                    val(child(level, "start")) != "1" ||
                    val(child(level, "lvlText")) != expectedLabel || otherOptions ||
                    bool(child(level, "rPr"));
                for (auto lvl : node.children())
                    if (is(lvl, "lvl"))
                        if (auto ind = child(child(lvl, "pPr"), "ind"))
                            abstractIndents[attr(node, "abstractNumId")][attr(lvl, "ilvl")] =
                                indentHolder.append_copy(ind);
            }
        for (auto node : definitions.document_element().children())
            if (is(node, "num")) {
                const auto id = attr(node, "numId");
                const auto abstract = val(child(node, "abstractNumId"));
                numbering[id] = abstracts[abstract];
                levelIndents[id] = abstractIndents[abstract];
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
            std::any_of(id.begin(), id.end(),
                        [](char ch) { return static_cast<unsigned char>(ch) < 32; }))
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
            warn(warnings,
                 "A character style could not be kept; its formatting is applied directly.");
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
        if (id.empty() || id.size() > 128 || std::any_of(id.begin(), id.end(), [](char ch) {
                return static_cast<unsigned char>(ch) < 32;
            }))
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
        readKeeps(child(node, "pPr"), style.paragraph);
        style.character = propertiesFor(base.character, character);
        auto trial = document.styles;
        trial.paragraphStyles.push_back(style);
        try {
            validateStyles(trial);
        } catch (const std::exception &) {
            warn(warnings,
                 "A document style could not be kept; its formatting is applied directly.");
            return importedStyles[id] = parent.empty() ? std::string("Normal") : parent;
        }
        document.styles = std::move(trial);
        createdStyles.emplace_back(style.id, node);
        return importedStyles[id] = style.id;
    };
    // Word's own Normal, Title and Heading definitions replace Wordforge's, so paragraphs
    // (and their paragraph marks) keep Word's look when saved again. Normal goes first:
    // the others are defined relative to it.
    for (const auto *id : {"Normal", "Title", "Heading1", "Heading2"}) {
        const auto found = styleNodes.find(id);
        if (found == styleNodes.end() || attr(found->second, "type") != "paragraph")
            continue;
        Paragraph format = defaultParagraph;
        auto character = defaultRun;
        std::set<std::string> seen;
        std::vector<std::string> ignored; // reported once, by the paragraphs using the style
        std::swap(ignored, warnings);
        applyStyle(id, format, character, seen);
        std::swap(ignored, warnings);
        auto trial = document.styles;
        auto *style = trial.find(id);
        const auto base =
            style->basedOn.empty() ? ResolvedStyle{} : resolveStyle(trial, style->basedOn);
        style->paragraph = propertiesFor(base.paragraph, format);
        readKeeps(child(found->second, "pPr"), style->paragraph);
        style->character = propertiesFor(base.character, character);
        try {
            validateStyles(trial);
            document.styles = std::move(trial);
        } catch (const std::exception &) {
            warn(warnings,
                 "A document style could not be kept; its formatting is applied directly.");
        }
    }
    // Inline (and floating, placed inline) pictures: PNG directly, other formats through
    // the converter. Sizes come from the drawing's extent and are fitted to the page later.
    std::size_t pictureBytes = 0;
    auto readPicture = [&](pugi::xml_node drawing,
                           const CharacterStyle &style) -> std::optional<Run> {
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
        return Run::fromImage(
            {id, std::max(1.0, width * scale), std::max(1.0, height * scale), description}, style);
    };
    // A page break that ended a paragraph starts the next body paragraph on a new page.
    bool pendingBreak = false;
    // A section's size, margins and columns; other section settings are reported.
    auto readGeometry = [&](pugi::xml_node section, PageStyle &page) {
        if (auto size = child(section, "pgSz")) {
            page.width = twips(attr(size, "w")) / 20;
            page.height = twips(attr(size, "h")) / 20;
        }
        if (auto margin = child(section, "pgMar")) {
            const auto top = twips(attr(margin, "top")) / 20;
            page.margin = top;
            for (auto side : {"left", "right", "bottom"})
                if (twips(attr(margin, side)) / 20 != top)
                    warn(warnings, "Unequal page margins are approximated by the top margin.");
        }
        page.columns = 1;
        page.columnGap = 36;
        if (auto columns = child(section, "cols")) {
            const auto number = std::atoi(attr(columns, "num").c_str());
            if (number > 1)
                page.columns = static_cast<std::size_t>(std::min(number, 6));
            if (!attr(columns, "space").empty())
                page.columnGap = std::clamp(twips(attr(columns, "space")) / 20, 0.0, 144.0);
            if (number > 6 || attr(columns, "equalWidth") == "0" || attr(columns, "sep") == "1" ||
                attr(columns, "sep") == "true")
                warn(warnings, "Unequal columns and column lines are shown as equal columns.");
        }
        for (auto propertyNode : section.children())
            if (propertyNode.type() == pugi::node_element && !is(propertyNode, "pgSz") &&
                !is(propertyNode, "pgMar") && !is(propertyNode, "headerReference") &&
                !is(propertyNode, "footerReference") && !is(propertyNode, "titlePg") &&
                !is(propertyNode, "cols") && !is(propertyNode, "type"))
                warn(warnings, "Section features are not preserved: " + local(propertyNode.name()));
        const auto type = val(child(section, "type"));
        if (type == "evenPage" || type == "oddPage" || type == "nextColumn")
            warn(warnings, "Even-page, odd-page and next-column section starts begin a new page.");
        try {
            validatePage(page);
        } catch (const std::exception &) {
            warn(warnings, "A section's page settings are invalid; the document's are used.");
            page = document.page;
        }
    };
    // Each section break's paragraph, and how each section starts (w:type), in order.
    std::vector<std::size_t> breakParagraphs;
    std::vector<std::string> sectionStarts;
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
        if (child(properties, "keepNext"))
            paragraph.direct |= KeepWithNextField;
        if (child(properties, "keepLines"))
            paragraph.direct |= KeepLinesField;
        if (cell) {
            paragraph.cell = cell;
            paragraph.pageBreakBefore = false; // not allowed inside a table
        } else if (pendingBreak) {
            paragraph.pageBreakBefore = true;
            pendingBreak = false;
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
                        warn(warnings,
                             "Custom list labels, starts, marker styles or indentation are "
                                   "approximated.");
                if (!previousNum.empty() && previousNum != id)
                    warn(warnings, "Adjacent list restarts may be renumbered.");
            }
            const auto &level = numberLevel;
            if (!level.empty() && level != "0")
                warn(warnings, "Nested lists are shown at a single level.");
            // Word: the paragraph's own indent, else its list level's, else its style's.
            if (paragraph.list != ListKind::None && !child(properties, "ind")) {
                const auto &levels = levelIndents[id];
                    if (auto found = levels.find(level.empty() ? "0" : level);
                        found != levels.end())
                    readIndent(found->second, paragraph, warnings);
            }
            previousNum = id;
        } else
            previousNum.clear();
        if (paragraph.list != ListKind::None) {
            // Word puts the number at left + firstLine and the text at left; Wordforge puts
            // the number at indentLeft + indentFirstLine and the text 18 pt after indentLeft.
            const double numberAt = paragraph.indentLeft + paragraph.indentFirstLine;
            paragraph.indentLeft = std::max(0.0, paragraph.indentLeft - 18);
            paragraph.indentFirstLine = numberAt - paragraph.indentLeft;
        }
        if (paragraph.indentLeft < 0 || paragraph.indentRight < 0) {
            warn(warnings, "Indents into the page margin are shown at the margin.");
            paragraph.indentLeft = std::max(0.0, paragraph.indentLeft);
            paragraph.indentRight = std::max(0.0, paragraph.indentRight);
        }
        if (paragraph.indentFirstLine < -paragraph.indentLeft) {
            warn(warnings, "Hanging indents into the page margin are shown at the margin.");
            paragraph.indentFirstLine = -paragraph.indentLeft;
        }
        paragraph.indentLeft = std::min(paragraph.indentLeft, 360.0);
        paragraph.indentRight = std::min(paragraph.indentRight, 360.0);
            paragraph.indentFirstLine =
                std::clamp(paragraph.indentFirstLine, -paragraph.indentLeft, 360.0);
        std::vector<CommentMark> marks; // offsets in this paragraph before line-break splits
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
                if (is(run, "commentRangeStart") || is(run, "commentRangeEnd")) {
                    std::size_t offset = 0;
                    for (const auto &existing : paragraph.runs)
                        offset += existing.text().size();
                    marks.push_back({attr(run, "id"), is(run, "commentRangeStart"), 0, offset});
                    continue;
                }
                if (is(run, "ins") || is(run, "del") || is(run, "moveTo") ||
                    is(run, "moveFrom")) {
                    const bool insertion = is(run, "ins") || is(run, "moveTo");
                    auto &mark = insertion ? insertedMark : deletedMark;
                    const auto saved = mark;
                    mark = revisionFor(run);
                    readRuns(run, depth + 1);
                    mark = saved;
                    continue;
                }
                if (is(run, "moveFromRangeStart") || is(run, "moveFromRangeEnd") ||
                    is(run, "moveToRangeStart") || is(run, "moveToRangeEnd"))
                    continue;
                if (is(run, "fldSimple")) {
                    const auto key = "\1f" + std::to_string(++fieldCount);
                    auto here = [&] {
                        std::size_t offset = 0;
                        for (const auto &existing : paragraph.runs)
                            offset += existing.text().size();
                        return offset;
                    };
                    marks.push_back({key, true, 0, here()});
                    readRuns(run, depth + 1);
                    marks.push_back({key, false, 0, here()});
                    fieldInstructions[key] = attr(run, "instr");
                    continue;
                }
                if (is(run, "hyperlink")) {
                    std::string target;
                    if (const auto anchor = attr(run, "anchor"); !anchor.empty())
                        target = "#" + anchor;
                        else if (const auto found =
                                     linkTargets.find(attrIn(run, "id", relationshipsNs));
                             found != linkTargets.end())
                        target = found->second;
                    try {
                        validateLink(target);
                    } catch (const std::exception &) {
                        target.clear();
                        warn(warnings, "Some link targets could not be kept; their text is.");
                    }
                    const auto first = paragraph.runs.size();
                    readRuns(run, depth + 1);
                    for (auto i = first; i < paragraph.runs.size(); ++i)
                        if (!paragraph.runs[i].note())
                            paragraph.runs[i].link = target;
                } else if (is(run, "r")) {
                    const auto firstRun = paragraph.runs.size();
                    auto style = base;
                    std::set<std::string> runSeen;
                        applyStyle(val(child(child(run, "rPr"), "rStyle")), paragraph, style,
                                   runSeen);
                    // A footnote reference's own formatting (superscript, its character
                    // style) is Wordforge's to draw, so it is not reported or imported.
                    const bool noteRun =
                        child(run, "footnoteReference") || child(run, "endnoteReference");
                    std::vector<std::string> ignored;
                    runProperties(child(run, "rPr"), style, noteRun ? ignored : warnings);
                    std::u16string text;
                    const auto characterStyle =
                            noteRun
                                ? std::string()
                                : importCharacterStyle(val(child(child(run, "rPr"), "rStyle")), 0);
                    bool pictures = false;
                    for (auto item : run.children()) {
                        if (is(item, "commentReference") || is(item, "annotationRef")) {
                            // The comment mark; its range markers carry the position.
                            } else if (is(item, "footnoteReference") ||
                                       is(item, "endnoteReference")) {
                            pictures = true;
                            const bool endnote = is(item, "endnoteReference");
                            const auto &texts = endnote ? endnoteTexts : footnoteTexts;
                            const auto found = texts.find(attr(item, "id"));
                            if (found == texts.end()) {
                                warn(warnings, "A footnote whose text is missing was omitted.");
                                continue;
                            }
                            if (!text.empty()) {
                                paragraph.runs.push_back({std::move(text), style});
                                paragraph.runs.back().characterStyle = characterStyle;
                                text.clear();
                            }
                            const auto id = document.allocateId();
                            document.footnotes.push_back({id, found->second});
                            paragraph.runs.push_back(Run::fromNote({id, endnote}, style));
                        } else if (is(item, "drawing")) {
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
                        } else if (is(item, "t") || is(item, "delText")) {
                            for (auto chunk : item.children())
                                if (chunk.type() == pugi::node_pcdata ||
                                    chunk.type() == pugi::node_cdata)
                                    text += utf16(chunk.value());
                        } else if (is(item, "tab"))
                            text += u'\t';
                        else if (is(item, "br") && attr(item, "type") == "page")
                            text += u'\f'; // a page break: the rest starts a new page
                        else if (is(item, "br") || is(item, "cr")) {
                            text += u'\n';
                            warn(warnings, "Inline breaks are converted to paragraph breaks; "
                                           "page/column break types are not preserved.");
                        } else if (is(item, "fldChar")) {
                            // begin, instruction text, separate, result, end; fields nest.
                            std::size_t here = text.size();
                            for (const auto &existing : paragraph.runs)
                                here += existing.text().size();
                            const auto type = attr(item, "fldCharType");
                            if (type == "begin")
                                    openFields.push_back(
                                        {"\1f" + std::to_string(++fieldCount), {}});
                            else if (type == "separate" && !openFields.empty() &&
                                     !openFields.back().separated) {
                                openFields.back().separated = true;
                                marks.push_back({openFields.back().key, true, 0, here});
                            } else if (type == "end" && !openFields.empty()) {
                                const auto field = openFields.back();
                                openFields.pop_back();
                                if (!field.separated)
                                    marks.push_back({field.key, true, 0, here});
                                marks.push_back({field.key, false, 0, here});
                                fieldInstructions[field.key] = field.instruction;
                            }
                        } else if (is(item, "instrText")) {
                            if (!openFields.empty() && !openFields.back().separated)
                                openFields.back().instruction += item.text().get();
                        } else if (!is(item, "rPr") && !is(item, "lastRenderedPageBreak"))
                                warn(warnings, "Unsupported inline content was omitted: " +
                                                   local(item.name()));
                    }
                    if (!text.empty() || !pictures) {
                        paragraph.runs.push_back({std::move(text), std::move(style)});
                        paragraph.runs.back().characterStyle = characterStyle;
                    }
                    for (auto i = firstRun; i < paragraph.runs.size(); ++i)
                        if (!paragraph.runs[i].text().empty()) {
                            paragraph.runs[i].inserted = insertedMark;
                            paragraph.runs[i].deleted = deletedMark;
                        }
                } else
                    warn(warnings,
                         "Unsupported paragraph content was omitted: " + local(run.name()));
            }
        };
        readRuns(node, 0);
        Paragraph split = paragraph;
        split.runs.clear();
        // Where each paragraph made from this one starts in its text, for comment marks.
        std::vector<std::pair<std::uint64_t, std::size_t>> segments{{split.id, 0}};
        std::size_t absolute = 0;
        for (const auto &run : paragraph.runs) {
            if (run.atom()) {
                split.runs.push_back(run);
                absolute += 1;
                continue;
            }
            std::size_t start = 0;
            for (;;) {
                const auto end = run.text().find_first_of(u"\n\f", start);
                split.runs.push_back(
                    {run.text().substr(start, end == std::u16string::npos ? end : end - start),
                     run.style});
                split.runs.back().characterStyle = run.characterStyle;
                split.runs.back().link = run.link;
                if (!split.runs.back().text().empty()) {
                    split.runs.back().inserted = run.inserted;
                    split.runs.back().deleted = run.deleted;
                }
                if (end == std::u16string::npos)
                    break;
                const bool pageBreak = !cell && run.text()[end] == u'\f';
                start = end + 1;
                // A page break before any content: this paragraph itself starts the page.
                if (pageBreak && segments.size() == 1 &&
                    std::all_of(split.runs.begin(), split.runs.end(),
                                [](const Run &piece) { return piece.text().empty(); })) {
                    split.pageBreakBefore = true;
                    split.runs.clear();
                    segments.back().second = absolute + start;
                    continue;
                }
                document.paragraphs.push_back(split);
                split.id = document.allocateId();
                split.pageBreakBefore = pageBreak;
                split.runs.clear();
                segments.emplace_back(split.id, absolute + start);
            }
            absolute += run.text().size();
        }
        if (split.runs.empty())
            split.runs.push_back({u"", base});
        document.paragraphs.push_back(std::move(split));
        // A section ends with this paragraph: its own size, margins and columns.
        if (const auto section = child(properties, "sectPr"); section && !cell) {
            SectionBreak end;
            end.page = document.page;
            end.page.header = end.page.footer = {};
            end.page.differentFirstPage = false;
            readGeometry(section, end.page);
            end.page.header = end.page.footer = {};
            end.page.differentFirstPage = false;
            document.paragraphs.back().sectionBreak = end;
            breakParagraphs.push_back(document.paragraphs.size() - 1);
            sectionStarts.push_back(val(child(section, "type")));
        }
        for (auto &mark : marks) {
            auto segment = segments.begin();
            for (auto it = segments.begin(); it != segments.end(); ++it)
                if (it->second <= mark.offset)
                    segment = it;
            mark.paragraph = segment->first;
            // A mark before a leading page break lands at the paragraph's start.
            mark.offset = mark.offset > segment->second ? mark.offset - segment->second : 0;
            commentMarks.push_back(std::move(mark));
        }
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
                        warn(warnings,
                             "Nested tables are flattened into the cell that holds them.");
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
        struct Fill {
            std::size_t row, column;
            std::uint32_t color;
        };
        std::vector<Fill> fills;
        bool cellBorders = false;
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
                    if (cell.type() == pugi::node_element && !is(cell, "trPr") &&
                        !is(cell, "tblPrEx"))
                        warn(warnings,
                             "Unsupported table content was omitted: " + local(cell.name()));
                    continue;
                }
                const auto properties = child(cell, "tcPr");
                if (const auto shading = child(properties, "shd")) {
                    const auto fill = attr(shading, "fill");
                    if (!fill.empty() && fill != "auto") {
                        try {
                            fills.push_back({rows.size(), cells.size(), color(fill)});
                        } catch (const std::runtime_error &) {
                            warn(warnings, "An unreadable cell shading colour was omitted.");
                        }
                    }
                }
                if (child(properties, "tcBorders"))
                    cellBorders = true;
                const auto span = val(child(properties, "gridSpan"));
                const auto count = span.empty() ? 1.0 : std::clamp(number(span), 1.0, 64.0);
                const auto vMerge = child(properties, "vMerge");
                const auto vMergeValue = attr(vMerge, "val");
                spans.push_back({rows.size(), cells.size(), static_cast<std::size_t>(count),
                                 !vMerge                    ? 0
                                 : vMergeValue == "restart" ? 1
                                                            : 2});
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
            warn(warnings,
                 "A table with more than 10 columns or 1000 rows was imported as paragraphs.");
            for (const auto &row : rows)
                for (auto cell : row)
                    if (cell)
                        readCell(cell, std::nullopt, 0);
            return;
        }
        if (cellBorders)
            warn(warnings, "Borders set on single cells are shown like the table's borders.");
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
        // Cell fills: a merged cell takes its top-left cell's (Word repeats it below).
        for (const auto &fill : fills) {
            if (fill.row >= created.rows || fill.column >= created.columns)
                continue;
            if (const auto *merge = created.mergeAt(fill.row, fill.column);
                merge && (merge->row != fill.row || merge->column != fill.column))
                continue;
            if (!created.fillAt(fill.row, fill.column))
                created.shading.push_back({fill.row, fill.column, fill.color});
        }
        // Borders: one width and colour for the whole table, from its own border settings
        // (borders from a table style keep Wordforge's default).
        if (const auto borders = child(child(table, "tblPr"), "tblBorders")) {
            bool any = false, none = true, plain = true;
            for (auto side : borders.children()) {
                if (side.type() != pugi::node_element)
                    continue;
                any = true;
                const auto kind = val(side);
                if (kind == "nil" || kind == "none")
                    continue;
                if (none) {
                    const auto size = attr(side, "sz");
                    created.border.width =
                        size.empty() ? 0.5 : std::clamp(number(size) / 8, 0.25, 6.0);
                    const auto shade = attr(side, "color");
                    if (shade.empty() || shade == "auto")
                        created.border.color = 0xff000000;
                    else
                        try {
                            created.border.color = color(shade);
                        } catch (const std::runtime_error &) {
                        }
                }
                none = false;
                plain = plain && kind == "single";
            }
            if (any && none)
                created.border.width = 0;
            if (!plain)
                warn(warnings, "Table border styles are shown as single lines of one width.");
        }
        // Column widths from the grid, when it matches.
        std::vector<double> widths;
        for (auto column : child(table, "tblGrid").children())
            if (is(column, "gridCol")) {
                const auto w = attr(column, "w");
                widths.push_back(w.empty() ? 0 : twips(w) / 20);
            }
        if (widths.size() == columns && std::all_of(widths.begin(), widths.end(), [](double w) {
                return std::isfinite(w) && w > 0 && w <= 2880;
            }))
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
            auto page = document.page;
            readGeometry(node, page);
            document.page.width = page.width;
            document.page.height = page.height;
            document.page.margin = page.margin;
            document.page.columns = page.columns;
            document.page.columnGap = page.columnGap;
            readBands(node);
            sectionStarts.push_back(val(child(node, "type")));
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
    // A section break is continuous when the section after it starts that way (Word records
    // how each section starts in that section's own w:type).
    for (std::size_t k = 0; k < breakParagraphs.size(); ++k)
        document.paragraphs[breakParagraphs[k]].sectionBreak->continuous =
            k + 1 < sectionStarts.size() && sectionStarts[k + 1] == "continuous";
    // The last section has no break; a break on the last paragraph would end an empty one.
    if (!document.paragraphs.empty() && document.paragraphs.back().sectionBreak &&
        breakParagraphs.size() + 1 > sectionStarts.size())
        document.paragraphs.back().sectionBreak.reset();
    // A table must be followed by a body paragraph.
    if (!document.paragraphs.empty() && document.paragraphs.back().cell)
        document.paragraphs.push_back(emptyParagraph(std::nullopt));
    if (document.paragraphs.empty())
        document.paragraphs.emplace_back(document.allocateId());
    // Indents that leave less than 54 pt to write in (a narrow table column, say) would not
    // lay out: shrink them in proportion.
    {
        const double content = document.page.width - 2 * document.page.margin;
        std::map<std::uint64_t, std::size_t> columns;
        for (const auto &table : document.tables)
            columns[table.id] = table.columns;
        bool shrunk = false;
        for (auto &paragraph : document.paragraphs) {
            const double available =
                paragraph.cell
                    ? content / static_cast<double>(columns.at(paragraph.cell->tableId)) - 8
                               : content;
            const double marker = paragraph.list != ListKind::None ? 18 : 0;
            const double used = paragraph.indentLeft + std::max(0.0, paragraph.indentFirstLine) +
                                paragraph.indentRight + marker;
            const double room = std::max(0.0, available - 54 - marker);
            if (used - marker > room && used > marker) {
                const double scale = room / (used - marker);
                paragraph.indentLeft *= scale;
                paragraph.indentFirstLine *= scale;
                paragraph.indentRight *= scale;
                shrunk = true;
            }
        }
        if (shrunk)
            warn(warnings, "Indents too wide for their page or table column were narrowed.");
    }
    // Pictures larger than their column or the page would not lay out: scale them down.
    if (!document.images.empty()) {
        const double content = document.page.width - 2 * document.page.margin;
        std::map<std::uint64_t, std::size_t> tableColumns;
        for (const auto &table : document.tables)
            tableColumns[table.id] = table.columns;
        for (auto &paragraph : document.paragraphs) {
            double available =
                paragraph.cell ? content / tableColumns.at(paragraph.cell->tableId) - 8 : content;
            available -= paragraph.indentLeft + std::max(0.0, paragraph.indentFirstLine) +
                         paragraph.indentRight + (paragraph.list != ListKind::None ? 40 : 0) + 1;
            double tallest = 0;
            for (const auto &run : paragraph.runs)
                tallest = std::max(tallest, run.style.size);
            const double maxHeight = (document.page.height - 2 * document.page.margin) /
                                         std::max(1.0, paragraph.lineSpacing) -
                                     tallest - 1;
            for (auto &run : paragraph.runs)
                if (const auto *image = run.image()) {
                    const auto scale =
                        std::min({1.0, available / image->width, maxHeight / image->height});
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
        const auto target = isBuiltinStyle(next)            ? next
                            : importedStyles.contains(next) ? importedStyles.at(next)
                                                              : std::string();
        if (!target.empty() && target != id)
            document.styles.find(id)->next = target;
    }
    adoptStyles(document); // DOCX values arrive resolved; mark those differing from styles direct
    if (simplifiedNotes)
        warn(warnings, "Footnotes are kept as one paragraph of plain text each.");
    if (!commentTexts.empty()) {
        std::map<std::string, std::pair<std::optional<Anchor>, std::optional<Anchor>>> ranges;
        for (const auto &mark : commentMarks) {
            auto &range = ranges[mark.id];
            auto &slot = mark.start ? range.first : range.second;
            if (!slot)
                slot = Anchor{mark.paragraph, mark.offset,
                              mark.start ? Affinity::Before : Affinity::After};
        }
        // Offsets on grapheme boundaries.
        auto snap = [&](Anchor anchor) {
            for (const auto &p : document.paragraphs)
                if (p.id == anchor.paragraphId) {
                    const auto boundaries = p.boundaries();
                    const auto found =
                        std::lower_bound(boundaries.begin(), boundaries.end(), anchor.offset);
                    anchor.offset = found == boundaries.end() ? boundaries.back() : *found;
                }
            return anchor;
        };
        bool unanchored = false;
        for (const auto &[id, comment] : commentTexts) {
            const auto found = ranges.find(id);
            if (found == ranges.end() || (!found->second.first && !found->second.second) ||
                document.comments.size() >= 10000) {
                unanchored = true;
                continue;
            }
            auto start = snap(found->second.first ? *found->second.first : *found->second.second);
            auto end = snap(found->second.second ? *found->second.second : start);
            start.affinity = Affinity::Before;
            end.affinity = Affinity::After;
            const auto from = document.resolve(start), to = document.resolve(end);
            if (!from || !to) {
                unanchored = true;
                continue;
            }
            if (*to < *from)
                end = {start.paragraphId, start.offset, Affinity::After};
            document.comments.push_back(
                {document.allocateId(), comment.author, comment.date, comment.text, start, end});
        }
        if (unanchored)
            warn(warnings, "Comments without a marked text range were omitted.");
        if (simplifiedComments)
            warn(warnings, "Comments are kept as one paragraph of plain text each.");
    }
    // Fields from their range marks, kept only when every one nests properly.
    if (!fieldInstructions.empty()) {
        std::map<std::string, std::pair<std::optional<Anchor>, std::optional<Anchor>>> ranges;
        for (const auto &mark : commentMarks)
            if (mark.id.starts_with("\1f")) {
                auto &range = ranges[mark.id];
                auto &slot = mark.start ? range.first : range.second;
                if (!slot)
                    slot = Anchor{mark.paragraph, mark.offset,
                                  mark.start ? Affinity::Before : Affinity::After};
            }
        auto snap = [&](Anchor anchor) {
            for (const auto &p : document.paragraphs)
                if (p.id == anchor.paragraphId) {
                    const auto boundaries = p.boundaries();
                    const auto found =
                        std::lower_bound(boundaries.begin(), boundaries.end(), anchor.offset);
                    anchor.offset = found == boundaries.end() ? boundaries.back() : *found;
                }
            return anchor;
        };
        bool dropped = false;
        for (std::size_t n = 1; n <= fieldCount; ++n) {
            const auto key = "\1f" + std::to_string(n);
            const auto found = ranges.find(key);
            const auto instruction = fieldInstructions.find(key);
            if (found == ranges.end() || !found->second.first || !found->second.second ||
                instruction == fieldInstructions.end())
                continue;
            auto text = instruction->second;
            text.erase(0, text.find_first_not_of(' '));
            text.erase(text.find_last_not_of(' ') + 1);
            try {
                validateFieldInstruction(text);
            } catch (const std::exception &) {
                dropped = true;
                continue;
            }
            const auto start = snap(*found->second.first), end = snap(*found->second.second);
            const auto from = document.resolve(start), to = document.resolve(end);
            if (!from || !to || *to < *from) {
                dropped = true;
                continue;
            }
            document.fields.push_back({document.allocateId(), text, start, end});
        }
        try {
            validateDocument(document);
        } catch (const std::exception &) {
            document.fields.clear();
            dropped = true;
        }
        if (dropped)
            warn(warnings, "Some fields were kept as plain text.");
    }
    for (std::size_t i = 0; i + 1 < document.paragraphs.size(); ++i) {
        const auto &p = document.paragraphs[i], &next = document.paragraphs[i + 1];
        if (p.keepWithNext && p.cell != next.cell && (p.cell || next.cell))
            warn(warnings, "Keep with next across table boundaries is preserved, but not laid out.");
    }
    document.preserved = preservePackage(parts, main, warnings);
    // Read settings that affect editing; retain the original package bytes.
    if (document.preserved)
        for (const auto &part : document.preserved->parts)
            if (part.contentType ==
                "application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml") {
                pugi::xml_document settings;
                if (settings.load_buffer(part.data.data(), part.data.size()))
                    for (auto node : settings.document_element().children())
                        if (is(node, "trackRevisions"))
                            document.trackChanges = attr(node, "val") != "false" &&
                                                    attr(node, "val") != "0" &&
                                                    attr(node, "val") != "off";
                        else if (is(node, "defaultTabStop") && twips(attr(node, "val")) != 720)
                            warn(warnings, "The custom default tab interval is preserved, but "
                                           "Wordforge displays automatic tabs every 36 points.");
            }
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
    unsigned long long changeId = 0; // w:ins / w:del identifiers
    std::set<std::uint64_t> imageIds;
    std::map<std::string, std::string> linkIds; // external link target -> relationship id
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
        auto indent = element(element(level, "pPr"), "ind");
        set(indent, "left", 360);
        set(indent, "hanging", 360);
    }
    ListKind previous = ListKind::None;
    int numId = 0;
    std::map<std::uint64_t, const Table *> tables;
    for (const auto &table : document.tables)
        tables.emplace(table.id, &table);
    std::vector<std::uint64_t> footnoteOrder; // footnote ids in reference order
    std::vector<std::uint64_t> endnoteOrder;
    // Comment range markers by paragraph index: (offset, end?, exported id).
    struct Marker {
        std::size_t offset;
        bool end;
        std::size_t id; // a comment's number, or a field's index in `exportedFields`
        bool field = false;
        bool empty = false; // a field with no result: written whole at its start
    };
    std::vector<const Field *> exportedFields; // in nesting order: outer before inner
    std::vector<pugi::xml_node> fieldParents;  // where each field's begin was written
    std::map<std::size_t, std::vector<Marker>> markers;
    std::vector<const Comment *> exportedComments;
    {
        // Empty covered cells of merged table cells are not written; neither are comments
        // anchored there.
        auto written = [&](std::size_t index) {
            const auto &p = document.paragraphs[index];
            if (!p.cell || !p.text().empty())
                return true;
            for (const auto &table : document.tables)
                if (table.id == p.cell->tableId)
                    if (const auto *merge = table.mergeAt(p.cell->row, p.cell->column))
                        return merge->row == p.cell->row && merge->column == p.cell->column;
            return true;
        };
        for (const auto &comment : document.comments) {
            const auto start = document.resolve(comment.start), end = document.resolve(comment.end);
            if (!start || !end || !written(start->paragraph) || !written(end->paragraph))
                continue;
            const auto id = exportedComments.size();
            exportedComments.push_back(&comment);
            markers[start->paragraph].push_back({start->offset, false, id});
            markers[end->paragraph].push_back({end->offset, true, id});
        }
        // Fields: begin, instruction and separator at the start of the result, end after it.
        std::vector<std::tuple<Position, Position, const Field *>> fields;
        for (const auto &field : document.fields) {
            const auto start = document.resolve(field.start), end = document.resolve(field.end);
            if (start && end && written(start->paragraph) && written(end->paragraph))
                fields.emplace_back(*start, *end, &field);
        }
        std::stable_sort(fields.begin(), fields.end(), [](const auto &a, const auto &b) {
            return std::get<0>(a) < std::get<0>(b) ||
                   (std::get<0>(a) == std::get<0>(b) && std::get<1>(b) < std::get<1>(a));
        });
        for (const auto &[start, end, field] : fields) {
            const auto id = exportedFields.size();
            exportedFields.push_back(field);
            fieldParents.emplace_back();
            const bool empty = start == end;
            markers[start.paragraph].push_back({start.offset, false, id, true, empty});
            if (!empty)
                markers[end.paragraph].push_back({end.offset, true, id, true});
        }
        // At one offset: field ends (inner first), comment starts, field starts (outer
        // first), comment ends.
        for (auto &[index, list] : markers)
            std::stable_sort(list.begin(), list.end(), [](const Marker &a, const Marker &b) {
                if (a.offset != b.offset)
                    return a.offset < b.offset;
                auto phase = [](const Marker &m) {
                    return m.field ? (m.end ? 0 : 2) : (m.end ? 3 : 1);
                };
                if (phase(a) != phase(b))
                    return phase(a) < phase(b);
                return a.field && (a.end ? a.id > b.id : a.id < b.id);
            });
    }
    // A section's sectPr: header/footer references (paragraph sections name the parts the
    // final section creates), how it starts, size, margins, columns and first-page setting.
    bool previousContinuous = false; // how the section being written starts
    bool wroteSection = false;
    auto writeSection = [&](pugi::xml_node section, const PageStyle &page, bool continuous,
                            bool references) {
        if (references && (!document.page.header.empty() || !document.page.footer.empty())) {
            for (const bool header : {true, false}) {
                auto node = element(section, header ? "headerReference" : "footerReference");
                set(node, "type", std::string("default"));
                node.append_attribute("r:id") = header ? "header" : "footer";
            }
            if (document.page.differentFirstPage)
                for (const bool header : {true, false}) {
                    auto node = element(section, header ? "headerReference" : "footerReference");
                    set(node, "type", std::string("first"));
                    node.append_attribute("r:id") = header ? "headerFirst" : "footerFirst";
                }
        }
        if (continuous)
            property(section, "type", std::string("continuous"));
        auto size = element(section, "pgSz");
        set(size, "w", page.width * 20);
        set(size, "h", page.height * 20);
        if (page.width > page.height)
            set(size, "orient", std::string("landscape"));
        auto margin = element(section, "pgMar");
        for (auto side : {"top", "bottom", "left", "right"})
            set(margin, side, page.margin * 20);
        for (auto side : {"header", "footer"}) // where Wordforge draws them: mid-margin
            set(margin, side, page.margin * 10);
        if (page.columns > 1) {
            auto columns = element(section, "cols");
            set(columns, "num", static_cast<double>(page.columns));
            set(columns, "space", page.columnGap * 20);
        }
        // Wordforge leaves out the header and footer on the document's first page only,
        // which in Word is the first section's title page.
        if (document.page.differentFirstPage && !wroteSection)
            element(section, "titlePg");
        wroteSection = true;
    };
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
                    if (table.border.width <= 0) {
                        set(border, "val", std::string("nil"));
                        continue;
                    }
                    set(border, "val", std::string("single"));
                    // Eighths of a point; Word accepts 2 to 96.
                    set(border, "sz", std::clamp(std::round(table.border.width * 8), 2.0, 96.0));
                    set(border, "color", hex(table.border.color));
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
                    if (const auto fill = merge ? table.fillAt(merge->row, merge->column)
                                                : table.fillAt(cell.row, cell.column)) {
                        auto shading = element(properties, "shd");
                        set(shading, "val", std::string("clear"));
                        set(shading, "color", std::string("auto"));
                        set(shading, "fill", hex(fill));
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
        if (paragraph.direct & KeepWithNextField)
            property(ppr, "keepNext", paragraph.keepWithNext ? 1.0 : 0.0);
        if (paragraph.direct & KeepLinesField)
            property(ppr, "keepLines", paragraph.keepLines ? 1.0 : 0.0);
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
        writeTabs(ppr, paragraph.tabs,
                  resolveStyle(document.styles, paragraph.style).paragraph.tabs);
        writeIndent(element(ppr, "ind"), paragraph);
        property(ppr, "jc", jcValue(paragraph.alignment));
        if (const auto &end = paragraph.sectionBreak) {
            writeSection(element(ppr, "sectPr"), end->page, previousContinuous, true);
            previousContinuous = end->continuous;
        }
        // Runs split where comment ranges start or end, with the markers between them.
        const auto paragraphIndex =
            static_cast<std::size_t>(&paragraph - document.paragraphs.data());
        const auto paragraphMarkers = markers.find(paragraphIndex);
        std::vector<std::pair<std::size_t, Run>> pieces;
        {
            std::size_t offset = 0;
            for (const auto &run : paragraph.runs) {
                const auto size = run.text().size();
                std::size_t from = 0;
                if (!run.atom() && paragraphMarkers != markers.end())
                    for (const auto &marker : paragraphMarkers->second)
                        if (marker.offset > offset + from && marker.offset < offset + size) {
                            pieces.emplace_back(offset + from,
                                                run.slice(from, marker.offset - offset));
                            from = marker.offset - offset;
                        }
                pieces.emplace_back(offset + from, from ? run.slice(from, size) : run);
                offset += size;
            }
        }
        std::size_t nextMarker = 0;
        pugi::xml_node hyperlink; // the open w:hyperlink, while runs continue its target
        std::string hyperlinkTarget;
        // Field characters go inside an open link when the link continues past them.
        pugi::xml_node fieldTarget;
        auto fieldChar = [&](const char *type) {
            set(element(element(fieldTarget, "r"), "fldChar"), "fldCharType", std::string(type));
        };
        auto emitMarkers = [&](std::size_t upTo) {
            if (paragraphMarkers == markers.end())
                return;
            const auto &list = paragraphMarkers->second;
            while (nextMarker < list.size() && list[nextMarker].offset <= upTo) {
                const auto &marker = list[nextMarker++];
                if (marker.field) {
                    if (marker.end) {
                        // Beside its begin: inside the link only if that one is still open.
                        const auto saved = fieldTarget;
                        fieldTarget = hyperlink && fieldParents[marker.id] == hyperlink &&
                                              p.last_child() == hyperlink
                                          ? hyperlink
                                          : p;
                        fieldChar("end");
                        fieldTarget = saved;
                        continue;
                    }
                    fieldParents[marker.id] = fieldTarget;
                    fieldChar("begin");
                    auto instruction = element(element(fieldTarget, "r"), "instrText");
                    instruction.append_attribute("xml:space") = "preserve";
                    instruction.text().set(
                        (" " + exportedFields[marker.id]->instruction + " ").c_str());
                    fieldChar("separate");
                    if (marker.empty)
                        fieldChar("end");
                    continue;
                }
                set(element(p, marker.end ? "commentRangeEnd" : "commentRangeStart"), "id",
                    static_cast<double>(marker.id));
                if (marker.end)
                    set(element(element(p, "r"), "commentReference"), "id",
                        static_cast<double>(marker.id));
            }
        };
        for (const auto &[pieceOffset, run] : pieces) {
            fieldTarget = hyperlink && p.last_child() == hyperlink && run.link == hyperlinkTarget
                              ? hyperlink
                              : p;
            emitMarkers(pieceOffset);
            // Links wrap runs: w:hyperlink, then tracked changes (w:ins, then w:del inside).
            auto container = p;
            if (!run.link.empty()) {
                if (!hyperlink || p.last_child() != hyperlink || hyperlinkTarget != run.link) {
                    hyperlink = element(p, "hyperlink");
                    hyperlinkTarget = run.link;
                    if (run.link[0] == '#')
                        set(hyperlink, "anchor", run.link.substr(1));
                    else {
                        auto &id = linkIds[run.link];
                        if (id.empty())
                            id = "link" + std::to_string(linkIds.size());
                        hyperlink.append_attribute("r:id") = id.c_str();
                    }
                    set(hyperlink, "history", std::string("1"));
                }
                container = hyperlink;
            } else
                hyperlink = {};
            for (const auto &[mark, name] :
                 {std::pair{run.inserted, "ins"}, std::pair{run.deleted, "del"}}) {
                if (!mark)
                    continue;
                container = element(container, name);
                set(container, "id", static_cast<double>(++changeId));
                if (const auto *revision = document.findRevision(mark)) {
                    set(container, "author", revision->author);
                    if (!revision->date.empty())
                        set(container, "date", revision->date);
                }
            }
            auto r = element(container, "r");
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
            // Wordforge's default ink is Word's automatic text colour.
            writeColor(rpr, run.style.color, activeTheme(run.style.theme, run.style.color), true);
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
            if (const auto *note = run.note()) {
                property(rpr, "vertAlign", std::string("superscript"));
                auto &order = note->endnote ? endnoteOrder : footnoteOrder;
                order.push_back(note->id);
                set(element(r, note->endnote ? "endnoteReference" : "footnoteReference"), "id",
                    static_cast<double>(order.size()));
                continue;
            }
            std::size_t begin = 0;
            for (;;) {
                auto end = run.text().find(u'\t', begin);
                auto t = element(r, run.deleted ? "delText" : "t");
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
        fieldTarget = p;
        emitMarkers(std::numeric_limits<std::size_t>::max());
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
    writeSection(section, document.page, previousContinuous, false);
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
        auto ppr = element(style, "pPr");
        writeParagraph(ppr, self.paragraph, &base.paragraph);
        if (definition.paragraph.keepWithNext)
            property(ppr, "keepNext", *definition.paragraph.keepWithNext ? 1.0 : 0.0);
        if (definition.paragraph.keepLines)
            property(ppr, "keepLines", *definition.paragraph.keepLines ? 1.0 : 0.0);
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
            writeColor(rpr, *c.color, c.theme ? activeTheme(*c.theme, *c.color) : std::nullopt,
                       false);
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
    if (!linkIds.empty()) {
        pugi::xml_document relationships;
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        for (const auto &[target, id] : linkIds) {
            auto relation = relationships.document_element().append_child("Relationship");
            relation.append_attribute("Id") = id.c_str();
            relation.append_attribute("Type") =
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
            relation.append_attribute("Target") = target.c_str();
            relation.append_attribute("TargetMode") = "External";
        }
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
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
                header
                    ? "application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"
                       : "application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml";
            auto relation = relationships.document_element().append_child("Relationship");
            relation.append_attribute("Id") = name.c_str();
            relation.append_attribute("Type") =
                header
                    ? "http://schemas.openxmlformats.org/officeDocument/2006/relationships/header"
                       : "http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer";
            relation.append_attribute("Target") = (name + ".xml").c_str();
            parts.emplace("word/" + name + ".xml", std::move(xmlText));
        }
        parts["[Content_Types].xml"] = saved(types);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
    if (!exportedComments.empty()) {
        pugi::xml_document comments;
        auto root = comments.append_child("w:comments");
        root.append_attribute("xmlns:w") = wordNs;
        for (std::size_t index = 0; index < exportedComments.size(); ++index) {
            const auto &comment = *exportedComments[index];
            auto node = element(root, "comment");
            set(node, "id", static_cast<double>(index));
            set(node, "author", comment.author.empty() ? std::string("Author") : comment.author);
            if (!comment.date.empty())
                set(node, "date", comment.date);
            std::string initials;
            for (std::size_t i = 0; i < comment.author.size(); ++i)
                if ((i == 0 || comment.author[i - 1] == ' ') &&
                    std::isalpha(static_cast<unsigned char>(comment.author[i])))
                    initials += comment.author[i];
            if (!initials.empty())
                set(node, "initials", initials);
            auto p = element(node, "p");
            element(element(p, "r"), "annotationRef");
            auto t = element(element(p, "r"), "t");
            t.append_attribute("xml:space") = "preserve";
            t.text().set(utf8(comment.text).c_str());
        }
        pugi::xml_document types, relationships;
        types.load_string(parts.at("[Content_Types].xml").c_str());
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        auto entry = types.document_element().append_child("Override");
        entry.append_attribute("PartName") = "/word/comments.xml";
        entry.append_attribute("ContentType") =
            "application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml";
        auto relation = relationships.document_element().append_child("Relationship");
        relation.append_attribute("Id") = "comments";
        relation.append_attribute("Type") =
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
        relation.append_attribute("Target") = "comments.xml";
        parts.emplace("word/comments.xml", saved(comments));
        parts["[Content_Types].xml"] = saved(types);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
    // footnotes.xml or endnotes.xml: Word's separator notes, then one note per reference.
    auto writeNotes = [&](const std::vector<std::uint64_t> &order, const std::string &kind) {
        if (order.empty())
            return;
        const auto item = kind.substr(0, kind.size() - 1); // "footnote" or "endnote"
        pugi::xml_document notes;
        auto root = notes.append_child(("w:" + kind).c_str());
        root.append_attribute("xmlns:w") = wordNs;
        // The separator notes Word expects, then one note per reference.
        for (auto [id, type, mark] :
             {std::tuple{-1, "separator", "separator"},
              std::tuple{0, "continuationSeparator", "continuationSeparator"}}) {
            auto note = element(root, item.c_str());
            set(note, "type", std::string(type));
            set(note, "id", static_cast<double>(id));
            auto p = element(note, "p");
            set(element(element(p, "pPr"), "spacing"), "after", 0.0);
            element(element(p, "r"), mark);
        }
        for (std::size_t index = 0; index < order.size(); ++index) {
            auto note = element(root, item.c_str());
            set(note, "id", static_cast<double>(index + 1));
            auto p = element(note, "p");
            set(element(element(p, "pPr"), "spacing"), "after", 0.0);
            auto mark = element(p, "r");
            auto markProperties = element(mark, "rPr");
            property(markProperties, "sz", 20.0);
            property(markProperties, "vertAlign", std::string("superscript"));
            element(mark, (item + "Ref").c_str());
            auto text = element(p, "r");
            property(element(text, "rPr"), "sz", 20.0);
            auto t = element(text, "t");
            t.append_attribute("xml:space") = "preserve";
            t.text().set(utf8(u" " + document.footnote(order[index])->text).c_str());
        }
        pugi::xml_document types, relationships;
        types.load_string(parts.at("[Content_Types].xml").c_str());
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        auto entry = types.document_element().append_child("Override");
        entry.append_attribute("PartName") = ("/word/" + kind + ".xml").c_str();
        entry.append_attribute("ContentType") =
            ("application/vnd.openxmlformats-officedocument.wordprocessingml." + kind + "+xml")
                .c_str();
        auto relation = relationships.document_element().append_child("Relationship");
        relation.append_attribute("Id") = kind.c_str();
        relation.append_attribute("Type") =
            ("http://schemas.openxmlformats.org/officeDocument/2006/relationships/" + kind).c_str();
        relation.append_attribute("Target") = (kind + ".xml").c_str();
        parts.emplace("word/" + kind + ".xml", saved(notes));
        parts["[Content_Types].xml"] = saved(types);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    };
    writeNotes(footnoteOrder, "footnotes");
    writeNotes(endnoteOrder, "endnotes");
    const bool keptSettings =
        document.preserved &&
        std::any_of(document.preserved->parts.begin(), document.preserved->parts.end(),
                    [](const PreservedPart &part) {
                        return part.contentType == "application/vnd.openxmlformats-"
                                                   "officedocument.wordprocessingml.settings+xml";
                    });
    if (document.trackChanges && !keptSettings) {
        pugi::xml_document settings, types, relationships;
        auto root = settings.append_child("w:settings");
        root.append_attribute("xmlns:w") = wordNs;
        element(root, "trackRevisions");
        types.load_string(parts.at("[Content_Types].xml").c_str());
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        auto entry = types.document_element().append_child("Override");
        entry.append_attribute("PartName") = "/word/settings.xml";
        entry.append_attribute("ContentType") =
            "application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml";
        auto relation = relationships.document_element().append_child("Relationship");
        relation.append_attribute("Id") = "settings";
        relation.append_attribute("Type") =
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings";
        relation.append_attribute("Target") = "settings.xml";
        parts.emplace("word/settings.xml", saved(settings));
        parts["[Content_Types].xml"] = saved(types);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
    if (document.preserved) {
        pugi::xml_document types, packageRelationships, relationships;
        types.load_string(parts.at("[Content_Types].xml").c_str());
        packageRelationships.load_string(parts.at("_rels/.rels").c_str());
        relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str());
        for (const auto &part : document.preserved->parts) {
            const bool settings =
                part.contentType ==
                "application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml";
            if (!parts
                     .emplace(part.path, settings
                                             ? withTrackRevisions(part.data, document.trackChanges)
                                                  : part.data)
                     .second)
                throw std::runtime_error("A kept document part collides with generated content");
            if (part.contentType.empty())
                continue;
            auto entry = types.document_element().append_child("Override");
            entry.append_attribute("PartName") = ("/" + part.path).c_str();
            entry.append_attribute("ContentType") = part.contentType.c_str();
        }
        int number = 0;
        for (const auto &relation : document.preserved->relationships) {
            auto node = (relation.package ? packageRelationships : relationships)
                            .document_element()
                            .append_child("Relationship");
            node.append_attribute("Id") = ("kept" + std::to_string(++number)).c_str();
            node.append_attribute("Type") = relation.type.c_str();
            const auto target = relation.package                    ? relation.target
                                : relation.target.starts_with("word/") ? relation.target.substr(5)
                                                                       : "../" + relation.target;
            node.append_attribute("Target") = target.c_str();
        }
        parts["[Content_Types].xml"] = saved(types);
        parts["_rels/.rels"] = saved(packageRelationships);
        parts["word/_rels/document.xml.rels"] = saved(relationships);
    }
    return opc::write(parts);
}
} // namespace wf
