# Phase 2A incremental pagination — work in progress (2026-11-23)

Status: **unfinished.** `layout.hpp` declares the checkpoint structures and
`compose(const Document &, Layout &&previous)`, but nothing implements them yet.
`layout.cpp` is unchanged, so the build still uses full composition everywhere.

Baseline (Windows ARM64 Release, `wordforge_benchmark`, 1000 pages / 32,000 paragraphs):
full layout 3577 ms, layout after one edit 116.7 ms, renderer record 132 ms.

## Design

- **Keys.** Each paragraph gets a key built from:
  - its id and revision
  - its paragraph format fields
  - its section geometry
  - its cell and table hash
  - the hashes of the notes it references

  A global key covers the note style.
- **Checkpoints.** Each step start (a paragraph, or the start of a table row group)
  records a checkpoint `{State, output sizes}`. A final checkpoint records the state
  after the last paragraph.
- **Resume point.** Find the first changed key and the common unchanged suffix. Resume
  at the last valid checkpoint at or before the first change, then step back further
  over any keep-with-next chain that runs into it.
- **Convergence.** Stop at index `i`, where both `i` and `i-1` are in the unchanged
  suffix, once the new state equals the old checkpoint `i-delta`. `pageIndex` may
  differ; pending notes are compared by id, text hash, number and height. Then splice
  the old tail with these shifts:
  - `paragraph += delta`
  - `page += pageDelta`
  - `box += cellShift`

  Checkpoints, page styles and endnote ids are shifted the same way.
- **Endnotes and bands.** Endnotes are always recomposed. Header and footer lines are
  tagged with `paragraph = noBox` and reused while the band key (text, page count,
  page geometry) is unchanged. `furniture` is rebuilt by a stable merge of notes and
  bands by page.
- **Failure.** `generation_` is reset during composition, so a composition that throws
  falls back to a full one next time. PageView's failure path already recomposes fully.
- **Wiring.** Only `PageView::change` should use the incremental overload. Every other
  caller keeps calling `compose(document)`.

## Remaining

1. Finish `paginate()`. The draft head is below. The loop body is the current `compose`
   body, with these changes:
   - state variables become references into `State`
   - a checkpoint is recorded at each step start
   - a convergence check runs at each step start
   - `NoteBlock` gains id, text hash and number
2. Write oracle tests comparing incremental and full composition on these documents:
   - long plain text
   - lists
   - keep-with-next headings
   - widow/orphan control
   - footnotes and endnotes
   - tables with merged rows
   - sections with columns, and continuous sections
   - page breaks

   Cover edits early, in the middle and at the end, plus inserts and deletes. Assert
   that `composedParagraphs` stays small for an edit in the middle.
3. Wire it into `PageView::change`, measure with the benchmark, then update
   PERFORMANCE_BASELINE and the roadmap.

## Draft head of paginate() (not compiled)

