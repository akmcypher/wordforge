#include "core/document/document.hpp"
#include <gtest/gtest.h>
using namespace wf;
TEST(Tables, InsertEditAndStructuralCommandsUndoAtomically) {
    Editor e;
    e.replace(u"beforeafter");
    e.selection = {{0, 6}, {0, 6}};
    e.insertTable(2, 2);
    ASSERT_EQ(e.document.tables.size(), 1);
    EXPECT_EQ(e.document.paragraphs.size(), 6);
    EXPECT_EQ(e.document.paragraphs.front().text(), u"before");
    EXPECT_EQ(e.document.paragraphs.back().text(), u"after");
    ASSERT_TRUE(e.selectedCell());
    EXPECT_EQ(e.selectedCell()->column, 0);
    e.replace(u"first");
    e.insertParagraphBreak();
    e.replace(u"second");
    EXPECT_EQ(e.document.paragraphs[1].text(), u"first");
    EXPECT_EQ(e.document.paragraphs[2].text(), u"second");
    EXPECT_EQ(e.document.paragraphs[1].cell, e.document.paragraphs[2].cell);
    ASSERT_TRUE(e.nextCell());
    EXPECT_EQ(e.selectedCell()->column, 1);
    e.replace(u"right");
    e.tableRow(true);
    EXPECT_EQ(e.document.tables[0].rows, 3);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables[0].rows, 2);
    e.tableColumn(true, true);
    EXPECT_EQ(e.document.tables[0].columns, 3);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables[0].columns, 2);
    e.tableColumn(false);
    EXPECT_EQ(e.document.tables[0].columns, 1);
    e.tableRow(false);
    EXPECT_EQ(e.document.tables[0].rows, 1);
    e.removeTable();
    EXPECT_TRUE(e.document.tables.empty());
    EXPECT_EQ(e.document.text(), u"before\nafter");
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables.size(), 1);
    EXPECT_NO_THROW(validateDocument(e.document));
}
TEST(Tables, BoundariesRejectCrossCellMergesAndDeletionRepairsBookmarks) {
    Editor e;
    e.insertTable(2, 2);
    e.replace(u"left");
    e.selection = {{0, 0}, {0, 0}};
    e.eraseBackward();
    EXPECT_EQ(e.document.paragraphs[0].text(), u"left");
    e.selection = {{0, 4}, {0, 4}};
    e.eraseForward();
    EXPECT_EQ(e.document.tables.size(), 1);
    e.selection = {{0, 0}, {1, 0}};
    EXPECT_THROW(e.replace(u"oops"), std::invalid_argument);
    EXPECT_EQ(e.document.paragraphs[0].text(), u"left");
    e.selection = {{0, 2}, {0, 2}};
    auto mark = e.addBookmark("cell");
    e.tableRow(false);
    EXPECT_NO_THROW(validateDocument(e.document));
    ASSERT_TRUE(e.goToBookmark(mark));
    EXPECT_TRUE(e.selectedCell());
    EXPECT_EQ(e.selectedCell()->row, 0);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables[0].rows, 2);
    auto broken = e.document;
    broken.paragraphs[0].cell->column = 1;
    EXPECT_THROW(validateDocument(broken), std::invalid_argument);
}
TEST(Tables, TabFromLastCellAddsRowAndRetiredIdsStayRetired) {
    Editor e;
    e.insertTable(1, 1);
    auto id = e.document.tables[0].id;
    e.replace(u"x");
    ASSERT_TRUE(e.nextCell());
    EXPECT_EQ(e.document.tables[0].rows, 2);
    EXPECT_EQ(e.selectedCell()->row, 1);
    ASSERT_TRUE(e.nextCell(true));
    EXPECT_EQ(e.selectedCell()->row, 0);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables[0].rows, 1);
    EXPECT_TRUE(e.undo());
    EXPECT_TRUE(e.undo());
    EXPECT_TRUE(e.document.tables.empty());
    e.insertTable(1, 1);
    EXPECT_GT(e.document.tables[0].id, id);
}
TEST(Tables, StartInsertionHasNoLeadingEmptyParagraphAndPreservesBodyBookmark) {
    Editor e;
    e.replace(u"body");
    e.selection = {{0, 0}, {0, 0}};
    auto bookmark = e.addBookmark("body");
    e.insertTable(1, 2);
    ASSERT_EQ(e.document.paragraphs.size(), 3);
    ASSERT_TRUE(e.document.paragraphs.front().cell);
    EXPECT_EQ(e.document.paragraphs.back().text(), u"body");
    ASSERT_TRUE(e.goToBookmark(bookmark));
    EXPECT_EQ(e.selection.focus.paragraph, 2);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.text(), u"body");
}
TEST(Tables, BacktabInFirstCellStaysInsideAndNavigationSkipsCellParagraphs) {
    Editor e;
    e.replace(u"prefix");
    e.insertTable(1, 2);
    e.replace(u"first\nsecond");
    e.selection = {{1, 0}, {1, 0}};
    auto original = e.selection.focus;
    ASSERT_TRUE(e.nextCell(true));
    EXPECT_EQ(e.selection.focus, original);
    ASSERT_TRUE(e.nextCell());
    EXPECT_EQ(e.selectedCell()->column, 1);
    ASSERT_TRUE(e.nextCell(true));
    EXPECT_EQ(e.selection.focus.paragraph, 1);
    EXPECT_EQ(e.selection.focus.offset, 0);
}
TEST(Tables, InsertingDirectlyAfterTableKeepsSeparatingBodyParagraph) {
    Editor e;
    e.insertTable(1, 1);
    e.selection = {{1, 0}, {1, 0}};
    ASSERT_NO_THROW(e.insertTable(1, 1));
    ASSERT_EQ(e.document.tables.size(), 2);
    ASSERT_EQ(e.document.paragraphs.size(), 4);
    EXPECT_FALSE(e.document.paragraphs[1].cell);
    EXPECT_EQ(e.document.paragraphs[2].cell->tableId, e.document.tables[1].id);
    EXPECT_EQ(e.selection.focus.paragraph, 2);
    EXPECT_NO_THROW(validateDocument(e.document));
}

