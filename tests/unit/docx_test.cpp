#include "core/formats/docx.hpp"
#include "core/formats/zip_package.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <sstream>
#include <pugixml.hpp>
using namespace wf;
TEST(BaselineText, DocxPreservesNominalSizeAndExplicitBaselineAgainstChangedParent) {
    Editor e;
    e.document.styles.find("Normal")->character.baseline = TextBaseline::Superscript;
    e.applyStyle("Normal");
    e.replace(u"a2");
    e.selection = {{0, 1}, {0, 2}};
    e.format([](auto &s) { s.baseline = TextBaseline::Normal; });
    auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document body, styles;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    auto first = body.select_node("//w:body/w:p/w:r[1]/w:rPr").node();
    EXPECT_FALSE(first.child("w:vertAlign"));
    auto second = body.select_node("//w:body/w:p/w:r[2]/w:rPr").node();
    EXPECT_STREQ(second.child("w:vertAlign").attribute("w:val").value(), "baseline");
    EXPECT_STREQ(second.child("w:sz").attribute("w:val").value(), "24");
    auto imported = parseDocx(opc::write(parts)).document;
    EXPECT_EQ(imported.paragraphs[0].runs[0].style.baseline, TextBaseline::Superscript);
    EXPECT_EQ(imported.paragraphs[0].runs[1].style.baseline, TextBaseline::Normal);
    ASSERT_TRUE(styles.load_string(parts.at("word/styles.xml").c_str()));
    auto normal = styles.select_node("//w:style[@w:styleId='Normal']/w:rPr/w:vertAlign").node();
    ASSERT_TRUE(normal);
    normal.attribute("w:val").set_value("baseline");
    std::ostringstream xml; styles.save(xml); parts["word/styles.xml"] = xml.str();
    imported = parseDocx(opc::write(parts)).document;
    EXPECT_NE(imported.paragraphs[0].runs[1].direct & BaselineField, 0);
    parts = opc::read(serializeDocx(imported));
    ASSERT_TRUE(styles.load_string(parts.at("word/styles.xml").c_str()));
    normal = styles.select_node("//w:style[@w:styleId='Normal']/w:rPr/w:vertAlign").node();
    ASSERT_TRUE(normal);
    normal.attribute("w:val").set_value("subscript");
    std::ostringstream changed; styles.save(changed); parts["word/styles.xml"] = changed.str();
    imported = parseDocx(opc::write(parts)).document;
    EXPECT_EQ(imported.paragraphs[0].runs[0].style.baseline, TextBaseline::Subscript);
    EXPECT_EQ(imported.paragraphs[0].runs[1].style.baseline, TextBaseline::Normal);
    EXPECT_DOUBLE_EQ(imported.paragraphs[0].runs[1].style.size, 12);
}
TEST(BaselineText, AutomaticNoteFormattingDoesNotLeakIntoFollowingTyping) {
    for (bool endnote : {false, true}) {
        Editor e;
        e.replace(u"body");
        e.insertFootnote(u"note", endnote);
        Editor restored;
        restored.reset(parseDocx(serializeDocx(e.document)).document);
        const Position end{0, restored.document.paragraphs[0].text().size()};
        restored.selection = {end, end};
        restored.typingStyle = restored.document.styleAt(end);
        restored.replace(u" appended");
        EXPECT_EQ(restored.document.paragraphs[0].runs.back().style.baseline, TextBaseline::Normal);
    }
}
TEST(Widows, DocxDefaultAndExplicitOverridesSurviveInheritanceChanges) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'/>";
    auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.document.paragraphs[0].widowControl);
    EXPECT_TRUE(resolveStyle(imported.document.styles, "Normal").paragraph.widowControl);
    Editor e;
    e.reset(imported.document);
    e.replace(u"override\ninherited");
    e.selection = {{0, 0}, {0, 0}};
    e.formatParagraph([](auto &p) { p.widowControl = false; });
    auto saved = opc::read(serializeDocx(e.document));
    pugi::xml_document body, styles;
    ASSERT_TRUE(body.load_string(saved.at("word/document.xml").c_str()));
    auto flag = body.select_node("//w:body/w:p[1]/w:pPr/w:widowControl").node();
    ASSERT_TRUE(flag);
    EXPECT_STREQ(flag.attribute("w:val").value(), "0");
    EXPECT_EQ(body.select_nodes("//w:body/w:p[2]/w:pPr/w:widowControl").size(), 0u);
    auto restored = parseDocx(opc::write(saved)).document;
    EXPECT_FALSE(restored.paragraphs[0].widowControl);
    EXPECT_TRUE(restored.paragraphs[1].widowControl);
    // An explicit false equal to the base must still override a later true base.
    ASSERT_TRUE(styles.load_string(saved.at("word/styles.xml").c_str()));
    auto normal = styles.select_node("//w:style[@w:styleId='Normal']/w:pPr/w:widowControl").node();
    ASSERT_TRUE(normal);
    normal.attribute("w:val").set_value("0");
    std::ostringstream xml;
    styles.save(xml); saved["word/styles.xml"] = xml.str();
    restored = parseDocx(opc::write(saved)).document;
    EXPECT_FALSE(restored.paragraphs[0].widowControl);
    EXPECT_FALSE(restored.paragraphs[1].widowControl);
    EXPECT_NE(restored.paragraphs[0].direct & WidowControlField, 0);
    EXPECT_EQ(restored.paragraphs[1].direct & WidowControlField, 0);
    saved = opc::read(serializeDocx(restored));
    ASSERT_TRUE(styles.load_string(saved.at("word/styles.xml").c_str()));
    normal = styles.select_node("//w:style[@w:styleId='Normal']/w:pPr/w:widowControl").node();
    ASSERT_TRUE(normal);
    normal.attribute("w:val").set_value("1");
    std::ostringstream changed;
    styles.save(changed); saved["word/styles.xml"] = changed.str();
    restored = parseDocx(opc::write(saved)).document;
    EXPECT_FALSE(restored.paragraphs[0].widowControl);
    EXPECT_TRUE(restored.paragraphs[1].widowControl);
    const auto nativeDefault = parseDocx(serializeDocx(Document{}));
    EXPECT_FALSE(nativeDefault.document.paragraphs[0].widowControl);
}
TEST(Pagination, DocumentDefaultsRemainInheritedWithoutNormalDefinition) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
        "<w:docDefaults><w:pPrDefault><w:pPr><w:keepNext/><w:keepLines/>"
        "</w:pPr></w:pPrDefault></w:docDefaults></w:styles>";
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(resolveStyle(imported.document.styles, "Normal").paragraph.keepWithNext);
    EXPECT_TRUE(resolveStyle(imported.document.styles, "Normal").paragraph.keepLines);
    Editor e;
    e.reset(imported.document);
    e.applyStyle("Normal");
    EXPECT_TRUE(e.document.paragraphs[0].keepWithNext);
    EXPECT_TRUE(e.document.paragraphs[0].keepLines);
}
TEST(Pagination, DocxPreservesExplicitKeepsWithoutPinningInheritance) {
    Editor e;
    e.document.styles.find("Normal")->paragraph.keepWithNext = true;
    e.document.styles.find("Heading1")->paragraph.keepWithNext = true;
    e.applyStyle("Normal");
    e.replace(u"direct\ninherited\nheading");
    e.applyStyle("Heading1");
    auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document body, styles;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:body/w:p[2]/w:pPr/w:keepNext").size(), 0u);
    auto first = body.select_node("//w:body/w:p[1]/w:pPr").node();
    auto direct = first.child("w:keepNext");
    if (!direct) direct = first.append_child("w:keepNext");
    if (!direct.attribute("w:val")) direct.append_attribute("w:val");
    direct.attribute("w:val").set_value("1");
    std::ostringstream xml;
    body.save(xml); parts["word/document.xml"] = xml.str();
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_NE(imported.document.paragraphs[0].direct & KeepWithNextField, 0);
    EXPECT_EQ(imported.document.paragraphs[1].direct & KeepWithNextField, 0);
    EXPECT_EQ(imported.document.styles.find("Heading1")->paragraph.keepWithNext,
              std::optional<bool>(true));
    auto saved = opc::read(serializeDocx(imported.document));
    ASSERT_TRUE(styles.load_string(saved.at("word/styles.xml").c_str()));
    auto normal = styles.select_node("//w:style[@w:styleId='Normal']/w:pPr/w:keepNext").node();
    ASSERT_TRUE(normal);
    normal.attribute("w:val").set_value("0");
    std::ostringstream changed;
    styles.save(changed); saved["word/styles.xml"] = changed.str();
    const auto redefined = parseDocx(opc::write(saved));
    EXPECT_TRUE(redefined.document.paragraphs[0].keepWithNext);
    EXPECT_FALSE(redefined.document.paragraphs[1].keepWithNext);
    EXPECT_TRUE(redefined.document.paragraphs[2].keepWithNext);
}
TEST(Pagination, TableBoundaryKeepIsPreservedWithWarning) {
    Editor e;
    e.replace(u"Heading");
    e.insertTable(1, 1);
    e.selection = {{0, 0}, {0, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = true; });
    const auto imported = parseDocx(serializeDocx(e.document));
    EXPECT_TRUE(imported.document.paragraphs[0].keepWithNext);
    EXPECT_TRUE(std::any_of(imported.warnings.begin(), imported.warnings.end(), [](const auto &w) {
        return w.find("Keep with next across table") != std::string::npos;
    }));
}
TEST(Pagination, DocxKeepsStyleInheritanceAndExplicitFalse) {
    Editor e;
    e.document.styles.find("Heading1")->paragraph.keepWithNext = true;
    e.document.styles.find("Heading1")->paragraph.keepLines = true;
    e.applyStyle("Heading1");
    e.replace(u"Heading\nSecond");
    e.selection = {{0, 0}, {0, 0}};
    e.formatParagraph([](auto &p) { p.keepWithNext = p.keepLines = false; });
    const auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document body, styles;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    ASSERT_TRUE(styles.load_string(parts.at("word/styles.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:body/w:p[1]/w:pPr/w:keepNext[@w:val='0']").size(), 1);
    EXPECT_EQ(body.select_nodes("//w:body/w:p[1]/w:pPr/w:keepLines[@w:val='0']").size(), 1);
    EXPECT_EQ(styles.select_nodes("//w:style[@w:styleId='Heading1']/w:pPr/w:keepNext").size(), 1);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    EXPECT_FALSE(imported.document.paragraphs[0].keepWithNext);
    EXPECT_FALSE(imported.document.paragraphs[0].keepLines);
    EXPECT_TRUE(resolveStyle(imported.document.styles, "Heading1").paragraph.keepWithNext);
    // Omitted w:val is true; explicit off remains false.
    auto changed = parts;
    auto keep = body.select_node("//w:body/w:p[1]/w:pPr/w:keepNext").node();
    keep.remove_attribute("w:val");
    std::ostringstream xml;
    body.save(xml);
    changed["word/document.xml"] = xml.str();
    EXPECT_TRUE(parseDocx(opc::write(changed)).document.paragraphs[0].keepWithNext);
}
TEST(Tabs, WarnsAboutCustomDefaultIntervalAndPreservesSettings) {
    Editor e;
    e.replace(u"Heading\t12");
    auto parts = opc::read(serializeDocx(e.document));
    const std::string settings =
        "<w:settings xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
        "<w:defaultTabStop w:val='1440'/></w:settings>";
    parts["word/settings.xml"] = settings;
    auto &types = parts["[Content_Types].xml"];
    types.insert(types.rfind("</Types>"),
                 "<Override PartName='/word/settings.xml' ContentType='application/"
                 "vnd.openxmlformats-officedocument.wordprocessingml.settings+xml'/>");
    auto &rels = parts["word/_rels/document.xml.rels"];
    rels.insert(rels.rfind("</Relationships>"),
                "<Relationship Id='customSettings' Type='http://schemas.openxmlformats.org/"
                "officeDocument/2006/relationships/settings' Target='settings.xml'/>");
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(std::any_of(imported.warnings.begin(), imported.warnings.end(), [](const auto &w) {
        return w.find("default tab interval") != std::string::npos;
    }));
    EXPECT_EQ(opc::read(serializeDocx(imported.document)).at("word/settings.xml"), settings);
    auto &standard = parts["word/settings.xml"];
    standard.replace(standard.find("1440"), 4, "720");
    const auto usual = parseDocx(opc::write(parts));
    EXPECT_TRUE(usual.warnings.empty());
}
TEST(Tabs, DocxRetainsAlignedStopsAndLeaders) {
    Editor e;
    e.replace(u"Heading\t12");
    e.formatParagraph([](auto &p) { p.tabs = {{240, TabAlignment::Right, TabLeader::Dots}}; });
    const auto bytes = serializeDocx(e.document);
    const auto parts = opc::read(bytes);
    EXPECT_NE(parts.at("word/document.xml").find("w:leader=\"dot\""), std::string::npos);
    const auto restored = parseDocx(bytes);
    EXPECT_EQ(restored.document.paragraphs[0].tabs, e.document.paragraphs[0].tabs);
}
TEST(Docx, ExportsRealRectangularTablesWithMultipleParagraphsAndImages) {
    Document document;
    document.page = {612, 792, 36};
    document.tables = {{10, 2, 2}};
    document.paragraphs = {Paragraph{1}, Paragraph{2}, Paragraph{3}, Paragraph{4},
                           Paragraph{5}, Paragraph{6}, Paragraph{7}};
    document.paragraphs.edit(0).runs = {{u"before", {}}};
    for (std::size_t index = 1; index <= 5; ++index) {
        const auto cell = index < 3 ? 0 : index - 2;
        document.paragraphs.edit(index).cell = TableCell{10, cell / 2, cell % 2};
        document.paragraphs.edit(index).runs = {{u"cell", {}}};
    }
    document.paragraphs.edit(1).runs[0].style.bold = true;
    document.paragraphs.edit(2).runs = {{u"second paragraph", {}}};
    document.paragraphs.edit(6).runs = {{u"after", {}}};
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    document.images.emplace(11, std::make_shared<const ImageResource>(ImageResource{4, 2, png}));
    document.paragraphs.edit(2).runs.push_back(Run::fromImage({11, 72, 36, "cell picture"}));
    document.nextId = 12;
    adoptStyles(document); // hand-built formatting is direct formatting
    const auto parts = opc::read(serializeDocx(document));
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(xml.select_nodes("/w:document/w:body/w:p").size(), 2);
    EXPECT_EQ(xml.select_nodes("//w:tbl").size(), 1);
    EXPECT_EQ(xml.select_nodes("//w:tbl/w:tr").size(), 2);
    EXPECT_EQ(xml.select_nodes("//w:tr/w:tc").size(), 4);
    EXPECT_EQ(xml.select_nodes("(//w:tc)[1]/w:p").size(), 2);
    EXPECT_EQ(xml.select_nodes("(//w:tc)[1]/w:p/w:r/w:drawing").size(), 1);
    EXPECT_STREQ(xml.select_node("(//w:tc)[1]/w:p[2]/w:r/w:t").node().text().get(),
                 "second paragraph");
    EXPECT_STREQ(
        xml.select_node("(//w:tc)[1]/w:p[1]/w:r/w:rPr/w:b").node().attribute("w:val").value(), "1");
    EXPECT_EQ(xml.select_nodes("//w:tblGrid/w:gridCol[@w:w='5400']").size(), 2);
    EXPECT_EQ(xml.select_nodes("//w:tcPr/w:tcW[@w:w='5400'][@w:type='dxa']").size(), 4);
    EXPECT_EQ(xml.select_nodes("//w:tblBorders/*[@w:val='single']").size(), 6);
    EXPECT_EQ(xml.select_nodes("//w:tblCellMar/*[@w:w='80'][@w:type='dxa']").size(), 4);
    EXPECT_EQ(parts.at("word/media/image11.png"), png);
    // Wordforge's own tables and pictures come back without loss or warnings.
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    const auto &back = imported.document;
    EXPECT_EQ(back.text(), document.text());
    ASSERT_EQ(back.tables.size(), 1u);
    EXPECT_EQ(back.tables[0].rows, 2u);
    EXPECT_EQ(back.tables[0].columns, 2u);
    ASSERT_EQ(back.paragraphs.size(), 7u);
    for (std::size_t index = 0; index < 7; ++index) {
        EXPECT_EQ(back.paragraphs[index].cell.has_value(),
                  document.paragraphs[index].cell.has_value());
        if (back.paragraphs[index].cell) {
            EXPECT_EQ(back.paragraphs[index].cell->row, document.paragraphs[index].cell->row);
            EXPECT_EQ(back.paragraphs[index].cell->column, document.paragraphs[index].cell->column);
        }
    }
    EXPECT_TRUE(back.paragraphs[1].runs[0].style.bold);
    const auto *picture = back.paragraphs[2].runs.back().image();
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->width, 72);
    EXPECT_EQ(picture->height, 36);
    EXPECT_EQ(picture->description, "cell picture");
    EXPECT_EQ(back.images.at(picture->resourceId)->png, png);
}
TEST(Docx, ImportsIndependentPythonDocxFixture) {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "fixtures/python-docx-basic.docx";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    const auto imported = parseDocx(bytes);
    EXPECT_EQ(imported.document.text(), u"Independent नमस्ते مرحبا\nnumbered item");
    EXPECT_TRUE(imported.document.paragraphs[0].runs[0].style.bold);
    EXPECT_TRUE(imported.document.paragraphs[0].runs[1].style.italic);
    EXPECT_EQ(imported.document.paragraphs[0].runs[0].style.family, "DejaVu Serif");
    EXPECT_EQ(imported.document.paragraphs[1].list, ListKind::Decimal);
    EXPECT_EQ(imported.document.page.margin, 36);
}
TEST(Docx, RoundTripsUnicodeFormattingListsAndPageGeometry) {
    Editor editor;
    editor.replace(u"  Hello नमस्ते & <world>\t\nمرحبا\nsecond list item");
    editor.selection = {{0, 0}, {0, 7}};
    editor.format([](auto &s) {
        s.family = "DejaVu Serif";
        s.size = 18;
        s.bold = s.italic = s.underline = s.strike = true;
        s.color = 0xff805a3b;
        s.highlight = 0xffeeddaa;
    });
    editor.selection = {{1, 0}, {2, 16}};
    editor.formatParagraph([](auto &p) {
        p.list = ListKind::Decimal;
        p.alignment = Alignment::Right;
        p.indentLeft = 18;
        p.spaceBefore = 12;
        p.spaceAfter = 10;
        p.lineSpacing = 1.5;
    });
    editor.document.paragraphs.edit(1).pageBreakBefore = true;
    editor.document.page = {612, 792, 36};
    const auto bytes = serializeDocx(editor.document);
    EXPECT_EQ(bytes.substr(0, 2), "PK");
    const auto imported = parseDocx(bytes);
    const auto &document = imported.document;
    EXPECT_TRUE(imported.warnings.empty());
    EXPECT_EQ(document.text(), editor.document.text());
    EXPECT_EQ(document.paragraphs[0].runs[0].style, editor.document.paragraphs[0].runs[0].style);
    EXPECT_EQ(static_cast<const ParagraphFormat &>(document.paragraphs[1]),
              static_cast<const ParagraphFormat &>(editor.document.paragraphs[1]));
    EXPECT_EQ(document.page.width, 612);
    EXPECT_EQ(document.page.margin, 36);
}
TEST(Docx, ResolvesRelationshipsNamespacesAndInheritedStyles) {
    opc::Parts parts{
        {"[Content_Types].xml",
         "<Types xmlns='http://schemas.openxmlformats.org/package/2006/content-types'/>"},
        {"_rels/.rels", "<Relationships "
                        "xmlns='http://schemas.openxmlformats.org/package/2006/"
                        "relationships'><Relationship Id='r1' "
                        "Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                        "officeDocument' Target='custom/main.xml'/></Relationships>"},
        {"custom/_rels/main.xml.rels",
         "<Relationships "
         "xmlns='http://schemas.openxmlformats.org/package/2006/relationships'><Relationship "
         "Id='s' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles' "
         "Target='styles.xml'/></Relationships>"},
        {"custom/styles.xml",
         "<x:styles "
         "xmlns:x='http://schemas.openxmlformats.org/wordprocessingml/2006/"
         "main'><x:docDefaults><x:rPrDefault><x:rPr><x:rFonts x:ascii='DejaVu Serif'/><x:sz "
         "x:val='28'/></x:rPr></x:rPrDefault></x:docDefaults><x:style x:type='paragraph' "
         "x:styleId='Base'><x:rPr><x:b/></x:rPr></x:style><x:style x:type='paragraph' "
         "x:styleId='Custom'><x:basedOn x:val='Base'/><x:pPr><x:jc "
         "x:val='center'/></x:pPr><x:rPr><x:i/></x:rPr></x:style></x:styles>"},
        {"custom/main.xml",
         "<x:document "
         "xmlns:x='http://schemas.openxmlformats.org/wordprocessingml/2006/"
         "main'><x:body><x:p><x:pPr><x:pStyle x:val='Custom'/></x:pPr><x:r><x:rPr><x:b "
         "x:val='0'/></x:rPr><x:t xml:space='preserve'>  kept  "
         "</x:t><x:tab/><x:t>नमस्ते</x:t></x:r></x:p></x:body></x:document>"}};
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_EQ(imported.document.text(), u"  kept  \tनमस्ते");
    const auto &paragraph = imported.document.paragraphs[0];
    EXPECT_EQ(paragraph.alignment, Alignment::Center);
    EXPECT_EQ(paragraph.runs[0].style.family, "DejaVu Serif");
    EXPECT_EQ(paragraph.runs[0].style.size, 14);
    EXPECT_FALSE(paragraph.runs[0].style.bold);
    EXPECT_TRUE(paragraph.runs[0].style.italic);
}
TEST(Docx, RejectsBrokenPackagesAndReportsUnsupportedContent) {
    EXPECT_THROW(parseDocx("not a zip"), std::runtime_error);
    EXPECT_THROW(opc::write({{"../escape.xml", "bad"}}), std::runtime_error);
    auto parts = opc::read(serializeDocx(Document{}));
    auto strict = parts;
    const std::string transitional = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    const std::string strictNamespace = "http://purl.oclc.org/ooxml/wordprocessingml/main";
    for (auto &[name, part] : strict) {
        for (std::size_t at = 0; (at = part.find(transitional, at)) != std::string::npos;
             at += strictNamespace.size())
            part.replace(at, transitional.size(), strictNamespace);
    }
    EXPECT_NO_THROW(parseDocx(opc::write(strict)));
    parts["word/document.xml"] = "<!DOCTYPE x><w:document "
                                 "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/"
                                 "main'><w:body/></w:document>";
    EXPECT_THROW(parseDocx(opc::write(parts)), std::runtime_error);
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body><w:tbl/"
        "><w:p><w:r><w:t>kept</w:t><w:drawing/></w:r></w:p></w:body></w:document>";
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_FALSE(imported.warnings.empty());
    EXPECT_EQ(imported.document.text(), u"kept");
    parts["word/document.xml"] = "<w:document xmlns:w='urn:wrong'><w:body/></w:document>";
    EXPECT_THROW(parseDocx(opc::write(parts)), std::runtime_error);
    parts["word/document.xml"] = "<w:document "
                                 "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/"
                                 "main'><w:body><w:p><w:bookmarkStart w:id='1' "
                                 "w:name='chapter'/><w:r><w:t>kept</w:t></w:r><w:bookmarkEnd "
                                 "w:id='1'/></w:p></w:body></w:document>";
    EXPECT_FALSE(parseDocx(opc::write(parts)).warnings.empty());
}
TEST(Docx, InheritsDefaultTrueStylesAndNumberingAndWarnsForCustomLabels) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:style "
        "w:type='paragraph' w:default='true' w:styleId='Default'><w:pPr><w:numPr><w:ilvl "
        "w:val='0'/><w:numId "
        "w:val='9'/></w:numPr></w:pPr><w:rPr><w:b/></w:rPr></w:style></w:styles>";
    parts["word/numbering.xml"] =
        "<w:numbering "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:abstractNum "
        "w:abstractNumId='0'><w:lvl w:ilvl='0'><w:start w:val='1'/><w:numFmt "
        "w:val='decimal'/><w:lvlText w:val='Article %1:'/></w:lvl></w:abstractNum><w:num "
        "w:numId='9'><w:abstractNumId w:val='0'/></w:num></w:numbering>";
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/"
        "main'><w:body><w:p><w:r><w:t>list</w:t></w:r></w:p></w:body></w:document>";
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.document.paragraphs[0].runs[0].style.bold);
    EXPECT_EQ(imported.document.paragraphs[0].list, ListKind::Decimal);
    EXPECT_FALSE(imported.warnings.empty());
}
TEST(Docx, RejectsDuplicateZipNamesOversizeInputAndCyclicStyles) {
    auto duplicate = opc::write({{"a.xml", "first"}, {"b.xml", "second"}});
    for (std::size_t position = 0;
         (position = duplicate.find("b.xml", position)) != std::string::npos; position += 5)
        duplicate.replace(position, 5, "a.xml");
    EXPECT_THROW(opc::read(duplicate), std::runtime_error);
    EXPECT_THROW(opc::read(std::string(16 * 1024 * 1024 + 1, 'x')), std::runtime_error);
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:style "
        "w:styleId='Normal'><w:basedOn w:val='Normal'/></w:style></w:styles>";
    EXPECT_THROW(parseDocx(opc::write(parts)), std::runtime_error);
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'/>";
    std::string nested;
    for (int index = 0; index < 66; ++index)
        nested += "<w:hyperlink>";
    nested += "<w:r><w:t>x</w:t></w:r>";
    for (int index = 0; index < 66; ++index)
        nested += "</w:hyperlink>";
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body><w:p>" +
        nested + "</w:p></w:body></w:document>";
    EXPECT_THROW(parseDocx(opc::write(parts)), std::runtime_error);
}
TEST(Docx, ResolvesStyleTogglesAndExportsComplexScriptEmphasis) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/"
        "main'><w:body><w:p><w:pPr><w:pStyle "
        "w:val='Normal'/></w:pPr><w:r><w:t>toggle</w:t></w:r></w:p></w:body></w:document>";
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:style "
        "w:styleId='Base'><w:rPr><w:b/></w:rPr></w:style><w:style w:styleId='Normal'><w:basedOn "
        "w:val='Base'/><w:rPr><w:b/></w:rPr></w:style></w:styles>";
    EXPECT_FALSE(parseDocx(opc::write(parts)).document.paragraphs[0].runs[0].style.bold);
    Editor editor;
    editor.replace(u"مرحبا שלום");
    editor.selection = {{0, 0}, {0, 10}};
    editor.format([](auto &style) { style.bold = style.italic = true; });
    const auto exported = opc::read(serializeDocx(editor.document));
    EXPECT_NE(exported.at("word/document.xml").find("w:bCs"), std::string::npos);
    EXPECT_NE(exported.at("word/document.xml").find("w:iCs"), std::string::npos);
}
TEST(Docx, ExportsInlinePictureMediaRelationshipsAndGeometry) {
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    Document document;
    document.images.emplace(2, std::make_shared<const ImageResource>(ImageResource{4, 2, png}));
    document.nextId = 3;
    document.paragraphs.edit(0).runs = {{u"before", {}},
                                   Run::fromImage({2, 72, 36, "red & blue"}),
                                   Run::fromImage({2, 36, 18, "second"}),
                                   {u"after", {}}};
    const auto parts = opc::read(serializeDocx(document));
    EXPECT_EQ(parts.at("word/media/image2.png"), png);
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(xml.select_nodes("//w:drawing").size(), 2);
    EXPECT_EQ(xml.select_node("//wp:extent").node().attribute("cx").as_llong(), 914400);
    EXPECT_EQ(xml.select_node("//wp:extent").node().attribute("cy").as_llong(), 457200);
    EXPECT_STREQ(xml.select_node("//wp:docPr").node().attribute("descr").value(), "red & blue");
    EXPECT_STREQ(xml.select_node("//a:blip").node().attribute("r:embed").value(), "image2");
    pugi::xml_document relationships;
    ASSERT_TRUE(relationships.load_string(parts.at("word/_rels/document.xml.rels").c_str()));
    EXPECT_EQ(relationships
                  .select_nodes("//Relationship[@Type='http://schemas.openxmlformats.org/"
                                "officeDocument/2006/relationships/image']")
                  .size(),
              1);
    EXPECT_STREQ(relationships.select_node("//Relationship[@Id='image2']")
                     .node()
                     .attribute("Target")
                     .value(),
                 "media/image2.png");
    EXPECT_STRNE(xml.select_node("(//wp:docPr)[1]").node().attribute("id").value(),
                 xml.select_node("(//wp:docPr)[2]").node().attribute("id").value());
    EXPECT_NE(parts.at("[Content_Types].xml").find("image/png"), std::string::npos);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    EXPECT_EQ(imported.document.text(), u"before￼￼after"); // both pictures come back
    EXPECT_EQ(imported.document.images.size(), 2u);
}
TEST(Docx, OmitsUnreferencedImageMediaAndRelationships) {
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
    auto parts = opc::read(serializeDocx(document));
    EXPECT_TRUE(parts.contains("word/media/image2.png"));
    EXPECT_FALSE(parts.contains("word/media/image3.png"));
    EXPECT_EQ(parts.at("word/_rels/document.xml.rels").find("image3"), std::string::npos);
    document.paragraphs.edit(0).runs = {{u"deleted picture", {}}};
    parts = opc::read(serializeDocx(document));
    for (const auto &[name, data] : parts)
        EXPECT_FALSE(name.starts_with("word/media/"));
    EXPECT_EQ(parts.at("word/_rels/document.xml.rels").find("/image"), std::string::npos);
    EXPECT_EQ(parts.at("[Content_Types].xml").find("image/png"), std::string::npos);
    EXPECT_EQ(parseDocx(opc::write(parts)).document.text(), u"deleted picture");
}
TEST(Docx, RoundTripsCustomParagraphStylesWithBasesAndNextStyles) {
    Editor editor;
    editor.replace(u"Quote\nNote\nBody");
    editor.selection = {{0, 0}, {0, 0}};
    editor.applyStyle("Heading1");
    editor.selection = {{0, 0}, {0, 5}};
    editor.format([](auto &s) {
        s.family = "Georgia";
        s.italic = true;
        s.bold = false; // un-bolds the bold heading it is based on
    });
    editor.formatParagraph([](auto &p) { p.alignment = Alignment::Center; });
    const auto quote = editor.createStyleFromSelection("Pull quote");
    editor.selection = {{1, 0}, {1, 4}};
    editor.format([](auto &s) { s.highlight = 0xffffee88; });
    editor.formatParagraph([](auto &p) { p.indentLeft = 36; });
    const auto note = editor.createStyleFromSelection("Note");
    editor.document.styles.find(note)->next = quote;
    const auto bytes = serializeDocx(editor.document);
    const auto parts = opc::read(bytes);
    const auto &stylesXml = parts.at("word/styles.xml");
    EXPECT_NE(stylesXml.find("w:styleId=\"Pullquote\""), std::string::npos);
    EXPECT_NE(stylesXml.find("w:val=\"Pull quote\""), std::string::npos);
    EXPECT_NE(stylesXml.find("w:val=\"heading 1\""), std::string::npos);
    EXPECT_NE(parts.at("word/document.xml").find("w:pStyle w:val=\"Pullquote\""),
              std::string::npos);

    const auto imported = parseDocx(bytes);
    EXPECT_TRUE(imported.warnings.empty());
    const auto &document = imported.document;
    for (const auto *id : {"Pullquote", "Note"}) {
        const auto *style = document.styles.find(id);
        ASSERT_NE(style, nullptr) << id;
        const auto *original = editor.document.styles.find(id);
        EXPECT_EQ(style->name, original->name);
        EXPECT_EQ(style->basedOn, original->basedOn);
        EXPECT_EQ(style->next, original->next);
        const auto a = resolveStyle(document.styles, id),
                   b = resolveStyle(editor.document.styles, id);
        EXPECT_EQ(a.character, b.character) << id;
        EXPECT_EQ(static_cast<const ParagraphFormat &>(a.paragraph),
                  static_cast<const ParagraphFormat &>(b.paragraph))
            << id;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const auto &p = document.paragraphs[i];
        EXPECT_EQ(p.style, editor.document.paragraphs[i].style);
        EXPECT_EQ(p.runs.front().style, editor.document.paragraphs[i].runs.front().style);
        EXPECT_EQ(p.direct, 0) << i; // everything comes from the style again
        EXPECT_EQ(p.runs.front().direct, 0) << i;
    }
}
TEST(Docx, ImportsWordCustomStylesAndKeepsNamesUnique) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
        "<w:style w:type='paragraph' w:default='1' w:styleId='Normal'><w:name w:val='Normal'/>"
        "<w:rPr><w:sz w:val='22'/></w:rPr></w:style>"
        "<w:style w:type='paragraph' w:styleId='MyQuote'><w:name w:val='My Quote'/>"
        "<w:basedOn w:val='Normal'/><w:next w:val='Normal'/><w:pPr><w:jc w:val='center'/></w:pPr>"
        "<w:rPr><w:i/></w:rPr></w:style>"
        "<w:style w:type='paragraph' w:styleId='Fancy'><w:name w:val='Title'/>"
        "<w:basedOn w:val='MyQuote'/><w:rPr><w:color w:val='C00000'/></w:rPr></w:style>"
        "<w:style w:type='paragraph' w:styleId='Unused'><w:name w:val='Unused'/></w:style>"
        "</w:styles>";
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body>"
        "<w:p><w:pPr><w:pStyle w:val='Fancy'/></w:pPr><w:r><w:t>fancy</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:pStyle w:val='MyQuote'/></w:pPr><w:r><w:rPr><w:b/></w:rPr>"
        "<w:t>quote</w:t></w:r></w:p></w:body></w:document>";
    const auto imported = parseDocx(opc::write(parts));
    const auto &document = imported.document;
    const auto *quote = document.styles.find("MyQuote");
    ASSERT_NE(quote, nullptr);
    EXPECT_EQ(quote->name, "My Quote");
    EXPECT_EQ(quote->basedOn, "Normal");
    EXPECT_EQ(quote->next, "Normal");
    const auto *fancy = document.styles.find("Fancy");
    ASSERT_NE(fancy, nullptr);
    EXPECT_EQ(fancy->name, "Title (2)"); // "Title" is Wordforge's built-in style
    EXPECT_EQ(fancy->basedOn, "MyQuote");
    EXPECT_EQ(document.styles.find("Unused"), nullptr);
    EXPECT_EQ(document.paragraphs[0].style, "Fancy");
    const auto &fancyRun = document.paragraphs[0].runs.front();
    EXPECT_TRUE(fancyRun.style.italic);
    EXPECT_EQ(fancyRun.style.color, 0xffc00000u);
    EXPECT_EQ(fancyRun.style.size, 11);
    EXPECT_EQ(document.paragraphs[0].alignment, Alignment::Center);
    // Word's Normal (11 pt) becomes Wordforge's Normal; styles based on it inherit the size.
    EXPECT_EQ(document.styles.find("Normal")->character.size, 11);
    EXPECT_FALSE(quote->character.size);
    EXPECT_EQ(fancyRun.direct, 0);
    const auto &quoteRun = document.paragraphs[1].runs.front();
    EXPECT_EQ(document.paragraphs[1].style, "MyQuote");
    EXPECT_TRUE(quoteRun.style.bold);
    EXPECT_EQ(quoteRun.direct, BoldField);
}
TEST(Docx, RoundTripsCharacterStyles) {
    Editor editor;
    editor.replace(u"see the Code here");
    editor.selection = {{0, 8}, {0, 12}};
    editor.format([](auto &s) {
        s.family = "Courier New";
        s.color = 0xff2a6a2a;
    });
    editor.createCharacterStyleFromSelection("Inline code");
    const auto bytes = serializeDocx(editor.document);
    const auto parts = opc::read(bytes);
    EXPECT_NE(parts.at("word/document.xml").find("w:rStyle w:val=\"Inlinecode\""),
              std::string::npos);
    EXPECT_NE(parts.at("word/styles.xml").find("w:type=\"character\""), std::string::npos);
    const auto imported = parseDocx(bytes);
    EXPECT_TRUE(imported.warnings.empty());
    const auto &document = imported.document;
    const auto *style = document.styles.findCharacter("Inlinecode");
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->name, "Inline code");
    const auto &runs = document.paragraphs[0].runs;
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[1].characterStyle, "Inlinecode");
    EXPECT_EQ(runs[1].style, editor.document.paragraphs[0].runs[1].style);
    EXPECT_EQ(runs[1].direct, 0);
    EXPECT_TRUE(runs[0].characterStyle.empty());
}

