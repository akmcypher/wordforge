// Incremental composition must give exactly what a fresh full composition gives.
#include "core/layout/layout.hpp"
#include <gtest/gtest.h>
#include <map>
#include <random>
using namespace wf;
namespace {
Shaper &sharedShaper() {
    static Shaper shaper(systemFontFiles());
    return shaper;
}
void expectSameLine(const Line &a, const Line &b, const char *what, std::size_t i) {
    SCOPED_TRACE(testing::Message() << what << ' ' << i);
    EXPECT_EQ(a.paragraph, b.paragraph);
    EXPECT_EQ(a.start, b.start);
    EXPECT_EQ(a.end, b.end);
    EXPECT_EQ(a.page, b.page);
    EXPECT_EQ(a.box, b.box);
    EXPECT_DOUBLE_EQ(a.x, b.x);
    EXPECT_DOUBLE_EQ(a.baseline, b.baseline);
    EXPECT_DOUBLE_EQ(a.height, b.height);
    EXPECT_DOUBLE_EQ(a.shaped.width, b.shaped.width);
    EXPECT_EQ(a.shaped.runs.size(), b.shaped.runs.size());
    EXPECT_EQ(a.markerText, b.markerText);
    EXPECT_DOUBLE_EQ(a.markerX, b.markerX);
}
// Every observable part of two layouts matches.
void expectSame(const Layout &a, const Layout &b) {
    ASSERT_EQ(a.pageCount, b.pageCount);
    ASSERT_EQ(a.pageStyles, b.pageStyles);
    EXPECT_EQ(a.page, b.page);
    ASSERT_EQ(a.lines.size(), b.lines.size());
    for (std::size_t i = 0; i < a.lines.size(); ++i)
        expectSameLine(a.lines[i], b.lines[i], "line", i);
    ASSERT_EQ(a.furniture.size(), b.furniture.size());
    for (std::size_t i = 0; i < a.furniture.size(); ++i)
        expectSameLine(a.furniture[i], b.furniture[i], "furniture", i);
    ASSERT_EQ(a.cells.size(), b.cells.size());
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        SCOPED_TRACE(testing::Message() << "cell " << i);
        EXPECT_DOUBLE_EQ(a.cells[i].x, b.cells[i].x);
        EXPECT_DOUBLE_EQ(a.cells[i].y, b.cells[i].y);
        EXPECT_DOUBLE_EQ(a.cells[i].width, b.cells[i].width);
        EXPECT_DOUBLE_EQ(a.cells[i].height, b.cells[i].height);
        EXPECT_EQ(a.cells[i].page, b.cells[i].page);
        EXPECT_EQ(a.cells[i].firstParagraph, b.cells[i].firstParagraph);
        EXPECT_EQ(a.cells[i].lastParagraph, b.cells[i].lastParagraph);
        EXPECT_EQ(a.cells[i].fill, b.cells[i].fill);
    }
    EXPECT_EQ(a.noteSeparators, b.noteSeparators);
}
// What is drawn on `page`, independent of paragraph and page indices.
std::vector<double> pageContent(const Layout &layout, std::size_t page) {
    std::vector<double> content;
    const auto &style = layout.pageStyle(page);
    content.insert(content.end(), {style.width, style.height, style.margin});
    for (const auto *lines : {&layout.lines, &layout.furniture})
        for (const auto &line : *lines)
            if (line.page == page)
                content.insert(content.end(),
                               {line.x, line.baseline, line.height, line.shaped.width,
                                static_cast<double>(line.start), static_cast<double>(line.end),
                                static_cast<double>(line.shaped.runs.size()), line.markerX});
    for (const auto &cell : layout.cells)
        if (cell.page == page)
            content.insert(content.end(), {cell.x, cell.y, cell.width, cell.height,
                                           static_cast<double>(cell.fill)});
    for (const auto &[at, y] : layout.noteSeparators)
        if (at == page)
            content.push_back(y);
    return content;
}
// An editing session that recomposes incrementally after each change and checks the
// result against a fresh engine (the full-layout oracle).
struct Session {
    Editor editor;
    LayoutEngine engine{sharedShaper()};
    Layout layout;
    explicit Session(Editor e) : editor(std::move(e)) {
        layout = engine.compose(editor.document);
    }
    std::size_t reusedPages = 0;
    std::size_t edit(const std::function<void(Editor &)> &change) {
        change(editor);
        const auto before = layout;
        layout = engine.compose(editor.document, std::move(layout));
        LayoutEngine oracle(sharedShaper());
        expectSame(layout, oracle.compose(editor.document));
        // A page keeping its stamp must look exactly as the page that had it.
        EXPECT_EQ(layout.pageStamps.size(), layout.pageCount);
        std::map<std::uint64_t, std::size_t> stamps;
        for (std::size_t page = 0; page < before.pageStamps.size(); ++page)
            stamps.emplace(before.pageStamps[page], page);
        reusedPages = 0;
        for (std::size_t page = 0; page < layout.pageCount; ++page)
            if (const auto found = stamps.find(layout.pageStamps[page]); found != stamps.end()) {
                ++reusedPages;
                EXPECT_EQ(pageContent(layout, page), pageContent(before, found->second))
                    << "page " << page << " reused from " << found->second;
            }
        return layout.composedParagraphs;
    }
};
std::u16string sentence(std::size_t n) {
    static const char16_t *words[] = {u"alpha", u"beta", u"gamma", u"delta", u"epsilon",
                                      u"zeta",  u"eta",  u"theta", u"iota",  u"kappa"};
    std::u16string text;
    for (std::size_t k = 0; k < 6 + n % 30; ++k) {
        if (k)
            text += u' ';
        text += words[(n * 7 + k * 3) % 10];
    }
    return text;
}
Editor longDocument(std::size_t paragraphs) {
    std::u16string text;
    for (std::size_t n = 0; n < paragraphs; ++n) {
        if (n)
            text += u'\n';
        text += sentence(n);
    }
    Editor e;
    e.replace(text);
    return e;
}
void caret(Editor &e, std::size_t paragraph, std::size_t offset = 0) {
    e.selection = {{paragraph, offset}, {paragraph, offset}};
}
} // namespace

