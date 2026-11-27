#include "core/layout/layout.hpp"
#include <atomic>
#include <bit>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
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
namespace {
// Order-sensitive 64-bit hash of pagination inputs; equal keys mean equal inputs.
struct Key {
    std::uint64_t value = 0xcbf29ce484222325ull;
    Key &add(std::uint64_t x) {
        value ^= x + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
        return *this;
    }
    Key &add(double x) {
        return add(std::bit_cast<std::uint64_t>(x));
    }
    Key &add(bool x) {
        return add(static_cast<std::uint64_t>(x));
    }
    Key &add(const std::u16string &x) {
        return add(static_cast<std::uint64_t>(std::hash<std::u16string>{}(x)));
    }
    Key &add(const std::string &x) {
        return add(static_cast<std::uint64_t>(std::hash<std::string>{}(x)));
    }
    Key &add(const CharacterStyle &s) {
        return add(static_cast<std::uint64_t>(s.baseline)).add(s.family).add(s.size).add(s.bold)
            .add(s.italic).add(s.underline)
            .add(static_cast<std::uint64_t>(s.underlineStyle)).add(s.strike)
            .add(static_cast<std::uint64_t>(s.color)).add(static_cast<std::uint64_t>(s.highlight))
            .add(s.spacing);
    }
    Key &geometry(const PageStyle &page) {
        return add(page.width).add(page.height).add(page.marginTop).add(page.marginBottom)
            .add(page.marginLeft).add(page.marginRight)
            .add(static_cast<std::uint64_t>(page.columns)).add(page.columnGap);
    }
};
std::uint64_t tableKey(const Table &table) {
    Key key;
    key.add(table.id).add(static_cast<std::uint64_t>(table.rows))
        .add(static_cast<std::uint64_t>(table.columns)).add(table.border.width)
        .add(static_cast<std::uint64_t>(table.border.color));
    for (auto width : table.widths)
        key.add(width);
    for (const auto &merge : table.merges)
        key.add(static_cast<std::uint64_t>(merge.row)).add(static_cast<std::uint64_t>(merge.column))
            .add(static_cast<std::uint64_t>(merge.rows))
            .add(static_cast<std::uint64_t>(merge.columns));
    for (const auto &shade : table.shading)
        key.add(static_cast<std::uint64_t>(shade.row)).add(static_cast<std::uint64_t>(shade.column))
            .add(static_cast<std::uint64_t>(shade.color));
    for (const auto &item : table.cellBorders)
        key.add(static_cast<std::uint64_t>(item.row)).add(static_cast<std::uint64_t>(item.column))
            .add(static_cast<std::uint64_t>(item.side)).add(item.border.width)
            .add(static_cast<std::uint64_t>(item.border.color));
    return key.value;
}
// Header and footer lines are marked so incremental composition can tell them apart from
// footnote and endnote lines in Layout::furniture.
constexpr std::size_t bandLine = noBox;
std::atomic<std::uint64_t> generations{0}, stamps{0};
} // namespace
Layout LayoutEngine::compose(const Document &document) {
    return paginate(document, nullptr);
}
Layout LayoutEngine::compose(const Document &document, Layout &&previous) {
    return paginate(document, &previous);
}
Layout LayoutEngine::paginate(const Document &document, Layout *previous) {
    validatePage(document.page);
    const auto count = document.paragraphs.size();
    // Each paragraph's section settings, from the section break that ends its section.
    std::vector<PageStyle> sectionPages{document.page};
    std::vector<std::size_t> sectionOf(count);
    // Each paragraph's own inputs, recomputed only where the paragraphs aren't shared with
    // the previous composition's.
    {
        const auto &now = document.paragraphs;
        std::size_t prefix = 0, suffix = 0;
        while (prefix < paragraphs_.size() && prefix < count && now.shares(prefix, paragraphs_, prefix))
            ++prefix;
        while (suffix < paragraphs_.size() - prefix && suffix < count - prefix &&
               now.shares(count - 1 - suffix, paragraphs_, paragraphs_.size() - 1 - suffix))
            ++suffix;
        std::vector<Local> changed;
        changed.reserve(count - suffix - prefix);
        for (auto i = prefix; i < count - suffix; ++i) {
            const auto &p = now[i];
            Local local;
            Key key;
            key.add(p.id).add(p.revision)
                .add(static_cast<std::uint64_t>(p.list)).add(static_cast<std::uint64_t>(p.alignment))
                .add(p.indentLeft).add(p.indentRight).add(p.indentFirstLine).add(p.spaceBefore)
                .add(p.spaceAfter).add(p.lineSpacing).add(p.pageBreakBefore).add(p.keepWithNext)
                .add(p.keepLines).add(p.widowControl).add(p.contextualSpacing).add(p.listId)
                .add(static_cast<std::uint64_t>(p.listLevel));
            for (const auto &tab : p.tabs)
                key.add(tab.position).add(static_cast<std::uint64_t>(tab.alignment))
                    .add(static_cast<std::uint64_t>(tab.leader));
            key.add(p.cell.has_value()).add(p.sectionBreak.has_value());
            if (p.sectionBreak)
                key.add(p.sectionBreak->continuous);
            if (p.cell) {
                key.add(p.cell->tableId).add(static_cast<std::uint64_t>(p.cell->row))
                    .add(static_cast<std::uint64_t>(p.cell->column));
                local.tableId = p.cell->tableId;
            }
            for (const auto &run : p.runs)
                if (const auto *note = run.note()) {
                    key.add(note->id).add(note->endnote);
                    local.notes = true;
                }
            local.key = key.value;
            local.id = p.id;
            local.contextual = p.contextualSpacing;
            // Paragraphs "of the same style" for contextual spacing: one style, one container.
            Key container;
            container.add(p.cell.has_value());
            if (p.cell)
                container.add(p.cell->tableId).add(static_cast<std::uint64_t>(p.cell->row))
                    .add(static_cast<std::uint64_t>(p.cell->column));
            local.container = container.value;
            local.group = Key{}.add(p.style).add(local.container).value;
            local.list = p.list;
            local.listId = p.listId;
            local.listLevel = p.listLevel;
            local.sectionBreak = p.sectionBreak.has_value();
            local.cell = p.cell.has_value();
            changed.push_back(local);
        }
        const auto at = locals_.begin() + static_cast<std::ptrdiff_t>(prefix);
        locals_.erase(at, locals_.end() - static_cast<std::ptrdiff_t>(suffix));
        locals_.insert(locals_.begin() + static_cast<std::ptrdiff_t>(prefix), changed.begin(),
                       changed.end());
        paragraphs_ = now;
    }
    for (auto i = count; i-- > 0;) {
        if (locals_[i].sectionBreak)
            sectionPages.push_back(sectionPage(document, i));
        sectionOf[i] = sectionPages.size() - 1;
    }
    auto sectionAt = [&](std::size_t i) -> const PageStyle & {
        return sectionPages[sectionOf[i]];
    };
    auto noteStyle = resolveStyle(document.styles, "Normal").character;
    noteStyle.size = 10;
    noteStyle.bold = noteStyle.italic = noteStyle.underline = noteStyle.strike = false;
    noteStyle.highlight = 0;
    // Pagination keys: everything a paragraph's placement reads besides the state where it
    // starts and its neighbours (text and character formatting through its revision).
    // Page styles carry the header and footer, so changing them recomposes everything.
    Key global;
    global.add(noteStyle).add(document.page.differentFirstPage).add(document.page.differentOddEven);
    for (const auto *band :
         {&document.page.header, &document.page.footer, &document.page.evenHeader,
          &document.page.evenFooter, &document.page.firstHeader, &document.page.firstFooter})
        global.add(band->left).add(band->center).add(band->right);
    // Pictures are drawn from their resources, so page stamps depend on them too.
    for (const auto &[id, resource] : document.images)
        global.add(id).add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(resource.get())));
    const auto globalKey = global.value;
    // Contextual spacing: which paragraphs drop their space before or after because a
    // neighbour has the same style.
    std::vector<char> dropBefore(count), dropAfter(count);
    for (std::size_t i = 0; i < count; ++i)
        if (locals_[i].contextual) {
            dropBefore[i] = i > 0 && locals_[i - 1].group == locals_[i].group;
            dropAfter[i] = i + 1 < count && locals_[i + 1].group == locals_[i].group;
        }
    auto spaceBefore = [&](std::size_t i) {
        return dropBefore[i] ? 0.0 : document.paragraphs[i].spaceBefore;
    };
    auto spaceAfter = [&](std::size_t i) {
        return dropAfter[i] ? 0.0 : document.paragraphs[i].spaceAfter;
    };
    // List labels depend only on the paragraphs before, never on pagination.
    std::vector<std::u16string> labels(count);
    if (std::any_of(locals_.begin(), locals_.end(),
                    [](const Local &local) { return local.list != ListKind::None; })) {
        ListNumbering numbering(document.lists);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &local = locals_[i];
            labels[i] = numbering.next(local.list, local.listId, local.listLevel, local.container);
        }
    }
    std::vector<std::uint64_t> keys(count), ids(count);
    {
        std::vector<std::uint64_t> sectionKeys;
        for (const auto &section : sectionPages)
            sectionKeys.push_back(Key{}.geometry(section).value);
        std::unordered_map<std::uint64_t, std::uint64_t> notes, tables;
        for (const auto &note : document.footnotes)
            notes.emplace(note.id, Key{}.add(note.text).value);
        for (const auto &table : document.tables)
            tables.emplace(table.id, tableKey(table));
        for (std::size_t i = 0; i < count; ++i) {
            const auto &local = locals_[i];
            Key key;
            key.add(local.key).add(sectionKeys[sectionOf[i]]).add(dropBefore[i] != 0)
                .add(dropAfter[i] != 0);
            if (!labels[i].empty())
                key.add(labels[i]);
            if (local.cell) {
                const auto found = tables.find(local.tableId);
                key.add(found == tables.end() ? std::uint64_t{0} : found->second);
            }
            if (local.notes)
                for (const auto &run : document.paragraphs[i].runs)
                    if (const auto *note = run.note()) {
                        const auto found = notes.find(note->id);
                        key.add(found == notes.end() ? std::uint64_t{0} : found->second);
                    }
            keys[i] = key.value;
            ids[i] = local.id;
        }
    }
    // Incremental composition: resume at the checkpoint before the first changed paragraph
    // (and before any keep-with-next chain running into it); after the changed range, stop
    // when the state matches the previous composition's at the same paragraph.
    const bool incremental = previous && generation_ && previous->generation == generation_ &&
                             globalKey == globalKey_ && checkpoints_.size() == keys_.size() + 1;
    const auto oldCount = keys_.size();
    std::size_t resume = 0, firstChanged = 0, suffixStart = count, oldSuffixStart = oldCount;
    if (incremental) {
        while (firstChanged < count && firstChanged < oldCount &&
               keys[firstChanged] == keys_[firstChanged])
            ++firstChanged;
        std::size_t same = 0;
        while (same < count - firstChanged && same < oldCount - firstChanged &&
               keys[count - 1 - same] == keys_[oldCount - 1 - same])
            ++same;
        suffixStart = count - same;
        oldSuffixStart = oldCount - same;
        resume = std::min(firstChanged, oldCount);
        auto stepBack = [&] {
            while (resume > 0 && !checkpoints_[resume].valid)
                --resume;
        };
        stepBack();
        while (resume > 0 && document.paragraphs[resume - 1].keepWithNext) {
            --resume;
            stepBack();
        }
    }
    generation_ = 0; // until this composition succeeds
    // The current section's page: text width, column width and height of the writing area.
    PageStyle page;
    double width = 0, columnWidth = 0, availableHeight = 0;
    auto useSection = [&](const PageStyle &style) {
        validatePage(style);
        page = style;
        width = page.contentWidth();
        availableHeight = page.contentHeight();
        columnWidth = (width - static_cast<double>(page.columns - 1) * page.columnGap) /
                      static_cast<double>(page.columns);
        if (!std::isfinite(width) || !std::isfinite(availableHeight) || columnWidth < 36 ||
            availableHeight < 36)
            throw std::runtime_error("Page margins leave insufficient writing space");
    };
    useSection(count ? sectionAt(resume ? resume - 1 : 0) : document.page);
    Layout result;
    result.page = count ? sectionAt(0) : document.page;
    result.images = document.images;
    State state;
    std::vector<std::uint64_t> endnoteIds;
    std::vector<Checkpoint> checkpoints;
    // The previous composition's output after the resume point, for reuse on convergence.
    struct Tail {
        std::vector<Line> lines, notes;
        std::vector<CellRectangle> cells;
        std::vector<std::pair<std::size_t, double>> separators;
        std::vector<PageStyle> pageStyles;
    } tail;
    std::vector<Line> oldBands;
    std::vector<std::uint64_t> oldEndnotes, oldStamps;
    // Old pages reused unchanged: [0, keptPrefix) at the same index, and old pages
    // (tailFirst, tailEnd) moved by tailShift.
    std::size_t keptPrefix = 0, tailFirst = 0, tailEnd = 0;
    std::ptrdiff_t tailShift = 0;
    Checkpoint base;
    if (incremental) {
        // The previous checkpoints stay where they are: those before the change keep their
        // index and those after it move to their paragraphs' new index (their values are
        // shifted on convergence). The changed range is recorded afresh.
        checkpoints = std::move(checkpoints_);
        base = checkpoints[resume];
        const auto at = checkpoints.begin() + static_cast<std::ptrdiff_t>(firstChanged);
        if (suffixStart > oldSuffixStart)
            checkpoints.insert(at, suffixStart - oldSuffixStart, Checkpoint{});
        else
            checkpoints.erase(at, at + static_cast<std::ptrdiff_t>(oldSuffixStart - suffixStart));
        oldEndnotes = std::move(endnoteIds_);
        state = base.state;
        keptPrefix = state.pageIndex;
        auto split = [](auto &from, auto &to, std::size_t at) {
            to.assign(std::make_move_iterator(from.begin() + static_cast<std::ptrdiff_t>(at)),
                      std::make_move_iterator(from.end()));
            from.resize(at);
        };
        oldStamps = std::move(previous->pageStamps);
        result.lines = std::move(previous->lines);
        split(result.lines, tail.lines, base.lines);
        result.cells = std::move(previous->cells);
        split(result.cells, tail.cells, base.cells);
        for (auto &line : previous->furniture)
            (line.paragraph == bandLine ? oldBands : result.furniture).push_back(std::move(line));
        split(result.furniture, tail.notes, base.furniture);
        result.noteSeparators = std::move(previous->noteSeparators);
        split(result.noteSeparators, tail.separators, base.separators);
        result.pageStyles = std::move(previous->pageStyles);
        split(result.pageStyles, tail.pageStyles, state.pageIndex);
        result.pageStyles.push_back(page);
        endnoteIds.assign(oldEndnotes.begin(),
                          oldEndnotes.begin() + static_cast<std::ptrdiff_t>(state.endnotes));
        // Paragraphs removed by the edit leave the measurement cache.
        std::unordered_set<std::uint64_t> kept(ids.begin() + static_cast<std::ptrdiff_t>(firstChanged),
                                               ids.begin() + static_cast<std::ptrdiff_t>(suffixStart));
        for (auto i = firstChanged; i < oldSuffixStart; ++i)
            if (!kept.contains(ids_[i]))
                cache_.erase(ids_[i]);
    } else {
        checkpoints.assign(count + 1, Checkpoint{});
        result.pageStyles = {page};
        state.y = state.sectionTop = state.sectionBottom = page.marginTop;
    }
    auto &y = state.y;
    auto &pageIndex = state.pageIndex;
    // Columns: text fills `column` from `sectionTop` down, then the next column, then the
    // next page. `sectionBottom` is the lowest text of the section on this page.
    auto &column = state.column;
    auto &sectionTop = state.sectionTop;
    auto &sectionBottom = state.sectionBottom;
    auto columnLeft = [&] {
        return page.marginLeft + static_cast<double>(column) * (columnWidth + page.columnGap);
    };
    std::unordered_set<std::uint64_t> liveIds;
    validateTables(document);
    auto measure = [&](const Paragraph &paragraph, double width, const std::u16string &label,
                       std::size_t firstNote,
                       std::size_t firstEndnote = 1) -> const std::vector<Line> & {
        validateParagraphFormat(paragraph);
        liveIds.insert(paragraph.id);
        auto found = cache_.find(paragraph.id);
        if (found == cache_.end() || found->second.revision != paragraph.revision ||
            found->second.width != width || found->second.label != label ||
            found->second.firstNote != firstNote || found->second.firstEndnote != firstEndnote) {
            ++result.measuredParagraphs;
            std::vector<Line> lines;
            const auto text = paragraph.text();
            const auto paragraphBoundaries = paragraph.boundaries();
            std::u16string markerText;
            ShapedLine marker;
            if (paragraph.list != ListKind::None && !label.empty()) {
                markerText = label;
                Paragraph label;
                auto style =
                    paragraph.runs.empty() ? CharacterStyle{} : paragraph.runs.front().style;
                style.highlight = 0;
                style.strike = false;
                style.underline = false;
                style.spacing = 0;
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
                                    Cached{paragraph.revision, width, std::move(lines), label,
                                           firstNote, firstEndnote});
            found = cache_.find(paragraph.id);
        }
        return found->second.lines;
    };
    // Footnotes: numbered in reference order, laid out at the bottom of the page holding
    // their reference, below a short rule. Pagination keeps room for them.
    constexpr double noteGap = 10;
    auto noteBlock = [&](std::uint64_t id, std::size_t number, bool endnote = false) {
        const auto *note = document.footnote(id);
        Paragraph text(id);
        text.revision = std::hash<std::u16string>{}(note->text);
        text.spaceBefore = text.spaceAfter = 0;
        text.lineSpacing = 1;
        text.runs = {Run::fromNote({id, endnote}, noteStyle), Run(u" " + note->text, noteStyle)};
        NoteBlock block;
        block.id = id;
        block.text = text.revision;
        block.number = number;
        for (const auto &line :
             measure(text, width, {}, endnote ? 1 : number, endnote ? number : 1)) {
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
    auto collectEndnotes = [&](const Paragraph &p) {
        for (const auto &run : p.runs)
            if (const auto *note = run.note(); note && note->endnote)
                endnoteIds.push_back(note->id);
    };
    auto &pageNotes = state.pageNotes;
    auto &notesHeight = state.notesHeight;
    auto &notesSoFar = state.notesSoFar;
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
               page.height - page.marginBottom;
    };
    auto addNotes = [&](std::vector<NoteBlock> &blocks) {
        notesHeight += notesNeed(blocks);
        for (auto &block : blocks)
            pageNotes.push_back(std::move(block));
    };
    auto flushNotes = [&] {
        if (pageNotes.empty())
            return;
        const double top = page.height - page.marginBottom - notesHeight;
        result.noteSeparators.emplace_back(pageIndex, top + noteGap / 2);
        double cursor = top + noteGap;
        for (auto &block : pageNotes)
            for (auto &line : block.lines) {
                line.page = pageIndex;
                line.x += page.marginLeft;
                line.markerX += page.marginLeft;
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
        y = sectionTop = sectionBottom = page.marginTop;
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
    auto record = [&](Checkpoint &checkpoint) {
        checkpoint.valid = true;
        checkpoint.state = state;
        checkpoint.state.endnotes = endnoteIds.size();
        checkpoint.lines = result.lines.size();
        checkpoint.cells = result.cells.size();
        checkpoint.furniture = result.furniture.size();
        checkpoint.separators = result.noteSeparators.size();
    };
    auto sameState = [](const State &a, const State &b) {
        if (a.y != b.y || a.column != b.column || a.sectionTop != b.sectionTop ||
            a.sectionBottom != b.sectionBottom ||
            a.notesSoFar != b.notesSoFar || a.endnotes != b.endnotes ||
            a.notesHeight != b.notesHeight || a.pageNotes.size() != b.pageNotes.size())
            return false;
        for (std::size_t i = 0; i < a.pageNotes.size(); ++i) {
            const auto &x = a.pageNotes[i], &z = b.pageNotes[i];
            if (x.id != z.id || x.text != z.text || x.number != z.number || x.height != z.height)
                return false;
        }
        return true;
    };
    const auto delta = static_cast<std::ptrdiff_t>(count) - static_cast<std::ptrdiff_t>(oldCount);
    // Splices the previous composition's output from old paragraph `j` to its end.
    auto converge = [&](std::size_t index) {
        const auto j = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) - delta);
        if (!incremental || index <= resume || index < suffixStart + 1 || j > oldCount ||
            j < oldSuffixStart + 1 || !checkpoints[index].valid)
            return false;
        auto current = state;
        current.endnotes = endnoteIds.size();
        if (!sameState(current, checkpoints[index].state))
            return false;
        const auto from = checkpoints[index];
        const auto end = checkpoints[count];
        tailFirst = from.state.pageIndex;
        tailEnd = end.state.pageIndex;
        tailShift = static_cast<std::ptrdiff_t>(pageIndex) -
                    static_cast<std::ptrdiff_t>(from.state.pageIndex);
        const auto pageDelta = static_cast<std::ptrdiff_t>(pageIndex) -
                               static_cast<std::ptrdiff_t>(from.state.pageIndex);
        const auto lineShift = static_cast<std::ptrdiff_t>(result.lines.size()) -
                               static_cast<std::ptrdiff_t>(from.lines);
        const auto cellShift = static_cast<std::ptrdiff_t>(result.cells.size()) -
                               static_cast<std::ptrdiff_t>(from.cells);
        const auto noteShift = static_cast<std::ptrdiff_t>(result.furniture.size()) -
                               static_cast<std::ptrdiff_t>(from.furniture);
        const auto ruleShift = static_cast<std::ptrdiff_t>(result.noteSeparators.size()) -
                               static_cast<std::ptrdiff_t>(from.separators);
        auto shift = [](std::size_t &value, std::ptrdiff_t by) {
            value = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(value) + by);
        };
        for (auto k = from.lines - base.lines; k < end.lines - base.lines; ++k) {
            auto &line = tail.lines[k];
            shift(line.paragraph, delta);
            shift(line.page, pageDelta);
            if (line.box != noBox)
                shift(line.box, cellShift);
            result.lines.push_back(std::move(line));
        }
        for (auto k = from.cells - base.cells; k < end.cells - base.cells; ++k) {
            auto &cell = tail.cells[k];
            shift(cell.page, pageDelta);
            shift(cell.firstParagraph, delta);
            shift(cell.lastParagraph, delta);
            result.cells.push_back(cell);
        }
        for (auto k = from.furniture - base.furniture; k < end.furniture - base.furniture; ++k) {
            auto &line = tail.notes[k];
            shift(line.page, pageDelta);
            result.furniture.push_back(std::move(line));
        }
        for (auto k = from.separators - base.separators; k < end.separators - base.separators; ++k) {
            auto separator = tail.separators[k];
            shift(separator.first, pageDelta);
            result.noteSeparators.push_back(separator);
        }
        // The current page's settings as the rest of the old composition left them.
        result.pageStyles.pop_back();
        for (auto k = from.state.pageIndex - base.state.pageIndex;
             k <= end.state.pageIndex - base.state.pageIndex; ++k)
            result.pageStyles.push_back(std::move(tail.pageStyles[k]));
        endnoteIds.insert(endnoteIds.end(),
                          oldEndnotes.begin() + static_cast<std::ptrdiff_t>(from.state.endnotes),
                          oldEndnotes.begin() + static_cast<std::ptrdiff_t>(end.state.endnotes));
        if (pageDelta || lineShift || cellShift || noteShift || ruleShift)
            for (auto k = index; k <= count; ++k) {
                auto &checkpoint = checkpoints[k];
                if (checkpoint.valid) {
                    shift(checkpoint.state.pageIndex, pageDelta);
                    shift(checkpoint.lines, lineShift);
                    shift(checkpoint.cells, cellShift);
                    shift(checkpoint.furniture, noteShift);
                    shift(checkpoint.separators, ruleShift);
                }
            }
        state = checkpoints[count].state;
        useSection(sectionAt(count - 1));
        return true;
    };
    bool converged = false;
    for (std::size_t index = resume; index < count; ++index) {
        if (converge(index)) {
            converged = true;
            break;
        }
        record(checkpoints[index]);
        ++result.composedParagraphs;
        const auto &paragraph = document.paragraphs[index];
        if (index && document.paragraphs[index - 1].sectionBreak) {
            const auto &next = sectionAt(index);
            const bool sameSheet = next.width == page.width && next.height == page.height &&
                                   next.marginTop == page.marginTop &&
                                   next.marginBottom == page.marginBottom &&
                                   next.marginLeft == page.marginLeft &&
                                   next.marginRight == page.marginRight;
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
                y = sectionTop = sectionBottom = page.marginTop;
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
                std::size_t first = 0, last = 0;
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
                    box.height += spaceBefore(end);
                const auto firstNote = notesSoFar + 1;
                for (auto &block : notesIn(p, firstNote, 0, p.text().size()))
                    groupNotes.push_back(std::move(block));
                notesSoFar += countNotes(p);
                const auto firstEndnote = endnoteIds.size() + 1;
                collectEndnotes(p);
                for (auto line : measure(p, boxWidth - 8, labels[end], firstNote, firstEndnote)) {
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
                    box.height += spaceAfter(end);
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
            if (y + groupHeight + notesHeight + notesNeed(groupNotes) > page.height - page.marginBottom &&
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
                                        table.fillAt(box.row, box.column),
                                        {table.borderAt(box.row, box.column, CellSide::Top),
                                         table.borderAt(box.row, box.column, CellSide::Right),
                                         table.borderAt(box.row, box.column, CellSide::Bottom),
                                         table.borderAt(box.row, box.column, CellSide::Left),
                                         table.borderAt(box.row, box.column, CellSide::DiagonalDown),
                                         table.borderAt(box.row, box.column, CellSide::DiagonalUp)}});
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
            // No checkpoints inside the row group (the reused vector may hold old ones).
            for (auto k = index + 1; k < end; ++k)
                checkpoints[k].valid = false;
            index = end - 1;
            continue;
        }
        if (index && paragraph.pageBreakBefore)
            newPage();
        if ((y > page.marginTop || sectionBottom > page.marginTop) &&
            (paragraph.keepWithNext || paragraph.keepLines)) {
            // Measure the minimum group before placing it. Explicit breaks and table
            // boundaries end a text group; oversized groups use ordinary line splitting.
            double bodyHeight = 0, noteHeight = 0;
            double ownBody = 0, ownNotes = 0;
            bool chainHasLink = false;
            auto noteNumber = notesSoFar + 1;
            auto endnoteNumber = endnoteIds.size() + 1;
            for (auto k = index; k < document.paragraphs.size(); ++k) {
                const auto &p = document.paragraphs[k];
                const bool follows = p.keepWithNext && !p.sectionBreak &&
                    k + 1 < document.paragraphs.size() &&
                    !document.paragraphs[k + 1].cell &&
                    !document.paragraphs[k + 1].pageBreakBefore;
                const auto &lines = measure(p, columnWidth, labels[k], noteNumber, endnoteNumber);
                const bool whole = p.keepLines || follows;
                const auto minimum = whole ? lines.size() :
                    p.widowControl ? std::min(lines.size(), std::size_t{lines.size() == 3 ? 3u : 2u}) : 1u;
                const auto end = lines[minimum - 1].end;
                if (k != index)
                    bodyHeight += spaceBefore(k);
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
                bodyHeight += spaceAfter(k);
                noteNumber += countNotes(p);
                endnoteNumber += countNotes(p, true);
            }
            const bool oversized = bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0) >
                                   availableHeight;
            if (paragraph.keepLines && oversized) {
                bodyHeight = ownBody;
                noteHeight = ownNotes;
            }
            const double freshNeed = bodyHeight + noteHeight + (noteHeight > 0 ? noteGap : 0);
            auto fitsHere = [&] {
                const double before = y > sectionTop ? spaceBefore(index) : 0;
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
            y += spaceBefore(index);
        const auto firstNote = notesSoFar + 1;
        notesSoFar += countNotes(paragraph);
        const auto firstEndnote = endnoteIds.size() + 1;
        collectEndnotes(paragraph);
        const auto &paragraphLines = measure(paragraph, columnWidth, labels[index], firstNote, firstEndnote);
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
                (y > page.marginTop || sectionBottom > page.marginTop)) {
                advance();
                if (!fits()) newPage();
                segmentStart = n;
            }
            if (line.height > availableHeight)
                throw std::runtime_error("A line is taller than the page writing area");
            auto blocks = notesIn(paragraph, firstNote, line.start, line.end);
            if (std::max(y + line.height, sectionBottom) + notesHeight + notesNeed(blocks) >
                    page.height - page.marginBottom && (y > page.marginTop || sectionBottom > page.marginTop)) {
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
        y += spaceAfter(index);
    }
    if (!converged)
        record(checkpoints[count]);
    // Endnotes follow the text below a short rule, continuing onto new pages as needed
    // (footnotes keep their room at the bottom of each page).
    if (!endnoteIds.empty()) {
        y = std::max(y, sectionBottom); // below every column
        if (y + 2 * noteGap + notesHeight > page.height - page.marginBottom)
            newPage();
        else
            y += noteGap;
        result.noteSeparators.emplace_back(pageIndex, y + noteGap / 2);
        y += noteGap;
        for (std::size_t i = 0; i < endnoteIds.size(); ++i)
            for (auto line : noteBlock(endnoteIds[i], i + 1, true).lines) {
                if (y + line.height + notesHeight > page.height - page.marginBottom && y > page.marginTop)
                    newPage();
                line.page = pageIndex;
                line.x += page.marginLeft;
                line.markerX += page.marginLeft;
                line.baseline = y + line.shaped.ascent;
                y += line.height;
                result.furniture.push_back(std::move(line));
            }
    }
    flushNotes();
    if (!incremental)
        std::erase_if(cache_, [&](const auto &entry) { return !liveIds.contains(entry.first); });
    result.pageCount = pageIndex + 1;
    result.pageStyles.resize(result.pageCount, page);
    // Headers and footers sit centred in the top and bottom margins (when there is room).
    auto bandStyle = resolveStyle(document.styles, "Normal").character;
    bandStyle.size = 10;
    bandStyle.bold = bandStyle.italic = bandStyle.underline = bandStyle.strike = false;
    bandStyle.highlight = 0;
    bandStyle.color = 0xff5f6673;
    Key bandKey;
    bandKey.add(bandStyle).add(document.page.differentFirstPage)
        .add(document.page.differentOddEven)
        .add(static_cast<std::uint64_t>(result.pageCount));
    for (const auto *band :
         {&document.page.header, &document.page.footer, &document.page.evenHeader,
          &document.page.evenFooter, &document.page.firstHeader, &document.page.firstFooter})
        bandKey.add(band->left).add(band->center).add(band->right);
    for (const auto &style : result.pageStyles)
        bandKey.geometry(style);
    std::vector<Line> bands;
    const bool bandsKept = incremental && bandKey.value == bandKey_;
    if (bandsKept)
        bands = std::move(oldBands);
    else if (document.page.anyBand()) {
        for (std::size_t number = 0; number < result.pageCount; ++number) {
            const auto &page = result.pageStyles[number];
            const double width = page.contentWidth();
            if (!document.page.band(true, number))
                continue;
            for (const bool top : {true, false}) {
                if ((top ? page.marginTop : page.marginBottom) < 24)
                    continue; // no room for the band in that margin
                const auto &band = *document.page.band(top, number);
                const std::u16string *texts[] = {&band.left, &band.center, &band.right};
                for (int position = 0; position < 3; ++position) {
                    if (texts[position]->empty())
                        continue;
                    Paragraph label;
                    label.runs = {
                        {bandText(*texts[position], number + 1, result.pageCount), bandStyle}};
                    Line line;
                    line.paragraph = bandLine;
                    line.page = number;
                    line.end = label.runs.front().text().size();
                    line.shaped = shaper_.shape(label, 0, line.end);
                    line.height = line.shaped.ascent + line.shaped.descent;
                    const double remaining = std::max(0.0, width - line.shaped.width);
                    line.x = page.marginLeft + (position == 1   ? remaining / 2
                                            : position == 2 ? remaining
                                                            : 0);
                    const double middle =
                        top ? page.marginTop / 2 : page.height - page.marginBottom / 2;
                    line.baseline = middle - line.height / 2 + line.shaped.ascent;
                    bands.push_back(std::move(line));
                }
            }
        }
    }
    // Notes and bands are each in page order; on a page the notes come first.
    if (!bands.empty()) {
        std::vector<Line> furniture;
        furniture.reserve(result.furniture.size() + bands.size());
        std::merge(std::make_move_iterator(result.furniture.begin()),
                   std::make_move_iterator(result.furniture.end()),
                   std::make_move_iterator(bands.begin()), std::make_move_iterator(bands.end()),
                   std::back_inserter(furniture),
                   [](const Line &a, const Line &b) { return a.page < b.page; });
        result.furniture = std::move(furniture);
    }
    // New stamps for every page this composition drew; reused pages keep theirs. New
    // header or footer text (page numbers after a page count change) touches every page.
    result.pageStamps.resize(result.pageCount);
    const bool everyPage = !incremental || (!bandsKept && !bands.empty());
    // Without endnotes the last page holds only reused text and footnotes, which the
    // converged state reproduces exactly; endnotes are recomposed, so their pages are new.
    if (converged && endnoteIds.empty() && oldEndnotes.empty())
        ++tailEnd;
    for (std::size_t page = 0; page < result.pageCount; ++page) {
        std::optional<std::size_t> old;
        if (!everyPage) {
            if (page < keptPrefix)
                old = page;
            else if (converged) {
                const auto from = static_cast<std::ptrdiff_t>(page) - tailShift;
                if (from > static_cast<std::ptrdiff_t>(tailFirst) &&
                    from < static_cast<std::ptrdiff_t>(tailEnd))
                    old = static_cast<std::size_t>(from);
            }
        }
        result.pageStamps[page] = old && *old < oldStamps.size() ? oldStamps[*old] : ++stamps;
    }
    keys_ = std::move(keys);
    ids_ = std::move(ids);
    endnoteIds_ = std::move(endnoteIds);
    checkpoints_ = std::move(checkpoints);
    globalKey_ = globalKey;
    bandKey_ = bandKey.value;
    generation_ = result.generation = ++generations;
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
        return {page.marginLeft, page.marginTop, 14, 0};
    double x = 0;
    for (auto [offset, caretX] : match->caretStops)
        if (offset == position.offset) {
            x = caretX;
            break;
        }
    return {match->x + x, match->baseline - match->shaped.ascent, match->height, match->page};
}
} // namespace wf
