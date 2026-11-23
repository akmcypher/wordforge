#include "core/rendering/page_renderer.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <include/core/SkSurface.h>
using namespace wf;
TEST(Rendering, HeadersAndFootersArePaintedAndInvalidatePages) {
    Editor editor;
    editor.replace(u"body");
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    PageRenderer renderer;
    auto inked = [&](double top, double bottom) {
        auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(600, 850));
        renderer.drawPage(*surface->getCanvas(), 0);
        SkPixmap pixels;
        surface->peekPixels(&pixels);
        int count = 0;
        for (int y = static_cast<int>(top); y < static_cast<int>(bottom); ++y)
            for (int x = 0; x < 595; ++x)
                if ((pixels.getColor(x, y) & 0xffffff) != 0xffffff)
                    ++count;
        return count;
    };
    renderer.synchronize(engine.compose(editor.document));
    const double margin = editor.document.page.margin, height = editor.document.page.height;
    EXPECT_EQ(inked(0, margin - 2), 0);
    EXPECT_EQ(inked(height - margin + 2, height), 0);
    auto page = editor.document.page;
    page.header.left = u"Header text";
    page.footer.right = u"Page {PAGE}";
    editor.setPage(page);
    EXPECT_EQ(renderer.synchronize(engine.compose(editor.document)), 1u);
    EXPECT_GT(inked(0, margin - 2), 20);
    EXPECT_GT(inked(height - margin + 2, height), 20);
}
TEST(Rendering, FootnoteRuleAndTextArePainted) {
    Editor editor;
    editor.replace(u"body");
    editor.insertFootnote(u"The footnote text");
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(editor.document);
    ASSERT_EQ(layout.noteSeparators.size(), 1u);
    PageRenderer renderer;
    renderer.synchronize(layout);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(600, 850));
    renderer.drawPage(*surface->getCanvas(), 0);
    SkPixmap pixels;
    surface->peekPixels(&pixels);
    const auto ruleY = static_cast<int>(std::lround(layout.noteSeparators[0].second));
    int ink = 0;
    for (int dy = -1; dy <= 1; ++dy)
        if ((pixels.getColor(100, ruleY + dy) & 0xffffff) != 0xffffff)
            ++ink;
    EXPECT_GT(ink, 0);
    int text = 0;
    for (int y = ruleY + 3; y < 841 - 56; ++y)
        for (int x = 57; x < 300; ++x)
            if ((pixels.getColor(x, y) & 0xffffff) != 0xffffff)
                ++text;
    EXPECT_GT(text, 30);
}
TEST(Rendering, InlinePicturesUseResourcesAndProduceActualPixels) {
    std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path() /
                           "fixtures/red-blue.png",
                       std::ios::binary);
    ASSERT_TRUE(file.good());
    auto resource = std::make_shared<ImageResource>(
        ImageResource{4, 2, std::string((std::istreambuf_iterator<char>(file)), {})});
    Editor editor;
    editor.insertImage(resource, 72, 36, "red and blue");
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    const auto layout = engine.compose(editor.document);
    ASSERT_EQ(layout.lines.size(), 1);
    EXPECT_GE(layout.lines[0].height, 36);
    PageRenderer renderer;
    renderer.synchronize(layout);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(600, 850));
    ASSERT_TRUE(surface);
    renderer.drawPage(*surface->getCanvas(), 0);
    SkPixmap pixels;
    ASSERT_TRUE(surface->peekPixels(&pixels));
    EXPECT_EQ(pixels.getColor(65, 65) & 0xffffff, 0xff0000);
    EXPECT_EQ(pixels.getColor(120, 65) & 0xffffff, 0x0000ff);
}
TEST(Rendering, DecorationsAndListMarkersAreRenderedAndInvalidatePictures) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    PageRenderer renderer;
    Editor e;
    e.replace(u"marked");
    e.selection = {{0, 0}, {0, 6}};
    renderer.synchronize(engine.compose(e.document));
    e.format([](auto &s) { s.highlight = 0xffffff00; });
    EXPECT_EQ(renderer.synchronize(engine.compose(e.document)), 1u);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(600, 850));
    ASSERT_TRUE(surface);
    renderer.drawPage(*surface->getCanvas(), 0);
    SkPixmap pixels;
    ASSERT_TRUE(surface->peekPixels(&pixels));
    int yellow = 0;
    for (int y = 55; y < 90; ++y)
        for (int x = 55; x < 150; ++x)
            if ((pixels.getColor(x, y) & 0xffffff) == 0xffff00)
                ++yellow;
    EXPECT_GT(yellow, 50);
    e.format([](auto &s) { s.strike = true; });
    EXPECT_EQ(renderer.synchronize(engine.compose(e.document)), 1u);
    e.formatParagraph([](auto &p) { p.list = ListKind::Bullet; });
    EXPECT_EQ(renderer.synchronize(engine.compose(e.document)), 1u);
    renderer.drawPage(*surface->getCanvas(), 0);
    int markerInk = 0;
    for (int y = 55; y < 90; ++y)
        for (int x = 56; x < 73; ++x)
            if ((pixels.getColor(x, y) & 0xffffff) < 0x909090)
                ++markerInk;
    EXPECT_GT(markerInk, 1);
}
TEST(Rendering, PaintsShapedGlyphsAndInvalidatesEditedPages) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    PageRenderer renderer;
    Editor e;
    e.replace(u"Native glyphs नमस्ते مرحبا");
    auto layout = engine.compose(e.document);
    EXPECT_EQ(renderer.synchronize(layout), 1u);
    EXPECT_EQ(renderer.synchronize(layout), 0u);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(600, 850));
    ASSERT_TRUE(surface);
    surface->getCanvas()->clear(SK_ColorWHITE);
    renderer.drawPage(*surface->getCanvas(), 0);
    SkPixmap pixels;
    ASSERT_TRUE(surface->peekPixels(&pixels));
    int ink = 0;
    for (int y = 55; y < 90; ++y)
        for (int x = 55; x < 450; ++x)
            if ((pixels.getColor(x, y) & 0xffffff) < 0x909090)
                ++ink;
    EXPECT_GT(ink, 100);
    e.replace(u" edited");
    EXPECT_EQ(renderer.synchronize(engine.compose(e.document)), 1u);
}
TEST(Rendering, TableBordersProducePixelsAndInvalidateAfterStructuralEdit) {
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    PageRenderer renderer;
    Editor e;
    e.insertTable(2, 2);
    auto layout = engine.compose(e.document);
    ASSERT_EQ(layout.cells.size(), 4);
    renderer.synchronize(layout);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(600, 850));
    renderer.drawPage(*surface->getCanvas(), 0);
    SkPixmap pixels;
    ASSERT_TRUE(surface->peekPixels(&pixels));
    const auto &cell = layout.cells.front();
    int border = 0;
    for (int y = static_cast<int>(cell.y) - 1; y <= static_cast<int>(cell.y) + 1; ++y)
        for (int x = static_cast<int>(cell.x) + 10; x < static_cast<int>(cell.x + cell.width) - 10;
             ++x)
            if ((pixels.getColor(x, y) & 0xffffff) != 0xffffff)
                ++border;
    EXPECT_GT(border, 100);
    EXPECT_EQ(renderer.synchronize(layout), 0);
    e.tableColumn(true);
    EXPECT_EQ(renderer.synchronize(engine.compose(e.document)), 1);
}
TEST(Rendering, IncrementalEditsRerecordOnlyChangedPagesAndMatchAFreshRenderer) {
    std::u16string text;
    for (int n = 0; n < 300; ++n)
        text += (n ? u"\n" : u"") + std::u16string(u"Paragraph text that fills a line or so of the page.");
    Editor editor;
    editor.replace(text);
    Shaper shaper(systemFontFiles());
    LayoutEngine engine(shaper);
    PageRenderer renderer;
    auto layout = engine.compose(editor.document);
    renderer.synchronize(layout);
    ASSERT_GT(layout.pageCount, 5u);
    auto pixels = [](const PageRenderer &source, const Layout &from, std::size_t page) {
        const auto &style = from.pageStyle(page);
        auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(
            static_cast<int>(style.width), static_cast<int>(style.height)));
        source.drawPage(*surface->getCanvas(), page);
        SkPixmap map;
        surface->peekPixels(&map);
        const auto *bytes = static_cast<const char *>(map.addr());
        return std::string(bytes, map.computeByteSize());
    };
    auto check = [&](std::size_t expectedMaximum) {
        layout = engine.compose(editor.document, std::move(layout));
        EXPECT_LE(renderer.synchronize(layout), expectedMaximum);
        LayoutEngine freshEngine(shaper);
        const auto fresh = freshEngine.compose(editor.document);
        PageRenderer freshRenderer;
        freshRenderer.synchronize(fresh);
        for (std::size_t page = 0; page < layout.pageCount; ++page)
            EXPECT_EQ(pixels(renderer, layout, page), pixels(freshRenderer, fresh, page))
                << "page " << page;
    };
    editor.selection = {{150, 0}, {150, 0}};
    editor.replace(u"x");
    check(1);
    // A new paragraph near the start moves every later page by a line; pages reflow.
    editor.selection = {{3, 5}, {3, 5}};
    editor.insertParagraphBreak();
    check(layout.pageCount);
    editor.undo();
    check(layout.pageCount);
}
