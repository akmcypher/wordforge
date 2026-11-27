#include "core/document/document.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
namespace wf {
const CellMerge *Table::mergeAt(std::size_t row, std::size_t column) const {
    for (const auto &merge : merges)
        if (merge.contains(row, column))
            return &merge;
    return nullptr;
}
std::vector<double> columnEdges(const Table &table, double width) {
    if (table.columns * minimumColumnWidth > width + 1e-9)
        throw std::runtime_error("Table columns leave insufficient writing space; use fewer "
                                 "columns or a wider page");
    std::vector<double> widths = table.widths.size() == table.columns
                                     ? table.widths
                                     : std::vector<double>(table.columns, 1.0);
    // Scale to the width; repeatedly pin columns below the minimum and rescale the rest.
    std::vector<bool> pinned(table.columns, false);
    for (std::size_t pass = 0; pass <= table.columns; ++pass) {
        double free = width, total = 0;
        for (std::size_t c = 0; c < table.columns; ++c)
            pinned[c] ? free -= minimumColumnWidth : total += widths[c];
        bool changed = false;
        for (std::size_t c = 0; c < table.columns; ++c) {
            if (pinned[c])
                continue;
            widths[c] = widths[c] * free / total;
            if (widths[c] < minimumColumnWidth) {
                pinned[c] = true;
                changed = true;
            }
        }
        if (!changed)
            break;
        for (std::size_t c = 0; c < table.columns; ++c)
            if (pinned[c])
                widths[c] = minimumColumnWidth;
    }
    std::vector<double> edges{0};
    for (auto w : widths)
        edges.push_back(edges.back() + w);
    edges.back() = width;
    return edges;
}
std::uint32_t Table::fillAt(std::size_t row, std::size_t column) const {
    for (const auto &shade : shading)
        if (shade.row == row && shade.column == column)
            return shade.color;
    return 0;
}
namespace {
auto borderOrder(const CellBorder &item) {
    return std::tuple(item.row, item.column, static_cast<int>(item.side));
}
} // namespace
const TableBorder *Table::ownBorder(std::size_t row, std::size_t column, CellSide side) const {
    const auto key = std::tuple(row, column, static_cast<int>(side));
    const auto found = std::lower_bound(
        cellBorders.begin(), cellBorders.end(), key,
        [](const CellBorder &item, const auto &wanted) { return borderOrder(item) < wanted; });
    return found != cellBorders.end() && borderOrder(*found) == key ? &found->border : nullptr;
}
TableBorder Table::borderAt(std::size_t row, std::size_t column, CellSide side) const {
    std::size_t rows = 1, columns = 1;
    if (const auto *merge = mergeAt(row, column)) {
        row = merge->row, column = merge->column;
        rows = merge->rows, columns = merge->columns;
    }
    const auto *own = ownBorder(row, column, side);
    if (side == CellSide::DiagonalDown || side == CellSide::DiagonalUp) {
        TableBorder none;
        none.width = 0;
        return own ? *own : none;
    }
    auto result = own ? *own : border;
    // The neighbouring cell across this side, if there is one.
    std::optional<std::pair<std::size_t, std::size_t>> next;
    if (side == CellSide::Top && row)
        next = {row - 1, column};
    else if (side == CellSide::Bottom && row + rows < this->rows)
        next = {row + rows, column};
    else if (side == CellSide::Left && column)
        next = {row, column - 1};
    else if (side == CellSide::Right && column + columns < this->columns)
        next = {row, column + columns};
    if (!next)
        return result;
    auto [r, c] = *next;
    if (const auto *merge = mergeAt(r, c))
        r = merge->row, c = merge->column;
    const auto opposite = static_cast<CellSide>((static_cast<int>(side) + 2) % 4);
    const auto *theirs = ownBorder(r, c, opposite);
    if (!own && !theirs)
        return result;
    const auto other = theirs ? *theirs : border;
    return other.width > result.width ? other : result;
}
void validateTables(const Document &document) {
    std::map<std::uint64_t, std::vector<std::size_t>> members;
    for (std::size_t i = 0; i < document.paragraphs.size(); ++i)
        if (const auto &cell = document.paragraphs[i].cell)
            members[cell->tableId].push_back(i);
    for (const auto &table : document.tables) {
        if (!table.id || !table.rows || table.rows > 1000 || !table.columns || table.columns > 10)
            throw std::invalid_argument("Table requires 1–1000 rows and 1–10 columns");
        if (!table.widths.empty() &&
            (table.widths.size() != table.columns ||
             std::any_of(table.widths.begin(), table.widths.end(),
                         [](double w) { return !std::isfinite(w) || w <= 0 || w > 2880; })))
            throw std::invalid_argument("Invalid table column widths");
        if (!std::isfinite(table.border.width) || table.border.width < 0 ||
            table.border.width > 6 || (table.border.color >> 24) != 0xff ||
            static_cast<unsigned>(table.border.style) > 3)
            throw std::invalid_argument("Invalid table border");
        if (table.cellBorders.size() > 6 * table.rows * table.columns)
            throw std::invalid_argument("Too many table cell borders");
        for (std::size_t i = 0; i < table.cellBorders.size(); ++i) {
            const auto &item = table.cellBorders[i];
            if (item.row >= table.rows || item.column >= table.columns ||
                static_cast<unsigned>(item.side) > 5 || !std::isfinite(item.border.width) ||
                item.border.width < 0 || item.border.width > 6 ||
                (item.border.color >> 24) != 0xff || static_cast<unsigned>(item.border.style) > 3 ||
                (i && !(borderOrder(table.cellBorders[i - 1]) < borderOrder(item))))
                throw std::invalid_argument("Invalid, repeated or unordered table cell border");
        }
        if (table.shading.size() > table.rows * table.columns)
            throw std::invalid_argument("Too many shaded table cells");
        std::vector<bool> shaded(table.rows * table.columns, false);
        for (const auto &shade : table.shading) {
            if (shade.row >= table.rows || shade.column >= table.columns ||
                (shade.color >> 24) != 0xff || shaded[shade.row * table.columns + shade.column])
                throw std::invalid_argument("Invalid table cell shading");
            shaded[shade.row * table.columns + shade.column] = true;
        }
        if (table.merges.size() > table.rows * table.columns)
            throw std::invalid_argument("Too many merged table cells");
        std::vector<bool> covered(table.rows * table.columns, false);
        for (const auto &merge : table.merges) {
            if (merge.rows < 1 || merge.columns < 1 || merge.rows * merge.columns < 2 ||
                merge.row >= table.rows || merge.column >= table.columns ||
                merge.rows > table.rows - merge.row || merge.columns > table.columns - merge.column)
                throw std::invalid_argument("Invalid merged table cells");
            for (auto r = merge.row; r < merge.row + merge.rows; ++r)
                for (auto c = merge.column; c < merge.column + merge.columns; ++c) {
                    if (covered[r * table.columns + c])
                        throw std::invalid_argument("Merged table cells overlap");
                    covered[r * table.columns + c] = true;
                }
        }
        auto found = members.find(table.id);
        if (found == members.end())
            throw std::invalid_argument("Table has no cells");
        const auto &indices = found->second;
        if (indices.back() - indices.front() + 1 != indices.size() ||
            indices.back() + 1 >= document.paragraphs.size() ||
            document.paragraphs[indices.back() + 1].cell)
            throw std::invalid_argument(
                "Table cells must be contiguous with a following body paragraph");
        std::size_t previous = 0;
        bool first = true;
        for (auto index : indices) {
            const auto &p = document.paragraphs[index];
            const auto &cell = *p.cell;
            if (cell.row >= table.rows || cell.column >= table.columns || p.pageBreakBefore)
                throw std::invalid_argument("Invalid table cell coordinates or page break");
            auto linear = cell.row * table.columns + cell.column;
            if ((first && linear != 0) || (!first && linear != previous && linear != previous + 1))
                throw std::invalid_argument("Table cells must form a complete row-major grid");
            first = false;
            previous = linear;
        }
        if (previous + 1 != table.rows * table.columns)
            throw std::invalid_argument("Table has missing cells");
        members.erase(found);
    }
    if (!members.empty())
        throw std::invalid_argument("Cell references an unknown table");
}
std::optional<TableCell> Editor::selectedCell() const {
    if (selection.focus.paragraph >= document.paragraphs.size())
        return {};
    return document.paragraphs[selection.focus.paragraph].cell;
}
namespace {
void repairBookmarks(Document &document, Position target) {
    std::unordered_set<std::uint64_t> live;
    for (const auto &p : document.paragraphs)
        live.insert(p.id);
    for (auto *anchor : document.anchors())
        if (!live.contains(anchor->paragraphId))
            *anchor = {document.paragraphs[target.paragraph].id, target.offset, anchor->affinity};
}
struct Grid {
    Table *table;
    std::size_t first = 0, last = 0;
    std::vector<std::vector<Paragraph>> cells;
};
Grid grid(Document &document, std::uint64_t id) {
    auto table = std::find_if(document.tables.begin(), document.tables.end(),
                              [&](const auto &t) { return t.id == id; });
    if (table == document.tables.end())
        throw std::invalid_argument("Unknown table");
    Grid result{&*table, 0, 0, std::vector<std::vector<Paragraph>>(table->rows * table->columns)};
    bool first = true;
    for (std::size_t i = 0; i < document.paragraphs.size(); ++i) {
        const auto &p = document.paragraphs[i];
        if (p.cell && p.cell->tableId == id) {
            if (first)
                result.first = i;
            result.last = i;
            first = false;
            result.cells[p.cell->row * table->columns + p.cell->column].push_back(p);
        }
    }
    return result;
}
Paragraph emptyCell(Document &document, TableCell cell, const CharacterStyle &style) {
    auto paragraph = styledParagraph(document, document.allocateId());
    paragraph.cell = cell;
    if (paragraph.spaceAfter != 0) {
        paragraph.spaceAfter = 0; // cells are tight regardless of the Normal style
        paragraph.direct |= SpaceAfterField;
    }
    const auto inherited = paragraph.runs.front().style;
    paragraph.runs.front().style = style;
    paragraph.runs.front().direct = differingFields(style, inherited);
    return paragraph;
}
void reshape(Editor &editor, bool row, bool insert, bool before) {
    auto cell = editor.selectedCell();
    if (!cell)
        throw std::invalid_argument("Place the caret in a table cell first");
    auto source = grid(editor.document, cell->tableId);
    auto &table = *source.table;
    const auto oldRows = table.rows, oldColumns = table.columns;
    const auto count = row ? oldRows : oldColumns;
    if (!insert && count == 1) {
        editor.removeTable();
        return;
    }
    if (insert && count >= (row ? 1000u : 10u))
        throw std::invalid_argument("Table size limit reached");
    const auto pivot = (row ? cell->row : cell->column) + (insert && !before ? 1 : 0);
    if (row)
        table.rows = insert ? oldRows + 1 : oldRows - 1;
    else
        table.columns = insert ? oldColumns + 1 : oldColumns - 1;
    // A merge grows when a row/column is inserted inside it and shrinks when one is deleted.
    std::vector<CellMerge> merges;
    for (auto merge : table.merges) {
        auto &start = row ? merge.row : merge.column;
        auto &size = row ? merge.rows : merge.columns;
        if (insert) {
            if (pivot <= start)
                ++start;
            else if (pivot < start + size)
                ++size;
        } else if (pivot < start)
            --start;
        else if (pivot < start + size)
            --size;
        if (size && merge.rows * merge.columns >= 2)
            merges.push_back(merge);
    }
    table.merges = std::move(merges);
    // Shading moves with its cell; a deleted row or column takes its shading along.
    std::vector<CellShading> shading;
    for (auto shade : table.shading) {
        auto &at = row ? shade.row : shade.column;
        if (insert) {
            if (at >= pivot)
                ++at;
        } else if (at == pivot)
            continue;
        else if (at > pivot)
            --at;
        shading.push_back(shade);
    }
    table.shading = std::move(shading);
    // So do cell borders.
    std::vector<CellBorder> cellBorders;
    for (auto item : table.cellBorders) {
        auto &at = row ? item.row : item.column;
        if (insert) {
            if (at >= pivot)
                ++at;
        } else if (at == pivot)
            continue;
        else if (at > pivot)
            --at;
        cellBorders.push_back(item);
    }
    table.cellBorders = std::move(cellBorders);
    if (!row && !table.widths.empty()) {
        if (insert)
            table.widths.insert(table.widths.begin() + static_cast<std::ptrdiff_t>(pivot),
                                table.widths[cell->column]);
        else
            table.widths.erase(table.widths.begin() + static_cast<std::ptrdiff_t>(pivot));
    }
    const auto targetRow = row ? std::min(pivot, table.rows - 1) : cell->row;
    const auto targetColumn = row ? cell->column : std::min(pivot, table.columns - 1);
    std::vector<Paragraph> replacement;
    Position caret;
    for (std::size_t r = 0; r < table.rows; ++r)
        for (std::size_t c = 0; c < table.columns; ++c) {
            if (r == targetRow && c == targetColumn)
                caret = {source.first + replacement.size(), 0};
            const auto axis = row ? r : c;
            const bool fresh = insert && axis == pivot;
            const auto oldAxis =
                axis - (insert && axis > pivot ? 1 : 0) + (!insert && axis >= pivot ? 1 : 0);
            std::vector<Paragraph> content;
            if (fresh)
                content.push_back(emptyCell(editor.document, {table.id, r, c}, editor.typingStyle));
            else
                content =
                    std::move(source.cells[(row ? oldAxis : r) * oldColumns + (row ? c : oldAxis)]);
            for (auto &p : content) {
                p.cell = TableCell{table.id, r, c};
                replacement.push_back(std::move(p));
            }
        }
    auto begin = editor.document.paragraphs.begin() + source.first;
    begin = editor.document.paragraphs.erase(begin,
                                             editor.document.paragraphs.begin() + source.last + 1);
    editor.document.paragraphs.insert(begin, std::make_move_iterator(replacement.begin()),
                                      std::make_move_iterator(replacement.end()));
    editor.selection = {caret, caret};
    editor.typingStyle = editor.document.styleAt(caret);
    repairBookmarks(editor.document, caret);
}
} // namespace
void Editor::insertTable(std::size_t rows, std::size_t columns) {
    if (!rows || rows > 1000 || !columns || columns > 10)
        throw std::invalid_argument("Choose 1–1000 rows and 1–10 columns");
    auto command = [&](Editor &e) {
        if (e.selectedCell())
            throw std::invalid_argument("Nested tables are not supported yet");
        e.replace(u"");
        const auto at = e.selection.focus;
        // Split mid-paragraph, and also after a table so it keeps its body paragraph.
        if (at.offset || (at.paragraph && e.document.paragraphs[at.paragraph - 1].cell))
            e.replace(u"\n");
        const auto start = e.selection.focus.paragraph;
        const Table table{e.document.allocateId(), rows, columns};
        std::vector<Paragraph> cells;
        for (std::size_t r = 0; r < rows; ++r)
            for (std::size_t c = 0; c < columns; ++c)
                cells.push_back(emptyCell(e.document, {table.id, r, c}, e.typingStyle));
        e.document.paragraphs.insert(e.document.paragraphs.begin() + start,
                                     std::make_move_iterator(cells.begin()),
                                     std::make_move_iterator(cells.end()));
        e.document.tables.push_back(table);
        e.selection = {{start, 0}, {start, 0}};
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
void Editor::tableRow(bool insert, bool before) {
    auto command = [&](Editor &e) { reshape(e, true, insert, before); };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
void Editor::tableColumn(bool insert, bool before) {
    auto command = [&](Editor &e) { reshape(e, false, insert, before); };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
void Editor::removeTable() {
    auto command = [](Editor &e) {
        auto cell = e.selectedCell();
        if (!cell)
            throw std::invalid_argument("Place the caret in a table first");
        auto source = grid(e.document, cell->tableId);
        e.document.paragraphs.erase(e.document.paragraphs.begin() + source.first,
                                    e.document.paragraphs.begin() + source.last + 1);
        std::erase_if(e.document.tables, [&](const auto &t) { return t.id == cell->tableId; });
        e.selection = {{source.first, 0}, {source.first, 0}};
        e.typingStyle = e.document.styleAt(e.selection.focus);
        repairBookmarks(e.document, e.selection.focus);
    };
    if (inTransaction_)
        command(*this);
    else
        transact(command);
}
namespace {
std::size_t cellStart(const Document &document, std::uint64_t table, std::size_t row,
                      std::size_t column) {
    for (std::size_t i = 0; i < document.paragraphs.size(); ++i)
        if (document.paragraphs[i].cell == TableCell{table, row, column})
            return i;
    throw std::logic_error("Missing table cell");
}
const Table &tableOf(const Document &document, std::uint64_t id) {
    const auto found = std::find_if(document.tables.begin(), document.tables.end(),
                                    [&](const auto &t) { return t.id == id; });
    if (found == document.tables.end())
        throw std::invalid_argument("Unknown table");
    return *found;
}
// Writes a grid's cells back over its paragraphs, in row-major order.
void store(Document &document, Grid &source) {
    const auto &table = *source.table;
    std::vector<Paragraph> replacement;
    for (std::size_t r = 0; r < table.rows; ++r)
        for (std::size_t c = 0; c < table.columns; ++c)
            for (auto &p : source.cells[r * table.columns + c]) {
                p.cell = TableCell{table.id, r, c};
                replacement.push_back(std::move(p));
            }
    auto begin = document.paragraphs.begin() + static_cast<std::ptrdiff_t>(source.first);
    begin = document.paragraphs.erase(
        begin, document.paragraphs.begin() + static_cast<std::ptrdiff_t>(source.last + 1));
    document.paragraphs.insert(begin, std::make_move_iterator(replacement.begin()),
                               std::make_move_iterator(replacement.end()));
}
template <class Command> void run(Editor &editor, bool inTransaction, Command command) {
    if (inTransaction)
        command(editor);
    else
        editor.transact(command);
}
} // namespace
bool Editor::nextCell(bool backward) {
    const auto cell = selectedCell();
    if (!cell)
        return false;
    const auto &table = tableOf(document, cell->tableId);
    const auto columns = table.columns;
    auto linear = cell->row * columns + cell->column;
    if (const auto *merge = table.mergeAt(cell->row, cell->column))
        linear = merge->row * columns + merge->column;
    // Covered cells are skipped: only merge anchors and plain cells are visited.
    auto visible = [&](std::size_t l) {
        const auto *merge = table.mergeAt(l / columns, l % columns);
        return !merge || (merge->row == l / columns && merge->column == l % columns);
    };
    std::optional<std::size_t> target;
    if (backward) {
        for (auto l = linear; l-- > 0;)
            if (visible(l)) {
                target = l;
                break;
            }
        if (!target)
            return true;
    } else {
        for (auto l = linear + 1; l < table.rows * columns; ++l)
            if (visible(l)) {
                target = l;
                break;
            }
        if (!target) { // past the last cell: add a row
            const auto id = table.id, lastRow = table.rows - 1;
            const auto at = cellStart(document, id, lastRow, 0);
            selection = {{at, 0}, {at, 0}};
            tableRow(true);
            target = (lastRow + 1) * columns;
        }
    }
    const auto index = cellStart(document, cell->tableId, *target / columns, *target % columns);
    selection = {{index, 0}, {index, 0}};
    typingStyle = document.styleAt(selection.focus);
    return true;
}
void Editor::mergeCells() {
    run(*this, inTransaction_, [](Editor &e) {
        const auto &paragraphs = e.document.paragraphs;
        const auto a = paragraphs[e.selection.anchor.paragraph].cell;
        const auto b = paragraphs[e.selection.focus.paragraph].cell;
        if (!a || !b || a->tableId != b->tableId || *a == *b)
            throw std::invalid_argument("Select from one cell into another cell of the same "
                                        "table first");
        auto source = grid(e.document, a->tableId);
        auto &table = *source.table;
        auto r0 = std::min(a->row, b->row), r1 = std::max(a->row, b->row);
        auto c0 = std::min(a->column, b->column), c1 = std::max(a->column, b->column);
        // Grow the rectangle to include every merge it touches.
        for (bool grown = true; grown;) {
            grown = false;
            for (const auto &m : table.merges)
                if (m.row <= r1 && m.row + m.rows > r0 && m.column <= c1 &&
                    m.column + m.columns > c0) {
                    const auto nr0 = std::min(r0, m.row), nr1 = std::max(r1, m.row + m.rows - 1);
                    const auto nc0 = std::min(c0, m.column),
                               nc1 = std::max(c1, m.column + m.columns - 1);
                    grown = grown || nr0 != r0 || nr1 != r1 || nc0 != c0 || nc1 != c1;
                    r0 = nr0, r1 = nr1, c0 = nc0, c1 = nc1;
                }
        }
        const CellMerge merged{r0, c0, r1 - r0 + 1, c1 - c0 + 1};
        std::erase_if(table.merges, [&](const CellMerge &m) {
            return merged.contains(m.row, m.column);
        });
        table.merges.push_back(merged);
        // The merged cell keeps the top-left cell's fill, or else the first one inside it.
        if (!table.fillAt(r0, c0))
            for (const auto &shade : table.shading)
                if (merged.contains(shade.row, shade.column)) {
                    table.shading.push_back({r0, c0, shade.color});
                    break;
                }
        std::erase_if(table.shading, [&](const CellShading &shade) {
            return merged.contains(shade.row, shade.column) &&
                   (shade.row != r0 || shade.column != c0);
        });
        // and its own borders.
        std::erase_if(table.cellBorders, [&](const CellBorder &item) {
            return merged.contains(item.row, item.column) &&
                   (item.row != r0 || item.column != c0);
        });
        // Content moves into the top-left cell, as in Word; covered cells keep one empty
        // paragraph.
        auto hasContent = [](const std::vector<Paragraph> &cell) {
            return std::any_of(cell.begin(), cell.end(),
                               [](const Paragraph &p) { return !p.text().empty(); });
        };
        auto &anchor = source.cells[r0 * table.columns + c0];
        std::vector<Paragraph> moved;
        for (auto r = r0; r <= r1; ++r)
            for (auto c = c0; c <= c1; ++c) {
                if (r == r0 && c == c0)
                    continue;
                auto &cell = source.cells[r * table.columns + c];
                if (hasContent(cell)) {
                    for (auto &p : cell)
                        moved.push_back(std::move(p));
                    cell = {emptyCell(e.document, {table.id, r, c}, e.typingStyle)};
                } else
                    cell.resize(1);
            }
        if (!moved.empty()) {
            if (!hasContent(anchor))
                anchor.clear();
            for (auto &p : moved)
                anchor.push_back(std::move(p));
        }
        store(e.document, source);
        const auto at = cellStart(e.document, table.id, r0, c0);
        e.selection = {{at, 0}, {at, 0}};
        e.typingStyle = e.document.styleAt(e.selection.focus);
        repairBookmarks(e.document, e.selection.focus);
    });
}
void Editor::splitCell() {
    run(*this, inTransaction_, [](Editor &e) {
        const auto cell = e.selectedCell();
        if (!cell)
            throw std::invalid_argument("Place the caret in a merged cell first");
        auto &table = *grid(e.document, cell->tableId).table;
        const auto before = table.merges.size();
        std::erase_if(table.merges,
                      [&](const CellMerge &m) { return m.contains(cell->row, cell->column); });
        if (table.merges.size() == before)
            throw std::invalid_argument("The caret is not in a merged cell");
    });
}
void Editor::setColumnWidth(double points) {
    run(*this, inTransaction_, [points](Editor &e) {
        const auto cell = e.selectedCell();
        if (!cell)
            throw std::invalid_argument("Place the caret in a table first");
        auto &table = *grid(e.document, cell->tableId).table;
        if (table.columns == 1)
            throw std::invalid_argument("A single column always spans the page width");
        const double width = e.document.page.contentWidth();
        const double most = width - minimumColumnWidth * (table.columns - 1);
        if (!std::isfinite(points) || points < minimumColumnWidth || points > most)
            throw std::invalid_argument("Column width must be between " +
                                        std::to_string(static_cast<int>(minimumColumnWidth)) +
                                        " and " + std::to_string(static_cast<int>(most)) + " pt");
        const auto edges = columnEdges(table, width);
        const double others = width - (edges[cell->column + 1] - edges[cell->column]);
        std::vector<double> widths;
        for (std::size_t c = 0; c < table.columns; ++c)
            widths.push_back(c == cell->column
                                 ? points
                                 : (edges[c + 1] - edges[c]) * (width - points) / others);
        table.widths = std::move(widths);
    });
}
void Editor::setTableBorder(TableBorder border) {
    run(*this, inTransaction_, [border](Editor &e) {
        const auto cell = e.selectedCell();
        if (!cell)
            throw std::invalid_argument("Place the caret in a table first");
        if (!std::isfinite(border.width) || border.width < 0 || border.width > 6)
            throw std::invalid_argument("Border width must be between 0 and 6 pt");
        auto &table = *grid(e.document, cell->tableId).table;
        table.border = border;
        table.border.color |= 0xff000000;
        table.cellBorders.clear(); // the whole table looks as chosen
    });
}
void Editor::shadeCells(std::uint32_t color) {
    run(*this, inTransaction_, [color](Editor &e) {
        const auto &paragraphs = e.document.paragraphs;
        const auto a = paragraphs[e.selection.anchor.paragraph].cell;
        auto b = paragraphs[e.selection.focus.paragraph].cell;
        if (!a)
            throw std::invalid_argument("Place the caret in a table first");
        if (!b || b->tableId != a->tableId)
            b = a;
        auto &table = *grid(e.document, a->tableId).table;
        const auto r0 = std::min(a->row, b->row), r1 = std::max(a->row, b->row);
        const auto c0 = std::min(a->column, b->column), c1 = std::max(a->column, b->column);
        for (auto r = r0; r <= r1; ++r)
            for (auto c = c0; c <= c1; ++c) {
                const auto *merge = table.mergeAt(r, c);
                const auto row = merge ? merge->row : r, column = merge ? merge->column : c;
                std::erase_if(table.shading, [&](const CellShading &shade) {
                    return shade.row == row && shade.column == column;
                });
                if (color)
                    table.shading.push_back({row, column, color | 0xff000000});
            }
    });
}
void Editor::distributeColumns() {
    run(*this, inTransaction_, [](Editor &e) {
        const auto cell = e.selectedCell();
        if (!cell)
            throw std::invalid_argument("Place the caret in a table first");
        grid(e.document, cell->tableId).table->widths.clear();
    });
}

} // namespace wf
