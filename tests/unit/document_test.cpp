#include "core/document/document.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
using namespace wf;
TEST(Editor, ResetUsesFormattingAtTheInitialCaretForTyping) {
    Editor source;
    source.replace(u"Formatted text");
    source.selection = {{0, 0}, {0, 14}};
    source.format([](auto &s) {
        s.family = "Georgia";
        s.size = 19;
        s.italic = true;
    });
    Editor editor;
    editor.reset(source.document);
    EXPECT_EQ(editor.typingStyle, editor.document.styleAt({0, 0}));
    editor.replace(u"X");
    EXPECT_EQ(editor.document.paragraphs[0].runs[0].style.family, "Georgia");
    EXPECT_EQ(editor.document.paragraphs[0].runs[0].style.size, 19);
    EXPECT_TRUE(editor.document.paragraphs[0].runs[0].style.italic);
    EXPECT_TRUE(editor.undo());
    EXPECT_FALSE(editor.undo());
    editor.reset({});
    EXPECT_EQ(editor.typingStyle, editor.document.styleAt({0, 0}));
}
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
        auto &list = candidate.document.paragraphs;
        const auto first = list[0], last = list.back();
        list.edit(0) = last;
        list.edit(list.size() - 1) = first;
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
        auto &list = candidate.document.paragraphs;
        const auto first = list[0], second = list[1];
        list.edit(0) = second;
        list.edit(1) = first;
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

TEST(Preservation, PartsAndRelationshipsAreValidated) {
    auto valid = [] {
        PreservedPackage package;
        package.parts = {{"word/theme/theme1.xml", "application/x-theme", "<t/>"}};
        package.relationships = {{false, "http://example/theme", "word/theme/theme1.xml"}};
        return package;
    };
    EXPECT_NO_THROW(validatePreserved(valid()));
    for (const auto *path : {"", "/abs.xml", "word/../x.xml", "word//x.xml", "a\\b.xml",
                             "[Content_Types].xml", "word/document.xml", "word/media/a.png",
                             "_rels/.rels", "word/x y.xml"}) {
        auto package = valid();
        package.parts[0].path = path;
        package.relationships.clear();
        EXPECT_THROW(validatePreserved(package), std::invalid_argument) << path;
    }
    auto package = valid();
    package.parts.push_back(package.parts[0]); // duplicate
    EXPECT_THROW(validatePreserved(package), std::invalid_argument);
    package = valid();
    package.parts[0].contentType.clear();
    EXPECT_THROW(validatePreserved(package), std::invalid_argument);
    package = valid();
    package.relationships[0].target = "word/other.xml";
    EXPECT_THROW(validatePreserved(package), std::invalid_argument);
    package = valid();
    package.parts[0].data.assign(maxPreservedBytes + 1, 'x');
    EXPECT_THROW(validatePreserved(package), std::invalid_argument);
}

TEST(Footnotes, InsertEditDeleteAndUndo) {
    Editor e;
    e.replace(u"claim");
    const auto id = e.insertFootnote(u"Source: a study.");
    ASSERT_EQ(e.document.footnotes.size(), 1u);
    EXPECT_EQ(e.document.paragraphs[0].text(), u"claim￼");
    EXPECT_EQ(e.selectedFootnote(), id); // caret just after the reference
    e.replace(u" more");
    EXPECT_EQ(e.document.paragraphs[0].text(), u"claim￼ more");
    e.selection = {{0, 6}, {0, 6}};
    e.setFootnoteText(id, u"Edited.");
    EXPECT_EQ(e.document.footnote(id)->text, u"Edited.");
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.footnote(id)->text, u"Source: a study.");
    // Deleting the reference deletes the footnote; undo brings both back.
    e.selection = {{0, 5}, {0, 6}};
    e.replace(u"");
    EXPECT_TRUE(e.document.footnotes.empty());
    EXPECT_NO_THROW(validateDocument(e.document));
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.footnotes.size(), 1u);
    // Copying text does not duplicate the reference.
    // (Undo may group the deletion with the typing before it.)
    e.selection = {{0, 0}, {0, e.document.paragraphs[0].text().size()}};
    const auto fragment = e.copySelection();
    for (const auto &p : fragment)
        for (const auto &run : p.runs)
            EXPECT_FALSE(run.note());
    EXPECT_THROW(e.insertFootnote(u"bad\ttext"), std::invalid_argument);
    EXPECT_THROW(e.insertFootnote(std::u16string(2001, u'x')), std::invalid_argument);
    EXPECT_THROW(e.setFootnoteText(id + 100, u"x"), std::invalid_argument);
}
TEST(Footnotes, ReferencesAndNotesMustMatch) {
    Editor e;
    e.replace(u"a");
    const auto id = e.insertFootnote(u"note");
    auto orphan = e.document;
    orphan.footnotes.push_back({orphan.allocateId(), u"orphan"});
    EXPECT_THROW(validateDocument(orphan), std::invalid_argument);
    auto twice = e.document;
    twice.paragraphs.edit(0).runs.push_back(Run::fromNote({id}));
    EXPECT_THROW(validateDocument(twice), std::invalid_argument);
    auto missing = e.document;
    missing.footnotes.clear();
    EXPECT_THROW(validateDocument(missing), std::invalid_argument);
}

