#include "core/rendering/page_renderer.hpp"
#include <algorithm>
#include <include/codec/SkPngDecoder.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkData.h>
#include <include/core/SkFont.h>
#include <include/core/SkFontMgr.h>
#include <include/core/SkImage.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkTypeface.h>
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
template <class Callback> void paintRuns(const Line &line, Callback callback) {
    for (const auto &run : line.marker.runs)
        callback(run, line.markerX + run.x);
    for (const auto &run : line.shaped.runs)
        callback(run, line.x + run.x);
}
std::string pageKey(const Layout &layout, std::size_t page) {
    std::string key;
    keyValue(key, layout.page.width);
    keyValue(key, layout.page.height);
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
        keyValue(key, cell.border.width);
        keyValue(key, cell.border.color);
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
            keyValue(key, run.style.color);
            keyValue(key, run.style.underline);
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
    impl_->pages.resize(layout.pageCount);
    for (std::size_t page = 0; page < layout.pageCount; ++page) {
        auto key = pageKey(layout, page);
        auto &cached = impl_->pages[page];
        if (cached.picture && cached.key == key)
            continue;
        SkPictureRecorder recorder;
        auto *canvas = recorder.beginRecording(static_cast<float>(layout.page.width),
                                               static_cast<float>(layout.page.height));
        canvas->clear(SK_ColorWHITE);
        // Cell fills first, then every border over them.
        for (const auto &cell : layout.cells)
            if (cell.page == page && cell.fill) {
                SkPaint fill;
                fill.setColor(cell.fill);
                canvas->drawRect(SkRect::MakeXYWH(cell.x, cell.y, cell.width, cell.height), fill);
            }
        for (const auto &cell : layout.cells)
            if (cell.page == page && cell.border.width > 0) {
                SkPaint border;
                border.setAntiAlias(true);
                border.setColor(cell.border.color);
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(static_cast<float>(cell.border.width));
                canvas->drawRect(SkRect::MakeXYWH(cell.x, cell.y, cell.width, cell.height), border);
            }
        for (const auto &[notePage, noteY] : layout.noteSeparators)
            if (notePage == page) {
                SkPaint rule;
                rule.setAntiAlias(true);
                rule.setColor(0xff8a8f99);
                rule.setStrokeWidth(0.6f);
                const auto left = static_cast<float>(layout.page.margin);
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
                if (run.style.underline) {
                    paint.setStrokeWidth(std::max(0.6f, static_cast<float>(run.style.size / 16)));
                    canvas->drawLine(static_cast<float>(x), static_cast<float>(line.baseline + 2),
                                     static_cast<float>(x + run.width),
                                     static_cast<float>(line.baseline + 2), paint);
                }
                if (run.style.strike) {
                    paint.setStrokeWidth(std::max(0.6f, static_cast<float>(run.style.size / 16)));
                    const auto y = static_cast<float>(line.baseline - run.style.size * 0.32);
                    canvas->drawLine(x, y, x + run.width, y, paint);
                }
            });
        }
        cached = {std::move(key), recorder.finishRecordingAsPicture()};
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