TEST(IncrementalLayout, TypingEarlyMiddleAndLateMatchesFullLayout) {
    Session s(longDocument(400));
    ASSERT_GT(s.layout.pageCount, 10u);
    const auto count = s.editor.document.paragraphs.size();
    for (const auto at : {std::size_t{0}, count / 2, count - 1}) {
        SCOPED_TRACE(at);
        s.edit([&](Editor &e) {
            caret(e, at);
            e.replace(u"x");
        });
        s.edit([&](Editor &e) {
            caret(e, at);
            e.replace(u"several more words that wrap onto another line of this paragraph ");
        });
        s.edit([&](Editor &e) {
            caret(e, at, 3);
            e.eraseForward();
        });
    }
}
TEST(IncrementalLayout, MiddleEditRecomposesOnlyNearbyParagraphs) {
    Session s(longDocument(600));
    const auto middle = s.editor.document.paragraphs.size() / 2;
    const auto steps = s.edit([&](Editor &e) {
        caret(e, middle);
        e.replace(u"y");
    });
    EXPECT_LT(steps, 10u);
    EXPECT_GT(steps, 0u);
    // New stamps only for the edited page and, at most, the page where pagination converged.
    EXPECT_GE(s.reusedPages + 2, s.layout.pageCount);
    // A line more in one paragraph shifts the paragraphs after it until a page boundary
    // absorbs the difference (how far depends on the fonts), then converges.
    const auto grown = s.edit([&](Editor &e) {
        caret(e, middle);
        e.replace(u"a longer insertion that certainly needs at least one more line to fit ");
    });
    EXPECT_LT(grown, s.editor.document.paragraphs.size() / 3);
}
TEST(IncrementalLayout, ParagraphInsertionAndDeletionShiftTheReusedTail) {
    Session s(longDocument(300));
    const auto middle = s.editor.document.paragraphs.size() / 2;
    s.edit([&](Editor &e) {
        caret(e, middle, 4);
        e.insertParagraphBreak();
    });
    s.edit([&](Editor &e) {
        e.selection = {{middle - 3, 2}, {middle + 4, 1}};
        e.eraseBackward();
    });
    s.edit([](Editor &e) { e.undo(); });
    s.edit([](Editor &e) { e.undo(); });
    s.edit([](Editor &e) { e.redo(); });
    s.edit([&](Editor &e) {
        caret(e, 10);
        e.insertPageBreak();
    });
}
TEST(IncrementalLayout, KeepsWidowControlListsAndHeadingsMatchFullLayout) {
    Session s(longDocument(250));
    s.edit([](Editor &e) {
        e.selection = {{0, 0}, {e.document.paragraphs.size() - 1, 0}};
        e.formatParagraph([](ParagraphFormat &p) { p.widowControl = true; });
    });
    for (std::size_t at = 5; at < 240; at += 23)
        s.edit([&](Editor &e) {
            caret(e, at);
            e.applyPreset(ParagraphPreset::Heading1);
            e.formatParagraph([](ParagraphFormat &p) { p.keepWithNext = true; });
        });
    s.edit([](Editor &e) {
        e.selection = {{60, 0}, {75, 0}};
        e.formatParagraph([](ParagraphFormat &p) {
            p.list = ListKind::Decimal;
            p.keepLines = true;
        });
    });
    for (const auto at : {std::size_t{4}, std::size_t{61}, std::size_t{120}, std::size_t{246}})
        s.edit([&](Editor &e) {
            caret(e, at);
            e.replace(u"words added to change how this paragraph and its neighbours paginate ");
        });
}
TEST(IncrementalLayout, ContextualAndCharacterSpacingMatchFullLayout) {
    Session s(longDocument(150));
    s.edit([](Editor &e) {
        e.selection = {{0, 0}, {e.document.paragraphs.size() - 1, 0}};
        e.formatParagraph([](ParagraphFormat &p) {
            p.contextualSpacing = true;
            p.spaceBefore = 12;
            p.spaceAfter = 18;
        });
    });
    for (std::size_t at = 3; at < 145; at += 17)
        s.edit([&](Editor &e) {
            caret(e, at);
            e.applyPreset(ParagraphPreset::Heading1);
        });
    s.edit([](Editor &e) {
        caret(e, 20);
        e.applyPreset(ParagraphPreset::Normal);
    });
    s.edit([](Editor &e) {
        caret(e, 50);
        e.formatParagraph([](ParagraphFormat &p) { p.contextualSpacing = false; });
    });
    s.edit([](Editor &e) {
        e.selection = {{10, 0}, {12, 5}};
        e.format([](CharacterStyle &c) { c.spacing = 3; });
    });
}
TEST(IncrementalLayout, FootnotesEndnotesAndHeadersMatchFullLayout) {
    Session s(longDocument(200));
    s.edit([](Editor &e) {
        auto page = e.document.page;
        page.header.center = u"Page {PAGE} of {NUMPAGES}";
        page.footer.right = u"Draft";
        e.setPage(page);
    });
    std::vector<std::uint64_t> notes;
    for (std::size_t at = 3; at < 190; at += 31)
        s.edit([&](Editor &e) {
            caret(e, at, 2);
            notes.push_back(e.insertFootnote(u"A footnote long enough to take two lines at the "
                                             u"bottom of the page where it is referenced."));
        });
    s.edit([](Editor &e) {
        caret(e, 150, 1);
        e.insertFootnote(u"An endnote.", true);
    });
    s.edit([&](Editor &e) { e.setFootnoteText(notes[2], u"Shorter."); });
    for (const auto at : {std::size_t{1}, std::size_t{95}, std::size_t{180}})
        s.edit([&](Editor &e) {
            caret(e, at);
            e.replace(u"text that moves the following references onto other lines and pages ");
        });
}
TEST(IncrementalLayout, TablesSectionsAndColumnsMatchFullLayout) {
    Session s(longDocument(180));
    s.edit([](Editor &e) {
        caret(e, 40);
        e.insertTable(4, 3);
    });
    s.edit([](Editor &e) {
        caret(e, 90);
        e.insertSectionBreak(true);
    });
    s.edit([](Editor &e) {
        caret(e, 100);
        auto page = e.sectionPage();
        page.columns = 2;
        e.setPage(page);
    });
    s.edit([](Editor &e) {
        caret(e, 130);
        e.insertSectionBreak(false);
    });
    s.edit([](Editor &e) {
        caret(e, 150);
        auto page = e.sectionPage();
        std::swap(page.width, page.height);
        e.setPage(page);
    });
    for (const auto at : {std::size_t{2}, std::size_t{41}, std::size_t{95}, std::size_t{140}})
        s.edit([&](Editor &e) {
            caret(e, at);
            e.replace(u"inserted words inside a table cell, a column or a landscape section ");
        });
}
TEST(IncrementalLayout, RandomEditsMatchFullLayout) {
    Session s(longDocument(220));
    s.edit([](Editor &e) {
        caret(e, 30, 1);
        e.insertFootnote(u"Random footnote.");
        caret(e, 70);
        e.insertTable(2, 2);
        caret(e, 120);
        e.formatParagraph([](ParagraphFormat &p) { p.keepWithNext = true; });
    });
    std::mt19937 random(20261123);
    for (int round = 0; round < 150; ++round) {
        SCOPED_TRACE(round);
        s.edit([&](Editor &e) {
            const auto paragraph = random() % e.document.paragraphs.size();
            const auto length = e.document.paragraphs[paragraph].text().size();
            caret(e, paragraph, length ? random() % (length + 1) : 0);
            switch (random() % 6) {
            case 0:
                e.insertParagraphBreak();
                break;
            case 1:
                e.eraseBackward();
                break;
            case 2:
                e.undo();
                break;
            case 3:
                e.formatParagraph([&](ParagraphFormat &p) { p.spaceAfter = random() % 30; });
                break;
            default:
                e.replace(sentence(random() % 40) + u" ");
            }
        });
        if (HasFatalFailure())
            return;
    }
}
TEST(IncrementalLayout, ForeignOrStaleLayoutFallsBackToFullComposition) {
    Session s(longDocument(50));
    LayoutEngine other(sharedShaper());
    auto foreign = other.compose(s.editor.document);
    s.editor.selection = {{20, 0}, {20, 0}};
    s.editor.replace(u"z");
    const auto layout = s.engine.compose(s.editor.document, std::move(foreign));
    EXPECT_EQ(layout.composedParagraphs, s.editor.document.paragraphs.size());
    LayoutEngine oracle(sharedShaper());
    expectSame(layout, oracle.compose(s.editor.document));
}