TEST(Comments, RangesFollowEditsAndUndo) {
    Editor e;
    e.replace(u"The quick brown fox");
    e.selection = {{0, 4}, {0, 9}}; // "quick"
    const auto id = e.addComment(u"Too informal?", "Ana", "2026-11-16T10:00:00Z");
    auto text = [&] {
        const auto range = e.commentRange(id);
        EXPECT_TRUE(range);
        const auto p = e.document.paragraphs[range->anchor.paragraph].text();
        return p.substr(range->anchor.offset, range->focus.offset - range->anchor.offset);
    };
    EXPECT_EQ(text(), u"quick");
    // Typing before the range moves it; typing inside or at its edges grows it.
    e.selection = {{0, 0}, {0, 0}};
    e.replace(u"Look: ");
    EXPECT_EQ(text(), u"quick");
    e.selection = {{0, 15}, {0, 15}}; // just after "quick"
    e.replace(u"est");
    EXPECT_EQ(text(), u"quickest");
    // Deleting the text collapses the comment to a point; it stays valid.
    const auto range = *e.commentRange(id);
    e.selection = {range.anchor, range.focus};
    e.replace(u"");
    EXPECT_EQ(text(), u"");
    EXPECT_NO_THROW(validateDocument(e.document));
    EXPECT_TRUE(e.undo()); // may also undo the typing grouped with it
    EXPECT_FALSE(text().empty());
    e.setCommentText(id, u"Fine.");
    EXPECT_EQ(e.document.comments[0].text, u"Fine.");
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.comments[0].text, u"Too informal?");
    e.deleteComment(id);
    EXPECT_TRUE(e.document.comments.empty());
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(e.document.comments.size(), 1u);
    EXPECT_THROW(e.addComment(u"a\nb", "Ana", ""), std::invalid_argument);
    EXPECT_THROW(e.deleteComment(id + 99), std::invalid_argument);
    // A reversed range is rejected.
    auto reversed = e.document;
    std::swap(reversed.comments[0].start.offset, reversed.comments[0].end.offset);
    reversed.comments[0].start.offset = 15;
    reversed.comments[0].end.offset = 2;
    EXPECT_THROW(validateDocument(reversed), std::invalid_argument);
}
TEST(Comments, SurviveTableStructureEdits) {
    Editor e;
    e.insertTable(2, 2);
    e.replace(u"cell");
    e.selection = {{0, 0}, {0, 4}};
    e.addComment(u"About this cell", "Ana", "");
    e.tableColumn(false); // deletes the column holding the comment
    EXPECT_NO_THROW(validateDocument(e.document));
    e.removeTable();
    EXPECT_NO_THROW(validateDocument(e.document));
    EXPECT_EQ(e.document.comments.size(), 1u);
}

