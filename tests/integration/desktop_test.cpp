#include "apps/desktop/appearance.hpp"
#include "apps/desktop/main_window.hpp"
#include "apps/desktop/page_view.hpp"
#include "apps/desktop/recovery_file.hpp"
#include "apps/desktop/spelling.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QPrinter>
#include <QProcess>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#include <filesystem>
class DesktopTest : public QObject {
    Q_OBJECT
  private slots:
    void minimalShellShowsMenusToolbarAndPanelWithoutARibbon() {
        wf::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QStringList menus;
        for (auto *action : window.menuBar()->actions())
            menus << action->text().remove('&');
        QCOMPARE(menus, QStringList({"File", "Edit", "Insert", "Format", "Tools", "View", "Help"}));
        auto *toolbar = window.findChild<QToolBar *>("compactToolbar");
        QVERIFY(toolbar && toolbar->isVisible());
        auto *panel = window.findChild<QDockWidget *>("formatPanel");
        QVERIFY(panel && panel->isVisible());
        QVERIFY(!window.findChild<QWidget *>("ribbonBar"));
        QVERIFY(!window.findChild<QAction *>("legacyRibbonAction"));
        // Menu bar plus one toolbar row stays well under the old ribbon's height.
        QVERIFY(toolbar->geometry().bottom() < 110);
    }
    void everyCommandIsReachableFromTheMenus() {
        wf::MainWindow window;
        QSet<QAction *> menuActions;
        std::function<void(QMenu *)> collect = [&](QMenu *menu) {
            for (auto *action : menu->actions()) {
                menuActions.insert(action);
                if (action->menu())
                    collect(action->menu());
            }
        };
        for (auto *action : window.menuBar()->actions())
            collect(action->menu());
        const QStringList ids{
            "new", "open", "recoverDrafts", "save", "saveAs", "exportPdf", "exportText", "print",
            "printPreview", "exit",
            "undo", "redo", "cut", "copy", "paste", "selectAll", "find", "replace", "bold",
            "italic", "underline", "strike", "highlight", "fontColor", "clearFormatting",
            "bullets", "numbering", "decreaseIndent", "increaseIndent", "alignLeft", "alignCenter",
            "alignRight", "lineSpacing1", "lineSpacing115", "lineSpacing15", "lineSpacing2",
            "styleNormal", "styleTitle", "styleHeading1", "styleHeading2", "updateStyle",
            "pageBreak", "date",
            "symbol0", "symbol6", "picture", "pictureSize", "table", "tableRowAbove", "tableRowBelow",
            "tableColumnLeft", "tableColumnRight", "tableDeleteRow", "tableDeleteColumn",
            "tableDelete", "bookmarks", "marginsDefault", "marginsNormal",
            "marginsModerate", "marginsNarrow", "portrait", "landscape", "paperA4", "paperLetter",
            "zoom100", "zoomPageWidth", "focusMode", "formatPanel", "commandPalette", "wordCount",
            "spellCheck", "themeSystem", "themeLight", "themeDark",
            "about"};
        for (const auto &id : ids) {
            auto *action = window.findChild<QAction *>(id + "Action");
            QVERIFY2(action, qPrintable(id));
            QVERIFY2(menuActions.contains(action), qPrintable(id + " is not in a menu"));
        }
        for (const auto &name :
             {"styleCombo", "fontFamilyCombo", "panelLineSpacing", "panelMargins",
              "panelOrientation", "panelPaper"})
            QVERIFY2(window.findChild<QComboBox *>(name), name);
        QVERIFY(window.findChild<QSpinBox *>("fontSizeSpin"));
        for (const auto &name : {"panelIndent", "panelSpaceBefore", "panelSpaceAfter"})
            QVERIFY2(window.findChild<QDoubleSpinBox *>(name), name);
    }
    void toolbarAndFormatPanelEditTheDocument() {
        wf::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = window.pageView();
        view->setDocument({});
        view->insertText("first\nsecond");
        view->selectAll();
        auto *bold = window.findChild<QToolButton *>("boldToolButton");
        QVERIFY(bold);
        QTest::mouseClick(bold, Qt::LeftButton);
        QVERIFY(view->document().paragraphs[1].runs[0].style.bold);
        QVERIFY(bold->isChecked());
        auto *styles = window.findChild<QComboBox *>("styleCombo");
        styles->setCurrentIndex(2);
        emit styles->activated(2);
        QCOMPARE(QString::fromStdString(view->document().paragraphs[0].style), QString("Heading1"));
        QCOMPARE(styles->currentIndex(), 2);
        auto *indent = window.findChild<QDoubleSpinBox *>("panelIndent");
        indent->setValue(36);
        QCOMPARE(view->document().paragraphs[1].indentLeft, 36.0);
        auto *spacing = window.findChild<QComboBox *>("panelLineSpacing");
        spacing->setCurrentIndex(3);
        emit spacing->activated(3);
        QCOMPARE(view->document().paragraphs[0].lineSpacing, 2.0);
        auto *margins = window.findChild<QComboBox *>("panelMargins");
        QCOMPARE(margins->currentIndex(), 0);
        emit margins->activated(3);
        QCOMPARE(view->document().page.margin, 36.0);
        QCOMPARE(margins->currentIndex(), 3);
        auto *orientation = window.findChild<QComboBox *>("panelOrientation");
        emit orientation->activated(1);
        QVERIFY(view->document().page.width > view->document().page.height);
        auto *paper = window.findChild<QComboBox *>("panelPaper");
        emit paper->activated(1);
        QCOMPARE(view->document().page.width, 792.0);
        QCOMPARE(paper->currentIndex(), 1);
        view->undo();
        QCOMPARE(paper->currentIndex(), 0);
        QVERIFY(!window.findChild<QPushButton *>("panelPictureSize")->isEnabled());
    }
    void commandPaletteFindsAndRunsCommands() {
        wf::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = window.pageView();
        view->setDocument({});
        view->insertText("text");
        window.findChild<QAction *>("commandPaletteAction")->trigger();
        auto *palette = window.findChild<QDialog *>("commandPalette");
        QVERIFY(palette && palette->isVisible());
        auto *query = palette->findChild<QLineEdit *>("paletteQuery");
        auto *results = palette->findChild<QListWidget *>("paletteResults");
        QTest::keyClicks(query, "insert page brea");
        QCOMPARE(results->count(), 1);
        QVERIFY(results->item(0)->text().startsWith("Page break"));
        QTest::keyClick(query, Qt::Key_Return);
        QVERIFY(!palette->isVisible());
        QCOMPARE(view->layout().pageCount, std::size_t(2));
        // A disabled command is listed but cannot run.
        window.findChild<QAction *>("commandPaletteAction")->trigger();
        QTest::keyClicks(query, "picture size");
        QCOMPARE(results->count(), 1);
        QVERIFY(!(results->item(0)->flags() & Qt::ItemIsEnabled));
        QTest::keyClick(query, Qt::Key_Return);
        QVERIFY(palette->isVisible());
        QTest::keyClick(query, Qt::Key_Escape);
        QVERIFY(!palette->isVisible());
    }
    void pictureDialogsInsertAndResizeWithAspectRatio() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        wf::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const auto path = QString::fromStdString(
            (std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png")
                .string());
        bool picked = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                dialog->selectFile(path);
                picked = QMetaObject::invokeMethod(dialog, "accept");
            }
        });
        window.findChild<QAction *>("pictureAction")->trigger();
        QVERIFY(picked);
        QVERIFY(window.pageView()->selectedImage());
        bool resized = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                if (auto *width = dialog->findChild<QDoubleSpinBox *>("pictureWidth")) {
                    width->setValue(72);
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    resized = true;
                } else
                    dialog->reject();
            }
        });
        QTest::mouseClick(window.findChild<QPushButton *>("panelPictureSize"), Qt::LeftButton);
        QVERIFY(resized);
        QCOMPARE(window.pageView()->selectedImage()->width, 72.0);
        QCOMPARE(window.pageView()->selectedImage()->height, 36.0);
    }
    void invalidPictureInputAndResizeLeaveTheDocumentUnchanged() {
        wf::PageView view;
        view.insertText("keep this text");
        QSignalSpy errors(&view, &wf::PageView::errorOccurred);
        QTemporaryDir directory;
        QFile invalid(directory.path() + "/invalid.png");
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a picture");
        invalid.close();
        QVERIFY(!view.insertPicture(invalid.fileName()));
        QCOMPARE(view.document().text(), std::u16string(u"keep this text"));
        QCOMPARE(errors.count(), 1);
        const auto valid = QString::fromStdString(
            (std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png")
                .string());
        QVERIFY(view.insertPicture(valid));
        const auto width = view.selectedImage()->width;
        QVERIFY(!view.resizePicture(0, 10));
        QCOMPARE(view.selectedImage()->width, width);
        QCOMPARE(errors.count(), 2);
    }
    void picturesInsertResizeSaveReopenAndExportRealImageData() {
        wf::MainWindow window;
        auto *view = window.pageView();
        const auto path = QString::fromStdString(
            (std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures/red-blue.png")
                .string());
        QVERIFY(view->insertPicture(path));
        QVERIFY(view->selectedImage());
        const auto originalWidth = view->selectedImage()->width;
        QVERIFY(view->resizePicture(72, 36));
        QCOMPARE(view->selectedImage()->width, 72.0);
        view->undo();
        QCOMPARE(view->selectedImage()->width, originalWidth);
        QTemporaryDir directory;
        window.saveFile(directory.path() + "/picture.wfg");
        window.saveFile(directory.path() + "/picture.docx");
        view->exportPdf(directory.path() + "/picture.pdf");
        QFile pdf(directory.path() + "/picture.pdf");
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().contains("/Image"));
        wf::MainWindow restored;
        restored.openFile(directory.path() + "/picture.wfg");
        QCOMPARE(restored.pageView()->document().images.size(), std::size_t(1));
        QVERIFY(window.findChild<QAction *>("pictureAction"));
        QVERIFY(window.findChild<QAction *>("pictureSizeAction"));
    }
    void bookmarkNavigationUpdatesTypingStyleAndClearsPreedit() {
        wf::PageView view;
        view.insertText("Heading\nBody");
        QTest::keyClick(&view, Qt::Key_Home, Qt::ControlModifier);
        view.applyPreset(wf::ParagraphPreset::Heading1);
        const auto id = view.addBookmark("heading");
        QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier);
        QVERIFY(!view.typingStyle().bold);
        QInputMethodEvent preedit("pending", {});
        QApplication::sendEvent(&view, &preedit);
        QVERIFY(view.goToBookmark(id));
        QVERIFY(view.typingStyle().bold);
        QCOMPARE(view.typingStyle().size, 20.0);
        view.insertText("X");
        QCOMPARE(view.document().text(), std::u16string(u"XHeading\nBody"));
        QVERIFY(view.document().paragraphs[0].runs[0].style.bold);
    }
    void bookmarksNavigateAndPersistThroughTheNativeWorkflow() {
        wf::MainWindow window;
        auto *view = window.pageView();
        view->insertText("first\nsecond");
        const auto id = view->addBookmark("chapter");
        QVERIFY(id);
        QTest::keyClick(view, Qt::Key_Home, Qt::ControlModifier);
        QVERIFY(view->goToBookmark(id));
        QCOMPARE(view->selection().focus.paragraph, std::size_t(1));
        QCOMPARE(view->selection().focus.offset, std::size_t(6));
        auto *action = window.findChild<QAction *>("bookmarksAction");
        QVERIFY(action);
        bool dialogWorked = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *name = dialog->findChild<QLineEdit *>("bookmarkName");
            auto *add = dialog->findChild<QPushButton *>("addBookmarkButton");
            auto *list = dialog->findChild<QListWidget *>("bookmarksList");
            if (name && add && list) {
                name->setText("dialog_created");
                add->click();
                dialogWorked =
                    list->count() == 2 && list->currentItem()->text() == "dialog_created";
            }
            dialog->accept();
        });
        action->trigger();
        QVERIFY(dialogWorked);
        QTemporaryDir directory;
        window.saveFile(directory.path() + "/bookmarks.wfg");
        const auto savedTitle = window.windowTitle();
        QFile protectedDocx(directory.path() + "/preserve.docx");
        QVERIFY(protectedDocx.open(QIODevice::WriteOnly));
        protectedDocx.write("original bytes");
        protectedDocx.close();
        QVERIFY_THROWS_EXCEPTION(std::exception, window.saveFile(protectedDocx.fileName()));
        QCOMPARE(window.windowTitle(), savedTitle);
        QVERIFY(protectedDocx.open(QIODevice::ReadOnly));
        QCOMPARE(protectedDocx.readAll(), QByteArray("original bytes"));
        wf::MainWindow restored;
        restored.openFile(directory.path() + "/bookmarks.wfg");
        QCOMPARE(restored.pageView()->document().bookmarks.size(), std::size_t(2));
        QVERIFY(restored.pageView()->goToBookmark(id));
        view->removeBookmark(id);
        QCOMPARE(view->document().bookmarks.size(), std::size_t(1));
        view->undo();
        QCOMPARE(view->document().bookmarks.size(), std::size_t(2));
    }
    void docxExportAndOpenUseRealZipAndAnUnsavedCopy() {
        QTemporaryDir directory;
        const auto path = directory.path() + "/draft.docx";
        wf::MainWindow writer;
        writer.pageView()->insertText("A real Word document नमस्ते");
        writer.pageView()->selectAll();
        writer.pageView()->format([](auto &style) { style.bold = true; });
        writer.saveFile(path);
        QFile package(path);
        QVERIFY(package.open(QIODevice::ReadOnly));
        QCOMPARE(package.read(2), QByteArray("PK"));
        wf::MainWindow reader;
        reader.openFile(path);
        QCOMPARE(reader.pageView()->document().text(), writer.pageView()->document().text());
        QVERIFY(reader.pageView()->document().paragraphs[0].runs[0].style.bold);
        QVERIFY(reader.windowTitle().contains("Untitled *"));
        QFile malformed(directory.path() + "/bad.docx");
        QVERIFY(malformed.open(QIODevice::WriteOnly));
        malformed.write("not a zip");
        malformed.close();
        QVERIFY_THROWS_EXCEPTION(std::exception, reader.openFile(malformed.fileName()));
        QCOMPARE(reader.pageView()->document().text(), writer.pageView()->document().text());
    }
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
    }
    void periodicRecoveryIsRemovedOnlyAfterFormattedSave() {
        QTemporaryDir directory;
        const QDir recovery(wf::RecoveryFile::directory());
        const auto before = recovery.entryList({"*.wfg"}, QDir::Files);
        wf::MainWindow window;
        window.pageView()->insertText("an unsaved draft");
        auto *timer = window.findChild<QTimer *>("recoveryTimer");
        QVERIFY(timer);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
        auto after = recovery.entryList({"*.wfg"}, QDir::Files);
        for (const auto &name : before)
            after.removeAll(name);
        QCOMPARE(after.size(), 1);
        const auto path = recovery.filePath(after.first());
        window.saveFile(directory.path() + "/export.txt");
        QVERIFY(QFile::exists(path));
        window.saveFile(directory.path() + "/saved.wfg");
        QVERIFY(!QFile::exists(path));
    }
    void recoverySnapshotsSurviveAbandonmentAndRespectLiveOwners() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString path;
        {
            wf::RecoveryFile draft(directory.path() + "/draft.wfg");
            QVERIFY(draft.locked());
            path = draft.path();
            wf::RecoveryFile competing(path);
            QVERIFY(!competing.locked());
            wf::Editor editor;
            editor.replace(u"Recovered नमस्ते draft");
            draft.write(editor.document);
            QCOMPARE(draft.read().text(), editor.document.text());
        }
        wf::RecoveryFile abandoned(path);
        QVERIFY(abandoned.locked());
        QCOMPARE(abandoned.read().text(), std::u16string(u"Recovered नमस्ते draft"));
        abandoned.clear();
        QVERIFY(!QFile::exists(path));
    }
    void recoverySurvivesAKilledProcess() {
        const auto childPath = qEnvironmentVariable("WORDFORGE_CRASH_TEST_PATH");
        if (!childPath.isEmpty()) {
            wf::RecoveryFile draft(childPath);
            wf::Editor editor;
            editor.replace(u"survives process termination");
            draft.write(editor.document);
            QTest::qWait(60000); // The parent kills this process while it owns the lock.
            QFAIL("Parent did not terminate the crash fixture");
        }
        QTemporaryDir directory;
        const auto path = directory.path() + "/crashed.wfg";
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("WORDFORGE_CRASH_TEST_PATH", path);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"recoverySurvivesAKilledProcess"});
        QVERIFY(child.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 5000);
        {
            wf::RecoveryFile live(path);
            QVERIFY(!live.locked());
        }
        child.kill();
        QVERIFY(child.waitForFinished());
        wf::RecoveryFile orphan(path);
        QVERIFY(orphan.locked());
        QCOMPARE(orphan.read().text(), std::u16string(u"survives process termination"));
        orphan.clear();
    }
    void recoveryWriteFailureKeepsPreviousSnapshot() {
        QTemporaryDir directory;
        wf::RecoveryFile draft(directory.path() + "/draft.wfg");
        wf::Editor editor;
        editor.replace(u"last good draft");
        draft.write(editor.document);
        auto invalid = editor.document;
        invalid.page.width = -1;
        QVERIFY_THROWS_EXCEPTION(std::exception, draft.write(invalid));
        QCOMPARE(draft.read().text(), editor.document.text());
    }
    void listTogglesExcludeTheParagraphAtAZeroOffsetSelectionEnd() {
        wf::MainWindow window;
        auto *view = window.pageView();
        view->insertText("first\nsecond");
        QVERIFY(view->findNext("first"));
        auto *bullets = window.findChild<QToolButton *>("bulletsToolButton");
        QVERIFY(bullets);
        bullets->click();
        QCOMPARE(view->document().paragraphs[0].list, wf::ListKind::Bullet);
        QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(view->selection().focus.paragraph, std::size_t(1));
        QCOMPARE(view->selection().focus.offset, std::size_t(0));
        QVERIFY(bullets->isChecked());
        bullets->click();
        QCOMPARE(view->document().paragraphs[0].list, wf::ListKind::None);
        auto *numbering = window.findChild<QToolButton *>("numberingToolButton");
        QVERIFY(numbering);
        numbering->click();
        QVERIFY(numbering->isChecked());
        numbering->click();
        QCOMPARE(view->document().paragraphs[0].list, wf::ListKind::None);
        QCOMPARE(view->document().paragraphs[1].list, wf::ListKind::None);
    }
    void formattingInsertAndStyleCommandsWorkWithoutTheRibbon() {
        wf::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = window.pageView();
        view->insertText("first\nsecond");
        view->selectAll();
        for (const auto &name : {"numberingToolButton", "increaseIndentToolButton"}) {
            auto *button = window.findChild<QToolButton *>(name);
            QVERIFY2(button, name);
            QTest::mouseClick(button, Qt::LeftButton);
        }
        QTest::mouseClick(window.findChild<QToolButton *>("highlightToolButton"), Qt::LeftButton);
        QTest::mouseClick(window.findChild<QToolButton *>("panelStrike"), Qt::LeftButton);
        QCOMPARE(view->document().paragraphs[0].list, wf::ListKind::Decimal);
        QCOMPARE(view->document().paragraphs[1].indentLeft, 18.0);
        QVERIFY(view->document().paragraphs[0].runs[0].style.strike);
        QCOMPARE(view->document().paragraphs[0].runs[0].style.highlight, std::uint32_t(0xffffff00));
        QTest::keyClick(view, Qt::Key_End, Qt::ControlModifier);
        window.findChild<QAction *>("pageBreakAction")->trigger();
        QCOMPARE(view->layout().pageCount, std::size_t(2));
        view->undo();
        QCOMPARE(view->layout().pageCount, std::size_t(1));
        auto *styles = window.findChild<QComboBox *>("styleCombo");
        emit styles->activated(2);
        QCOMPARE(QString::fromStdString(view->document().paragraphs.back().style), QString("Heading1"));
    }
    void findBarUsesTheEditingCommandsAndEscReturnsToWriting() {
        wf::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.pageView()->insertText("first draft");
        auto *action = window.findChild<QAction *>("findAction");
        QVERIFY(action);
        action->trigger();
        auto *query = window.findChild<QLineEdit *>("findQuery");
        QVERIFY(query);
        query->setText("draft");
        QTest::keyClick(query, Qt::Key_Return);
        QCOMPARE(window.pageView()->selectedText(), QString("draft"));
        auto *replacement = window.findChild<QLineEdit *>("replacementText");
        QVERIFY(replacement);
        replacement->setText("edition");
        auto *replace = window.findChild<QAction *>("replaceMatchAction");
        QVERIFY(replace);
        replace->trigger();
        QCOMPARE(window.pageView()->document().text(), std::u16string(u"first edition"));
        auto *search = window.findChild<QToolBar *>("searchBar");
        QVERIFY(search);
        QTest::keyClick(query, Qt::Key_Escape);
        QVERIFY(!search->isVisible());
        window.pageView()->undo();
        QCOMPARE(window.pageView()->document().text(), std::u16string(u"first draft"));
    }
    void formattingButtonsAndFocusModeFollowTheEditor() {
        wf::MainWindow window;
        window.show();
        auto *bold = window.findChild<QToolButton *>("boldToolButton");
        QVERIFY(bold);
        auto *focus = window.findChild<QAction *>("focusModeAction");
        QVERIFY(focus);
        auto *toolbar = window.findChild<QToolBar *>("compactToolbar");
        auto *panel = window.findChild<QDockWidget *>("formatPanel");
        QVERIFY(toolbar && panel);
        window.pageView()->insertText("text");
        window.pageView()->selectAll();
        QTest::keyClick(window.pageView(), Qt::Key_B, Qt::ControlModifier);
        QVERIFY(bold->isChecked());
        bold->click();
        QVERIFY(!window.pageView()->document().paragraphs[0].runs[0].style.bold);
        focus->trigger();
        QVERIFY(!toolbar->isVisible() && !panel->isVisible() && !window.menuBar()->isVisible());
        focus->trigger();
        QVERIFY(toolbar->isVisible() && panel->isVisible() && window.menuBar()->isVisible());
        panel->close();
        focus->trigger();
        focus->trigger();
        QVERIFY(!panel->isVisible());
    }
    void findWrapsAndReplacementPreservesUndoAndUnicodeBoundaries() {
        wf::PageView view;
        view.insertText("one two one");
        QVERIFY(view.findNext("one"));
        QCOMPARE(view.selection().anchor.offset, std::size_t(0));
        QVERIFY(view.replaceCurrent("one", "three"));
        QCOMPARE(view.document().text(), std::u16string(u"three two one"));
        view.undo();
        QCOMPARE(view.document().text(), std::u16string(u"one two one"));
        QVERIFY(!view.findNext("absent"));
        view.setDocument({});
        view.insertText(QString::fromUtf8("á"));
        QVERIFY(!view.findNext("a"));
        QCOMPARE(view.document().text(), std::u16string(u"á"));
        view.setDocument({});
        view.insertText("hello");
        QTest::keyClick(&view, Qt::Key_Home);
        QTest::keyClick(&view, Qt::Key_Right);
        QVERIFY(view.findNext("hello"));
        QCOMPARE(view.selectedText(), QString("hello"));
    }
    void caretAndSelectionReuseRasterizedPages() {
        wf::PageView view;
        view.resize(900, 700);
        view.setDocument(wf::sampleDocument());
        view.show();
        view.setFocus();
        view.viewport()->grab();
        QSignalSpy rasters(&view, &wf::PageView::viewportRasterized);
        QTest::keyClick(&view, Qt::Key_Right);
        view.viewport()->grab();
        QCOMPARE(rasters.count(), 0);
        const auto plain = view.viewport()->grab().toImage();
        view.selectAll();
        const auto selected = view.viewport()->grab().toImage();
        QCOMPARE(rasters.count(), 0);
        QVERIFY(plain != selected);
        view.insertText("updated");
        view.viewport()->grab();
        QCOMPARE(rasters.count(), 1);
        view.setZoom(1.25);
        view.viewport()->grab();
        QCOMPARE(rasters.count(), 2);
        view.verticalScrollBar()->setValue(25);
        view.viewport()->grab();
        QCOMPARE(rasters.count(), 3);
    }
    void keyboardClipboardAndUndo() {
        wf::PageView view;
        view.resize(900, 700);
        view.show();
        view.setFocus();
        QTest::keyClicks(&view, "Hello");
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::keyClicks(&view, "world");
        QCOMPARE(view.document().text(), std::u16string(u"Hello\nworld"));
        QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello\nworld"));
        QTest::keyClick(&view, Qt::Key_Backspace);
        QCOMPARE(view.document().text(), std::u16string(u""));
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(view.document().text(), std::u16string(u"Hello\nworld"));
    }
    void formattingAndPdfUseTheDocumentScene() {
        wf::PageView view;
        view.insertText(QString::fromUtf8("Native नमस्ते مرحبا"));
        view.selectAll();
        view.format([](auto &style) {
            style.bold = true;
            style.size = 18;
        });
        QVERIFY(view.document().paragraphs[0].runs[0].style.bold);
        view.setZoom(1.5);
        QCOMPARE(view.zoom(), 1.5);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto path = directory.filePath("page.pdf");
        view.exportPdf(path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("%PDF-"));
    }
    void keyboardFormattingAppliesToSelectedText() {
        wf::PageView view;
        view.insertText("emphasis");
        view.selectAll();
        QTest::keyClick(&view, Qt::Key_B, Qt::ControlModifier);
        QVERIFY(view.document().paragraphs[0].runs[0].style.bold);
        QTest::keyClick(&view, Qt::Key_I, Qt::ControlModifier);
        QVERIFY(view.document().paragraphs[0].runs[0].style.italic);
    }
    void fileSaveAndReopenPreserveFormatting() {
        QTemporaryDir directory;
        wf::MainWindow window;
        window.pageView()->insertText("Saved manuscript");
        window.pageView()->selectAll();
        window.pageView()->format([](auto &style) { style.italic = true; });
        auto path = directory.filePath("manuscript.wfg");
        window.saveFile(path);
        QFile file(path);
        QVERIFY(file.exists());
        wf::MainWindow reopened;
        reopened.openFile(path);
        QCOMPARE(reopened.pageView()->document().text(), std::u16string(u"Saved manuscript"));
        QVERIFY(reopened.pageView()->document().paragraphs[0].runs[0].style.italic);
    }
    void rejectedLayoutPreservesTheOpenDocumentAndUndo() {
        wf::PageView view;
        view.insertText("keep me");
        wf::Document invalid;
        invalid.page = {72, 72, 18};
        invalid.paragraphs[0].runs = {{u"oversize", {}}};
        invalid.paragraphs[0].runs[0].style.size = 200;
        invalid.paragraphs[0].runs[0].direct = wf::SizeField; // valid model, impossible layout
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, view.setDocument(invalid));
        QCOMPARE(view.document().text(), std::u16string(u"keep me"));
        view.undo();
        QCOMPARE(view.document().text(), std::u16string(u""));
    }
    void rejectedFormattingIsAtomic() {
        wf::PageView view;
        wf::Document small;
        small.page = {100, 100, 20};
        view.setDocument(small);
        view.insertText("keep");
        view.selectAll();
        QSignalSpy errors(&view, &wf::PageView::errorOccurred);
        view.format([](auto &style) { style.size = 200; });
        QCOMPARE(errors.count(), 1);
        QCOMPARE(view.document().paragraphs[0].runs[0].style.size, 12.0);
        view.undo();
        QCOMPARE(view.document().text(), std::u16string(u""));
    }
    void tabIsDocumentInputAndPreeditIsNotCommittedEarly() {
        wf::MainWindow window;
        window.show();
        auto &view = *window.pageView();
        view.setFocus();
        view.insertText("a");
        QTest::keyClick(&view, Qt::Key_Tab);
        QCOMPARE(view.document().text(), std::u16string(u"a\t"));
        QInputMethodEvent preedit(QString::fromUtf8("न"), {});
        QApplication::sendEvent(&view, &preedit);
        QCOMPARE(view.document().text(), std::u16string(u"a\t"));
        QInputMethodEvent commit;
        commit.setCommitString(QString::fromUtf8("न"));
        QApplication::sendEvent(&view, &commit);
        QCOMPARE(view.document().text(), std::u16string(u"a\tन"));
    }
    void wordCountShowsSelectionAndDocumentTotals() {
        wf::MainWindow window;
        window.show();
        auto *view = window.pageView();
        view->insertText("one two three\nfour");
        auto *status = window.findChild<QLabel *>("statistics");
        QVERIFY(status);
        QVERIFY(status->text().endsWith("4 words"));
        QTest::keyClick(view, Qt::Key_Home, Qt::ControlModifier);
        for (int i = 0; i < 7; ++i)
            QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY2(status->text().endsWith("2 of 4 words"), qPrintable(status->text()));
        view->selectAll();
        QString words, paragraphs;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("wordCountDialog");
            if (!dialog)
                return;
            words = dialog->findChild<QLabel *>("countWords")->text();
            paragraphs = dialog->findChild<QLabel *>("countParagraphs")->text();
            dialog->reject();
        });
        window.findChild<QAction *>("wordCountAction")->trigger();
        QCOMPARE(words, QString("4"));
        QCOMPARE(paragraphs, QString("2"));
    }
    void printingWritesEveryPageOrTheChosenRange() {
        wf::MainWindow window;
        auto *view = window.pageView();
        view->insertText("first page");
        view->insertPageBreak();
        view->insertText("second page");
        view->insertPageBreak();
        view->insertText("third page");
        QCOMPARE(view->layout().pageCount, std::size_t(3));
        QTemporaryDir directory;
        auto pages = [&](const QString &name, int from, int to) {
            QPrinter printer(QPrinter::HighResolution);
            view->configurePrinter(printer);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(directory.filePath(name));
            if (from) {
                printer.setPrintRange(QPrinter::PageRange);
                printer.setFromTo(from, to);
            }
            view->print(printer);
            QFile file(directory.filePath(name));
            if (!file.open(QIODevice::ReadOnly))
                return -1;
            // Count page objects ("/Type /Page", not "/Type /Pages").
            int count = 0;
            const QRegularExpression page(R"(/Type\s*/Page[^s])");
            for (auto it = page.globalMatch(QString::fromLatin1(file.readAll())); it.hasNext();
                 it.next())
                ++count;
            return count;
        };
        QCOMPARE(pages("all.pdf", 0, 0), 3);
        QCOMPARE(pages("range.pdf", 2, 3), 2);
        QVERIFY(window.findChild<QAction *>("printAction"));
        QCOMPARE(window.findChild<QAction *>("printAction")->shortcut(), QKeySequence(QKeySequence::Print));
    }
    void tableDialogEditingStructureAndFileWorkflow() {
        wf::MainWindow window;
        window.show();
        auto *view = window.pageView();
        view->setDocument({});
        auto *action = window.findChild<QAction *>("tableAction");
        QVERIFY(action);
        auto *rowBelow = window.findChild<QAction *>("tableRowBelowAction");
        QVERIFY(rowBelow);
        QVERIFY(!rowBelow->isEnabled());
        bool inserted = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                auto *rows = dialog->findChild<QSpinBox *>("tableRows");
                auto *columns = dialog->findChild<QSpinBox *>("tableColumns");
                if (rows && columns) {
                    rows->setValue(2);
                    columns->setValue(2);
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    inserted = true;
                } else
                    dialog->reject();
            }
        });
        action->trigger();
        QVERIFY(inserted);
        QCOMPARE(view->document().tables.size(), std::size_t(1));
        QVERIFY(view->selectedCell());
        QVERIFY(rowBelow->isEnabled());
        // A picture in a two-column cell is limited to the cell's writing width.
        const auto &page = view->document().page;
        QCOMPARE(view->writingWidth(), (page.width - 2 * page.margin) / 2 - 8);
        view->insertText("Left");
        QTest::keyClick(view, Qt::Key_Tab);
        view->insertText("Right");
        QCOMPARE(view->selection().focus.paragraph, std::size_t(1));
        QTest::keyClick(view, Qt::Key_Backtab);
        QCOMPARE(view->selection().focus.paragraph, std::size_t(0));
        auto *panelRow = window.findChild<QToolButton *>("panelTableRowBelow");
        QVERIFY(panelRow && panelRow->isEnabled());
        QTest::mouseClick(panelRow, Qt::LeftButton);
        QCOMPARE(view->document().tables[0].rows, std::size_t(3));
        view->undo();
        QCOMPARE(view->document().tables[0].rows, std::size_t(2));
        // Tab from the last cell adds a row, undone in one step.
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(view, Qt::Key_Tab);
        QVERIFY(view->selectedCell());
        const auto last = *view->selectedCell();
        QCOMPARE(last.row, std::size_t(1));
        QCOMPARE(last.column, std::size_t(1));
        QTest::keyClick(view, Qt::Key_Tab);
        QCOMPARE(view->document().tables[0].rows, std::size_t(3));
        view->undo();
        QCOMPARE(view->document().tables[0].rows, std::size_t(2));
        QTemporaryDir directory;
        window.saveFile(directory.path() + "/table.wfg");
        window.saveFile(directory.path() + "/table.docx");
        view->exportPdf(directory.path() + "/table.pdf");
        QVERIFY(QFileInfo(directory.path() + "/table.pdf").size() > 100);
        QVERIFY(QFileInfo(directory.path() + "/table.docx").size() > 100);
        // Keep the DOCX for checking with an independent reader (python-docx).
        if (const auto keep = qEnvironmentVariable("WORDFORGE_EVIDENCE_DIR"); !keep.isEmpty())
            QFile::copy(directory.path() + "/table.docx", keep + "/table.docx");
        wf::MainWindow restored;
        restored.openFile(directory.path() + "/table.wfg");
        QCOMPARE(restored.pageView()->document().tables.size(), std::size_t(1));
        QCOMPARE(restored.pageView()->document().paragraphs[0].text(), std::u16string(u"Left"));
        QCOMPARE(restored.pageView()->document().paragraphs[1].text(), std::u16string(u"Right"));
        window.findChild<QAction *>("tableDeleteAction")->trigger();
        QVERIFY(view->document().tables.empty());
    }
    void pastingHtmlKeepsFormattingAndCopyRoundTrips() {
        wf::MainWindow window;
        auto *view = window.pageView();
        auto *web = new QMimeData;
        web->setText("Title plain bold red item");
        web->setHtml("<html><body style='color:#ffffff;background:#111'>"
                     "<h1>Title</h1><p style='text-align:center'>plain <b>bold</b> "
                     "<span style='color:#cc0000;font-size:18pt;font-family:Georgia'>red</span></p>"
                     "<ul><li>item</li></ul></body></html>");
        QApplication::clipboard()->setMimeData(web);
        view->paste();
        const auto &doc = view->document();
        QCOMPARE(doc.paragraphs.size(), std::size_t(3));
        QCOMPARE(doc.paragraphs[0].text(), std::u16string(u"Title"));
        QCOMPARE(QString::fromStdString(doc.paragraphs[0].style), QString("Heading1"));
        QCOMPARE(doc.paragraphs[1].alignment, wf::Alignment::Center);
        QVERIFY(doc.styleAt({1, 9}).bold);
        const auto red = doc.styleAt({1, 13});
        QCOMPARE(red.color, std::uint32_t(0xffcc0000));
        QCOMPARE(red.size, 18.0);
        QCOMPARE(red.family, std::string("Georgia"));
        // White-on-dark web text is not pasted as invisible white text.
        QCOMPARE(doc.styleAt({1, 3}).color, wf::CharacterStyle{}.color);
        QCOMPARE(doc.styleAt({1, 3}).highlight, std::uint32_t(0));
        QCOMPARE(doc.paragraphs[2].list, wf::ListKind::Bullet);
        view->undo();
        QCOMPARE(view->document().text(), std::u16string(u""));
        view->redo();

        // Copy within Wordforge and paste into another document.
        view->selectAll();
        view->copy();
        QVERIFY(QApplication::clipboard()->mimeData()->hasHtml());
        QCOMPARE(QApplication::clipboard()->text(), QString("Title\nplain bold red\nitem"));
        wf::MainWindow other;
        other.pageView()->paste();
        const auto &copy = other.pageView()->document();
        QCOMPARE(copy.text(), view->document().text());
        QCOMPARE(QString::fromStdString(copy.paragraphs[0].style), QString("Heading1"));
        QCOMPARE(copy.paragraphs[2].list, wf::ListKind::Bullet);
        QVERIFY(copy.styleAt({1, 9}).bold);
        QCOMPARE(copy.styleAt({1, 13}).color, std::uint32_t(0xffcc0000));
    }
    void spellCheckMarksMisspelledWordsAndOffersSuggestions() {
        auto &checker = wf::SpellChecker::shared();
        const auto fixtures = QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/../fixtures/spelling";
        QVERIFY(checker.load("en_TEST", {fixtures}));
        QTemporaryDir directory;
        checker.setPersonalDictionary(directory.filePath("personal.txt"));
        wf::MainWindow window;
        auto *view = window.pageView();
        view->insertText("the quikc brown fox NASA 42 नमस्ते wrld");
        // Ranges include the word at the caret; only painting skips it while typing.
        auto marked = [&] {
            QStringList words;
            const auto text = QString::fromStdU16String(view->document().paragraphs[0].text());
            for (const auto &[start, end] : view->misspellings(0))
                words << text.mid(int(start), int(end - start));
            return words;
        };
        QCOMPARE(marked(), QStringList({"quikc", "wrld"}));
        QVERIFY(checker.suggestions("quikc").contains("quick"));
        QVERIFY(view->replaceRange(0, 4, 9, "quick"));
        QCOMPARE(marked(), QStringList({"wrld"}));
        view->undo();
        QCOMPARE(marked(), QStringList({"quikc", "wrld"}));
        checker.addToDictionary("quikc");
        QCOMPARE(marked(), QStringList({"wrld"}));
        QFile personal(directory.filePath("personal.txt"));
        QVERIFY(personal.open(QIODevice::ReadOnly));
        QCOMPARE(personal.readAll().trimmed(), QByteArray("quikc"));
        if (const auto keep = qEnvironmentVariable("WORDFORGE_EVIDENCE_DIR"); !keep.isEmpty()) {
            window.resize(900, 500);
            window.show();
            QTest::keyClick(view, Qt::Key_Home);
            view->grab().save(keep + "/spelling.png");
        }
        checker.ignore("wrld");
        QVERIFY(marked().isEmpty());
        auto *toggle = window.findChild<QAction *>("spellCheckAction");
        QVERIFY(toggle && toggle->isChecked());
        toggle->trigger();
        QVERIFY(!view->spellCheck());
        toggle->trigger();
        // Leave the shared checker as other tests expect: no personal file in a temp dir.
        checker.setPersonalDictionary({});
    }
    void updateStyleFromSelectionRestylesTheDocumentAndPersists() {
        QTemporaryDir directory;
        const auto path = directory.filePath("styled.wfg");
        {
            wf::MainWindow window;
            auto *view = window.pageView();
            view->insertText("One\nTwo\nbody");
            QTest::keyClick(view, Qt::Key_Home, Qt::ControlModifier);
            window.findChild<QAction *>("styleHeading1Action")->trigger();
            QTest::keyClick(view, Qt::Key_Down);
            window.findChild<QAction *>("styleHeading1Action")->trigger();
            QTest::keyClick(view, Qt::Key_Home, Qt::ControlModifier);
            QTest::keyClick(view, Qt::Key_End, Qt::ShiftModifier);
            view->format([](auto &s) { s.family = "Georgia"; });
            QTest::keyClick(view, Qt::Key_Home);
            QTest::keyClick(view, Qt::Key_Right);
            QCOMPARE(view->selection().focus.paragraph, std::size_t(0));
            window.findChild<QAction *>("updateStyleAction")->trigger();
            QCOMPARE(view->document().paragraphs[1].runs[0].style.family, std::string("Georgia"));
            QCOMPARE(view->document().paragraphs[2].runs[0].style.family, std::string("Arial"));
            window.saveFile(path);
        }
        wf::MainWindow reopened;
        reopened.openFile(path);
        const auto &document = reopened.pageView()->document();
        QCOMPARE(wf::resolveStyle(document.styles, "Heading1").character.family,
                 std::string("Georgia"));
        QCOMPARE(document.paragraphs[1].runs[0].direct, std::uint16_t(0));
    }
    void spellCheckLoadsDictionaryFromUnicodeFolder() {
        // Users unpack the portable build anywhere; Hunspell must open it from such a folder.
        const auto fixtures = QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/../fixtures/spelling";
        QTemporaryDir directory;
        const auto folder = directory.filePath(QString::fromUtf8("словари ✓ 辞書"));
        QVERIFY(QDir().mkpath(folder));
        for (const auto *extension : {".aff", ".dic"})
            QVERIFY(QFile::copy(fixtures + "/en_TEST" + extension, folder + "/en_TEST" + extension));
        wf::SpellChecker checker;
        QVERIFY(checker.load("en_TEST", {folder}));
        QVERIFY(checker.isCorrect("quick"));
        QVERIFY(!checker.isCorrect("quikc"));
    }
    void themeChoiceIsAppliedRememberedAndDisabledControlsLookDisabled() {
        // Test mode keeps the settings file out of the user's real config folder.
        {
            wf::MainWindow window;
            window.findChild<QAction *>("themeDarkAction")->trigger();
            QVERIFY(window.palette().color(QPalette::Window).lightness() < 128);
            const auto palette = window.palette();
            QVERIFY(palette.color(QPalette::Disabled, QPalette::ButtonText) !=
                    palette.color(QPalette::Active, QPalette::ButtonText));
            window.findChild<QAction *>("themeLightAction")->trigger();
            QVERIFY(window.palette().color(QPalette::Window).lightness() > 128);
        }
        wf::MainWindow reopened;
        QVERIFY(reopened.findChild<QAction *>("themeLightAction")->isChecked());
        QVERIFY(reopened.palette().color(QPalette::Window).lightness() > 128);
        reopened.findChild<QAction *>("themeSystemAction")->trigger();
        QCOMPARE(wf::savedTheme(), wf::Theme::System);
        // The toolbar's Insert button has its own menu, not the menu bar's.
        auto *button = reopened.findChild<QToolButton *>("insertMenuButton");
        QVERIFY(button && button->menu());
        for (auto *action : reopened.menuBar()->actions())
            QVERIFY(action->menu() != button->menu());
        // A font that is not installed is named, not replaced by another family.
        auto *family = reopened.findChild<QComboBox *>("fontFamilyCombo");
        reopened.pageView()->format([](auto &s) { s.family = "No Such Family"; });
        QCOMPARE(family->currentIndex(), -1);
        QCOMPARE(family->placeholderText(), QString("No Such Family"));
    }
};
QTEST_MAIN(DesktopTest)
#include "desktop_test.moc"