```cpp
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
    Key &add(std::size_t x) requires(!std::is_same_v<std::size_t, std::uint64_t>) {
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
            .add(s.italic).add(s.underline).add(s.strike)
            .add(static_cast<std::uint64_t>(s.color)).add(static_cast<std::uint64_t>(s.highlight));
    }
    Key &geometry(const PageStyle &page) {
        return add(page.width).add(page.height).add(page.margin)
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
    return key.value;
}
// Header and footer lines are marked so incremental composition can tell them apart from
// footnote and endnote lines in Layout::furniture.
constexpr std::size_t bandLine = noBox;
std::atomic<std::uint64_t> generations{0};
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
    for (auto i = count; i-- > 0;) {
        if (document.paragraphs[i].sectionBreak)
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
    const auto globalKey = Key{}.add(noteStyle).value;
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
            const auto &p = document.paragraphs[i];
            Key key;
            key.add(p.id).add(p.revision).add(sectionKeys[sectionOf[i]])
                .add(static_cast<std::uint64_t>(p.list)).add(static_cast<std::uint64_t>(p.alignment))
                .add(p.indentLeft).add(p.indentRight).add(p.indentFirstLine).add(p.spaceBefore)
                .add(p.spaceAfter).add(p.lineSpacing).add(p.pageBreakBefore).add(p.keepWithNext)
                .add(p.keepLines).add(p.widowControl);
            for (const auto &tab : p.tabs)
                key.add(tab.position).add(static_cast<std::uint64_t>(tab.alignment))
                    .add(static_cast<std::uint64_t>(tab.leader));
            key.add(p.cell.has_value()).add(p.sectionBreak.has_value());
            if (p.sectionBreak)
                key.add(p.sectionBreak->continuous);
            if (p.cell) {
                const auto found = tables.find(p.cell->tableId);
                key.add(p.cell->tableId).add(static_cast<std::uint64_t>(p.cell->row))
                    .add(static_cast<std::uint64_t>(p.cell->column))
                    .add(found == tables.end() ? std::uint64_t{0} : found->second);
            }
            for (const auto &run : p.runs)
                if (const auto *note = run.note()) {
                    const auto found = notes.find(note->id);
                    key.add(note->id).add(note->endnote)
                        .add(found == notes.end() ? std::uint64_t{0} : found->second);
                }
            keys[i] = key.value;
            ids[i] = p.id;
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
        width = page.width - 2 * page.margin;
        availableHeight = page.height - 2 * page.margin;
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
    std::vector<Checkpoint> checkpoints(count + 1), oldCheckpoints;
    // The previous composition's output after the resume point, for reuse on convergence.
    struct Tail {
        std::vector<Line> lines, notes;
        std::vector<CellRectangle> cells;
        std::vector<std::pair<std::size_t, double>> separators;
        std::vector<PageStyle> pageStyles;
    } tail;
    std::vector<Line> oldBands;
    std::vector<std::uint64_t> oldEndnotes;
    Checkpoint base;
    if (incremental) {
        oldCheckpoints = std::move(checkpoints_);
        oldEndnotes = std::move(endnoteIds_);
        base = oldCheckpoints[resume];
        state = base.state;
        auto split = [](auto &from, auto &to, std::size_t at) {
            to.assign(std::make_move_iterator(from.begin() + static_cast<std::ptrdiff_t>(at)),
                      std::make_move_iterator(from.end()));
            from.resize(at);
        };
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
        split(result.pageStyles, tail.pageStyles, state.pageIndex + 1);
        result.pageStyles.back() = page;
        endnoteIds.assign(oldEndnotes.begin(),
                          oldEndnotes.begin() + static_cast<std::ptrdiff_t>(state.endnotes));
        std::move(oldCheckpoints.begin(),
                  oldCheckpoints.begin() + static_cast<std::ptrdiff_t>(resume),
                  checkpoints.begin());
        // Paragraphs removed by the edit leave the measurement cache.
        std::unordered_set<std::uint64_t> kept(ids.begin() + static_cast<std::ptrdiff_t>(firstChanged),
                                               ids.begin() + static_cast<std::ptrdiff_t>(suffixStart));
        for (auto i = firstChanged; i < oldSuffixStart; ++i)
            if (!kept.contains(ids_[i]))
                cache_.erase(ids_[i]);
    } else {
        result.pageStyles = {page};
        state.y = state.sectionTop = state.sectionBottom = page.margin;
    }
    auto &y = state.y;
    auto &pageIndex = state.pageIndex;
    // Columns: text fills `column` from `sectionTop` down, then the next column, then the
    // next page. `sectionBottom` is the lowest text of the section on this page.
    auto &column = state.column;
    auto &sectionTop = state.sectionTop;
    auto &sectionBottom = state.sectionBottom;
    auto columnLeft = [&] {
        return page.margin + static_cast<double>(column) * (columnWidth + page.columnGap);
    };
    auto &listNumber = state.listNumber;
    std::unordered_set<std::uint64_t> liveIds;
    validateTables(document);
```
