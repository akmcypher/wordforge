#include "core/document/document.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
using namespace wf;
TEST(Images, InsertResizeAndDeleteAreAtomicAndRetainImmutableResources) {
    Editor editor;
    editor.replace(u"before after");
    editor.selection = {{0, 7}, {0, 7}};
    auto resource = std::make_shared<ImageResource>();
    resource->pixelWidth = 4;
    resource->pixelHeight = 2;
    std::ifstream fixture(std::filesystem::path(__FILE__).parent_path().parent_path() /
                              "fixtures/red-blue.png",
                          std::ios::binary);
    ASSERT_TRUE(fixture.good());
    resource->png = std::string((std::istreambuf_iterator<char>(fixture)), {});
    const auto id = editor.insertImage(resource, 144, 72, "test image");
    ASSERT_EQ(editor.document.images.size(), 1);
    EXPECT_NE(editor.document.images.at(id).get(), resource.get());
    EXPECT_EQ(editor.document.images.at(id)->png, resource->png);
    const auto frozen = editor.document.images.at(id);
    resource->png[0] = 'x';
    EXPECT_NE(editor.document.images.at(id)->png, resource->png);
    EXPECT_EQ(editor.document.text(), u"before \ufffcafter");
    ASSERT_TRUE(editor.selectedImage());
    editor.resizeImage(72, 36);
    EXPECT_EQ(editor.selectedImage()->width, 72);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.selectedImage()->width, 144);
    EXPECT_EQ(editor.document.images.at(id).get(), frozen.get());
    editor.eraseForward();
    EXPECT_EQ(editor.document.text(), u"before after");
    EXPECT_TRUE(editor.document.images.empty());
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document.text(), u"before \ufffcafter");
    EXPECT_EQ(editor.document.images.size(), 1);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document.text(), u"before after");
}
TEST(Images, PictureAtomsHaveHardEdgesNextToCombiningText) {
    std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path() /
                           "fixtures/red-blue.png",
                       std::ios::binary);
    ASSERT_TRUE(file.good());
    auto resource = std::make_shared<const ImageResource>(
        ImageResource{4, 2, std::string((std::istreambuf_iterator<char>(file)), {})});
    Editor editor;
    editor.replace(u"a\u0301");
    editor.insertImage(resource, 72, 36);
    editor.selection.anchor = editor.selection.focus;
    editor.replace(u"\u0301");
    EXPECT_EQ(editor.document.text(), u"a\u0301\ufffc\u0301");
    validateDocument(editor.document);
    editor.eraseBackward();
    EXPECT_EQ(editor.document.text(), u"a\u0301\ufffc");
    EXPECT_EQ(editor.document.images.size(), 1);
    editor.eraseBackward();
    EXPECT_EQ(editor.document.text(), u"a\u0301");
    EXPECT_TRUE(editor.document.images.empty());
}
TEST(Anchors, BookmarksFollowEditsReorderingAndUndo) {
    Editor editor;
    editor.replace(u"first\nsecond");
    editor.selection = {{1, 3}, {1, 3}};
    const auto id = editor.addBookmark("chapter_one");
    const auto anchor = editor.document.bookmarks[0].anchor;
    editor.selection = {{0, 0}, {0, 0}};
    editor.replace(u"new\n");
    ASSERT_TRUE(editor.document.resolve(anchor));
    EXPECT_EQ(editor.document.resolve(anchor)->paragraph, 2);
    EXPECT_TRUE(editor.goToBookmark(id));
    EXPECT_EQ(editor.selection.focus.offset, 3);
    editor.replace(u"added");
    EXPECT_EQ(editor.document.bookmarks[0].anchor.offset, 8);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document.bookmarks[0].anchor.offset, 3);
    editor.transact([](Editor &candidate) {
        std::swap(candidate.document.paragraphs.front(), candidate.document.paragraphs.back());
        candidate.selection = {};
    });
    EXPECT_EQ(editor.document.resolve(editor.document.bookmarks[0].anchor)->paragraph, 0);
    ASSERT_TRUE(editor.undo());
    EXPECT_THROW(editor.addBookmark("chapter_one"), std::invalid_argument);
    editor.removeBookmark(id);
    EXPECT_TRUE(editor.document.bookmarks.empty());
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document.bookmarks[0].id, id);
}
TEST(Anchors, NavigationUsesTheDestinationCharacterStyle) {
    Editor editor;
    editor.replace(u"Heading");
    editor.applyPreset(ParagraphPreset::Heading1);
    const auto id = editor.addBookmark("heading");
    editor.typingStyle = {};
    ASSERT_TRUE(editor.goToBookmark(id));
    EXPECT_TRUE(editor.typingStyle.bold);
    EXPECT_EQ(editor.typingStyle.size, 20);
}
TEST(Anchors, ReplacementCollapsesBookmarksAndKeepsGraphemeBoundaries) {
    Editor editor;
    editor.replace(u"one\ntwo\nthree");
    editor.selection = {{1, 1}, {1, 1}};
    editor.addBookmark("inside");
    editor.selection = {{0, 1}, {2, 2}};
    editor.replace(u"A\nB");
    const auto position = editor.document.resolve(editor.document.bookmarks[0].anchor);
    ASSERT_TRUE(position);
    EXPECT_EQ(*position, (Position{1, 1}));
    validateDocument(editor.document);
    auto invalid = editor.document;
    invalid.bookmarks[0].anchor.paragraphId = 999;
    EXPECT_THROW(validateDocument(invalid), std::invalid_argument);
    Editor combining;
    combining.replace(u"a");
    combining.addBookmark("before", Affinity::Before);
    combining.addBookmark("after", Affinity::After);
    combining.replace(u"\u0301");
    EXPECT_EQ(combining.document.bookmarks[0].anchor.offset, 0);
    EXPECT_EQ(combining.document.bookmarks[1].anchor.offset, 2);
    validateDocument(combining.document);
}
TEST(Identity, RejectsInvalidDocumentsAndPreservesAllocatorAcrossUndoBranches) {
    Document invalid;
    invalid.paragraphs.push_back(invalid.paragraphs[0]);
    EXPECT_THROW(validateDocument(invalid), std::invalid_argument);
    invalid.paragraphs.pop_back();
    invalid.nextId = 1;
    EXPECT_THROW(validateDocument(invalid), std::invalid_argument);
    Editor editor;
    editor.replace(u"first\nsecond");
    const auto removed = editor.document.paragraphs[1].id;
    ASSERT_TRUE(editor.undo());
    editor.replace(u"new\nbranch");
    EXPECT_GT(editor.document.paragraphs[1].id, removed);
}
TEST(Identity, ExhaustedAllocationDoesNotChangeContentOrUndoHistory) {
    Editor editor;
    editor.document.nextId = std::numeric_limits<std::uint64_t>::max();
    EXPECT_THROW(editor.replace(u"cannot\nallocate"), std::overflow_error);
    EXPECT_EQ(editor.document.text(), u"");
    EXPECT_FALSE(editor.undo());
}
TEST(Transactions, GroupsCommandsAndRollsBackFailuresIncludingHistory) {
    Editor editor;
    editor.replace(u"before");
    const auto revision = editor.document.revision;
    const auto changes = editor.transact([](Editor &candidate) {
        candidate.replace(u"\nsecond");
        candidate.selection = {{1, 0}, {1, 6}};
        candidate.format([](auto &style) { style.bold = true; });
    });
    EXPECT_EQ(changes.beforeRevision, revision);
    EXPECT_EQ(changes.afterRevision, editor.document.revision);
    ASSERT_EQ(changes.inserted.size(), 1);
    EXPECT_EQ(changes.inserted[0], editor.document.paragraphs[1].id);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document.text(), u"before");
    const auto selectionBeforeFailure = editor.selection;
    const auto styleBeforeFailure = editor.typingStyle;
    const auto revisionBeforeFailure = editor.document.revision;
    EXPECT_THROW(editor.transact([](Editor &candidate) {
        candidate.replace(u"must not leak");
        candidate.setPage({10, 10, 20});
    }),
                 std::invalid_argument);
    EXPECT_EQ(editor.document.text(), u"before");
    EXPECT_EQ(editor.selection.anchor, selectionBeforeFailure.anchor);
    EXPECT_EQ(editor.selection.focus, selectionBeforeFailure.focus);
    EXPECT_EQ(editor.typingStyle, styleBeforeFailure);
    EXPECT_EQ(editor.document.revision, revisionBeforeFailure);
    ASSERT_TRUE(editor.redo());
    EXPECT_EQ(editor.document.text(), u"before\nsecond");
    EXPECT_TRUE(editor.document.paragraphs[1].runs[0].style.bold);
    const auto unchanged = editor.transact([](Editor &) {});
    EXPECT_EQ(unchanged.beforeRevision, unchanged.afterRevision);
    EXPECT_TRUE(unchanged.changed.empty());
    EXPECT_THROW(editor.transact([](Editor &candidate) { candidate.undo(); }), std::logic_error);
}
TEST(Transactions, ReportsMovesAndRejectsInvalidTypingState) {
    Editor editor;
    editor.replace(u"first\nsecond");
    editor.selection = {};
    const auto oldRevision = editor.document.revision;
    const auto changes = editor.transact([](Editor &candidate) {
        std::swap(candidate.document.paragraphs[0], candidate.document.paragraphs[1]);
    });
    EXPECT_GT(changes.afterRevision, oldRevision);
    EXPECT_TRUE(changes.orderChanged);
    EXPECT_TRUE(changes.changed.empty());
    EXPECT_EQ(editor.document.text(), u"second\nfirst");
    EXPECT_THROW(editor.transact([](Editor &candidate) { candidate.typingStyle.size = -1; }),
                 std::invalid_argument);
    EXPECT_EQ(editor.typingStyle.size, 12);
    ASSERT_TRUE(editor.undo());
    EXPECT_EQ(editor.document.text(), u"first\nsecond");
}
TEST(Transactions, IndexShiftsDoNotInvalidateUntouchedParagraphsOrReuseIds) {
    Editor editor;
    editor.replace(u"first\nsecond\nthird");
    const auto lastId = editor.document.paragraphs[2].id;
    const auto lastRevision = editor.document.paragraphs[2].revision;
    editor.selection = {{0, 2}, {0, 2}};
    const auto changes = editor.transact([](Editor &candidate) { candidate.insertPageBreak(); });
    EXPECT_FALSE(changes.orderChanged);
    EXPECT_EQ(changes.changed.size(), 1);
    EXPECT_EQ(editor.document.paragraphs.back().id, lastId);
    EXPECT_EQ(editor.document.paragraphs.back().revision, lastRevision);
    const auto retired = editor.document.paragraphs[1].id;
    ASSERT_TRUE(editor.undo());
    editor.transact([&](Editor &candidate) {
        candidate.document.nextId = retired;
        candidate.replace(u"\nnew");
    });
    EXPECT_GT(editor.document.paragraphs[1].id, retired);
}
TEST(Editing, StyleChangesPreserveExplicitPageBoundaries) {
    Editor e;
    e.replace(u"before");
    e.insertPageBreak();
    e.replace(u"heading");
    ASSERT_TRUE(e.document.paragraphs[1].pageBreakBefore);
    e.applyPreset(ParagraphPreset::Heading1);
    EXPECT_TRUE(e.document.paragraphs[1].pageBreakBefore);
}
TEST(Editing, ParagraphFormattingKeepsIdsAndSupportsUndo) {
    Editor e;
    e.replace(u"first\nsecond");
    const auto id = e.document.paragraphs[0].id;
    e.selection = {{0, 0}, {1, 0}};
    e.formatParagraph([](auto &p) {
        p.list = ListKind::Decimal;
        p.indentLeft = 18;
        p.lineSpacing = 1.5;
    });
    EXPECT_EQ(e.document.paragraphs[0].list, ListKind::Decimal);
    EXPECT_EQ(e.document.paragraphs[1].list, ListKind::None);
    EXPECT_EQ(e.document.paragraphs[0].id, id);
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.paragraphs[0].list, ListKind::None);
    EXPECT_THROW(e.formatParagraph([](auto &p) { p.lineSpacing = -1; }), std::invalid_argument);
}
TEST(Editing, ListsContinueAndExitOnEmptyParagraphs) {
    Editor e;
    e.formatParagraph([](auto &p) { p.list = ListKind::Bullet; });
    e.replace(u"item");
    e.insertParagraphBreak();
    ASSERT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.document.paragraphs[1].list, ListKind::Bullet);
    e.insertParagraphBreak();
    EXPECT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.document.paragraphs[1].list, ListKind::None);
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.paragraphs[1].list, ListKind::Bullet);
}
TEST(Editing, PageBreakIsOneUndoTransactionAndDoesNotRepeatOnEnter) {
    Editor e;
    e.replace(u"beforeafter");
    e.selection = {{0, 6}, {0, 6}};
    e.insertPageBreak();
    ASSERT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.document.paragraphs[1].text(), u"after");
    EXPECT_TRUE(e.document.paragraphs[1].pageBreakBefore);
    e.insertParagraphBreak();
    EXPECT_FALSE(e.document.paragraphs[2].pageBreakBefore);
    ASSERT_TRUE(e.undo());
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.text(), u"beforeafter");
}
TEST(Editing, PresetsApplyAndReturnToBodyTextAfterHeading) {
    Editor e;
    e.replace(u"Heading");
    e.applyPreset(ParagraphPreset::Heading1);
    EXPECT_EQ(e.document.paragraphs[0].style, "Heading1");
    EXPECT_TRUE(e.document.paragraphs[0].runs[0].style.bold);
    e.insertParagraphBreak();
    ASSERT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.document.paragraphs[1].style, "Normal");
    EXPECT_FALSE(e.typingStyle.bold);
}
TEST(Editing, SplitJoinAndUndoAreAtomic) {
    Editor e;
    e.replace(u"Hello\nworld");
    ASSERT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.document.text(), u"Hello\nworld");
    e.selection = {{1, 0}, {1, 0}};
    e.eraseBackward();
    EXPECT_EQ(e.document.text(), u"Helloworld");
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.document.text(), u"Hello\nworld");
    ASSERT_TRUE(e.redo());
    EXPECT_EQ(e.document.text(), u"Helloworld");
}
TEST(Editing, DeletesAnEntireEmojiCluster) {
    Editor e;
    e.replace(u"A👩‍💻");
    e.eraseBackward();
    EXPECT_EQ(e.document.text(), u"A");
}
TEST(Editing, FormattingSurvivesPartialReplacement) {
    Editor e;
    e.replace(u"abcdef");
    e.selection = {{0, 1}, {0, 5}};
    e.format([](auto &s) { s.bold = true; });
    e.selection = {{0, 2}, {0, 4}};
    e.replace(u"X");
    EXPECT_EQ(e.document.text(), u"abXef");
    ASSERT_GE(e.document.paragraphs[0].runs.size(), 3u);
    EXPECT_FALSE(e.document.paragraphs[0].runs.front().style.bold);
    EXPECT_TRUE(e.document.paragraphs[0].runs[1].style.bold);
    EXPECT_FALSE(e.document.paragraphs[0].runs.back().style.bold);
}
TEST(Editing, RejectsInvalidSelectionWithoutChangingHistory) {
    Editor e;
    e.selection = {{4, 0}, {4, 0}};
    EXPECT_THROW(e.replace(u"x"), std::out_of_range);
    EXPECT_EQ(e.document.text(), u"");
    EXPECT_FALSE(e.undo());
}
TEST(Editing, NewEditsClearRedoAndIdsAreStable) {
    Editor e;
    e.replace(u"one\ntwo");
    const auto id = e.document.paragraphs[0].id;
    e.replace(u"!");
    ASSERT_TRUE(e.undo());
    e.replace(u"?");
    EXPECT_FALSE(e.redo());
    EXPECT_EQ(e.document.paragraphs[0].id, id);
}
TEST(Unicode, RoundTripsAndPreservesEmptyParagraphs) {
    EXPECT_EQ(utf16(utf8(u"नमस्ते مرحبا 🪷")), u"नमस्ते مرحبا 🪷");
    Editor e;
    e.replace(u"\n\nlast\n");
    EXPECT_EQ(e.document.text(), u"\n\nlast\n");
}
TEST(Editing, PageSetupIsUndoableAndRejectsImpossibleMargins) {
    Editor e;
    e.setPage({612, 792, 36});
    EXPECT_EQ(e.document.page.width, 612);
    ASSERT_TRUE(e.undo());
    EXPECT_DOUBLE_EQ(e.document.page.width, 595.28);
    EXPECT_THROW(e.setPage({100, 100, 80}), std::invalid_argument);
    EXPECT_DOUBLE_EQ(e.document.page.width, 595.28);
}
TEST(Editing, UndoDeletionRestoresTheOriginalCollapsedCaret) {
    Editor e;
    e.replace(u"ab");
    e.eraseBackward();
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.selection.anchor, (Position{0, 2}));
    EXPECT_EQ(e.selection.focus, (Position{0, 2}));
    e.replace(u"c");
    EXPECT_EQ(e.document.text(), u"abc");
    e.selection = {{0, 1}, {0, 1}};
    e.eraseForward();
    ASSERT_TRUE(e.undo());
    EXPECT_EQ(e.selection.anchor, e.selection.focus);
    e.replace(u"X");
    EXPECT_EQ(e.document.text(), u"aXbc");
}
TEST(Editing, JoiningEmojiKeepsTheCaretOnAGraphemeBoundary) {
    Editor e;
    e.replace(u"👩💻");
    e.selection = {{0, 2}, {0, 2}};
    e.replace(u"‍");
    EXPECT_NO_THROW(e.replace(u"x"));
    EXPECT_EQ(e.document.text(), u"👩‍💻x");
}
TEST(Statistics, CountsWordsGraphemesAndNonEmptyParagraphs) {
    const auto count = countText(u"Hello, wörld 42!\n\n👩‍💻 नमस्ते\t\n");
    EXPECT_EQ(count.words, 4u);
    // The emoji is one character and not a word; "नमस्ते" is three graphemes.
    EXPECT_EQ(count.characters, 22u);
    EXPECT_EQ(count.charactersWithoutSpaces, 18u);
    EXPECT_EQ(count.paragraphs, 2u);
    EXPECT_EQ(countText(u""), TextCount{});
}
TEST(Clipboard, FragmentRoundTripKeepsFormattingInOneUndoStep) {
    Editor source;
    source.replace(u"plain bold\nHeading");
    source.selection = {{0, 6}, {0, 10}};
    source.format([](auto &s) { s.bold = true; });
    source.selection = {{1, 0}, {1, 0}};
    source.applyPreset(ParagraphPreset::Heading1);
    source.selection = {{0, 0}, {1, 7}};
    const auto fragment = source.copySelection();
    ASSERT_EQ(fragment.size(), 2u);

    Editor target;
    target.replace(u"<>");
    target.selection = {{0, 1}, {0, 1}};
    target.insertFragment(fragment);
    ASSERT_EQ(target.document.paragraphs.size(), 2u);
    EXPECT_EQ(target.document.paragraphs[0].text(), u"<plain bold");
    EXPECT_EQ(target.document.paragraphs[1].text(), u"Heading>");
    // The first paragraph keeps the target's format; later ones bring their own.
    EXPECT_EQ(target.document.paragraphs[0].style, "Normal");
    EXPECT_EQ(target.document.paragraphs[1].style, "Heading1");
    EXPECT_TRUE(target.document.styleAt({0, 9}).bold);
    EXPECT_FALSE(target.document.styleAt({0, 3}).bold);
    EXPECT_TRUE(target.undo());
    EXPECT_EQ(target.document.text(), u"<>");
}
