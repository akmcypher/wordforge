#include "core/document/document.hpp"
#include "core/formats/native.hpp"
#include <gtest/gtest.h>

using namespace wf;

namespace {
Editor headingsAndBody() {
    // 0: Heading1 "Chapter", 1: Heading2 "Section part" (" part" italic), 2: Normal "Body".
    Editor e;
    e.replace(u"Chapter\nSection part\nBody");
    e.selection = {{0, 0}, {0, 0}};
    e.applyPreset(ParagraphPreset::Heading1);
    e.selection = {{1, 0}, {1, 0}};
    e.applyPreset(ParagraphPreset::Heading2);
    e.selection = {{1, 7}, {1, 12}};
    e.format([](auto &s) { s.italic = true; });
    return e;
}
} // namespace

TEST(Styles, BuiltinsResolveThroughBasedOnChain) {
    const auto sheet = builtinStyles();
    const auto heading2 = resolveStyle(sheet, "Heading2");
    EXPECT_EQ(heading2.character.size, 16);        // its own
    EXPECT_TRUE(heading2.character.bold);           // from Heading1
    EXPECT_EQ(heading2.character.color, 0xff185abdu);
    EXPECT_EQ(heading2.paragraph.spaceBefore, 12);  // from Heading1
    EXPECT_EQ(heading2.character.family, "Arial");  // defaults
    EXPECT_NO_THROW(validateStyles(sheet));
}

TEST(Styles, CyclesDanglingBasesAndMissingBuiltinsAreRejected) {
    auto cycle = builtinStyles();
    cycle.find("Heading1")->basedOn = "Heading2";
    EXPECT_THROW(validateStyles(cycle), std::invalid_argument);
    auto dangling = builtinStyles();
    dangling.find("Title")->basedOn = "Missing";
    EXPECT_THROW(validateStyles(dangling), std::invalid_argument);
    auto missing = builtinStyles();
    missing.paragraphStyles.pop_back();
    EXPECT_THROW(validateStyles(missing), std::invalid_argument);
    auto self = builtinStyles();
    self.find("Title")->basedOn = "Title";
    EXPECT_THROW(resolveStyle(self, "Title"), std::invalid_argument);
}

TEST(Styles, ChangingABaseStyleUpdatesInheritorsButKeepsDirectFormatting) {
    auto e = headingsAndBody();
    // Restyle Heading1 by example: Georgia, not bold.
    e.selection = {{0, 0}, {0, 7}};
    e.format([](auto &s) {
        s.family = "Georgia";
        s.bold = false;
    });
    e.selection = {{0, 3}, {0, 3}};
    e.updateStyleFromSelection();
    EXPECT_NO_THROW(validateDocument(e.document));
    const auto &chapter = e.document.paragraphs[0].runs.front();
    EXPECT_EQ(chapter.style.family, "Georgia");
    EXPECT_EQ(chapter.direct, 0) << "the example paragraph now simply uses its style";
    // Heading2 is based on Heading1: its text follows, and its direct italic survives.
    const auto &section = e.document.paragraphs[1];
    for (const auto &run : section.runs) {
        EXPECT_EQ(run.style.family, "Georgia");
        EXPECT_FALSE(run.style.bold);
        EXPECT_EQ(run.style.size, 16);
    }
    EXPECT_TRUE(e.document.styleAt({1, 9}).italic);
    EXPECT_FALSE(e.document.styleAt({1, 2}).italic);
    // Normal is not based on Heading1.
    EXPECT_EQ(e.document.paragraphs[2].runs.front().style.family, "Arial");
    // One undo restores both the style and every paragraph.
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.styles, builtinStyles());
    EXPECT_TRUE(e.document.paragraphs[1].runs.front().style.bold);
}

