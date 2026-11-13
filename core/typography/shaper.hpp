#pragma once
#include "core/document/document.hpp"
#include <memory>

namespace wf {
struct Glyph {
    std::uint16_t id;
    std::size_t cluster;
    double x, y, advance;
};
struct GlyphRun {
    std::string fontPath;
    int faceIndex = 0;
    CharacterStyle style;
    std::size_t start = 0, end = 0;
    bool rtl = false;
    double x = 0, width = 0, ascent = 0, descent = 0;
    std::vector<Glyph> glyphs;
    std::optional<InlineImage> image;
};
struct ShapedLine {
    std::vector<GlyphRun> runs;
    double width = 0, ascent = 0, descent = 0;
};
class Shaper {
  public:
    explicit Shaper(const std::vector<std::string> &fontFiles);
    ~Shaper();
    Shaper(const Shaper &) = delete;
    Shaper &operator=(const Shaper &) = delete;
    ShapedLine shape(const Paragraph &, std::size_t start, std::size_t end);
    std::vector<std::string> families() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::vector<std::string> systemFontFiles();
} // namespace wf
