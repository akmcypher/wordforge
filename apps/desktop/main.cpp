#include "apps/desktop/main_window.hpp"
#include "apps/desktop/self_test.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_LINUX
#include <QFileInfo>
// Under WSL, X11 apps go through XWayland and WSLg's remote windows, where a closing
// menu lingers for about a second next to the next one. Native Wayland does not, so
// prefer it there (falling back to X11) unless the user chose a platform.
static void preferWaylandUnderWsl() {
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM") || !qEnvironmentVariableIsSet("WSL_DISTRO_NAME"))
        return;
    auto display = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (display.isEmpty())
        return;
    // WSLg's socket may not be in this user's XDG_RUNTIME_DIR (e.g. for root).
    const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!display.startsWith('/') && (runtime.isEmpty() || !QFileInfo::exists(runtime + "/" + display))) {
        const auto wslg = "/mnt/wslg/runtime-dir/" + display;
        if (!QFileInfo::exists(wslg))
            return;
        qputenv("WAYLAND_DISPLAY", wslg.toUtf8());
    }
    qputenv("QT_QPA_PLATFORM", "wayland;xcb");
}
#endif
int main(int argc, char **argv) {
#ifdef Q_OS_LINUX
    preferWaylandUnderWsl();
#endif
    QApplication application(argc, argv);
    QApplication::setApplicationName("Wordforge");
    QApplication::setOrganizationName("Wordforge");
    QApplication::setApplicationVersion(WORDFORGE_VERSION);
    QApplication::setStyle("Fusion");
#ifdef _WIN32
    QApplication::setFont(QFont("Segoe UI", 10));
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription("Native page-based word processor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("document", "Wordforge, DOCX or UTF-8 text document to open");
    parser.addOption({"sample", "Open the editable sample document"});
    parser.addOption({"snapshot", "Capture the native window and exit", "png"});
    parser.addOption({"self-test", "Check this installation, write a report and exit", "report"});
    parser.process(application);
    try {
        if (parser.isSet("self-test"))
            return wf::runSelfTest(parser.value("self-test"));
        wf::MainWindow window;
        if (!parser.positionalArguments().isEmpty())
            window.openFile(parser.positionalArguments().first());
        else if (parser.isSet("sample"))
            window.pageView()->setDocument(wf::sampleDocument());
        window.show();
        window.pageView()->setFocus();
        if (parser.positionalArguments().isEmpty() && !parser.isSet("sample") &&
            !parser.isSet("snapshot"))
            QTimer::singleShot(0, &window, &wf::MainWindow::recoverDrafts);
        if (parser.isSet("snapshot"))
            QTimer::singleShot(500, &window, [&] {
                const bool saved = window.grab().save(parser.value("snapshot"));
                application.exit(saved ? 0 : 1);
            });
        return application.exec();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Wordforge: %s\n", error.what());
        QMessageBox::critical(nullptr, "Wordforge could not start",
                              QString::fromUtf8(error.what()));
        return 1;
    }
}
