#include "core/layout/layout.hpp"
#include <algorithm>
#include <cmath>
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
    auto measure = [&](const Paragraph &paragraph, double width,
                       std::size_t listNumber) -> const std::vector<Line> & {
        validateParagraphFormat(paragraph);
        liveIds.insert(paragraph.id);
        auto found = cache_.find(paragraph.id);
        if (found == cache_.end() || found->second.revision != paragraph.revision ||
            found->second.width != width || found->second.number != listNumber) {
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
            auto complete = shaper_.shape(paragraph, 0, text.size());
            const bool singleLine = complete.width <= textWidth;
            do {
                std::size_t fit = start;
                ShapedLine fitted = shaper_.shape(paragraph, start, start);
                if (singleLine) {
                    fit = text.size();
                    fitted = std::move(complete);
                }
                for (auto boundary =
                         singleLine ? UBRK_DONE
                                    : ubrk_following(breaks.get(), static_cast<int32_t>(start));
                     boundary != UBRK_DONE; boundary = ubrk_next(breaks.get())) {
                    auto candidate =
                        shaper_.shape(paragraph, start, static_cast<std::size_t>(boundary));
                    if (candidate.width > textWidth)
                        break;
                    fit = static_cast<std::size_t>(boundary);
                    fitted = std::move(candidate);
                }
                if (fit == start && start < text.size()) {
                    for (auto boundary = nextGrapheme(paragraph, start); boundary <= text.size();) {
                        auto candidate = shaper_.shape(paragraph, start, boundary);
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
                paragraph.id, Cached{paragraph.revision, width, std::move(lines), listNumber});
            found = cache_.find(paragraph.id);
        }
        return found->second.lines;
    };
    for (std::size_t index = 0; index < document.paragraphs.size(); ++index) {
        const auto &paragraph = document.paragraphs[index];
        if (paragraph.cell) {
            const auto cell = *paragraph.cell;
            const auto table =
                std::find_if(document.tables.begin(), document.tables.end(),
                             [&](const auto &value) { return value.id == cell.tableId; });
            const double columnWidth = width / table->columns;
            if (columnWidth - 8 < 36)
                throw std::runtime_error("Table columns leave insufficient writing space; use "
                                         "fewer columns or a wider page");
            std::vector<std::vector<Line>> columns(table->columns);
            std::vector<double> heights(table->columns, 4);
            std::vector<std::size_t> first(table->columns, index), last(table->columns, index);
            std::vector<bool> seen(table->columns, false);
            std::vector<std::size_t> cellNumbers(table->columns, 0);
            auto end = index;
            for (; end < document.paragraphs.size(); ++end) {
                const auto &p = document.paragraphs[end];
                if (!p.cell || p.cell->tableId != cell.tableId || p.cell->row != cell.row)
                    break;
                const auto c = p.cell->column;
                if (!seen[c]) {
                    first[c] = end;
                    seen[c] = true;
                }
                last[c] = end;
                heights[c] += p.spaceBefore;
                cellNumbers[c] = p.list == ListKind::Decimal ? cellNumbers[c] + 1 : 0;
                for (auto line : measure(p, columnWidth - 8, cellNumbers[c])) {
                    line.paragraph = end;
                    line.x += 4 + c * columnWidth;
                    line.markerX += 4 + c * columnWidth;
                    line.baseline = heights[c] + line.shaped.ascent;
                    heights[c] += line.height;
                    columns[c].push_back(std::move(line));
                }
                heights[c] += p.spaceAfter;
            }
            const double rowHeight =
                std::max(24.0, *std::max_element(heights.begin(), heights.end()) + 4);
            if (rowHeight > availableHeight)
                throw std::runtime_error("A table row is taller than the page; shorten its content "
                                         "or increase the page size");
            if (y + rowHeight > page.height - page.margin && y > page.margin) {
                ++pageIndex;
                y = page.margin;
            }
            for (std::size_t c = 0; c < table->columns; ++c) {
                result.cells.push_back({page.margin + c * columnWidth, y, columnWidth, rowHeight,
                                        pageIndex, first[c], last[c]});
                for (auto &line : columns[c]) {
                    line.page = pageIndex;
                    line.x += page.margin;
                    line.markerX += page.margin;
                    line.baseline += y;
                    result.lines.push_back(std::move(line));
                }
            }
            y += rowHeight;
            if (cell.row + 1 == table->rows)
                y += 8;
            index = end - 1;
            listNumber = 0;
            continue;
        }
        listNumber = paragraph.list == ListKind::Decimal ? listNumber + 1 : 0;
        if (index && paragraph.pageBreakBefore) {
            ++pageIndex;
            y = page.margin;
        }
        if (y > page.margin)
            y += paragraph.spaceBefore;
        for (auto line : measure(paragraph, width, listNumber)) {
            if (line.height > availableHeight)
                throw std::runtime_error("A line is taller than the page writing area");
            if (y + line.height > page.height - page.margin && y > page.margin) {
                ++pageIndex;
                y = page.margin;
            }
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
    std::erase_if(cache_, [&](const auto &entry) { return !liveIds.contains(entry.first); });
    result.pageCount = pageIndex + 1;
    return result;
}
Position Layout::hitTest(double x, double y, std::size_t pageIndex) const {
    const Line *closest = nullptr;
    double distance = std::numeric_limits<double>::max();
    const CellRectangle *targetCell = nullptr;
    for (const auto &cell : cells)
        if (cell.page == pageIndex && x >= cell.x && x <= cell.x + cell.width && y >= cell.y &&
            y <= cell.y + cell.height) {
            targetCell = &cell;
            break;
        }
    for (const auto &line : lines) {
        if (targetCell && (line.paragraph < targetCell->firstParagraph ||
                           line.paragraph > targetCell->lastParagraph))
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