TEST(Styles, DirectFormattingSurvivesUnrelatedStyleChanges) {
    auto e = headingsAndBody();
    e.selection = {{2, 0}, {2, 4}};
    e.format([](auto &s) { s.size = 30; }); // direct size on body text
    e.selection = {{2, 0}, {2, 0}};
    e.formatParagraph([](auto &p) { p.alignment = Alignment::Center; });
    // Redefine Normal from a paragraph that has no direct formatting except the size.
    e.selection = {{2, 2}, {2, 2}};
    e.updateStyleFromSelection();
    EXPECT_EQ(resolveStyle(e.document.styles, "Normal").paragraph.alignment, Alignment::Center);
    EXPECT_EQ(resolveStyle(e.document.styles, "Normal").character.size, 30);
    // Headings are based on Normal but set their own size, so only alignment flows to them.
    EXPECT_EQ(e.document.paragraphs[0].alignment, Alignment::Center);
    EXPECT_EQ(e.document.paragraphs[0].runs.front().style.size, 20);
}

TEST(Styles, WfgSixPersistsStylesAndDirectMarks) {
    auto e = headingsAndBody();
    e.selection = {{0, 0}, {0, 7}};
    e.format([](auto &s) { s.family = "Georgia"; });
    e.selection = {{0, 1}, {0, 1}};
    e.updateStyleFromSelection();
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(bytes.find("version=\"6\""), std::string::npos);
    auto reopened = parseNative(bytes);
    EXPECT_EQ(reopened.styles, e.document.styles);
    ASSERT_EQ(reopened.paragraphs.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(reopened.paragraphs[i].style, e.document.paragraphs[i].style);
        EXPECT_EQ(reopened.paragraphs[i].runs, e.document.paragraphs[i].runs);
    }
    // The distinction still works after reopening: change Heading1 again.
    Editor again;
    again.reset(reopened);
    again.selection = {{0, 0}, {0, 7}};
    again.format([](auto &s) { s.family = "Courier New"; });
    again.selection = {{0, 1}, {0, 1}};
    again.updateStyleFromSelection();
    EXPECT_EQ(again.document.paragraphs[1].runs.front().style.family, "Courier New");
    EXPECT_TRUE(again.document.styleAt({1, 9}).italic);
}

TEST(Styles, BuiltinOnlyDocumentsKeepOlderFileVersions) {
    auto e = headingsAndBody();
    const auto bytes = serializeNative(e.document);
    EXPECT_NE(bytes.find("version=\"2\""), std::string::npos);
    auto reopened = parseNative(bytes);
    EXPECT_EQ(reopened.paragraphs[1].style, "Heading2");
    // Italic differs from Heading2, so it comes back as direct formatting.
    EXPECT_EQ(reopened.paragraphs[1].runs.back().direct, ItalicField);
}

TEST(Styles, LegacyFilesMigrateFormattingAsDirectAgainstStyles) {
    const std::string legacy =
        R"(<wordforge version="2" nextId="3" width="595.28" height="841.89" margin="56.7">)"
        R"(<p id="1" align="0" after="6" preset="2" before="12" line="1">)"
        R"(<r family="Arial" size="20" bold="true" italic="false" underline="false" color="4279786173">Head</r>)"
        R"(<r family="Arial" size="20" bold="false" italic="false" underline="false" color="4279786173">ing</r></p>)"
        R"(<p id="2" align="1" after="8"><r family="Arial" size="12" bold="false" italic="false" underline="false" color="4280297009">x</r></p>)"
        R"(</wordforge>)";
    const auto document = parseNative(legacy);
    EXPECT_EQ(document.paragraphs[0].style, "Heading1");
    EXPECT_EQ(document.paragraphs[0].runs[0].direct, 0);
    EXPECT_EQ(document.paragraphs[0].runs[1].direct, BoldField);
    EXPECT_EQ(document.paragraphs[1].direct, AlignmentField);
}

TEST(Styles, TamperedWfgSixIsRejected) {
    auto e = headingsAndBody();
    e.selection = {{0, 0}, {0, 7}};
    e.format([](auto &s) { s.family = "Georgia"; });
    e.selection = {{0, 1}, {0, 1}};
    e.updateStyleFromSelection();
    auto bytes = serializeNative(e.document);
    // A value the style does not produce, without a direct mark, is not a valid document.
    const auto at = bytes.find("family=\"Georgia\"", bytes.find("<p "));
    ASSERT_NE(at, std::string::npos);
    bytes.replace(at, 16, "family=\"Verdana\"");
    EXPECT_THROW(parseNative(bytes), std::exception);
    auto cyclic = serializeNative(e.document);
    const auto base = cyclic.find("basedOn=\"Heading1\"");
    ASSERT_NE(base, std::string::npos);
    cyclic.replace(base, 18, "basedOn=\"Heading2\"");
    const auto heading1 = cyclic.find("id=\"Heading1\"");
    cyclic.insert(cyclic.find(' ', heading1 + 1), " basedOn=\"Heading2\"");
    EXPECT_THROW(parseNative(cyclic), std::exception);
}

