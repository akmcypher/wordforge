#include "core/document/document.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unordered_map>
#include <unordered_set>

namespace wf {
void validateCharacterStyle(const CharacterStyle &style) {
    const auto family = utf16(style.family);
    if (style.family.size() > 256 || !std::isfinite(style.size) || style.size < 1 ||
        style.size > 200 || static_cast<unsigned>(style.baseline) > 2 ||
        std::any_of(family.begin(), family.end(), [](auto ch) { return ch < 32; }) ||
        style.theme.index > 16 || style.theme.shade < -1 || style.theme.shade > 255 ||
        style.theme.tint < -1 || style.theme.tint > 255)
        throw std::invalid_argument("Invalid character formatting");
}
std::uint64_t Document::allocateId() {
    if (!nextId || nextId == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Document node identifiers exhausted");
    return nextId++;
}
void validatePreserved(const PreservedPackage &package) {
    auto printable = [](const std::string &text) {
        return !text.empty() && text.size() <= 255 &&
               std::all_of(text.begin(), text.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
    };
    // Paths Wordforge writes itself when saving DOCX.
    auto reserved = [](const std::string &path) {
        return path == "_rels/.rels" || path == "word/_rels/document.xml.rels" ||
               path == "word/document.xml" || path == "word/styles.xml" ||
               path == "word/numbering.xml" || path == "word/footnotes.xml" ||
               path == "word/comments.xml" || path.starts_with("word/media/") ||
               path == "word/header.xml" || path == "word/footer.xml" ||
               path == "word/headerFirst.xml" || path == "word/footerFirst.xml";
    };
    if (package.parts.size() > maxPreservedParts || package.relationships.size() > 64)
        throw std::invalid_argument("Too many preserved document parts");
    std::size_t total = 0;
    std::unordered_set<std::string> paths;
    for (const auto &part : package.parts) {
        const auto &path = part.path;
        bool safe = printable(path) && path.front() != '/' && path.back() != '/' &&
                    path.find("//") == std::string::npos;
        for (char c : path)
            safe = safe && (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' ||
                            c == '-' || c == '/');
        for (std::size_t start = 0; safe && start <= path.size();) {
            const auto end = std::min(path.find('/', start), path.size());
            const auto segment = path.substr(start, end - start);
            safe = segment != "." && segment != "..";
            start = end + 1;
        }
        const bool relationships = path.ends_with(".rels");
        if (!safe || reserved(path) || !paths.insert(path).second ||
            (relationships ? !part.contentType.empty() : !printable(part.contentType)))
            throw std::invalid_argument("Invalid preserved document part");
        total += part.data.size();
    }
    if (total > maxPreservedBytes)
        throw std::invalid_argument("Preserved document parts exceed 16 MiB");
    for (const auto &relationship : package.relationships)
        if (!printable(relationship.type) || !paths.contains(relationship.target) ||
            relationship.target.ends_with(".rels"))
            throw std::invalid_argument("Invalid preserved document relationship");
}
void validateDocument(const Document &document, const Document *validated) {
    validatePage(document.page);
    if (document.preserved)
        validatePreserved(*document.preserved);
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
    if (document.footnotes.size() > 10000)
        throw std::invalid_argument("Too many footnotes");
    for (const auto &note : document.footnotes) {
        if (!note.id || note.id >= document.nextId || !ids.insert(note.id).second)
            throw std::invalid_argument("Invalid footnote identifier");
        validateFootnoteText(note.text);
    }
    if (document.revisions.size() > 100000)
        throw std::invalid_argument("Too many tracked changes");
    for (const auto &revision : document.revisions) {
        if (!revision.id || revision.id >= document.nextId || !ids.insert(revision.id).second)
            throw std::invalid_argument("Invalid revision identifier");
        validateCommentAuthor(revision.author);
        if (revision.date.size() > 64 || std::any_of(revision.date.begin(), revision.date.end(),
                        [](char c) { return c < 0x20 || c > 0x7e; }))
            throw std::invalid_argument("Invalid revision date");
    }
    std::unordered_set<std::uint64_t> noteReferences;
    // Paragraphs shared with an already validated document (with the same styles) passed
    // the checks that depend on them alone; references to other parts are still checked.
    const auto &paragraphs = document.paragraphs;
    std::size_t prefix = 0, suffix = 0;
    if (validated && validated->styles == document.styles) {
        const auto &old = validated->paragraphs;
        while (prefix < old.size() && prefix < paragraphs.size() &&
               paragraphs.shares(prefix, old, prefix))
            ++prefix;
        while (suffix < old.size() - prefix && suffix < paragraphs.size() - prefix &&
               paragraphs.shares(paragraphs.size() - 1 - suffix, old, old.size() - 1 - suffix))
            ++suffix;
    }
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        const auto &paragraph = paragraphs[index];
        if (!paragraph.id || paragraph.id >= document.nextId)
            throw std::invalid_argument("Invalid or duplicate document node identifier");
        if (index < prefix || index >= paragraphs.size() - suffix) {
            // Shared paragraphs had distinct ids; they are checked against the rest below.
            for (const auto &run : paragraph.runs) {
                if (const auto *image = run.image(); image && !document.images.contains(image->resourceId))
                    throw std::invalid_argument("Missing image resource");
                for (const auto mark : {run.inserted, run.deleted})
                    if (mark && !document.findRevision(mark))
                        throw std::invalid_argument("Tracked change without a revision");
                if (const auto *note = run.note())
                    if (!document.footnote(note->id) || !noteReferences.insert(note->id).second)
                        throw std::invalid_argument(
                            "Footnote reference without a unique footnote");
            }
            continue;
        }
        if (!ids.insert(paragraph.id).second)
            throw std::invalid_argument("Invalid or duplicate document node identifier");
        validateParagraphFormat(paragraph);
        if (paragraph.runs.empty())
            throw std::invalid_argument("Paragraph requires a text run");
        // Inherited values must equal the style's: the style, not a stale copy, is authoritative.
        auto style = resolved.find(paragraph.style);
        if (style == resolved.end())
            style =
                resolved.emplace(paragraph.style, resolveStyle(document.styles, paragraph.style))
                    .first;
        if ((paragraph.direct & ~AllParagraphFields) ||
            (differingFields(static_cast<const ParagraphFormat &>(paragraph),
                             style->second.paragraph) &
             ~paragraph.direct))
            throw std::invalid_argument("Paragraph formatting disagrees with its style");
        for (const auto &run : paragraph.runs) {
            if ((run.direct & ~AllCharacterFields) ||
                (differingFields(run.style,
                                 inheritedCharacter(document.styles, style->second.character,
                                                               run.characterStyle)) &
                 ~run.direct))
                throw std::invalid_argument("Character formatting disagrees with its style");
            if (const auto *image = run.image()) {
                validateInlineImage(*image);
                if (!document.images.contains(image->resourceId))
                    throw std::invalid_argument("Missing image resource");
            }
            for (const auto mark : {run.inserted, run.deleted})
                if (mark && !document.findRevision(mark))
                    throw std::invalid_argument("Tracked change without a revision");
            if (run.tracked() && run.text().empty())
                throw std::invalid_argument("Empty tracked run");
            if (!run.link.empty())
                validateLink(run.link);
            if (const auto *note = run.note())
                if (!document.footnote(note->id) || !noteReferences.insert(note->id).second)
                    throw std::invalid_argument("Footnote reference without a unique footnote");
            utf8(run.text());
            for (auto ch : run.text())
                if (ch < 32 && ch != u'\t')
                    throw std::invalid_argument("Invalid character in paragraph");
            validateCharacterStyle(run.style);
        }
    }
    if (noteReferences.size() != document.footnotes.size())
        throw std::invalid_argument("A footnote has no reference in the text");
    std::unordered_set<std::string> names;
    if (!document.bookmarks.empty() || !document.comments.empty())
        (void)document.bookmarkPositions();
    if (document.comments.size() > 10000)
        throw std::invalid_argument("Too many comments");
    for (const auto &comment : document.comments) {
        if (!comment.id || comment.id >= document.nextId || !ids.insert(comment.id).second)
            throw std::invalid_argument("Invalid comment identifier");
        validateCommentText(comment.text);
        validateCommentAuthor(comment.author);
        if (comment.date.size() > 64 || std::any_of(comment.date.begin(), comment.date.end(),
                        [](char c) { return c < 0x20 || c > 0x7e; }))
            throw std::invalid_argument("Invalid comment date");
        const auto start = document.resolve(comment.start), end = document.resolve(comment.end);
        if (!start || !end || *end < *start)
            throw std::invalid_argument("Comment range is dangling or reversed");
    }
    if (document.fields.size() > 100000)
        throw std::invalid_argument("Too many fields");
    std::vector<std::pair<Position, Position>> fieldRanges;
    for (const auto &field : document.fields) {
        if (!field.id || field.id >= document.nextId || !ids.insert(field.id).second)
            throw std::invalid_argument("Invalid field identifier");
        validateFieldInstruction(field.instruction);
        const auto start = document.resolve(field.start), end = document.resolve(field.end);
        if (!start || !end || *end < *start)
            throw std::invalid_argument("Field range is dangling or reversed");
        fieldRanges.emplace_back(*start, *end);
    }
    // Fields nest or are apart; they never cross.
    std::sort(fieldRanges.begin(), fieldRanges.end(), [](const auto &a, const auto &b) {
        return a.first < b.first || (a.first == b.first && b.second < a.second);
    });
    std::vector<Position> open;
    for (const auto &[start, end] : fieldRanges) {
        while (!open.empty() && open.back() <= start && !(open.back() == start && start == end))
            open.pop_back();
        if (!open.empty() && open.back() < end)
            throw std::invalid_argument("Fields overlap without nesting");
        open.push_back(end);
    }
    for (const auto &paragraph : document.paragraphs)
        if (const auto &end = paragraph.sectionBreak) {
            if (paragraph.cell || !end->page.header.empty() || !end->page.footer.empty() ||
                end->page.differentFirstPage)
                throw std::invalid_argument("Invalid section break");
            validatePage(end->page);
        }
    for (const auto &bookmark : document.bookmarks) {
        validateBookmarkName(bookmark.name);
        if (!bookmark.id || bookmark.id >= document.nextId || !ids.insert(bookmark.id).second ||
            !names.insert(bookmark.name).second)
            throw std::invalid_argument("Invalid, duplicate or dangling bookmark");
    }
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        if (index == prefix)
            index = paragraphs.size() - suffix;
        if (index < paragraphs.size() && ids.contains(paragraphs[index].id))
            throw std::invalid_argument("Invalid or duplicate document node identifier");
    }
}
void validateParagraphFormat(const ParagraphFormat &value) {
    double previous = -1;
    if (value.tabs.size() > 64)
        throw std::invalid_argument("Too many tab stops");
    for (const auto &tab : value.tabs) {
        if (!std::isfinite(tab.position) || tab.position < 0 || tab.position <= previous || tab.position > 2880 ||
            static_cast<unsigned>(tab.alignment) > 2 || static_cast<unsigned>(tab.leader) > 1)
            throw std::invalid_argument("Invalid tab stop");
        previous = tab.position;
    }
    if (!std::isfinite(value.indentLeft) || value.indentLeft < 0 || value.indentLeft > 360 ||
        !std::isfinite(value.spaceBefore) || value.spaceBefore < 0 || value.spaceBefore > 200 ||
        !std::isfinite(value.spaceAfter) || value.spaceAfter < 0 || value.spaceAfter > 200 ||
        !std::isfinite(value.lineSpacing) || value.lineSpacing < 1 || value.lineSpacing > 3 ||
        !std::isfinite(value.indentFirstLine) || value.indentFirstLine < -value.indentLeft ||
        value.indentFirstLine > 360 || !std::isfinite(value.indentRight) || value.indentRight < 0 ||
        value.indentRight > 360 || static_cast<unsigned>(value.alignment) > 3 ||
        static_cast<unsigned>(value.list) > 2)
        throw std::invalid_argument("Invalid paragraph formatting");
}
void validatePage(const PageStyle &page) {
    if (!std::isfinite(page.width) || !std::isfinite(page.height) || !std::isfinite(page.margin) ||
        page.width < 72 || page.width > 2880 || page.height < 72 || page.height > 2880 ||
        page.margin < 0 || page.width - 2 * page.margin < 36 ||
        page.height - 2 * page.margin < 36 || page.columns < 1 || page.columns > 6 ||
        !std::isfinite(page.columnGap) || page.columnGap < 0 || page.columnGap > 144 ||
        (page.width - 2 * page.margin - (page.columns - 1) * page.columnGap) / page.columns < 36)
        throw std::invalid_argument("Invalid page geometry");
    for (const auto *band : {&page.header, &page.footer})
        for (const auto *text : {&band->left, &band->center, &band->right}) {
            if (text->size() > 500)
                throw std::invalid_argument("Header or footer text is too long");
            for (auto c : *text)
                if (c < 0x20 || c == 0x7f || c == 0xfffc)
                    throw std::invalid_argument("Header or footer text has control characters");
        }
}
std::u16string bandText(const std::u16string &text, std::size_t number, std::size_t count) {
    std::u16string result;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 6, u"{PAGE}") == 0) {
            result += utf16(std::to_string(number));
            i += 6;
        } else if (text.compare(i, 10, u"{NUMPAGES}") == 0) {
            result += utf16(std::to_string(count));
            i += 10;
        } else
            result += text[i++];
    }
    return result;
}
PageStyle sectionPage(const Document &document, std::size_t index) {
    for (auto i = index; i < document.paragraphs.size(); ++i)
        if (const auto &end = document.paragraphs[i].sectionBreak) {
            auto page = end->page;
            page.header = document.page.header;
            page.footer = document.page.footer;
            page.differentFirstPage = document.page.differentFirstPage;
            return page;
        }
    return document.page;
}
PageStyle Editor::sectionPage() const {
    return wf::sectionPage(document, selection.focus.paragraph);
}
void Editor::setPage(PageStyle page) {
    validatePage(page);
    checkpoint();
    auto geometry = page;
    geometry.header = geometry.footer = {};
    geometry.differentFirstPage = false;
    bool own = false;
    for (auto i = selection.focus.paragraph; i < document.paragraphs.size(); ++i)
        if (document.paragraphs[i].sectionBreak) {
            auto &ending = document.paragraphs.edit(i);
            ending.sectionBreak->page = geometry;
            ending.revision = serial_ + 1;
            own = true;
            break;
        }
    if (own) {
        document.page.header = page.header;
        document.page.footer = page.footer;
        document.page.differentFirstPage = page.differentFirstPage;
    } else
        document.page = page;
    document.revision = ++serial_;
}
void Editor::insertSectionBreak(bool continuous) {
    auto command = [&](Editor &e) {
        e.replace(u"");
        const auto at = e.selection.focus;
        if (e.document.paragraphs[at.paragraph].cell)
            throw std::invalid_argument("Section breaks cannot go inside a table");
        auto page = e.sectionPage();
        page.header = page.footer = {};
        page.differentFirstPage = false;
        e.replace(u"\n"); // the break ends the first part; the next section starts after it
        const auto ending = at.paragraph;
        e.document.paragraphs.edit(ending).sectionBreak = SectionBreak{page, continuous};
        e.document.paragraphs.edit(ending).revision = e.serial_ + 1;
        e.document.revision = ++e.serial_;
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
bool Editor::removeSectionBreak() {
    for (auto i = selection.focus.paragraph; i < document.paragraphs.size(); ++i)
        if (document.paragraphs[i].sectionBreak) {
            checkpoint();
            document.paragraphs.edit(i).sectionBreak.reset();
            document.paragraphs.edit(i).revision = serial_ + 1;
            document.revision = ++serial_;
            return true;
        }
    return false;
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
// The link of the characters on both sides of `offset` when it is the same, or empty.
std::string linkInside(const Paragraph &p, std::size_t offset) {
    std::string before, after;
    std::size_t at = 0;
    for (const auto &run : p.runs) {
        const auto end = at + run.text().size();
        if (offset > at && offset <= end)
            before = run.link;
        if (offset >= at && offset < end)
            after = run.link;
        at = end;
    }
    return before == after ? before : std::string();
}
void append(std::vector<Run> &runs, Run run) {
    if (run.text().empty())
        return;
    if (!run.atom() && !runs.empty() && !runs.back().atom() && runs.back().style == run.style &&
        runs.back().direct == run.direct && runs.back().characterStyle == run.characterStyle &&
        runs.back().link == run.link && runs.back().inserted == run.inserted &&
        runs.back().deleted == run.deleted)
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
// Gives a paragraph a style and drops all its direct formatting; runs keep their
// character styles.
void setStyle(Paragraph &paragraph, const std::string &id, const ResolvedStyle &style,
              const StyleSheet &sheet) {
    const bool pageBreak = paragraph.pageBreakBefore;
    static_cast<ParagraphFormat &>(paragraph) = style.paragraph;
    paragraph.pageBreakBefore = pageBreak;
    paragraph.style = id;
    paragraph.direct = 0;
    for (auto &run : paragraph.runs) {
        run.style = inheritedCharacter(sheet, style.character, run.characterStyle);
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
    validateDocument(candidate.document, &document);
    candidate.validate(candidate.selection);
    validateCharacterStyle(candidate.typingStyle);
    ChangeSet changes;
    changes.beforeRevision = document.revision;
    changes.pageChanged = document.page != candidate.document.page;
    changes.bookmarksChanged = document.bookmarks != candidate.document.bookmarks;
    changes.resourcesChanged = document.images != candidate.document.images;
    changes.tablesChanged = document.tables != candidate.document.tables;
    changes.notesChanged = document.footnotes != candidate.document.footnotes;
    changes.commentsChanged = document.comments != candidate.document.comments;
    changes.fieldsChanged = document.fields != candidate.document.fields;
    changes.revisionsChanged = document.revisions != candidate.document.revisions ||
                               document.trackChanges != candidate.document.trackChanges;
    for (const auto &revision : candidate.document.revisions)
        if (revision.id < idHighWater_ &&
            std::none_of(
                document.revisions.begin(), document.revisions.end(),
                         [&](const Revision &old) { return old.id == revision.id; }))
            throw std::invalid_argument("Transaction reuses a retired revision identifier");
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
    // Paragraphs both documents still share are unchanged; only the span between the
    // shared prefix and the shared suffix is compared.
    const auto &before = document.paragraphs;
    const auto &after = std::as_const(candidate.document.paragraphs);
    std::size_t prefix = 0, suffix = 0;
    while (prefix < before.size() && prefix < after.size() && after.shares(prefix, before, prefix))
        ++prefix;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
           after.shares(after.size() - 1 - suffix, before, before.size() - 1 - suffix))
        ++suffix;
    std::unordered_map<std::uint64_t, const Paragraph *> previous;
    for (auto index = prefix; index < before.size() - suffix; ++index)
        previous.emplace(before[index].id, &before[index]);
    std::vector<std::uint64_t> previousOrder, nextOrder;
    std::vector<std::size_t> dirty;
    std::unordered_set<std::uint64_t> remaining;
    for (auto index = prefix; index < after.size() - suffix; ++index) {
        const auto &paragraph = after[index];
        remaining.insert(paragraph.id);
        const auto found = previous.find(paragraph.id);
        if (found == previous.end()) {
            if (paragraph.id < idHighWater_)
                throw std::invalid_argument("Transaction reuses a retired document identifier");
            changes.inserted.push_back(paragraph.id);
            dirty.push_back(index);
        } else {
            nextOrder.push_back(paragraph.id);
            if (&paragraph != found->second && paragraph != *found->second) {
                changes.changed.push_back(paragraph.id);
                dirty.push_back(index);
            }
        }
    }
    for (auto index = prefix; index < before.size() - suffix; ++index)
        if (!remaining.contains(before[index].id))
            changes.removed.push_back(before[index].id);
        else
            previousOrder.push_back(before[index].id);
    changes.orderChanged = previousOrder != nextOrder;
    const bool contentChanged =
        changes.pageChanged || changes.orderChanged || changes.bookmarksChanged ||
        changes.resourcesChanged || changes.tablesChanged || changes.notesChanged ||
        changes.commentsChanged || changes.fieldsChanged || changes.revisionsChanged ||
        !changes.inserted.empty() || !changes.removed.empty() || !changes.changed.empty();
    if (contentChanged) {
        candidate.document.revision = ++candidate.serial_;
        for (const auto index : dirty)
            candidate.document.paragraphs.edit(index).revision = candidate.document.revision;
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
    // Undo and redo bring back paragraphs exactly as they were at their revisions, so those
    // keep them (and layout reuses what is unchanged). A new document's paragraphs are
    // stamped afresh: their revisions came from elsewhere.
    if (!preserveAllocator)
        for (std::size_t i = 0; i < document.paragraphs.size(); ++i)
            document.paragraphs.edit(i).revision = serial_;
}
void Editor::replace(const std::u16string &input) {
    if (document.trackChanges && !untracked_)
        tracked([&](Editor &e) { e.trackedReplace(input, e.selection); });
    else
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
    if (start != end && (!document.images.empty() || !document.footnotes.empty()))
        for (auto index = start.paragraph; index <= end.paragraph; ++index) {
            std::size_t offset = 0;
            const auto first = index == start.paragraph ? start.offset : 0;
            const auto last =
                index == end.paragraph ? end.offset : document.paragraphs[index].text().size();
            for (const auto &run : document.paragraphs[index].runs) {
                if (run.atom() && first < offset + 1 && last > offset)
                    removedImage = true;
                offset += run.text().size();
            }
        }
    auto prefix = slice(document.paragraphs[start.paragraph], 0, start.offset);
    auto suffix = slice(document.paragraphs[end.paragraph], end.offset,
                        document.paragraphs[end.paragraph].text().size());
    Paragraph prototype = document.paragraphs[start.paragraph];
    // The paragraph mark that survives is the last one: it keeps the section break.
    const auto endingBreak = document.paragraphs[end.paragraph].sectionBreak;
    prototype.sectionBreak.reset();
    const auto inherited = resolveStyle(document.styles, prototype.style).character;
    // Text joining from another paragraph keeps its look under this paragraph's style.
    if (end.paragraph != start.paragraph)
        for (auto &run : suffix)
            run.direct |= differingFields(
                run.style, inheritedCharacter(document.styles, inherited, run.characterStyle));
    // Typed text joins a link only inside it, not at its start or end (as in Word).
    const auto typedLink =
        forcedLink_ ? *forcedLink_ : linkInside(document.paragraphs[start.paragraph], start.offset);
    // Right after a link, typed text drops the link's blue underline too.
    auto typing = typingStyle;
    if (const auto &before = document.runAt(start);
        !forcedLink_ && typedLink.empty() && !before.link.empty() && typing == before.style) {
        const auto plain = inheritedCharacter(
            document.styles, resolveStyle(document.styles, prototype.style).character,
                                              before.characterStyle);
        typing.color = plain.color;
        typing.theme = plain.theme;
        typing.underline = plain.underline;
    }
    // Typed text continues the character style of the run it is typed into.
    const auto typedCharacterStyle =
        forcedCharacterStyle_ ? *forcedCharacterStyle_ : document.runAt(start).characterStyle;
    const auto typedInherited = inheritedCharacter(document.styles, inherited, typedCharacterStyle);
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
            inserted.direct |=
                differingFields(inserted.style, inheritedCharacter(document.styles, inherited,
                                                                   inserted.characterStyle));
            append(p.runs, std::move(inserted));
        } else {
            auto typed = styledRun(text.substr(offset, split - offset), typing, typedInherited);
            typed.characterStyle = typedCharacterStyle;
            typed.link = typedLink;
            append(p.runs, std::move(typed));
        }
        const auto caretOffset = (replacement.empty() ? start.offset : 0) + split - offset;
        if (last) {
            for (auto &run : suffix)
                append(p.runs, std::move(run));
            p.sectionBreak = endingBreak;
        }
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
    if (document.trackChanges && !untracked_) {
        tracked([](Editor &e) { e.trackedErase(true); });
        return;
    }
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
    if (document.trackChanges && !untracked_) {
        tracked([](Editor &e) { e.trackedErase(false); });
        return;
    }
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
        auto &p = document.paragraphs.edit(i);
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
        auto &paragraph = document.paragraphs.edit(i);
        paragraph.direct |=
            differingFields(static_cast<const ParagraphFormat &>(paragraph), updated[i - first]);
        static_cast<ParagraphFormat &>(paragraph) = updated[i - first];
        paragraph.revision = serial_;
    }
}
void Editor::applyPreset(ParagraphPreset preset) {
    applyStyle(presetStyleId(preset));
}
void Editor::applyStyle(const std::string &id) {
    validate(selection);
    const auto style = resolveStyle(document.styles, id);
    const auto [first, last] = selectedParagraphs(selection);
    checkpoint();
    document.revision = ++serial_;
    for (auto i = first; i <= last; ++i) {
        setStyle(document.paragraphs.edit(i), id, style, document.styles);
        document.paragraphs.edit(i).revision = serial_;
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
        auto &p = document.paragraphs.edit(i);
        const auto inherited = resolveStyle(document.styles, p.style).character;
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : p.text().size();
        auto runs = slice(p, 0, a);
        for (auto &run : slice(p, a, b)) {
            run.style = inherited;
            run.direct = 0;
            run.characterStyle.clear();
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
    for (std::size_t index = 0; index < document.paragraphs.size(); ++index) {
        auto paragraph = document.paragraphs[index];
        auto found = resolved.find(paragraph.style);
        if (found == resolved.end())
            found =
                resolved.emplace(paragraph.style, resolveStyle(document.styles, paragraph.style))
                    .first;
        const auto before = paragraph;
        inheritFields(paragraph, found->second.paragraph, paragraph.direct);
        for (auto &run : paragraph.runs)
            inheritFields(
                run.style,
                inheritedCharacter(document.styles, found->second.character, run.characterStyle),
                          run.direct);
        if (paragraph != before) {
            paragraph.revision = serial_ + 1;
            document.paragraphs.edit(index) = std::move(paragraph);
        }
    }
}
void Editor::updateStyleFromSelection() {
    validate(selection);
    const auto &source = document.paragraphs[selection.focus.paragraph];
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
    auto &edited = document.paragraphs.edit(selection.focus.paragraph);
    edited.direct = differingFields(static_cast<const ParagraphFormat &>(edited), now.paragraph);
    for (auto &run : edited.runs)
        run.direct = differingFields(
            run.style, inheritedCharacter(document.styles, now.character, run.characterStyle));
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
        auto &paragraph = document.paragraphs.edit(i);
        paragraph.style = style.id;
        paragraph.direct =
            differingFields(static_cast<const ParagraphFormat &>(paragraph), resolved.paragraph);
        for (auto &run : paragraph.runs)
            run.direct =
                differingFields(run.style, inheritedCharacter(document.styles, resolved.character,
                                                              run.characterStyle));
        paragraph.revision = serial_ + 1;
    }
    document.revision = ++serial_;
    return style.id;
}
void Editor::renameStyle(const std::string &id, const std::string &name) {
    if (isBuiltinStyle(id))
        throw std::invalid_argument("Built-in styles cannot be renamed");
    auto styles = document.styles;
    std::string *current = nullptr;
    if (auto *paragraph = styles.find(id))
        current = &paragraph->name;
    else if (auto *character = styles.findCharacter(id))
        current = &character->name;
    else
        throw std::invalid_argument("Unknown style: " + id);
    if (*current == name)
        return;
    *current = name;
    validateStyles(styles);
    checkpoint();
    document.styles = std::move(styles);
    document.revision = ++serial_;
}
void Editor::deleteStyle(const std::string &id) {
    if (isBuiltinStyle(id))
        throw std::invalid_argument("Built-in styles cannot be deleted");
    if (const auto *character = document.styles.findCharacter(id)) {
        const auto removed = *character;
        auto styles = document.styles;
        std::erase_if(styles.characterStyles, [&](const auto &style) { return style.id == id; });
        for (auto &style : styles.characterStyles)
            if (style.basedOn == id) {
                fillUnset(style.character, removed.character);
                style.basedOn = removed.basedOn;
            }
        validateStyles(styles);
        checkpoint();
        document.styles = std::move(styles);
        document.paragraphs.editEach([&](Paragraph &paragraph) {
            for (auto &run : paragraph.runs)
                if (run.characterStyle == id) {
                    run.characterStyle = removed.basedOn;
                    paragraph.revision = serial_ + 1;
                }
        });
        restyle();
        typingStyle = document.styleAt(selection.focus);
        document.revision = ++serial_;
        return;
    }
    const auto *found = document.styles.find(id);
    if (!found)
        throw std::invalid_argument("Unknown style: " + id);
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
    document.paragraphs.editEach([&](Paragraph &paragraph) {
        if (paragraph.style == id)
            paragraph.style = replacement;
    });
    restyle();
    typingStyle = document.styleAt(selection.focus);
    document.revision = ++serial_;
}
void Editor::applyCharacterStyle(const std::string &id) {
    validate(selection);
    if (!id.empty() && !document.styles.findCharacter(id))
        throw std::invalid_argument("Unknown character style: " + id);
    const auto [start, end] = ordered(selection);
    const auto caret =
        resolveStyle(document.styles, document.paragraphs[selection.focus.paragraph].style);
    if (start == end) {
        typingStyle = inheritedCharacter(document.styles, caret.character, id);
        return;
    }
    checkpoint();
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        auto &p = document.paragraphs.edit(i);
        const auto paragraphCharacter = resolveStyle(document.styles, p.style).character;
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : p.text().size();
        auto runs = slice(p, 0, a);
        for (auto &run : slice(p, a, b)) {
            run.characterStyle = id;
            run.style = inheritedCharacter(document.styles, paragraphCharacter, id);
            run.direct = 0;
            append(runs, std::move(run));
        }
        for (auto &run : slice(p, b, p.text().size()))
            append(runs, std::move(run));
        if (runs.empty())
            runs.push_back({u"", paragraphCharacter});
        p.runs = std::move(runs);
        p.revision = serial_ + 1;
    }
    typingStyle = document.styleAt(selection.focus);
    document.revision = ++serial_;
}
std::string Editor::createCharacterStyleFromSelection(const std::string &name) {
    validate(selection);
    const auto [start, end] = ordered(selection);
    if (start == end)
        throw std::invalid_argument(
            "Select the text whose formatting the character style should use");
    // The look of the first selected text, relative to its paragraph style.
    auto first = start;
    while (first.offset == document.paragraphs[first.paragraph].text().size() && first < end)
        first = {first.paragraph + 1, 0};
    const auto &paragraph = document.paragraphs[first.paragraph];
    const auto paragraphCharacter = resolveStyle(document.styles, paragraph.style).character;
    const auto selected = slice(paragraph, first.offset, paragraph.text().size());
    const auto &look = selected.empty() ? document.styleAt(first) : selected.front().style;
    auto styles = document.styles;
    NamedCharacterStyle style{uniqueStyleId(styles, name), name, "", {}};
    style.character = propertiesFor(paragraphCharacter, look);
    styles.characterStyles.push_back(style);
    validateStyles(styles);
    checkpoint();
    document.styles = std::move(styles);
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        auto &p = document.paragraphs.edit(i);
        const auto inherited = inheritedCharacter(
            document.styles, resolveStyle(document.styles, p.style).character, style.id);
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : p.text().size();
        auto runs = slice(p, 0, a);
        for (auto &run : slice(p, a, b)) {
            // Same appearance; only what differs from the new style stays direct.
            run.characterStyle = style.id;
            run.direct = differingFields(run.style, inherited);
            append(runs, std::move(run));
        }
        for (auto &run : slice(p, b, p.text().size()))
            append(runs, std::move(run));
        if (!runs.empty())
            p.runs = std::move(runs);
        p.revision = serial_ + 1;
    }
    document.revision = ++serial_;
    return style.id;
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
        auto &created = document.paragraphs.edit(selection.focus.paragraph);
        setStyle(created, next, resolved, document.styles);
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
    const auto initialStyle = d.styleAt({0, 0});
    restore({std::move(d), {}, initialStyle}, false);
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
            if (!run.atom() && !run.deleted) {
                run.inserted = 0;
                p.runs.push_back(std::move(run));
            }
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
                if (run.atom() || run.text().empty())
                    continue;
                e.typingStyle = run.style;
                const auto characterStyle = e.document.styles.findCharacter(run.characterStyle)
                                                ? run.characterStyle
                                                : std::string();
                e.forcedCharacterStyle_ = &characterStyle;
                e.forcedLink_ = &run.link;
                e.replace(run.text());
                e.forcedCharacterStyle_ = nullptr;
                e.forcedLink_ = nullptr;
            }
        }
    });
}
// Tracked changes.
namespace {
struct Raise {
    bool &flag;
    bool saved;
    explicit Raise(bool &value) : flag(value), saved(value) {
        flag = true;
    }
    ~Raise() {
        flag = saved;
    }
};
// Runs of text with the same tracked change, as [begin, end) offsets in one paragraph.
std::vector<std::pair<std::size_t, std::size_t>> changeSpans(const Paragraph &paragraph) {
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    const Run *previous = nullptr;
    std::size_t offset = 0;
    for (const auto &run : paragraph.runs) {
        const auto size = run.text().size();
        if (run.tracked()) {
            if (previous && !spans.empty() && spans.back().second == offset &&
                previous->inserted == run.inserted && previous->deleted == run.deleted)
                spans.back().second += size;
            else
                spans.emplace_back(offset, offset + size);
        }
        previous = &run;
        offset += size;
    }
    return spans;
}
} // namespace
const Revision *Document::findRevision(std::uint64_t id) const {
    for (const auto &revision : revisions)
        if (revision.id == id)
            return &revision;
    return nullptr;
}
CharacterStyle shownStyle(const Run &run) {
    auto style = run.style;
    if (run.inserted)
        style.underline = true;
    if (run.deleted)
        style.strike = true;
    if (run.tracked())
        style.color = 0xffb3261e;
    return style;
}
void Editor::tracked(const std::function<void(Editor &)> &command) {
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
void Editor::setTrackChanges(bool on) {
    tracked([on](Editor &e) { e.document.trackChanges = on; });
}
std::uint64_t Editor::currentRevision() {
    for (const auto &revision : document.revisions)
        if (revision.author == reviewer && revision.date == reviewDate)
            return revision.id;
    validateCommentAuthor(reviewer);
    if (reviewDate.size() > 64)
        throw std::invalid_argument("Invalid revision date");
    document.nextId = std::max(document.nextId, idHighWater_);
    const auto id = document.allocateId();
    document.revisions.push_back({id, reviewer, reviewDate});
    return id;
}
void Editor::markRuns(std::size_t index, std::size_t from, std::size_t to,
                      const std::function<void(Run &)> &change) {
    if (from >= to)
        return;
    auto &paragraph = document.paragraphs.edit(index);
    const auto size = paragraph.text().size();
    auto runs = slice(paragraph, 0, from);
    for (auto run : slice(paragraph, from, to)) {
        change(run);
        append(runs, std::move(run));
    }
    for (auto run : slice(paragraph, to, size))
        append(runs, std::move(run));
    paragraph.runs = std::move(runs);
}
std::size_t Editor::resolveRange(Position start, Position end,
                                 const std::function<bool(Run &)> &decide) {
    Raise guard(untracked_);
    std::size_t droppedAtEnd = 0;
    for (auto i = end.paragraph + 1; i-- > start.paragraph;) {
        auto &paragraph = document.paragraphs.edit(i);
        const auto size = paragraph.text().size();
        const auto from = i == start.paragraph ? start.offset : 0;
        const auto to = i == end.paragraph ? end.offset : size;
        if (from >= to)
            continue;
        std::vector<std::pair<std::size_t, std::size_t>> drops;
        auto runs = slice(paragraph, 0, from);
        std::size_t offset = from;
        for (auto run : slice(paragraph, from, to)) {
            const auto length = run.text().size();
            auto changed = run;
            if (decide(changed)) {
                if (!drops.empty() && drops.back().second == offset)
                    drops.back().second += length;
                else
                    drops.emplace_back(offset, offset + length);
                append(runs, std::move(run));
            } else
                append(runs, std::move(changed));
            offset += length;
        }
        for (auto run : slice(paragraph, to, size))
            append(runs, std::move(run));
        paragraph.runs = std::move(runs);
        for (auto drop = drops.rbegin(); drop != drops.rend(); ++drop) {
            replaceRange(u"", {{i, drop->first}, {i, drop->second}});
            if (i == end.paragraph)
                droppedAtEnd += drop->second - drop->first;
        }
    }
    return droppedAtEnd;
}
Position Editor::trackedDelete(Position start, Position end) {
    const auto cell = document.paragraphs[start.paragraph].cell;
    for (auto i = start.paragraph; i <= end.paragraph; ++i)
        if (document.paragraphs[i].cell != cell)
            throw std::invalid_argument(
                "Selection crosses table cell boundaries; edit one cell or use table controls");
    const auto id = currentRevision();
    const auto dropped = resolveRange(start, end, [&](Run &run) {
        if (run.deleted)
            return false;
        if (run.inserted) // the reviewer's own insertion simply goes
            if (const auto *revision = document.findRevision(run.inserted);
                revision && revision->author == reviewer)
                return true;
        run.deleted = id;
        return false;
    });
    return {end.paragraph, end.offset - dropped};
}
void Editor::trackedReplace(const std::u16string &input, Selection range) {
    Raise guard(untracked_);
    validate(range);
    const auto [start, end] = ordered(range);
    const auto caret = start == end ? start : trackedDelete(start, end);
    selection = {caret, caret};
    if (input.empty())
        return;
    replaceRange(input, selection);
    const auto id = currentRevision();
    const auto finish = selection.focus;
    for (auto i = caret.paragraph; i <= finish.paragraph; ++i)
        markRuns(i, i == caret.paragraph ? caret.offset : 0,
                 i == finish.paragraph ? finish.offset : document.paragraphs[i].text().size(),
                 [id](Run &run) { run.inserted = id; });
}
void Editor::trackedErase(bool backward) {
    Raise guard(untracked_);
    auto [start, end] = ordered(selection);
    if (start != end) {
        trackedDelete(start, end);
        selection = {start, start};
        return;
    }
    const auto &paragraph = document.paragraphs[start.paragraph];
    const auto size = paragraph.text().size();
    // Paragraph marks are not tracked: at a paragraph's edge the caret just moves on.
    if (backward && !start.offset) {
        if (start.paragraph && document.paragraphs[start.paragraph - 1].cell == paragraph.cell) {
            const Position previous{start.paragraph - 1,
                                    document.paragraphs[start.paragraph - 1].text().size()};
            selection = {previous, previous};
        }
        return;
    }
    if (!backward && start.offset == size) {
        if (start.paragraph + 1 < document.paragraphs.size() &&
            document.paragraphs[start.paragraph + 1].cell == paragraph.cell)
            selection = {Position{start.paragraph + 1, 0}, Position{start.paragraph + 1, 0}};
        return;
    }
    if (backward)
        start.offset = previousGrapheme(paragraph, start.offset);
    else
        end.offset = nextGrapheme(paragraph, end.offset);
    const auto after = trackedDelete(start, end);
    selection.anchor = selection.focus = backward ? start : after;
}
std::optional<Selection> Editor::changeAtCaret() const {
    const auto caret = selection.focus;
    if (caret.paragraph >= document.paragraphs.size())
        return {};
    for (const auto &[begin, end] : changeSpans(document.paragraphs[caret.paragraph]))
        if (begin <= caret.offset && caret.offset <= end)
            return Selection{{caret.paragraph, begin}, {caret.paragraph, end}};
    return {};
}
bool Editor::selectionHasChanges() const {
    const auto [start, end] = ordered(selection);
    if (start == end)
        return changeAtCaret().has_value();
    for (auto i = start.paragraph; i <= end.paragraph; ++i)
        for (const auto &[begin, finish] : changeSpans(document.paragraphs[i]))
            if ((i != start.paragraph || finish > start.offset) &&
                (i != end.paragraph || begin < end.offset))
                return true;
    return false;
}
bool Editor::resolveChanges(bool accept, bool all) {
    Position start, end;
    if (all) {
        end = {document.paragraphs.size() - 1, document.paragraphs.back().text().size()};
    } else if (selection.anchor != selection.focus) {
        std::tie(start, end) = ordered(selection);
    } else if (const auto span = changeAtCaret()) {
        start = span->anchor;
        end = span->focus;
    } else
        return false;
    bool found = false;
    tracked([&](Editor &e) {
        e.resolveRange(start, end, [&](Run &run) {
            if (!run.tracked())
                return false;
            found = true;
            if (accept ? run.deleted != 0 : run.inserted != 0)
                return true;
            run.inserted = run.deleted = 0;
            return false;
        });
        e.selection = {start, start};
    });
    return found;
}
bool Editor::acceptChanges() {
    return resolveChanges(true, false);
}
bool Editor::rejectChanges() {
    return resolveChanges(false, false);
}
void Editor::acceptAllChanges() {
    resolveChanges(true, true);
}
void Editor::rejectAllChanges() {
    resolveChanges(false, true);
}
bool Editor::nextChange() {
    const auto from = std::max(selection.anchor, selection.focus);
    std::optional<Selection> first;
    for (std::size_t i = 0; i < document.paragraphs.size(); ++i)
        for (const auto &[begin, end] : changeSpans(document.paragraphs[i])) {
            const Selection span{{i, begin}, {i, end}};
            if (!first)
                first = span;
            if (span.anchor >= from && span.focus > from) {
                selection = span;
                return true;
            }
        }
    if (first)
        selection = *first;
    return first.has_value();
}
} // namespace wf

