// Opens a DOCX the way Wordforge does, saves it back to DOCX and writes a JSON report of
// what Wordforge understood (counts, warnings, page count). Used by
// tools/fidelity/word-fidelity.ps1 to compare Wordforge's output with the original in
// Microsoft Word.
//
//   wordforge_roundtrip input.docx output.docx report.json
#include "apps/desktop/pictures.hpp"
#include "core/formats/docx.hpp"
#include "core/layout/layout.hpp"
#include <QCoreApplication>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string json(const std::string &text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\')
            out += '\\', out += static_cast<char>(c);
        else if (c < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
            out += buffer;
        } else
            out += static_cast<char>(c);
    }
    return out + "\"";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); // image format plugins for JPEG/GIF/BMP pictures
    if (argc != 4) {
        std::cerr << "usage: wordforge_roundtrip input.docx output.docx report.json\n";
        return 2;
    }
    std::ostringstream report;
    int status = 0;
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot read the input");
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        const auto imported = wf::parseDocx(bytes, wf::pictureToPng);
        const auto &document = imported.document;
        const auto saved = wf::serializeDocx(document);
        std::ofstream(argv[2], std::ios::binary) << saved;
        std::size_t images = 0, tracked = 0, cellParagraphs = 0;
        for (const auto &paragraph : document.paragraphs) {
            if (paragraph.cell)
                ++cellParagraphs;
            for (const auto &run : paragraph.runs) {
                if (run.image())
                    ++images;
                if (run.tracked())
                    ++tracked;
            }
        }
        wf::Shaper shaper(wf::systemFontFiles());
        wf::LayoutEngine engine(shaper);
        std::size_t pages = 0;
        std::string layoutError;
        try {
            pages = engine.compose(document).pageCount;
        } catch (const std::exception &error) {
            layoutError = error.what();
        }
        const auto count = wf::countText(document.text());
        report << "{\"ok\":true,\"paragraphs\":" << document.paragraphs.size()
               << ",\"cellParagraphs\":" << cellParagraphs << ",\"tables\":"
               << document.tables.size() << ",\"images\":" << images
               << ",\"footnotes\":" << document.footnotes.size()
               << ",\"comments\":" << document.comments.size()
               << ",\"revisions\":" << document.revisions.size() << ",\"trackedRuns\":" << tracked
               << ",\"words\":" << count.words << ",\"pages\":" << pages
               << ",\"layoutError\":" << json(layoutError) << ",\"warnings\":[";
        for (std::size_t i = 0; i < imported.warnings.size(); ++i)
            report << (i ? "," : "") << json(imported.warnings[i]);
        report << "]}";
    } catch (const std::exception &error) {
        report.str("");
        report << "{\"ok\":false,\"error\":" << json(error.what()) << "}";
        status = 1;
    }
    std::ofstream(argv[3], std::ios::binary) << report.str();
    std::cout << report.str() << "\n";
    return status;
}
