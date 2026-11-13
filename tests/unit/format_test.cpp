#include "core/formats/native.hpp"
#include "core/formats/zip_package.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
using namespace wf;
TEST(NativeFormat, RoundTripsTableCellsImagesAndExactIdentifiersInV5) {
    Document document;
    const auto tableId = 9007199254740993ull;
    document.tables = {{tableId, 1, 2}};
    document.paragraphs = {Paragraph{1}, Paragraph{2}, Paragraph{3}, Paragraph{4}};
    document.paragraphs[0].cell = document.paragraphs[1].cell = TableCell{tableId, 0, 0};
    document.paragraphs[2].cell = TableCell{tableId, 0, 1};
    document.paragraphs[0].runs = {{u"first नमस्ते", {}}};
    document.paragraphs[1].runs = {{u"second paragraph", {}}};
    document.paragraphs[2].runs = {{u"right", {}}};
    document.nextId = tableId + 1;
    auto bytes = serializeNative(document);
    ASSERT_TRUE(bytes.starts_with("PK"));
    EXPECT_NE(opc::read(bytes).at("document.xml").find("version=\"5\""), std::string::npos);
    auto restored = parseNative(bytes);
    EXPECT_EQ(restored.tables, document.tables);
    EXPECT_EQ(restored.nextId, document.nextId);
    EXPECT_EQ(restored.paragraphs, document.paragraphs);
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    document.images.emplace(5, std::make_shared<const ImageResource>(ImageResource{4, 2, png}));
    document.paragraphs[1].runs.push_back(Run::fromImage({5, 72, 36, "cell picture"}));
    document.bookmarks.push_back({6, "cell", {2, 0, Affinity::After}});
    restored = parseNative(serializeNative(document));
    EXPECT_EQ(restored.paragraphs, document.paragraphs);
    EXPECT_EQ(restored.tables, document.tables);
    EXPECT_EQ(restored.bookmarks, document.bookmarks);
    EXPECT_EQ(*restored.images.at(5), *document.images.at(5));
}
TEST(NativeFormat, RejectsMalformedOrIncompleteV5TableMembership) {
    const auto package = [](const std::string &content) {
        return opc::write(
            {{"document.xml", "<wordforge version='5' nextId='99'>" + content + "</wordforge>"}});
    };
    const std::string table = "<table id='10' rows='1' columns='2'/>";
    const std::string cells = "<p id='1' table='10' row='0' column='0'/><p id='2' table='10' "
                              "row='0' column='1'/><p id='3'/>";
    EXPECT_NO_THROW(parseNative(package(table + cells)));
    for (const auto *metadata :
         {"id='10' rows='1.0' columns='2'", "id='10' rows='+1' columns='2'",
          "id='10' rows='0' columns='2'", "id='10' rows='1001' columns='2'",
          "id='10' rows='1' columns='11'", "id='10' rows='1' columns='2' future='x'",
          "id='10' rows='1'", "id='1' rows='1' columns='2'"})
        EXPECT_THROW(parseNative(package(std::string("<table ") + metadata + "/>" + cells)),
                     std::exception)
            << metadata;
    for (const auto *content :
         {"<p id='1' table='10' row='0'/><p id='2'/>", "<p id='1' row='0' column='0'/><p id='2'/>",
          "<p id='1' table='10' row='0.0' column='0'/><p id='2'/>",
          "<p id='1' table='10' row='18446744073709551616' column='0'/><p id='2'/>",
          "<p id='1' table='11' row='0' column='0'/><p id='2'/>",
          "<p id='1' table='10' row='0' column='1'/><p id='2' table='10' row='0' column='0'/><p "
          "id='3'/>",
          "<p id='1' table='10' row='0' column='0'/><p id='2'/><p id='3' table='10' row='0' "
          "column='1'/><p id='4'/>",
          "<p id='1' table='10' row='0' column='0'/><p id='2' table='10' row='0' column='1'/>",
          "<p id='1'/>", "<table id='10' rows='1' columns='2'/><p id='1'/>"})
        EXPECT_THROW(parseNative(package(table + content)), std::exception) << content;
    EXPECT_THROW(parseNative(package(cells)), std::exception);
    EXPECT_THROW(parseNative(package(table + "<table id='10' rows='1' columns='2'/>" + cells)),
                 std::exception);
    EXPECT_THROW(
        parseNative(package("<table id='10' rows='1' columns='2'><future/></table>" + cells)),
        std::exception);
    EXPECT_THROW(
        parseNative("<wordforge version='5' nextId='99'>" + table + cells + "</wordforge>"),
        std::exception);
    EXPECT_THROW(parseNative(opc::write({{"document.xml", "<wordforge version='4' nextId='99'>" +
                                                              table + cells + "</wordforge>"}})),
                 std::exception);
}
TEST(NativeFormat, PersistsBookmarksAndRejectsDanglingAnchors) {
    Editor editor;
    editor.replace(u"first\nsecond");
    editor.selection = {{1, 2}, {1, 2}};
    const auto id = editor.addBookmark("chapter");
    const auto restored = parseNative(serializeNative(editor.document));
    ASSERT_EQ(restored.bookmarks.size(), 1);
    EXPECT_EQ(restored.bookmarks[0], editor.document.bookmarks[0]);
    EXPECT_EQ(restored.bookmarks[0].id, id);
    EXPECT_EQ(restored.resolve(restored.bookmarks[0].anchor), (Position{1, 2}));
    for (const auto *attributes :
         {"id='1' name='duplicate' paragraph='1' offset='0' affinity='after'",
          "id='2' name='' paragraph='1' offset='0' affinity='after'",
          "id='2' name='bad' paragraph='1' offset='99' affinity='after'",
          "id='2' name='bad' paragraph='1' offset='0' affinity='sideways'"}) {
        const auto invalid =
            std::string("<wordforge version='3' nextId='3'><p id='1'/><bookmark ") + attributes +
            "/></wordforge>";
        EXPECT_THROW(parseNative(invalid), std::exception);
    }
    EXPECT_THROW(parseNative("<wordforge version='3' nextId='3'><p id='1'/><bookmark id='2' "
                             "name='bad' paragraph='99' offset='0' affinity='after'/></wordforge>"),
                 std::exception);
}
TEST(NativeFormat, PreservesParagraphFeaturesAndTextDecorations) {
    Editor e;
    e.replace(u"marked\nnext");
    e.selection = {{0, 0}, {0, 6}};
    e.applyPreset(ParagraphPreset::Heading2);
    e.format([](auto &s) {
        s.strike = true;
        s.highlight = 0xffffff00;
    });
    e.formatParagraph([](auto &p) {
        p.list = ListKind::Decimal;
        p.indentLeft = 18;
        p.spaceBefore = 12;
        p.lineSpacing = 2;
        p.pageBreakBefore = true;
    });
    auto restored = parseNative(serializeNative(e.document));
    const auto &p = restored.paragraphs[0];
    EXPECT_EQ(p.list, ListKind::Decimal);
    EXPECT_EQ(p.indentLeft, 18);
    EXPECT_EQ(p.spaceBefore, 12);
    EXPECT_EQ(p.lineSpacing, 2);
    EXPECT_TRUE(p.pageBreakBefore);
    EXPECT_EQ(p.style, "Heading2");
    EXPECT_TRUE(p.runs[0].style.strike);
    EXPECT_EQ(p.runs[0].style.highlight, 0xffffff00);
}
TEST(NativeFormat, MigratesV1AndPersistsExactSparseV2Identifiers) {
    const auto fixture =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/legacy-v1.wfg";
    std::ifstream file(fixture, std::ios::binary);
    ASSERT_TRUE(file.good());
    auto legacy = parseNative(std::string((std::istreambuf_iterator<char>(file)), {}));
    EXPECT_EQ(legacy.text(), u"old नमस्ते\ntwo");
    EXPECT_EQ(legacy.page.margin, 36);
    EXPECT_EQ(legacy.paragraphs[0].alignment, Alignment::Center);
    EXPECT_TRUE(legacy.paragraphs[0].runs[0].style.bold);
    EXPECT_EQ(legacy.paragraphs[1].list, ListKind::Decimal);
    EXPECT_EQ(legacy.paragraphs[0].id, 1);
    EXPECT_EQ(legacy.paragraphs[1].id, 2);
    legacy.paragraphs[0].id = 9007199254740993ull;
    legacy.paragraphs[1].id = 9007199254741001ull;
    legacy.nextId = 9007199254741010ull;
    const auto bytes = serializeNative(legacy);
    EXPECT_NE(bytes.find("version=\"2\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.text(), legacy.text());
    EXPECT_EQ(restored.paragraphs[0].id, legacy.paragraphs[0].id);
    EXPECT_EQ(restored.paragraphs[1].id, legacy.paragraphs[1].id);
    EXPECT_EQ(restored.nextId, legacy.nextId);
    EXPECT_THROW(
        parseNative("<wordforge version='2' nextId='3'><p id='1'/><p id='1'/></wordforge>"),
        std::exception);
    EXPECT_THROW(parseNative("<wordforge version='2' nextId='2'><p id='2'/></wordforge>"),
                 std::exception);
    EXPECT_THROW(parseNative("<wordforge version='2' nextId='3'><p id='+1'/></wordforge>"),
                 std::exception);
    EXPECT_THROW(
        parseNative("<wordforge version='2' nextId='18446744073709551616'><p id='1'/></wordforge>"),
        std::exception);
}
TEST(NativeFormat, RoundTripsUnicodeRunsAndPageGeometry) {
    Editor e;
    e.replace(u"First\nनमस्ते & <text>");
    e.selection = {{1, 0}, {1, 6}};
    e.format([](auto &s) {
        s.bold = true;
        s.size = 22;
    });
    e.align(Alignment::Center);
    e.document.page = {612, 792, 36};
    const auto d = parseNative(serializeNative(e.document));
    EXPECT_EQ(d.text(), u"First\nनमस्ते & <text>");
    ASSERT_EQ(d.paragraphs.size(), 2u);
    EXPECT_EQ(d.paragraphs[1].alignment, Alignment::Center);
    EXPECT_TRUE(d.paragraphs[1].runs.front().style.bold);
    EXPECT_EQ(d.page.width, 612);
    EXPECT_EQ(d.page.margin, 36);
}
TEST(NativeFormat, RejectsMalformedAndUnsafeGeometry) {
    EXPECT_THROW(parseNative("broken"), std::runtime_error);
    EXPECT_THROW(parseNative("<wordforge version='99'/>"), std::runtime_error);
    EXPECT_THROW(parseNative("<wordforge version='1' width='100' height='100' "
                             "margin='90'><p><r>x</r></p></wordforge>"),
                 std::runtime_error);
    EXPECT_THROW(parseNative("<!DOCTYPE x [<!ENTITY e SYSTEM 'file:///etc/passwd'>]><wordforge "
                             "version='1'><p><r>&e;</r></p></wordforge>"),
                 std::runtime_error);
}
TEST(NativeFormat, PreservesWhitespaceAndAllTextNodes) {
    Editor e;
    e.replace(u"   \n\t  ");
    EXPECT_EQ(parseNative(serializeNative(e.document)).text(), u"   \n\t  ");
    auto parsed = parseNative(
        "<wordforge version='1'><p><r>before<![CDATA[ & middle]]>after</r></p></wordforge>");
    EXPECT_EQ(parsed.text(), u"before & middleafter");
    EXPECT_THROW(
        parseNative("<wordforge version='1'><p><r>first</r></p></wordforge><other>lost</other>"),
        std::runtime_error);
}
TEST(NativeFormat, RoundTripsTypedImagesWithStylesAndBookmarks) {
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    Document document;
    document.images.emplace(2, std::make_shared<const ImageResource>(ImageResource{4, 2, png}));
    CharacterStyle style;
    style.bold = true;
    document.paragraphs[0].runs = {
        {u"before", {}}, Run::fromImage({2, 72, 36, "red & blue"}, style), {u"after", {}}};
    document.bookmarks.push_back({3, "picture", {1, 6, Affinity::Before}});
    document.nextId = 4;
    adoptStyles(document); // hand-built formatting is direct formatting
    const auto bytes = serializeNative(document);
    ASSERT_EQ(bytes.substr(0, 2), "PK");
    const auto parts = opc::read(bytes);
    EXPECT_EQ(parts.at("images/2.png"), png);
    const auto restored = parseNative(bytes);
    ASSERT_EQ(restored.images.size(), 1);
    EXPECT_EQ(*restored.images.at(2), *document.images.at(2));
    EXPECT_EQ(restored.paragraphs[0].runs, document.paragraphs[0].runs);
    EXPECT_EQ(restored.text(), u"before\ufffcafter");
    EXPECT_EQ(restored.bookmarks, document.bookmarks);
}
TEST(NativeFormat, RejectsInvalidImagePackagesWithoutLosingUnknownParts) {
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    const std::string prefix = "<wordforge version='4' nextId='3'><imageResource id='2' "
                               "path='images/2.png' pixelWidth='4' pixelHeight='2'/>";
    const std::string suffix = "</wordforge>";
    opc::Parts good{
        {"document.xml",
         prefix + "<p id='1'><image resource='2' width='72' height='36'/></p>" + suffix},
        {"images/2.png", png}};
    EXPECT_NO_THROW(parseNative(opc::write(good)));
    for (const auto *image : {"<image resource='99' width='72' height='36'/>",
                              "<image resource='2' width='0' height='36'/>",
                              "<image resource='2' width='72' height='nan'/>"}) {
        auto parts = good;
        parts["document.xml"] = prefix + "<p id='1'>" + image + "</p>" + suffix;
        EXPECT_THROW(parseNative(opc::write(parts)), std::exception);
    }
    auto extra = good;
    extra["future.xml"] = "preserve me";
    EXPECT_THROW(parseNative(opc::write(extra)), std::exception);
    auto missing = good;
    missing.erase("images/2.png");
    EXPECT_THROW(parseNative(opc::write(missing)), std::exception);
    auto duplicate = good;
    duplicate["document.xml"] =
        prefix +
        "<imageResource id='2' path='images/2.png' pixelWidth='4' pixelHeight='2'/><p id='1'/>" +
        suffix;
    EXPECT_THROW(parseNative(opc::write(duplicate)), std::exception);
    auto invalid = good;
    invalid["images/2.png"] = "not a PNG";
    EXPECT_THROW(parseNative(opc::write(invalid)), std::exception);
    EXPECT_THROW(parseNative(good.at("document.xml")), std::exception);
    for (const auto *metadata :
         {"id='2' path='other.png' pixelWidth='4' pixelHeight='2'",
          "id='2' path='../images/2.png' pixelWidth='4' pixelHeight='2'",
          "id='2' path='images/2.png' pixelWidth='5' pixelHeight='2'",
          "id='2' path='images/2.png' pixelWidth='4294967296' pixelHeight='2'",
          "id='1' path='images/1.png' pixelWidth='4' pixelHeight='2'"}) {
        auto parts = good;
        parts["document.xml"] = std::string("<wordforge version='4' nextId='3'><imageResource ") +
                                metadata + "/><p id='1'/></wordforge>";
        if (std::string_view(metadata).starts_with("id='1'"))
            parts["images/1.png"] = png;
        EXPECT_THROW(parseNative(opc::write(parts)), std::exception);
    }
}
TEST(NativeFormat, OmitsUnreferencedImageResourcesFromSavedDocuments) {
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    Document document;
    auto resource = std::make_shared<const ImageResource>(ImageResource{4, 2, png});
    document.images = {{2, resource}, {3, resource}};
    document.nextId = 4;
    document.paragraphs[0].runs = {Run::fromImage({2, 72, 36, "live"})};
    const auto parts = opc::read(serializeNative(document));
    EXPECT_TRUE(parts.contains("images/2.png"));
    EXPECT_FALSE(parts.contains("images/3.png"));
    EXPECT_EQ(parseNative(opc::write(parts)).images.size(), 1);
    document.paragraphs[0].runs = {{u"deleted picture", {}}};
    const auto saved = serializeNative(document);
    EXPECT_FALSE(saved.starts_with("PK"));
    const auto restored = parseNative(saved);
    EXPECT_TRUE(restored.images.empty());
    EXPECT_EQ(restored.text(), u"deleted picture");
    document.bookmarks.push_back({4, "kept", {1, 0, Affinity::After}});
    document.nextId = 5;
    const auto bookmarked = serializeNative(document);
    EXPECT_FALSE(bookmarked.starts_with("PK"));
    EXPECT_EQ(parseNative(bookmarked).bookmarks, document.bookmarks);
}
