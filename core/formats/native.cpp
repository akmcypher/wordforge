#include "core/formats/native.hpp"
#include "core/formats/zip_package.hpp"
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
    if (p.spaceBefore)
        node.append_attribute("before") = *p.spaceBefore;
    if (p.spaceAfter)
        node.append_attribute("after") = *p.spaceAfter;
    if (p.lineSpacing)
        node.append_attribute("line") = *p.lineSpacing;
    const auto &c = style.character;
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
ParagraphStyle readStyle(pugi::xml_node node) {
    attributes(node, {"id", "name", "basedOn", "next", "align", "list", "indent", "before", "after",
                      "line", "family", "size", "bold", "italic", "underline", "strike", "color",
                      "highlight"});
    for (auto child : node.children())
        if (!indentation(child))
            throw std::runtime_error("Unexpected style content");
    ParagraphStyle style;
    style.id = node.attribute("id").value();
    style.name = node.attribute("name").value();
    style.basedOn = node.attribute("basedOn").value();
    style.next = node.attribute("next").value();
    style.paragraph.alignment = whole<Alignment>(node, "align", 2);
    style.paragraph.list = whole<ListKind>(node, "list", 2);
    style.paragraph.indentLeft = real(node, "indent", 0, 360);
    style.paragraph.spaceBefore = real(node, "before", 0, 200);
    style.paragraph.spaceAfter = real(node, "after", 0, 200);
    style.paragraph.lineSpacing = real(node, "line", 1, 3);
    if (node.attribute("family"))
        style.character.family = node.attribute("family").value();
    style.character.size = real(node, "size", 1, 200);
    style.character.bold = flag(node, "bold");
    style.character.italic = flag(node, "italic");
    style.character.underline = flag(node, "underline");
    style.character.strike = flag(node, "strike");
    style.character.color = whole<std::uint32_t>(node, "color", 4294967295.0);
    style.character.highlight = whole<std::uint32_t>(node, "highlight", 4294967295.0);
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
    const bool packaged = tables || !imageIds.empty();
    // WFG6 only when styles were customized; built-in styles keep files readable by 0.6.0.
    const bool styled = document.styles != builtinStyles();
    root.append_attribute("version") = styled                       ? "6"
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
    if (styled)
        for (const auto &style : document.styles.paragraphStyles)
            writeStyle(root, style);
    for (const auto &table : document.tables) {
        auto node = root.append_child("table");
        node.append_attribute("id") = static_cast<unsigned long long>(table.id);
        node.append_attribute("rows") = static_cast<unsigned long long>(table.rows);
        node.append_attribute("columns") = static_cast<unsigned long long>(table.columns);
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
        if (styled) {
            if (p.style != "Normal")
                paragraph.append_attribute("style") = p.style.c_str();
            if (p.direct)
                paragraph.append_attribute("direct") = p.direct;
        } else if (const auto preset = presetFor(p.style); preset && *preset != ParagraphPreset::Normal)
            paragraph.append_attribute("preset") = static_cast<int>(*preset);
        if (p.indentLeft)
            paragraph.append_attribute("indent") = p.indentLeft;
        if (p.spaceBefore)
            paragraph.append_attribute("before") = p.spaceBefore;
        if (p.lineSpacing != 1.15)
            paragraph.append_attribute("line") = p.lineSpacing;
        if (p.pageBreakBefore)
            paragraph.append_attribute("break") = true;
        for (const auto &r : p.runs) {
            const auto *image = r.image();
            auto run = paragraph.append_child(image ? "image" : "r");
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
            if (r.style.strike)
                run.append_attribute("strike") = true;
            if (r.style.highlight)
                run.append_attribute("highlight") =
                    static_cast<unsigned long long>(r.style.highlight);
            if (styled && r.direct)
                run.append_attribute("direct") = r.direct;
            if (!image)
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
         version != "6"))
        throw std::runtime_error("Unsupported native document version");
    const bool v6 = version == "6";
    if (!v6 && packaged != (version == "4" || version == "5"))
        throw std::runtime_error("WFG4/5 require ZIP transport; legacy versions require XML");
    if (version == "1")
        attributes(root, {"version", "width", "height", "margin"});
    else
        attributes(root, {"version", "width", "height", "margin", "nextId"});
    Document document;
    document.paragraphs.clear();
    document.nextId = 1;
    if (v6)
        document.styles.paragraphStyles.clear();
    document.page = {number(root, "width", 595.28, 72, 2880),
                     number(root, "height", 841.89, 72, 2880),
                     number(root, "margin", 56.7, 0, 1400)};
    pageGeometry(document.page);
    for (auto p : root.children()) {
        if (indentation(p))
            continue;
        if (v6 && std::string_view(p.name()) == "style") {
            document.styles.paragraphStyles.push_back(readStyle(p));
            continue;
        }
        if ((version == "5" || v6) && std::string_view(p.name()) == "table") {
            attributes(p, {"id", "rows", "columns"});
            for (auto nested : p.children())
                if (!indentation(nested))
                    throw std::runtime_error("Unexpected native table metadata content");
            document.tables.push_back({identifier(p, "id"), index(p, "rows"), index(p, "columns")});
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
        auto alignment = number(p, "align", 0, 0, 2);
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
        paragraph.spaceBefore = number(p, "before", 0, 0, 200);
        paragraph.lineSpacing = number(p, "line", 1.15, 1, 3);
        paragraph.pageBreakBefore = boolean(p, "break");
        paragraph.runs.clear();
        for (auto r : p.children()) {
            if (indentation(r))
                continue;
            const bool image = (version == "4" || version == "5" || v6) &&
                               std::string_view(r.name()) == "image";
            if (std::string_view(r.name()) != "r" && !image)
                throw std::runtime_error("Unknown inline content; refusing lossy import");
            if (image)
                attributes(r, {"resource", "width", "height", "description", "family", "size",
                               "bold", "italic", "underline", "color", "strike", "highlight",
                               v6 ? "direct" : "resource"});
            else
                attributes(r, {"family", "size", "bold", "italic", "underline", "color", "strike",
                               "highlight", v6 ? "direct" : "family"});
            for (auto child : r.children())
                if (image ? !indentation(child)
                          : child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata)
                    throw std::runtime_error("Unexpected nested inline content");
            Run run;
            if (!image)
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
            if (image)
                run = Run::fromImage({identifier(r, "resource"), number(r, "width", 0, 0, 2880),
                                      number(r, "height", 0, 0, 2880),
                                      r.attribute("description").value()},
                                     run.style);
            if (v6 && r.attribute("direct"))
                run.direct = mask(r, AllCharacterFields);
            paragraph.runs.push_back(std::move(run));
        }
        if (paragraph.runs.empty())
            paragraph.runs.push_back({u"", {}});
        document.paragraphs.push_back(std::move(paragraph));
    }
    if (document.paragraphs.empty())
        throw std::runtime_error("Native document has no paragraphs");
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
    if (packaged && consumed.size() != parts.size())
        throw std::runtime_error("Unknown native package part; refusing lossy import");
    validateDocument(document);
    return document;
}
} // namespace wf