namespace {
// One letter per run: '-' plain, 'i' inserted, 'd' deleted, 'b' both.
std::string marks(const wf::Paragraph &paragraph) {
    std::string out;
    for (const auto &run : paragraph.runs)
        out += run.inserted && run.deleted ? 'b' : run.inserted ? 'i' : run.deleted ? 'd' : '-';
    return out;
}
} // namespace
TEST(TrackedChanges, TypingAndDeletingAreRecordedAndResolved) {
    Editor e;
    e.replace(u"The cat sat");
    e.reviewer = "Ana";
    e.reviewDate = "2026-11-16T11:00:00Z";
    e.setTrackChanges(true);
    EXPECT_TRUE(e.document.trackChanges);
    e.selection = {{0, 4}, {0, 7}}; // "cat"
    e.replace(u"dog");
    EXPECT_EQ(e.document.paragraphs[0].text(), u"The catdog sat");
    EXPECT_EQ(marks(e.document.paragraphs[0]), "-di-");
    ASSERT_EQ(e.document.revisions.size(), 1u);
    EXPECT_EQ(e.document.revisions[0].author, "Ana");
    EXPECT_EQ(e.selection.focus, (Position{0, 10}));
    // Backspace over the reviewer's own insertion removes it; over original text, marks it.
    e.eraseBackward();
    EXPECT_EQ(e.document.paragraphs[0].text(), u"The catdo sat");
    e.selection = {{0, 3}, {0, 3}};
    e.eraseBackward();
    EXPECT_EQ(e.document.paragraphs[0].text(), u"The catdo sat");
    EXPECT_EQ(e.selection.focus, (Position{0, 2}));
    EXPECT_EQ(marks(e.document.paragraphs[0]), "-d-di-");
    // Delete forward over deleted text just moves past it.
    e.eraseForward();
    EXPECT_EQ(e.selection.focus, (Position{0, 3}));
    EXPECT_NO_THROW(validateDocument(e.document));

    auto accepted = e;
    accepted.acceptAllChanges();
    EXPECT_EQ(accepted.document.paragraphs[0].text(), u"Th do sat");
    EXPECT_EQ(marks(accepted.document.paragraphs[0]), "-");
    EXPECT_TRUE(accepted.document.revisions.empty());
    auto rejected = e;
    rejected.rejectAllChanges();
    EXPECT_EQ(rejected.document.paragraphs[0].text(), u"The cat sat");
    EXPECT_TRUE(rejected.document.revisions.empty());

    // The change at the caret only.
    e.selection = {{0, 5}, {0, 5}}; // inside "cat"
    ASSERT_TRUE(e.changeAtCaret());
    EXPECT_EQ(e.changeAtCaret()->anchor, (Position{0, 4}));
    EXPECT_TRUE(e.rejectChanges());
    EXPECT_EQ(marks(e.document.paragraphs[0]), "-d-i-");
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(marks(e.document.paragraphs[0]), "-d-di-");
    EXPECT_TRUE(e.redo());
    e.selection = {{0, 0}, {0, 0}};
    EXPECT_FALSE(e.acceptChanges()); // nothing at the caret
    EXPECT_TRUE(e.nextChange());
    EXPECT_EQ(e.selection.anchor, (Position{0, 2}));
    EXPECT_EQ(e.selection.focus, (Position{0, 3}));
    EXPECT_TRUE(e.nextChange());
    EXPECT_EQ(e.selection.anchor, (Position{0, 7}));
    EXPECT_EQ(e.selection.focus, (Position{0, 9}));
    EXPECT_TRUE(e.nextChange()); // wraps
    EXPECT_EQ(e.selection.anchor, (Position{0, 2}));

    // Another reviewer deleting Ana's insertion marks it deleted too; rejecting removes it.
    e.reviewer = "Bo";
    e.selection = {{0, 7}, {0, 9}};
    e.replace(u"");
    EXPECT_EQ(marks(e.document.paragraphs[0]), "-d-b-");
    EXPECT_EQ(e.document.revisions.size(), 2u);
    e.selection = {{0, 8}, {0, 8}};
    EXPECT_TRUE(e.selectionHasChanges());
    EXPECT_TRUE(e.rejectChanges());
    EXPECT_EQ(e.document.paragraphs[0].text(), u"The cat sat");
    EXPECT_EQ(e.document.revisions.size(), 1u);

    // Copying leaves deleted text out and drops the marks.
    e.selection = {{0, 0}, {0, 11}};
    const auto copied = e.copySelection();
    ASSERT_EQ(copied.size(), 1u);
    EXPECT_EQ(copied[0].text(), u"Th cat sat");
    EXPECT_EQ(marks(copied[0]), "--");
}
TEST(TrackedChanges, ParagraphsAndOtherContent) {
    Editor e;
    e.replace(u"one\ntwo");
    e.setTrackChanges(true);
    // Paragraph marks are not tracked: backspace at a paragraph start only moves the caret.
    e.selection = {{1, 0}, {1, 0}};
    e.eraseBackward();
    EXPECT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.selection.focus, (Position{0, 3}));
    e.selection = {{0, 3}, {0, 3}};
    e.eraseForward();
    EXPECT_EQ(e.selection.focus, (Position{1, 0}));
    // Typed paragraphs: their text is marked, the break is not.
    e.selection = {{0, 3}, {0, 3}};
    e.replace(u"!\nnew");
    EXPECT_EQ(e.document.paragraphs.size(), 3u);
    EXPECT_EQ(marks(e.document.paragraphs[0]), "-i");
    EXPECT_EQ(marks(e.document.paragraphs[1]), "i");
    // A selection across paragraphs is marked, not joined.
    e.selection = {{0, 1}, {2, 2}};
    e.replace(u"");
    EXPECT_EQ(e.document.paragraphs.size(), 3u);
    EXPECT_EQ(e.document.paragraphs[0].text(), u"one"); // the inserted "!" went
    EXPECT_EQ(e.document.paragraphs[1].text(), u"");
    EXPECT_EQ(marks(e.document.paragraphs[2]), "d-");
    EXPECT_NO_THROW(validateDocument(e.document));
    // Comments survive changes being resolved.
    e.selection = {{2, 0}, {2, 3}};
    e.addComment(u"Note", "Ana", "");
    e.acceptAllChanges();
    EXPECT_EQ(e.document.paragraphs[2].text(), u"o");
    EXPECT_EQ(e.document.comments.size(), 1u);
    EXPECT_NO_THROW(validateDocument(e.document));
    // A picture or footnote replacing a selection deletes it as a tracked change.
    EXPECT_EQ(e.document.paragraphs[0].text(), u"o");
    e.selection = {{0, 0}, {0, 1}};
    e.insertFootnote(u"Source");
    EXPECT_EQ(e.document.paragraphs[0].text(), u"o￼");
    EXPECT_EQ(marks(e.document.paragraphs[0]), "d-");
    // Turning tracking off makes edits plain again.
    e.setTrackChanges(false);
    e.selection = {{0, 0}, {0, 1}};
    e.replace(u"");
    EXPECT_EQ(e.document.paragraphs[0].text(), u"￼");
    // Invalid documents: a mark without a revision, an empty tracked run.
    auto broken = e.document;
    broken.paragraphs.edit(0).runs[0].inserted = 999999;
    EXPECT_THROW(validateDocument(broken), std::invalid_argument);
    e.reviewer = std::string(300, 'x');
    e.setTrackChanges(true);
    e.selection = {{1, 0}, {1, 0}};
    EXPECT_THROW(e.replace(u"x"), std::invalid_argument);
}

