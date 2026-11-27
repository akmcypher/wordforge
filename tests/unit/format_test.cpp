#include "core/formats/native.hpp"
#include "core/formats/plain_text.hpp"
#include <unicode/ucnv.h>
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
    document.paragraphs.edit(0).cell = document.paragraphs.edit(1).cell = TableCell{tableId, 0, 0};
    document.paragraphs.edit(2).cell = TableCell{tableId, 0, 1};
    document.paragraphs.edit(0).runs = {{u"first नमस्ते", {}}};
    document.paragraphs.edit(1).runs = {{u"second paragraph", {}}};
    document.paragraphs.edit(2).runs = {{u"right", {}}};
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
    document.paragraphs.edit(1).runs.push_back(Run::fromImage({5, 72, 36, "cell picture"}));
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
    legacy.paragraphs.edit(0).id = 9007199254740993ull;
    legacy.paragraphs.edit(1).id = 9007199254741001ull;
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
    document.paragraphs.edit(0).runs = {
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
    document.paragraphs.edit(0).runs = {Run::fromImage({2, 72, 36, "live"})};
    const auto parts = opc::read(serializeNative(document));
    EXPECT_TRUE(parts.contains("images/2.png"));
    EXPECT_FALSE(parts.contains("images/3.png"));
    EXPECT_EQ(parseNative(opc::write(parts)).images.size(), 1);
    document.paragraphs.edit(0).runs = {{u"deleted picture", {}}};
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

TEST(NativeFormat, FirstPageHeadersNeedWfg20Only) {
    Document document;
    document.page.header = {u"Running", u"", u""};
    document.page.differentFirstPage = true;
    EXPECT_EQ(serializeNative(document).find("version=\"20\""), std::string::npos);
    document.page.firstHeader = {u"", u"Title", u""};
    document.page.firstFooter = {u"", u"", u"{PAGE}"};
    const auto bytes = serializeNative(document);
    EXPECT_NE(bytes.find("version=\"20\""), std::string::npos);
    EXPECT_EQ(parseNative(bytes).page, document.page);
    // WFG19 doesn't know first-page bands.
    auto older = bytes;
    older.replace(older.find("version=\"20\""), 12, "version=\"19\"");
    EXPECT_THROW(parseNative(older), std::runtime_error);
    auto twice = bytes;
    const auto at = twice.find("<firstHeader");
    twice.insert(at, twice.substr(at, twice.find("/>", at) + 2 - at));
    EXPECT_THROW(parseNative(twice), std::runtime_error);
}

TEST(NativeFormat, BorderStylesNeedWfg20Only) {
    Editor e;
    e.insertTable(1, 2);
    auto document = e.document;
    validateDocument(document);
    EXPECT_EQ(serializeNative(document).find("version=\"20\""), std::string::npos);
    document.tables[0].border = {1, 0xff000000, BorderStyle::Dotted};
    document.tables[0].cellBorders = {{0, 1, CellSide::Top, {3, 0xff112233, BorderStyle::Double}}};
    const auto bytes = serializeNative(document);
    const auto xml = opc::read(bytes).at("document.xml");
    EXPECT_NE(xml.find("version=\"20\""), std::string::npos);
    EXPECT_NE(xml.find("borderStyle=\"dotted\""), std::string::npos);
    EXPECT_NE(xml.find("style=\"double\""), std::string::npos);
    EXPECT_EQ(parseNative(bytes).tables, document.tables);
}

TEST(NativeFormat, DiagonalCellBordersNeedWfg20Only) {
    Editor e;
    e.insertTable(1, 2);
    auto document = e.document;
    document.tables[0].cellBorders = {{0, 1, CellSide::DiagonalUp, {1, 0xff000000}}};
    validateDocument(document);
    const auto bytes = serializeNative(document);
    const auto xml = opc::read(bytes).at("document.xml");
    EXPECT_NE(xml.find("version=\"20\""), std::string::npos);
    EXPECT_NE(xml.find("side=\"diagonalUp\""), std::string::npos);
    EXPECT_EQ(parseNative(bytes).tables, document.tables);
}

TEST(NativeFormat, HeadersAndFootersRoundTripInV8Only) {
    Document document;
    EXPECT_EQ(serializeNative(document).find("version=\"8\""), std::string::npos);
    document.page.header = {u"Left & <left>", u"", u"Draft"};
    document.page.footer = {u"", u"Page {PAGE} of {NUMPAGES}", u""};
    document.page.differentFirstPage = true;
    const auto bytes = serializeNative(document);
    EXPECT_NE(bytes.find("version=\"8\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.page, document.page);
    // Only the first-page option still needs WFG8.
    Document firstOnly;
    firstOnly.page.differentFirstPage = true;
    EXPECT_TRUE(parseNative(serializeNative(firstOnly)).page.differentFirstPage);

    // Malformed variants of a valid file.
    Document simple;
    simple.page.footer.center = u"{PAGE}";
    const auto valid = serializeNative(simple);
    const auto start = valid.find("<footer");
    ASSERT_NE(start, std::string::npos);
    const auto footer = valid.substr(start, valid.find("/>", start) + 2 - start);
    auto variant = [&](const std::string &from, const std::string &to) {
        auto text = valid;
        text.replace(text.find(from), from.size(), to);
        return text;
    };
    EXPECT_NO_THROW(parseNative(valid));
    EXPECT_THROW(parseNative(variant("version=\"8\"", "version=\"7\"")), std::exception);
    for (const auto *bad :
         {"<footer middle='x'/>", "<footer/>", "<footer left='a'/><footer left='b'/>",
          "<footer left='a&#10;b'/>", "<footer left='a'><x/></footer>",
          "<footer left='a' differentFirstPage='true'/>"})
        EXPECT_THROW(parseNative(variant(footer, bad)), std::exception) << bad;
    EXPECT_THROW(parseNative(variant("version=\"8\"", "version=\"8\" differentFirstPage=\"x\"")),
                 std::exception);
}

TEST(NativeFormat, TableWidthsAndMergesRoundTripInV8) {
    Editor e;
    e.insertTable(2, 3);
    std::size_t last = 0;
    for (std::size_t i = 0; i < e.document.paragraphs.size(); ++i)
        if (e.document.paragraphs[i].cell == TableCell{e.document.tables[0].id, 1, 1})
            last = i;
    e.selection = {{0, 0}, {last, 0}};
    e.mergeCells();
    e.setColumnWidth(120.5);
    const auto bytes = serializeNative(e.document);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.tables, e.document.tables);
    // Hand-edited invalid metadata is rejected.
    auto variant = [&](const std::string &from, const std::string &to) {
        auto parts = opc::read(bytes);
        auto &xml = parts.at("document.xml");
        const auto at = xml.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        xml.replace(at, from.size(), to);
        return opc::write(parts);
    };
    EXPECT_THROW(parseNative(variant("rows=\"2\" columns=\"2\"", "rows=\"3\" columns=\"2\"")),
                 std::exception);
    EXPECT_THROW(parseNative(variant("<merge ", "<merge extra=\"1\" ")), std::exception);
    EXPECT_THROW(parseNative(variant("width=\"120.5\"", "width=\"-1\"")), std::exception);
    EXPECT_THROW(parseNative(variant("version=\"8\"", "version=\"7\"")), std::exception);
}

TEST(NativeFormat, HeaderDocumentsWithDirectFormattingReopen) {
    // WFG8 must store direct-formatting masks even without custom styles.
    Editor e;
    e.replace(u"plain bold");
    e.selection = {{0, 6}, {0, 10}};
    e.format([](CharacterStyle &style) { style.bold = true; });
    auto page = e.document.page;
    page.footer.center = u"{PAGE}";
    e.setPage(page);
    const auto restored = parseNative(serializeNative(e.document));
    ASSERT_EQ(restored.paragraphs.size(), 1u);
    const auto &runs = restored.paragraphs[0].runs;
    EXPECT_EQ(restored.paragraphs[0].text(), u"plain bold");
    ASSERT_EQ(runs.size(), e.document.paragraphs[0].runs.size());
    for (std::size_t i = 0; i < runs.size(); ++i) {
        EXPECT_EQ(runs[i].style, e.document.paragraphs[0].runs[i].style);
        EXPECT_EQ(runs[i].direct, e.document.paragraphs[0].runs[i].direct);
    }
    EXPECT_TRUE(runs.back().style.bold);
    EXPECT_EQ(restored.page, e.document.page);
}

TEST(NativeFormat, KeptDocxPartsRoundTripInV9) {
    Document document;
    EXPECT_EQ(serializeNative(document).find("version=\"9\""), std::string::npos);
    auto kept = std::make_shared<PreservedPackage>();
    kept->parts = {{"docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml",
                    "<cp:coreProperties/>"},
                   {"customXml/item1.xml", "application/xml", std::string("\0binary\xff", 8)},
                   {"customXml/_rels/item1.xml.rels", "", "<Relationships/>"}};
    kept->relationships = {{true, "http://example/core-properties", "docProps/core.xml"},
                           {false, "http://example/customXml", "customXml/item1.xml"}};
    document.preserved = kept;
    const auto bytes = serializeNative(document);
    const auto restored = parseNative(bytes);
    ASSERT_TRUE(restored.preserved);
    EXPECT_EQ(*restored.preserved, *kept);
    auto variant = [&](const std::string &from, const std::string &to) {
        auto parts = opc::read(bytes);
        auto &xml = parts.at("document.xml");
        const auto at = xml.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        xml.replace(at, from.size(), to);
        return opc::write(parts);
    };
    EXPECT_THROW(parseNative(variant("version=\"9\"", "version=\"8\"")), std::exception);
    EXPECT_THROW(parseNative(variant("path=\"docProps/core.xml\"", "path=\"../core.xml\"")),
                 std::exception);
    EXPECT_THROW(parseNative(variant("path=\"docProps/core.xml\"", "path=\"word/document.xml\"")),
                 std::exception);
    EXPECT_THROW(parseNative(variant("target=\"customXml/item1.xml\"", "target=\"missing.xml\"")),
                 std::exception);
    EXPECT_THROW(parseNative(variant("part=\"preserved/0\"", "part=\"preserved/1\"")),
                 std::exception);
}

TEST(NativeFormat, FootnotesRoundTripInV9) {
    Editor e;
    e.replace(u"text");
    const auto id = e.insertFootnote(u"A note with <markup> & \"quotes\"");
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(bytes.find("version=\"9\""), std::string::npos);
    const auto restored = parseNative(bytes);
    ASSERT_EQ(restored.footnotes, e.document.footnotes);
    ASSERT_NE(restored.paragraphs[0].runs.back().note(), nullptr);
    EXPECT_EQ(restored.paragraphs[0].runs.back().note()->id, id);
    auto variant = [&](const std::string &from, const std::string &to) {
        auto text = bytes;
        const auto at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        return text;
    };
    EXPECT_THROW(parseNative(variant("version=\"9\"", "version=\"8\"")), std::exception);
    EXPECT_THROW(parseNative(variant("<footnote id=", "<footnote extra=\"1\" id=")),
                 std::exception);
    // A reference to a missing footnote.
    EXPECT_THROW(parseNative(variant("<note id=\"" + std::to_string(id),
                                     "<note id=\"" + std::to_string(id + 50))),
                 std::exception);
}

TEST(NativeFormat, CommentsRoundTripInV9) {
    Editor e;
    e.replace(u"first\nsecond line");
    e.selection = {{0, 2}, {1, 6}};
    e.addComment(u"Spans two paragraphs & <more>", "Ana Lima", "2026-11-16T10:00:00Z");
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(bytes.find("version=\"9\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.comments, e.document.comments);
    auto variant = [&](const std::string &from, const std::string &to) {
        auto text = bytes;
        const auto at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        return text;
    };
    EXPECT_THROW(parseNative(variant("startOffset=\"2\"", "startOffset=\"99\"")), std::exception);
    EXPECT_THROW(parseNative(variant("<comment ", "<comment color=\"red\" ")), std::exception);
}

TEST(NativeFormat, TrackedChangesRoundTripInV10) {
    Editor e;
    e.replace(u"keep old text");
    e.reviewer = "Ana Lima";
    e.reviewDate = "2026-11-16T11:00:00Z";
    e.setTrackChanges(true);
    e.selection = {{0, 5}, {0, 8}};
    e.replace(u"new");
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(bytes.find("version=\"10\""), std::string::npos);
    EXPECT_NE(bytes.find("trackChanges=\"true\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_TRUE(restored.trackChanges);
    EXPECT_EQ(restored.revisions, e.document.revisions);
    EXPECT_EQ(restored.paragraphs[0].runs, e.document.paragraphs[0].runs);
    auto variant = [&](const std::string &from, const std::string &to) {
        auto text = bytes;
        const auto at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        return text;
    };
    EXPECT_THROW(parseNative(variant("<revision ", "<revision color=\"red\" ")), std::exception);
    EXPECT_THROW(parseNative(variant("version=\"10\"", "version=\"9\"")), std::exception);
    // Tracking switched on with no changes yet still needs WFG10; plain files stay older.
    Editor plain;
    plain.replace(u"x");
    EXPECT_EQ(serializeNative(plain.document).find("version=\"10\""), std::string::npos);
    plain.setTrackChanges(true);
    EXPECT_TRUE(parseNative(serializeNative(plain.document)).trackChanges);
}

TEST(NativeFormat, TableBordersAndShadingRoundTripInV11) {
    Editor e;
    e.insertTable(2, 2);
    // Tables travel in the ZIP transport; the XML is document.xml.
    auto xml = [](const std::string &bytes) { return opc::read(bytes).at("document.xml"); };
    EXPECT_EQ(xml(serializeNative(e.document)).find("version=\"11\""), std::string::npos);
    e.setTableBorder({0, 0xff000000});
    e.shadeCells(0xffaabbcc);
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(xml(bytes).find("version=\"11\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.tables, e.document.tables);
    auto variant = [&](const std::string &from, const std::string &to) {
        auto parts = opc::read(bytes);
        auto &text = parts.at("document.xml");
        const auto at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        return opc::write(parts);
    };
    EXPECT_THROW(parseNative(variant("<shade ", "<shade tint=\"1\" ")), std::exception);
    EXPECT_THROW(parseNative(variant("version=\"11\"", "version=\"10\"")), std::exception);
}

TEST(NativeFormat, IndentsAndJustificationRoundTripInV11) {
    Editor e;
    e.replace(u"text");
    auto xml = [](const std::string &bytes) {
        return bytes.starts_with("PK") ? opc::read(bytes).at("document.xml") : bytes;
    };
    EXPECT_EQ(xml(serializeNative(e.document)).find("version=\"11\""), std::string::npos);
    e.formatParagraph([](auto &p) {
        p.indentLeft = 36;
        p.indentFirstLine = -18;
        p.indentRight = 12;
        p.alignment = Alignment::Justify;
    });
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(xml(bytes).find("version=\"11\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(static_cast<const ParagraphFormat &>(restored.paragraphs[0]),
              static_cast<const ParagraphFormat &>(e.document.paragraphs[0]));
    // Earlier versions do not know these attributes.
    ASSERT_FALSE(bytes.starts_with("PK")); // plain text travels as XML
    auto older = bytes;
    older.replace(older.find("version=\"11\""), 12, "version=\"10\"");
    EXPECT_THROW(parseNative(older), std::exception);
}

TEST(NativeFormat, ThemeColoursRoundTripInV12) {
    Editor e;
    e.replace(u"text");
    auto xml = [](const std::string &bytes) {
        return bytes.starts_with("PK") ? opc::read(bytes).at("document.xml") : bytes;
    };
    EXPECT_EQ(xml(serializeNative(e.document)).find("version=\"12\""), std::string::npos);
    auto &run = e.document.paragraphs.edit(0).runs.front();
    run.style.color = 0xff0f4761;
    run.style.theme = {themeColorIndex("accent1"), 0xbf, -1, 0xff0f4761};
    run.direct |= ColorField;
    auto *heading = e.document.styles.find("Heading1");
    heading->character.color = 0xffe97132;
    heading->character.theme = ThemeColor{themeColorIndex("accent2"), -1, 0x99, 0xffe97132};
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(xml(bytes).find("version=\"12\""), std::string::npos);
    EXPECT_NE(xml(bytes).find("theme=\"accent1 s:BF\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.paragraphs[0].runs.front().style, run.style);
    EXPECT_EQ(*restored.styles.find("Heading1"), *heading);
    // Earlier versions do not know the attribute, and malformed values are refused.
    auto older = bytes;
    older.replace(older.find("version=\"12\""), 12, "version=\"11\"");
    EXPECT_THROW(parseNative(older), std::exception);
    auto broken = bytes;
    broken.replace(broken.find("accent1 s:BF"), 12, "accent9 s:BF");
    EXPECT_THROW(parseNative(broken), std::exception);
    // A lapsed reference (colour changed since) is not written.
    run.style.color = 0xff112233;
    heading->character.theme.reset();
    EXPECT_EQ(xml(serializeNative(e.document)).find("theme="), std::string::npos);
}

TEST(NativeFormat, LinksRoundTripInV12) {
    Editor e;
    e.replace(u"a link here");
    e.selection = {{0, 2}, {0, 6}};
    e.setLink("https://example.com/?q=1&r=2");
    const auto bytes = serializeNative(e.document);
    const auto xml = bytes.starts_with("PK") ? opc::read(bytes).at("document.xml") : bytes;
    EXPECT_NE(xml.find("version=\"12\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.paragraphs[0].runs, e.document.paragraphs[0].runs);
    auto unsafe = bytes;
    unsafe.replace(unsafe.find("https://example.com/?q=1&amp;r=2"), 32, "javascript:alert(1)");
    EXPECT_THROW(parseNative(unsafe), std::exception);
}

TEST(NativeFormat, EndnotesRoundTripInV12) {
    Editor e;
    e.replace(u"text");
    e.insertFootnote(u"foot");
    e.insertFootnote(u"end", true);
    const auto bytes = serializeNative(e.document);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.paragraphs[0].runs, e.document.paragraphs[0].runs);
    EXPECT_EQ(restored.footnotes, e.document.footnotes);
}

TEST(NativeFormat, FieldsRoundTripInV12) {
    Editor e;
    e.replace(u"Heading");
    e.applyStyle("Heading1");
    e.insertParagraphBreak();
    e.selection = {{0, 0}, {0, 0}};
    e.insertTableOfContents([](std::size_t) { return 3; });
    const auto bytes = serializeNative(e.document);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.fields, e.document.fields);
    auto broken = bytes;
    const auto at = broken.find("instruction=\"");
    ASSERT_NE(at, std::string::npos);
    broken.insert(at + 13, "&#9;");
    EXPECT_THROW(parseNative(broken), std::exception);
}

TEST(NativeFormat, SectionsRoundTripInV13) {
    Editor e;
    e.replace(u"one two");
    e.selection = {{0, 3}, {0, 3}};
    e.insertSectionBreak(true);
    e.selection = {{0, 0}, {0, 0}};
    auto page = e.sectionPage();
    page.columns = 2;
    e.setPage(page);
    const auto bytes = serializeNative(e.document);
    const auto xml = bytes.starts_with("PK") ? opc::read(bytes).at("document.xml") : bytes;
    EXPECT_NE(xml.find("version=\"13\""), std::string::npos);
    const auto restored = parseNative(bytes);
    EXPECT_EQ(restored.paragraphs[0].sectionBreak, e.document.paragraphs[0].sectionBreak);
    EXPECT_EQ(restored.page, e.document.page);
    auto older = bytes;
    older.replace(older.find("version=\"13\""), 12, "version=\"12\"");
    EXPECT_THROW(parseNative(older), std::exception);
}

namespace {
std::string encode(const std::u16string &text, const char *encoding) {
    UErrorCode error = U_ZERO_ERROR;
    auto *converter = ucnv_open(encoding, &error);
    std::string out(text.size() * 4 + 8, '\0');
    const auto length = ucnv_fromUChars(converter, out.data(), static_cast<int32_t>(out.size()),
                                        reinterpret_cast<const UChar *>(text.data()),
                                        static_cast<int32_t>(text.size()), &error);
    ucnv_close(converter);
    EXPECT_TRUE(U_SUCCESS(error)) << encoding;
    out.resize(static_cast<std::size_t>(length));
    return out;
}
} // namespace

TEST(PlainText, DecodesTheWorldsCommonEncodings) {
    // Marked and unmarked Unicode, with Windows and old Mac line endings.
    const std::u16string mixed = u"English, Ελληνικά, Русский, العربية, עברית, हिन्दी, ไทย, "
                                 u"中文, 日本語, 한국어 🙂";
    for (const char *encoding : {"UTF-8", "UTF-16LE", "UTF-16BE"}) {
        const auto plain = encode(mixed + u"\r\nsecond\rthird", encoding);
        const auto decoded = decodePlainText(plain);
        EXPECT_EQ(decoded.text, mixed + u"\nsecond\nthird") << encoding;
        EXPECT_EQ(decoded.encoding, encoding);
    }
    EXPECT_EQ(decodePlainText("\xef\xbb\xbf" "caf\xc3\xa9").text, u"café");
    EXPECT_EQ(decodePlainText("\xff\xfe" + encode(u"Ω", "UTF-16LE")).encoding, "UTF-16LE");
    EXPECT_EQ(decodePlainText(std::string("\xff\xfe\0\0", 4) + encode(u"Ω", "UTF-32LE")).text, u"Ω");
    // Legacy code pages, detected from the bytes.
    struct Sample {
        const char *encoding;
        std::u16string text;
    };
    const std::vector<Sample> samples{
        {"Shift_JIS", u"日本語の文章です。これは文字コードの判定を確かめるための長めの文章で、"
                      u"ひらがなとカタカナと漢字を含んでいます。東京都の天気は晴れです。"},
        {"GB18030", u"这是一段用于检测字符编码的中文文本。我们希望软件能够正确地识别简体中文，"
                    u"并且显示所有的汉字。北京的天气很好，大家都很高兴。"},
        {"Big5", u"這是一段用於檢測字元編碼的繁體中文文字。我們希望軟體能夠正確地識別繁體中文，"
                 u"並且顯示所有的漢字。臺北的天氣很好，大家都很高興。"},
        {"EUC-KR", u"이것은 문자 인코딩을 확인하기 위한 한국어 문장입니다. 프로그램이 한글을 "
                   u"올바르게 인식하고 표시하기를 바랍니다. 서울의 날씨는 맑습니다."},
        {"windows-1251", u"Это русский текст для проверки определения кодировки. Программа "
                         u"должна правильно распознать кириллицу и показать все буквы. "
                         u"Сегодня в Москве хорошая погода, и все люди очень довольны."},
        // Short Big5 text, once taken for Thai.
        {"Big5", u"今天臺北的天氣非常好，陽光明媚。\n感謝您的快速回覆和幫助。\n學生們正在圖書館裡安靜地學習。"},
        {"windows-874", u"วันนี้อากาศที่กรุงเทพดีมากและมีแดดจัด ขอบคุณสำหรับคำตอบที่รวดเร็วและความช่วยเหลือ "
                        u"นักเรียนกำลังอ่านหนังสืออย่างเงียบ ๆ ในห้องสมุด"},
        {"windows-1252", u"Le café était très agréable, et la crème brûlée était délicieuse. "
                         u"Où êtes-vous allés pendant les vacances d'été ? À bientôt !"},
    };
    for (const auto &sample : samples) {
        const auto decoded = decodePlainText(encode(sample.text, sample.encoding));
        EXPECT_EQ(decoded.text, sample.text) << sample.encoding << " detected as " << decoded.encoding;
    }
    // Control characters other than tabs and line breaks are dropped.
    EXPECT_EQ(decodePlainText(std::string("a\0b\tc\fd", 7)).text, u"ab\tc\nd");
}