namespace {
opc::Parts wordDocument(const std::string &body, const std::string &rels = {}) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/document.xml"] =
        "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main' "
        "xmlns:wp='http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing' "
        "xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' "
        "xmlns:pic='http://schemas.openxmlformats.org/drawingml/2006/picture' "
        "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'><w:body>" +
        body + "</w:body></w:document>";
    if (!rels.empty()) {
        auto &relationships = parts["word/_rels/document.xml.rels"];
        relationships.insert(relationships.rfind("</Relationships>"), rels);
    }
    return parts;
}
std::string cell(const std::string &text, const std::string &properties = {}) {
    return "<w:tc>" + properties + "<w:p><w:r><w:t>" + text + "</w:t></w:r></w:p></w:tc>";
}
std::string drawing(const char *kind, const char *id, long long cx, long long cy) {
    return std::string("<w:r><w:drawing><wp:") + kind + "><wp:extent cx='" + std::to_string(cx) +
           "' cy='" + std::to_string(cy) +
           "'/><wp:docPr id='1' name='Photo' descr='A photo'/><a:graphic><a:graphicData><pic:pic>"
           "<pic:blipFill><a:blip r:embed='" +
           id +
           "'/></pic:blipFill></pic:pic></a:graphicData>"
           "</a:graphic></wp:" +
           kind + "></w:drawing></w:r>";
}
std::string redBluePng() {
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace
TEST(Docx, ImportsWordTablesSplittingMergesAndFlatteningNestedTables) {
    // Two adjacent tables, the second at the very end: Wordforge inserts body paragraphs.
    const auto body =
        "<w:p><w:r><w:t>intro</w:t></w:r></w:p>"
        "<w:tbl><w:tblPr><w:tblStyle w:val='TableGrid'/><w:tblLook w:val='04A0'/></w:tblPr>"
        "<w:tblGrid><w:gridCol w:w='3000'/><w:gridCol w:w='3000'/><w:gridCol "
        "w:w='3000'/></w:tblGrid>"
        "<w:tr>" +
        cell("wide", "<w:tcPr><w:gridSpan w:val='2'/></w:tcPr>") + cell("c") +
        "</w:tr>"
        "<w:tr>" +
        cell("a") + cell("b") +
        "</w:tr>" // short row: padded with an empty cell
        "<w:tr><w:tc><w:p><w:r><w:t>outer</w:t></w:r></w:p><w:tbl><w:tr>" +
        cell("inner") + "</w:tr></w:tbl><w:p/></w:tc>" + cell("x") +
        cell("y", "<w:tcPr><w:vMerge/></w:tcPr>") +
        "</w:tr></w:tbl>"
        "<w:tbl><w:tr>" +
        cell("second") + "</w:tr></w:tbl>";
    const auto imported = parseDocx(opc::write(wordDocument(body)));
    const auto &document = imported.document;
    EXPECT_NO_THROW(validateDocument(document));
    ASSERT_EQ(document.tables.size(), 2u);
    EXPECT_EQ(document.tables[0].rows, 3u);
    EXPECT_EQ(document.tables[0].columns, 3u);
    EXPECT_EQ(document.tables[1].rows, 1u);
    EXPECT_EQ(document.tables[1].columns, 1u);
    auto cellText = [&](std::uint64_t table, std::size_t row, std::size_t column) {
        std::u16string text;
        for (const auto &p : document.paragraphs)
            if (p.cell && p.cell->tableId == table && p.cell->row == row &&
                p.cell->column == column)
                text += p.text() + u"|";
        return text;
    };
    const auto first = document.tables[0].id;
    // gridSpan becomes a merge; the vMerge continuation below has no cell above to join.
    ASSERT_EQ(document.tables[0].merges.size(), 1u);
    EXPECT_EQ(document.tables[0].merges[0], (CellMerge{0, 0, 1, 2}));
    EXPECT_EQ(document.tables[0].widths, (std::vector<double>{150, 150, 150}));
    EXPECT_EQ(cellText(first, 0, 0), u"wide|");
    EXPECT_EQ(cellText(first, 0, 1), u"|"); // covered by the span
    EXPECT_EQ(cellText(first, 0, 2), u"c|");
    EXPECT_EQ(cellText(first, 1, 2), u"|"); // padded
    EXPECT_EQ(cellText(first, 2, 0), u"outer|inner||");
    EXPECT_FALSE(document.paragraphs.back().cell); // a body paragraph closes the document
    // The tables are separated by a body paragraph.
    std::size_t separators = 0;
    for (std::size_t i = 1; i + 1 < document.paragraphs.size(); ++i)
        if (!document.paragraphs[i].cell && document.paragraphs[i - 1].cell &&
            document.paragraphs[i + 1].cell)
            ++separators;
    EXPECT_EQ(separators, 1u);
    auto warned = [&](const char *text) {
        return std::any_of(
            imported.warnings.begin(), imported.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_FALSE(warned("Merged table cells"));
    EXPECT_TRUE(warned("Nested tables"));
    EXPECT_FALSE(warned("Unsupported"));
}
TEST(Docx, LargeTablesBecomeParagraphs) {
    std::string row = "<w:tr>";
    for (int column = 0; column < 11; ++column)
        row += cell("c" + std::to_string(column));
    row += "</w:tr>";
    const auto imported = parseDocx(opc::write(wordDocument("<w:tbl>" + row + "</w:tbl>")));
    EXPECT_TRUE(imported.document.tables.empty());
    EXPECT_EQ(imported.document.paragraphs.size(), 11u);
    EXPECT_NO_THROW(validateDocument(imported.document));
}
TEST(Docx, ImportsPicturesConvertingOtherFormatsAndFittingThePage) {
    const auto png = redBluePng();
    ASSERT_FALSE(png.empty());
    const std::string rels =
        "<Relationship Id='rPng' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/image' Target='media/a.png'/>"
        "<Relationship Id='rJpg' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/image' Target='media/b.jpeg'/>";
    // A 20-inch-wide inline PNG, and a floating "JPEG".
    const auto body = "<w:p><w:r><w:t>see </w:t></w:r>" +
                      drawing("inline", "rPng", 18288000, 9144000) + "<w:r><w:t> and </w:t></w:r>" +
                      drawing("anchor", "rJpg", 914400, 914400) + "</w:p>";
    auto parts = wordDocument(body, rels);
    parts["word/media/a.png"] = png;
    parts["word/media/b.jpeg"] = "\xff\xd8\xff\xe0 not really a jpeg";
    const auto bytes = opc::write(parts);

    const auto withoutConverter = parseDocx(bytes);
    const auto &plain = withoutConverter.document;
    ASSERT_EQ(plain.images.size(), 1u);
    const auto &runs = plain.paragraphs[0].runs;
    ASSERT_EQ(runs.size(), 3u);
    const auto *picture = runs[1].image();
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->description, "A photo");
    const double content = plain.page.width - 2 * plain.page.margin;
    EXPECT_LE(picture->width, content);
    EXPECT_NEAR(picture->width / picture->height, 2.0, 0.01); // aspect ratio kept
    EXPECT_EQ(plain.images.at(picture->resourceId)->png, png);
    auto warned = [](const DocxImport &result, const char *text) {
        return std::any_of(
            result.warnings.begin(), result.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned(withoutConverter, "unsupported format"));
    EXPECT_TRUE(warned(withoutConverter, "Floating pictures"));

    // With a converter (the desktop uses Qt) the second picture is kept too.
    int conversions = 0;
    const auto converted = parseDocx(bytes, [&](const std::string &) {
        ++conversions;
        return std::optional<std::string>(png);
    });
    EXPECT_EQ(conversions, 1);
    EXPECT_EQ(converted.document.images.size(), 2u);
    EXPECT_FALSE(warned(converted, "unsupported format"));
    const auto &last = converted.document.paragraphs[0].runs.back();
    ASSERT_NE(last.image(), nullptr);
    EXPECT_EQ(last.image()->width, 72); // one inch, fits
    EXPECT_NO_THROW(validateDocument(converted.document));
}

TEST(Docx, HeadersAndFootersRoundTripWithPageFields) {
    Document document;
    document.page.header = {u"Report", u"", u"Draft"};
    document.page.footer = {u"", u"Page {PAGE} of {NUMPAGES}", u""};
    document.page.differentFirstPage = true;
    const auto parts = opc::read(serializeDocx(document));
    ASSERT_TRUE(parts.contains("word/header.xml"));
    ASSERT_TRUE(parts.contains("word/footer.xml"));
    ASSERT_TRUE(parts.contains("word/footerFirst.xml"));
    EXPECT_NE(parts.at("word/footer.xml").find("instr=\" PAGE \""), std::string::npos);
    EXPECT_NE(parts.at("word/footer.xml").find("instr=\" NUMPAGES \""), std::string::npos);
    EXPECT_NE(parts.at("[Content_Types].xml").find("wordprocessingml.footer+xml"),
              std::string::npos);
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(parts.at("word/document.xml").c_str()));
    const auto section = xml.select_node("//w:sectPr").node();
    EXPECT_STREQ(section.first_child().name(), "w:headerReference"); // schema order
    EXPECT_TRUE(section.child("w:titlePg"));
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    const auto &page = imported.document.page; // geometry is rounded to twips
    EXPECT_EQ(page.header, document.page.header);
    EXPECT_EQ(page.footer, document.page.footer);
    EXPECT_TRUE(page.differentFirstPage);
}
TEST(Docx, ImportsWordHeadersAndFootersWithComplexFields) {
    const std::string rels =
        "<Relationship Id='rH' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/header' Target='header1.xml'/>"
        "<Relationship Id='rF' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/footer' Target='footer1.xml'/>"
        "<Relationship Id='rE' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/header' Target='header2.xml'/>";
    auto parts =
        wordDocument("<w:p><w:r><w:t>body</w:t></w:r></w:p><w:sectPr>"
        "<w:headerReference w:type='default' r:id='rH'/>"
        "<w:headerReference w:type='even' r:id='rE'/>"
        "<w:footerReference w:type='default' r:id='rF'/>"
        "<w:pgSz w:w='12240' w:h='15840'/><w:pgMar w:top='1440' w:bottom='1440' "
        "w:left='1440' w:right='1440' w:header='720' w:footer='720'/></w:sectPr>",
        rels);
    const std::string ns =
        " xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'";
    // Header: "Title<tab><tab>Confidential" with a picture that is dropped.
    parts["word/header1.xml"] =
        "<w:hdr" + ns +
                                "><w:p><w:r><w:t>Title</w:t></w:r><w:r><w:tab/></w:r><w:r><w:tab/>"
                                "<w:t>Confidential</w:t></w:r><w:r><w:drawing/></w:r></w:p></w:hdr>";
    parts["word/header2.xml"] = "<w:hdr" + ns + "><w:p/></w:hdr>";
    // Footer: centred "Page N of M" with Word's complex fields and stale results.
    auto complex = [](const char *code, const char *result) {
        return std::string("<w:r><w:fldChar w:fldCharType='begin'/></w:r><w:r><w:instrText "
                           "xml:space='preserve'> ") +
               code +
               " \\* MERGEFORMAT </w:instrText></w:r><w:r><w:fldChar w:fldCharType='separate'/>"
               "</w:r><w:r><w:t>" +
               result + "</w:t></w:r><w:r><w:fldChar w:fldCharType='end'/></w:r>";
    };
    parts["word/footer1.xml"] = "<w:ftr" + ns +
                                "><w:p><w:pPr><w:jc w:val='center'/></w:pPr><w:r><w:t "
                                "xml:space='preserve'>Page </w:t></w:r>" +
                                complex("PAGE", "7") +
                                "<w:r><w:t xml:space='preserve'> of </w:t></w:r>" +
                                complex("NUMPAGES", "9") + "</w:p></w:ftr>";
    const auto imported = parseDocx(opc::write(parts));
    const auto &page = imported.document.page;
    EXPECT_EQ(page.header.left, u"Title");
    EXPECT_EQ(page.header.center, u"");
    EXPECT_EQ(page.header.right, u"Confidential");
    EXPECT_EQ(page.footer.center, u"Page {PAGE} of {NUMPAGES}");
    EXPECT_EQ(page.footer.left, u"");
    EXPECT_FALSE(page.differentFirstPage);
    auto warned = [&](const char *text) {
        return std::any_of(
            imported.warnings.begin(), imported.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("even-page"));
    EXPECT_TRUE(warned("Pictures and tables in headers"));
    EXPECT_FALSE(warned("Section features"));
    EXPECT_NO_THROW(validateDocument(imported.document));
}

TEST(Docx, MergedCellsAndColumnWidthsRoundTrip) {
    Editor e;
    e.insertTable(3, 3);
    e.replace(u"top");
    auto index = [&](std::size_t row, std::size_t column) {
        for (std::size_t i = 0; i < e.document.paragraphs.size(); ++i)
            if (e.document.paragraphs[i].cell == TableCell{e.document.tables[0].id, row, column})
                return i;
        return std::size_t{0};
    };
    e.selection = {{index(0, 0), 0}, {index(1, 1), 0}};
    e.mergeCells();
    e.setColumnWidth(150);
    // Text typed into a covered cell is saved inside the merged cell.
    e.selection = {{index(1, 0), 0}, {index(1, 0), 0}};
    e.replace(u"typed");
    const auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document xml;
    ASSERT_TRUE(xml.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(xml.select_nodes("//w:gridCol").size(), 3u);
    EXPECT_STREQ(xml.select_node("(//w:gridCol)[1]").node().attribute("w:w").value(), "3000");
    EXPECT_STREQ(xml.select_node("//w:vMerge[@w:val='restart']/../w:gridSpan")
                     .node()
                     .attribute("w:val")
                     .value(),
                 "2");
    EXPECT_EQ(xml.select_nodes("//w:vMerge[not(@w:val)]").size(), 1u);
    EXPECT_EQ(xml.select_nodes("//w:tr[1]/w:tc").size(), 2u);
    EXPECT_EQ(xml.select_nodes("//w:tr[2]/w:tc").size(), 2u);
    EXPECT_EQ(xml.select_nodes("//w:tr[3]/w:tc").size(), 3u);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    const auto &table = imported.document.tables.at(0);
    EXPECT_EQ(table.merges, e.document.tables[0].merges);
    ASSERT_EQ(table.widths.size(), 3u);
    EXPECT_NEAR(table.widths[0], 150, 0.05);
    std::u16string merged;
    for (const auto &p : imported.document.paragraphs)
        if (p.cell && p.cell->row == 0 && p.cell->column == 0)
            merged += p.text() + u"|";
    EXPECT_EQ(merged, u"top|typed|");
    EXPECT_NO_THROW(validateDocument(imported.document));
}

namespace {
std::string fixture(const char *name) {
    std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" /
                           name,
                       std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
const PreservedPart *keptPart(const Document &document, const std::string &path) {
    if (!document.preserved)
        return nullptr;
    for (const auto &part : document.preserved->parts)
        if (part.path == path)
            return &part;
    return nullptr;
}
} // namespace
TEST(Docx, KeepsDocumentPropertiesThemeSettingsAndCustomXmlThroughEditsAndSave) {
    const auto bytes = fixture("python-docx-basic.docx");
    ASSERT_FALSE(bytes.empty());
    const auto source = opc::read(bytes);
    auto imported = parseDocx(bytes);
    auto &document = imported.document;
    ASSERT_TRUE(document.preserved);
    for (const auto *path :
         {"docProps/core.xml", "docProps/app.xml", "word/theme/theme1.xml", "word/settings.xml",
          "word/webSettings.xml", "word/fontTable.xml", "customXml/item1.xml",
          "customXml/_rels/item1.xml.rels", "customXml/itemProps1.xml"}) {
        const auto *part = keptPart(document, path);
        ASSERT_NE(part, nullptr) << path;
        EXPECT_EQ(part->data, source.at(path)) << path; // byte for byte
    }
    EXPECT_FALSE(keptPart(document, "docProps/thumbnail.jpeg")); // stale after edits
    EXPECT_FALSE(keptPart(document, "word/stylesWithEffects.xml"));
    EXPECT_EQ(document.preserved->relationships.size(), 7u);
    for (const auto &warning : imported.warnings)
        EXPECT_EQ(warning.find("not kept"), std::string::npos) << warning;

    // Edit, save, and look at the package Word would read.
    Editor editor;
    editor.document = document;
    editor.selection = {{0, 0}, {0, 0}};
    editor.replace(u"Edited. ");
    const auto saved = opc::read(serializeDocx(editor.document));
    for (const auto *path : {"docProps/core.xml", "word/theme/theme1.xml", "customXml/item1.xml",
                             "customXml/itemProps1.xml", "customXml/_rels/item1.xml.rels"})
        EXPECT_EQ(saved.at(path), source.at(path)) << path;
    pugi::xml_document rels, documentRels, types;
    ASSERT_TRUE(rels.load_string(saved.at("_rels/.rels").c_str()));
    ASSERT_TRUE(documentRels.load_string(saved.at("word/_rels/document.xml.rels").c_str()));
    ASSERT_TRUE(types.load_string(saved.at("[Content_Types].xml").c_str()));
    EXPECT_STREQ(rels.select_node("//*[contains(@Type,'core-properties')]")
                     .node()
                     .attribute("Target")
                     .value(),
                 "docProps/core.xml");
    EXPECT_STREQ(documentRels.select_node("//*[contains(@Type,'/theme')]")
                     .node()
                     .attribute("Target")
                     .value(),
                 "theme/theme1.xml");
    EXPECT_STREQ(documentRels.select_node("//*[contains(@Type,'/customXml')]")
                     .node()
                     .attribute("Target")
                     .value(),
                 "../customXml/item1.xml");
    EXPECT_STREQ(types.select_node("//*[@PartName='/customXml/itemProps1.xml']")
                     .node()
                     .attribute("ContentType")
                     .value(),
                 "application/vnd.openxmlformats-officedocument.customXmlProperties+xml");
    // Every internal relationship target exists.
    for (const auto &[path, data] : saved) {
        if (!path.ends_with(".rels"))
            continue;
        pugi::xml_document relationships;
        ASSERT_TRUE(relationships.load_string(data.c_str()));
        const auto directory = path.substr(0, path.find("_rels/"));
        for (auto node : relationships.document_element().children()) {
            if (std::string_view(node.attribute("TargetMode").value()) == "External")
                continue;
            const auto target =
                (std::filesystem::path(directory) / node.attribute("Target").value())
                                    .lexically_normal()
                                    .generic_string();
            EXPECT_TRUE(saved.contains(target)) << path << " -> " << target;
        }
    }
    // Reopening keeps the same parts.
    const auto again = parseDocx(opc::write(saved));
    ASSERT_TRUE(again.document.preserved);
    EXPECT_EQ(*again.document.preserved, *document.preserved);
    EXPECT_EQ(again.document.paragraphs[0].text().substr(0, 8), u"Edited. ");
}
TEST(Docx, NamesDroppedPartsAndKeepsOnlyCompleteOnes) {
    const std::string w = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    const std::string rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
    auto parts = wordDocument("<w:p><w:r><w:t>x</w:t></w:r></w:p>",
                              "<Relationship Id='s' Type='" + rel +
                                  "settings' Target='settings.xml'/>"
                                  "<Relationship Id='t' Type='" +
                                  rel +
                                  "theme' Target='theme/theme1.xml'/>"
                                  "<Relationship Id='c' Type='" +
                                  rel +
                                  "comments' Target='comments.xml'/>"
                                  "<Relationship Id='f' Type='" +
                                  rel +
                                  "footnotes' Target='footnotes.xml'/>"
                                  "<Relationship Id='e' Type='" +
                                  rel + "endnotes' Target='endnotes.xml'/>");
    parts["word/endnotes.xml"] =
        "<w:endnotes xmlns:w='" + w + "'><w:endnote w:id='1'><w:p/></w:endnote></w:endnotes>";
    parts["word/settings.xml"] =
        "<w:settings xmlns:w='" + w +
        "'><w:zoom w:percent='100'/><w:evenAndOddHeaders/>"
        "<w:footnotePr><w:footnote w:id='-1'/></w:footnotePr><w:defaultTabStop w:val='720'/>"
        "</w:settings>";
    // The theme references an image that is missing: it is not kept.
    parts["word/theme/theme1.xml"] = "<a:theme xmlns:a='http://schemas.openxmlformats.org/"
                                     "drawingml/2006/main' name='T'/>";
    parts["word/theme/_rels/theme1.xml.rels"] =
        "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
        "<Relationship Id='i' Type='" +
        rel + "image' Target='../media/gone.png'/></Relationships>";
    parts["word/comments.xml"] =
        "<w:comments xmlns:w='" + w + "'><w:comment w:id='1'><w:p/></w:comment></w:comments>";
    parts["word/footnotes.xml"] = "<w:footnotes xmlns:w='" + w +
                                  "'><w:footnote w:type='separator' w:id='-1'><w:p/></w:footnote>"
                                  "</w:footnotes>"; // separators only: nothing to miss
    auto &types = parts["[Content_Types].xml"];
    types.insert(types.rfind("</Types>"),
                 "<Override PartName='/word/settings.xml' ContentType='application/"
                 "vnd.openxmlformats-officedocument.wordprocessingml.settings+xml'/>"
                 "<Override PartName='/word/theme/theme1.xml' ContentType='application/"
                 "vnd.openxmlformats-officedocument.theme+xml'/>");
    const auto imported = parseDocx(opc::write(parts));
    ASSERT_TRUE(imported.document.preserved);
    const auto *settings = keptPart(imported.document, "word/settings.xml");
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->data.find("footnotePr"), std::string::npos);
    EXPECT_EQ(settings->data.find("evenAndOddHeaders"), std::string::npos);
    EXPECT_NE(settings->data.find("defaultTabStop"), std::string::npos);
    EXPECT_FALSE(keptPart(imported.document, "word/theme/theme1.xml"));
    std::string named;
    for (const auto &warning : imported.warnings)
        if (warning.find("not kept when saving") != std::string::npos)
            named = warning;
    EXPECT_EQ(named.find("endnotes"), std::string::npos); // imported since 0.6.8
    EXPECT_EQ(named.find("footnotes"), std::string::npos);
    EXPECT_EQ(named.find("comments"), std::string::npos); // imported (this one has no range)
    // A document without kept parts exports exactly as before.
    EXPECT_FALSE(parseDocx(serializeDocx(Document{})).document.preserved);
}

TEST(Docx, FootnotesRoundTripAndImportFromWord) {
    Editor e;
    e.replace(u"First claim");
    e.insertFootnote(u"First source.");
    e.replace(u" and second");
    e.insertFootnote(u"Second source & more.");
    const auto parts = opc::read(serializeDocx(e.document));
    ASSERT_TRUE(parts.contains("word/footnotes.xml"));
    pugi::xml_document notes;
    ASSERT_TRUE(notes.load_string(parts.at("word/footnotes.xml").c_str()));
    EXPECT_EQ(notes.select_nodes("//w:footnote").size(), 4u); // two separators + two notes
    EXPECT_EQ(notes.select_nodes("//w:footnoteRef").size(), 2u);
    pugi::xml_document body;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:footnoteReference").size(), 2u);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    ASSERT_EQ(imported.document.footnotes.size(), 2u);
    EXPECT_EQ(imported.document.footnotes[0].text, u"First source.");
    EXPECT_EQ(imported.document.footnotes[1].text, u"Second source & more.");
    EXPECT_EQ(imported.document.text(), e.document.text());

    // A Word-style footnotes part: separators, styled reference, two paragraphs in a note.
    const std::string w = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    auto word = wordDocument(
        "<w:p><w:r><w:t>Text</w:t></w:r><w:r><w:rPr><w:rStyle w:val='FootnoteReference'/>"
        "<w:vertAlign w:val='superscript'/></w:rPr><w:footnoteReference w:id='1'/></w:r>"
        "<w:r><w:endnoteReference w:id='1'/></w:r></w:p>",
        "<Relationship Id='fn' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/footnotes' Target='footnotes.xml'/>");
    word["word/footnotes.xml"] =
        "<w:footnotes xmlns:w='" + w +
        "'><w:footnote w:type='separator' w:id='-1'><w:p><w:r><w:separator/></w:r></w:p>"
        "</w:footnote><w:footnote w:id='1'><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t "
        "xml:space='preserve'> See page</w:t></w:r><w:r><w:tab/><w:t>12.</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>Also chapter 3.</w:t></w:r></w:p></w:footnote></w:footnotes>";
    const auto fromWord = parseDocx(opc::write(word));
    ASSERT_EQ(fromWord.document.footnotes.size(), 1u);
    EXPECT_EQ(fromWord.document.footnotes[0].text, u"See page 12. Also chapter 3.");
    auto warned = [&](const char *text) {
        return std::any_of(
            fromWord.warnings.begin(), fromWord.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("one paragraph of plain text"));
    EXPECT_TRUE(warned("text is missing")); // the endnote reference has no endnotes part
    EXPECT_FALSE(warned("vertAlign"));
    EXPECT_FALSE(warned("not kept when saving"));
    EXPECT_NO_THROW(validateDocument(fromWord.document));
}

TEST(Docx, CommentsRoundTripAndImportFromWord) {
    Editor e;
    e.replace(u"Alpha beta gamma\nsecond");
    e.selection = {{0, 6}, {0, 10}}; // "beta"
    e.addComment(u"Check this word.", "Ana Lima", "2026-11-16T10:00:00Z");
    e.selection = {{0, 11}, {1, 3}}; // "gamma" to "sec"
    e.addComment(u"Across paragraphs.", "Bo", "");
    const auto parts = opc::read(serializeDocx(e.document));
    ASSERT_TRUE(parts.contains("word/comments.xml"));
    pugi::xml_document body, comments;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    ASSERT_TRUE(comments.load_string(parts.at("word/comments.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:commentRangeStart").size(), 2u);
    EXPECT_EQ(body.select_nodes("//w:commentRangeEnd").size(), 2u);
    EXPECT_EQ(body.select_nodes("//w:commentReference").size(), 2u);
    EXPECT_STREQ(
        comments.select_node("//w:comment[@w:id='0']").node().attribute("w:initials").value(),
                 "AL");
    // The text between the first comment's markers is exactly "beta".
    std::string between;
    bool inside = false;
    for (auto node : body.select_node("//w:p").node().children()) {
        const std::string name = node.name();
        if (name == "w:commentRangeStart" && std::string(node.attribute("w:id").value()) == "0")
            inside = true;
        else if (name == "w:commentRangeEnd" && std::string(node.attribute("w:id").value()) == "0")
            inside = false;
        else if (inside && name == "w:r")
            for (auto t : node.children("w:t"))
                between += t.text().get();
    }
    EXPECT_EQ(between, "beta");
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    ASSERT_EQ(imported.document.comments.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto &a = imported.document.comments[i], &b = e.document.comments[i];
        EXPECT_EQ(a.text, b.text);
        EXPECT_EQ(a.author, b.author);
        EXPECT_EQ(a.date, b.date);
        EXPECT_EQ(imported.document.resolve(a.start), e.document.resolve(b.start));
        EXPECT_EQ(imported.document.resolve(a.end), e.document.resolve(b.end));
    }

    // Word-style: a range across a line break inside one Word paragraph, and a comment
    // without any range.
    const std::string w = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    auto word = wordDocument(
        "<w:p><w:r><w:t>one </w:t></w:r><w:commentRangeStart w:id='3'/><w:r><w:t>two</w:t>"
        "<w:br/><w:t>three</w:t></w:r><w:commentRangeEnd w:id='3'/><w:r><w:rPr><w:rStyle "
        "w:val='CommentReference'/></w:rPr><w:commentReference w:id='3'/></w:r></w:p>",
        "<Relationship Id='c' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/comments' Target='comments.xml'/>");
    word["word/comments.xml"] =
        "<w:comments xmlns:w='" + w +
        "'><w:comment w:id='3' w:author='Cy' w:date='2026-01-02T03:04:05Z'><w:p><w:r>"
        "<w:annotationRef/></w:r><w:r><w:t>Reword.</w:t></w:r></w:p></w:comment>"
        "<w:comment w:id='4' w:author='Dee'><w:p><w:r><w:t>Lost</w:t></w:r></w:p></w:comment>"
        "</w:comments>";
    const auto fromWord = parseDocx(opc::write(word));
    ASSERT_EQ(fromWord.document.comments.size(), 1u);
    const auto &comment = fromWord.document.comments[0];
    EXPECT_EQ(comment.author, "Cy");
    EXPECT_EQ(comment.text, u"Reword.");
    const auto start = fromWord.document.resolve(comment.start);
    const auto end = fromWord.document.resolve(comment.end);
    ASSERT_TRUE(start && end);
    EXPECT_EQ(*start, (Position{0, 4}));
    EXPECT_EQ(*end, (Position{1, 5})); // after "three" in the paragraph made by the break
    auto warned = [&](const char *text) {
        return std::any_of(
            fromWord.warnings.begin(), fromWord.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("without a marked text range"));
    EXPECT_FALSE(warned("not kept when saving"));
    EXPECT_FALSE(warned("commentRange"));
    EXPECT_FALSE(warned("commentReference"));
}

TEST(Docx, TrackedChangesRoundTripAndImportFromWord) {
    Editor e;
    e.replace(u"Alpha beta");
    e.reviewer = "Ana Lima";
    e.reviewDate = "2026-11-16T11:00:00Z";
    e.setTrackChanges(true);
    e.selection = {{0, 6}, {0, 10}};
    e.replace(u"gamma");
    const auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document body;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:ins/w:r").size(), 1u);
    EXPECT_EQ(body.select_nodes("//w:del/w:r/w:delText").size(), 1u);
    EXPECT_STREQ(body.select_node("//w:ins").node().attribute("w:author").value(), "Ana Lima");
    ASSERT_TRUE(parts.contains("word/settings.xml"));
    EXPECT_NE(parts.at("word/settings.xml").find("trackRevisions"), std::string::npos);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    EXPECT_TRUE(imported.document.trackChanges);
    ASSERT_EQ(imported.document.paragraphs[0].runs.size(), 3u);
    EXPECT_EQ(imported.document.paragraphs[0].text(), u"Alpha betagamma");
    EXPECT_TRUE(imported.document.paragraphs[0].runs[1].deleted);
    EXPECT_TRUE(imported.document.paragraphs[0].runs[2].inserted);
    ASSERT_EQ(imported.document.revisions.size(), 1u);
    EXPECT_EQ(imported.document.revisions[0].author, "Ana Lima");
    EXPECT_EQ(imported.document.revisions[0].date, "2026-11-16T11:00:00Z");

    // Word: an insertion holding a deletion and a line break; a move counts as both.
    auto word = wordDocument(
        "<w:p><w:r><w:t>a</w:t></w:r><w:ins w:id='1' w:author='Cy' w:date='2026-01-01T00:00:00Z'>"
        "<w:r><w:t>b</w:t><w:br/><w:t>c</w:t></w:r><w:del w:id='2' w:author='Dee'><w:r>"
        "<w:delText>d</w:delText></w:r></w:del></w:ins><w:moveFrom w:id='3' w:author='Cy' "
        "w:date='2026-01-01T00:00:00Z'><w:r><w:t>e</w:t></w:r></w:moveFrom></w:p>");
    const auto fromWord = parseDocx(opc::write(word));
    for (const auto &warning : fromWord.warnings)
        EXPECT_EQ(warning.find("omitted"), std::string::npos) << warning;
    ASSERT_EQ(fromWord.document.paragraphs.size(), 2u);
    EXPECT_EQ(fromWord.document.paragraphs[0].text(), u"ab");
    EXPECT_EQ(fromWord.document.paragraphs[1].text(), u"cde");
    const auto &second = fromWord.document.paragraphs[1].runs;
    ASSERT_EQ(second.size(), 3u);
    EXPECT_TRUE(second[0].inserted && !second[0].deleted);
    EXPECT_TRUE(second[1].inserted && second[1].deleted);
    EXPECT_TRUE(!second[2].inserted && second[2].deleted);
    EXPECT_EQ(second[0].inserted, second[2].deleted); // same author and date
    EXPECT_EQ(fromWord.document.revisions.size(), 2u);
    EXPECT_FALSE(fromWord.document.trackChanges);
    EXPECT_NO_THROW(validateDocument(fromWord.document));
}
TEST(Docx, TrackChangesSwitchFollowsKeptSettings) {
    const std::string w = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    const std::string rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
    auto parts =
        wordDocument("<w:p><w:r><w:t>x</w:t></w:r></w:p>",
                     "<Relationship Id='s' Type='" + rel + "settings' Target='settings.xml'/>");
    parts["word/settings.xml"] = "<w:settings xmlns:w='" + w +
                                 "'><w:zoom w:percent='100'/><w:trackRevisions/>"
                                 "<w:defaultTabStop w:val='720'/></w:settings>";
    auto &types = parts["[Content_Types].xml"];
    types.insert(types.rfind("</Types>"),
                 "<Override PartName='/word/settings.xml' ContentType='application/"
                 "vnd.openxmlformats-officedocument.wordprocessingml.settings+xml'/>");
    auto document = parseDocx(opc::write(parts)).document;
    EXPECT_TRUE(document.trackChanges);
    // Unchanged: the kept bytes are written back as they were.
    EXPECT_EQ(opc::read(serializeDocx(document)).at("word/settings.xml"),
              keptPart(document, "word/settings.xml")->data);
    document.trackChanges = false;
    const auto off = opc::read(serializeDocx(document)).at("word/settings.xml");
    EXPECT_EQ(off.find("trackRevisions"), std::string::npos);
    EXPECT_NE(off.find("defaultTabStop"), std::string::npos);
    auto again = parseDocx(opc::write(opc::read(serializeDocx(document)))).document;
    EXPECT_FALSE(again.trackChanges);
    again.trackChanges = true;
    const auto on = opc::read(serializeDocx(again)).at("word/settings.xml");
    EXPECT_LT(on.find("zoom"), on.find("trackRevisions"));
    EXPECT_LT(on.find("trackRevisions"), on.find("defaultTabStop"));
}

TEST(Docx, TableBordersAndShadingBothWays) {
    Editor e;
    e.insertTable(2, 2);
    e.setTableBorder({1.5, 0xff336699});
    e.shadeCells(0xffffeecc);
    auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document body;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    const auto top = body.select_node("//w:tblBorders/w:top").node();
    EXPECT_STREQ(top.attribute("w:sz").value(), "12");
    EXPECT_STREQ(top.attribute("w:color").value(), "336699");
    EXPECT_EQ(body.select_nodes("//w:tcPr/w:shd").size(), 1u);
    EXPECT_STREQ(body.select_node("//w:tcPr/w:shd").node().attribute("w:fill").value(), "FFEECC");
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    ASSERT_EQ(imported.document.tables.size(), 1u);
    EXPECT_EQ(imported.document.tables[0].border, (TableBorder{1.5, 0xff336699}));
    EXPECT_EQ(imported.document.tables[0].shading, e.document.tables[0].shading);

    // No borders round-trips as "nil".
    e.setTableBorder({0, 0xff000000});
    const auto none = parseDocx(serializeDocx(e.document));
    EXPECT_EQ(none.document.tables[0].border.width, 0);

    // Word: a merged cell shaded on every row, dashed borders and a cell's own borders.
    const auto word = wordDocument(
        "<w:tbl><w:tblPr><w:tblBorders><w:top w:val='dashed' w:sz='8' w:color='auto'/>"
        "<w:insideH w:val='dashed' w:sz='8'/></w:tblBorders></w:tblPr>"
        "<w:tr><w:tc><w:tcPr><w:vMerge w:val='restart'/><w:shd w:val='clear' w:fill='00FF00'/>"
        "</w:tcPr><w:p><w:r><w:t>a</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:tcPr><w:tcBorders><w:top "
        "w:val='single'/></w:tcBorders></w:tcPr><w:p/></w:tc></w:tr>"
        "<w:tr><w:tc><w:tcPr><w:vMerge/><w:shd w:val='clear' w:fill='00FF00'/></w:tcPr><w:p/>"
        "</w:tc><w:tc><w:tcPr><w:shd w:val='clear' w:fill='auto'/></w:tcPr><w:p/></w:tc></w:tr>"
        "</w:tbl><w:p/>");
    const auto fromWord = parseDocx(opc::write(word));
    ASSERT_EQ(fromWord.document.tables.size(), 1u);
    const auto &table = fromWord.document.tables[0];
    EXPECT_EQ(table.border, (TableBorder{1, 0xff000000}));
    ASSERT_EQ(table.shading.size(), 1u);
    EXPECT_EQ(table.shading[0], (CellShading{0, 0, 0xff00ff00}));
    auto warned = [&](const char *text) {
        return std::any_of(
            fromWord.warnings.begin(), fromWord.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("single lines"));
    EXPECT_TRUE(warned("single cells"));
    EXPECT_FALSE(warned("shading"));
}

TEST(Docx, WordPageAndSectionBreaksStartNewPages) {
    const auto word = wordDocument(
        "<w:p><w:r><w:t>one</w:t></w:r></w:p>"
        "<w:p><w:r><w:br w:type='page'/></w:r></w:p>"
        "<w:p><w:r><w:t>two</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>a</w:t><w:br w:type='page'/><w:t>b</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:sectPr><w:type w:val='nextPage'/></w:sectPr></w:pPr><w:r><w:t>end of "
        "section</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>next section</w:t></w:r></w:p>");
    const auto imported = parseDocx(opc::write(word));
    std::vector<std::pair<std::u16string, bool>> got;
    for (const auto &paragraph : imported.document.paragraphs)
        got.emplace_back(paragraph.text(), paragraph.pageBreakBefore);
    const std::vector<std::pair<std::u16string, bool>> expected{
        // As in Word, the paragraph holding only the break starts the new page.
        {u"one", false},         {u"", true},  {u"two", false},
        {u"a", false},           {u"b", true}, {u"end of section", false},
        {u"next section", false}};
    EXPECT_EQ(got, expected);
    // The section ends at its paragraph; the next one starts on a new page.
    const auto &end = imported.document.paragraphs[5].sectionBreak;
    ASSERT_TRUE(end);
    EXPECT_FALSE(end->continuous);
    for (const auto &warning : imported.warnings)
        EXPECT_EQ(warning.find("Inline breaks"), std::string::npos) << warning;
}

TEST(Docx, ThemeFontsAndAutomaticColourSurvive) {
    const std::string rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
    auto word = wordDocument(
        "<w:p><w:r><w:rPr><w:rFonts w:asciiTheme='minorHAnsi' w:hAnsiTheme='minorHAnsi'/>"
        "<w:color w:val='auto'/></w:rPr><w:t>body</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:rFonts "
        "w:asciiTheme='majorHAnsi'/></w:rPr><w:t>heading</w:t></w:r></w:p>",
        "<Relationship Id='t' Type='" + rel + "theme' Target='theme/theme1.xml'/>");
    word["word/theme/theme1.xml"] =
        "<a:theme xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' name='T'>"
        "<a:themeElements><a:fontScheme name='F'><a:majorFont><a:latin typeface='Aptos Display'/>"
        "</a:majorFont><a:minorFont><a:latin typeface='Aptos'/></a:minorFont></a:fontScheme>"
        "</a:themeElements></a:theme>";
    auto &types = word["[Content_Types].xml"];
    types.insert(types.rfind("</Types>"),
                 "<Override PartName='/word/theme/theme1.xml' ContentType='application/"
                 "vnd.openxmlformats-officedocument.theme+xml'/>");
    const auto imported = parseDocx(opc::write(word));
    ASSERT_GE(imported.document.paragraphs.size(), 2u);
    EXPECT_EQ(imported.document.paragraphs[0].runs[0].style.family, "Aptos");
    EXPECT_EQ(imported.document.paragraphs[1].runs[0].style.family, "Aptos Display");
    EXPECT_EQ(imported.document.paragraphs[0].runs[0].style.color, CharacterStyle{}.color);
    for (const auto &warning : imported.warnings)
        EXPECT_EQ(warning.find("Theme fonts"), std::string::npos) << warning;
    // The default ink goes back out as Word's automatic colour.
    const auto parts = opc::read(serializeDocx(imported.document));
    EXPECT_NE(parts.at("word/document.xml").find("w:color w:val=\"auto\""), std::string::npos);
}

TEST(Docx, IndentsAndJustificationBothWays) {
    const auto word = wordDocument(
        "<w:p><w:pPr><w:ind w:left='720' w:hanging='360' w:right='1440'/><w:jc w:val='both'/>"
        "</w:pPr><w:r><w:t>hanging</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:ind w:start='0' w:firstLine='480' w:end='240'/><w:jc w:val='distribute'/>"
        "</w:pPr><w:r><w:t>first</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:ind w:left='-200'/></w:pPr><w:r><w:t>margin</w:t></w:r></w:p>");
    const auto imported = parseDocx(opc::write(word));
    const auto &p = imported.document.paragraphs;
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].indentLeft, 36);
    EXPECT_EQ(p[0].indentFirstLine, -18);
    EXPECT_EQ(p[0].indentRight, 72);
    EXPECT_EQ(p[0].alignment, Alignment::Justify);
    EXPECT_EQ(p[1].indentFirstLine, 24);
    EXPECT_EQ(p[1].indentRight, 12);
    EXPECT_EQ(p[1].alignment, Alignment::Justify);
    EXPECT_EQ(p[2].indentLeft, 0);
    auto warned = [&](const char *text) {
        return std::any_of(imported.warnings.begin(), imported.warnings.end(),
                           [&](const auto &w) { return w.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("Distributed"));
    EXPECT_TRUE(warned("page margin"));
    EXPECT_FALSE(warned("not preserved"));
    const auto back = parseDocx(serializeDocx(imported.document));
    for (std::size_t n = 0; n < 3; ++n) {
        EXPECT_EQ(back.document.paragraphs[n].indentLeft, p[n].indentLeft) << n;
        EXPECT_EQ(back.document.paragraphs[n].indentFirstLine, p[n].indentFirstLine) << n;
        EXPECT_EQ(back.document.paragraphs[n].indentRight, p[n].indentRight) << n;
        EXPECT_EQ(back.document.paragraphs[n].alignment, p[n].alignment) << n;
    }
    const auto xml = opc::read(serializeDocx(imported.document)).at("word/document.xml");
    EXPECT_NE(xml.find("w:hanging=\"360\""), std::string::npos);
    EXPECT_NE(xml.find("w:val=\"both\""), std::string::npos);
}

TEST(Docx, WordListIndentsComeFromTheirLevel) {
    auto parts = wordDocument(
        "<w:p><w:pPr><w:numPr><w:ilvl w:val='0'/><w:numId w:val='9'/></w:numPr></w:pPr>"
        "<w:r><w:t>one</w:t></w:r></w:p>"
        "<w:p><w:pPr><w:numPr><w:ilvl w:val='1'/><w:numId w:val='9'/></w:numPr></w:pPr>"
        "<w:r><w:t>nested</w:t></w:r></w:p>");
    parts["word/numbering.xml"] =
        "<w:numbering xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
        "<w:abstractNum w:abstractNumId='0'>"
        "<w:lvl w:ilvl='0'><w:start w:val='1'/><w:numFmt w:val='bullet'/><w:lvlText w:val='•'/>"
        "<w:pPr><w:ind w:left='720' w:hanging='360'/></w:pPr></w:lvl>"
        "<w:lvl w:ilvl='1'><w:start w:val='1'/><w:numFmt w:val='bullet'/><w:lvlText w:val='o'/>"
        "<w:pPr><w:ind w:left='1440' w:hanging='360'/></w:pPr></w:lvl></w:abstractNum>"
        "<w:num w:numId='9'><w:abstractNumId w:val='0'/></w:num></w:numbering>";
    const auto imported = parseDocx(opc::write(parts));
    const auto &p = imported.document.paragraphs;
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(p[0].list, ListKind::Bullet);
    EXPECT_EQ(p[0].indentLeft, 18); // bullet at 18 pt, text at 36 pt, as in Word
    EXPECT_EQ(p[0].indentFirstLine, 0);
    EXPECT_EQ(p[1].indentLeft, 54); // the nested level shows indented
    for (const auto &w : imported.warnings)
        EXPECT_EQ(w.find("approximated"), std::string::npos) << w;
    const auto xml = opc::read(serializeDocx(imported.document)).at("word/document.xml");
    EXPECT_NE(xml.find("w:left=\"720\" w:hanging=\"360\""), std::string::npos);
    EXPECT_NE(xml.find("w:left=\"1440\" w:hanging=\"360\""), std::string::npos);
}

TEST(Docx, StrictOpenXmlLengthsWithUnits) {
    // Word's "Strict Open XML" writes universal measures instead of twips.
    const auto word = wordDocument(
        "<w:p><w:pPr><w:spacing w:before='6pt' w:after='0pt' w:line='13.8pt' w:lineRule='auto'/>"
        "<w:ind w:start='0.5in' w:hanging='18pt' w:end='1.27cm'/></w:pPr>"
        "<w:r><w:rPr><w:sz w:val='14pt'/></w:rPr><w:t>strict</w:t></w:r></w:p>"
        "<w:sectPr><w:pgSz w:w='595.3pt' w:h='841.9pt'/></w:sectPr>");
    const auto imported = parseDocx(opc::write(word));
    const auto &p = imported.document.paragraphs[0];
    EXPECT_EQ(p.spaceBefore, 6);
    EXPECT_NEAR(p.lineSpacing, 1.15, 0.001); // 240ths of a line, written as 13.8pt
    EXPECT_EQ(p.indentLeft, 36);
    EXPECT_EQ(p.indentFirstLine, -18);
    EXPECT_NEAR(p.indentRight, 36, 0.01);
    EXPECT_EQ(p.runs[0].style.size, 14);
    EXPECT_NEAR(imported.document.page.width, 595.3, 0.01);
    EXPECT_THROW(parseDocx(opc::write(
                     wordDocument("<w:p><w:pPr><w:ind w:start='3furlongs'/></w:pPr></w:p>"))),
                 std::runtime_error);
}
TEST(Docx, WordHeadingStylesAndThemeColoursSurviveARoundTrip) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/styles.xml"] =
        "<w:styles xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'>"
        "<w:style w:type='paragraph' w:default='1' w:styleId='Normal'><w:name w:val='Normal'/>"
        "<w:rPr><w:rFonts w:ascii='Aptos'/><w:sz w:val='24'/></w:rPr></w:style>"
        "<w:style w:type='paragraph' w:styleId='Heading1'><w:name w:val='heading 1'/>"
        "<w:basedOn w:val='Normal'/><w:pPr><w:spacing w:before='360' w:after='80'/></w:pPr>"
        "<w:rPr><w:rFonts w:ascii='Aptos Display'/><w:color w:val='0F4761' "
        "w:themeColor='accent1' w:themeShade='BF'/><w:sz w:val='40'/></w:rPr></w:style>"
        "</w:styles>";
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body>"
        "<w:p><w:pPr><w:pStyle w:val='Heading1'/></w:pPr><w:r><w:t>Heading</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:color w:val='E97132' w:themeColor='accent2'/></w:rPr>"
        "<w:t>body</w:t></w:r></w:p></w:body></w:document>";
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(std::none_of(imported.warnings.begin(), imported.warnings.end(),
                             [](const auto &w) { return w.find("Theme colors") != w.npos; }));
    const auto &document = imported.document;
    // Word's definitions replace Wordforge's built-in ones.
    const auto *heading = document.styles.find("Heading1");
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->character.family, "Aptos Display");
    EXPECT_EQ(heading->character.size, 20);
    // Wordforge's Heading 1 is bold; Word's is not.
    EXPECT_FALSE(resolveStyle(document.styles, "Heading1").character.bold);
    ASSERT_TRUE(heading->character.theme);
    EXPECT_EQ(heading->character.theme->index, themeColorIndex("accent1"));
    EXPECT_EQ(heading->character.theme->shade, 0xbf);
    EXPECT_EQ(heading->paragraph.spaceBefore, 18);
    EXPECT_EQ(document.styles.find("Normal")->character.family, "Aptos");
    const auto &headingRun = document.paragraphs[0].runs.front();
    EXPECT_EQ(headingRun.style.color, 0xff0f4761u);
    EXPECT_EQ(headingRun.direct, 0);
    const auto &bodyRun = document.paragraphs[1].runs.front();
    EXPECT_EQ(bodyRun.style.theme.index, themeColorIndex("accent2"));
    EXPECT_EQ(bodyRun.style.theme.shade, -1);

    // Saved again: the style and the run name their theme colours.
    const auto saved = opc::read(serializeDocx(document));
    const auto &styles = saved.at("word/styles.xml");
    EXPECT_NE(styles.find("w:themeColor=\"accent1\" w:themeShade=\"BF\""), std::string::npos);
    EXPECT_NE(styles.find("Aptos Display"), std::string::npos);
    EXPECT_NE(saved.at("word/document.xml").find("w:themeColor=\"accent2\""), std::string::npos);
    const auto again = parseDocx(serializeDocx(document)).document;
    EXPECT_EQ(again.paragraphs[1].runs.front().style.theme, bodyRun.style.theme);
    EXPECT_EQ(*again.styles.find("Heading1"), *heading);

    // A colour changed in Wordforge drops the theme reference.
    auto changed = document;
    changed.paragraphs.edit(1).runs.front().style.color = 0xff00aa00;
    changed.paragraphs.edit(1).runs.front().direct |= ColorField;
    const auto plain = opc::read(serializeDocx(changed)).at("word/document.xml");
    EXPECT_EQ(plain.find("accent2"), std::string::npos);
    EXPECT_NE(plain.find("00AA00"), std::string::npos);
}
TEST(Docx, HyperlinksBothWays) {
    auto parts = opc::read(serializeDocx(Document{}));
    parts["word/_rels/document.xml.rels"] =
        "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
        "<Relationship Id='rId9' Type='http://schemas.openxmlformats.org/officeDocument/2006/"
        "relationships/hyperlink' Target='https://example.com/page?a=1&amp;b=2' "
        "TargetMode='External'/><Relationship Id='rId8' Type='http://schemas.openxmlformats.org/"
        "officeDocument/2006/relationships/hyperlink' Target='javascript:alert(1)' "
        "TargetMode='External'/><Relationship Id='styles' Type='http://schemas.openxmlformats.org/"
        "officeDocument/2006/relationships/styles' Target='styles.xml'/></Relationships>";
    parts["word/document.xml"] =
        "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main' "
        "xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'><w:body>"
        "<w:p><w:r><w:t xml:space='preserve'>Go to </w:t></w:r><w:hyperlink r:id='rId9'>"
        "<w:r><w:t>the</w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t xml:space='preserve'> page"
        "</w:t></w:r></w:hyperlink><w:r><w:t xml:space='preserve'> or </w:t></w:r>"
        "<w:hyperlink w:anchor='Intro'><w:r><w:t>intro</w:t></w:r></w:hyperlink>"
        "<w:hyperlink r:id='rId8'><w:r><w:t>bad</w:t></w:r></w:hyperlink></w:p>"
        "</w:body></w:document>";
    const auto imported = parseDocx(opc::write(parts));
    const auto &runs = imported.document.paragraphs[0].runs;
    std::map<std::u16string, std::string> links;
    for (const auto &run : runs)
        links[run.text()] = run.link;
    EXPECT_EQ(links[u"Go to "], "");
    EXPECT_EQ(links[u"the"], "https://example.com/page?a=1&b=2");
    EXPECT_EQ(links[u" page"], "https://example.com/page?a=1&b=2");
    EXPECT_EQ(links[u"intro"], "#Intro");
    EXPECT_EQ(links[u"bad"], ""); // unsafe targets keep their text only
    EXPECT_TRUE(std::any_of(imported.warnings.begin(), imported.warnings.end(),
                            [](const auto &w) { return w.find("link targets") != w.npos; }));
    // Saved: one w:hyperlink holding both runs of the first link, an External relationship.
    const auto saved = opc::read(serializeDocx(imported.document));
    const auto &document = saved.at("word/document.xml");
    const auto first = document.find("<w:hyperlink r:id=\"link1\"");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(document.find("<w:hyperlink r:id=", first + 1), std::string::npos);
    EXPECT_NE(document.find("w:anchor=\"Intro\""), std::string::npos);
    const auto &relationships = saved.at("word/_rels/document.xml.rels");
    EXPECT_NE(relationships.find("Target=\"https://example.com/page?a=1&amp;b=2\" "
                                 "TargetMode=\"External\""),
              std::string::npos);
    const auto again = parseDocx(serializeDocx(imported.document)).document;
    EXPECT_EQ(again.paragraphs[0].runs, imported.document.paragraphs[0].runs);
}
TEST(Docx, EndnotesRoundTripAndImportFromWord) {
    Editor e;
    e.replace(u"Claim");
    e.insertFootnote(u"A footnote.");
    e.replace(u" and more");
    e.insertFootnote(u"First endnote & source.", true);
    e.insertFootnote(u"Second endnote.", true);
    const auto parts = opc::read(serializeDocx(e.document));
    ASSERT_TRUE(parts.contains("word/endnotes.xml"));
    pugi::xml_document notes;
    ASSERT_TRUE(notes.load_string(parts.at("word/endnotes.xml").c_str()));
    EXPECT_EQ(notes.select_nodes("//w:endnote").size(), 4u); // two separators + two notes
    EXPECT_EQ(notes.select_nodes("//w:endnoteRef").size(), 2u);
    EXPECT_NE(parts.at("word/_rels/document.xml.rels").find("relationships/endnotes"),
              std::string::npos);
    EXPECT_NE(parts.at("[Content_Types].xml").find("endnotes+xml"), std::string::npos);
    pugi::xml_document body;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:endnoteReference").size(), 2u);
    EXPECT_EQ(body.select_nodes("//w:footnoteReference").size(), 1u);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty());
    std::vector<std::pair<bool, std::u16string>> found;
    for (const auto &run : imported.document.paragraphs[0].runs)
        if (const auto *note = run.note())
            found.emplace_back(note->endnote, imported.document.footnote(note->id)->text);
    const std::vector<std::pair<bool, std::u16string>> expected{
        {false, u"A footnote."}, {true, u"First endnote & source."}, {true, u"Second endnote."}};
    EXPECT_EQ(found, expected);
}
TEST(Docx, FieldsAndTablesOfContentsBothWays) {
    auto parts = opc::read(serializeDocx(Document{}));
    auto r = [](const std::string &inner) { return "<w:r>" + inner + "</w:r>"; };
    auto fld = [&](const char *type) {
        return r(std::string("<w:fldChar w:fldCharType='") + type + "'/>");
    };
    auto instr = [&](const char *text) {
        return r(std::string("<w:instrText xml:space='preserve'>") + text + "</w:instrText>");
    };
    auto entry = [&](const char *anchor, const char *text, const char *page) {
        return std::string("<w:p><w:hyperlink w:anchor='") + anchor + "'>" +
               r(std::string("<w:t>") + text + "</w:t>") + r("<w:tab/>") + fld("begin") +
               instr(" PAGEREF _Toc1 \\h ") + fld("separate") +
               r(std::string("<w:t>") + page + "</w:t>") + fld("end") + "</w:hyperlink>";
    };
    parts["word/document.xml"] =
        "<w:document "
        "xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body>" +
        std::string("<w:p>") + fld("begin") + instr(" TOC \\o \"1-3\" \\h \\z \\u ") +
        fld("separate") + entry("_Toc1", "One", "1").substr(5) + "</w:p>" +
        entry("_Toc2", "Two", "2") + fld("end") + "</w:p>" +
        "<w:p><w:r><w:t xml:space='preserve'>Page </w:t></w:r><w:fldSimple w:instr=' PAGE '>"
        "<w:r><w:t>4</w:t></w:r></w:fldSimple></w:p></w:body></w:document>";
    const auto imported = parseDocx(opc::write(parts));
    const auto &document = imported.document;
    EXPECT_TRUE(std::none_of(imported.warnings.begin(), imported.warnings.end(), [](const auto &w) {
        return w.find("fldChar") != w.npos || w.find("instrText") != w.npos;
    }));
    ASSERT_EQ(document.fields.size(), 4u);
    std::vector<std::string> instructions;
    for (const auto &field : document.fields)
        instructions.push_back(field.instruction);
    EXPECT_EQ(instructions[0], "TOC \\o \"1-3\" \\h \\z \\u");
    EXPECT_EQ(instructions[1], "PAGEREF _Toc1 \\h");
    EXPECT_EQ(instructions[3], "PAGE");
    EXPECT_EQ(document.paragraphs[0].text(), u"One\t1");
    EXPECT_EQ(document.paragraphs[1].text(), u"Two\t2");
    const auto toc = document.resolve(document.fields[0].end);
    ASSERT_TRUE(toc);
    EXPECT_EQ(toc->paragraph, 1u);
    // Saved and read again: the same fields over the same text, links intact.
    const auto saved = serializeDocx(document);
    const auto xml = opc::read(saved).at("word/document.xml");
    EXPECT_NE(xml.find("TOC \\o"), std::string::npos);
    const auto again = parseDocx(saved).document;
    ASSERT_EQ(again.fields.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(again.fields[i].instruction, document.fields[i].instruction);
        EXPECT_EQ(again.resolve(again.fields[i].start), document.resolve(document.fields[i].start));
        EXPECT_EQ(again.resolve(again.fields[i].end), document.resolve(document.fields[i].end));
    }
    EXPECT_EQ(again.paragraphs[0].runs.back().link, "#_Toc1");
    EXPECT_NO_THROW(validateDocument(again));
}
TEST(Docx, SectionsWithOrientationAndColumnsBothWays) {
    Editor e;
    e.replace(u"Portrait");
    e.insertSectionBreak(false);
    e.selection = {{1, 0}, {1, 0}};
    e.replace(u"Landscape in two columns");
    auto page = e.sectionPage();
    std::swap(page.width, page.height);
    page.columns = 2;
    page.columnGap = 18;
    e.setPage(page);
    e.insertSectionBreak(true);
    e.selection = {{2, 0}, {2, 0}};
    e.replace(u"Continuous, one column");
    auto last = e.sectionPage(); // landscape, from the section it was split from
    last.columns = 1;
    e.setPage(last);
    const auto parts = opc::read(serializeDocx(e.document));
    pugi::xml_document body;
    ASSERT_TRUE(body.load_string(parts.at("word/document.xml").c_str()));
    EXPECT_EQ(body.select_nodes("//w:p/w:pPr/w:sectPr").size(), 2u);
    EXPECT_EQ(body.select_nodes("//w:sectPr/w:cols[@w:num='2']").size(), 1u);
    EXPECT_EQ(body.select_nodes("//w:sectPr/w:pgSz[@w:orient='landscape']").size(), 2u);
    // The third section starts continuously: its own (body) sectPr says so.
    EXPECT_EQ(body.select_nodes("/w:document/w:body/w:sectPr/w:type[@w:val='continuous']").size(),
              1u);
    const auto imported = parseDocx(opc::write(parts));
    const auto &paragraphs = imported.document.paragraphs;
    ASSERT_EQ(paragraphs.size(), 3u);
    ASSERT_TRUE(paragraphs[0].sectionBreak && paragraphs[1].sectionBreak);
    EXPECT_FALSE(paragraphs[0].sectionBreak->continuous);
    EXPECT_TRUE(paragraphs[1].sectionBreak->continuous);
    EXPECT_EQ(paragraphs[1].sectionBreak->page.columns, 2u);
    EXPECT_EQ(paragraphs[1].sectionBreak->page.columnGap, 18);
    EXPECT_GT(paragraphs[1].sectionBreak->page.width, paragraphs[1].sectionBreak->page.height);
    EXPECT_LT(paragraphs[0].sectionBreak->page.width, paragraphs[0].sectionBreak->page.height);
    EXPECT_FALSE(paragraphs[2].sectionBreak);
    EXPECT_GT(imported.document.page.width, imported.document.page.height);
    for (const auto &warning : imported.warnings)
        EXPECT_EQ(warning.find("Section"), std::string::npos) << warning;
}
TEST(Docx, AppliesWordTableStylesToCellShadingTextAndSpacing) {
    // A style like Word's "Grid Table 4": blue header row with white bold text, banded
    // rows, grey borders and no space after cell paragraphs; based on a style that sets
    // the space after.
    const std::string styles =
        "<w:style w:type='table' w:styleId='Base'><w:name w:val='Base'/>"
        "<w:pPr><w:spacing w:after='120'/></w:pPr></w:style>"
        "<w:style w:type='table' w:styleId='GridTable4'><w:name w:val='Grid Table 4'/>"
        "<w:basedOn w:val='Base'/><w:pPr><w:spacing w:after='0' w:line='240' "
        "w:lineRule='auto'/></w:pPr>"
        "<w:tblPr><w:tblStyleRowBandSize w:val='1'/><w:tblBorders>"
        "<w:top w:val='single' w:sz='4' w:color='8EAADB'/><w:bottom w:val='single' w:sz='4' "
        "w:color='8EAADB'/><w:insideH w:val='single' w:sz='4' w:color='8EAADB'/></w:tblBorders>"
        "</w:tblPr>"
        "<w:tblStylePr w:type='firstRow'><w:rPr><w:b/><w:color w:val='FFFFFF'/></w:rPr>"
        "<w:tcPr><w:shd w:val='clear' w:color='auto' w:fill='4472C4'/></w:tcPr></w:tblStylePr>"
        "<w:tblStylePr w:type='band1Horz'><w:tcPr><w:shd w:val='clear' w:color='auto' "
        "w:fill='D9E2F3'/></w:tcPr></w:tblStylePr>"
        "<w:tblStylePr w:type='firstCol'><w:rPr><w:i/></w:rPr></w:tblStylePr></w:style>"
        "<w:style w:type='table' w:default='1' w:styleId='TableNormal'>"
        "<w:name w:val='Normal Table'/></w:style>";
    auto row = [](const char *a, const char *b) {
        return std::string("<w:tr>") + cell(a) + cell(b) + "</w:tr>";
    };
    const auto body =
        "<w:tbl><w:tblPr><w:tblStyle w:val='GridTable4'/><w:tblLook w:firstRow='1' "
        "w:lastRow='0' w:firstColumn='0' w:lastColumn='0' w:noHBand='0' w:noVBand='1'/>"
        "</w:tblPr><w:tblGrid><w:gridCol w:w='3000'/><w:gridCol w:w='3000'/></w:tblGrid>" +
        row("Site", "Value") + row("North", "8") + row("Mill", "7") +
        "<w:tr>" + cell("Delta", "<w:tcPr><w:shd w:val='clear' w:fill='FFEEDD'/></w:tcPr>") +
        cell("6") + "</w:tr></w:tbl>"
        "<w:p/><w:tbl><w:tblPr><w:tblLook w:val='04A0'/></w:tblPr><w:tr>" + cell("plain") +
        "</w:tr></w:tbl>";
    auto parts = wordDocument(body);
    auto &styleXml = parts["word/styles.xml"];
    styleXml.insert(styleXml.rfind("</w:styles>"), styles);
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_TRUE(imported.warnings.empty()) << imported.warnings.front();
    const auto &document = imported.document;
    ASSERT_EQ(document.tables.size(), 2u);
    const auto &table = document.tables[0];
    EXPECT_EQ(table.fillAt(0, 0), 0xff4472c4u); // header row
    EXPECT_EQ(table.fillAt(0, 1), 0xff4472c4u);
    EXPECT_EQ(table.fillAt(1, 0), 0xffd9e2f3u); // first band
    EXPECT_EQ(table.fillAt(2, 1), 0u);          // second band
    EXPECT_EQ(table.fillAt(3, 0), 0xffffeeddu); // the cell's own shading wins
    EXPECT_EQ(table.fillAt(3, 1), 0xffd9e2f3u);
    EXPECT_DOUBLE_EQ(table.border.width, 0.5);
    EXPECT_EQ(table.border.color, 0xff8eaadbu);
    // The default table style has no borders, so neither does a table without a style.
    EXPECT_DOUBLE_EQ(document.tables[1].border.width, 0);
    auto at = [&](std::size_t row, std::size_t column) -> const Paragraph & {
        for (const auto &p : document.paragraphs)
            if (p.cell && p.cell->tableId == table.id && p.cell->row == row &&
                p.cell->column == column)
                return p;
        throw std::runtime_error("missing cell");
    };
    EXPECT_TRUE(at(0, 0).runs[0].style.bold);
    EXPECT_EQ(at(0, 0).runs[0].style.color, 0xffffffffu);
    EXPECT_FALSE(at(1, 0).runs[0].style.bold);
    EXPECT_FALSE(at(1, 0).runs[0].style.italic); // firstColumn is off in tblLook
    EXPECT_DOUBLE_EQ(at(1, 0).spaceAfter, 0);
    // Saved as direct formatting, it survives a round trip.
    const auto again = parseDocx(serializeDocx(document)).document;
    EXPECT_EQ(again.tables[0].fillAt(0, 0), 0xff4472c4u);
    EXPECT_EQ(again.tables[0].fillAt(1, 1), 0xffd9e2f3u);
    for (const auto &p : again.paragraphs)
        if (p.cell && p.cell->tableId == again.tables[0].id && p.cell->row == 0) {
            EXPECT_TRUE(p.runs[0].style.bold);
            EXPECT_EQ(p.runs[0].style.color, 0xffffffffu);
        }
}
