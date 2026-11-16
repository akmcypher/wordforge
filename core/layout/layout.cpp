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
void stops(Line &line, const std::vector<std::size_t> &paragraphBoundaries) {
    struct Cluster {
        double left = std::numeric_limits<double>::max(), right = 0;
    };
    for (const auto &run : line.shaped.runs) {
        if (run.glyphs.empty()) {
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
    const auto &page = document.page;
    validatePage(page);
    const double width = page.width - 2 * page.margin,
                 availableHeight = page.height - 2 * page.margin;
    if (!std::isfinite(width) || !std::isfinite(availableHeight) || width < 36 ||
        availableHeight < 36)
        throw std::runtime_error("Page margins leave insufficient writing space");
    Layout result;
    result.page = page;
    result.images = document.images;
    double y = page.margin;
    std::size_t pageIndex = 0;
    std::size_t listNumber = 0;
    std::unordered_set<std::uint64_t> liveIds;
    validateTables(document);
    auto measure = [&](const Paragraph &paragraph, double width, std::size_t listNumber,
                       std::size_t firstNote) -> const std::vector<Line> & {
        validateParagraphFormat(paragraph);
        liveIds.insert(paragraph.id);
        auto found = cache_.find(paragraph.id);
        if (found == cache_.end() || found->second.revision != paragraph.revision ||
            found->second.width != width || found->second.number != listNumber ||
            found->second.firstNote != firstNote) {
            auto shapeRange = [&](std::size_t from, std::size_t to) {
                return shaper_.shape(paragraph, from, to, firstNote);
            };
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
            const double textLeft = paragraph.indentLeft + markerWidth;
            const double textWidth = width - textLeft;
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
            const bool singleLine = complete.width <= textWidth;
            do {
                std::size_t fit = start;
                ShapedLine fitted = shapeRange(start, start);
                if (singleLine) {
                    fit = text.size();
                    fitted = std::move(complete);
                }
                for (auto boundary =
                         singleLine ? UBRK_DONE
                                    : ubrk_following(breaks.get(), static_cast<int32_t>(start));
                     boundary != UBRK_DONE; boundary = ubrk_next(breaks.get())) {
                    auto candidate =
                        shapeRange(start, static_cast<std::size_t>(boundary));
                    if (candidate.width > textWidth)
                        break;
                    fit = static_cast<std::size_t>(boundary);
                    fitted = std::move(candidate);
                }
                if (fit == start && start < text.size()) {
                    for (auto boundary = nextGrapheme(paragraph, start); boundary <= text.size();) {
                        auto candidate = shapeRange(start, boundary);
                        if (candidate.width > textWidth && fit != start)
                            break;
                        fit = boundary;
                        fitted = std::move(candidate);
                        if (boundary == text.size() || fitted.width > textWidth)
                            break;
                        boundary = nextGrapheme(paragraph, boundary);
                    }
                }
                Line line;
                line.start = start;
                line.end = fit;
                line.shaped = std::move(fitted);
                line.height = (line.shaped.ascent + line.shaped.descent) * paragraph.lineSpacing;
                const double remaining = std::max(0.0, textWidth - line.shaped.width);
                line.x = textLeft + (paragraph.alignment == Alignment::Center  ? remaining / 2
                                     : paragraph.alignment == Alignment::Right ? remaining
                                                                               : 0);
                if (start == 0 && !markerText.empty()) {
                    line.markerText = markerText;
                    line.marker = std::move(marker);
                    line.markerX = paragraph.indentLeft + markerWidth - 4 - line.marker.width;
                }
                stops(line, paragraphBoundaries);
                lines.push_back(std::move(line));
                start = fit;
            } while (start < text.size());
            cache_.insert_or_assign(
                paragraph.id,
                Cached{paragraph.revision, width, std::move(lines), listNumber, firstNote});
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
    auto noteBlock = [&](std::uint64_t id, std::size_t number) {
        const auto *note = document.footnote(id);
        Paragraph text(id);
        text.revision = std::hash<std::u16string>{}(note->text);
        text.spaceBefore = text.spaceAfter = 0;
        text.lineSpacing = 1;
        text.runs = {Run::fromNote({id}, noteStyle), Run(u" " + note->text, noteStyle)};
        NoteBlock block;
        for (const auto &line : measure(text, width, 0, number)) {
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
            if (const auto *note = run.note()) {
                if (offset >= from && offset < to)
                    blocks.push_back(noteBlock(note->id, number));
                ++number;
            }
            offset += run.text().size();
        }
        return blocks;
    };
    auto countNotes = [](const Paragraph &p) {
        return static_cast<std::size_t>(
            std::count_if(p.runs.begin(), p.runs.end(), [](const Run &r) { return r.note(); }));
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
        y = page.margin;
    };
    for (std::size_t index = 0; index < document.paragraphs.size(); ++index) {
        const auto &paragraph = document.paragraphs[index];
        if (paragraph.cell) {
            const auto cell = *paragraph.cell;
            const auto &table =
                *std::find_if(document.tables.begin(), document.tables.end(),
                              [&](const auto &value) { return value.id == cell.tableId; });
            const auto edges = columnEdges(table, width);
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
                const bool ghost = (row != p.cell->row || column != p.cell->column) && p.text().empty();
                const double boxWidth = edges[column + box.columns] - edges[column];
                if (!ghost)
                    box.height += p.spaceBefore;
                box.number = p.list == ListKind::Decimal ? box.number + 1 : 0;
                const auto firstNote = notesSoFar + 1;
                for (auto &block : notesIn(p, firstNote, 0, p.text().size()))
                    groupNotes.push_back(std::move(block));
                notesSoFar += countNotes(p);
                for (auto line : measure(p, boxWidth - 8, box.number, firstNote)) {
                    line.paragraph = end;
                    line.x += 4 + edges[column];
                    line.markerX += 4 + edges[column];
                    if (ghost) {
                        line.baseline = std::max(4.0, box.height - line.height) + line.shaped.ascent;
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
            if (y + groupHeight + notesHeight + notesNeed(groupNotes) >
                    page.height - page.margin &&
                y > page.margin)
                newPage();
            addNotes(groupNotes);
            for (auto &[key, box] : boxes) {
                double top = y, height = 0;
                for (auto r = cell.row; r < box.row; ++r)
                    top += heights[r - cell.row];
                for (auto r = box.row; r < box.row + box.rows; ++r)
                    height += heights[r - cell.row];
                result.cells.push_back({page.margin + edges[box.column], top,
                                        edges[box.column + box.columns] - edges[box.column], height,
                                        pageIndex, box.first, box.last});
                for (auto &line : box.lines) {
                    line.page = pageIndex;
                    line.box = result.cells.size() - 1;
                    line.x += page.margin;
                    line.markerX += page.margin;
                    line.baseline += top;
                    result.lines.push_back(std::move(line));
                }
            }
            y += groupHeight;
            if (groupEnd == table.rows)
                y += 8;
            index = end - 1;
            listNumber = 0;
            continue;
        }
        listNumber = paragraph.list == ListKind::Decimal ? listNumber + 1 : 0;
        if (index && paragraph.pageBreakBefore)
            newPage();
        if (y > page.margin)
            y += paragraph.spaceBefore;
        const auto firstNote = notesSoFar + 1;
        notesSoFar += countNotes(paragraph);
        for (auto line : measure(paragraph, width, listNumber, firstNote)) {
            if (line.height > availableHeight)
                throw std::runtime_error("A line is taller than the page writing area");
            auto blocks = notesIn(paragraph, firstNote, line.start, line.end);
            if (y + line.height + notesHeight + notesNeed(blocks) > page.height - page.margin &&
                y > page.margin)
                newPage();
            addNotes(blocks);
            line.paragraph = index;
            line.page = pageIndex;
            line.x += page.margin;
            line.markerX += page.margin;
            line.baseline = y + line.shaped.ascent;
            result.lines.push_back(std::move(line));
            y += result.lines.back().height;
        }
        y += paragraph.spaceAfter;
    }
    flushNotes();
    std::erase_if(cache_, [&](const auto &entry) { return !liveIds.contains(entry.first); });
    result.pageCount = pageIndex + 1;
    // Headers and footers sit centred in the top and bottom margins (when there is room).
    if (page.margin >= 24 && (!page.header.empty() || !page.footer.empty())) {
        auto style = resolveStyle(document.styles, "Normal").character;
        style.size = 10;
        style.bold = style.italic = style.underline = style.strike = false;
        style.highlight = 0;
        style.color = 0xff5f6673;
        for (std::size_t number = 0; number < result.pageCount; ++number) {
            if (number == 0 && page.differentFirstPage)
                continue;
            for (const bool top : {true, false}) {
                const auto &band = top ? page.header : page.footer;
                const std::u16string *texts[] = {&band.left, &band.center, &band.right};
                for (int position = 0; position < 3; ++position) {
                    if (texts[position]->empty())
                        continue;
                    Paragraph label;
                    label.runs = {{bandText(*texts[position], number + 1, result.pageCount), style}};
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
