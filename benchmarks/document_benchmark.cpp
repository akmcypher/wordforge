#include "core/formats/native.hpp"
#include "core/rendering/page_renderer.hpp"
#include <chrono>
#include <cmath>
#include <include/core/SkSurface.h>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
std::size_t peakMemoryMiB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;
    return counters.PeakWorkingSetSize / (1024 * 1024);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage))
        return 0;
    return static_cast<std::size_t>(usage.ru_maxrss) / 1024;
#endif
}
} // namespace
int main(int argc, char **argv) {
    try {
        wf::Shaper shaper(wf::systemFontFiles());
        wf::Paragraph example;
        example.runs = {{u"An independent document engine measures this line.", {}}};
        example.spaceAfter = 2;
        auto line = shaper.shape(example, 0, example.text().size());
        wf::PageStyle page{612, 792, 72};
        const auto perPage = static_cast<std::size_t>(
            std::floor((page.height - 2 * page.margin + example.spaceAfter) /
                       ((line.ascent + line.descent) * 1.15 + example.spaceAfter)));
        std::vector<int> sizes = {1, 100, 500, 1000};
        if (argc > 1)
            sizes = {std::stoi(argv[1])};
        std::cout << "requested_pages,actual_pages,paragraphs,layout_ms,edit_layout_ms,remeasured,"
                     "record_ms,repaint_ms,save_ms,peak_process_mib\n"
                  << std::fixed << std::setprecision(2) << std::flush;
        for (auto size : sizes) {
            if (size < 1 || size > 1000)
                throw std::invalid_argument("Page count must be between 1 and 1000");
            wf::Document document;
            document.page = page;
            document.paragraphs.clear();
            for (std::size_t n = 0; n < perPage * static_cast<std::size_t>(size); ++n) {
                auto p = example;
                p.id = n + 1;
                document.paragraphs.push_back(std::move(p));
            }
            document.nextId = document.paragraphs.size() + 1;
            wf::Editor editor;
            editor.reset(std::move(document));
            wf::LayoutEngine engine(shaper);
            wf::PageRenderer renderer;
            auto begin = Clock::now();
            auto layout = engine.compose(editor.document);
            auto initial = milliseconds(begin);
            begin = Clock::now();
            renderer.synchronize(layout);
            auto record = milliseconds(begin);
            editor.selection = {{editor.document.paragraphs.size() / 2, 0},
                                {editor.document.paragraphs.size() / 2, 0}};
            editor.replace(u"Edited ");
            begin = Clock::now();
            layout = engine.compose(editor.document);
            auto edit = milliseconds(begin);
            auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(900, 1200));
            if (!surface)
                throw std::runtime_error("Cannot allocate benchmark surface");
            begin = Clock::now();
            renderer.drawPage(*surface->getCanvas(), 0);
            auto repaint = milliseconds(begin);
            begin = Clock::now();
            auto bytes = wf::serializeNative(editor.document);
            auto save = milliseconds(begin);
            if (bytes.empty())
                throw std::runtime_error("Empty persistence result");
            std::cout << size << ',' << layout.pageCount << ',' << editor.document.paragraphs.size()
                      << ',' << initial << ',' << edit << ',' << layout.measuredParagraphs << ','
                      << record << ',' << repaint << ',' << save << ',' << peakMemoryMiB() << '\n'
                      << std::flush;
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