TEST(Styles, TypingClearingAndJoiningKeepTheInvariant) {
    Editor e;
    e.replace(u"Heading\nbody");
    e.selection = {{0, 0}, {0, 0}};
    e.applyPreset(ParagraphPreset::Heading1);
    // Toggled bold while typing in body text is direct.
    e.selection = {{1, 4}, {1, 4}};
    e.typingStyle = e.document.styleAt(e.selection.focus); // as the view does on caret moves
    e.format([](auto &s) { s.bold = true; });
    e.replace(u" bold");
    EXPECT_EQ(e.document.paragraphs[1].runs.back().direct, BoldField);
    // Clear formatting returns the selection to its style.
    e.selection = {{1, 0}, {1, 9}};
    e.clearCharacterFormatting();
    for (const auto &run : e.document.paragraphs[1].runs) {
        EXPECT_EQ(run.direct, 0);
        EXPECT_FALSE(run.style.bold);
    }
    // Joining body text into the heading keeps the body text's look as direct formatting.
    e.selection = {{0, 7}, {1, 0}};
    e.replace(u"");
    ASSERT_EQ(e.document.paragraphs.size(), 1u);
    EXPECT_EQ(e.document.styleAt({0, 9}).size, 12);
    EXPECT_NO_THROW(validateDocument(e.document));
    // New table cells and paragraphs under a customized Normal match it.
    e.selection = {{0, 0}, {0, 7}};
    e.applyPreset(ParagraphPreset::Normal);
    e.format([](auto &s) { s.family = "Georgia"; });
    e.selection = {{0, 1}, {0, 1}};
    e.updateStyleFromSelection();
    e.selection = {{0, 0}, {0, 0}};
    e.insertTable(2, 2);
    const auto last = e.document.paragraphs.size() - 1; // the body paragraph after the table
    e.selection = {{last, 0}, {last, 0}};
    e.insertPageBreak();
    EXPECT_NO_THROW(validateDocument(e.document));
    for (const auto &paragraph : e.document.paragraphs)
        if (paragraph.cell)
            EXPECT_EQ(paragraph.runs.front().style.family, "Georgia");
}

TEST(Styles, NewStyleFromSelectionKeepsAppearanceAndRestylesFollowers) {
    auto e = headingsAndBody();
    e.selection = {{2, 0}, {2, 4}};
    e.format([](auto &s) {
        s.family = "Georgia";
        s.color = 0xff336699;
    });
    e.selection = {{2, 0}, {2, 0}};
    e.formatParagraph([](auto &p) { p.spaceBefore = 18; });
    const auto before = e.document.paragraphs[2];
    e.selection = {{2, 1}, {2, 1}};
    const auto id = e.createStyleFromSelection("Quote");
    EXPECT_EQ(id, "Quote");
    EXPECT_NO_THROW(validateDocument(e.document));
    const auto &after = e.document.paragraphs[2];
    EXPECT_EQ(after.style, "Quote");
    EXPECT_EQ(static_cast<const ParagraphFormat &>(after), static_cast<const ParagraphFormat &>(before));
    EXPECT_EQ(after.runs.front().style, before.runs.front().style);
    EXPECT_EQ(after.direct, 0);
    EXPECT_EQ(after.runs.front().direct, 0);
    const auto *quote = e.document.styles.find("Quote");
    ASSERT_NE(quote, nullptr);
    EXPECT_EQ(quote->basedOn, "Normal");
    EXPECT_EQ(quote->character.family, "Georgia");
    EXPECT_EQ(quote->paragraph.spaceBefore, 18);
    // A second style with a name that maps to the same identifier gets a fresh one.
    EXPECT_EQ(e.createStyleFromSelection("Quote!"), "Quote2");
    EXPECT_THROW(e.createStyleFromSelection("Quote"), std::invalid_argument); // duplicate name
    EXPECT_THROW(e.createStyleFromSelection(""), std::invalid_argument);
    EXPECT_EQ(uniqueStyleId(e.document.styles, "Heading 1"), "Heading12");
    EXPECT_EQ(uniqueStyleId(e.document.styles, "Цитата"), "Style");
    // Undo removes the second style and gives the paragraph back to Quote.
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.styles.find("Quote2"), nullptr);
    EXPECT_EQ(e.document.paragraphs[2].style, "Quote");
}

