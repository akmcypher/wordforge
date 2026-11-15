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
    EXPECT_TRUE(warned("Merged table cells"));
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
