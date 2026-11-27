#include "core/rendering/page_renderer.hpp"
#include <unordered_map>
#include <algorithm>
#include <include/codec/SkPngDecoder.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkData.h>
#include <include/core/SkFont.h>
#include <include/core/SkFontMgr.h>
#include <include/core/SkImage.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPathBuilder.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkTypeface.h>
#include <include/effects/SkDashPathEffect.h>
#include <map>
#include <span>
#include <stdexcept>
#ifdef _WIN32
#include <include/ports/SkTypeface_win.h>
#else
#include <include/ports/SkFontMgr_fontconfig.h>
#include <include/ports/SkFontScanner_FreeType.h>
#endif
namespace wf {
namespace {
std::span<const Line> pageLines(const std::vector<Line> &lines, std::size_t page) {
    auto first =
        std::lower_bound(lines.begin(), lines.end(), page,
                         [](const Line &line, std::size_t index) { return line.page < index; });
    auto last =
        std::upper_bound(first, lines.end(), page,
                         [](std::size_t index, const Line &line) { return index < line.page; });
    return {first, last};
}
template <class T> void keyValue(std::string &key, const T &value) {
    key.append(reinterpret_cast<const char *>(&value), sizeof(value));
}
// An underline from `left` to `right` at `y`, in the run's underline style. `paint` carries
// the text colour; the dash effect and stroke settings are reset afterwards.
void drawUnderline(SkCanvas &canvas, SkPaint &paint, const CharacterStyle &style, float left,
                   float right, float y) {
    const float width = std::max(0.6f, static_cast<float>(style.size / 16));
    paint.setStrokeWidth(width);
    switch (style.underlineStyle) {
    case UnderlineStyle::Single:
        canvas.drawLine(left, y, right, y, paint);
        break;
    case UnderlineStyle::Thick:
        paint.setStrokeWidth(width * 2);
        canvas.drawLine(left, y + width / 2, right, y + width / 2, paint);
        break;
    case UnderlineStyle::Double:
        paint.setStrokeWidth(width * 0.75f);
        canvas.drawLine(left, y - width * 0.25f, right, y - width * 0.25f, paint);
        canvas.drawLine(left, y + width * 1.5f, right, y + width * 1.5f, paint);
        break;
    case UnderlineStyle::Dotted:
    case UnderlineStyle::Dashed: {
        const bool dots = style.underlineStyle == UnderlineStyle::Dotted;
        const SkScalar intervals[] = {dots ? width : std::max(3.0f, width * 4),
                                      dots ? width : std::max(2.0f, width * 2)};
        paint.setPathEffect(SkDashPathEffect::Make(intervals, 0));
        canvas.drawLine(left, y, right, y, paint);
        paint.setPathEffect(nullptr);
        break;
    }
    case UnderlineStyle::Wavy: {
        // Half-waves of four widths, one width and a half above and below the line.
        const float half = std::max(2.0f, width * 2), height = std::max(1.0f, width * 1.5f);
        SkPathBuilder wave;
        wave.moveTo(left, y);
        int n = 0;
        for (float x = left; x < right; x += half, ++n) {
            const float end = std::min(right, x + half);
            wave.quadTo((x + end) / 2, n % 2 ? y + height * 2 : y - height * 2, end, y);
        }
        paint.setStyle(SkPaint::kStroke_Style);
        canvas.drawPath(wave.detach(), paint);
        paint.setStyle(SkPaint::kFill_Style);
        break;
    }
    }
}
template <class Callback> void paintRuns(const Line &line, Callback callback) {
    for (const auto &run : line.marker.runs)
        callback(run, line.markerX + run.x);
    for (const auto &run : line.shaped.runs)
        callback(run, line.x + run.x);
}
std::string pageKey(const Layout &layout, std::size_t page) {
    std::string key;
    keyValue(key, layout.pageStyle(page).width);
    keyValue(key, layout.pageStyle(page).height);
    keyValue(key, layout.pageStyle(page).marginTop);
    keyValue(key, layout.pageStyle(page).marginBottom);
    keyValue(key, layout.pageStyle(page).marginLeft);
    keyValue(key, layout.pageStyle(page).marginRight);
    for (const auto &[notePage, noteY] : layout.noteSeparators)
        if (notePage == page)
            keyValue(key, noteY);
    for (const auto &cell : layout.cells) {
        if (cell.page != page)
            continue;
        keyValue(key, cell.x);
        keyValue(key, cell.y);
        keyValue(key, cell.width);
        keyValue(key, cell.height);
        keyValue(key, cell.fill);
        for (const auto &border : cell.borders) {
            keyValue(key, border.width);
            keyValue(key, border.color);
            keyValue(key, static_cast<std::uint8_t>(border.style));
        }
    }
    for (const auto *source : {&layout.lines, &layout.furniture})
    for (const auto &line : pageLines(*source, page)) {
        keyValue(key, line.x);
        keyValue(key, line.baseline);
        paintRuns(line, [&](const GlyphRun &run, double x) {
            key += run.fontPath;
            key += '\0';
            keyValue(key, run.faceIndex);
            keyValue(key, run.x);
            keyValue(key, x);
            keyValue(key, run.width);
            keyValue(key, run.style.size);
            keyValue(key, run.baselineOffset);
            keyValue(key, run.style.color);
            keyValue(key, run.style.underline);
            keyValue(key, static_cast<int>(run.style.underlineStyle));
            keyValue(key, run.style.strike);
            keyValue(key, run.style.highlight);
            if (run.image) {
                keyValue(key, run.image->resourceId);
                keyValue(key, run.image->width);
                keyValue(key, run.image->height);
                const auto found = layout.images.find(run.image->resourceId);
                if (found == layout.images.end() || !found->second)
                    throw std::runtime_error("Missing picture resource");
                keyValue(key, found->second.get());
            }
            for (const auto &glyph : run.glyphs) {
                keyValue(key, glyph.id);
                keyValue(key, glyph.x);
                keyValue(key, glyph.y);
            }
        });
    }
    return key;
}
} // namespace
struct PageRenderer::Impl {
    struct Page {
        std::string key;
        sk_sp<SkPicture> picture;
        std::uint64_t stamp = 0;
    };
    std::vector<Page> pages;
    sk_sp<SkFontMgr> fonts;
    std::map<std::pair<std::string, int>, sk_sp<SkTypeface>> typefaces;
    struct ImageEntry {
        std::shared_ptr<const ImageResource> resource;
        sk_sp<SkImage> image;
    };
    std::map<std::uint64_t, ImageEntry> images;
    Impl() {
#ifdef _WIN32
        fonts = SkFontMgr_New_DirectWrite();
#else
        fonts = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#endif
        if (!fonts)
            throw std::runtime_error("Cannot initialize Skia font manager");
    }
    sk_sp<SkTypeface> typeface(const GlyphRun &run) {
        const auto key = std::make_pair(run.fontPath, run.faceIndex);
        auto found = typefaces.find(key);
        if (found != typefaces.end())
            return found->second;
        auto face = fonts->makeFromFile(run.fontPath.c_str(), run.faceIndex);
        if (!face)
            throw std::runtime_error("Skia cannot load font: " + run.fontPath);
        typefaces.emplace(key, face);
        return face;
    }
    sk_sp<SkImage> image(std::uint64_t id, const ImageResources &resources) {
        const auto resource = resources.at(id);
        if (const auto found = images.find(id);
            found != images.end() && found->second.resource == resource)
            return found->second.image;
        validateImageResource(*resource);
        auto data = SkData::MakeWithCopy(resource->png.data(), resource->png.size());
        auto codec = SkPngDecoder::Decode(std::move(data), nullptr);
        if (!codec || codec->dimensions().width() != static_cast<int>(resource->pixelWidth) ||
            codec->dimensions().height() != static_cast<int>(resource->pixelHeight))
            throw std::runtime_error("Could not decode the picture");
        SkBitmap validation;
        const auto info =
            codec->getInfo().makeColorType(kN32_SkColorType).makeAlphaType(kPremul_SkAlphaType);
        if (!validation.tryAllocPixels(info) ||
            codec->getPixels(validation.info(), validation.getPixels(), validation.rowBytes()) !=
                SkCodec::kSuccess)
            throw std::runtime_error("Picture pixel data is corrupt or exceeds memory limits");
        auto image = SkCodecs::DeferredImage(std::move(codec));
        if (!image)
            throw std::runtime_error("Could not retain picture pixels");
        images.insert_or_assign(id, ImageEntry{resource, image});
        return image;
    }
};
PageRenderer::PageRenderer() : impl_(std::make_unique<Impl>()) {}
PageRenderer::~PageRenderer() = default;
std::size_t PageRenderer::synchronize(const Layout &layout) {
    std::erase_if(impl_->images, [&](const auto &entry) {
        const auto found = layout.images.find(entry.first);
        return found == layout.images.end() || found->second != entry.second.resource;
    });
    std::size_t rebuilt = 0;
    // Pages whose layout stamp is unchanged are reused without comparing their content,
    // even where an edit moved them to another index; other pages compare content keys.
    auto previous = std::move(impl_->pages);
    std::unordered_map<std::uint64_t, std::size_t> stamped;
    for (std::size_t i = 0; i < previous.size(); ++i)
        if (previous[i].stamp && previous[i].picture)
            stamped.emplace(previous[i].stamp, i);
    impl_->pages.assign(layout.pageCount, {});
    for (std::size_t page = 0; page < layout.pageCount; ++page) {
        const auto stamp = page < layout.pageStamps.size() ? layout.pageStamps[page] : 0;
        auto &cached = impl_->pages[page];
        if (const auto found = stamped.find(stamp); stamp && found != stamped.end()) {
            cached = std::move(previous[found->second]);
            stamped.erase(found);
            if (cached.picture)
                continue;
        }
        auto key = pageKey(layout, page);
        if (page < previous.size() && previous[page].picture && previous[page].key == key) {
            cached = {std::move(key), std::move(previous[page].picture), stamp};
            continue;
        }
        SkPictureRecorder recorder;
        auto *canvas = recorder.beginRecording(static_cast<float>(layout.pageStyle(page).width),
                                               static_cast<float>(layout.pageStyle(page).height));
        canvas->clear(SK_ColorWHITE);
        // Cell fills first, then every border over them.
        for (const auto &cell : layout.cells)
            if (cell.page == page && cell.fill) {
                SkPaint fill;
                fill.setColor(cell.fill);
                canvas->drawRect(SkRect::MakeXYWH(cell.x, cell.y, cell.width, cell.height), fill);
            }
        // Each side on its own; a shared side is drawn alike by both cells. Lines reach past
        // the corners by half their width so that sides meet like a rectangle's.
        for (const auto &cell : layout.cells)
            if (cell.page == page)
                for (int side = 0; side < 6; ++side) {
                    const auto &line = cell.borders[side];
                    if (line.width <= 0)
                        continue;
                    const auto width = static_cast<float>(line.width);
                    SkPaint border;
                    border.setAntiAlias(true);
                    border.setColor(line.color);
                    border.setStyle(SkPaint::kStroke_Style);
                    border.setStrokeWidth(width);
                    // Diagonals end at the corners; sides reach past them to meet.
                    border.setStrokeCap(side < 4 ? SkPaint::kSquare_Cap : SkPaint::kButt_Cap);
                    // Dashes and dots as Word draws them: dashes three widths long (at
                    // least 3 points), dots one width apart. Their ends are cut square.
                    if (line.style == BorderStyle::Dashed ||
                        line.style == BorderStyle::Dotted) {
                        const bool dashed = line.style == BorderStyle::Dashed;
                        const float intervals[] = {
                            dashed ? std::max(3.0f, 3 * width) : width,
                            dashed ? std::max(2.0f, 2 * width) : width};
                        border.setStrokeCap(SkPaint::kButt_Cap);
                        border.setPathEffect(SkDashPathEffect::Make(intervals, 0));
                    }
                    // A double line: two lines a third of the width each, a third apart.
                    const bool twin = line.style == BorderStyle::Double;
                    if (twin)
                        border.setStrokeWidth(width / 3);
                    for (const float offset :
                         {twin ? -width / 3 : 0.0f, twin ? width / 3 : 0.0f}) {
                        const auto left = static_cast<float>(cell.x),
                                   top = static_cast<float>(cell.y),
                                   right = static_cast<float>(cell.x + cell.width),
                                   bottom = static_cast<float>(cell.y + cell.height);
                        switch (side) {
                        case 0: canvas->drawLine(left, top + offset, right, top + offset, border); break;
                        case 1: canvas->drawLine(right + offset, top, right + offset, bottom, border); break;
                        case 2: canvas->drawLine(left, bottom + offset, right, bottom + offset, border); break;
                        case 4: canvas->drawLine(left, top + offset, right, bottom + offset, border); break;
                        case 5: canvas->drawLine(left, bottom + offset, right, top + offset, border); break;
                        default: canvas->drawLine(left + offset, top, left + offset, bottom, border); break;
                        }
                        if (!twin)
                            break;
                    }
                }
        for (const auto &[notePage, noteY] : layout.noteSeparators)
            if (notePage == page) {
                SkPaint rule;
                rule.setAntiAlias(true);
                rule.setColor(0xff8a8f99);
                rule.setStrokeWidth(0.6f);
                const auto left = static_cast<float>(layout.pageStyle(page).marginLeft);
                canvas->drawLine(left, static_cast<float>(noteY), left + 120,
                                 static_cast<float>(noteY), rule);
            }
        for (const auto *source : {&layout.lines, &layout.furniture})
        for (const auto &line : pageLines(*source, page)) {
            paintRuns(line, [&](const GlyphRun &run, double x) {
                if (run.style.highlight) {
                    SkPaint background;
                    background.setColor(run.style.highlight);
                    canvas->drawRect(SkRect::MakeXYWH(x, line.baseline - line.shaped.ascent,
                                                      run.width,
                                                      line.shaped.ascent + line.shaped.descent),
                                     background);
                }
            });
            paintRuns(line, [&](const GlyphRun &run, double x) {
                if (run.image) {
                    auto image = impl_->image(run.image->resourceId, layout.images);
                    canvas->drawImageRect(image.get(),
                                          SkRect::MakeXYWH(x, line.baseline - run.image->height,
                                                           run.image->width, run.image->height),
                                          SkSamplingOptions(SkFilterMode::kLinear), nullptr);
                    return;
                }
                SkFont font(impl_->typeface(run), static_cast<float>(run.style.size));
                font.setEdging(SkFont::Edging::kAntiAlias);
                font.setSubpixel(true);
                SkPaint paint;
                paint.setAntiAlias(true);
                paint.setColor(run.style.color);
                std::vector<SkGlyphID> glyphs;
                std::vector<SkPoint> positions;
                glyphs.reserve(run.glyphs.size());
                positions.reserve(run.glyphs.size());
                for (const auto &glyph : run.glyphs) {
                    glyphs.push_back(glyph.id);
                    positions.push_back(
                        SkPoint::Make(static_cast<float>(glyph.x), static_cast<float>(glyph.y)));
                }
                canvas->drawGlyphs(
                    {glyphs.data(), glyphs.size()}, {positions.data(), positions.size()},
                    SkPoint::Make(static_cast<float>(x), static_cast<float>(line.baseline)), font,
                    paint);
                if (run.style.underline)
                    drawUnderline(*canvas, paint, run.style, static_cast<float>(x),
                                  static_cast<float>(x + run.width),
                                  static_cast<float>(line.baseline + run.baselineOffset + 2));
                if (run.style.strike) {
                    paint.setStrokeWidth(std::max(0.6f, static_cast<float>(run.style.size / 16)));
                    const auto y = static_cast<float>(line.baseline + run.baselineOffset - run.style.size * 0.32);
                    canvas->drawLine(x, y, x + run.width, y, paint);
                }
            });
        }
        cached = {std::move(key), recorder.finishRecordingAsPicture(), stamp};
        ++rebuilt;
    }
    return rebuilt;
}
void PageRenderer::drawPage(SkCanvas &canvas, std::size_t page) const {
    if (page >= impl_->pages.size())
        throw std::out_of_range("Page outside retained scene");
    canvas.drawPicture(impl_->pages[page].picture);
}
void PageRenderer::drawSelection(SkCanvas &canvas, const Layout &layout, Selection selection,
                                 std::size_t page, std::uint32_t color) const {
    const auto start = std::min(selection.anchor, selection.focus),
               end = std::max(selection.anchor, selection.focus);
    if (start == end)
        return;
    SkPaint paint;
    paint.setColor(color);
    for (const auto &line : layout.lines)
        if (line.page == page && line.paragraph >= start.paragraph &&
            line.paragraph <= end.paragraph) {
            const auto a = line.paragraph == start.paragraph ? start.offset : line.start;
            const auto b = line.paragraph == end.paragraph ? end.offset : line.end;
            for (std::size_t i = 1; i < line.caretStops.size(); ++i) {
                auto [left, x1] = line.caretStops[i - 1];
                auto [right, x2] = line.caretStops[i];
                if (left >= a && right <= b && right > left)
                    canvas.drawRect(
                        SkRect::MakeXYWH(static_cast<float>(line.x + std::min(x1, x2)),
                                         static_cast<float>(line.baseline - line.shaped.ascent),
                                         static_cast<float>(std::abs(x2 - x1)),
                                         static_cast<float>(line.height)),
                        paint);
            }
        }
}
} // namespace wf
