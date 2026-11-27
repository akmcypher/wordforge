#include "core/layout/layout.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
using namespace wf;
namespace {
Editor widowFixture(Shaper &shaper, std::size_t leading, std::size_t words, double slots) {
    Paragraph probe;
    probe.runs = {{u"lineword ", {}}};
    const auto line = shaper.shape(probe, 0, probe.text().size());
    std::u16string text;
    for (std::size_t n = 0; n < leading; ++n) text += u"lineword\n";
    for (std::size_t n = 0; n < words; ++n) text += n ? u" lineword" : u"lineword";
    Editor e;
    e.replace(text);
    e.selection = {{0, 0}, Position{e.document.paragraphs.size() - 1, e.document.paragraphs.back().text().size()}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    e.document.page = {line.width + 42, (line.ascent + line.descent) * slots + 40, 20};
    e.selection = {{leading, 0}, {leading, 0}};
    return e;
}
}
TEST(BaselineText, ShapingMovesAndScalesTextWithoutChangingModelSize) {
    Shaper shaper(systemFontFiles());
    Paragraph p;
    p.runs = {{u"x2", {}}};
    const auto ordinary = shaper.shape(p, 0, 2);
    p.runs[0].style.baseline = TextBaseline::Superscript;
    const auto raised = shaper.shape(p, 0, 2);
    EXPECT_LT(raised.width, ordinary.width);
    EXPECT_LT(raised.runs[0].glyphs[0].y, ordinary.runs[0].glyphs[0].y);
    p.runs[0].style.baseline = TextBaseline::Subscript;
    const auto lowered = shaper.shape(p, 0, 2);
    EXPECT_GT(lowered.runs[0].glyphs[0].y, ordinary.runs[0].glyphs[0].y);
    EXPECT_GT(lowered.descent, ordinary.descent);
    EXPECT_DOUBLE_EQ(p.runs[0].style.size, 12);
}
TEST(BaselineText, AutomaticFootnoteDoesNotDoubleScaleWithCharacterMode) {
    Shaper shaper(systemFontFiles());
    Editor e;
    e.insertFootnote(u"note");
    auto &p = e.document.paragraphs.edit(0);
    const auto ordinary = shaper.shape(p, 0, 1);
    p.runs[0].style.baseline = TextBaseline::Superscript;
    const auto raised = shaper.shape(p, 0, 1);
    EXPECT_DOUBLE_EQ(ordinary.width, raised.width);
    ASSERT_EQ(ordinary.runs.size(), raised.runs.size());
    EXPECT_DOUBLE_EQ(ordinary.runs[0].glyphs[0].y, raised.runs[0].glyphs[0].y);
}
TEST(BaselineText, DottedLeadersUseTheSameEffectiveSizeAndBaseline) {
    Shaper shaper(systemFontFiles());
    Paragraph p;
    p.runs = {{u"x\t2", {}}};
    p.runs[0].style.baseline = TextBaseline::Superscript;
    p.tabs = {{150, TabAlignment::Right, TabLeader::Dots}};
    const auto shaped = shaper.shape(p, 0, 3);
    ASSERT_EQ(shaped.runs.size(), 3u);
    const auto &tab = shaped.runs[1];
    ASSERT_FALSE(tab.glyphs.empty());
    Paragraph dot;
    dot.runs = {{u".", {}}};
    dot.runs[0].style.size *= 0.65;
    const auto expected = shaper.shape(dot, 0, 1);
    EXPECT_NEAR(tab.glyphs[0].advance, expected.runs[0].glyphs[0].advance, 0.001);
    EXPECT_NEAR(tab.glyphs[0].y, expected.runs[0].glyphs[0].y + tab.baselineOffset, 0.001);
}
TEST(Widows, AvoidsSingleFirstAndLastLinesAndKeepsThreeLines) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto orphan = widowFixture(shaper, 3, 4, 4.5);
    EXPECT_EQ(engine.compose(orphan.document).lines[3].page, 0u);
    orphan.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto protectedFirst = engine.compose(orphan.document);
    ASSERT_EQ(protectedFirst.lines.size(), 7u);
    for (std::size_t i = 3; i < 7; ++i) EXPECT_EQ(protectedFirst.lines[i].page, 1u);
    engine.clear();
    auto widow = widowFixture(shaper, 0, 4, 3.5);
    widow.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto protectedLast = engine.compose(widow.document);
    ASSERT_EQ(protectedLast.lines.size(), 4u);
    EXPECT_EQ(protectedLast.lines[0].page, 0u);
    EXPECT_EQ(protectedLast.lines[1].page, 0u);
    EXPECT_EQ(protectedLast.lines[2].page, 1u);
    EXPECT_EQ(protectedLast.lines[3].page, 1u);
    engine.clear();
    auto three = widowFixture(shaper, 2, 3, 4.5);
    three.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto protectedThree = engine.compose(three.document);
    ASSERT_EQ(protectedThree.lines.size(), 5u);
    for (std::size_t i = 2; i < 5; ++i) EXPECT_EQ(protectedThree.lines[i].page, 1u);
}
TEST(Widows, ColumnsContinuousSectionsAndKeepsUseTheSameReservations) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto columns = widowFixture(shaper, 0, 7, 3.5);
    columns.document.page.width = 2 * (columns.document.page.width - 40) + 40 +
                                  columns.document.page.columnGap;
    columns.document.page.columns = 2;
    columns.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto layout = engine.compose(columns.document);
    ASSERT_EQ(layout.lines.size(), 7u);
    EXPECT_EQ(layout.lines[0].page, 0u);
    EXPECT_EQ(layout.lines[3].page, 0u);
    EXPECT_GT(layout.lines[3].x, layout.lines[0].x);
    EXPECT_EQ(layout.lines[4].page, 0u);
    EXPECT_EQ(layout.lines[5].page, 1u);
    EXPECT_EQ(layout.lines[6].page, 1u);
    engine.clear();
    auto continuous = widowFixture(shaper, 3, 4, 4.5);
    continuous.document.paragraphs.edit(2).sectionBreak = SectionBreak{continuous.document.page, true};
    continuous.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto continued = engine.compose(continuous.document);
    EXPECT_EQ(continued.lines[3].page, 1u);
    EXPECT_EQ(continued.lines.back().page, 1u);
    engine.clear();
    auto kept = widowFixture(shaper, 3, 3, 4.5);
    kept.formatParagraph([](auto &p) { p.widowControl = true; });
    kept.selection = {{2, 0}, {2, 0}};
    kept.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto grouped = engine.compose(kept.document);
    EXPECT_EQ(grouped.lines[2].page, 1u);
    for (std::size_t n = 3; n < 6; ++n) EXPECT_EQ(grouped.lines[n].page, 1u);
}
TEST(Widows, FeasibleKeepPairWinsWhenThreeLineBodyCannotFitWithHeading) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto e = widowFixture(shaper, 1, 3, 3.5);
    e.formatParagraph([](auto &p) { p.widowControl = true; });
    e.selection = {{0, 0}, {0, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 4u);
    EXPECT_EQ(layout.lines[0].page, layout.lines[1].page);
    EXPECT_EQ(layout.lines[1].page, layout.lines[2].page);
}
TEST(Widows, FootnotesReserveSpaceBelowEarlierColumns) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"a\nb\nc\nd\nbody words words words words words words words words words words words");
    e.selection = {{0, 0}, {4, e.document.paragraphs[4].text().size()}};
    e.formatParagraph([](auto &p) {
        p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1;
    });
    const double height = engine.compose(e.document).lines[0].height;
    e.document.page = {320, height * 4.5 + 40, 20};
    e.document.page.columns = 2;
    e.selection = {{4, 4}, {4, 4}};
    e.insertFootnote(u"A note.");
    e.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.noteSeparators.size(), 1u);
    EXPECT_EQ(layout.lines[4].page, 1u);
    for (const auto &line : layout.lines)
        if (line.page == layout.noteSeparators[0].first)
            EXPECT_LE(line.baseline - line.shaped.ascent + line.height,
                      layout.noteSeparators[0].second);
}
TEST(Widows, LateFootnoteDoesNotCreateAnAvoidableFinalWidow) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto e = widowFixture(shaper, 0, 7, 4.1);
    e.document.page.width += 3;
    e.selection = {{0, e.document.paragraphs[0].text().size()},
                   {0, e.document.paragraphs[0].text().size()}};
    e.insertFootnote(u"A note.");
    e.formatParagraph([](auto &p) { p.widowControl = true; });
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 7u);
    EXPECT_EQ(layout.lines[0].page, 0u);
    EXPECT_EQ(layout.lines[2].page, 0u);
    EXPECT_EQ(layout.lines[3].page, 1u);
    EXPECT_EQ(layout.lines[4].page, 1u);
    EXPECT_EQ(layout.lines[5].page, 2u);
    EXPECT_EQ(layout.lines[6].page, 2u);
}
TEST(Widows, ImpossibleConstraintsRelaxAndEditsMatchFreshComposition) {
    Shaper shaper(systemFontFiles());
    LayoutEngine cached(shaper);
    auto e = widowFixture(shaper, 0, 3, 2.5);
    e.formatParagraph([](auto &p) { p.widowControl = true; });
    auto layout = cached.compose(e.document);
    EXPECT_EQ(layout.pageCount, 2u);
    EXPECT_EQ(layout.lines.front().page, 0u);
    e.replace(u"lineword lineword lineword lineword lineword lineword lineword");
    e.formatParagraph([](auto &p) { p.widowControl = true; p.pageBreakBefore = true; });
    layout = cached.compose(e.document);
    LayoutEngine fresh(shaper);
    const auto oracle = fresh.compose(e.document);
    ASSERT_EQ(layout.lines.size(), oracle.lines.size());
    EXPECT_EQ(layout.pageCount, oracle.pageCount);
    for (std::size_t n = 0; n < layout.lines.size(); ++n) {
        EXPECT_EQ(layout.lines[n].page, oracle.lines[n].page);
        EXPECT_DOUBLE_EQ(layout.lines[n].baseline, oracle.lines[n].baseline);
    }
}
TEST(Pagination, KeepsHeadingAndShortParagraphTogether) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"filler\nheading\nbody\nmore");
    e.selection = {{0, 0}, {3, 4}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    const auto natural = engine.compose(e.document);
    const double height = natural.lines[0].height;
    e.document.page = {300, height * 2.5 + 40, 20};
    e.selection = {{1, 0}, {1, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto kept = engine.compose(e.document);
    ASSERT_EQ(kept.lines.size(), 4u);
    EXPECT_EQ(kept.lines[0].page, 0u);
    EXPECT_EQ(kept.lines[1].page, 1u);
    EXPECT_EQ(kept.lines[2].page, 1u);
    e.undo();
    EXPECT_EQ(engine.compose(e.document).lines[1].page, 0u);
    // A two-line paragraph fits on a page, but not below the filler.
    e.selection = {{1, 0}, {1, 7}};
    e.replace(u"words words words words words words words words words words words words");
    e.formatParagraph([](auto &p) { p.keepLines = true; });
    const auto together = engine.compose(e.document);
    std::size_t count = 0;
    for (const auto &line : together.lines)
        if (line.paragraph == 1) { EXPECT_EQ(line.page, 1u); ++count; }
    EXPECT_EQ(count, 2u);
}
TEST(Pagination, KeepsUseColumnsAndExplicitBreaksWin) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"filler\nheading\nbody");
    e.selection = {{0, 0}, {2, 4}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    const double height = engine.compose(e.document).lines[0].height;
    auto page = e.document.page;
    page.width = 320; page.height = height * 2.5 + 40; page.margin = 20; page.columns = 2;
    e.setPage(page);
    e.selection = {{1, 0}, {1, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto kept = engine.compose(e.document);
    ASSERT_EQ(kept.lines.size(), 3u);
    EXPECT_EQ(kept.lines[1].page, 0u);
    EXPECT_GT(kept.lines[1].x, kept.lines[0].x);
    EXPECT_DOUBLE_EQ(kept.lines[1].x, kept.lines[2].x);
    e.selection = {{2, 0}, {2, 0}};
    e.formatParagraph([](auto &p) { p.pageBreakBefore = true; });
    const auto broken = engine.compose(e.document);
    EXPECT_EQ(broken.lines[1].page, 0u);
    EXPECT_DOUBLE_EQ(broken.lines[1].x, broken.lines[0].x);
    EXPECT_EQ(broken.lines[2].page, 1u);
    // A fresh engine is the oracle after changes; cached measurement must not retain placement.
    LayoutEngine fresh(shaper);
    const auto oracle = fresh.compose(e.document);
    ASSERT_EQ(broken.lines.size(), oracle.lines.size());
    for (std::size_t i = 0; i < broken.lines.size(); ++i) {
        EXPECT_EQ(broken.lines[i].page, oracle.lines[i].page);
        EXPECT_DOUBLE_EQ(broken.lines[i].baseline, oracle.lines[i].baseline);
    }
    e.document.paragraphs.edit(2).pageBreakBefore = false;
    e.document.paragraphs.edit(1).sectionBreak = SectionBreak{e.document.page, false};
    const auto section = engine.compose(e.document);
    EXPECT_EQ(section.lines[1].page, 0u);
    EXPECT_DOUBLE_EQ(section.lines[1].x, section.lines[0].x);
    EXPECT_EQ(section.lines[2].page, 1u);
}
TEST(Pagination, FootnotesMoveWithTheKeptGroup) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"filler\nfiller\nfiller\nheading\nbody");
    e.selection = {{0, 0}, {4, 4}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    const double height = engine.compose(e.document).lines[0].height;
    e.document.page = {300, height * 5 + 40, 20};
    e.selection = {{4, 4}, {4, 4}};
    e.insertFootnote(u"A footnote.");
    e.selection = {{3, 0}, {3, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto kept = engine.compose(e.document);
    EXPECT_EQ(kept.lines[3].page, 1u);
    EXPECT_EQ(kept.lines[4].page, 1u);
    ASSERT_FALSE(kept.furniture.empty());
    EXPECT_EQ(kept.furniture[0].page, 1u);
}
TEST(Pagination, OversizedKeepsSplitWithoutBlankPages) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"filler\na\nb\nc\nd\ne\nf\ng\nh");
    e.selection = {{0, 0}, {8, 1}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    const double height = engine.compose(e.document).lines[0].height;
    e.document.page = {300, height * 3.5 + 40, 20};
    e.selection = {{1, 0}, {8, 1}};
    e.formatParagraph([](auto &p) { p.keepWithNext = p.keepLines = true; });
    const auto kept = engine.compose(e.document);
    EXPECT_EQ(kept.lines[0].page, 0u);
    EXPECT_EQ(kept.lines[1].page, 1u);
    EXPECT_EQ(kept.pageCount, 4u);
    for (std::size_t page = 0; page < kept.pageCount; ++page)
        EXPECT_TRUE(std::any_of(kept.lines.begin(), kept.lines.end(),
                               [&](const auto &line) { return line.page == page; }));
    e.selection = {{1, 0}, {1, 1}};
    e.replace(std::u16string(500, u'W'));
    const auto longParagraph = engine.compose(e.document);
    EXPECT_GT(longParagraph.pageCount, 4u);
    EXPECT_EQ(longParagraph.lines.back().paragraph, 8u);
}
TEST(Pagination, ContinuousSectionPreflightsItsFirstKeptHeading) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"first\nsecond\nheading\nbody");
    e.selection = {{0, 0}, {3, 4}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    const double height = engine.compose(e.document).lines[0].height;
    e.document.page = {300, height * 3.5 + 40, 20};
    e.document.paragraphs.edit(1).sectionBreak = SectionBreak{e.document.page, true};
    e.selection = {{2, 0}, {2, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto kept = engine.compose(e.document);
    EXPECT_EQ(kept.lines[2].page, 1u);
    EXPECT_EQ(kept.lines[3].page, 1u);
}
TEST(Pagination, KeptColumnNotesDoNotOverlapEarlierColumns) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"a\nb\nc\nd\nheading\nbody");
    e.selection = {{0, 0}, {5, 4}};
    e.formatParagraph([](auto &p) { p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; });
    const double height = engine.compose(e.document).lines[0].height;
    e.document.page = {320, height * 4.5 + 40, 20};
    e.document.page.columns = 2;
    e.selection = {{5, 4}, {5, 4}};
    e.insertFootnote(u"A note.");
    e.selection = {{4, 0}, {4, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto kept = engine.compose(e.document);
    EXPECT_EQ(kept.lines[4].page, 1u);
    EXPECT_EQ(kept.lines[5].page, 1u);
    ASSERT_EQ(kept.noteSeparators.size(), 1u);
    for (const auto &line : kept.lines)
        if (line.page == kept.noteSeparators[0].first)
            EXPECT_LE(line.baseline - line.shaped.ascent + line.height,
                      kept.noteSeparators[0].second);
}
TEST(Pagination, OversizedChainStillHonoursIndividualKeepLines) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    const auto twice = u"words words words words words words words words words words words words";
    e.replace(std::u16string(twice) + u"\n" + twice + u"\n" + twice + u"\nend");
    e.selection = {{0, 0}, {3, 3}};
    e.formatParagraph([](auto &p) {
        p.spaceBefore = p.spaceAfter = 0; p.lineSpacing = 1; p.keepWithNext = true;
    });
    const double height = engine.compose(e.document).lines[0].height;
    e.document.page = {300, height * 3.5 + 40, 20};
    e.selection = {{1, 0}, {1, 0}};
    e.formatParagraph([](auto &p) { p.keepLines = true; });
    const auto kept = engine.compose(e.document);
    std::vector<std::size_t> pages;
    for (const auto &line : kept.lines)
        if (line.paragraph == 1) pages.push_back(line.page);
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0], pages[1]);
}
TEST(Tabs, AlignmentsUseShapedFieldsAcrossFormattingAndIndents) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    for (auto alignment : {TabAlignment::Left, TabAlignment::Center, TabAlignment::Right}) {
        Editor e;
        e.replace(u"Title\tWide 123");
        e.selection = {{0, 6}, {0, 10}};
        e.format([](auto &style) { style.bold = true; style.size = 18; });
        e.formatParagraph([&](auto &p) {
            p.indentLeft = 18;
            p.indentFirstLine = 12;
            p.tabs = {{240, alignment, TabLeader::Dots}};
        });
        for (bool listed : {false, true}) {
            e.formatParagraph([&](auto &p) { p.list = listed ? ListKind::Bullet : ListKind::None; });
            const auto layout = engine.compose(e.document);
            ASSERT_EQ(layout.lines.size(), 1u);
            const double begin = layout.caret({0, 6}).x;
            const double end = layout.caret({0, 14}).x;
            const double anchor = alignment == TabAlignment::Right ? end
                                  : alignment == TabAlignment::Center ? (begin + end) / 2
                                                                     : begin;
            EXPECT_NEAR(anchor, e.document.page.margin + 240, 0.1);
        }
    }
}
TEST(Tabs, AlignedMultiwordFieldMovesTogetherAfterUnbrokenPrefix) {
    Shaper shaper(systemFontFiles());
    Paragraph prefix, field;
    prefix.runs = {{u"Unbrokenprefix12345678901234567890", {}}};
    field.runs = {{u"field with several words", {}}};
    const auto width = shaper.shape(prefix, 0, prefix.text().size()).width +
                       shaper.shape(field, 0, field.text().size()).width - 20;
    Editor e;
    e.document.page.width = width + 72;
    e.document.page.margin = 36;
    e.replace(prefix.text() + u"\t" + field.text());
    e.formatParagraph([&](auto &p) { p.tabs = {{width, TabAlignment::Right}}; });
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 2);
    EXPECT_EQ(layout.lines[0].end, prefix.text().size());
    EXPECT_EQ(layout.lines[1].start, prefix.text().size());
    EXPECT_EQ(layout.lines[1].end, e.document.text().size());
}
TEST(Tabs, RightAlignedFieldAndLeaderKeepCaretEdges) {
    Editor editor;
    editor.replace(u"Heading\t123");
    editor.formatParagraph([](auto &p) { p.tabs = {{240, TabAlignment::Right, TabLeader::Dots}}; });
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(editor.document);
    EXPECT_NEAR(layout.caret({0, 11}).x, editor.document.page.margin + 240, 0.1);
    EXPECT_GT(layout.caret({0, 8}).x, layout.caret({0, 7}).x);
    EXPECT_EQ(layout.hitTest(layout.caret({0, 8}).x, layout.lines[0].baseline - 2, 0).offset, 8);
}
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

