#pragma once
#include "core/typography/shaper.hpp"
#include <unordered_map>
namespace wf {
constexpr std::size_t noBox = static_cast<std::size_t>(-1);
struct Line {
    std::size_t paragraph = 0, start = 0, end = 0, page = 0;
    std::size_t box = noBox; // index into Layout::cells for table text
    double x = 0, baseline = 0, height = 0;
    ShapedLine shaped;
    ShapedLine marker;
    std::u16string markerText;
    double markerX = 0;
    std::vector<std::pair<std::size_t, double>> caretStops;
};
struct CellRectangle {
    double x, y, width, height;
    std::size_t page, firstParagraph, lastParagraph;
    std::uint32_t fill = 0; // background, 0 for none
    TableBorder border;
};
struct Layout {
    std::vector<Line> lines;
    std::vector<CellRectangle> cells;
    // Header, footer and footnote lines, sorted by page; not hit-tested.
    std::vector<Line> furniture;
    // The rule above each page's footnotes: (page, y).
    std::vector<std::pair<std::size_t, double>> noteSeparators;
    std::size_t pageCount = 1, measuredParagraphs = 0;
    // Steps (paragraphs or table row groups) paginated by this composition; an incremental
    // one skips the unchanged ones before and after an edit.
    std::size_t composedParagraphs = 0;
    // Identifies this result to the engine that made it, for incremental composition.
    std::uint64_t generation = 0;
    // One per page. A page keeps its stamp while an incremental composition reuses it
    // unchanged (even at a new index); recomposed pages get new stamps. Equal stamps mean
    // equal content, so renderers can skip comparing those pages.
    std::vector<std::uint64_t> pageStamps;
    PageStyle page; // the first page's
    // Each page's size, margins and columns (sections can differ); pageCount entries.
    std::vector<PageStyle> pageStyles;
    const PageStyle &pageStyle(std::size_t index) const {
        return index < pageStyles.size() ? pageStyles[index] : page;
    }
    ImageResources images;
    Position hitTest(double x, double y, std::size_t pageIndex) const;
    struct Caret {
        double x, y, height;
        std::size_t page;
    };
    Caret caret(Position) const;
};
class LayoutEngine {
  public:
    explicit LayoutEngine(Shaper &shaper) : shaper_(shaper) {}
    Layout compose(const Document &);
    // The same result as compose(document), recomposed from the earliest paragraph whose
    // pagination inputs changed and reusing `previous` (this engine's latest result) before
    // that point and, where pagination converges again, after it. Any other `previous`
    // gives a full composition. `previous` is consumed either way.
    Layout compose(const Document &, Layout &&previous);
    void clear() {
        cache_.clear();
        generation_ = 0;
    }

  private:
    struct Cached {
        std::uint64_t revision;
        double width;
        std::vector<Line> lines;
        std::size_t number = 0, firstNote = 1, firstEndnote = 1;
    };
    struct NoteBlock {
        std::uint64_t id = 0, text = 0; // the note and a hash of its text
        std::size_t number = 0;
        std::vector<Line> lines;
        double height = 0;
    };
    // Pagination state where a paragraph (or a table's row group) starts.
    struct State {
        std::size_t pageIndex = 0, column = 0, listNumber = 0, notesSoFar = 0, endnotes = 0;
        double y = 0, sectionTop = 0, sectionBottom = 0, notesHeight = 0;
        std::vector<NoteBlock> pageNotes; // footnotes waiting for the bottom of this page
    };
    struct Checkpoint {
        bool valid = false; // false inside a table row group
        State state;
        std::size_t lines = 0, cells = 0, furniture = 0, separators = 0; // output so far
    };
    Layout paginate(const Document &, Layout *previous);
    Shaper &shaper_;
    std::unordered_map<std::uint64_t, Cached> cache_;
    // Retained from the latest composition: each paragraph's pagination key and id, the
    // checkpoints (one per paragraph plus the state after the last), endnote order.
    std::uint64_t generation_ = 0, globalKey_ = 0, bandKey_ = 0;
    std::vector<std::uint64_t> keys_, ids_, endnoteIds_;
    std::vector<Checkpoint> checkpoints_;
    // Each paragraph's own pagination inputs, kept for the paragraphs of the latest
    // composition (shared paragraphs are unchanged, so their entries carry over).
    struct Local {
        std::uint64_t key = 0, id = 0, tableId = 0;
        bool sectionBreak = false, cell = false, notes = false;
    };
    ParagraphList paragraphs_;
    std::vector<Local> locals_;
};
} // namespace wf
