#include "core/formats/docx.hpp"
#include "core/formats/zip_package.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <pugixml.hpp>
using namespace wf;
TEST(Docx, ExportsRealRectangularTablesWithMultipleParagraphsAndImages) {
    Document document;
    document.page = {612, 792, 36};
    document.tables = {{10, 2, 2}};
    document.paragraphs = {Paragraph{1}, Paragraph{2}, Paragraph{3}, Paragraph{4},
                           Paragraph{5}, Paragraph{6}, Paragraph{7}};
    document.paragraphs[0].runs = {{u"before", {}}};
    for (std::size_t index = 1; index <= 5; ++index) {
        const auto cell = index < 3 ? 0 : index - 2;
        document.paragraphs[index].cell = TableCell{10, cell / 2, cell % 2};
        document.paragraphs[index].runs = {{u"cell", {}}};
    }
    document.paragraphs[1].runs[0].style.bold = true;
    document.paragraphs[2].runs = {{u"second paragraph", {}}};
    document.paragraphs[6].runs = {{u"after", {}}};
    const auto path =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png";
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string png((std::istreambuf_iterator<char>(file)), {});
    document.images.emplace(11, std::make_shared<const ImageResource>(ImageResource{4, 2, png}));
    document.paragraphs[2].runs.push_back(Run::fromImage({11, 72, 36, "cell picture"}));
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
        EXPECT_EQ(back.paragraphs[index].cell.has_value(), document.paragraphs[index].cell.has_value());
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
    editor.document.paragraphs[1].pageBreakBefore = true;
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
    document.paragraphs[0].runs = {{u"before", {}},
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
    document.paragraphs[0].runs = {Run::fromImage({2, 72, 36, "live"})};
    auto parts = opc::read(serializeDocx(document));
    EXPECT_TRUE(parts.contains("word/media/image2.png"));
    EXPECT_FALSE(parts.contains("word/media/image3.png"));
    EXPECT_EQ(parts.at("word/_rels/document.xml.rels").find("image3"), std::string::npos);
    document.paragraphs[0].runs = {{u"deleted picture", {}}};
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
    EXPECT_NE(parts.at("word/document.xml").find("w:pStyle w:val=\"Pullquote\""), std::string::npos);

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
        const auto a = resolveStyle(document.styles, id), b = resolveStyle(editor.document.styles, id);
        EXPECT_EQ(a.character, b.character) << id;
        EXPECT_EQ(static_cast<const ParagraphFormat &>(a.paragraph),
                  static_cast<const ParagraphFormat &>(b.paragraph)) << id;
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
        "<w:document xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'><w:body>"
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
    // Word's Normal is 11 pt, so the imported styles carry 11 pt themselves.
    EXPECT_EQ(quote->character.size, 11);
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
    EXPECT_NE(parts.at("word/document.xml").find("w:rStyle w:val=\"Inlinecode\""), std::string::npos);
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
           "<pic:blipFill><a:blip r:embed='" + id + "'/></pic:blipFill></pic:pic></a:graphicData>"
           "</a:graphic></wp:" + kind + "></w:drawing></w:r>";
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
        "<w:tblGrid><w:gridCol w:w='3000'/><w:gridCol w:w='3000'/><w:gridCol w:w='3000'/></w:tblGrid>"
        "<w:tr>" + cell("wide", "<w:tcPr><w:gridSpan w:val='2'/></w:tcPr>") + cell("c") + "</w:tr>"
        "<w:tr>" + cell("a") + cell("b") + "</w:tr>"  // short row: padded with an empty cell
        "<w:tr><w:tc><w:p><w:r><w:t>outer</w:t></w:r></w:p><w:tbl><w:tr>" + cell("inner") +
        "</w:tr></w:tbl><w:p/></w:tc>" + cell("x") + cell("y", "<w:tcPr><w:vMerge/></w:tcPr>") +
        "</w:tr></w:tbl>"
        "<w:tbl><w:tr>" + cell("second") + "</w:tr></w:tbl>";
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
            if (p.cell && p.cell->tableId == table && p.cell->row == row && p.cell->column == column)
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
        return std::any_of(imported.warnings.begin(), imported.warnings.end(),
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
    const auto body = "<w:p><w:r><w:t>see </w:t></w:r>" + drawing("inline", "rPng", 18288000, 9144000) +
                      "<w:r><w:t> and </w:t></w:r>" + drawing("anchor", "rJpg", 914400, 914400) +
                      "</w:p>";
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
        return std::any_of(result.warnings.begin(), result.warnings.end(),
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
    auto parts = wordDocument(
        "<w:p><w:r><w:t>body</w:t></w:r></w:p><w:sectPr>"
        "<w:headerReference w:type='default' r:id='rH'/>"
        "<w:headerReference w:type='even' r:id='rE'/>"
        "<w:footerReference w:type='default' r:id='rF'/>"
        "<w:pgSz w:w='12240' w:h='15840'/><w:pgMar w:top='1440' w:bottom='1440' "
        "w:left='1440' w:right='1440' w:header='720' w:footer='720'/></w:sectPr>",
        rels);
    const std::string ns =
        " xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'";
    // Header: "Title<tab><tab>Confidential" with a picture that is dropped.
    parts["word/header1.xml"] = "<w:hdr" + ns +
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
        return std::any_of(imported.warnings.begin(), imported.warnings.end(),
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
    EXPECT_STREQ(xml.select_node("//w:vMerge[@w:val='restart']/../w:gridSpan").node()
                     .attribute("w:val").value(), "2");
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
    std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / name,
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
    EXPECT_STREQ(rels.select_node("//*[contains(@Type,'core-properties')]").node()
                     .attribute("Target").value(),
                 "docProps/core.xml");
    EXPECT_STREQ(documentRels.select_node("//*[contains(@Type,'/theme')]").node()
                     .attribute("Target").value(),
                 "theme/theme1.xml");
    EXPECT_STREQ(documentRels.select_node("//*[contains(@Type,'/customXml')]").node()
                     .attribute("Target").value(),
                 "../customXml/item1.xml");
    EXPECT_STREQ(types.select_node("//*[@PartName='/customXml/itemProps1.xml']").node()
                     .attribute("ContentType").value(),
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
            const auto target = (std::filesystem::path(directory) / node.attribute("Target").value())
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
    auto parts = wordDocument(
        "<w:p><w:r><w:t>x</w:t></w:r></w:p>",
        "<Relationship Id='s' Type='" + rel + "settings' Target='settings.xml'/>"
        "<Relationship Id='t' Type='" + rel + "theme' Target='theme/theme1.xml'/>"
        "<Relationship Id='c' Type='" + rel + "comments' Target='comments.xml'/>"
        "<Relationship Id='f' Type='" + rel + "footnotes' Target='footnotes.xml'/>"
        "<Relationship Id='e' Type='" + rel + "endnotes' Target='endnotes.xml'/>");
    parts["word/endnotes.xml"] = "<w:endnotes xmlns:w='" + w +
                                 "'><w:endnote w:id='1'><w:p/></w:endnote></w:endnotes>";
    parts["word/settings.xml"] =
        "<w:settings xmlns:w='" + w + "'><w:zoom w:percent='100'/><w:evenAndOddHeaders/>"
        "<w:footnotePr><w:footnote w:id='-1'/></w:footnotePr><w:defaultTabStop w:val='720'/>"
        "</w:settings>";
    // The theme references an image that is missing: it is not kept.
    parts["word/theme/theme1.xml"] = "<a:theme xmlns:a='http://schemas.openxmlformats.org/"
                                     "drawingml/2006/main' name='T'/>";
    parts["word/theme/_rels/theme1.xml.rels"] =
        "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
        "<Relationship Id='i' Type='" + rel + "image' Target='../media/gone.png'/></Relationships>";
    parts["word/comments.xml"] = "<w:comments xmlns:w='" + w +
                                 "'><w:comment w:id='1'><w:p/></w:comment></w:comments>";
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
    EXPECT_NE(named.find("endnotes"), std::string::npos);
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
        return std::any_of(fromWord.warnings.begin(), fromWord.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("one paragraph of plain text"));
    EXPECT_TRUE(warned("endnoteReference"));
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
    EXPECT_STREQ(comments.select_node("//w:comment[@w:id='0']").node().attribute("w:initials").value(),
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
        return std::any_of(fromWord.warnings.begin(), fromWord.warnings.end(),
                           [&](const auto &warning) { return warning.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(warned("without a marked text range"));
    EXPECT_FALSE(warned("not kept when saving"));
    EXPECT_FALSE(warned("commentRange"));
    EXPECT_FALSE(warned("commentReference"));
}
