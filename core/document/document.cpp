#include "core/document/document.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unordered_map>
#include <unordered_set>

namespace wf {
void validateCharacterStyle(const CharacterStyle &style) {
    const auto family = utf16(style.family);
    if (style.family.size() > 256 || !std::isfinite(style.size) || style.size < 1 ||
        style.size > 200 ||
        std::any_of(family.begin(), family.end(), [](auto ch) { return ch < 32; }))
        throw std::invalid_argument("Invalid character formatting");
}
std::uint64_t Document::allocateId() {
    if (!nextId || nextId == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Document node identifiers exhausted");
    return nextId++;
}
void validateDocument(const Document &document) {
    validatePage(document.page);
    if (document.paragraphs.empty() || !document.nextId)
        throw std::invalid_argument("Document requires paragraphs and a valid allocator");
    std::unordered_set<std::uint64_t> ids;
    validateStyles(document.styles);
    std::unordered_map<std::string, ResolvedStyle> resolved;
    validateTables(document);
    for (const auto &table : document.tables)
        if (!table.id || table.id >= document.nextId || !ids.insert(table.id).second)
            throw std::invalid_argument("Invalid table identifier");
    std::size_t imageBytes = 0;
    for (const auto &[id, resource] : document.images) {
        if (!id || id >= document.nextId || !resource || !ids.insert(id).second)
            throw std::invalid_argument("Invalid image resource identifier");
        validateImageResource(*resource);
        if (resource->png.size() > 64 * 1024 * 1024 - imageBytes)
            throw std::invalid_argument("Image resources exceed the 64 MiB preview limit");
        imageBytes += resource->png.size();
    }
    for (const auto &paragraph : document.paragraphs) {
        if (!paragraph.id || paragraph.id >= document.nextId || !ids.insert(paragraph.id).second)
            throw std::invalid_argument("Invalid or duplicate document node identifier");
        validateParagraphFormat(paragraph);
        if (paragraph.runs.empty())
            throw std::invalid_argument("Paragraph requires a text run");
        // Inherited values must equal the style's: the style, not a stale copy, is authoritative.
        auto style = resolved.find(paragraph.style);
        if (style == resolved.end())
            style = resolved.emplace(paragraph.style, resolveStyle(document.styles, paragraph.style)).first;
        if ((paragraph.direct & ~AllParagraphFields) ||
            (differingFields(static_cast<const ParagraphFormat &>(paragraph), style->second.paragraph) &
             ~paragraph.direct))
            throw std::invalid_argument("Paragraph formatting disagrees with its style");
        for (const auto &run : paragraph.runs) {
            if ((run.direct & ~AllCharacterFields) ||
                (differingFields(run.style, style->second.character) & ~run.direct))
                throw std::invalid_argument("Character formatting disagrees with its style");
            if (const auto *image = run.image()) {
                validateInlineImage(*image);
                if (!document.images.contains(image->resourceId))
                    throw std::invalid_argument("Missing image resource");
            }
            utf8(run.text());
            for (auto ch : run.text())
                if (ch < 32 && ch != u'\t')
                    throw std::invalid_argument("Invalid character in paragraph");
            validateCharacterStyle(run.style);
        }
    }
    std::unordered_set<std::string> names;
    if (!document.bookmarks.empty())
        (void)document.bookmarkPositions();
    for (const auto &bookmark : document.bookmarks) {
        validateBookmarkName(bookmark.name);
        if (!bookmark.id || bookmark.id >= document.nextId || !ids.insert(bookmark.id).second ||
            !names.insert(bookmark.name).second)
            throw std::invalid_argument("Invalid, duplicate or dangling bookmark");
    }
}
void validateParagraphFormat(const ParagraphFormat &value) {
    if (!std::isfinite(value.indentLeft) || value.indentLeft < 0 || value.indentLeft > 360 ||
        !std::isfinite(value.spaceBefore) || value.spaceBefore < 0 || value.spaceBefore > 200 ||
        !std::isfinite(value.spaceAfter) || value.spaceAfter < 0 || value.spaceAfter > 200 ||
        !std::isfinite(value.lineSpacing) || value.lineSpacing < 1 || value.lineSpacing > 3 ||
        static_cast<unsigned>(value.alignment) > 2 || static_cast<unsigned>(value.list) > 2)
        throw std::invalid_argument("Invalid paragraph formatting");
}
void validatePage(const PageStyle &page) {
    if (!std::isfinite(page.width) || !std::isfinite(page.height) || !std::isfinite(page.margin) ||
        page.width < 72 || page.width > 2880 || page.height < 72 || page.height > 2880 ||
        page.margin < 0 || page.width - 2 * page.margin < 36 || page.height - 2 * page.margin < 36)
        throw std::invalid_argument("Invalid page geometry");
}
void Editor::setPage(PageStyle page) {
    validatePage(page);
    checkpoint();
    document.page = page;
    document.revision = ++serial_;
}
namespace {
using BreakIterator = std::unique_ptr<UBreakIterator, decltype(&ubrk_close)>;
BreakIterator graphemes(const std::u16string &text) {
    UErrorCode error = U_ZERO_ERROR;
    auto result =
        BreakIterator(ubrk_open(UBRK_CHARACTER, "und", reinterpret_cast<const UChar *>(text.data()),
                                static_cast<int32_t>(text.size()), &error),
                      ubrk_close);
    if (U_FAILURE(error))
        throw std::runtime_error(u_errorName(error));
    return result;
}
void append(std::vector<Run> &runs, Run run) {
    if (run.text().empty())
        return;
    if (!run.image() && !runs.empty() && !runs.back().image() && runs.back().style == run.style &&
        runs.back().direct == run.direct)
        runs.back().appendText(run.text());
    else
        runs.push_back(std::move(run));
}
std::vector<Run> slice(const Paragraph &p, std::size_t start, std::size_t end) {
    std::vector<Run> result;
    std::size_t offset = 0;
    for (const auto &r : p.runs) {
        const auto a = std::max(start, offset), b = std::min(end, offset + r.text().size());
        if (a < b)
            append(result, r.slice(a - offset, b - offset));
        offset += r.text().size();
    }
    return result;
}
std::pair<Position, Position> ordered(Selection selection) {
    return {std::min(selection.anchor, selection.focus),
            std::max(selection.anchor, selection.focus)};
}
// Gives a paragraph a style and drops all its direct formatting.
void setStyle(Paragraph &paragraph, const std::string &id, const ResolvedStyle &style) {
    const bool pageBreak = paragraph.pageBreakBefore;
    static_cast<ParagraphFormat &>(paragraph) = style.paragraph;
    paragraph.pageBreakBefore = pageBreak;
    paragraph.style = id;
    paragraph.direct = 0;
    for (auto &run : paragraph.runs) {
        run.style = style.character;
        run.direct = 0;
    }
}
Run styledRun(std::u16string text, const CharacterStyle &value, const CharacterStyle &inherited) {
    Run run(std::move(text), value);
    run.direct = differingFields(value, inherited);
    return run;
}
} // namespace
std::pair<std::size_t, std::size_t> selectedParagraphs(Selection selection) {
    auto [start, end] = ordered(selection);
    if (end.paragraph > start.paragraph && end.offset == 0)
        --end.paragraph;
    return {start.paragraph, end.paragraph};
}
std::u16string Paragraph::text() const {
    std::u16string result;
    for (const auto &run : runs)
        result += run.text();
    return result;
}
std::u16string Document::text() const {
    std::u16string result;
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        if (i)
            result += u'\n';
        result += paragraphs[i].text();
    }
    return result;
}
std::string utf8(const std::u16string &text) {
    UErrorCode error = U_ZERO_ERROR;
    int32_t length = 0;
    u_strToUTF8(nullptr, 0, &length, reinterpret_cast<const UChar *>(text.data()),
                static_cast<int32_t>(text.size()), &error);
    if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error))
        throw std::runtime_error("Invalid UTF-16");
    error = U_ZERO_ERROR;
    std::string result(length, '\0');
    u_strToUTF8(result.data(), length, nullptr, reinterpret_cast<const UChar *>(text.data()),
                static_cast<int32_t>(text.size()), &error);
    if (U_FAILURE(error))
        throw std::runtime_error("Invalid UTF-16");
    return result;
}
std::u16string utf16(const std::string &text) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        throw std::length_error("Text is too large");
    UErrorCode error = U_ZERO_ERROR;
    int32_t length = 0;
    u_strFromUTF8(nullptr, 0, &length, text.data(), static_cast<int32_t>(text.size()), &error);
    if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error))
        throw std::runtime_error("Invalid UTF-8");
    error = U_ZERO_ERROR;
    std::u16string result(length, u'\0');
    u_strFromUTF8(reinterpret_cast<UChar *>(result.data()), length, nullptr, text.data(),
                  static_cast<int32_t>(text.size()), &error);
    if (U_FAILURE(error))
        throw std::runtime_error("Invalid UTF-8");
    return result;
}
std::size_t previousGrapheme(const std::u16string &text, std::size_t n) {
    if (n > text.size())
        throw std::out_of_range("Caret outside text");
    if (!n)
        return 0;
    auto iterator = graphemes(text);
    return static_cast<std::size_t>(ubrk_preceding(iterator.get(), static_cast<int32_t>(n)));
}
std::vector<std::size_t> graphemeBoundaries(const std::u16string &text) {
    auto iterator = graphemes(text);
    std::vector<std::size_t> boundaries;
    for (auto position = ubrk_first(iterator.get()); position != UBRK_DONE;
         position = ubrk_next(iterator.get()))
        boundaries.push_back(static_cast<std::size_t>(position));
    return boundaries;
}
std::size_t nextGrapheme(const std::u16string &text, std::size_t n) {
    if (n > text.size())
        throw std::out_of_range("Caret outside text");
    if (n == text.size())
        return n;
    auto iterator = graphemes(text);
    return static_cast<std::size_t>(ubrk_following(iterator.get(), static_cast<int32_t>(n)));
}
void Editor::validate(Selection range) const {
    for (auto p : {range.anchor, range.focus}) {
        if (p.paragraph >= document.paragraphs.size())
            throw std::out_of_range("Paragraph outside document");
        const auto text = document.paragraphs[p.paragraph].text();
        if (p.offset > text.size())
            throw std::out_of_range("Caret outside paragraph");
        const auto boundaries = document.paragraphs[p.paragraph].boundaries();
        if (!std::binary_search(boundaries.begin(), boundaries.end(), p.offset))
            throw std::out_of_range("Caret splits grapheme");
    }
}
Editor::State Editor::state() const {
    return {document, selection, typingStyle};
}
void Editor::checkpoint() {
    if (inTransaction_)
        return;
    undo_.push_back(std::make_shared<const State>(state()));
    redo_.clear();
}
ChangeSet Editor::transact(const std::function<void(Editor &)> &command) {
    if (inTransaction_)
        throw std::logic_error("Nested transactions are not supported");
    Editor candidate = *this;
    candidate.inTransaction_ = true;
    command(candidate);
    candidate.document.pruneUnusedImages();
    candidate.document.nextId = std::max(
        {document.nextId, candidate.document.nextId, idHighWater_, candidate.idHighWater_});
    validateDocument(candidate.document);
    candidate.validate(candidate.selection);
    validateCharacterStyle(candidate.typingStyle);
    ChangeSet changes;
    changes.beforeRevision = document.revision;
    changes.pageChanged = document.page != candidate.document.page;
    changes.bookmarksChanged = document.bookmarks != candidate.document.bookmarks;
    changes.resourcesChanged = document.images != candidate.document.images;
    changes.tablesChanged = document.tables != candidate.document.tables;
    for (const auto &table : candidate.document.tables)
        if (table.id < idHighWater_ && std::none_of(
                                           document.tables.begin(), document.tables.end(),
                                           [&](const auto &old) { return old.id == table.id; }))
            throw std::invalid_argument("Transaction reuses a retired table identifier");
    for (const auto &[id, resource] : candidate.document.images)
        if (id < idHighWater_ && !document.images.contains(id))
            throw std::invalid_argument("Transaction reuses a retired resource identifier");
    std::unordered_set<std::uint64_t> previousBookmarks;
    for (const auto &bookmark : document.bookmarks)
        previousBookmarks.insert(bookmark.id);
    for (const auto &bookmark : candidate.document.bookmarks)
        if (bookmark.id < idHighWater_ && !previousBookmarks.contains(bookmark.id))
            throw std::invalid_argument("Transaction reuses a retired bookmark identifier");
    std::unordered_map<std::uint64_t, const Paragraph *> previous;
    for (const auto &paragraph : document.paragraphs)
        previous.emplace(paragraph.id, &paragraph);
    std::vector<std::uint64_t> previousOrder, nextOrder;
    std::unordered_set<std::uint64_t> remaining;
    for (std::size_t index = 0; index < candidate.document.paragraphs.size(); ++index) {
        const auto &paragraph = candidate.document.paragraphs[index];
        remaining.insert(paragraph.id);
        const auto found = previous.find(paragraph.id);
        if (found == previous.end()) {
            if (paragraph.id < idHighWater_)
                throw std::invalid_argument("Transaction reuses a retired document identifier");
            changes.inserted.push_back(paragraph.id);
        } else {
            nextOrder.push_back(paragraph.id);
            if (paragraph != *found->second)
                changes.changed.push_back(paragraph.id);
        }
    }
    for (const auto &paragraph : document.paragraphs)
        if (!remaining.contains(paragraph.id))
            changes.removed.push_back(paragraph.id);
        else
            previousOrder.push_back(paragraph.id);
    changes.orderChanged = previousOrder != nextOrder;
    const bool contentChanged = changes.pageChanged || changes.orderChanged ||
                                changes.bookmarksChanged || changes.resourcesChanged ||
                                changes.tablesChanged || !changes.inserted.empty() ||
                                !changes.removed.empty() || !changes.changed.empty();
    if (contentChanged) {
        candidate.document.revision = ++candidate.serial_;
        std::unordered_set<std::uint64_t> dirty(changes.changed.begin(), changes.changed.end());
        dirty.insert(changes.inserted.begin(), changes.inserted.end());
        for (auto &paragraph : candidate.document.paragraphs)
            if (dirty.contains(paragraph.id))
                paragraph.revision = candidate.document.revision;
        candidate.undo_.push_back(std::make_shared<const State>(state()));
        candidate.redo_.clear();
    } else
        candidate.document.revision = document.revision;
    candidate.inTransaction_ = false;
    candidate.idHighWater_ = std::max(candidate.idHighWater_, candidate.document.nextId);
    changes.afterRevision = candidate.document.revision;
    *this = std::move(candidate);
    return changes;
}
void Editor::restore(State state, bool preserveAllocator) {
    if (preserveAllocator)
        state.document.nextId = std::max({state.document.nextId, document.nextId, idHighWater_});
    idHighWater_ = state.document.nextId;
    document = std::move(state.document);
    selection = state.selection;
    typingStyle = std::move(state.typingStyle);
    document.revision = ++serial_;
    for (auto &p : document.paragraphs)
        p.revision = serial_;
}
void Editor::replace(const std::u16string &input) {
    replaceRange(input, selection);
}
void Editor::replaceRange(const std::u16string &input, Selection range, const Run *object) {
    validate(range);
    // Validate Unicode before changing the document or its undo history.
    utf8(input);
    std::u16string text;
    for (std::size_t i = 0; i < input.size(); ++i) {
        auto ch = input[i];
        if (ch == u'\r') {
            if (i + 1 < input.size() && input[i + 1] == u'\n')
                ++i;
            ch = u'\n';
        }
        if (ch < 32 && ch != u'\n' && ch != u'\t')
            throw std::invalid_argument("Unsupported control character");
        text += ch;
    }
    const auto [start, end] = ordered(range);
    const auto cell = document.paragraphs[start.paragraph].cell;
    for (auto i = start.paragraph; i <= end.paragraph; ++i)
        if (document.paragraphs[i].cell != cell)
            throw std::invalid_argument(
                "Selection crosses table cell boundaries; edit one cell or use table controls");
    if (start == end && text.empty())
        return;
    const auto bookmarkPositions = document.bookmarkPositions();
    bool removedImage = false;
    if (start != end && !document.images.empty())
        for (auto index = start.paragraph; index <= end.paragraph; ++index) {
            std::size_t offset = 0;
            const auto first = index == start.paragraph ? start.offset : 0;
            const auto last =
                index == end.paragraph ? end.offset : document.paragraphs[index].text().size();
            for (const auto &run : document.paragraphs[index].runs) {
                if (run.image() && first < offset + 1 && last > offset)
                    removedImage = true;
                offset += run.text().size();
            }
        }
    auto prefix = slice(document.paragraphs[start.paragraph], 0, start.offset);
    auto suffix = slice(document.paragraphs[end.paragraph], end.offset,
                        document.paragraphs[end.paragraph].text().size());
    Paragraph prototype = document.paragraphs[start.paragraph];
    const auto inherited = resolveStyle(document.styles, prototype.style).character;
    // Text joining from another paragraph keeps its look under this paragraph's style.
    if (end.paragraph != start.paragraph)
        for (auto &run : suffix)
            run.direct |= differingFields(run.style, inherited);
    const auto newParagraphs =
        static_cast<std::uint64_t>(std::count(text.begin(), text.end(), u'\n'));
    const auto allocationStart = std::max(document.nextId, idHighWater_);
    if (!allocationStart ||
        newParagraphs > std::numeric_limits<std::uint64_t>::max() - allocationStart)
        throw std::overflow_error("Document node identifiers exhausted");
    checkpoint();
    document.nextId = allocationStart;
    std::vector<Paragraph> replacement;
    std::size_t offset = 0;
    do {
        auto split = text.find(u'\n', offset);
        const bool last = split == std::u16string::npos;
        if (last)
            split = text.size();
        Paragraph p = prototype;
        if (!replacement.empty()) {
            p.id = document.allocateId();
            p.pageBreakBefore = false;
        }
        p.revision = serial_ + 1;
        p.runs = replacement.empty() ? std::move(prefix) : std::vector<Run>{};
        if (object) {
            auto inserted = *object;
            inserted.direct |= differingFields(inserted.style, inherited);
            append(p.runs, std::move(inserted));
        } else
            append(p.runs, styledRun(text.substr(offset, split - offset), typingStyle, inherited));
        const auto caretOffset = (replacement.empty() ? start.offset : 0) + split - offset;
        if (last)
            for (auto &run : suffix)
                append(p.runs, std::move(run));
        if (p.runs.empty())
            p.runs.push_back(styledRun(u"", typingStyle, inherited));
        replacement.push_back(std::move(p));
        if (last) {
            selection.focus = {start.paragraph + replacement.size() - 1, caretOffset};
            selection.anchor = selection.focus;
            break;
        }
        offset = split + 1;
    } while (true);
    auto begin = document.paragraphs.begin() + static_cast<std::ptrdiff_t>(start.paragraph);
    auto after = document.paragraphs.erase(
        begin, document.paragraphs.begin() + static_cast<std::ptrdiff_t>(end.paragraph + 1));
    document.paragraphs.insert(after, std::make_move_iterator(replacement.begin()),
                               std::make_move_iterator(replacement.end()));
    remapBookmarks(bookmarkPositions, start, end, selection.focus);
    if (removedImage)
        document.pruneUnusedImages();
    const auto joinedBoundaries = document.paragraphs[selection.focus.paragraph].boundaries();
    selection.focus.offset =
        *std::lower_bound(joinedBoundaries.begin(), joinedBoundaries.end(), selection.focus.offset);
    selection.anchor = selection.focus;
    document.revision = ++serial_;
    idHighWater_ = std::max(idHighWater_, document.nextId);
}
void Editor::eraseBackward() {
    validate(selection);
    auto range = selection;
    if (range.anchor == range.focus) {
        auto &p = range.anchor;
        if (p.offset)
            p.offset = previousGrapheme(document.paragraphs[p.paragraph], p.offset);
        else if (p.paragraph) {
            if (document.paragraphs[p.paragraph].cell != document.paragraphs[p.paragraph - 1].cell)
                return;
            --p.paragraph;
            p.offset = document.paragraphs[p.paragraph].text().size();
        }
    }
    replaceRange(u"", range);
}
void Editor::eraseForward() {
    validate(selection);
    auto range = selection;
    if (range.anchor == range.focus) {
        auto &p = range.focus;
        const auto text = document.paragraphs[p.paragraph].text();
        if (p.offset < text.size())
            p.offset = nextGrapheme(document.paragraphs[p.paragraph], p.offset);
        else if (p.paragraph + 1 < document.paragraphs.size()) {
            if (document.paragraphs[p.paragraph].cell != document.paragraphs[p.paragraph + 1].cell)
                return;
            ++p.paragraph;
            p.offset = 0;
        }
    }
    replaceRange(u"", range);
}
void Editor::format(const std::function<void(CharacterStyle &)> &change) {
    validate(selection);
    auto updated = typingStyle;
    change(updated);
    const auto [start, end] = ordered(selection);
    if (start == end) {
        typingStyle = updated;
        return;
    }
    checkpoint();
    typingStyle = updated;
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        auto &p = document.paragraphs[i];
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : p.text().size();
        auto runs = slice(p, 0, a), selected = slice(p, a, b),
             suffix = slice(p, b, p.text().size());
        for (auto &run : selected) {
            const auto before = run.style;
            change(run.style);
            run.direct |= differingFields(before, run.style);
            append(runs, std::move(run));
        }
        for (auto &run : suffix)
            append(runs, std::move(run));
        if (runs.empty())
            runs.push_back(
                styledRun(u"", updated, resolveStyle(document.styles, p.style).character));
        p.runs = std::move(runs);
        p.revision = serial_ + 1;
    }
    document.revision = ++serial_;
}
void Editor::align(Alignment alignment) {
    formatParagraph([alignment](auto &p) { p.alignment = alignment; });
}
void Editor::formatParagraph(const std::function<void(ParagraphFormat &)> &change) {
    validate(selection);
    const auto [first, last] = selectedParagraphs(selection);
    std::vector<ParagraphFormat> updated;
    bool changed = false;
    for (auto i = first; i <= last; ++i) {
        auto value = static_cast<const ParagraphFormat &>(document.paragraphs[i]);
        change(value);
        validateParagraphFormat(value);
        if (document.paragraphs[i].cell && value.pageBreakBefore)
            throw std::invalid_argument("Page breaks inside table cells are not supported");
        changed = changed || value != static_cast<const ParagraphFormat &>(document.paragraphs[i]);
        updated.push_back(value);
    }
    if (!changed)
        return;
    checkpoint();
    document.revision = ++serial_;
    for (auto i = first; i <= last; ++i) {
        auto &paragraph = document.paragraphs[i];
        paragraph.direct |=
            differingFields(static_cast<const ParagraphFormat &>(paragraph), updated[i - first]);
        static_cast<ParagraphFormat &>(paragraph) = updated[i - first];
        paragraph.revision = serial_;
    }
}
void Editor::applyPreset(ParagraphPreset preset) { applyStyle(presetStyleId(preset)); }
void Editor::applyStyle(const std::string &id) {
    validate(selection);
    const auto style = resolveStyle(document.styles, id);
    const auto [first, last] = selectedParagraphs(selection);
    checkpoint();
    document.revision = ++serial_;
    for (auto i = first; i <= last; ++i) {
        setStyle(document.paragraphs[i], id, style);
        document.paragraphs[i].revision = serial_;
    }
    typingStyle = style.character;
}
void Editor::clearCharacterFormatting() {
    validate(selection);
    const auto [start, end] = ordered(selection);
    const auto caretStyle =
        resolveStyle(document.styles, document.paragraphs[selection.focus.paragraph].style);
    if (start == end) {
        typingStyle = caretStyle.character;
        return;
    }
    checkpoint();
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        auto &p = document.paragraphs[i];
        const auto inherited = resolveStyle(document.styles, p.style).character;
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : p.text().size();
        auto runs = slice(p, 0, a);
        for (auto &run : slice(p, a, b)) {
            run.style = inherited;
            run.direct = 0;
            append(runs, std::move(run));
        }
        for (auto &run : slice(p, b, p.text().size()))
            append(runs, std::move(run));
        if (runs.empty())
            runs.push_back({u"", inherited});
        p.runs = std::move(runs);
        p.revision = serial_ + 1;
    }
    typingStyle = caretStyle.character;
    document.revision = ++serial_;
}
void Editor::restyle() {
    std::unordered_map<std::string, ResolvedStyle> resolved;
    for (auto &paragraph : document.paragraphs) {
        auto found = resolved.find(paragraph.style);
        if (found == resolved.end())
            found = resolved.emplace(paragraph.style, resolveStyle(document.styles, paragraph.style)).first;
        const auto before = paragraph;
        inheritFields(paragraph, found->second.paragraph, paragraph.direct);
        for (auto &run : paragraph.runs)
            inheritFields(run.style, found->second.character, run.direct);
        if (paragraph != before)
            paragraph.revision = serial_ + 1;
    }
}
void Editor::updateStyleFromSelection() {
    validate(selection);
    auto &source = document.paragraphs[selection.focus.paragraph];
    auto styles = document.styles;
    auto *style = styles.find(source.style);
    const auto base =
        style->basedOn.empty() ? ResolvedStyle{} : resolveStyle(styles, style->basedOn);
    style->paragraph = propertiesFor(base.paragraph, source);
    style->character = propertiesFor(base.character, document.styleAt(selection.focus));
    validateStyles(styles);
    checkpoint();
    document.styles = std::move(styles);
    const auto now = resolveStyle(document.styles, source.style);
    // The source paragraph now matches its style; keep only what still differs from it.
    source.direct = differingFields(static_cast<const ParagraphFormat &>(source), now.paragraph);
    for (auto &run : source.runs)
        run.direct = differingFields(run.style, now.character);
    restyle();
    typingStyle = document.styleAt(selection.focus);
    document.revision = ++serial_;
}
std::string Editor::createStyleFromSelection(const std::string &name) {
    validate(selection);
    const auto &source = document.paragraphs[selection.focus.paragraph];
    auto styles = document.styles;
    const auto base = resolveStyle(styles, source.style);
    ParagraphStyle style{uniqueStyleId(styles, name), name, source.style, "", {}, {}};
    style.paragraph = propertiesFor(base.paragraph, source);
    style.character = propertiesFor(base.character, document.styleAt(selection.focus));
    styles.paragraphStyles.push_back(style);
    validateStyles(styles);
    checkpoint();
    document.styles = std::move(styles);
    const auto resolved = resolveStyle(document.styles, style.id);
    const auto [first, last] = selectedParagraphs(selection);
    for (auto i = first; i <= last; ++i) {
        // Same appearance; only what differs from the new style stays direct.
        auto &paragraph = document.paragraphs[i];
        paragraph.style = style.id;
        paragraph.direct =
            differingFields(static_cast<const ParagraphFormat &>(paragraph), resolved.paragraph);
        for (auto &run : paragraph.runs)
            run.direct = differingFields(run.style, resolved.character);
        paragraph.revision = serial_ + 1;
    }
    document.revision = ++serial_;
    return style.id;
}
void Editor::renameStyle(const std::string &id, const std::string &name) {
    if (isBuiltinStyle(id))
        throw std::invalid_argument("Built-in styles cannot be renamed");
    auto styles = document.styles;
    auto *style = styles.find(id);
    if (!style)
        throw std::invalid_argument("Unknown paragraph style: " + id);
    if (style->name == name)
        return;
    style->name = name;
    validateStyles(styles);
    checkpoint();
    document.styles = std::move(styles);
    document.revision = ++serial_;
}
void Editor::deleteStyle(const std::string &id) {
    if (isBuiltinStyle(id))
        throw std::invalid_argument("Built-in styles cannot be deleted");
    const auto *found = document.styles.find(id);
    if (!found)
        throw std::invalid_argument("Unknown paragraph style: " + id);
    const auto removed = *found;
    const auto replacement = removed.basedOn.empty() ? std::string("Normal") : removed.basedOn;
    auto styles = document.styles;
    std::erase_if(styles.paragraphStyles, [&](const auto &style) { return style.id == id; });
    for (auto &style : styles.paragraphStyles) {
        if (style.basedOn == id) {
            fillUnset(style.paragraph, removed.paragraph);
            fillUnset(style.character, removed.character);
            style.basedOn = removed.basedOn;
        }
        if (style.next == id)
            style.next = replacement == style.id ? "" : replacement;
    }
    validateStyles(styles);
    checkpoint();
    document.styles = std::move(styles);
    for (auto &paragraph : document.paragraphs)
        if (paragraph.style == id)
            paragraph.style = replacement;
    restyle();
    typingStyle = document.styleAt(selection.focus);
    document.revision = ++serial_;
}
void Editor::insertParagraphBreak() {
    validate(selection);
    const auto &current = document.paragraphs[selection.focus.paragraph];
    if (selection.anchor == selection.focus && current.text().empty() &&
        current.list != ListKind::None) {
        formatParagraph([](auto &p) { p.list = ListKind::None; });
        return;
    }
    // Enter at the end of a paragraph continues with its style's "next" style.
    const auto *style = document.styles.find(current.style);
    const auto next = style && !style->next.empty() ? style->next : current.style;
    const bool body = selection.anchor == selection.focus &&
                      selection.focus.offset == current.text().size() && next != current.style;
    replace(u"\n");
    if (body) {
        const auto resolved = resolveStyle(document.styles, next);
        auto &created = document.paragraphs[selection.focus.paragraph];
        setStyle(created, next, resolved);
        created.revision = document.revision;
        typingStyle = resolved.character;
    }
}
void Editor::insertPageBreak() {
    auto command = [](Editor &editor) {
        editor.replace(u"\n");
        editor.formatParagraph([](auto &paragraph) { paragraph.pageBreakBefore = true; });
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
bool Editor::undo() {
    if (inTransaction_)
        throw std::logic_error("Undo cannot run inside a transaction");
    if (undo_.empty())
        return false;
    redo_.push_back(std::make_shared<const State>(state()));
    auto previous = *undo_.back();
    undo_.pop_back();
    restore(std::move(previous));
    return true;
}
bool Editor::redo() {
    if (inTransaction_)
        throw std::logic_error("Redo cannot run inside a transaction");
    if (redo_.empty())
        return false;
    undo_.push_back(std::make_shared<const State>(state()));
    auto next = *redo_.back();
    redo_.pop_back();
    restore(std::move(next));
    return true;
}
void Editor::reset(Document d) {
    if (inTransaction_)
        throw std::logic_error("Reset cannot run inside a transaction");
    d.pruneUnusedImages();
    validateDocument(d);
    restore({std::move(d), {}, {}}, false);
    undo_.clear();
    redo_.clear();
}
std::vector<Paragraph> Editor::copySelection() const {
    validate(selection);
    const auto [start, end] = ordered(selection);
    std::vector<Paragraph> fragment;
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        const auto &source = document.paragraphs[i];
        Paragraph p;
        static_cast<ParagraphFormat &>(p) = source;
        p.style = source.style;
        p.direct = source.direct;
        p.pageBreakBefore = false;
        p.runs.clear();
        for (auto &run : slice(source, i == start.paragraph ? start.offset : 0,
                               i == end.paragraph ? end.offset : source.text().size()))
            if (!run.image())
                p.runs.push_back(std::move(run));
        if (p.runs.empty())
            p.runs.push_back({u"", source.runs.empty() ? CharacterStyle{} : source.runs[0].style});
        fragment.push_back(std::move(p));
    }
    return fragment;
}
void Editor::insertFragment(const std::vector<Paragraph> &fragment) {
    if (fragment.empty())
        return;
    for (const auto &p : fragment) {
        validateParagraphFormat(p);
        for (const auto &run : p.runs)
            validateCharacterStyle(run.style);
    }
    transact([&](Editor &e) {
        e.replace(u"");
        const bool targetEmpty = e.document.paragraphs[e.selection.focus.paragraph].text().empty();
        for (std::size_t i = 0; i < fragment.size(); ++i) {
            if (i)
                e.replace(u"\n");
            // A style this document lacks (pasted from elsewhere) arrives as direct formatting.
            if (i || targetEmpty)
                e.applyStyle(e.document.styles.find(fragment[i].style) ? fragment[i].style
                                                                       : std::string("Normal"));
            if (i || targetEmpty)
                e.formatParagraph([&](ParagraphFormat &format) {
                    format = fragment[i];
                    format.pageBreakBefore = false;
                });
            for (const auto &run : fragment[i].runs) {
                if (run.image() || run.text().empty())
                    continue;
                e.typingStyle = run.style;
                e.replace(run.text());
            }
        }
    });
}
} // namespace wf