namespace {
Editor threePages() {
    Editor editor;
    editor.replace(u"one");
    editor.insertPageBreak();
    editor.replace(u"two");
    editor.insertPageBreak();
    editor.replace(u"three");
    return editor;
}
} // namespace
TEST(Layout, HeadersAndFootersRepeatInTheMarginsWithPageNumbers) {
    EXPECT_EQ(bandText(u"Page {PAGE} of {NUMPAGES}", 2, 3), u"Page 2 of 3");
    EXPECT_EQ(bandText(u"{PAGE", 2, 3), u"{PAGE");
    auto editor = threePages();
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto plain = engine.compose(editor.document);
    ASSERT_EQ(plain.pageCount, 3u);
    EXPECT_TRUE(plain.furniture.empty());
    auto page = editor.document.page;
    page.header = {u"Report", u"", u"Draft"};
    page.footer = {u"", u"Page {PAGE} of {NUMPAGES}", u""};
    editor.setPage(page);
    const auto layout = engine.compose(editor.document);
    ASSERT_EQ(layout.furniture.size(), 9u); // three pieces on each of three pages
    for (std::size_t i = 0; i < layout.furniture.size(); ++i) {
        const auto &line = layout.furniture[i];
        EXPECT_EQ(line.page, i / 3);
        const double top = line.baseline - line.shaped.ascent;
        if (i % 3 < 2) { // header: left at the margin, right ending at the margin
            EXPECT_GT(top, 0);
            EXPECT_LT(line.baseline, page.margin);
            EXPECT_NEAR(i % 3 == 0 ? line.x : line.x + line.shaped.width,
                        i % 3 == 0 ? page.margin : page.width - page.margin, 0.01);
        } else { // footer, centred
            EXPECT_GT(top, page.height - page.margin);
            EXPECT_LT(line.baseline, page.height);
            EXPECT_NEAR(line.x + line.shaped.width / 2, page.width / 2, 0.01);
        }
    }
    // The body does not move.
    ASSERT_EQ(layout.lines.size(), plain.lines.size());
    for (std::size_t i = 0; i < layout.lines.size(); ++i)
        EXPECT_EQ(layout.lines[i].baseline, plain.lines[i].baseline);

    page.differentFirstPage = true;
    editor.setPage(page);
    const auto skipped = engine.compose(editor.document);
    ASSERT_EQ(skipped.furniture.size(), 6u);
    EXPECT_EQ(skipped.furniture.front().page, 1u);

    page.margin = 20; // no room
    editor.setPage(page);
    EXPECT_TRUE(engine.compose(editor.document).furniture.empty());
}
TEST(Layout, FirstPageShowsItsOwnHeaderAndFooter) {
    auto editor = threePages();
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto page = editor.document.page;
    page.header = {u"Running", u"", u""};
    page.firstHeader = {u"", u"", u"Title"};
    page.firstFooter = {u"", u"{PAGE}", u""};
    editor.setPage(page);
    // Without the switch the first-page bands are unused.
    ASSERT_EQ(engine.compose(editor.document).furniture.size(), 3u);
    page.differentFirstPage = true;
    editor.setPage(page);
    const auto layout = engine.compose(editor.document);
    // Page 1: the first-page header and footer; pages 2 and 3: the header.
    ASSERT_EQ(layout.furniture.size(), 4u);
    EXPECT_EQ(layout.furniture[0].page, 0u);
    EXPECT_NEAR(layout.furniture[0].x + layout.furniture[0].shaped.width,
                page.width - page.margin, 0.01);
    EXPECT_EQ(layout.furniture[1].page, 0u);
    EXPECT_GT(layout.furniture[1].baseline, page.height - page.margin);
    EXPECT_EQ(layout.furniture[2].page, 1u);
    EXPECT_EQ(layout.furniture[3].page, 2u);
}
TEST(Layout, EvenPagesShowTheirOwnHeaderAndFooter) {
    auto editor = threePages();
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto page = editor.document.page;
    page.header = {u"Odd", u"", u""};
    page.evenHeader = {u"", u"", u"Even"};
    page.evenFooter = {u"", u"{PAGE}", u""};
    editor.setPage(page);
    // Without the switch, every page shows the header alone.
    auto layout = engine.compose(editor.document);
    ASSERT_EQ(layout.furniture.size(), 3u);
    page.differentOddEven = true;
    editor.setPage(page);
    layout = engine.compose(editor.document);
    // Pages 1 and 3: the header; page 2: the even-page header and footer.
    ASSERT_EQ(layout.furniture.size(), 4u);
    EXPECT_EQ(layout.furniture[0].page, 0u);
    EXPECT_EQ(layout.furniture[1].page, 1u);
    EXPECT_NEAR(layout.furniture[1].x + layout.furniture[1].shaped.width,
                page.width - page.margin, 0.01);
    EXPECT_EQ(layout.furniture[2].page, 1u);
    EXPECT_GT(layout.furniture[2].baseline, page.height - page.margin);
    EXPECT_EQ(layout.furniture[3].page, 2u);
    // An empty odd header with even-page text still draws the even pages.
    page.header = {};
    editor.setPage(page);
    EXPECT_EQ(engine.compose(editor.document).furniture.size(), 2u);
}
TEST(Layout, HeaderTextIsValidated) {
    PageStyle page;
    page.footer.center = u"a\nb";
    EXPECT_THROW(validatePage(page), std::invalid_argument);
    page.footer.center = std::u16string(501, u'x');
    EXPECT_THROW(validatePage(page), std::invalid_argument);
    page.footer.center = u"Page {PAGE}";
    EXPECT_NO_THROW(validatePage(page));
}