TEST(Tables, BordersAndShadingFollowStructureEdits) {
    Editor e;
    e.insertTable(3, 3);
    auto table = [&]() -> const Table & { return e.document.tables[0]; };
    auto at = [&](std::size_t row, std::size_t column) {
        for (std::size_t i = 0; i < e.document.paragraphs.size(); ++i)
            if (e.document.paragraphs[i].cell == TableCell{table().id, row, column})
                return Position{i, 0};
        ADD_FAILURE() << row << "," << column;
        return Position{};
    };
    e.selection = {at(0, 0), at(0, 0)};
    e.setTableBorder({1.5, 0x112233});
    EXPECT_EQ(table().border, (TableBorder{1.5, 0xff112233})); // made opaque
    EXPECT_THROW(e.setTableBorder({7, 0xff000000}), std::invalid_argument);
    e.setTableBorder({0, 0xff000000});
    EXPECT_EQ(table().border.width, 0);
    // A block of cells, then one cleared.
    e.selection = {at(0, 0), at(1, 1)};
    e.shadeCells(0xffeeddcc);
    EXPECT_EQ(table().shading.size(), 4u);
    EXPECT_EQ(table().fillAt(1, 1), 0xffeeddccu);
    e.selection = {at(1, 1), at(1, 1)};
    e.shadeCells(0);
    EXPECT_EQ(table().shading.size(), 3u);
    // A row inserted above moves the shading down; a deleted column takes its shading.
    e.selection = {at(0, 0), at(0, 0)};
    e.tableRow(true, true);
    EXPECT_EQ(table().fillAt(0, 0), 0u);
    EXPECT_EQ(table().fillAt(1, 0), 0xffeeddccu);
    e.selection = {at(1, 1), at(1, 1)};
    e.tableColumn(false);
    EXPECT_EQ(table().shading.size(), 2u); // (1,0) and (2,0)
    // A merged cell keeps a fill from inside it, on its top-left cell.
    e.selection = {at(0, 0), at(1, 0)};
    e.mergeCells();
    EXPECT_EQ(table().fillAt(0, 0), 0xffeeddccu);
    EXPECT_EQ(table().fillAt(1, 0), 0u);
    EXPECT_EQ(table().shading.size(), 2u);
    // Shading from a covered cell shades the merged cell.
    e.selection = {at(1, 0), at(1, 0)};
    e.shadeCells(0xff0000ff);
    EXPECT_EQ(table().fillAt(0, 0), 0xff0000ffu);
    EXPECT_NO_THROW(validateDocument(e.document));
    EXPECT_TRUE(e.undo());
    EXPECT_EQ(table().fillAt(0, 0), 0xffeeddccu);
    auto broken = e.document;
    broken.tables[0].shading.push_back(broken.tables[0].shading[0]);
    EXPECT_THROW(validateDocument(broken), std::invalid_argument);
    broken = e.document;
    broken.tables[0].border.color = 0x80000000;
    EXPECT_THROW(validateDocument(broken), std::invalid_argument);
}
TEST(Links, SetTypeAroundAndRemove) {
    Editor editor;
    editor.replace(u"see the site");
    editor.selection = {{0, 4}, {0, 12}};
    editor.setLink("https://example.com/a");
    EXPECT_EQ(editor.document.paragraphs[0].runs.size(), 2u);
    const auto &linked = editor.document.paragraphs[0].runs[1];
    EXPECT_EQ(linked.link, "https://example.com/a");
    EXPECT_TRUE(linked.style.underline); // Word's look for a link
    EXPECT_EQ(linked.style.color, 0xff0563c1u);
    EXPECT_NO_THROW(validateDocument(editor.document));
    // Typing inside a link extends it; typing at its end does not.
    editor.selection = {{0, 6}, {0, 6}};
    editor.typingStyle = editor.document.styleAt({0, 6}); // as moving the caret does
    EXPECT_EQ(editor.linkAt(), "https://example.com/a");
    editor.replace(u"X");
    EXPECT_EQ(editor.document.paragraphs[0].runs[1].text(), u"thXe site");
    editor.selection = {{0, 13}, {0, 13}};
    editor.typingStyle = editor.document.styleAt({0, 13});
    editor.replace(u"!");
    EXPECT_EQ(editor.document.paragraphs[0].runs.back().text(), u"!");
    EXPECT_TRUE(editor.document.paragraphs[0].runs.back().link.empty());
    // Removing the link around the caret restores the paragraph's look.
    editor.selection = {{0, 8}, {0, 8}};
    const auto range = editor.linkRange();
    ASSERT_TRUE(range);
    EXPECT_EQ(range->anchor.offset, 4u);
    EXPECT_EQ(range->focus.offset, 13u);
    editor.setLink({});
    for (const auto &run : editor.document.paragraphs[0].runs) {
        EXPECT_TRUE(run.link.empty());
        EXPECT_FALSE(run.style.underline);
    }
    EXPECT_EQ(editor.document.paragraphs[0].runs.size(), 1u);
    // One undo step per command.
    EXPECT_TRUE(editor.undo());
    EXPECT_FALSE(editor.document.paragraphs[0].runs[1].link.empty());
}
TEST(Links, InsertAtCaretAndRejectUnsafeTargets) {
    Editor editor;
    editor.replace(u"Mail ");
    editor.setLink("mailto:me@example.com", u"me");
    const auto &runs = editor.document.paragraphs[0].runs;
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[1].text(), u"me");
    EXPECT_EQ(runs[1].link, "mailto:me@example.com");
    EXPECT_EQ(editor.selection.focus.offset, 7u);
    editor.undo();
    EXPECT_EQ(editor.document.paragraphs[0].text(), u"Mail ");
    for (const auto *bad : {"javascript:alert(1)", "https://a b", "", "#", "http://", "data:x"})
        EXPECT_THROW(validateLink(bad), std::invalid_argument) << bad;
    EXPECT_NO_THROW(validateLink("#Intro"));
    EXPECT_NO_THROW(validateLink("HTTPS://Example.com"));
    EXPECT_TRUE(externalLink("https://x.org"));
    EXPECT_FALSE(externalLink("file:///etc/passwd"));
    EXPECT_FALSE(externalLink("#Intro"));
    auto document = editor.document;
    document.paragraphs.edit(0).runs[0].link = "javascript:void(0)";
    EXPECT_THROW(validateDocument(document), std::invalid_argument);
}
TEST(Links, CopyAndPasteKeepTheTarget) {
    Editor editor;
    editor.replace(u"go there now");
    editor.selection = {{0, 3}, {0, 8}};
    editor.setLink("https://example.org");
    editor.selection = {{0, 0}, {0, 12}};
    const auto fragment = editor.copySelection();
    editor.selection = {{0, 12}, {0, 12}};
    editor.insertFragment(fragment);
    std::size_t linked = 0;
    for (const auto &run : editor.document.paragraphs[0].runs)
        if (run.link == "https://example.org") {
            EXPECT_EQ(run.text(), u"there");
            ++linked;
        }
    EXPECT_EQ(linked, 2u);
}
TEST(Fields, TableOfContentsListsHeadingsAndUpdates) {
    Editor e;
    e.replace(u"Intro text");
    e.insertParagraphBreak();
    e.replace(u"Chapter one");
    e.applyStyle("Heading1");
    e.insertParagraphBreak();
    e.applyStyle("Normal");
    e.replace(u"Body");
    e.insertParagraphBreak();
    e.replace(u"Section A");
    e.applyStyle("Heading2");
    e.selection = {{0, 0}, {0, 0}};
    auto pageOf = [](std::size_t paragraph) { return paragraph + 1; };
    const auto id = e.insertTableOfContents(pageOf);
    ASSERT_EQ(e.document.fields.size(), 1u);
    const auto &field = e.document.fields[0];
    EXPECT_EQ(field.id, id);
    EXPECT_TRUE(tableOfContentsField(field.instruction));
    const auto start = e.document.resolve(field.start), end = e.document.resolve(field.end);
    ASSERT_TRUE(start && end);
    EXPECT_EQ(start->paragraph, 0u);
    EXPECT_EQ(end->paragraph, 1u);
    EXPECT_EQ(e.document.paragraphs[0].text().substr(0, 12), u"Chapter one\t");
    EXPECT_EQ(e.document.paragraphs[1].text().substr(0, 10), u"Section A\t");
    EXPECT_EQ(e.document.paragraphs[1].indentLeft, 18);
    EXPECT_EQ(e.document.paragraphs[2].text(), u"Intro text"); // the table has its own lines
    EXPECT_NO_THROW(validateDocument(e.document));
    // A new heading appears after an update; one undo step removes the update.
    e.selection = {{2, 0}, {2, 0}};
    e.insertParagraphBreak();
    e.selection = {{2, 0}, {2, 0}};
    e.replace(u"Preface");
    e.applyStyle("Heading1");
    EXPECT_TRUE(e.updateTablesOfContents(pageOf));
    const auto updated = e.document.resolve(e.document.fields[0].end);
    ASSERT_TRUE(updated);
    EXPECT_EQ(updated->paragraph, 2u);
    EXPECT_EQ(e.document.paragraphs[0].text().substr(0, 8), u"Preface\t");
    e.undo();
    EXPECT_EQ(e.document.paragraphs[0].text().substr(0, 12), u"Chapter one\t");
    // Without headings the table says so.
    Editor empty;
    empty.insertTableOfContents();
    EXPECT_EQ(empty.document.paragraphs[0].text(), u"No table of contents entries found.");
}
TEST(Fields, MustNestAndHaveValidInstructions) {
    Editor e;
    e.replace(u"abcdefghij");
    auto document = e.document;
    const auto id = document.paragraphs[0].id;
    document.nextId += 3;
    const auto a = document.nextId - 3, b = document.nextId - 2;
    document.fields = {{a, "PAGE", {id, 0, Affinity::Before}, {id, 6, Affinity::After}},
                       {b, "PAGE", {id, 2, Affinity::Before}, {id, 4, Affinity::After}}};
    EXPECT_NO_THROW(validateDocument(document)); // nested
    document.fields[1].end.offset = 8;
    EXPECT_THROW(validateDocument(document), std::invalid_argument); // crossing
    document.fields[1] = {b, "PAGE", {id, 6, Affinity::Before}, {id, 8, Affinity::After}};
    EXPECT_NO_THROW(validateDocument(document)); // adjacent
    document.fields[1].instruction = "bad\tinstruction";
    EXPECT_THROW(validateDocument(document), std::invalid_argument);
}
TEST(Sections, InsertSetAndRemoveBreaks) {
    Editor e;
    e.replace(u"first second");
    e.selection = {{0, 5}, {0, 5}};
    e.insertSectionBreak(false);
    ASSERT_EQ(e.document.paragraphs.size(), 2u);
    EXPECT_EQ(e.document.paragraphs[0].text(), u"first");
    ASSERT_TRUE(e.document.paragraphs[0].sectionBreak);
    EXPECT_FALSE(e.document.paragraphs[1].sectionBreak);
    // Page settings go to the caret's section only.
    e.selection = {{0, 0}, {0, 0}};
    auto page = e.sectionPage();
    page.columns = 3;
    page.header.center = u"{PAGE}";
    e.setPage(page);
    EXPECT_EQ(e.document.paragraphs[0].sectionBreak->page.columns, 3u);
    EXPECT_EQ(e.document.page.columns, 1u);
    EXPECT_EQ(e.document.page.header.center, u"{PAGE}"); // headers stay document-wide
    EXPECT_TRUE(e.document.paragraphs[0].sectionBreak->page.header.empty());
    EXPECT_EQ(sectionPage(e.document, 1).columns, 1u);
    EXPECT_EQ(sectionPage(e.document, 0).header.center, u"{PAGE}");
    EXPECT_NO_THROW(validateDocument(e.document));
    // Joining the paragraphs keeps the surviving mark's (the second's) settings: no break.
    e.selection = {{0, 5}, {1, 0}};
    e.replace(u" ");
    EXPECT_EQ(e.document.paragraphs.size(), 1u);
    EXPECT_FALSE(e.document.paragraphs[0].sectionBreak);
    e.undo();
    ASSERT_TRUE(e.document.paragraphs[0].sectionBreak);
    e.selection = {{0, 2}, {0, 2}};
    EXPECT_TRUE(e.removeSectionBreak());
    EXPECT_FALSE(e.document.paragraphs[0].sectionBreak);
    EXPECT_FALSE(e.removeSectionBreak());
    // Invalid: a break inside a table, or one that carries its own header.
    auto document = e.document;
    document.paragraphs.edit(0).sectionBreak = SectionBreak{};
    document.paragraphs.edit(0).sectionBreak->page.header.left = u"x";
    EXPECT_THROW(validateDocument(document), std::invalid_argument);
    page = PageStyle{};
    page.columns = 7;
    EXPECT_THROW(validatePage(page), std::invalid_argument);
}
TEST(Lists, NumbersLevelsFormatsAndRestartsLikeWord) {
    EXPECT_EQ(formatListNumber(28, NumberFormat::LowerLetter), u"bb");
    EXPECT_EQ(formatListNumber(14, NumberFormat::UpperRoman), u"XIV");
    EXPECT_EQ(formatListNumber(9, NumberFormat::Decimal), u"9");
    // Wordforge's own lists: 1. a. i. by level; restarted outside a list and per container.
    ListDefinitions none;
    ListNumbering own(none);
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 0, 0), u"1.");
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 1, 0), u"a.");
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 1, 0), u"b.");
    EXPECT_EQ(own.next(ListKind::Bullet, 0, 2, 0), u"▪");
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 0, 0), u"2.");
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 1, 0), u"a."); // restarted under a new parent
    EXPECT_EQ(own.next(ListKind::None, 0, 0, 0), u"");
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 0, 0), u"1.");
    EXPECT_EQ(own.next(ListKind::Decimal, 0, 0, 7), u"1."); // a new table cell
    // Definitions: labels with several levels, starts, and numbering that continues past
    // other paragraphs; two definitions count separately.
    ListDefinition outline{5, {}};
    outline.levels[0] = {NumberFormat::UpperRoman, u"%1.", 3};
    outline.levels[1] = {NumberFormat::Decimal, u"%1.%2)", 1};
    ListDefinition other{6, {}};
    other.levels[0] = {NumberFormat::LowerLetter, u"(%1)", 1};
    ListDefinitions lists{outline, other};
    ListNumbering defined(lists);
    EXPECT_EQ(defined.next(ListKind::Decimal, 5, 0, 0), u"III.");
    EXPECT_EQ(defined.next(ListKind::Decimal, 5, 1, 0), u"III.1)");
    EXPECT_EQ(defined.next(ListKind::Decimal, 6, 0, 0), u"(a)");
    EXPECT_EQ(defined.next(ListKind::None, 0, 0, 0), u"");
    EXPECT_EQ(defined.next(ListKind::Decimal, 5, 1, 0), u"III.2)");
    EXPECT_EQ(defined.next(ListKind::Decimal, 5, 0, 0), u"IV.");
    EXPECT_EQ(defined.next(ListKind::Decimal, 6, 0, 0), u"(b)");
}
TEST(Lists, ValidatesLevelsDefinitionsAndPastedReferences) {
    Editor e;
    e.replace(u"item");
    ListDefinition list{e.document.allocateId(), {}};
    list.levels[0] = {NumberFormat::Bullet, u"–", 1};
    e.document.lists.push_back(list);
    e.formatParagraph([&](auto &p) {
        p.list = ListKind::Bullet;
        p.listId = list.id;
        p.listLevel = 2;
    });
    validateDocument(e.document);
    auto bad = e.document;
    bad.paragraphs.edit(0).listId = 999;
    EXPECT_THROW(validateDocument(bad), std::invalid_argument);
    bad = e.document;
    bad.paragraphs.edit(0).listLevel = 9;
    EXPECT_THROW(validateDocument(bad), std::invalid_argument);
    bad = e.document;
    bad.lists[0].levels[3].start = 40000;
    EXPECT_THROW(validateDocument(bad), std::invalid_argument);
    // Pasted into a document without that list, the paragraph joins its own list.
    e.selection = {{0, 0}, {0, 4}};
    const auto fragment = e.copySelection();
    Editor target;
    target.replace(u"x\n");
    target.insertFragment(fragment);
    validateDocument(target.document);
    EXPECT_EQ(target.document.paragraphs[1].listId, 0u);
    EXPECT_EQ(target.document.paragraphs[1].list, ListKind::Bullet);
    // Enter on an empty list item leaves the list entirely.
    e.selection = {{0, 0}, {0, 4}};
    e.replace(u"");
    e.insertParagraphBreak();
    EXPECT_EQ(e.document.paragraphs[0].list, ListKind::None);
    EXPECT_EQ(e.document.paragraphs[0].listId, 0u);
    EXPECT_EQ(e.document.paragraphs[0].listLevel, 0);
}
