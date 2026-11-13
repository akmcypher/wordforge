#include "core/layout/layout.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
using namespace wf;
TEST(Layout, RightToLeftImageCaretUsesLogicalEdges) {
    std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path() /
                           "fixtures/red-blue.png",
                       std::ios::binary);
    ASSERT_TRUE(file.good());
    auto resource = std::make_shared<const ImageResource>(
        ImageResource{4, 2, std::string((std::istreambuf_iterator<char>(file)), {})});
    Editor editor;
    editor.replace(u"אב גד");
    editor.selection = {{0, 3}, {0, 3}};
    editor.insertImage(resource, 72, 36);
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(editor.document);
    const auto &line = layout.lines[0];
    const auto picture = std::find_if(line.shaped.runs.begin(), line.shaped.runs.end(),
                                      [](const auto &run) { return run.image.has_value(); });
    ASSERT_NE(picture, line.shaped.runs.end());
    ASSERT_TRUE(picture->rtl);
    EXPECT_NEAR(layout.caret({0, 3}).x, line.x + picture->x + picture->width, 0.01);
    EXPECT_EQ(
        layout.hitTest(line.x + picture->x + picture->width, line.baseline - 5, line.page).offset,
        3);
}
TEST(Typography, UbuntuBitmapEmojiUsesRequestedPointSize) {
    const std::string path = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf";
    if (!std::filesystem::exists(path))
        GTEST_SKIP() << "Ubuntu Noto Color Emoji fixture not installed";
    Shaper shaper({path});
    Paragraph p;
    p.runs = {{u"🙂", {}}};
    auto small = shaper.shape(p, 0, p.text().size());
    p.runs[0].style.size = 24;
    auto large = shaper.shape(p, 0, p.text().size());
    EXPECT_NEAR(large.width / small.width, 2, 0.05);
    EXPECT_LT(small.ascent, 30);
    ASSERT_FALSE(small.runs.empty());
    EXPECT_NE(small.runs[0].glyphs[0].id, 0);
}
TEST(Layout, NumberingRenumbersAfterInsertionAndMarkerDoesNotBecomeEditableText) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"one\ntwo");
    e.selection = {{0, 0}, {1, 3}};
    e.formatParagraph([](auto &p) { p.list = ListKind::Decimal; });
    auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_EQ(layout.lines[0].markerText, u"1.");
    EXPECT_EQ(layout.lines[1].markerText, u"2.");
    EXPECT_EQ(e.document.text(), u"one\ntwo");
    e.selection = {{0, 0}, {0, 0}};
    e.insertParagraphBreak();
    layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 3u);
    EXPECT_EQ(layout.lines[2].markerText, u"3.");
}
TEST(Layout, AppliesIndentSpacingAndExplicitPageBreaks) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"first\nsecond");
    e.selection = {{0, 0}, {0, 5}};
    e.formatParagraph([](auto &p) {
        p.indentLeft = 36;
        p.lineSpacing = 2;
    });
    e.selection = {{1, 0}, {1, 6}};
    e.formatParagraph([](auto &p) { p.pageBreakBefore = true; });
    auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_DOUBLE_EQ(layout.lines[0].x, e.document.page.margin + 36);
    EXPECT_GT(layout.lines[0].height, layout.lines[1].height);
    EXPECT_EQ(layout.lines[1].page, 1u);
    EXPECT_EQ(layout.pageCount, 2u);
}
TEST(Typography, ShapesLatinArabicAndIndicWithRealGlyphs) {
    Shaper s(systemFontFiles());
    for (auto text : {u"office", u"مرحبا", u"नमस्ते"}) {
        Paragraph p;
        p.runs = {{text, {}}};
        auto line = s.shape(p, 0, p.text().size());
        EXPECT_GT(line.width, 10);
        EXPECT_GT(line.ascent, 0);
        ASSERT_FALSE(line.runs.empty());
        for (auto &run : line.runs)
            for (auto g : run.glyphs)
                EXPECT_NE(g.id, 0);
    }
}
TEST(Typography, OffersEveryInstalledFamilyAndFallsBackToACommonFace) {
    Shaper s(systemFontFiles());
    const auto families = s.families();
    // Every installed font is offered, not a short built-in list.
    EXPECT_GT(families.size(), 20u);
    std::string common;
    for (auto name : {"Arial", "Liberation Sans", "DejaVu Sans", "Noto Sans"})
        if (common.empty() && std::find(families.begin(), families.end(), name) != families.end())
            common = name;
    ASSERT_FALSE(common.empty());
    auto shapedWith = [&](std::string family) {
        Paragraph p;
        CharacterStyle style;
        style.family = family;
        p.runs = {{u"office", style}};
        return s.shape(p, 0, p.text().size()).runs.at(0).fontPath;
    };
    // A missing family (e.g. Calibri in an imported DOCX) uses a common face.
    EXPECT_EQ(shapedWith("No Such Font Family"), shapedWith(common));
}
TEST(Layout, PaginatesWithinMarginsAndReusesUnchangedParagraphs) {
    Shaper s(systemFontFiles());
    LayoutEngine engine(s);
    Editor e;
    e.document.page = {220, 140, 25};
    e.replace(u"Words wrap on a small page. This sentence needs several lines.\nSecond "
              u"paragraph.\nThird paragraph with more words that must wrap.");
    auto first = engine.compose(e.document);
    EXPECT_GT(first.pageCount, 1u);
    EXPECT_EQ(first.measuredParagraphs, 3u);
    for (const auto &line : first.lines) {
        EXPECT_GE(line.x, 25);
        EXPECT_LE(line.x + line.shaped.width, 195.01);
        EXPECT_LE(line.baseline + line.shaped.descent, 115.01);
    }
    auto unchanged = engine.compose(e.document);
    EXPECT_EQ(unchanged.measuredParagraphs, 0u);
    e.replace(u"!");
    auto edited = engine.compose(e.document);
    EXPECT_EQ(edited.measuredParagraphs, 1u);
}
TEST(Layout, WrapsLongTokensWithoutSplittingEmoji) {
    Shaper s(systemFontFiles());
    LayoutEngine engine(s);
    Editor e;
    e.document.page = {100, 200, 20};
    e.replace(u"abcdefghijklmnopqrstuv👩‍💻xyz");
    auto l = engine.compose(e.document);
    ASSERT_GT(l.lines.size(), 1u);
    for (auto &line : l.lines) {
        EXPECT_NE(line.end, 23u);
        EXPECT_NE(line.end, 24u);
        EXPECT_NE(line.end, 25u);
        EXPECT_NE(line.end, 26u);
    }
}
TEST(Typography, TabsAdvanceToPhysicalStopsWithoutMissingGlyphs) {
    Shaper shaper(systemFontFiles());
    Paragraph paragraph;
    paragraph.runs = {{u"a\tb", {}}};
    const auto shaped = shaper.shape(paragraph, 0, 3);
    bool found = false;
    for (const auto &run : shaped.runs) {
        for (const auto &glyph : run.glyphs)
            EXPECT_NE(glyph.id, 0);
        if (run.start == 2) {
            EXPECT_DOUBLE_EQ(run.x, 36);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
TEST(Layout, TableCellsWrapHitTestAndPaginateAsWholeRows) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.insertTable(12, 2);
    e.replace(u"left cell words wrap within a narrow column with several more words");
    e.nextCell();
    e.replace(u"right");
    auto layout = engine.compose(e.document);
    auto left = layout.caret({0, 0}), right = layout.caret({1, 0});
    EXPECT_GT(right.x - left.x, 100);
    EXPECT_NEAR(left.y, right.y, 0.01);
    EXPECT_EQ(layout.hitTest(right.x + 1, right.y + 2, right.page).paragraph, 1);
    EXPECT_EQ(layout.hitTest(left.x + 1, left.y + 2, left.page).paragraph, 0);
    auto page = e.document.page;
    page.height = 200;
    page.margin = 20;
    e.setPage(page);
    layout = engine.compose(e.document);
    EXPECT_GT(layout.pageCount, 1);
    for (std::size_t row = 0; row < 12; ++row)
        EXPECT_EQ(layout.caret({row * 2, 0}).page, layout.caret({1 + row * 2, 0}).page);
    e.selection = {{0, 0}, {0, 0}};
    e.replace(std::u16string(4000, u'X'));
    EXPECT_THROW(engine.compose(e.document), std::runtime_error);
}
TEST(Layout, NumberedListsInsideCellsRestartAtOnePerCell) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.insertTable(1, 2);
    e.replace(u"first\nsecond");
    e.selection = {{0, 0}, {1, 6}};
    e.formatParagraph([](auto &p) { p.list = ListKind::Decimal; });
    e.nextCell();
    e.replace(u"other");
    e.formatParagraph([](auto &p) { p.list = ListKind::Decimal; });
    auto layout = engine.compose(e.document);
    std::vector<std::u16string> markers;
    for (const auto &line : layout.lines)
        if (!line.markerText.empty())
            markers.push_back(line.markerText);
    ASSERT_EQ(markers.size(), 3);
    EXPECT_EQ(markers[0], u"1.");
    EXPECT_EQ(markers[1], u"2.");
    EXPECT_EQ(markers[2], u"1.");
}
