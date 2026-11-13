#include "apps/desktop/self_test.hpp"
#include "apps/desktop/main_window.hpp"
#include "apps/desktop/page_view.hpp"
#include "apps/desktop/spelling.hpp"
#include "core/formats/docx.hpp"
#include "core/formats/native.hpp"
#include "core/typography/shaper.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QPrinter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <functional>

namespace wf {
namespace {
QByteArray readAll(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QByteArray bytes(const std::string &text) {
    return QByteArray(text.data(), static_cast<qsizetype>(text.size()));
}
} // namespace

int runSelfTest(const QString &reportPath) {
    QStringList lines;
    int failures = 0;
    const auto check = [&](const char *name, const std::function<QString()> &body) {
        QString detail;
        bool passed = false;
        try {
            detail = body();
            passed = !detail.startsWith("FAIL:");
            if (!passed)
                detail = detail.mid(5).trimmed();
        } catch (const std::exception &error) {
            detail = QString::fromUtf8(error.what());
        }
        failures += passed ? 0 : 1;
        lines << QString("%1 %2: %3").arg(passed ? "PASS" : "FAIL", name, detail);
    };

    lines << "Wordforge " + QApplication::applicationVersion() + " self-test";
    lines << "Executable: " + QApplication::applicationFilePath();
    check("platform", [] {
        const auto name = QGuiApplication::platformName();
        return name.isEmpty() ? QString("FAIL: no Qt platform plugin") : name;
    });

    // Every file goes through a folder whose name needs Unicode file APIs.
    QTemporaryDir temporary;
    const QString folder = temporary.path() + QString::fromUtf8("/Wordforge тест ✓ 文档");
    check("unicode folder", [&] {
        return temporary.isValid() && QDir().mkpath(folder) ? folder
                                                             : QString("FAIL: cannot create " + folder);
    });

    const Document sample = sampleDocument();
    const auto paragraphs = sample.paragraphs.size();
    check("native round trip", [&] {
        const QString path = folder + "/sample.wfg";
        if (!writeAll(path, bytes(serializeNative(sample))))
            return QString("FAIL: cannot write " + path);
        const auto data = readAll(path);
        const auto loaded = parseNative(data.toStdString());
        return loaded.paragraphs.size() == paragraphs
                   ? QString("%1 paragraphs").arg(paragraphs)
                   : QString("FAIL: %1 of %2 paragraphs").arg(loaded.paragraphs.size()).arg(paragraphs);
    });
    check("docx round trip", [&] {
        const QString path = folder + "/sample.docx";
        if (!writeAll(path, bytes(serializeDocx(sample))))
            return QString("FAIL: cannot write " + path);
        const auto data = readAll(path);
        const auto loaded = parseDocx(data.toStdString());
        return loaded.document.paragraphs.empty()
                   ? QString("FAIL: no paragraphs read back")
                   : QString("%1 paragraphs, %2 warnings")
                         .arg(loaded.document.paragraphs.size())
                         .arg(loaded.warnings.size());
    });

    check("fonts", [] {
        const auto files = systemFontFiles();
        Shaper shaper(files);
        const auto families = shaper.families();
        return files.empty() || families.empty()
                   ? QString("FAIL: no system fonts found")
                   : QString("%1 font files, %2 families").arg(files.size()).arg(families.size());
    });
    check("qt fonts", [] {
        const auto families = QFontDatabase::families();
        return families.isEmpty() ? QString("FAIL: Qt found no fonts")
                                  : QString("%1 families").arg(families.size());
    });

    check("image formats", [] {
        const auto formats = QImageReader::supportedImageFormats();
        for (const char *format : {"png", "jpeg"})
            if (!formats.contains(format))
                return QString("FAIL: %1 decoder missing").arg(format);
        return QString::fromLatin1(formats.join(' '));
    });

    check("spelling", [] {
        auto &spelling = SpellChecker::shared();
        if (!spelling.load("en_US"))
            return QString("FAIL: en_US dictionary not found in " +
                           SpellChecker::searchPaths().join("; "));
        return spelling.isCorrect("document") && !spelling.isCorrect("documnet")
                   ? QString("en_US")
                   : QString("FAIL: dictionary loaded but gives wrong answers");
    });

    MainWindow window; // built, never shown
    window.pageView()->setDocument(sample);
    check("render", [&] {
        const auto image = window.grab().toImage();
        return image.isNull() ? QString("FAIL: window did not render")
                              : QString("%1x%2").arg(image.width()).arg(image.height());
    });
    check("pdf export", [&] {
        const QString path = folder + "/sample.pdf";
        window.pageView()->exportPdf(path);
        const auto data = readAll(path);
        return data.startsWith("%PDF") ? QString("%1 bytes").arg(data.size())
                                       : QString("FAIL: no PDF written");
    });
    check("print", [&] {
        const QString path = folder + "/printed.pdf";
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        window.pageView()->configurePrinter(printer);
        window.pageView()->print(printer);
        const auto data = readAll(path);
        return data.startsWith("%PDF") ? QString("%1 bytes").arg(data.size())
                                       : QString("FAIL: print produced no output");
    });

    check("clipboard", [] {
        auto *clipboard = QGuiApplication::clipboard();
        // Keep whatever the user had copied, in every format, and put it back afterwards.
        auto *previous = new QMimeData;
        if (const auto *current = clipboard->mimeData())
            for (const auto &format : current->formats())
                previous->setData(format, current->data(format));
        const QString probe = QString::fromUtf8("Wordforge clipboard ✓");
        clipboard->setText(probe);
        const bool same = clipboard->text() == probe;
        clipboard->setMimeData(previous);
        return same ? QString("text") : QString("FAIL: clipboard did not round trip");
    });
    check("app data", [] {
        const auto location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString probe = location + "/self-test.tmp";
        const bool ok = QDir().mkpath(location) && writeAll(probe, "ok") && QFile::remove(probe);
        return ok ? location : QString("FAIL: cannot write " + location);
    });

    lines << (failures == 0 ? QString("RESULT: all checks passed")
                            : QString("RESULT: %1 check(s) failed").arg(failures));
    QFile report(reportPath);
    if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&report);
        out.setEncoding(QStringConverter::Utf8);
        out << lines.join('\n') << '\n';
    }
    return failures == 0 ? 0 : 1;
}
} // namespace wf
