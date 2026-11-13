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
