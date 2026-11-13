#include "core/document/document.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
namespace wf {
void validateBookmarkName(const std::string &name) {
    const auto text = utf16(name);
    if (text.empty() || name.size() > 256 ||
        std::all_of(text.begin(), text.end(), [](auto ch) { return ch == u' '; }) ||
        std::any_of(text.begin(), text.end(), [](auto ch) { return ch < 32; }))
        throw std::invalid_argument(
            "Bookmark name must contain text, without controls (256 byte limit)");
}
Anchor Document::anchorAt(Position position, Affinity affinity) const {
    if (position.paragraph >= paragraphs.size())
        throw std::out_of_range("Anchor paragraph is missing");
    Anchor anchor{paragraphs[position.paragraph].id, position.offset, affinity};
    if (!resolve(anchor))
        throw std::out_of_range("Anchor must lie on a grapheme boundary");
    return anchor;
}
std::optional<Position> Document::resolve(const Anchor &anchor) const {
    if (static_cast<unsigned>(anchor.affinity) > 1)
        return {};
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        if (paragraphs[index].id != anchor.paragraphId)
            continue;
        const auto boundaries = paragraphs[index].boundaries();
        if (!std::binary_search(boundaries.begin(), boundaries.end(), anchor.offset))
            return {};
        return Position{index, anchor.offset};
    }
    return {};
}
std::vector<Position> Document::bookmarkPositions() const {
    if (bookmarks.empty())
        return {};
    std::unordered_map<std::uint64_t, std::size_t> indexes;
    for (std::size_t index = 0; index < paragraphs.size(); ++index)
        indexes.emplace(paragraphs[index].id, index);
    std::vector<Position> positions;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> boundariesByParagraph;
    positions.reserve(bookmarks.size());
    for (const auto &bookmark : bookmarks) {
        const auto found = indexes.find(bookmark.anchor.paragraphId);
        if (found == indexes.end() || static_cast<unsigned>(bookmark.anchor.affinity) > 1)
            throw std::invalid_argument("Dangling or invalid bookmark anchor");
        auto [cache, inserted] = boundariesByParagraph.try_emplace(bookmark.anchor.paragraphId);
        if (inserted)
            cache->second = paragraphs[found->second].boundaries();
        const auto &boundaries = cache->second;
        if (!std::binary_search(boundaries.begin(), boundaries.end(), bookmark.anchor.offset))
            throw std::invalid_argument("Bookmark anchor splits a grapheme");
        positions.push_back({found->second, bookmark.anchor.offset});
    }
    return positions;
}
const CharacterStyle &Document::styleAt(Position position) const {
    const auto &runs = paragraphs.at(position.paragraph).runs;
    if (runs.empty())
        throw std::invalid_argument("Paragraph requires a text run");
    std::size_t offset = 0;
    for (const auto &run : runs) {
        if (position.offset >= offset && position.offset <= offset + run.text().size())
            return run.style;
        offset += run.text().size();
    }
    throw std::out_of_range("Caret outside paragraph");
}
std::uint64_t Editor::addBookmark(const std::string &name, Affinity affinity) {
    validateBookmarkName(name);
    if (std::any_of(document.bookmarks.begin(), document.bookmarks.end(),
                    [&](const auto &item) { return item.name == name; }))
        throw std::invalid_argument("A bookmark already has this name");
    const auto anchor = document.anchorAt(selection.focus, affinity);
    std::uint64_t id = 0;
    auto command = [&](Editor &editor) {
        editor.document.nextId = std::max(editor.document.nextId, editor.idHighWater_);
        id = editor.document.allocateId();
        editor.document.bookmarks.push_back({id, name, anchor});
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
    return id;
}
void Editor::removeBookmark(std::uint64_t id) {
    auto command = [id](Editor &editor) {
        const auto removed = std::erase_if(editor.document.bookmarks,
                                           [id](const auto &item) { return item.id == id; });
        if (!removed)
            throw std::out_of_range("Bookmark is missing");
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
bool Editor::goToBookmark(std::uint64_t id) {
    for (const auto &bookmark : document.bookmarks)
        if (bookmark.id == id) {
            if (auto position = document.resolve(bookmark.anchor)) {
                selection = {*position, *position};
                typingStyle = document.styleAt(*position);
                return true;
            }
            return false;
        }
    return false;
}
void Editor::remapBookmarks(const std::vector<Position> &previous, Position start, Position end,
                            Position insertedEnd) {
    if (document.bookmarks.empty())
        return;
    std::unordered_map<std::uint64_t, const Paragraph *> targets;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> boundariesByParagraph;
    for (const auto &paragraph : document.paragraphs)
        targets.emplace(paragraph.id, &paragraph);
    for (std::size_t index = 0; index < document.bookmarks.size(); ++index) {
        auto &anchor = document.bookmarks[index].anchor;
        const auto old = previous[index];
        if (old < start || (old > end && old.paragraph != end.paragraph))
            continue;
        if (old >= start && old <= end) {
            const auto target = anchor.affinity == Affinity::Before ? start : insertedEnd;
            anchor.paragraphId = document.paragraphs[target.paragraph].id;
            anchor.offset = target.offset;
        } else if (old > end && old.paragraph == end.paragraph) {
            anchor.paragraphId = document.paragraphs[insertedEnd.paragraph].id;
            anchor.offset = insertedEnd.offset + (old.offset - end.offset);
        }
        const auto target = targets.find(anchor.paragraphId);
        if (target == targets.end())
            throw std::logic_error("Bookmark transform lost its paragraph");
        auto [cache, inserted] = boundariesByParagraph.try_emplace(anchor.paragraphId);
        if (inserted)
            cache->second = target->second->boundaries();
        const auto &boundaries = cache->second;
        auto boundary = std::lower_bound(boundaries.begin(), boundaries.end(), anchor.offset);
        if (boundary == boundaries.end())
            throw std::logic_error("Bookmark transform exceeded its paragraph");
        if (*boundary != anchor.offset && anchor.affinity == Affinity::Before &&
            boundary != boundaries.begin())
            --boundary;
        anchor.offset = *boundary;
    }
}
} // namespace wf
