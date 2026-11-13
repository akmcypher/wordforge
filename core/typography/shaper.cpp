#include "core/typography/shaper.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <ft2build.h>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <unicode/ubidi.h>
#include <unicode/uscript.h>
#include <unicode/utf16.h>
namespace wf {
namespace {
std::string lower(std::string value) {
    for (auto &ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
using Bidi = std::unique_ptr<UBiDi, decltype(&ubidi_close)>;
using Buffer = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>;
using HbFont = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
} // namespace
// Fallback order when a requested family is not installed (e.g. Calibri in a DOCX).
constexpr const char *preferredFallbacks[] = {"arial", "liberation sans", "dejavu sans",
                                              "noto sans", "times new roman"};
struct Shaper::Impl {
    // Faces are catalogued at startup but opened only when text needs them, so that
    // hundreds of installed fonts cost little memory and few open files.
    struct Face {
        FT_Face face = nullptr;
        std::string path, family, name;
        int index = 0;
        bool bold = false, italic = false, broken = false;
    };
    FT_Library library = nullptr;
    std::vector<Face> faces;
    using FontChoice = std::tuple<std::string, bool, bool, std::u16string>;
    std::map<FontChoice, std::size_t> choices;
    ~Impl() {
        for (auto &f : faces)
            if (f.face)
                FT_Done_Face(f.face);
        if (library)
            FT_Done_FreeType(library);
    }
    bool open(Face &f) {
        if (!f.face && !f.broken && FT_New_Face(library, f.path.c_str(), f.index, &f.face)) {
            f.face = nullptr;
            f.broken = true;
        }
        return f.face;
    }
    Face &select(const std::u16string &text, std::size_t begin, std::size_t end,
                 const CharacterStyle &style) {
        FontChoice key{lower(style.family), style.bold, style.italic,
                       text.substr(begin, end - begin)};
        if (auto found = choices.find(key); found != choices.end())
            return faces[found->second];
        // Rank by family and style without opening anything, then check glyph coverage
        // in that order; the first face that covers the text cannot be beaten.
        std::vector<std::pair<int, std::size_t>> ranked;
        ranked.reserve(faces.size());
        for (std::size_t i = 0; i < faces.size(); ++i) {
            const auto &candidate = faces[i];
            int score = 100;
            if (candidate.family == std::get<0>(key))
                score = 0;
            else
                for (int p = 0; p < int(std::size(preferredFallbacks)); ++p)
                    if (candidate.family == preferredFallbacks[p])
                        score = 50 + p;
            if (candidate.bold != style.bold)
                score += 10;
            if (candidate.italic != style.italic)
                score += 10;
            ranked.emplace_back(score, i);
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto &l, const auto &r) { return l.first < r.first; });
        std::size_t best = faces.size();
        int bestScore = std::numeric_limits<int>::max();
        for (const auto &[base, i] : ranked) {
            if (base >= bestScore)
                break;
            if (!open(faces[i]))
                continue;
            int score = base;
            for (int32_t pos = static_cast<int32_t>(begin); pos < static_cast<int32_t>(end);) {
                UChar32 cp;
                U16_NEXT(text.data(), pos, static_cast<int32_t>(end), cp);
                if (cp != 0x200d && !(cp >= 0xfe00 && cp <= 0xfe0f) &&
                    !FT_Get_Char_Index(faces[i].face, cp))
                    score += 10000;
            }
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
            if (score == base)
                break;
        }
        if (best == faces.size())
            throw std::runtime_error("No usable fonts found");
        choices.emplace(std::move(key), best);
        return faces[best];
    }
};
Shaper::Shaper(const std::vector<std::string> &paths) : impl_(std::make_unique<Impl>()) {
    if (FT_Init_FreeType(&impl_->library))
        throw std::runtime_error("Cannot initialize FreeType");
    for (const auto &path : paths) {
        FT_Long count = 1;
        for (FT_Long index = 0; index < count; ++index) {
            FT_Face face = nullptr;
            if (FT_New_Face(impl_->library, path.c_str(), index, &face))
                continue;
            count = face->num_faces;
            if (face->family_name)
                impl_->faces.push_back({nullptr, path, lower(face->family_name), face->family_name,
                                        int(index), bool(face->style_flags & FT_STYLE_FLAG_BOLD),
                                        bool(face->style_flags & FT_STYLE_FLAG_ITALIC)});
            FT_Done_Face(face);
        }
    }
    if (impl_->faces.empty())
        throw std::runtime_error("No usable fonts found");
}
Shaper::~Shaper() = default;
std::vector<std::string> Shaper::families() const {
    std::vector<std::string> names;
    for (const auto &face : impl_->faces)
        names.push_back(face.name);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}
ShapedLine Shaper::shape(const Paragraph &paragraph, std::size_t start, std::size_t end) {
    const auto text = paragraph.text();
    if (start > end || end > text.size())
        throw std::out_of_range("Shaping range outside paragraph");
    ShapedLine result;
    const auto defaultSize = paragraph.runs.empty() ? 12 : paragraph.runs.front().style.size;
    result.ascent = defaultSize * 0.9;
    result.descent = defaultSize * 0.3;
    if (start == end)
        return result;
    const auto boundaries = paragraph.boundaries();
    UErrorCode error = U_ZERO_ERROR;
    Bidi paragraphBidi(ubidi_open(), ubidi_close), lineBidi(ubidi_open(), ubidi_close);
    ubidi_setPara(paragraphBidi.get(), reinterpret_cast<const UChar *>(text.data()),
                  static_cast<int32_t>(text.size()), UBIDI_DEFAULT_LTR, nullptr, &error);
    ubidi_setLine(paragraphBidi.get(), static_cast<int32_t>(start), static_cast<int32_t>(end),
                  lineBidi.get(), &error);
    const auto count = ubidi_countRuns(lineBidi.get(), &error);
    if (U_FAILURE(error))
        throw std::runtime_error(u_errorName(error));
    struct Span {
        std::size_t begin, end;
        CharacterStyle style;
        Impl::Face *face;
        UScriptCode script;
        bool tab;
        const InlineImage *image;
    };
    for (int32_t visual = 0; visual < count; ++visual) {
        int32_t logical = 0, length = 0;
        const bool rtl = ubidi_getVisualRun(lineBidi.get(), visual, &logical, &length) == UBIDI_RTL;
        const auto a = start + logical, b = a + length;
        std::vector<Span> spans;
        std::size_t runStart = 0;
        UScriptCode previousScript = USCRIPT_LATIN;
        for (const auto &run : paragraph.runs) {
            const auto begin = std::max(a, runStart),
                       finish = std::min(b, runStart + run.text().size());
            if (run.image() && begin < finish) {
                spans.push_back(
                    {begin, finish, run.style, nullptr, previousScript, false, run.image()});
                runStart += 1;
                continue;
            }
            for (auto pos = begin; pos < finish;) {
                const auto next =
                    std::min(*std::upper_bound(boundaries.begin(), boundaries.end(), pos), finish);
                auto &face = impl_->select(text, pos, next, run.style);
                int32_t cursor = static_cast<int32_t>(pos);
                UChar32 cp;
                U16_NEXT(text.data(), cursor, static_cast<int32_t>(next), cp);
                auto script = uscript_getScript(cp, &error);
                if (script == USCRIPT_COMMON || script == USCRIPT_INHERITED)
                    script = previousScript;
                else
                    previousScript = script;
                const bool tab = text[pos] == u'\t';
                if (!tab && !spans.empty() && !spans.back().tab && spans.back().end == pos &&
                    spans.back().face == &face && spans.back().style == run.style &&
                    spans.back().script == script)
                    spans.back().end = next;
                else
                    spans.push_back({pos, next, run.style, &face, script, tab, nullptr});
                pos = next;
            }
            runStart += run.text().size();
        }
        if (rtl)
            std::reverse(spans.begin(), spans.end());
        for (const auto &span : spans) {
            if (span.image) {
                GlyphRun shaped;
                shaped.image = *span.image;
                shaped.style = span.style;
                shaped.start = span.begin;
                shaped.end = span.end;
                shaped.rtl = rtl;
                shaped.x = result.width;
                shaped.width = span.image->width;
                shaped.ascent = span.image->height;
                result.width += shaped.width;
                result.ascent = std::max(result.ascent, shaped.ascent);
                result.runs.push_back(std::move(shaped));
                continue;
            }
            auto *face = span.face->face;
            double metricScale = 1;
            if (!FT_IS_SCALABLE(face) && face->num_fixed_sizes > 0) {
                int nearest = 0;
                for (int i = 1; i < face->num_fixed_sizes; ++i)
                    if (std::abs(face->available_sizes[i].y_ppem / 64.0 - span.style.size) <
                        std::abs(face->available_sizes[nearest].y_ppem / 64.0 - span.style.size))
                        nearest = i;
                if (FT_Select_Size(face, nearest))
                    throw std::runtime_error("Cannot select bitmap font strike");
                metricScale = span.style.size / (face->available_sizes[nearest].y_ppem / 64.0);
            } else if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(span.style.size * 64), 72,
                                        72))
                throw std::runtime_error("Cannot set font size");
            HbFont font(hb_ft_font_create_referenced(span.face->face), hb_font_destroy);
            Buffer buffer(hb_buffer_create(), hb_buffer_destroy);
            hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
            hb_buffer_add_utf16(buffer.get(), reinterpret_cast<const uint16_t *>(text.data()),
                                static_cast<int>(text.size()), static_cast<unsigned>(span.begin),
                                static_cast<int>(span.end - span.begin));
            hb_buffer_set_direction(buffer.get(), rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer.get(),
                                 hb_script_from_string(uscript_getShortName(span.script), -1));
            hb_buffer_set_language(buffer.get(), hb_language_from_string("und", -1));
            hb_shape(font.get(), buffer.get(), nullptr, 0);
            unsigned glyphCount = 0;
            auto info = hb_buffer_get_glyph_infos(buffer.get(), &glyphCount);
            auto positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
            GlyphRun shaped;
            shaped.fontPath = span.face->path;
            shaped.faceIndex = span.face->index;
            shaped.style = span.style;
            shaped.start = span.begin;
            shaped.end = span.end;
            shaped.rtl = rtl;
            shaped.x = result.width;
            shaped.ascent = face->size->metrics.ascender / 64.0 * metricScale;
            shaped.descent = -face->size->metrics.descender / 64.0 * metricScale;
            if (span.tab)
                shaped.width = 36 - std::fmod(result.width, 36);
            for (unsigned i = 0; !span.tab && i < glyphCount; ++i) {
                const auto &p = positions[i];
                shaped.glyphs.push_back(
                    {static_cast<std::uint16_t>(info[i].codepoint), info[i].cluster,
                     shaped.width + p.x_offset / 64.0 * metricScale,
                     -p.y_offset / 64.0 * metricScale, p.x_advance / 64.0 * metricScale});
                shaped.width += p.x_advance / 64.0 * metricScale;
            }
            result.width += shaped.width;
            result.ascent = std::max(result.ascent, shaped.ascent);
            result.descent = std::max(result.descent, shaped.descent);
            result.runs.push_back(std::move(shaped));
        }
    }
    return result;
}
std::vector<std::string> systemFontFiles() {
    std::vector<std::filesystem::path> directories;
#ifdef _WIN32
    const auto *windows = std::getenv("WINDIR");
    directories.emplace_back(std::filesystem::path(windows ? windows : "C:/Windows") / "Fonts");
    if (auto local = std::getenv("LOCALAPPDATA"))
        directories.emplace_back(std::filesystem::path(local) / "Microsoft/Windows/Fonts");
#else
    directories = {"/usr/share/fonts", "/usr/local/share/fonts"};
    // Inside a Snap, the desktop interface mounts the host's fonts at /usr/share/fonts;
    // the snap's own fallback fonts live under $SNAP, and HOME is the snap's private
    // directory (SNAP_REAL_HOME is the user's).
    if (const auto snap = std::getenv("SNAP"))
        directories.emplace_back(std::filesystem::path(snap) / "usr/share/fonts");
    for (const auto *variable : {"HOME", "SNAP_REAL_HOME"})
        if (auto home = std::getenv(variable)) {
            directories.emplace_back(std::filesystem::path(home) / ".local/share/fonts");
            directories.emplace_back(std::filesystem::path(home) / ".fonts");
        }
#endif
    std::vector<std::string> result;
    for (const auto &directory : directories) {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(directory, error), end; it != end;
             it.increment(error)) {
            if (error)
                break;
            auto extension = lower(it->path().extension().string());
            if (extension == ".ttf" || extension == ".otf" || extension == ".ttc")
                result.push_back(it->path().generic_string());
        }
    }
    // Same order everywhere, so fallback choices do not depend on directory listing order.
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace wf