TEST(Styles, RenamingKeepsIdentifierAndPersists) {
    auto e = headingsAndBody();
    e.selection = {{2, 0}, {2, 0}};
    const auto id = e.createStyleFromSelection("Body text");
    e.renameStyle(id, "Body copy");
    EXPECT_EQ(e.document.styles.find(id)->name, "Body copy");
    EXPECT_EQ(e.document.paragraphs[2].style, id);
    EXPECT_THROW(e.renameStyle("Heading1", "Chapter"), std::invalid_argument);
    EXPECT_THROW(e.renameStyle(id, "Title"), std::invalid_argument); // name in use
    EXPECT_THROW(e.renameStyle("Missing", "X"), std::invalid_argument);
    const auto reopened = parseNative(serializeNative(e.document));
    EXPECT_EQ(reopened.styles, e.document.styles);
    EXPECT_EQ(reopened.paragraphs[2].style, id);
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.styles.find(id)->name, "Body text");
}

TEST(Styles, DeletingAStyleFallsBackToItsBaseAndKeepsDerivedStylesLooking) {
    auto e = headingsAndBody();
    // Callout (based on Normal): bold, 14 pt. Warning (based on Callout): red.
    e.selection = {{2, 0}, {2, 4}};
    e.format([](auto &s) {
        s.bold = true;
        s.size = 14;
    });
    e.selection = {{2, 0}, {2, 0}};
    const auto callout = e.createStyleFromSelection("Callout");
    e.replace(u"Warn\n");
    e.selection = {{2, 0}, {2, 4}};
    e.format([](auto &s) { s.color = 0xffcc0000; });
    e.selection = {{2, 1}, {2, 1}};
    const auto warning = e.createStyleFromSelection("Warning");
    ASSERT_EQ(e.document.paragraphs[3].style, callout);
    // Enter after a Warning paragraph continues as Callout.
    e.document.styles.find(warning)->next = callout;
    const auto warningLook = e.document.paragraphs[2].runs.front().style;
    // Direct italic on the Callout paragraph survives deletion.
    e.selection = {{3, 0}, {3, 4}};
    e.format([](auto &s) { s.italic = true; });

    e.deleteStyle(callout);
    EXPECT_NO_THROW(validateDocument(e.document));
    EXPECT_EQ(e.document.styles.find(callout), nullptr);
    const auto *derived = e.document.styles.find(warning);
    EXPECT_EQ(derived->basedOn, "Normal");
    EXPECT_EQ(derived->next, "Normal");
    EXPECT_EQ(e.document.paragraphs[2].runs.front().style, warningLook);
    const auto &body = e.document.paragraphs[3];
    EXPECT_EQ(body.style, "Normal");
    EXPECT_FALSE(body.runs.front().style.bold);  // came from Callout
    EXPECT_EQ(body.runs.front().style.size, 12); // Normal's size
    EXPECT_TRUE(body.runs.front().style.italic); // direct
    EXPECT_THROW(e.deleteStyle("Normal"), std::invalid_argument);
    EXPECT_THROW(e.deleteStyle(callout), std::invalid_argument);
    ASSERT_TRUE(e.undo());
    EXPECT_NE(e.document.styles.find(callout), nullptr);
    EXPECT_EQ(e.document.paragraphs[3].style, callout);
    EXPECT_TRUE(e.document.paragraphs[3].runs.front().style.bold);
}