namespace {
// A 3x3 table whose cells hold "r,c"; returns the editor with the caret in (0,0).
Editor grid3() {
    Editor e;
    e.insertTable(3, 3);
    for (int i = 0; i < 9; ++i) {
        e.replace(std::u16string{char16_t(u'0' + i / 3), u',', char16_t(u'0' + i % 3)});
        if (i < 8)
            e.nextCell();
    }
    e.selection = {{0, 0}, {0, 0}};
    return e;
}
std::size_t cellIndex(const Editor &e, std::size_t row, std::size_t column) {
    for (std::size_t i = 0; i < e.document.paragraphs.size(); ++i)
        if (const auto &c = e.document.paragraphs[i].cell; c && c->row == row && c->column == column)
            return i;
    return static_cast<std::size_t>(-1);
}
std::u16string cellText(const Editor &e, std::size_t row, std::size_t column) {
    std::u16string text;
    for (const auto &p : e.document.paragraphs)
        if (p.cell && p.cell->row == row && p.cell->column == column)
            text += p.text() + u"|";
    return text;
}
} // namespace
TEST(Tables, MergeMovesContentIntoTheTopLeftCellAndSplitUndoes) {
    auto e = grid3();
    // Select from (0,0) to (1,1).
    e.selection = {{cellIndex(e, 0, 0), 1}, {cellIndex(e, 1, 1), 1}};
    e.mergeCells();
    const auto &table = e.document.tables[0];
    ASSERT_EQ(table.merges.size(), 1u);
    EXPECT_EQ(table.merges[0], (CellMerge{0, 0, 2, 2}));
    EXPECT_EQ(cellText(e, 0, 0), u"0,0|0,1|1,0|1,1|");
    EXPECT_EQ(cellText(e, 0, 1), u"|");
    EXPECT_EQ(cellText(e, 1, 1), u"|");
    EXPECT_EQ(e.selectedCell(), (TableCell{table.id, 0, 0}));
    EXPECT_NO_THROW(validateDocument(e.document));
    // Merging an overlapping range grows to include the existing merge.
    e.selection = {{cellIndex(e, 1, 1), 0}, {cellIndex(e, 1, 2), 0}};
    e.mergeCells();
    ASSERT_EQ(e.document.tables[0].merges.size(), 1u);
    EXPECT_EQ(e.document.tables[0].merges[0], (CellMerge{0, 0, 2, 3}));
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables[0].merges[0], (CellMerge{0, 0, 2, 2}));
    // Split from a covered cell removes the merge; content stays in the top-left cell.
    e.selection = {{cellIndex(e, 1, 0), 0}, {cellIndex(e, 1, 0), 0}};
    e.splitCell();
    EXPECT_TRUE(e.document.tables[0].merges.empty());
    EXPECT_EQ(cellText(e, 0, 0), u"0,0|0,1|1,0|1,1|");
    EXPECT_THROW(e.splitCell(), std::invalid_argument);
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.tables[0].merges.size(), 1u);
    // A selection inside one cell cannot merge.
    e.selection = {{cellIndex(e, 2, 2), 0}, {cellIndex(e, 2, 2), 2}};
    EXPECT_THROW(e.mergeCells(), std::invalid_argument);
}
TEST(Tables, MergesFollowRowAndColumnInsertsAndDeletes) {
    auto e = grid3();
    e.selection = {{cellIndex(e, 0, 1), 0}, {cellIndex(e, 1, 2), 0}};
    e.mergeCells(); // rows 0-1, columns 1-2
    auto merge = [&] { return e.document.tables[0].merges.at(0); };
    e.selection = {{cellIndex(e, 0, 0), 0}, {cellIndex(e, 0, 0), 0}};
    e.tableRow(true); // below row 0: inside the merge
    EXPECT_EQ(merge(), (CellMerge{0, 1, 3, 2}));
    e.selection = {{cellIndex(e, 0, 0), 0}, {cellIndex(e, 0, 0), 0}};
    e.tableRow(true, true); // above row 0: shifts it down
    EXPECT_EQ(merge(), (CellMerge{1, 1, 3, 2}));
    e.selection = {{cellIndex(e, 0, 2), 0}, {cellIndex(e, 0, 2), 0}};
    e.tableColumn(false); // delete column 2: narrower
    EXPECT_EQ(merge(), (CellMerge{1, 1, 3, 1}));
    e.selection = {{cellIndex(e, 0, 1), 0}, {cellIndex(e, 0, 1), 0}};
    e.tableColumn(false); // delete column 1: the merge is gone
    EXPECT_TRUE(e.document.tables[0].merges.empty());
    EXPECT_NO_THROW(validateDocument(e.document));
}
TEST(Tables, TabSkipsCoveredCells) {
    auto e = grid3();
    e.selection = {{cellIndex(e, 0, 0), 0}, {cellIndex(e, 1, 1), 0}};
    e.mergeCells();
    ASSERT_TRUE(e.nextCell());
    EXPECT_EQ(e.selectedCell()->column, 2u);
    EXPECT_EQ(e.selectedCell()->row, 0u);
    ASSERT_TRUE(e.nextCell());
    EXPECT_EQ(e.selectedCell()->row, 1u);
    EXPECT_EQ(e.selectedCell()->column, 2u); // (1,0) and (1,1) are covered
    ASSERT_TRUE(e.nextCell(true));
    ASSERT_TRUE(e.nextCell(true));
    EXPECT_EQ(e.selectedCell()->row, 0u);
    EXPECT_EQ(e.selectedCell()->column, 0u);
    // Tab from the last cell still adds a row.
    e.selection = {{cellIndex(e, 2, 2), 0}, {cellIndex(e, 2, 2), 0}};
    ASSERT_TRUE(e.nextCell());
    EXPECT_EQ(e.document.tables[0].rows, 4u);
    EXPECT_EQ(e.selectedCell()->row, 3u);
    EXPECT_EQ(e.selectedCell()->column, 0u);
}
TEST(Tables, ColumnWidthsFitThePageAndFollowColumnEdits) {
    auto e = grid3();
    const double width = e.document.page.width - 2 * e.document.page.margin;
    auto edges = columnEdges(e.document.tables[0], width);
    EXPECT_NEAR(edges[1], width / 3, 1e-9);
    EXPECT_EQ(edges.back(), width);
    e.setColumnWidth(100);
    edges = columnEdges(e.document.tables[0], width);
    EXPECT_NEAR(edges[1] - edges[0], 100, 1e-9);
    EXPECT_NEAR(edges[2] - edges[1], edges[3] - edges[2], 1e-9); // others share the rest
    EXPECT_THROW(e.setColumnWidth(10), std::invalid_argument);
    EXPECT_THROW(e.setColumnWidth(width), std::invalid_argument);
    e.tableColumn(true); // the new column copies its neighbour's width
    ASSERT_EQ(e.document.tables[0].widths.size(), 4u);
    EXPECT_EQ(e.document.tables[0].widths[1], e.document.tables[0].widths[0]);
    e.tableColumn(false);
    EXPECT_EQ(e.document.tables[0].widths.size(), 3u);
    e.distributeColumns();
    EXPECT_TRUE(e.document.tables[0].widths.empty());
    EXPECT_TRUE(e.undo());
    EXPECT_FALSE(e.document.tables[0].widths.empty());
    // Very narrow stored widths are widened to the minimum.
    Table narrow{1, 1, 3, {1, 1, 1000}, {}};
    edges = columnEdges(narrow, width);
    EXPECT_NEAR(edges[1], minimumColumnWidth, 1e-9);
    EXPECT_NEAR(edges[2] - edges[1], minimumColumnWidth, 1e-9);
    EXPECT_THROW(columnEdges(Table{1, 1, 10, {}, {}}, 400), std::runtime_error);
}
TEST(Tables, InvalidWidthsAndMergesAreRejected) {
    auto e = grid3();
    auto document = e.document;
    auto check = [&](auto change) {
        auto copy = document;
        change(copy.tables[0]);
        EXPECT_THROW(validateDocument(copy), std::invalid_argument);
    };
    check([](Table &t) { t.widths = {1, 2}; });
    check([](Table &t) { t.widths = {1, 0, 2}; });
    check([](Table &t) { t.merges = {{0, 0, 1, 1}}; });
    check([](Table &t) { t.merges = {{2, 2, 2, 1}}; });
    check([](Table &t) { t.merges = {{0, 0, 2, 2}, {1, 1, 2, 2}}; });
    check([](Table &t) { t.merges = {{0, 3, 1, 2}}; });
}
