#include "apps/desktop/main_window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPainter>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char **argv) {
    QElapsedTimer startup;
    startup.start();
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
#ifdef _WIN32
    QApplication::setFont(QFont("Segoe UI", 10));
#endif
    if (argc > 1 && std::strcmp(argv[1], "--long") == 0) {
        // Typing in a 32,000-paragraph (about 1000-page) document through the editor's own
        // path: model edit, incremental layout, renderer sync and a viewport paint.
        wf::Document document;
        document.paragraphs.clear();
        for (int i = 0; i < 32000; ++i) {
            wf::Paragraph paragraph(document.allocateId());
            paragraph.runs = {{u"Long document paragraph used to measure typing latency.", {}}};
            document.paragraphs.push_back(std::move(paragraph));
        }
        wf::MainWindow window;
        auto *view = window.pageView();
        QElapsedTimer opened;
        opened.start();
        view->setDocument(std::move(document));
        const auto open = opened.nsecsElapsed() / 1e6;
        window.show();
        view->setFocus();
        QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::ControlModifier);
        QApplication::sendEvent(view, &end);
        QImage target(view->viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        view->viewport()->render(&target);
        std::vector<double> samples;
        for (int i = 0; i < 50; ++i) {
            QElapsedTimer keystroke;
            keystroke.start();
            view->insertText(QStringLiteral("x"));
            view->viewport()->render(&target);
            samples.push_back(keystroke.nsecsElapsed() / 1e6);
        }
        std::sort(samples.begin(), samples.end());
        std::printf("paragraphs,open_ms,keystrokes,median_keystroke_ms,p95_keystroke_ms,max_"
                    "keystroke_ms\n32000,%.2f,%zu,%.2f,%.2f,%.2f\n",
                    open, samples.size(), samples[samples.size() / 2],
                    samples[samples.size() * 95 / 100], samples.back());
        return 0;
    }
    wf::MainWindow window;
    window.pageView()->setDocument(wf::sampleDocument());
    const auto constructed = startup.nsecsElapsed() / 1e6;
    window.show();
    auto *view = window.pageView();
    view->setFocus();
    QImage target(view->viewport()->size(), QImage::Format_ARGB32_Premultiplied);
    view->viewport()->render(&target);
    const auto firstPaint = startup.nsecsElapsed() / 1e6;
    int rasters = 0;
    QObject::connect(view, &wf::PageView::viewportRasterized, [&] { ++rasters; });
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 0; i < 100; ++i) {
        QKeyEvent event(QEvent::KeyPress, i % 2 ? Qt::Key_Left : Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(view, &event);
        view->viewport()->render(&target);
    }
    const auto movement = elapsed.nsecsElapsed() / 1e6;
    const auto movementRasters = rasters;
    rasters = 0;
    elapsed.restart();
    for (int i = 0; i < 100; ++i)
        view->viewport()->render(&target);
    std::printf("construct_ms,entry_to_first_paint_ms,100_caret_frames_ms,caret_rasters,100_"
                "repaint_frames_ms,repaint_rasters\n%.2f,%.2f,%.2f,%d,%.2f,%d\n",
                constructed, firstPaint, movement, movementRasters, elapsed.nsecsElapsed() / 1e6,
                rasters);
}
