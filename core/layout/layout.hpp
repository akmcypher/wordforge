#pragma once
#include "core/typography/shaper.hpp"
#include <unordered_map>
namespace wf {
struct Line {
    std::size_t paragraph = 0, start = 0, end = 0, page = 0;
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
};
struct Layout {
    std::vector<Line> lines;
    std::vector<CellRectangle> cells;
    std::size_t pageCount = 1, measuredParagraphs = 0;
    PageStyle page;
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
    void clear() {
        cache_.clear();
    }

  private:
    struct Cached {
        std::uint64_t revision;
        double width;
        std::vector<Line> lines;
        std::size_t number = 0;
    };
    Shaper &shaper_;
    std::unordered_map<std::uint64_t, Cached> cache_;
};
} // namespace wf
