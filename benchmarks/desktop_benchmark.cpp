#include "apps/desktop/main_window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPainter>
#include <cstdio>

int main(int argc, char **argv) {
    QElapsedTimer startup;
    startup.start();
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
#ifdef _WIN32
    QApplication::setFont(QFont("Segoe UI", 10));
#endif
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
