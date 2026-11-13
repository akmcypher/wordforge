#include "core/rendering/page_renderer.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <include/core/SkSurface.h>
using namespace wf;
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
