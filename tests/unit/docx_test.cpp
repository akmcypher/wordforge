#include "core/formats/docx.hpp"
#include "core/formats/zip_package.hpp"
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
    const auto imported = parseDocx(opc::write(parts));
    EXPECT_FALSE(imported.warnings.empty());
    EXPECT_EQ(imported.document.text(), u"before\nafter");
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
    EXPECT_FALSE(imported.warnings.empty());
    EXPECT_EQ(imported.document.text(), u"beforeafter");
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
