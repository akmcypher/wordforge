#include "core/document/document.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_set>
namespace wf {
void validateTables(const Document &document) {
    std::map<std::uint64_t, std::vector<std::size_t>> members;
    for (std::size_t i = 0; i < document.paragraphs.size(); ++i)
        if (const auto &cell = document.paragraphs[i].cell)
            members[cell->tableId].push_back(i);
    for (const auto &table : document.tables) {
        if (!table.id || !table.rows || table.rows > 1000 || !table.columns || table.columns > 10)
            throw std::invalid_argument("Table requires 1–1000 rows and 1–10 columns");
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
    for (auto &mark : document.bookmarks)
        if (!live.contains(mark.anchor.paragraphId))
            mark.anchor = {document.paragraphs[target.paragraph].id, target.offset,
                           mark.anchor.affinity};
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
bool Editor::nextCell(bool backward) {
    const auto cell = selectedCell();
    if (!cell)
        return false;
    if (backward && cell->row == 0 && cell->column == 0)
        return true;
    auto index = selection.focus.paragraph;
    if (backward) {
        while (index && document.paragraphs[index - 1].cell == cell)
            --index;
        --index;
        const auto target = document.paragraphs[index].cell;
        while (index && document.paragraphs[index - 1].cell == target)
            --index;
    } else {
        while (index + 1 < document.paragraphs.size() &&
               document.paragraphs[index + 1].cell == cell)
            ++index;
        ++index;
        if (!document.paragraphs[index].cell ||
            document.paragraphs[index].cell->tableId != cell->tableId) {
            tableRow(true);
            index = selection.focus.paragraph;
            const auto row = *selectedCell();
            while (index && document.paragraphs[index - 1].cell &&
                   document.paragraphs[index - 1].cell->tableId == row.tableId &&
                   document.paragraphs[index - 1].cell->row == row.row)
                --index;
        }
    }
    selection = {{index, 0}, {index, 0}};
    typingStyle = document.styleAt(selection.focus);
    return true;
}

} // namespace wf