namespace wf {
constexpr std::uint32_t linkColor = 0xff0563c1; // Word's Hyperlink style colour
void validateLink(const std::string &target) {
    if (target.empty() || target.size() > 2048)
        throw std::invalid_argument("Invalid link target");
    for (unsigned char ch : target)
        if (ch <= 32 || ch == 127)
            throw std::invalid_argument("Invalid link target");
    if (target[0] == '#') {
        validateBookmarkName(target.substr(1));
        return;
    }
    auto lower = target.substr(0, 8);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char *scheme : {"http://", "https://", "mailto:", "ftp://", "file:///"})
        if (lower.starts_with(scheme) && target.size() > std::string_view(scheme).size())
            return;
    throw std::invalid_argument("Unsupported link target");
}
bool externalLink(const std::string &target) {
    auto lower = target.substr(0, 8);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.starts_with("http://") || lower.starts_with("https://") ||
           lower.starts_with("mailto:");
}
std::string Editor::linkAt() const {
    const auto [start, end] = ordered(selection);
    const auto &p = document.paragraphs.at(start.paragraph);
    std::size_t at = 0;
    // The character after the caret, or before it at the paragraph end.
    const auto *found = &p.runs.back();
    for (const auto &run : p.runs) {
        if (start.offset >= at && start.offset < at + run.text().size()) {
            found = &run;
            break;
        }
        at += run.text().size();
    }
    return found->link;
}
std::optional<Selection> Editor::linkRange() const {
    const auto link = linkAt();
    if (link.empty())
        return std::nullopt;
    const auto [start, end] = ordered(selection);
    const auto &p = document.paragraphs.at(start.paragraph);
    // Grow from the caret over neighbouring runs with the same target.
    std::size_t at = 0, from = 0, to = 0;
    bool open = false, covered = false;
    for (const auto &run : p.runs) {
        const auto next = at + run.text().size();
        if (run.link == link) {
            if (!open)
                from = at;
            open = true;
            to = next;
            if (start.offset >= at && start.offset <= next)
                covered = true;
        } else {
            if (covered)
                break;
            open = false;
        }
        at = next;
    }
    if (!covered)
        return std::nullopt;
    return Selection{{start.paragraph, from}, {start.paragraph, to}};
}
void Editor::setLink(const std::string &target, const std::u16string &text) {
    if (!inTransaction_) {
        transact([&](Editor &e) { e.setLink(target, text); });
        return;
    }
    validate(selection);
    if (!target.empty())
        validateLink(target);
    auto [start, end] = ordered(selection);
    if (start == end) {
        if (target.empty()) {
            if (const auto range = linkRange()) {
                const auto caret = selection;
                selection = *range;
                setLink({});
                selection = caret;
            }
            return;
        }
        const auto shown = text.empty() ? utf16(target) : text;
        if (shown.find(u'\n') != std::u16string::npos || shown.size() > 4096)
            throw std::invalid_argument("Invalid link text");
        replace(shown);
        start = selection.focus;
        start.offset -= shown.size();
        end = selection.focus;
    }
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        auto &p = document.paragraphs.edit(i);
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : p.text().size();
        const auto inherited = resolveStyle(document.styles, p.style).character;
        auto runs = slice(p, 0, a);
        for (auto &run : slice(p, a, b)) {
            if (run.note()) { // a footnote mark stays outside links
                append(runs, std::move(run));
                continue;
            }
            // Links look like Word's: blue and underlined; removing one restores the text.
            const auto plain = inheritedCharacter(document.styles, inherited, run.characterStyle);
            const auto before = run.style;
            if (!target.empty() && run.link.empty()) {
                run.style.color = linkColor;
                run.style.theme = {};
                run.style.underline = true;
            } else if (target.empty() && !run.link.empty() && run.style.color == linkColor &&
                       run.style.underline) {
                run.style.color = plain.color;
                run.style.theme = plain.theme;
                run.style.underline = plain.underline;
            }
            run.direct = (run.direct | differingFields(before, run.style)) &
                         differingFields(run.style, plain);
            run.link = target;
            append(runs, std::move(run));
        }
        for (auto &run : slice(p, b, p.text().size()))
            append(runs, std::move(run));
        if (!runs.empty())
            p.runs = std::move(runs);
        p.revision = serial_ + 1;
    }
    document.revision = ++serial_;
}
} // namespace wf

