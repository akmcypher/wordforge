#include "core/layout/layout.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unicode/ubrk.h>
#include <unordered_set>
namespace wf {
namespace {
// Justified lines: spreads `extra` points over the line's spaces before `end` (trailing
// spaces excluded), moving every glyph, picture and run right of each widened space.
void justify(ShapedLine &line, const std::u16string &text, std::size_t end, double extra) {
    auto trailing = end;
    while (trailing > 0 && text[trailing - 1] == u' ')
        --trailing;
    auto stretches = [&](const Glyph &glyph) {
        return glyph.cluster < trailing && text[glyph.cluster] == u' ';
    };
    std::vector<double> spaces; // left edges of the stretchable spaces, in line coordinates
    for (const auto &run : line.runs)
        for (const auto &glyph : run.glyphs)
            if (stretches(glyph))
                spaces.push_back(run.x + glyph.x);
    if (spaces.empty() || extra <= 0)
        return;
    std::sort(spaces.begin(), spaces.end());
    const double each = extra / static_cast<double>(spaces.size());
    auto shift = [&](double x) { // total widening of the spaces that start left of x
        const auto count =
            std::lower_bound(spaces.begin(), spaces.end(), x - 1e-6) - spaces.begin();
        return each * static_cast<double>(count);
    };
    for (auto &run : line.runs) {
        const double oldX = run.x;
        run.x = oldX + shift(oldX + 1e-6);
        for (auto &glyph : run.glyphs) {
            const double at = oldX + glyph.x;
            glyph.x = at + shift(at) - run.x;
            if (stretches(glyph)) {
                glyph.advance += each;
                run.width += each;
            }
        }
    }
    line.width += extra;
}
void stops(Line &line, const std::vector<std::size_t> &paragraphBoundaries) {
    struct Cluster {
        double left = std::numeric_limits<double>::max(), right = 0;
    };
    for (const auto &run : line.shaped.runs) {
        if (run.glyphs.empty() || run.tab) {
            line.caretStops.emplace_back(run.start, run.x + (run.rtl ? run.width : 0));
            line.caretStops.emplace_back(run.end, run.x + (run.rtl ? 0 : run.width));
            continue;
        }
        std::map<std::size_t, Cluster> clusters;
        for (const auto &glyph : run.glyphs) {
            auto &cluster = clusters[glyph.cluster];
            cluster.left = std::min(cluster.left, run.x + glyph.x);
            cluster.right = std::max(cluster.right, run.x + glyph.x + glyph.advance);
        }
        for (auto it = clusters.begin(); it != clusters.end(); ++it) {
            const auto next = std::next(it);
            const auto end = next == clusters.end() ? run.end : next->first;
            std::vector<std::size_t> boundaries{it->first};
            for (auto p = std::upper_bound(paragraphBoundaries.begin(), paragraphBoundaries.end(),
                                           it->first);
                 p != paragraphBoundaries.end() && *p < end; ++p)
                boundaries.push_back(*p);
            boundaries.push_back(end);
            for (std::size_t n = 0; n < boundaries.size(); ++n) {
                auto fraction = static_cast<double>(n) / (boundaries.size() - 1);
                if (run.rtl)
                    fraction = 1 - fraction;
                line.caretStops.emplace_back(boundaries[n],
                                             it->second.left +
                                                 fraction * (it->second.right - it->second.left));
            }
        }
    }
    if (line.caretStops.empty())
        line.caretStops.emplace_back(line.start, 0);
}
} // namespace
Layout LayoutEngine::compose(const Document &document) {
    validatePage(document.page);
    // Each paragraph's section settings, from the section break that ends its section.
    std::vector<PageStyle> sections(document.paragraphs.size());
    {
        auto current = document.page;
        for (auto i = document.paragraphs.size(); i-- > 0;) {
            if (document.paragraphs[i].sectionBreak)
                current = sectionPage(document, i);
            sections[i] = current;
        }
    }
    // The current section's page: text width, column width and height of the writing area.
    PageStyle page;
    double width = 0, columnWidth = 0, availableHeight = 0;
    auto useSection = [&](const PageStyle &style) {
        validatePage(style);
        page = style;
        width = page.width - 2 * page.margin;
        availableHeight = page.height - 2 * page.margin;
        columnWidth = (width - static_cast<double>(page.columns - 1) * page.columnGap) /
                      static_cast<double>(page.columns);
        if (!std::isfinite(width) || !std::isfinite(availableHeight) || columnWidth < 36 ||
            availableHeight < 36)
            throw std::runtime_error("Page margins leave insufficient writing space");
    };
    useSection(sections.empty() ? document.page : sections.front());
    Layout result;
    result.page = page;
    result.pageStyles = {page};
    result.images = document.images;
    double y = page.margin;
    std::size_t pageIndex = 0;
    // Columns: text fills `column` from `sectionTop` down, then the next column, then the
    // next page. `sectionBottom` is the lowest text of the section on this page.
    std::size_t column = 0;
    double sectionTop = page.margin, sectionBottom = page.margin;
    auto columnLeft = [&] {
        return page.margin + static_cast<double>(column) * (columnWidth + page.columnGap);
    };
    std::size_t listNumber = 0;
    std::unordered_set<std::uint64_t> liveIds;
    validateTables(document);
    auto measure = [&](const Paragraph &paragraph, double width, std::size_t listNumber,
                       std::size_t firstNote,
                       std::size_t firstEndnote = 1) -> const std::vector<Line> & {
        validateParagraphFormat(paragraph);
        liveIds.insert(paragraph.id);
        auto found = cache_.find(paragraph.id);
        if (found == cache_.end() || found->second.revision != paragraph.revision ||
            found->second.width != width || found->second.number != listNumber ||
            found->second.firstNote != firstNote || found->second.firstEndnote != firstEndnote) {
            ++result.measuredParagraphs;
            std::vector<Line> lines;
            const auto text = paragraph.text();
            const auto paragraphBoundaries = paragraph.boundaries();
            std::u16string markerText;
            ShapedLine marker;
            if (paragraph.list != ListKind::None) {
                markerText = paragraph.list == ListKind::Bullet
                                 ? u"•"
                                 : utf16(std::to_string(listNumber) + ".");
                Paragraph label;
                auto style =
                    paragraph.runs.empty() ? CharacterStyle{} : paragraph.runs.front().style;
                style.highlight = 0;
                style.strike = false;
                style.underline = false;
                label.runs = {{markerText, style}};
                marker = shaper_.shape(label, 0, markerText.size());
            }
            const double markerWidth = markerText.empty() ? 0 : std::max(18.0, marker.width + 4);
            // A list's first-line indent moves its bullet or number (and the text after it);
            // otherwise it moves the first line's text. Negative values hang.
            const double first = paragraph.indentFirstLine;
            const double markerLeft = paragraph.indentLeft + first;
            const double firstLeft =
                markerText.empty() ? paragraph.indentLeft + first
                                   : paragraph.indentLeft + std::max(0.0, first) + markerWidth;
            const double otherLeft = paragraph.indentLeft + markerWidth;
            const double right = width - paragraph.indentRight;
            auto lineLeft = [&](std::size_t from) { return from == 0 ? firstLeft : otherLeft; };
            auto shapeRange = [&](std::size_t from, std::size_t to) {
                return shaper_.shape(paragraph, from, to, firstNote, firstEndnote, lineLeft(from));
            };
            auto lineWidth = [&](std::size_t from) { return right - lineLeft(from); };
            const double textWidth = std::min(right - firstLeft, right - otherLeft);
            for (const auto &run : paragraph.runs)
                if (run.image() && run.image()->width > textWidth)
                    throw std::runtime_error("Picture is wider than this paragraph's writing area");
            if (textWidth < 36)
                throw std::runtime_error("Paragraph indent leaves insufficient writing space");
            UErrorCode error = U_ZERO_ERROR;
            std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> breaks(
                ubrk_open(UBRK_LINE, "und", reinterpret_cast<const UChar *>(text.data()),
                          static_cast<int32_t>(text.size()), &error),
                ubrk_close);
            if (U_FAILURE(error))
                throw std::runtime_error(u_errorName(error));
            std::size_t start = 0;
            auto complete = shapeRange(0, text.size());
            const bool singleLine = complete.width <= lineWidth(0);
            do {
                const double available = lineWidth(start);
                std::size_t fit = start;
                ShapedLine fitted = shapeRange(start, start);
                if (singleLine) {
                    fit = text.size();
                    fitted = std::move(complete);
                }
                if (!singleLine) {
                    for (const auto &run : complete.runs) {
                        if (!run.tab || run.tabAlignment == TabAlignment::Left || run.start <= start) continue;
                        auto prefix = shapeRange(start, run.start);
                        if (prefix.width <= available && run.start > fit) {
                            fit = run.start; fitted = std::move(prefix);
                        }
                    }
                }
                for (auto boundary =
                         singleLine ? UBRK_DONE
                                    : ubrk_following(breaks.get(), static_cast<int32_t>(start));
                     boundary != UBRK_DONE; boundary = ubrk_next(breaks.get())) {
                    if (!paragraph.tabs.empty() && boundary < static_cast<int32_t>(text.size()) &&
                        text[boundary - 1] == u'\t')
                        continue;
                    if (boundary < static_cast<int32_t>(text.size())) {
                        const auto tab = text.rfind(u'\t', boundary - 1);
                        const bool aligned = std::any_of(complete.runs.begin(), complete.runs.end(), [&](const auto &r) {
                            return r.tab && r.start == tab && r.tabAlignment != TabAlignment::Left;
                        });
                        if (tab != std::u16string::npos && tab >= start && aligned) {
                            const auto next = text.find(u'\t', tab + 1);
                            if (next == std::u16string::npos || static_cast<std::size_t>(boundary) < next) continue;
                        }
                    }
                    auto candidate = shapeRange(start, static_cast<std::size_t>(boundary));
                    if (candidate.width > available)
                        break;
                    if (static_cast<std::size_t>(boundary) > fit) {
                        fit = static_cast<std::size_t>(boundary);
                        fitted = std::move(candidate);
                    }
                }
                if (fit == start && start < text.size()) {
                    for (auto boundary = nextGrapheme(paragraph, start); boundary <= text.size();) {
                        auto candidate = shapeRange(start, boundary);
                        if (candidate.width > available && fit != start)
                            break;
                        fit = boundary;
                        fitted = std::move(candidate);
                        if (boundary == text.size() || fitted.width > available)
                            break;
                        boundary = nextGrapheme(paragraph, boundary);
                    }
                }
                Line line;
                line.start = start;
                line.end = fit;
                line.shaped = std::move(fitted);
                line.height = (line.shaped.ascent + line.shaped.descent) * paragraph.lineSpacing;
                const double remaining = std::max(0.0, available - line.shaped.width);
                if (paragraph.alignment == Alignment::Justify && fit < text.size()) {
                    // The space ending the line hangs past the right edge, as in Word.
                    double hanging = 0;
                    for (const auto &run : line.shaped.runs)
                        for (const auto &glyph : run.glyphs)
                            if (glyph.cluster + 1 == fit && text[glyph.cluster] == u' ')
                                hanging += glyph.advance;
                    justify(line.shaped, text, fit, remaining + hanging);
                }
                line.x = lineLeft(start) + (paragraph.alignment == Alignment::Center ? remaining / 2
                          : paragraph.alignment == Alignment::Right ? remaining
                                                                    : 0);
                if (start == 0 && !markerText.empty()) {
                    line.markerText = markerText;
                    line.marker = std::move(marker);
                    line.markerX = markerLeft + markerWidth - 4 - line.marker.width;
                }
                stops(line, paragraphBoundaries);
                lines.push_back(std::move(line));
                start = fit;
            } while (start < text.size());
            cache_.insert_or_assign(paragraph.id,
                                    Cached{paragraph.revision, width, std::move(lines), listNumber,
                                           firstNote, firstEndnote});
            found = cache_.find(paragraph.id);
        }
        return found->second.lines;
    };
    // Footnotes: numbered in reference order, laid out at the bottom of the page holding
    // their reference, below a short rule. Pagination keeps room for them.
    constexpr double noteGap = 10;
    struct NoteBlock {
        std::vector<Line> lines;
        double height = 0;
    };
    auto noteStyle = resolveStyle(document.styles, "Normal").character;
    noteStyle.size = 10;
    noteStyle.bold = noteStyle.italic = noteStyle.underline = noteStyle.strike = false;
    noteStyle.highlight = 0;
    auto noteBlock = [&](std::uint64_t id, std::size_t number, bool endnote = false) {
        const auto *note = document.footnote(id);
        Paragraph text(id);
        text.revision = std::hash<std::u16string>{}(note->text);
        text.spaceBefore = text.spaceAfter = 0;
        text.lineSpacing = 1;
        text.runs = {Run::fromNote({id, endnote}, noteStyle), Run(u" " + note->text, noteStyle)};
        NoteBlock block;
        for (const auto &line :
             measure(text, width, 0, endnote ? 1 : number, endnote ? number : 1)) {
            block.height += line.height;
            block.lines.push_back(line);
        }
        return block;
    };
    // Footnotes referenced in [from, to) of a paragraph whose first note is `firstNote`.
    auto notesIn = [&](const Paragraph &p, std::size_t firstNote, std::size_t from,
                       std::size_t to) {
        std::vector<NoteBlock> blocks;
        std::size_t offset = 0, number = firstNote;
        for (const auto &run : p.runs) {
            if (const auto *note = run.note(); note && !note->endnote) {
                if (offset >= from && offset < to)
                    blocks.push_back(noteBlock(note->id, number));
                ++number;
            }
            offset += run.text().size();
        }
        return blocks;
    };
    auto countNotes = [](const Paragraph &p, bool endnotes = false) {
        return static_cast<std::size_t>(
            std::count_if(p.runs.begin(), p.runs.end(),
                          [&](const Run &r) { return r.note() && r.note()->endnote == endnotes; }));
    };
    // Endnotes: numbered in reference order, listed after the last paragraph.
    std::vector<std::uint64_t> endnoteIds;
    auto collectEndnotes = [&](const Paragraph &p) {
        for (const auto &run : p.runs)
            if (const auto *note = run.note(); note && note->endnote)
                endnoteIds.push_back(note->id);
    };
    std::vector<NoteBlock> pageNotes;
    double notesHeight = 0;
    std::size_t notesSoFar = 0;
    auto notesNeed = [&](const std::vector<NoteBlock> &blocks) {
        double needed = 0;
        for (const auto &block : blocks)
            needed += block.height;
        return needed > 0 && pageNotes.empty() ? needed + noteGap : needed;
    };
    // Footnotes span every column, so protect the deepest body content already placed.
    auto fitsWritingArea = [&](double bodyHeight, double newNotes, double before = 0) {
        const double rule = newNotes > 0 && pageNotes.empty() ? noteGap : 0;
        return std::max(y + before + bodyHeight, sectionBottom) + notesHeight + newNotes + rule <=
               page.height - page.margin;
    };
    auto addNotes = [&](std::vector<NoteBlock> &blocks) {
        notesHeight += notesNeed(blocks);
        for (auto &block : blocks)
            pageNotes.push_back(std::move(block));
    };
    auto flushNotes = [&] {
        if (pageNotes.empty())
            return;
        const double top = page.height - page.margin - notesHeight;
        result.noteSeparators.emplace_back(pageIndex, top + noteGap / 2);
        double cursor = top + noteGap;
        for (auto &block : pageNotes)
            for (auto &line : block.lines) {
                line.page = pageIndex;
                line.x += page.margin;
                line.markerX += page.margin;
                line.baseline = cursor + line.shaped.ascent;
                cursor += line.height;
                result.furniture.push_back(std::move(line));
            }
        pageNotes.clear();
        notesHeight = 0;
    };
    auto newPage = [&] {
        flushNotes();
        ++pageIndex;
        result.pageStyles.push_back(page);
        y = sectionTop = sectionBottom = page.margin;
        column = 0;
    };
    // Where text that does not fit goes: the next column, else the next page.
    auto advance = [&] {
        if (column + 1 < page.columns) {
            ++column;
            y = sectionTop;
        } else
            newPage();
    };
    for (std::size_t index = 0; index < document.paragraphs.size(); ++index) {
        const auto &paragraph = document.paragraphs[index];
        if (index && document.paragraphs[index - 1].sectionBreak) {
            const auto &next = sections[index];
            const bool sameSheet = next.width == page.width && next.height == page.height &&
                                   next.margin == page.margin;
            if (document.paragraphs[index - 1].sectionBreak->continuous && sameSheet) {
                // Continues on this page below the previous section's text.
                y = std::max(y, sectionBottom);
                useSection(next);
                result.pageStyles.back() = page;
                column = 0;
                sectionTop = sectionBottom = y;
            } else {
                flushNotes(); // with the previous section's page
                useSection(next);
                ++pageIndex;
                result.pageStyles.push_back(page);
                y = sectionTop = sectionBottom = page.margin;
                column = 0;
            }
        }
        if (paragraph.cell) {
            const auto cell = *paragraph.cell;
            const auto &table =
                *std::find_if(document.tables.begin(), document.tables.end(),
                              [&](const auto &value) { return value.id == cell.tableId; });
            const auto edges = columnEdges(table, columnWidth);
            // Rows tied together by a vertical merge are laid out (and paginated) together.
            auto groupEnd = cell.row + 1;
            for (bool grown = true; grown;) {
                grown = false;
                for (const auto &merge : table.merges)
                    if (merge.row >= cell.row && merge.row < groupEnd &&
                        merge.row + merge.rows > groupEnd) {
                        groupEnd = merge.row + merge.rows;
                        grown = true;
                    }
            }
            // One box per visible cell; covered cells' paragraphs go into their merged cell,
            // and empty ones overlay its last line so they add no height.
            struct Box {
                std::size_t row, column, rows = 1, columns = 1;
                std::vector<Line> lines;
                double height = 4;
                std::size_t number = 0, first = 0, last = 0;
                bool seen = false;
            };
            std::map<std::pair<std::size_t, std::size_t>, Box> boxes;
            std::vector<NoteBlock> groupNotes;
            auto end = index;
            for (; end < document.paragraphs.size(); ++end) {
                const auto &p = document.paragraphs[end];
                if (!p.cell || p.cell->tableId != cell.tableId || p.cell->row >= groupEnd)
                    break;
                const auto *merge = table.mergeAt(p.cell->row, p.cell->column);
                const auto row = merge ? merge->row : p.cell->row;
                const auto column = merge ? merge->column : p.cell->column;
                auto &box = boxes[{row, column}];
                if (!box.seen) {
                    box.row = row;
                    box.column = column;
                    box.rows = merge ? merge->rows : 1;
                    box.columns = merge ? merge->columns : 1;
                    box.first = end;
                    box.seen = true;
                }
                box.first = std::min(box.first, end);
                box.last = std::max(box.last, end);
                const bool ghost =
                    (row != p.cell->row || column != p.cell->column) && p.text().empty();
                const double boxWidth = edges[column + box.columns] - edges[column];
                if (!ghost)
                    box.height += p.spaceBefore;
                box.number = p.list == ListKind::Decimal ? box.number + 1 : 0;
                const auto firstNote = notesSoFar + 1;
                for (auto &block : notesIn(p, firstNote, 0, p.text().size()))
                    groupNotes.push_back(std::move(block));
                notesSoFar += countNotes(p);
                const auto firstEndnote = endnoteIds.size() + 1;
                collectEndnotes(p);
                for (auto line : measure(p, boxWidth - 8, box.number, firstNote, firstEndnote)) {
                    line.paragraph = end;
                    line.x += 4 + edges[column];
                    line.markerX += 4 + edges[column];
                    if (ghost) {
                        line.baseline =
                            std::max(4.0, box.height - line.height) + line.shaped.ascent;
                    } else {
                        line.baseline = box.height + line.shaped.ascent;
                        box.height += line.height;
                    }
                    box.lines.push_back(std::move(line));
                }
                if (!ghost)
                    box.height += p.spaceAfter;
            }
            // Row heights: single-row cells first, then spanning cells stretch their last row.
            std::vector<double> heights(groupEnd - cell.row, 24);
            for (const auto &[key, box] : boxes)
                if (box.rows == 1)
                    heights[box.row - cell.row] =
                        std::max(heights[box.row - cell.row], box.height + 4);
            for (const auto &[key, box] : boxes) {
                if (box.rows == 1)
                    continue;
                double spanned = 0;
                for (auto r = box.row; r < box.row + box.rows; ++r)
                    spanned += heights[r - cell.row];
                if (box.height + 4 > spanned)
                    heights[box.row + box.rows - 1 - cell.row] += box.height + 4 - spanned;
            }
            double groupHeight = 0;
            for (auto h : heights)
                groupHeight += h;
            if (groupHeight > availableHeight)
                throw std::runtime_error(groupEnd - cell.row > 1
                                             ? "Merged table rows are taller than the page; "
                                               "shorten their content or split the cells"
                                             : "A table row is taller than the page; shorten its "
                                               "content or increase the page size");
            if (y + groupHeight + notesHeight + notesNeed(groupNotes) > page.height - page.margin &&
                y > sectionTop)
                advance();
            addNotes(groupNotes);
            for (auto &[key, box] : boxes) {
                double top = y, height = 0;
                for (auto r = cell.row; r < box.row; ++r)
                    top += heights[r - cell.row];
                for (auto r = box.row; r < box.row + box.rows; ++r)
                    height += heights[r - cell.row];
                result.cells.push_back({columnLeft() + edges[box.column], top,
                                        edges[box.column + box.columns] - edges[box.column], height,
                                        pageIndex, box.first, box.last,
                                        table.fillAt(box.row, box.column), table.border});
                for (auto &line : box.lines) {
                    line.page = pageIndex;
                    line.box = result.cells.size() - 1;
                    line.x += columnLeft();
                    line.markerX += columnLeft();
                    line.baseline += top;
                    result.lines.push_back(std::move(line));
                }
            }
            y += groupHeight;
            if (groupEnd == table.rows)
                y += 8;
            sectionBottom = std::max(sectionBottom, y);
            index = end - 1;
            listNumber = 0;
            continue;
        }
        listNumber = paragraph.list == ListKind::Decimal ? listNumber + 1 : 0;
        if (index && paragraph.pageBreakBefore)
            newPage();
        if ((y > page.margin || sectionBottom > page.margin) &&
            (paragraph.keepWithNext || paragraph.keepLines)) {
            // Measure the minimum group before placing it. Explicit breaks and table
            // boundaries end a text group; oversized groups use ordinary line splitting.
            double bodyHeight = 0, noteHeight = 0;
            double ownBody = 0, ownNotes = 0;
            bool chainHasLink = false;
            auto number = listNumber, noteNumber = notesSoFar + 1;
            auto endnoteNumber = endnoteIds.size() + 1;
            for (auto k = index; k < document.paragraphs.size(); ++k) {
                const auto &p = document.paragraphs[k];
                const bool follows = p.keepWithNext && !p.sectionBreak &&
                    k + 1 < document.paragraphs.size() &&
                    !document.paragraphs[k + 1].cell &&
                    !document.paragraphs[k + 1].pageBreakBefore;
                const auto &lines = measure(p, columnWidth, number, noteNumber, endnoteNumber);
                const bool whole = p.keepLines || follows;
                const auto minimum = whole ? lines.size() :
                    p.widowControl ? std::min(lines.size(), std::size_t{lines.size() == 3 ? 3u : 2u}) : 1u;
                const auto end = lines[minimum - 1].end;
                if (k != index)
                    bodyHeight += p.spaceBefore;
                for (std::size_t n = 0; n < minimum; ++n)
                    bodyHeight += lines[n].height;
                for (const auto &block : notesIn(p, noteNumber, 0, end))
                    noteHeight += block.height;
                if (k == index) {
                    ownBody = bodyHeight;
                    ownNotes = noteHeight;
                    chainHasLink = follows;
                }
                if (!follows || bodyHeight + noteHeight > availableHeight)
                    break;
                bodyHeight += p.spaceAfter;
                noteNumber += countNotes(p);
                endnoteNumber += countNotes(p, true);
                number = document.paragraphs[k + 1].list == ListKind::Decimal ? number + 1 : 0;
            }
            const bool oversized = bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0) >
                                   availableHeight;
            if (paragraph.keepLines && oversized) {
                bodyHeight = ownBody;
                noteHeight = ownNotes;
            }
            const double freshNeed = bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0);
            auto fitsHere = [&] {
                const double before = y > sectionTop ? paragraph.spaceBefore : 0;
                return fitsWritingArea(bodyHeight, noteHeight, before);
            };
            const bool startsChain = chainHasLink &&
                (index == 0 || !document.paragraphs[index - 1].keepWithNext ||
                 document.paragraphs[index - 1].sectionBreak);
            if ((startsChain && oversized) || (!fitsHere() && freshNeed <= availableHeight)) {
                advance();
                // A continuous section or notes already on this page can leave all
                // remaining columns too short even when a fresh page can hold it.
                if (freshNeed <= availableHeight && !fitsHere())
                    newPage();
            }
        }
        if (y > sectionTop)
            y += paragraph.spaceBefore;
        const auto firstNote = notesSoFar + 1;
        notesSoFar += countNotes(paragraph);
        const auto firstEndnote = endnoteIds.size() + 1;
        collectEndnotes(paragraph);
        const auto &paragraphLines = measure(paragraph, columnWidth, listNumber, firstNote, firstEndnote);
        std::size_t segmentStart = 0;
        for (std::size_t n = 0; n < paragraphLines.size(); ++n) {
            auto line = paragraphLines[n];
            auto reservation = [&](std::size_t from, std::size_t count) {
                double height = 0, noteHeight = 0;
                for (std::size_t k = from; k < from + count; ++k)
                    height += paragraphLines[k].height;
                for (const auto &block : notesIn(paragraph, firstNote, paragraphLines[from].start,
                                                paragraphLines[from + count - 1].end))
                    noteHeight += block.height;
                return std::pair{height, noteHeight};
            };
            // Reserve the opening pair and the closing pair. A three-line paragraph
            // needs all three together when possible: splitting it always isolates a line.
            std::size_t reserve = 1;
            if (paragraph.widowControl && paragraphLines.size() > 1) {
                if (n == segmentStart)
                    reserve = std::min(paragraphLines.size() - n, std::size_t{2});
                if (n + 3 == paragraphLines.size() && n != segmentStart + 1)
                    reserve = 3;
                if (n + 2 == paragraphLines.size() && n > segmentStart + 1)
                    reserve = 2;
            }
            if (paragraph.widowControl && n + 4 == paragraphLines.size()) {
                const auto tail = reservation(n + 1, 3);
                if (tail.first + tail.second + (tail.second > 0 ? noteGap : 0) > availableHeight)
                    reserve = 2; // Split an infeasible three-line tail as two pairs instead.
            }
            auto [bodyHeight, noteHeight] = reservation(n, reserve);
            // Impossible constraints relax to ordinary splitting, without retry loops.
            if (bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0) > availableHeight) {
                reserve = std::min(reserve, std::size_t{2});
                std::tie(bodyHeight, noteHeight) = reservation(n, reserve);
                if (bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0) > availableHeight)
                    std::tie(bodyHeight, noteHeight) = reservation(n, 1);
            }
            auto fits = [&] {
                return fitsWritingArea(bodyHeight, noteHeight);
            };
            // An ordinary overflow starts a new segment too. Reserve its opening
            // pair before selecting a column whose remaining note area may be short.
            if (paragraph.widowControl && reserve == 1 && !fits() && n + 1 < paragraphLines.size()) {
                const auto pair = reservation(n, 2);
                if (pair.first + pair.second + (pair.second > 0 ? noteGap : 0) <= availableHeight)
                    std::tie(bodyHeight, noteHeight) = pair;
            }
            // If all three lines cannot share this area with their kept heading, the
            // feasible heading + opening pair takes priority over widow control.
            if (n == 0 && reserve == 3 && !fits() && index &&
                document.paragraphs[index - 1].keepWithNext &&
                !document.paragraphs[index - 1].sectionBreak &&
                !document.paragraphs[index - 1].cell && !paragraph.pageBreakBefore) {
                const auto pair = reservation(n, 2);
                if (fitsWritingArea(pair.first, pair.second))
                    std::tie(bodyHeight, noteHeight) = pair;
            }
            if (!fits() && bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0) <= availableHeight &&
                (y > page.margin || sectionBottom > page.margin)) {
                advance();
                if (!fits()) newPage();
                segmentStart = n;
            }
            if (line.height > availableHeight)
                throw std::runtime_error("A line is taller than the page writing area");
            auto blocks = notesIn(paragraph, firstNote, line.start, line.end);
            if (std::max(y + line.height, sectionBottom) + notesHeight + notesNeed(blocks) >
                    page.height - page.margin && (y > page.margin || sectionBottom > page.margin)) {
                advance();
                segmentStart = n;
            }
            addNotes(blocks);
            line.paragraph = index;
            line.page = pageIndex;
            line.x += columnLeft();
            line.markerX += columnLeft();
            line.baseline = y + line.shaped.ascent;
            result.lines.push_back(std::move(line));
            y += result.lines.back().height;
            sectionBottom = std::max(sectionBottom, y);
        }
        y += paragraph.spaceAfter;
    }
    // Endnotes follow the text below a short rule, continuing onto new pages as needed
    // (footnotes keep their room at the bottom of each page).
    if (!endnoteIds.empty()) {
        y = std::max(y, sectionBottom); // below every column
        if (y + 2 * noteGap + notesHeight > page.height - page.margin)
            newPage();
        else
            y += noteGap;
        result.noteSeparators.emplace_back(pageIndex, y + noteGap / 2);
        y += noteGap;
        for (std::size_t i = 0; i < endnoteIds.size(); ++i)
            for (auto line : noteBlock(endnoteIds[i], i + 1, true).lines) {
                if (y + line.height + notesHeight > page.height - page.margin && y > page.margin)
                    newPage();
                line.page = pageIndex;
                line.x += page.margin;
                line.markerX += page.margin;
                line.baseline = y + line.shaped.ascent;
                y += line.height;
                result.furniture.push_back(std::move(line));
            }
    }
    flushNotes();
    std::erase_if(cache_, [&](const auto &entry) { return !liveIds.contains(entry.first); });
    result.pageCount = pageIndex + 1;
    result.pageStyles.resize(result.pageCount, page);
    // Headers and footers sit centred in the top and bottom margins (when there is room).
    if (!document.page.header.empty() || !document.page.footer.empty()) {
        auto style = resolveStyle(document.styles, "Normal").character;
        style.size = 10;
        style.bold = style.italic = style.underline = style.strike = false;
        style.highlight = 0;
        style.color = 0xff5f6673;
        for (std::size_t number = 0; number < result.pageCount; ++number) {
            const auto &page = result.pageStyles[number];
            const double width = page.width - 2 * page.margin;
            if ((number == 0 && document.page.differentFirstPage) || page.margin < 24)
                continue;
            for (const bool top : {true, false}) {
                const auto &band = top ? document.page.header : document.page.footer;
                const std::u16string *texts[] = {&band.left, &band.center, &band.right};
                for (int position = 0; position < 3; ++position) {
                    if (texts[position]->empty())
                        continue;
                    Paragraph label;
                    label.runs = {
                        {bandText(*texts[position], number + 1, result.pageCount), style}};
                    Line line;
                    line.page = number;
                    line.end = label.runs.front().text().size();
                    line.shaped = shaper_.shape(label, 0, line.end);
                    line.height = line.shaped.ascent + line.shaped.descent;
                    const double remaining = std::max(0.0, width - line.shaped.width);
                    line.x = page.margin + (position == 1   ? remaining / 2
                                            : position == 2 ? remaining
                                                            : 0);
                    const double middle = top ? page.margin / 2 : page.height - page.margin / 2;
                    line.baseline = middle - line.height / 2 + line.shaped.ascent;
                    result.furniture.push_back(std::move(line));
                }
            }
        }
    }
    std::stable_sort(result.furniture.begin(), result.furniture.end(),
                     [](const Line &a, const Line &b) { return a.page < b.page; });
    return result;
}
Position Layout::hitTest(double x, double y, std::size_t pageIndex) const {
    const Line *closest = nullptr;
    double distance = std::numeric_limits<double>::max();
    std::size_t targetCell = noBox;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto &cell = cells[i];
        if (cell.page == pageIndex && x >= cell.x && x <= cell.x + cell.width && y >= cell.y &&
            y <= cell.y + cell.height) {
            targetCell = i;
            break;
        }
    }
    for (const auto &line : lines) {
        if (targetCell != noBox && line.box != targetCell)
            continue;
        const double top = line.baseline - line.shaped.ascent;
        const double vertical = y < top                 ? top - y
                                : y > top + line.height ? y - top - line.height
                                                        : 0;
        const double candidate =
            vertical + std::abs(static_cast<double>(line.page) - pageIndex) * page.height +
            (x < line.x                       ? line.x - x
             : x > line.x + line.shaped.width ? x - line.x - line.shaped.width
                                              : 0) *
                0.001;
        if (candidate < distance) {
            distance = candidate;
            closest = &line;
        }
    }
    if (!closest)
        return {};
    std::size_t offset = closest->start;
    distance = std::numeric_limits<double>::max();
    for (auto [position, caretX] : closest->caretStops) {
        auto candidate = std::abs(x - closest->x - caretX);
        if (candidate < distance) {
            distance = candidate;
            offset = position;
        }
    }
    return {closest->paragraph, offset};
}
Layout::Caret Layout::caret(Position position) const {
    const Line *match = nullptr;
    for (const auto &line : lines) {
        if (line.paragraph == position.paragraph && position.offset >= line.start &&
            position.offset <= line.end)
            match = &line;
        if (match && position.offset < line.end)
            break;
    }
    if (!match)
        return {page.margin, page.margin, 14, 0};
    double x = 0;
    for (auto [offset, caretX] : match->caretStops)
        if (offset == position.offset) {
            x = caretX;
            break;
        }
    return {match->x + x, match->baseline - match->shaped.ascent, match->height, match->page};
}
} // namespace wf
