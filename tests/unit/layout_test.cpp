#include "core/layout/layout.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
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
    EXPECT_EQ(e.document.paragraphs[hit.paragraph].cell, (TableCell{e.document.tables[0].id, 0, 0}));
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
            if (e.document.paragraphs[i].cell &&
                e.document.paragraphs[i].cell->row == row)
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
        EXPECT_EQ(cell.border, (TableBorder{2, 0xff336699}));
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
    const auto endnoteRule = std::min(layout.noteSeparators[0].second, layout.noteSeparators[1].second);
    EXPECT_GT(endnoteRule, lastBody);
    // Three note lines: two endnotes after the text, the footnote at the page bottom.
    ASSERT_EQ(layout.furniture.size(), 3u);
    std::vector<double> tops;
    for (const auto &line : layout.furniture)
        tops.push_back(line.baseline - line.shaped.ascent);
    std::sort(tops.begin(), tops.end());
    EXPECT_GT(tops[0], endnoteRule);
    const auto footnoteRule = std::max(layout.noteSeparators[0].second, layout.noteSeparators[1].second);
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