TEST(Layout, MergedCellsSpanTheirRowsAndColumnsWithCustomWidths) {
    Editor e;
    e.insertTable(3, 3);
    e.replace(u"merged");
    // Select (0,0) to (1,1).
    std::size_t target = 0;
    for (std::size_t i = 0; i < e.document.paragraphs.size(); ++i)
        if (e.document.paragraphs[i].cell == TableCell{e.document.tables[0].id, 1, 1})
            target = i;
    e.selection = {{0, 0}, {target, 0}};
    e.mergeCells();
    e.setColumnWidth(200); // column 0 (caret in the merged cell)
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.cells.size(), 6u); // 9 cells, 4 of them merged into one
    const auto &merged = layout.cells[0];
    const auto &right = layout.cells[1]; // (0,2)
    const auto &below = layout.cells[3]; // (2,0)
    const double width = e.document.page.width - 2 * e.document.page.margin;
    EXPECT_NEAR(merged.x, e.document.page.margin, 1e-9);
    // Column 0 is 200 pt; columns 1 and 2 share the rest.
    EXPECT_NEAR(merged.width, 200 + (width - 200) / 2, 1e-6);
    EXPECT_NEAR(right.x, merged.x + merged.width, 1e-6);
    EXPECT_NEAR(merged.height, layout.cells[2].y + layout.cells[2].height - merged.y, 1e-6);
    EXPECT_NEAR(below.y, merged.y + merged.height, 1e-6);
    // Clicking anywhere in the merged cell lands in its text.
    const auto hit = layout.hitTest(merged.x + merged.width - 5, merged.y + merged.height - 3, 0);
    EXPECT_EQ(e.document.paragraphs[hit.paragraph].cell,
              (TableCell{e.document.tables[0].id, 0, 0}));
    // Covered cells add no height: the merged cell is about two plain rows tall.
    EXPECT_GE(merged.height, 48);
    EXPECT_LT(merged.height, 56);
    // The caret in a covered cell shows inside the merged cell.
    const auto caret = layout.caret({target, 0});
    EXPECT_GE(caret.x, merged.x);
    EXPECT_LE(caret.x, merged.x + merged.width);
    EXPECT_GE(caret.y, merged.y);
    EXPECT_LE(caret.y, merged.y + merged.height);
}
TEST(Layout, VerticallyMergedRowsStayOnOnePage) {
    Editor e;
    e.insertTable(40, 1);
    auto cellAt = [&](std::size_t row) {
        for (std::size_t i = 0; i < e.document.paragraphs.size(); ++i)
            if (e.document.paragraphs[i].cell && e.document.paragraphs[i].cell->row == row)
                return i;
        return std::size_t{0};
    };
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    auto plain = engine.compose(e.document);
    // Find the first row on page 2 and merge it with the row before.
    std::size_t first = 0;
    for (const auto &cell : plain.cells)
        if (cell.page == 1) {
            first = e.document.paragraphs[cell.firstParagraph].cell->row;
            break;
        }
    ASSERT_GT(first, 0u);
    e.selection = {{cellAt(first - 1), 0}, {cellAt(first), 0}};
    e.mergeCells();
    const auto layout = engine.compose(e.document);
    for (const auto &cell : layout.cells)
        if (e.document.paragraphs[cell.firstParagraph].cell->row == first - 1) {
            EXPECT_EQ(cell.page, 1u); // moved to the next page as a whole
            EXPECT_NEAR(cell.height, 48, 1e-6);
        }
}

