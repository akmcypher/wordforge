#include "core/document/document.hpp"
#include <gtest/gtest.h>
#include <functional>
#include <string>
#include <utility>
using namespace wf;
namespace {
// How many paragraphs of `after` are not shared with `before`: everything between the
// longest shared prefix and the longest shared suffix.
std::size_t unshared(const Document &before, const Document &after) {
    const auto &a = before.paragraphs;
    const auto &b = after.paragraphs;
    std::size_t prefix = 0, suffix = 0;
    while (prefix < a.size() && prefix < b.size() && b.shares(prefix, a, prefix))
        ++prefix;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           b.shares(b.size() - 1 - suffix, a, a.size() - 1 - suffix))
        ++suffix;
    return b.size() - prefix - suffix;
}
Editor longEditor() {
    Document document;
    document.paragraphs.clear();
    for (int i = 0; i < 300; ++i) {
        Paragraph paragraph(document.allocateId());
        paragraph.runs = {{u"Paragraph number " + std::u16string(1, u'a' + i % 26), {}}};
        document.paragraphs.push_back(std::move(paragraph));
    }
    Editor editor;
    editor.reset(std::move(document));
    editor.selection = {{150, 3}, {150, 3}};
    return editor;
}
} // namespace
TEST(SharedParagraphs, CopiesShareAndAnEditClonesOnlyItsParagraph) {
    ParagraphList list{Paragraph{1}, Paragraph{2}, Paragraph{3}};
    const auto copy = list;
    EXPECT_TRUE(list.shares(1, copy, 1));
    list.edit(1).revision = 7;
    EXPECT_FALSE(list.shares(1, copy, 1));
    EXPECT_TRUE(list.shares(0, copy, 0));
    EXPECT_TRUE(list.shares(2, copy, 2));
    EXPECT_EQ(copy[1].revision, 0u);
    EXPECT_NE(list, copy);
    list.erase(list.begin() + 1);
    list.insert(list.begin() + 1, copy[1]);
    EXPECT_EQ(list, copy);
    list.push_back(Paragraph{4});
    EXPECT_EQ(copy.size(), 3u);
    EXPECT_EQ(std::as_const(list).back().id, 4u);
}
TEST(SharedParagraphs, EditsLeaveTheRestOfTheDocumentShared) {
    struct Case {
        const char *name;
        std::function<void(Editor &)> edit;
        std::size_t allowed;
    };
    const std::vector<Case> cases = {
        {"type", [](Editor &e) { e.replace(u"x"); }, 1},
        {"erase backward", [](Editor &e) { e.eraseBackward(); }, 1},
        {"erase forward", [](Editor &e) { e.eraseForward(); }, 1},
        {"select and type across paragraphs",
         [](Editor &e) {
             e.selection = {{150, 2}, {152, 2}};
             e.replace(u"joined");
         },
         3}, // undo brings back the two joined paragraphs
        {"paragraph break", [](Editor &e) { e.insertParagraphBreak(); }, 2},
        {"page break", [](Editor &e) { e.insertPageBreak(); }, 2},
        {"character format",
         [](Editor &e) {
             e.selection = {{150, 0}, {150, 5}};
             e.format([](auto &s) { s.bold = true; });
         },
         1},
        {"paragraph format", [](Editor &e) { e.formatParagraph([](auto &p) { p.spaceAfter = 4; }); },
         1},
        {"alignment", [](Editor &e) { e.align(Alignment::Center); }, 1},
        {"heading", [](Editor &e) { e.applyPreset(ParagraphPreset::Heading1); }, 1},
        {"link",
         [](Editor &e) {
             e.selection = {{150, 0}, {150, 5}};
             e.setLink("https://example.com");
         },
         1},
        {"bookmark", [](Editor &e) { e.addBookmark("here"); }, 0},
        {"comment", [](Editor &e) { e.addComment(u"note", "Reviewer", ""); }, 0},
        {"footnote", [](Editor &e) { e.insertFootnote(u"note text"); }, 1},
        {"tracked typing",
         [](Editor &e) {
             e.setTrackChanges(true);
             e.replace(u"x");
         },
         1},
        {"table", [](Editor &e) { e.insertTable(2, 2); }, 6},
        {"section break", [](Editor &e) { e.insertSectionBreak(false); }, 2},
        {"page header",
         [](Editor &e) {
             auto page = e.sectionPage();
             page.header.left = u"Header";
             e.setPage(page);
         },
         0},
    };
    for (const auto &test : cases) {
        auto editor = longEditor();
        const auto before = editor.document;
        test.edit(editor);
        EXPECT_LE(unshared(before, editor.document), test.allowed) << test.name;
        const auto edited = editor.document;
        ASSERT_TRUE(editor.undo()) << test.name;
        EXPECT_LE(unshared(edited, editor.document), test.allowed) << test.name << " undo";
        const auto undone = editor.document;
        ASSERT_TRUE(editor.redo()) << test.name;
        EXPECT_LE(unshared(undone, editor.document), test.allowed) << test.name << " redo";
        EXPECT_EQ(editor.document.paragraphs, edited.paragraphs) << test.name;
    }
}
TEST(SharedParagraphs, UndoRestoresContentEvenWhenEditsReachSharedParagraphs) {
    auto editor = longEditor();
    const auto original = editor.document.paragraphs;
    for (int i = 0; i < 20; ++i) {
        editor.selection = {{static_cast<std::size_t>(i * 7), 1}, {static_cast<std::size_t>(i * 7), 1}};
        editor.replace(u"z");
        if (i % 3 == 0)
            editor.insertParagraphBreak();
    }
    while (editor.undo()) {
    }
    EXPECT_EQ(editor.document.paragraphs, original);
}