namespace wf {
void validateFieldInstruction(const std::string &instruction) {
    if (instruction.empty() || instruction.size() > 4096)
        throw std::invalid_argument("Invalid field instruction");
    for (unsigned char ch : instruction)
        if (ch < 32 || ch == 127)
            throw std::invalid_argument("Invalid field instruction");
    (void)utf16(instruction); // valid UTF-8
}
bool tableOfContentsField(const std::string &instruction) {
    const auto first = instruction.find_first_not_of(' ');
    if (first == std::string::npos)
        return false;
    auto word = instruction.substr(first, instruction.find(' ', first) - first);
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return word == "TOC";
}
namespace {
// The text of a table of contents for `document`: one line per heading.
struct TocEntries {
    std::u16string text;
    std::vector<bool> indented;
};
TocEntries tocEntries(const Document &document, const Editor::PageOf &pageOf,
                      const std::set<std::size_t> &skip) {
    TocEntries entries;
    for (std::size_t i = 0; i < document.paragraphs.size(); ++i) {
        const auto &p = document.paragraphs[i];
        if (skip.contains(i) || p.cell ||
            (p.style != "Title" && p.style != "Heading1" && p.style != "Heading2"))
            continue;
        std::u16string line;
        for (const auto &run : p.runs)
            if (!run.atom() && !run.deleted)
                line += run.text();
        for (auto &ch : line)
            if (ch == u'\t')
                ch = u' ';
        if (line.find_first_not_of(u' ') == std::u16string::npos)
            continue;
        if (pageOf)
            line += u'\t' + utf16(std::to_string(pageOf(i)));
        if (!entries.text.empty())
            entries.text += u'\n';
        entries.text += line;
        entries.indented.push_back(p.style == "Heading2");
    }
    if (entries.text.empty()) {
        entries.text = u"No table of contents entries found.";
        entries.indented.push_back(false);
    }
    return entries;
}
void formatTocEntry(Editor &e, std::size_t paragraph, bool indented, bool numbered) {
    const auto page = sectionPage(e.document, paragraph);
    const double width =
        (page.width - 2 * page.margin - (page.columns - 1) * page.columnGap) / page.columns;
    e.selection = {{paragraph, 0}, {paragraph, 0}};
    e.formatParagraph([&](ParagraphFormat &f) {
        f.indentLeft = indented ? 18 : 0;
        f.indentFirstLine = f.indentRight = 0;
        f.tabs = numbered ? std::vector<TabStop>{{width, TabAlignment::Right, TabLeader::Dots}}
                          : std::vector<TabStop>{};
    });
}
} // namespace
std::uint64_t Editor::insertTableOfContents(const PageOf &pageOf) {
    std::uint64_t id = 0;
    auto command = [&](Editor &e) {
        if (e.document.fields.size() >= 100000)
            throw std::invalid_argument("Too many fields");
        // Own paragraphs: break before (unless at an empty paragraph's start) and after.
        e.replace(u"");
        auto at = e.selection.focus;
        if (!e.document.paragraphs[at.paragraph].text().empty()) {
            if (at.offset)
                e.insertParagraphBreak();
            e.insertParagraphBreak();
            e.selection.focus = e.selection.anchor = {e.selection.focus.paragraph - 1, 0};
        }
        e.applyStyle("Normal");
        const auto first = e.selection.focus.paragraph;
        const auto entries = tocEntries(e.document, pageOf, {});
        e.replace(entries.text);
        const auto last = e.selection.focus;
        for (std::size_t k = 0; k < entries.indented.size(); ++k)
            formatTocEntry(e, first + k, entries.indented[k], bool(pageOf));
        e.document.nextId = std::max(e.document.nextId, e.idHighWater_);
        id = e.document.allocateId();
        e.document.fields.push_back({id, "TOC \\o \"1-3\" \\h \\z \\u",
                                     e.document.anchorAt({first, 0}, Affinity::Before),
                                     e.document.anchorAt(last, Affinity::After)});
        e.selection = {last, last};
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
    return id;
}
bool Editor::updateTablesOfContents(const PageOf &pageOf) {
    bool found = false;
    auto command = [&](Editor &e) {
        for (std::size_t n = 0; n < e.document.fields.size(); ++n) {
            if (!tableOfContentsField(e.document.fields[n].instruction))
                continue;
            found = true;
            const auto start = e.document.resolve(e.document.fields[n].start);
            const auto end = e.document.resolve(e.document.fields[n].end);
            if (!start || !end)
                continue;
            std::set<std::size_t> inside;
            for (auto i = start->paragraph; i <= end->paragraph; ++i)
                inside.insert(i);
            const auto entries = tocEntries(e.document, pageOf, inside);
            // Fields nested in the old entries (page references) go with them.
            const auto id = e.document.fields[n].id;
            std::erase_if(e.document.fields, [&](const Field &f) {
                if (f.id == id)
                    return false;
                const auto a = e.document.resolve(f.start), b = e.document.resolve(f.end);
                return a && b && *start <= *a && *b <= *end;
            });
            auto &field = *std::find_if(e.document.fields.begin(), e.document.fields.end(),
                                        [&](const Field &f) { return f.id == id; });
            e.selection = {*start, *end};
            e.replace(entries.text);
            const auto last = e.selection.focus;
            const auto first = last.paragraph + 1 - entries.indented.size();
            for (std::size_t k = 0; k < entries.indented.size(); ++k) {
                formatTocEntry(e, first + k, entries.indented[k], bool(pageOf));
            }
            field.start = e.document.anchorAt({first, start->offset}, Affinity::Before);
            field.end = e.document.anchorAt(last, Affinity::After);
            e.selection = {last, last};
            n = static_cast<std::size_t>(&field - e.document.fields.data());
        }
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
    return found;
}
} // namespace wf