TEST(Layout, FootnotesSitAtTheBottomOfTheirPageNumberedInOrder) {
    Editor e;
    // Enough text for several pages, with a footnote every few paragraphs.
    std::vector<std::uint64_t> notes;
    for (int i = 0; i < 120; ++i) {
        e.replace(u"Paragraph text that fills a line or so of the page.");
        if (i % 7 == 3)
            notes.push_back(e.insertFootnote(u"A footnote explaining paragraph " +
                                             std::u16string(1, char16_t(u'A' + i % 26)) +
                                             u" in some detail so that it wraps onto a second "
                                             u"line at ten points on an A4 page."));
        e.insertParagraphBreak();
    }
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    ASSERT_GT(layout.pageCount, 2u);
    const auto &page = e.document.page;
    const double bottom = page.height - page.margin;
    // Each page with notes has one rule; note lines sit between it and the bottom margin,
    // below every body line of that page.
    std::map<std::size_t, double> rule;
    for (auto [p, y] : layout.noteSeparators)
        EXPECT_TRUE(rule.emplace(p, y).second);
    EXPECT_FALSE(rule.empty());
    std::map<std::size_t, double> lowestBody;
    for (const auto &line : layout.lines)
        lowestBody[line.page] =
            std::max(lowestBody[line.page], line.baseline - line.shaped.ascent + line.height);
    std::size_t noteLines = 0;
    for (const auto &line : layout.furniture) {
        ASSERT_TRUE(rule.contains(line.page));
        const double top = line.baseline - line.shaped.ascent;
        EXPECT_GT(top, rule[line.page]);
        EXPECT_LE(top + line.height, bottom + 0.5);
        EXPECT_GE(top, lowestBody[line.page] - 0.5);
        ++noteLines;
    }
    EXPECT_GE(noteLines, notes.size());
    // A reference is drawn as a raised superscript number on the same page as its note.
    std::size_t index = 0;
    for (const auto &line : layout.lines) {
        const auto &paragraph = e.document.paragraphs[line.paragraph];
        std::size_t offset = 0;
        for (const auto &run : paragraph.runs) {
            if (run.note() && offset >= line.start && offset < line.end) {
                ++index;
                EXPECT_TRUE(rule.contains(line.page));
                bool raised = false;
                for (const auto &glyphs : line.shaped.runs)
                    if (glyphs.start == offset && !glyphs.glyphs.empty())
                        raised = glyphs.glyphs[0].y < 0 && glyphs.style.size < 12;
                EXPECT_TRUE(raised);
            }
            offset += run.text().size();
        }
    }
    EXPECT_EQ(index, notes.size());
}

