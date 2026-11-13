#include "apps/desktop/main_window.hpp"
#include "apps/desktop/appearance.hpp"
#include "apps/desktop/bookmarks_dialog.hpp"
#include "apps/desktop/commands.hpp"
#include "apps/desktop/search_bar.hpp"
#include "apps/desktop/shell.hpp"
#include "apps/desktop/spelling.hpp"
#include "core/formats/docx.hpp"
#include "core/formats/native.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace wf {
Document sampleDocument() {
    Document document;
    document.paragraphs.clear();
    CharacterStyle body;
    body.family = "Times New Roman";
    body.size = 12;
    CharacterStyle label;
    label.size = 10;
    label.bold = true;
    label.color = 0xff805a3b;
    auto heading = body;
    heading.size = 30;
    auto subtitle = body;
    subtitle.size = 15;
    subtitle.italic = true;
    subtitle.color = 0xff776b60;
    auto add = [&](std::u16string text, CharacterStyle style, double after = 12) {
        Paragraph paragraph;
        paragraph.id = document.allocateId();
        paragraph.runs = {{std::move(text), std::move(style)}};
        paragraph.spaceAfter = after;
        document.paragraphs.push_back(std::move(paragraph));
    };
    add(u"W O R D F O R G E  /  F I E L D  N O T E S", label, 20);
    add(u"The shape of a thought", heading, 10);
    add(u"A working page, ready for your next draft.", subtitle, 24);
    add(u"A good document begins with room to think. The page provides a quiet boundary: a place "
        u"to gather an observation, test an argument, and give an idea its own shape.",
        body);
    add(u"This is editable text. Select a passage to change its font, size, emphasis or alignment. "
        u"Use the Format panel to change the paper and margins, and View to choose a comfortable "
        u"zoom.",
        body);
    auto section = label;
    section.size = 12;
    add(u"Words across languages", section, 10);
    add(u"Typography carries more than letters. नमस्ते दुनिया — مرحبا بالعالم — שלום עולם. Each "
        u"script asks for its own shaping, spacing and direction.",
        body, 20);
    add(u"Save your work as a Wordforge document to retain formatting, or export a PDF to share "
        u"the page. Plain-text export contains the words alone.",
        body);
    adoptStyles(document); // the sample's formatting is direct on top of Normal
    return document;
}
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), view_(new PageView(this)) {
    applyAppearance(*this, savedTheme());
    resize(1180, 900);
    buildUi();
    connect(view_, &PageView::contentChanged, this, [this] {
        dirty_ = true;
        updateStatus();
    });
    connect(view_, &PageView::selectionChanged, this, &MainWindow::updateStatus);
    connect(view_, &PageView::errorOccurred, this, &MainWindow::showError);
    auto *recoveryTimer = new QTimer(this);
    recoveryTimer->setObjectName("recoveryTimer");
    recoveryTimer->setInterval(30000);
    connect(recoveryTimer, &QTimer::timeout, this, &MainWindow::checkpointRecovery);
    recoveryTimer->start();
    updateStatus();
}
void MainWindow::checkpointRecovery() {
    if (!dirty_)
        return;
    try {
        recovery_.write(view_->document());
        statusBar()->showMessage("Recovery snapshot saved", 3000);
    } catch (const std::exception &error) {
        statusBar()->showMessage(QString("Recovery unavailable: ") + error.what());
    }
}
void MainWindow::clearRecovery() {
    try {
        recovery_.clear();
    } catch (const std::exception &error) {
        statusBar()->showMessage(QString("Recovery cleanup failed: ") + error.what());
    }
}
void MainWindow::recoverDrafts() {
    const QDir directory(RecoveryFile::directory());
    for (const auto &name : directory.entryList({"*.wfg"}, QDir::Files, QDir::Time)) {
        RecoveryFile abandoned(directory.filePath(name));
        if (!abandoned.locked())
            continue;
        const auto response =
            QMessageBox::question(this, "Recover document",
                                  "A draft was left by a previous Wordforge session. Restore it?\n"
                                  "Restored drafts open as unsaved documents.",
                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (response == QMessageBox::Cancel)
            return; // Keep the remaining drafts for a later session.
        try {
            if (response == QMessageBox::No) {
                abandoned.clear();
                continue;
            }
            auto document = abandoned.read();
            if (!confirmDiscard())
                return;
            // Commit to the view first; failed layout must preserve the abandoned draft.
            view_->setDocument(std::move(document));
            path_.clear();
            dirty_ = true;
            recovery_.write(view_->document());
            abandoned.clear();
            updateStatus();
            return;
        } catch (const std::exception &error) {
            showError(QString::fromUtf8(error.what()));
            return;
        }
    }
}
void MainWindow::showError(const QString &error) {
    QMessageBox::critical(this, "Wordforge", error);
}
bool MainWindow::confirmDiscard() {
    if (!dirty_)
        return true;
    const auto result = QMessageBox::warning(
        this, "Unsaved document", "Save your changes before closing this document?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (result == QMessageBox::Save) {
        save();
        return !dirty_;
    }
    return result == QMessageBox::Discard;
}
void MainWindow::openFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());
    if (file.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Document exceeds the current 16 MiB file limit");
    const auto bytes = file.readAll();
    Document document;
    if (QFileInfo(path).suffix().compare("txt", Qt::CaseInsensitive) == 0) {
        auto text = bytes.toStdString();
        if (text.starts_with("\xef\xbb\xbf"))
            text.erase(0, 3);
        Editor imported;
        imported.replace(utf16(text));
        document = std::move(imported.document);
    } else if (QFileInfo(path).suffix().compare("docx", Qt::CaseInsensitive) == 0) {
        auto imported = parseDocx(bytes.toStdString());
        if (!imported.warnings.empty()) {
            QStringList warnings;
            for (const auto &warning : imported.warnings)
                warnings.push_back(QString::fromStdString(warning));
            const auto response = QMessageBox::warning(
                this, "DOCX import limitations",
                "This document contains features Wordforge cannot preserve:\n\n" +
                    warnings.join("\n") +
                    "\n\nOpen an editable copy? The source file stays unchanged.",
                QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel);
            if (response != QMessageBox::Open)
                return;
        }
        document = std::move(imported.document);
    } else
        document = parseNative(bytes.toStdString());
    view_->setDocument(std::move(document));
    const bool importedDocx = QFileInfo(path).suffix().compare("docx", Qt::CaseInsensitive) == 0;
    path_ = importedDocx ? QString{} : path;
    dirty_ = importedDocx;
    clearRecovery();
    updateStatus();
}
void MainWindow::saveFile(const QString &path) {
    const bool plain = QFileInfo(path).suffix().compare("txt", Qt::CaseInsensitive) == 0;
    const bool docx = QFileInfo(path).suffix().compare("docx", Qt::CaseInsensitive) == 0;
    const auto bytes = plain  ? utf8(view_->document().text())
                       : docx ? serializeDocx(view_->document())
                              : serializeNative(view_->document());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes.data(), static_cast<qint64>(bytes.size())) !=
            static_cast<qint64>(bytes.size()) ||
        !file.commit())
        throw std::runtime_error("Could not atomically save the document");
    // A text export does not become the authoritative formatted document.
    if (!plain) {
        path_ = path;
        dirty_ = false;
        clearRecovery();
        updateStatus();
    }
}
void MainWindow::save(bool saveAs) {
    auto path = path_;
    QString selectedFilter;
    if (saveAs || path.isEmpty() ||
        QFileInfo(path).suffix().compare("txt", Qt::CaseInsensitive) == 0)
        path = QFileDialog::getSaveFileName(
            this, "Save document", path.isEmpty() ? "Untitled.wfg" : path,
            "Wordforge document (*.wfg);;Word document (*.docx)", &selectedFilter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += selectedFilter.contains("*.docx") ? ".docx" : ".wfg";
    try {
        saveFile(path);
    } catch (const std::exception &error) {
        showError(QString::fromUtf8(error.what()));
    }
}
void MainWindow::updateStatus() {
    documentState_->setText(dirty_            ? "Unsaved changes"
                            : path_.isEmpty() ? "New document"
                                              : "Saved locally");
    setWindowTitle((path_.isEmpty() ? QString("Untitled") : QFileInfo(path_).fileName()) +
                   (dirty_ ? " *" : "") + " — Wordforge");
    if (statisticsRevision_ != view_->document().revision) {
        words_ = countText(view_->document().text()).words;
        statisticsRevision_ = view_->document().revision;
    }
    const auto caret = view_->layout().caret(view_->selection().focus);
    const auto selection = view_->selection();
    const auto words = selection.anchor == selection.focus
                           ? QString("%1 words").arg(words_)
                           : QString("%1 of %2 words")
                                 .arg(countText(view_->selectedText().toStdU16String()).words)
                                 .arg(words_);
    statistics_->setText(QString("Page %1 of %2    ·    %3")
                             .arg(caret.page + 1)
                             .arg(view_->layout().pageCount)
                             .arg(words));
}
void MainWindow::showWordCount() {
    const auto selection = view_->selection();
    const bool selected = selection.anchor != selection.focus;
    const auto count = countText(selected ? view_->selectedText().toStdU16String()
                                          : view_->document().text());
    QDialog dialog(this);
    dialog.setObjectName("wordCountDialog");
    dialog.setWindowTitle(selected ? "Word count — selection" : "Word count");
    auto *form = new QFormLayout(&dialog);
    auto row = [form](const QString &label, std::size_t value, const QString &name) {
        auto *number = new QLabel(QLocale().toString(qulonglong(value)));
        number->setObjectName(name);
        number->setAlignment(Qt::AlignRight);
        form->addRow(label, number);
    };
    row("Words", count.words, "countWords");
    row("Characters (no spaces)", count.charactersWithoutSpaces, "countCharactersNoSpaces");
    row("Characters (with spaces)", count.characters, "countCharacters");
    row("Paragraphs", count.paragraphs, "countParagraphs");
    if (!selected)
        row("Pages", view_->layout().pageCount, "countPages");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.exec();
}
void MainWindow::buildUi() {
    auto *commands = new Commands(view_, this);
    setCentralWidget(view_);
    auto named = [](QAction *action, const QString &id) {
        action->setObjectName(id + "Action");
        return action;
    };

    auto *file = new QMenu("&File", this);
    auto *create = commands->add(named(file->addAction("&New", QKeySequence::New), "new"), "File");
    connect(create, &QAction::triggered, this, [this] {
        if (confirmDiscard()) {
            view_->setDocument({});
            path_.clear();
            dirty_ = false;
            clearRecovery();
            updateStatus();
        }
    });
    auto *open =
        commands->add(named(file->addAction("&Open…", QKeySequence::Open), "open"), "File");
    connect(open, &QAction::triggered, this, [this] {
        if (!confirmDiscard())
            return;
        auto path = QFileDialog::getOpenFileName(this, "Open document", {},
                                                 "Documents (*.wfg *.docx *.txt)");
        if (path.isEmpty())
            return;
        try {
            openFile(path);
        } catch (const std::exception &error) {
            showError(QString::fromUtf8(error.what()));
        }
    });
    connect(commands->add(named(file->addAction("Recover drafts…"), "recoverDrafts"), "File"),
            &QAction::triggered, this, &MainWindow::recoverDrafts);
    auto *saveAction =
        commands->add(named(file->addAction("&Save", QKeySequence::Save), "save"), "File");
    saveAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    auto *saveAs =
        commands->add(named(file->addAction("Save &as…", QKeySequence::SaveAs), "saveAs"), "File");
    connect(saveAs, &QAction::triggered, this, [this] { save(true); });
    file->addSeparator();
    auto *pdf = commands->add(named(file->addAction("Export &PDF…"), "exportPdf"), "File");
    connect(pdf, &QAction::triggered, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Export PDF", "Document.pdf", "PDF (*.pdf)");
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".pdf";
        try {
            view_->exportPdf(path);
        } catch (const std::exception &error) {
            showError(QString::fromUtf8(error.what()));
        }
    });
    auto *text =
        commands->add(named(file->addAction("Export plain &text…"), "exportText"), "File");
    connect(text, &QAction::triggered, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Export words without formatting",
                                                 "Document.txt", "UTF-8 text (*.txt)");
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".txt";
        try {
            saveFile(path);
        } catch (const std::exception &error) {
            showError(QString::fromUtf8(error.what()));
        }
    });
    file->addSeparator();
    auto *printAction =
        commands->add(named(file->addAction("&Print…", QKeySequence::Print), "print"), "File");
    connect(printAction, &QAction::triggered, this, [this] {
        QPrinter printer(QPrinter::HighResolution);
        view_->configurePrinter(printer);
        QPrintDialog dialog(&printer, this);
        dialog.setOption(QAbstractPrintDialog::PrintCurrentPage);
        if (dialog.exec() != QDialog::Accepted)
            return;
        try {
            view_->print(printer);
        } catch (const std::exception &error) {
            showError(QString::fromUtf8(error.what()));
        }
    });
    auto *previewAction =
        commands->add(named(file->addAction("Print pre&view…"), "printPreview"), "File");
    connect(previewAction, &QAction::triggered, this, [this] {
        QPrinter printer(QPrinter::HighResolution);
        view_->configurePrinter(printer);
        QPrintPreviewDialog preview(&printer, this);
        preview.setObjectName("printPreviewDialog");
        connect(&preview, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter *target) {
            try {
                view_->print(*target);
            } catch (const std::exception &error) {
                showError(QString::fromUtf8(error.what()));
            }
        });
        preview.exec();
    });
    file->addSeparator();
    connect(commands->add(named(file->addAction("E&xit", QKeySequence::Quit), "exit"), "File"),
            &QAction::triggered, this, &QWidget::close);

    // Editing shortcuts are handled by the document surface so they also work
    // in embedding applications. Menus invoke the same command path.
    auto *edit = new QMenu("&Edit", this);
    auto fill = [commands](QMenu *menu, std::initializer_list<const char *> ids) {
        for (const auto *id : ids)
            if (*id)
                menu->addAction(commands->get(id));
            else
                menu->addSeparator();
    };
    fill(edit, {"undo", "redo", "", "cut", "copy", "paste", "selectAll", ""});
    auto *findAction =
        commands->add(named(edit->addAction("&Find…", QKeySequence::Find), "find"), "Edit");
    findAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    auto *replaceAction = commands->add(
        named(edit->addAction("&Replace…", QKeySequence::Replace), "replace"), "Edit");

    auto *insert = new QMenu("&Insert", this);
    fill(insert, {"pageBreak", "picture", "table", "date"});
    auto *symbols = insert->addMenu("Symbol");
    for (int index = 0; index < 7; ++index)
        symbols->addAction(commands->get("symbol" + QString::number(index)));
    insert->addSeparator();
    auto *bookmarksAction = commands->add(
        named(insert->addAction("Bookmarks…", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5)),
              "bookmarks"),
        "Insert");
    connect(bookmarksAction, &QAction::triggered, this,
            [this] { showBookmarksDialog(view_, this); });

    auto *format = new QMenu("F&ormat", this);
    fill(format, {"bold", "italic", "underline", "strike", "highlight", "fontColor",
                  "clearFormatting", "", "bullets", "numbering", "decreaseIndent",
                  "increaseIndent", "", "alignLeft", "alignCenter", "alignRight"});
    fill(format->addMenu("Line spacing"),
         {"lineSpacing1", "lineSpacing115", "lineSpacing15", "lineSpacing2"});
    fill(format->addMenu("Styles"),
         {"styleNormal", "styleTitle", "styleHeading1", "styleHeading2", "", "updateStyle"});
    format->addSeparator();
    format->addAction(commands->get("pictureSize"));
    fill(format->addMenu("Table"), {"tableRowAbove", "tableRowBelow", "tableColumnLeft",
                                    "tableColumnRight", "", "tableDeleteRow",
                                    "tableDeleteColumn", "tableDelete"});
    fill(format->addMenu("Page setup"), {"marginsDefault", "marginsNormal", "marginsModerate",
                                         "marginsNarrow", "",
                                         "portrait", "landscape", "", "paperA4", "paperLetter"});

    auto *panel = new FormatPanel(view_, *commands, this);
    addDockWidget(Qt::RightDockWidgetArea, panel);
    resizeDocks({panel}, {260}, Qt::Horizontal);
    auto *panelAction = commands->add(named(panel->toggleViewAction(), "formatPanel"), "View");
    panelAction->setText("Format panel");
    panelAction->setIconText("Format");
    panelAction->setToolTip("Show or hide the format panel");
    auto *palette = new CommandPalette(*commands, this);
    auto *paletteAction =
        commands->add(named(new QAction("Command palette…", this), "commandPalette"), "View");
    paletteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    paletteAction->setIconText("Commands");
    paletteAction->setToolTip("Search all commands (Ctrl+K)");
    connect(paletteAction, &QAction::triggered, palette, &CommandPalette::showPalette);
    auto *viewMenu = new QMenu("&View", this);
    viewMenu->addAction(panelAction);
    viewMenu->addAction(paletteAction);
    viewMenu->addSeparator();
    fill(viewMenu, {"zoom100", "zoomPageWidth", ""});
    auto *themes = viewMenu->addMenu("Theme");
    auto *themeGroup = new QActionGroup(this);
    const auto current = savedTheme();
    for (auto [id, text, theme] : {std::tuple{"themeSystem", "Match system", Theme::System},
                                   std::tuple{"themeLight", "Light", Theme::Light},
                                   std::tuple{"themeDark", "Dark", Theme::Dark}}) {
        auto *action = commands->add(named(themes->addAction(text), id), "View");
        action->setText(QString("Theme: ") + text);
        action->setIconText(text);
        action->setCheckable(true);
        action->setChecked(theme == current);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, commands, theme] {
            saveTheme(theme);
            applyAppearance(*this, theme);
            commands->refreshIcons();
        });
    }
    viewMenu->addSeparator();
    auto *focusAction = commands->add(
        named(viewMenu->addAction("Focus mode", QKeySequence(Qt::Key_F11)), "focusMode"), "View");
    focusAction->setCheckable(true);

    auto *tools = new QMenu("&Tools", this);
    auto *countAction = commands->add(
        named(tools->addAction("Word count…", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)),
              "wordCount"),
        "Tools");
    connect(countAction, &QAction::triggered, this, &MainWindow::showWordCount);
    tools->addSeparator();
    auto *spellAction = commands->add(
        named(tools->addAction("Check spelling as you type", QKeySequence(Qt::Key_F7)),
              "spellCheck"),
        "Tools");
    spellAction->setCheckable(true);
    spellAction->setChecked(view_->spellCheck());
    connect(spellAction, &QAction::toggled, view_, &PageView::setSpellCheck);
    auto *languages = tools->addMenu("Spelling language");
    languages->setObjectName("spellingLanguageMenu");
    // Filled when opened, so dictionaries installed while running appear.
    connect(languages, &QMenu::aboutToShow, this, [this, languages] {
        languages->clear();
        auto &checker = SpellChecker::shared();
        const auto available = SpellChecker::availableLanguages();
        if (available.isEmpty()) {
            languages->addAction("No dictionaries installed")->setEnabled(false);
            return;
        }
        auto *group = new QActionGroup(languages);
        for (const auto &language : available) {
            auto *action = languages->addAction(language);
            action->setCheckable(true);
            action->setChecked(language == checker.language());
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, language] {
                SpellChecker::shared().load(language);
                view_->viewport()->update();
            });
        }
    });

    auto *help = new QMenu("&Help", this);
    auto *about = commands->add(named(help->addAction("About Wordforge"), "about"), "Help");
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, "Wordforge " + QApplication::applicationVersion(),
            "Native document editor\n\nQt 6 · Skia · HarfBuzz · FreeType · ICU · Hunspell\n\n"
            "Developer preview: paragraph styles, lists, tables, pictures, bookmarks, spell "
            "check, printing, find/replace, native files and PDF export, with partial DOCX "
            "import/export. Headers and footers, comments, tracked changes and advanced "
            "layout are not implemented yet.\n\nBuild " WORDFORGE_BUILD_ID);
    });
    for (auto *menu : {file, edit, insert, format, tools, viewMenu, help})
        menuBar()->addMenu(menu);
    // Shortcuts keep working while focus mode hides the menu bar.
    for (auto *action : commands->all())
        if (!action->shortcut().isEmpty())
            addAction(action);

    auto *toolbar = new CompactToolbar(view_, *commands, insert, this);
    addToolBar(toolbar);
    connect(focusAction, &QAction::toggled, this, [this, toolbar, panel](bool focus) {
        if (focus)
            panelBeforeFocus_ = panel->isVisible();
        menuBar()->setVisible(!focus);
        toolbar->setVisible(!focus);
        panel->setVisible(!focus && panelBeforeFocus_);
    });
    addToolBarBreak();
    auto *search = new SearchBar(view_, this);
    addToolBar(search);
    connect(findAction, &QAction::triggered, this, [search] { search->focusSearch(); });
    connect(replaceAction, &QAction::triggered, this, [search] { search->focusSearch(true); });
    documentState_ = new QLabel;
    documentState_->setObjectName("documentState");
    statusBar()->addPermanentWidget(documentState_);
    statistics_ = new QLabel;
    statistics_->setObjectName("statistics");
    statusBar()->addWidget(statistics_);
    auto *zoom = new QSlider(Qt::Horizontal);
    zoom->setRange(25, 300);
    zoom->setValue(100);
    zoom->setMaximumWidth(150);
    zoom->setAccessibleName("Zoom percentage");
    auto *percentage = new QLabel("100%");
    percentage->setMinimumWidth(45);
    statusBar()->addPermanentWidget(zoom);
    statusBar()->addPermanentWidget(percentage);
    connect(zoom, &QSlider::valueChanged, this, [this, percentage](int value) {
        view_->setZoom(value / 100.0);
        percentage->setText(QString::number(value) + "%");
    });
    connect(view_, &PageView::zoomChanged, this, [zoom, percentage](double value) {
        QSignalBlocker blocker(zoom);
        zoom->setValue(static_cast<int>(std::round(value * 100)));
        percentage->setText(QString::number(static_cast<int>(std::round(value * 100))) + "%");
    });
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (confirmDiscard()) {
        clearRecovery();
        event->accept();
    } else
        event->ignore();
}
} // namespace wf
