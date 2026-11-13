#include "core/document/document.hpp"
#include <chrono>
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
double elapsed(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
double peakMiB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        throw std::runtime_error("Cannot read process memory");
    return counters.PeakWorkingSetSize / (1024.0 * 1024.0);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage))
        throw std::runtime_error("Cannot read process memory");
    return usage.ru_maxrss / 1024.0;
#endif
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--tables") {
            wf::Document large;
            large.paragraphs.clear();
            for (std::size_t i = 0; i < 34000; ++i) {
                wf::Paragraph p(large.allocateId());
                p.runs = {{u"Long document navigation workload", {}}};
                large.paragraphs.push_back(std::move(p));
            }
            wf::Editor editor;
            editor.reset(std::move(large));
            editor.selection = {{33999, 0}, {33999, 0}};
            editor.insertTable(2, 2);
            auto begin = Clock::now();
            for (int i = 0; i < 10000; ++i) {
                editor.nextCell();
                editor.nextCell(true);
            }
            const auto navigation = elapsed(begin);
            begin = Clock::now();
            editor.tableRow(true);
            const auto row = elapsed(begin);
            begin = Clock::now();
            editor.tableColumn(true);
            std::cout << "body_paragraphs,cell_moves,navigation_ms,insert_row_ms,insert_column_ms,"
                         "peak_mib\n"
                      << std::fixed << std::setprecision(3) << "34000,20000," << navigation << ','
                      << row << ',' << elapsed(begin) << ',' << peakMiB() << '\n';
            return 0;
        }
        wf::Document document;
        document.paragraphs.clear();
        const bool anchors = argc > 1 && std::string(argv[1]) == "--anchors";
        const std::size_t paragraphs = anchors ? 1 : 34000;
        for (std::size_t index = 0; index < paragraphs; ++index) {
            wf::Paragraph paragraph(document.allocateId());
            paragraph.runs = {{anchors ? std::u16string(100000, u'a')
                                       : u"A short paragraph for model transaction measurement.",
                               {}}};
            document.paragraphs.push_back(std::move(paragraph));
        }
        if (anchors)
            for (int index = 0; index < 1000; ++index)
                document.bookmarks.push_back(
                    {document.allocateId(),
                     "mark_" + std::to_string(index),
                     {document.paragraphs[0].id, 0, wf::Affinity::Before}});
        wf::Editor editor;
        editor.reset(std::move(document));
        editor.selection =
            anchors ? wf::Selection{{0, 50000}, {0, 50000}} : wf::Selection{{17000, 0}, {17000, 0}};
        auto begin = Clock::now();
        editor.replace(u"edited ");
        const auto single = elapsed(begin);
        const auto peakAfterSingle = peakMiB();
        begin = Clock::now();
        editor.insertPageBreak();
        const auto grouped = elapsed(begin);
        const auto peakAfterGrouped = peakMiB();
        begin = Clock::now();
        if (!editor.undo())
            throw std::runtime_error("Missing grouped undo");
        const auto undo = elapsed(begin);
        begin = Clock::now();
        if (!editor.redo())
            throw std::runtime_error("Missing grouped redo");
        const auto redo = elapsed(begin);
        std::cout << "paragraphs,bookmarks,single_edit_ms,grouped_break_ms,undo_ms,redo_ms,peak_"
                     "after_single_"
                     "mib,peak_after_grouped_mib,peak_process_mib\n"
                  << std::fixed << std::setprecision(2) << paragraphs << ',' << (anchors ? 1000 : 0)
                  << ',' << single << ',' << grouped << ',' << undo << ',' << redo << ','
                  << peakAfterSingle << ',' << peakAfterGrouped << ',' << peakMiB() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