TEST(Layout, CellsCarryTheirFillAndTableBorder) {
    Editor e;
    e.insertTable(2, 2);
    e.setTableBorder({2, 0xff336699});
    e.shadeCells(0xffffeecc); // the caret cell (0,0)
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.cells.size(), 4u);
    EXPECT_EQ(layout.cells[0].fill, 0xffffeeccu);
    EXPECT_EQ(layout.cells[1].fill, 0u);
    for (const auto &cell : layout.cells)
        for (int side = 0; side < 6; ++side)
            if (side < 4)
                EXPECT_EQ(cell.borders[side], (TableBorder{2, 0xff336699}));
            else // no diagonals unless the cell sets them
                EXPECT_EQ(cell.borders[side].width, 0);
}
TEST(Layout, FirstLineHangingAndRightIndentsAndJustifiedLines) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    std::u16string text;
    for (int n = 0; n < 40; ++n)
        text += u"justified words wrap ";
    e.replace(text);
    e.formatParagraph([](auto &p) {
        p.indentLeft = 36;
        p.indentFirstLine = -18; // hanging
        p.indentRight = 72;
        p.alignment = Alignment::Justify;
    });
    const auto layout = engine.compose(e.document);
    ASSERT_GT(layout.lines.size(), 3u);
    const double margin = e.document.page.margin;
    const double right = e.document.page.width - margin - 72;
    EXPECT_DOUBLE_EQ(layout.lines[0].x, margin + 18);
    EXPECT_DOUBLE_EQ(layout.lines[1].x, margin + 36);
    for (std::size_t n = 0; n + 1 < layout.lines.size(); ++n) {
        // Justified: every line but the last reaches the right indent (its last space hangs).
        const auto &line = layout.lines[n];
        double hanging = 0;
        for (const auto &run : line.shaped.runs)
            for (const auto &glyph : run.glyphs)
                if (glyph.cluster + 1 == line.end)
                    hanging = glyph.advance;
        EXPECT_NEAR(line.x + line.shaped.width - hanging, right, 0.5) << n;
    }
    const auto &last = layout.lines.back();
    EXPECT_LT(last.x + last.shaped.width, right - 1);
    // Carets follow the widened spaces.
    const auto &second = layout.lines[1];
    EXPECT_NEAR(layout.caret({0, second.end - 1}).x, right, 0.5);
    // A first-line indent and a list's moved bullet.
    e.formatParagraph([](auto &p) {
        p.indentLeft = 0;
        p.indentFirstLine = 24;
        p.alignment = Alignment::Left;
    });
    const auto indented = engine.compose(e.document);
    EXPECT_DOUBLE_EQ(indented.lines[0].x, margin + 24);
    EXPECT_DOUBLE_EQ(indented.lines[1].x, margin);
    e.formatParagraph([](auto &p) { p.list = ListKind::Bullet; });
    const auto listed = engine.compose(e.document);
    EXPECT_GT(listed.lines[0].markerX, margin + 24);
    EXPECT_DOUBLE_EQ(listed.lines[1].x, margin + 18);
    EXPECT_THROW(e.formatParagraph([](auto &p) { p.indentFirstLine = -1; }), std::exception);
}
TEST(Layout, EndnotesFollowTheTextNumberedInRomanNumerals) {
    Editor e;
    e.replace(u"Body with a footnote");
    e.insertFootnote(u"Footnote text.");
    e.replace(u" and endnotes");
    e.insertFootnote(u"First endnote.", true);
    e.insertFootnote(u"Second endnote.", true);
    e.insertParagraphBreak();
    e.replace(u"Last paragraph.");
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.pageCount, 1u);
    // One rule for the endnotes, below the text, and one above the footnote.
    ASSERT_EQ(layout.noteSeparators.size(), 2u);
    const double lastBody = layout.lines.back().baseline;
    const auto endnoteRule =
        std::min(layout.noteSeparators[0].second, layout.noteSeparators[1].second);
    EXPECT_GT(endnoteRule, lastBody);
    // Three note lines: two endnotes after the text, the footnote at the page bottom.
    ASSERT_EQ(layout.furniture.size(), 3u);
    std::vector<double> tops;
    for (const auto &line : layout.furniture)
        tops.push_back(line.baseline - line.shaped.ascent);
    std::sort(tops.begin(), tops.end());
    EXPECT_GT(tops[0], endnoteRule);
    const auto footnoteRule =
        std::max(layout.noteSeparators[0].second, layout.noteSeparators[1].second);
    EXPECT_LT(tops[1], footnoteRule);
    EXPECT_GT(tops[2], footnoteRule);
    EXPECT_EQ(romanNumeral(1), "i");
    EXPECT_EQ(romanNumeral(4), "iv");
    EXPECT_EQ(romanNumeral(1994), "mcmxciv");
}
TEST(Layout, LongEndnotesContinueOnNewPages) {
    Editor e;
    e.replace(u"Text");
    for (int i = 0; i < 60; ++i)
        e.insertFootnote(u"An endnote long enough to wrap onto a second line at ten points on "
                         u"an A4 page, repeated many times.",
                         true);
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    EXPECT_GE(layout.pageCount, 2u);
    const double bottom = e.document.page.height - e.document.page.margin;
    for (const auto &line : layout.furniture)
        EXPECT_LE(line.baseline - line.shaped.ascent + line.height, bottom + 0.5);
    EXPECT_EQ(layout.furniture.back().page, layout.pageCount - 1);
}
TEST(Layout, SectionsChangePageSizeAndColumnsFlowLeftToRight) {
    Editor e;
    e.replace(u"Portrait text");
    e.insertSectionBreak(false);
    // The second section: landscape, two columns, enough text to fill the first column.
    auto page = e.sectionPage();
    std::swap(page.width, page.height);
    page.columns = 2;
    e.selection = {{1, 0}, {1, 0}};
    e.setPage(page);
    for (int i = 0; i < 60; ++i) {
        e.replace(u"Column text that wraps across a narrow column of the landscape page.");
        e.insertParagraphBreak();
    }
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    ASSERT_GE(layout.pageCount, 2u);
    EXPECT_LT(layout.pageStyle(0).width, layout.pageStyle(0).height); // portrait
    EXPECT_GT(layout.pageStyle(1).width, layout.pageStyle(1).height); // landscape
    EXPECT_EQ(layout.pageStyle(1).columns, 2u);
    // Page 2 holds lines in both columns: the second starts at the column top, to the right.
    const auto &sheet = layout.pageStyle(1);
    const double columnWidth = (sheet.width - 2 * sheet.margin - sheet.columnGap) / 2;
    double leftTop = 1e9, rightTop = 1e9, leftMaxX = 0;
    for (const auto &line : layout.lines) {
        if (line.page != 1)
            continue;
        const double top = line.baseline - line.shaped.ascent;
        if (line.x < sheet.margin + columnWidth) {
            leftTop = std::min(leftTop, top);
            leftMaxX = std::max(leftMaxX, line.x + line.shaped.width);
        } else
            rightTop = std::min(rightTop, top);
    }
    EXPECT_LE(leftMaxX, sheet.margin + columnWidth + 0.5);
    EXPECT_LT(rightTop, 1e9);
    EXPECT_NEAR(leftTop, rightTop, 0.5);
    // A continuous break keeps the next section on the same page, below the text.
    Editor c;
    c.replace(u"Above");
    c.insertSectionBreak(true);
    c.selection = {{1, 0}, {1, 0}};
    c.replace(u"Below, same page");
    const auto same = engine.compose(c.document);
    EXPECT_EQ(same.pageCount, 1u);
    EXPECT_GT(same.lines[1].baseline, same.lines[0].baseline);
}
TEST(Spacing, ContextualSpacingDropsSpaceOnlyNextToTheSameStyle) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"one\ntwo\nthree");
    e.selection = {{0, 0}, {2, 1}};
    e.formatParagraph([](auto &p) {
        p.spaceBefore = 10;
        p.spaceAfter = 20;
        p.lineSpacing = 1;
    });
    auto gap = [&](std::size_t a) {
        engine.clear();
        const auto layout = engine.compose(e.document);
        const auto &first = layout.lines[a], &second = layout.lines[a + 1];
        return (second.baseline - second.shaped.ascent) -
               (first.baseline - first.shaped.ascent + first.height);
    };
    EXPECT_NEAR(gap(0), 30, 0.01);
    e.selection = {{0, 0}, {0, 0}};
    e.formatParagraph([](auto &p) { p.contextualSpacing = true; });
    EXPECT_NEAR(gap(0), 10, 0.01); // only the first paragraph drops its space after
    EXPECT_NEAR(gap(1), 30, 0.01);
    e.selection = {{0, 0}, {2, 1}};
    e.formatParagraph([](auto &p) { p.contextualSpacing = true; });
    EXPECT_NEAR(gap(0), 0, 0.01);
    EXPECT_NEAR(gap(1), 0, 0.01);
    e.selection = {{2, 0}, {2, 0}};
    e.applyStyle("Heading1");
    EXPECT_NEAR(gap(0), 0, 0.01);
    // Different styles keep their spacing (Heading 1 brings its own space before).
    EXPECT_GT(e.document.paragraphs[2].spaceBefore, 0);
    EXPECT_NEAR(gap(1), e.document.paragraphs[1].spaceAfter + e.document.paragraphs[2].spaceBefore, 0.01);
}
TEST(Spacing, CharacterSpacingFollowsEachCharacterWithoutLigatures) {
    Shaper shaper(systemFontFiles());
    Paragraph p;
    p.runs = {{u"abc", {}}};
    const auto plain = shaper.shape(p, 0, 3);
    p.runs[0].style.spacing = 2;
    const auto spaced = shaper.shape(p, 0, 3);
    EXPECT_NEAR(spaced.width, plain.width + 6, 0.001);
    ASSERT_EQ(spaced.runs[0].glyphs.size(), 3u);
    EXPECT_NEAR(spaced.runs[0].glyphs[1].x, plain.runs[0].glyphs[1].x + 2, 0.001);
    p.runs[0].style.spacing = -1;
    EXPECT_NEAR(shaper.shape(p, 0, 3).width, plain.width - 3, 0.001);
    p.runs = {{u"office", {}}};
    p.runs[0].style.spacing = 1;
    EXPECT_EQ(shaper.shape(p, 0, 6).runs[0].glyphs.size(), 6u);
}
TEST(Lists, LayoutShowsEachLevelsLabel) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    Editor e;
    e.replace(u"one\nnested\ntwo");
    e.selection = {{0, 0}, {2, 1}};
    e.formatParagraph([](auto &p) { p.list = ListKind::Decimal; });
    e.selection = {{1, 0}, {1, 0}};
    e.formatParagraph([](auto &p) {
        p.listLevel = 1;
        p.indentLeft = 36;
    });
    auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.lines.size(), 3u);
    EXPECT_EQ(layout.lines[0].markerText, u"1.");
    EXPECT_EQ(layout.lines[1].markerText, u"a.");
    EXPECT_EQ(layout.lines[2].markerText, u"2.");
    EXPECT_GT(layout.lines[1].markerX, layout.lines[0].markerX);
    ListDefinition list{e.document.allocateId(), {}};
    list.levels[0] = {NumberFormat::Decimal, u"Step %1:", 1};
    list.levels[1] = {NumberFormat::Decimal, u"%1.%2", 1};
    e.document.lists.push_back(list);
    e.selection = {{0, 0}, {2, 1}};
    e.formatParagraph([&](auto &p) { p.listId = list.id; });
    layout = engine.compose(e.document);
    EXPECT_EQ(layout.lines[0].markerText, u"Step 1:");
    EXPECT_EQ(layout.lines[1].markerText, u"1.1");
    EXPECT_EQ(layout.lines[2].markerText, u"Step 2:");
}
TEST(CellBorders, EditingMovesThemWithTheirCellsAndTheTableBorderReplacesThem) {
    Editor e;
    e.insertTable(2, 2);
    auto &table = e.document.tables[0];
    table.cellBorders.push_back({1, 1, CellSide::Left, {2, 0xffff0000}});
    validateDocument(e.document);
    e.selection = {{0, 0}, {0, 0}}; // first cell
    e.tableRow(true, true);         // a row above: the border moves down with its cell
    ASSERT_EQ(e.document.tables[0].cellBorders.size(), 1u);
    EXPECT_EQ(e.document.tables[0].cellBorders[0].row, 2u);
    e.tableColumn(false); // the first column goes; the border's cell moves left
    ASSERT_EQ(e.document.tables[0].cellBorders.size(), 1u);
    EXPECT_EQ(e.document.tables[0].cellBorders[0].column, 0u);
    // A layout draws each side.
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(e.document);
    bool found = false;
    for (const auto &cell : layout.cells)
        if (cell.borders[static_cast<int>(CellSide::Left)] == TableBorder{2, 0xffff0000})
            found = true;
    EXPECT_TRUE(found);
    e.undo();
    EXPECT_EQ(e.document.tables[0].cellBorders[0].column, 1u);
    // Choosing the table's border applies it to every side.
    e.setTableBorder({1, 0xff000000});
    EXPECT_TRUE(e.document.tables[0].cellBorders.empty());
    // Repeated, unordered or out-of-range sides are rejected.
    auto bad = e.document;
    bad.tables[0].cellBorders = {{9, 0, CellSide::Top, {}}};
    EXPECT_THROW(validateDocument(bad), std::invalid_argument);
    bad.tables[0].cellBorders = {{0, 0, CellSide::Top, {}}, {0, 0, CellSide::Top, {}}};
    EXPECT_THROW(validateDocument(bad), std::invalid_argument);
    bad.tables[0].cellBorders = {{0, 0, CellSide::Bottom, {}}, {0, 0, CellSide::Top, {}}};
    EXPECT_THROW(validateDocument(bad), std::invalid_argument); // out of order
    bad.tables[0].cellBorders = {{0, 0, CellSide::Top, {}}, {0, 0, CellSide::Bottom, {}}};
    EXPECT_NO_THROW(validateDocument(bad));
}
