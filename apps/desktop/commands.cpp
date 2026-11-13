#include "apps/desktop/commands.hpp"
#include "apps/desktop/icons.hpp"
#include "apps/desktop/page_view.hpp"
#include "apps/desktop/pictures.hpp"
#include <QAction>
#include <QColorDialog>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QLocale>
#include <QWidget>
#include <algorithm>
namespace wf {
namespace {
bool selectedList(PageView *view, ListKind kind) {
    const auto [first, last] = selectedParagraphs(view->selection());
    for (auto i = first; i <= last; ++i)
        if (view->document().paragraphs[i].list != kind)
            return false;
    return true;
}
void insertTableDialog(PageView *view, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Insert table");
    dialog.setObjectName("insertTableDialog");
    auto *form = new QFormLayout(&dialog);
    auto *rows = new QSpinBox;
    rows->setObjectName("tableRows");
    rows->setRange(1, 1000);
    rows->setValue(2);
    auto *columns = new QSpinBox;
    columns->setObjectName("tableColumns");
    columns->setRange(1, 10);
    columns->setValue(3);
    form->addRow("Columns", columns);
    form->addRow("Rows", rows);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted)
        view->insertTable(static_cast<std::size_t>(rows->value()),
                          static_cast<std::size_t>(columns->value()));
}
} // namespace
QString Commands::label(const QAction *action) {
    return action->text().section('\t', 0, 0).remove('&').remove(QStringLiteral("…"));
}
QString Commands::hint(const QAction *action) {
    if (!action->shortcut().isEmpty())
        return action->shortcut().toString(QKeySequence::NativeText);
    const auto tab = action->text().section('\t', 1);
    return tab.isEmpty() ? action->property("hint").toString() : tab;
}
QString Commands::category(const QAction *action) {
    return action->property("category").toString();
}
QAction *Commands::add(QAction *action, const QString &category) {
    action->setProperty("category", category);
    actions_.push_back(action);
    return action;
}
QAction *Commands::create(const QString &id, const QString &text, const QString &category,
                          const std::function<void()> &command, const QString &hint) {
    auto *action = new QAction(text, window_);
    action->setObjectName(id + "Action");
    action->setToolTip(hint.isEmpty() ? label(action) : label(action) + " (" + hint + ")");
    action->setProperty("hint", hint);
    connect(action, &QAction::triggered, this, [this, command] {
        command();
        view_->setFocus();
    });
    return add(action, category);
}
QAction *Commands::get(const QString &id) const {
    const auto name = id + "Action";
    const auto found = std::find_if(actions_.begin(), actions_.end(),
                                    [&](const auto *action) { return action->objectName() == name; });
    return found == actions_.end() ? nullptr : *found;
}
Commands::Commands(PageView *view, QWidget *window) : QObject(window), view_(view), window_(window) {
    auto toggle = [view](bool CharacterStyle::*member) {
        return [view, member] {
            const auto value = !(view->typingStyle().*member);
            view->format([member, value](auto &s) { s.*member = value; });
        };
    };
    // Edit. The document surface owns these shortcuts; the hint only documents them.
    create("undo", "Undo", "Edit", [view] { view->undo(); }, "Ctrl+Z");
    create("redo", "Redo", "Edit", [view] { view->redo(); }, "Ctrl+Shift+Z");
    create("cut", "Cut", "Edit", [view] { view->cut(); }, "Ctrl+X");
    create("copy", "Copy", "Edit", [view] { view->copy(); }, "Ctrl+C");
    create("paste", "Paste", "Edit", [view] { view->paste(); }, "Ctrl+V");
    create("selectAll", "Select all", "Edit", [view] { view->selectAll(); }, "Ctrl+A");

    // Text.
    auto *bold = create("bold", "Bold", "Format", toggle(&CharacterStyle::bold), "Ctrl+B");
    auto *italic = create("italic", "Italic", "Format", toggle(&CharacterStyle::italic), "Ctrl+I");
    auto *underline =
        create("underline", "Underline", "Format", toggle(&CharacterStyle::underline), "Ctrl+U");
    auto *strike = create("strike", "Strikethrough", "Format", toggle(&CharacterStyle::strike));
    create("highlight", "Highlight", "Format", [view] {
        const auto value = view->typingStyle().highlight ? 0u : 0xffffff00u;
        view->format([value](auto &s) { s.highlight = value; });
    });
    create("fontColor", "Font color…", "Format", [this, view] {
        const auto color = QColorDialog::getColor(QColor::fromRgba(view->typingStyle().color),
                                                  window_, "Font color");
        if (color.isValid())
            view->format([&](auto &s) { s.color = color.rgba(); });
    });
    create("clearFormatting", "Clear formatting", "Format",
           [view] { view->clearCharacterFormatting(); });
    for (auto [action, text, flag] :
         {std::tuple{bold, "B", &QFont::setBold}, std::tuple{italic, "I", &QFont::setItalic},
          std::tuple{underline, "U", &QFont::setUnderline},
          std::tuple{strike, "abc", &QFont::setStrikeOut}}) {
        action->setIconText(text);
        auto font = action->font();
        (font.*flag)(true);
        action->setFont(font);
    }
    get("highlight")->setIconText("ab");
    get("fontColor")->setIconText("A");
    get("clearFormatting")->setIconText("A×");

    // Paragraph.
    auto list = [view](ListKind kind) {
        return [view, kind] {
            const auto value = selectedList(view, kind) ? ListKind::None : kind;
            view->formatParagraph([value](auto &f) { f.list = value; });
        };
    };
    create("bullets", "Bulleted list", "Paragraph", list(ListKind::Bullet));
    create("numbering", "Numbered list", "Paragraph", list(ListKind::Decimal));
    create("decreaseIndent", "Decrease indent", "Paragraph", [view] {
        view->formatParagraph([](auto &p) { p.indentLeft = std::max(0.0, p.indentLeft - 18); });
    });
    create("increaseIndent", "Increase indent", "Paragraph", [view] {
        view->formatParagraph([](auto &p) { p.indentLeft += 18; });
    });
    const QStringList alignments{"Left", "Center", "Right"};
    for (int index = 0; index < 3; ++index)
        create("align" + alignments[index],
               index == 1 ? QString("Center") : "Align " + alignments[index].toLower(),
               "Paragraph", [view, index] { view->align(static_cast<Alignment>(index)); });
    for (double value : {1.0, 1.15, 1.5, 2.0})
        create("lineSpacing" + QString::number(value).remove('.'),
               "Line spacing " + QString::number(value), "Paragraph", [view, value] {
                   view->formatParagraph([value](auto &p) { p.lineSpacing = value; });
               });
    const QStringList styles{"Normal", "Title", "Heading 1", "Heading 2"};
    for (int index = 0; index < 4; ++index)
        create("style" + QString(styles[index]).remove(' '), styles[index] + " style", "Styles",
               [view, index] { view->applyPreset(static_cast<ParagraphPreset>(index)); });
    create("updateStyle", "Update style to match selection", "Styles",
           [view] { view->updateStyleFromSelection(); });

    // Insert.
    create("pageBreak", "Page break", "Insert", [view] { view->insertPageBreak(); }, "Ctrl+Enter");
    create("date", "Current date", "Insert", [view] {
        view->insertText(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat));
    });
    int symbolIndex = 0;
    for (const auto &value : QStringList{"©", "®", "™", "—", "€", "✓", "§"})
        create("symbol" + QString::number(symbolIndex++), "Symbol " + value, "Insert",
               [view, value] { view->insertText(value); });
    create("picture", "Picture…", "Insert", [this, view] { insertPictureDialog(view, window_); });
    create("pictureSize", "Picture size…", "Format",
           [this, view] { pictureSizeDialog(view, window_); });

    // Tables. Structural commands are enabled only with the caret in a table.
    create("table", "Table…", "Insert", [this, view] { insertTableDialog(view, window_); })
        ->setIconText("Insert table");
    for (auto [id, text, short_, command] :
         {std::tuple{"tableRowAbove", "Insert row above", "Row above",
                     std::function<void(PageView *)>([](PageView *v) { v->tableRow(true, true); })},
          std::tuple{"tableRowBelow", "Insert row below", "Row below",
                     std::function<void(PageView *)>([](PageView *v) { v->tableRow(true); })},
          std::tuple{"tableColumnLeft", "Insert column left", "Column left",
                     std::function<void(PageView *)>([](PageView *v) { v->tableColumn(true, true); })},
          std::tuple{"tableColumnRight", "Insert column right", "Column right",
                     std::function<void(PageView *)>([](PageView *v) { v->tableColumn(true); })},
          std::tuple{"tableDeleteRow", "Delete row", "Delete row",
                     std::function<void(PageView *)>([](PageView *v) { v->tableRow(false); })},
          std::tuple{"tableDeleteColumn", "Delete column", "Delete column",
                     std::function<void(PageView *)>([](PageView *v) { v->tableColumn(false); })},
          std::tuple{"tableDelete", "Delete table", "Delete table",
                     std::function<void(PageView *)>([](PageView *v) { v->removeTable(); })}})
        create(id, text, "Table", [view, command] { command(view); })->setIconText(short_);

    // Page setup.
    auto page = [view](const std::function<void(PageStyle &)> &change) {
        return [view, change] {
            auto p = view->document().page;
            change(p);
            view->setPage(p);
        };
    };
    for (auto [id, text, value] : {std::tuple{"marginsDefault", "Margins: default (2 cm)", 56.7},
                                   std::tuple{"marginsNormal", "Margins: normal (1 in)", 72.0},
                                   std::tuple{"marginsModerate", "Margins: moderate (0.75 in)", 54.0},
                                   std::tuple{"marginsNarrow", "Margins: narrow (0.5 in)", 36.0}})
        create(id, text, "Page", page([value](auto &p) { p.margin = value; }));
    for (bool landscape : {false, true})
        create(landscape ? "landscape" : "portrait",
               landscape ? "Orientation: landscape" : "Orientation: portrait", "Page",
               page([landscape](auto &p) {
                   if ((p.width > p.height) != landscape)
                       std::swap(p.width, p.height);
               }));
    create("paperA4", "Paper: A4 (210 × 297 mm)", "Page", page([](auto &p) {
               const bool landscape = p.width > p.height;
               p.width = landscape ? 841.89 : 595.28;
               p.height = landscape ? 595.28 : 841.89;
           }));
    create("paperLetter", "Paper: Letter (8.5 × 11 in)", "Page", page([](auto &p) {
               const bool landscape = p.width > p.height;
               p.width = landscape ? 792 : 612;
               p.height = landscape ? 612 : 792;
           }));

    // View.
    create("zoom100", "Zoom 100%", "View", [view] { view->setZoom(1); });
    create("zoomPageWidth", "Zoom to page width", "View", [view] {
        view->setZoom((view->viewport()->width() - 64) / (view->document().page.width * 96 / 72));
    });

    for (const auto &id : {"bold", "italic", "underline", "strike", "highlight", "bullets",
                           "numbering", "alignLeft", "alignCenter", "alignRight", "styleNormal",
                           "styleTitle", "styleHeading1", "styleHeading2", "lineSpacing1",
                           "lineSpacing115", "lineSpacing15", "lineSpacing2"})
        get(id)->setCheckable(true);
    connect(view, &PageView::selectionChanged, this, &Commands::sync);
    connect(view, &PageView::contentChanged, this, &Commands::sync);
    sync();
    refreshIcons();
}
void Commands::refreshIcons() {
    const auto ink = window_->palette().color(QPalette::Text);
    get("paste")->setIcon(clipboardIcon(ink));
    const QStringList paragraph{"bullets", "numbering", "decreaseIndent", "increaseIndent"};
    for (int n = 0; n < 4; ++n)
        get(paragraph[n])->setIcon(paragraphIcon(n, ink));
    const QStringList alignments{"alignLeft", "alignCenter", "alignRight"};
    for (int n = 0; n < 3; ++n)
        get(alignments[n])->setIcon(alignmentIcon(n, ink));
    get("picture")->setIcon(pictureIcon(ink));
}
void Commands::sync() {
    const auto &s = view_->typingStyle();
    const auto &p = view_->document().paragraphs[selectedParagraphs(view_->selection()).first];
    get("bold")->setChecked(s.bold);
    get("italic")->setChecked(s.italic);
    get("underline")->setChecked(s.underline);
    get("strike")->setChecked(s.strike);
    get("highlight")->setChecked(s.highlight != 0);
    get("bullets")->setChecked(selectedList(view_, ListKind::Bullet));
    get("numbering")->setChecked(selectedList(view_, ListKind::Decimal));
    const QStringList alignments{"alignLeft", "alignCenter", "alignRight"};
    for (int n = 0; n < 3; ++n)
        get(alignments[n])->setChecked(static_cast<int>(p.alignment) == n);
    const QStringList styles{"styleNormal", "styleTitle", "styleHeading1", "styleHeading2"};
    for (int n = 0; n < 4; ++n)
        get(styles[n])->setChecked(p.style == presetStyleId(static_cast<ParagraphPreset>(n)));
    for (double value : {1.0, 1.15, 1.5, 2.0})
        get("lineSpacing" + QString::number(value).remove('.'))
            ->setChecked(std::abs(p.lineSpacing - value) < 0.001);
    const auto &page = view_->document().page;
    get("decreaseIndent")->setEnabled(p.indentLeft > 0);
    get("increaseIndent")
        ->setEnabled(page.width - 2 * page.margin - p.indentLeft -
                             (p.list == ListKind::None ? 0 : 18) >=
                         54 &&
                     p.indentLeft < 360);
    get("pictureSize")->setEnabled(view_->selectedImage().has_value());
    const auto cell = view_->selectedCell();
    const Table *table = nullptr;
    if (cell)
        for (const auto &t : view_->document().tables)
            if (t.id == cell->tableId)
                table = &t;
    get("tableRowAbove")->setEnabled(table && table->rows < 1000);
    get("tableRowBelow")->setEnabled(table && table->rows < 1000);
    get("tableColumnLeft")->setEnabled(table && table->columns < 10);
    get("tableColumnRight")->setEnabled(table && table->columns < 10);
    get("tableDeleteRow")->setEnabled(table && table->rows > 1);
    get("tableDeleteColumn")->setEnabled(table && table->columns > 1);
    get("tableDelete")->setEnabled(table != nullptr);
}
} // namespace wf
