#include "core/document/document.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace wf {
Run::Run(std::u16string value, CharacterStyle character)
    : style(std::move(character)), content_(TextContent{std::move(value)}) {}
const std::u16string &Run::text() const {
    static const std::u16string object = u"\ufffc";
    if (const auto *text = std::get_if<TextContent>(&content_))
        return text->value;
    return object;
}
const InlineImage *Run::image() const {
    return std::get_if<InlineImage>(&content_);
}
void validateInlineImage(const InlineImage &image) {
    if (!image.resourceId || !std::isfinite(image.width) || !std::isfinite(image.height) ||
        image.width < 1 || image.height < 1 || image.width > 2880 || image.height > 2880 ||
        image.description.size() > 4096)
        throw std::invalid_argument("Invalid image reference or dimensions");
    for (auto ch : utf16(image.description))
        if (ch < 32)
            throw std::invalid_argument("Image description contains a control character");
}
namespace {
std::optional<std::pair<std::size_t, std::size_t>> selected(const Editor &editor) {
    const auto start = std::min(editor.selection.anchor, editor.selection.focus);
    const auto end = std::max(editor.selection.anchor, editor.selection.focus);
    if (start.paragraph != end.paragraph || start.paragraph >= editor.document.paragraphs.size() ||
        (start != end && end.offset - start.offset != 1))
        return {};
    std::size_t offset = 0;
    const auto &runs = editor.document.paragraphs[start.paragraph].runs;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (runs[index].image() &&
            ((start.offset == offset && (start == end || end.offset == offset + 1)) ||
             (start == end && start.offset == offset + 1)))
            return std::pair{start.paragraph, index};
        offset += runs[index].text().size();
    }
    return {};
}
} // namespace
Run Run::fromImage(InlineImage image, CharacterStyle style) {
    validateInlineImage(image);
    Run run({}, std::move(style));
    run.content_ = std::move(image);
    return run;
}
Run Run::slice(std::size_t start, std::size_t end) const {
    if (start > end || end > text().size())
        throw std::out_of_range("Invalid inline slice");
    if (image()) {
        if (start != 0 || end != 1)
            throw std::invalid_argument("Cannot split an image atom");
        return *this;
    }
    Run part(text().substr(start, end - start), style);
    part.direct = direct;
    part.characterStyle = characterStyle;
    return part;
}
void Run::appendText(const std::u16string &text) {
    auto *value = std::get_if<TextContent>(&content_);
    if (!value)
        throw std::logic_error("Cannot append text to an image atom");
    value->value += text;
}
void validateImageResource(const ImageResource &resource) {
    if (resource.png.size() < 24 || resource.png.size() > 16 * 1024 * 1024 ||
        resource.png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) ||
        resource.png.compare(12, 4, "IHDR") || !resource.pixelWidth || !resource.pixelHeight ||
        resource.pixelWidth > 16384 || resource.pixelHeight > 16384 ||
        static_cast<std::uint64_t>(resource.pixelWidth) * resource.pixelHeight > 25000000)
        throw std::invalid_argument("Unsupported image data or decoded size (25 megapixel limit)");
    auto dimension = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t index = offset; index < offset + 4; ++index)
            value = (value << 8) | static_cast<unsigned char>(resource.png[index]);
        return value;
    };
    if (dimension(16) != resource.pixelWidth || dimension(20) != resource.pixelHeight)
        throw std::invalid_argument("PNG dimensions do not match resource metadata");
}
void Document::pruneUnusedImages() {
    if (images.empty())
        return;
    std::unordered_set<std::uint64_t> used;
    for (const auto &paragraph : paragraphs)
        for (const auto &run : paragraph.runs)
            if (run.image())
                used.insert(run.image()->resourceId);
    std::erase_if(images, [&](const auto &entry) { return !used.contains(entry.first); });
}
std::vector<std::size_t> Paragraph::boundaries() const {
    auto result = graphemeBoundaries(text());
    std::size_t offset = 0;
    for (const auto &run : runs) {
        if (run.image()) {
            result.push_back(offset);
            result.push_back(offset + 1);
        }
        offset += run.text().size();
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
std::size_t previousGrapheme(const Paragraph &paragraph, std::size_t offset) {
    const auto boundaries = paragraph.boundaries();
    if (offset > boundaries.back())
        throw std::out_of_range("Caret outside paragraph");
    const auto found = std::lower_bound(boundaries.begin(), boundaries.end(), offset);
    return found == boundaries.begin() ? 0 : *std::prev(found);
}
std::size_t nextGrapheme(const Paragraph &paragraph, std::size_t offset) {
    const auto boundaries = paragraph.boundaries();
    if (offset > boundaries.back())
        throw std::out_of_range("Caret outside paragraph");
    const auto found = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
    return found == boundaries.end() ? offset : *found;
}
std::uint64_t Editor::insertImage(std::shared_ptr<const ImageResource> resource, double width,
                                  double height, const std::string &description) {
    if (!resource)
        throw std::invalid_argument("Missing image resource");
    validateImageResource(*resource);
    const auto frozen = std::make_shared<const ImageResource>(*resource);
    std::uint64_t id = 0;
    auto command = [&](Editor &editor) {
        editor.document.nextId = std::max(editor.document.nextId, editor.idHighWater_);
        id = editor.document.allocateId();
        auto run = Run::fromImage({id, width, height, description}, editor.typingStyle);
        editor.document.images.emplace(id, frozen);
        const auto start = std::min(editor.selection.anchor, editor.selection.focus);
        editor.replaceRange(u"\ufffc", editor.selection, &run);
        editor.selection = {start, {start.paragraph, start.offset + 1}};
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
    return id;
}
std::optional<InlineImage> Editor::selectedImage() const {
    const auto location = selected(*this);
    if (!location)
        return {};
    return *document.paragraphs[location->first].runs[location->second].image();
}
void Editor::resizeImage(double width, double height) {
    auto command = [&](Editor &editor) {
        const auto location = selected(editor);
        if (!location)
            throw std::invalid_argument("Select a picture first");
        auto &paragraph = editor.document.paragraphs[location->first];
        auto &run = paragraph.runs[location->second];
        auto image = *run.image();
        if (image.width == width && image.height == height)
            return;
        image.width = width;
        image.height = height;
        const auto direct = run.direct;
        auto characterStyle = run.characterStyle;
        run = Run::fromImage(std::move(image), run.style);
        run.direct = direct;
        run.characterStyle = std::move(characterStyle);
        paragraph.revision = editor.document.revision = ++editor.serial_;
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
} // namespace wf
