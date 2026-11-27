#include "apps/desktop/commands.hpp"
#include "apps/desktop/icons.hpp"
#include "apps/desktop/page_view.hpp"
#include "apps/desktop/pictures.hpp"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QLocale>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
namespace wf {
namespace {
// The style of the paragraph holding the caret.
// The style Rename/Delete act on: the character style of the text at the caret if it
// has one, otherwise the caret paragraph's style.
std::string caretStyle(PageView *view) {
    const auto &document = view->document();
    const auto &characterStyle = document.runAt(view->selection().focus).characterStyle;
    return characterStyle.empty() ? document.paragraphs[view->selection().focus.paragraph].style
                                  : characterStyle;
}
QString styleName(PageView *view, const std::string &id) {
    const auto &styles = view->document().styles;
    if (const auto *style = styles.findCharacter(id))
        return QString::fromStdString(style->name);
    return QString::fromStdString(styles.find(id)->name);
}
// Asks for a style name; empty when cancelled or the name belongs to another style.
QString askStyleName(QWidget *window, PageView *view, const QString &title, const QString &current,
                     const std::string &except = {}) {
    for (auto name = current;;) {
        bool ok = false;
        name = QInputDialog::getText(window, title, "Style name:", QLineEdit::Normal, name, &ok)
                   .trimmed();
        if (!ok || name.isEmpty())
            return {};
        const auto &sheet = view->document().styles;
        auto same = [&](const auto &style) {
            return style.id != except && QString::fromStdString(style.name) == name;
        };
        const bool taken =
            std::any_of(sheet.paragraphStyles.begin(), sheet.paragraphStyles.end(), same) ||
            std::any_of(sheet.characterStyles.begin(), sheet.characterStyles.end(), same);
        if (!taken)
            return name;
        QMessageBox::information(window, title, "A style named \"" + name + "\" already exists.");
    }
}
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
// Page margins of the caret's section, each side on its own, in centimetres.
void marginsDialog(PageView *view, QWidget *parent) {
    auto page = view->sectionPage();
    QDialog dialog(parent);
    dialog.setWindowTitle("Margins");
    dialog.setObjectName("marginsDialog");
    auto *form = new QFormLayout(&dialog);
    constexpr double pointsPerCm = 72 / 2.54;
    std::array<QDoubleSpinBox *, 4> boxes{};
    std::array<double, 4> shown{};
    const std::array<std::pair<const char *, double PageStyle::*>, 4> sides{
        {{"Top", &PageStyle::marginTop},
         {"Bottom", &PageStyle::marginBottom},
         {"Left", &PageStyle::marginLeft},
         {"Right", &PageStyle::marginRight}}};
    for (std::size_t n = 0; n < sides.size(); ++n) {
        auto *box = boxes[n] = new QDoubleSpinBox;
        box->setObjectName(QString("margin") + sides[n].first);
        box->setAccessibleName(QString(sides[n].first) + " margin");
        box->setSuffix(" cm");
        box->setDecimals(2);
        box->setSingleStep(0.25);
        box->setRange(0, 1400 / pointsPerCm);
        box->setValue(page.*sides[n].second / pointsPerCm);
        form->addRow(sides[n].first, box);
        shown[n] = box->value();
    }
    auto *problem = new QLabel;
    problem->setObjectName("marginsProblem");
    form->addRow(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    // Keep OK off while the margins leave too little room to write.
    auto chosen = [&] {
        auto candidate = page;
        // A side left as shown keeps its exact value, not the rounded centimetres.
        for (std::size_t n = 0; n < sides.size(); ++n)
            if (boxes[n]->value() != shown[n])
                candidate.*sides[n].second = boxes[n]->value() * pointsPerCm;
        return candidate;
    };
    auto check = [&] {
        const auto candidate = chosen();
        bool valid = true;
        try {
            validatePage(candidate);
        } catch (const std::invalid_argument &) {
            valid = false;
        }
        problem->setText(valid ? QString() : "The margins leave too little room for text.");
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    for (auto *box : boxes)
        QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, check);
    check();
    if (dialog.exec() != QDialog::Accepted)
        return;
    view->setPage(chosen());
}
// Comment text: one paragraph of plain text. `id` edits an existing comment.
void commentDialog(PageView *view, QWidget *parent, std::optional<std::uint64_t> id) {
    QDialog dialog(parent);
    dialog.setWindowTitle(id ? "Edit comment" : "New comment");
    dialog.setObjectName("commentDialog");
    auto *form = new QFormLayout(&dialog);
    auto *text = new QLineEdit;
    text->setObjectName("commentText");
    text->setAccessibleName("Comment text");
    text->setMaxLength(4000);
    text->setMinimumWidth(420);
    if (id)
        for (const auto &comment : view->document().comments)
            if (comment.id == *id)
                text->setText(QString::fromStdU16String(comment.text));
    form->addRow("Comment", text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto value = text->text().trimmed().toStdU16String();
    for (auto &character : value)
        if (character < 0x20 || character == 0x7f || character == 0xfffc)
            character = u' ';
    if (id)
        view->setCommentText(*id, value);
    else
        view->addComment(value);
}
// Footnote or endnote text: one paragraph of plain text. `id` edits an existing note.
void footnoteDialog(PageView *view, QWidget *parent, std::optional<std::uint64_t> id,
                    bool endnote = false) {
    QDialog dialog(parent);
    if (id)
        endnote = view->selectedNoteIsEndnote();
    const QString kind = endnote ? "endnote" : "footnote";
    dialog.setWindowTitle((id ? "Edit " : "Insert ") + kind);
    dialog.setObjectName("footnoteDialog");
    auto *form = new QFormLayout(&dialog);
    auto *text = new QLineEdit;
    text->setObjectName("footnoteText");
    text->setAccessibleName("Footnote text");
    text->setMaxLength(2000);
    text->setMinimumWidth(420);
    if (id)
        if (const auto *note = view->document().footnote(*id))
            text->setText(QString::fromStdU16String(note->text));
    form->addRow(endnote ? "Endnote" : "Footnote", text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto value = text->text().trimmed().toStdU16String();
    for (auto &character : value)
        if (character < 0x20 || character == 0x7f || character == 0xfffc)
            character = u' ';
    if (id)
        view->setFootnoteText(*id, value);
    else
        view->insertFootnote(value, endnote);
}
// Insert or edit a link. With no selection it also asks for the text to show.
void linkDialog(PageView *view, QWidget *parent) {
    const auto existing = view->linkAtCaret();
    const auto selection = view->selection();
    const bool selected = selection.anchor != selection.focus;
    QDialog dialog(parent);
    dialog.setObjectName("linkDialog");
    dialog.setWindowTitle(existing.empty() ? "Insert link" : "Edit link");
    auto *form = new QFormLayout(&dialog);
    auto *address = new QLineEdit(QString::fromStdString(existing));
    address->setObjectName("linkAddress");
    address->setAccessibleName("Link address");
    address->setPlaceholderText("https://example.com, name@example.com or #bookmark");
    address->setMaxLength(2048);
    address->setMinimumWidth(420);
    form->addRow("Address", address);
    QLineEdit *shown = nullptr;
    if (!selected && existing.empty()) {
        shown = new QLineEdit;
        shown->setObjectName("linkText");
        shown->setAccessibleName("Text to show");
        shown->setPlaceholderText("The address itself");
        shown->setMaxLength(4096);
        form->addRow("Text to show", shown);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto target = address->text().trimmed().toStdString();
    if (target.empty())
        return;
    // Bare addresses get the scheme Word would give them.
    if (target[0] != '#' && target.find(':') == std::string::npos)
        target = (target.find('@') != std::string::npos ? "mailto:" : "https://") + target;
    try {
        validateLink(target);
    } catch (const std::exception &) {
        QMessageBox::warning(parent, "Link",
                             "Enter a web address, an e-mail address or # and a bookmark name.");
        return;
    }
    // Editing a link without a selection changes the whole link.
    if (!existing.empty() && !selected)
        if (const auto range = view->linkRange())
            view->selectRange(*range);
    view->setLink(target, shown ? shown->text().toStdU16String() : std::u16string());
}
// The caret column's width, in centimetres.
void columnWidthDialog(PageView *view, QWidget *parent) {
    const auto cell = view->selectedCell();
    if (!cell)
        return;
    const auto &document = view->document();
    const auto table = std::find_if(document.tables.begin(), document.tables.end(),
                                    [&](const auto &t) { return t.id == cell->tableId; });
    const double width = document.page.contentWidth();
    const auto edges = columnEdges(*table, width);
    constexpr double pointsPerCm = 72 / 2.54;
    QDialog dialog(parent);
    dialog.setWindowTitle("Column width");
    dialog.setObjectName("columnWidthDialog");
    auto *form = new QFormLayout(&dialog);
    auto *value = new QDoubleSpinBox;
    value->setObjectName("columnWidth");
    value->setSuffix(" cm");
    value->setDecimals(2);
    value->setSingleStep(0.25);
    value->setRange(std::ceil(minimumColumnWidth / pointsPerCm * 100) / 100,
                    std::floor((width - minimumColumnWidth * (table->columns - 1)) /
                               pointsPerCm * 100) / 100);
    value->setValue((edges[cell->column + 1] - edges[cell->column]) / pointsPerCm);
    form->addRow("Width of column " + QString::number(cell->column + 1), value);
    form->addRow(new QLabel("The other columns adjust so the table fits the page."));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted)
        view->setColumnWidth(value->value() * pointsPerCm);
}
const Table *caretTable(PageView *view) {
    const auto cell = view->selectedCell();
    if (!cell)
        return nullptr;
    for (const auto &table : view->document().tables)
        if (table.id == cell->tableId)
            return &table;
    return nullptr;
}
// Table borders: one width (0 for none), style and colour for every line of the table.
void tableBorderDialog(PageView *view, QWidget *parent) {
    const auto *table = caretTable(view);
    if (!table)
        return;
    auto border = table->border;
    QDialog dialog(parent);
    dialog.setWindowTitle("Table borders");
    dialog.setObjectName("tableBorderDialog");
    auto *form = new QFormLayout(&dialog);
    auto *width = new QDoubleSpinBox;
    width->setObjectName("borderWidth");
    width->setAccessibleName("Border width");
    width->setSuffix(" pt");
    width->setDecimals(2);
    width->setSingleStep(0.25);
    width->setRange(0, 6);
    width->setSpecialValueText("No borders");
    width->setValue(border.width);
    form->addRow("Width", width);
    auto *style = new QComboBox;
    style->setObjectName("borderStyle");
    style->setAccessibleName("Border style");
    style->addItems({"Single", "Dashed", "Dotted", "Double"});
    style->setCurrentIndex(static_cast<int>(border.style));
    form->addRow("Style", style);
    auto *colour = new QPushButton;
    colour->setObjectName("borderColor");
    colour->setAccessibleName("Border colour");
    auto paint = [colour, &border] {
        colour->setText(QColor::fromRgba(border.color).name());
        colour->setStyleSheet("QPushButton { border-left: 18px solid " +
                              QColor::fromRgba(border.color).name() + "; }");
    };
    paint();
    QObject::connect(colour, &QPushButton::clicked, &dialog, [&] {
        const auto chosen =
            QColorDialog::getColor(QColor::fromRgba(border.color), &dialog, "Border colour");
        if (chosen.isValid()) {
            border.color = chosen.rgb();
            paint();
        }
    });
    form->addRow("Colour", colour);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    border.width = width->value();
    border.style = static_cast<BorderStyle>(style->currentIndex());
    view->setTableBorder(border);
}
// Cell shading: a colour for the selected cells.
void cellShadingDialog(PageView *view, QWidget *parent) {
    const auto *table = caretTable(view);
    const auto cell = view->selectedCell();
    if (!table || !cell)
        return;
    QColorDialog dialog(parent);
    dialog.setObjectName("cellShadingDialog");
    dialog.setWindowTitle("Cell shading");
    const auto *merge = table->mergeAt(cell->row, cell->column);
    const auto current = merge ? table->fillAt(merge->row, merge->column)
                               : table->fillAt(cell->row, cell->column);
    dialog.setCurrentColor(current ? QColor::fromRgba(current) : QColor(0xf2, 0xe6, 0xd0));
    if (dialog.exec() == QDialog::Accepted && dialog.selectedColor().isValid())
        view->shadeCells(dialog.selectedColor().rgb());
}
// Header and footer: left/centre/right text for each (and for even pages), page number
// fields, first page and odd/even options.
void headerFooterDialog(PageView *view, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Header and footer");
    dialog.setObjectName("headerFooterDialog");
    auto *layout = new QGridLayout(&dialog);
    auto page = view->sectionPage();
    const char *columns[] = {"Left", "Centre", "Right"};
    for (int column = 0; column < 3; ++column)
        layout->addWidget(new QLabel(columns[column]), 0, column + 1);
    std::vector<QLineEdit *> edits;
    std::vector<QWidget *> evenRows;  // enabled with different odd and even pages
    std::vector<QWidget *> firstRows; // enabled with a different first page
    QLineEdit *focused = nullptr;
    int row = 1;
    struct BandRow {
        const char *label, *id;
        PageBand *band;
    };
    for (auto [name, id, band] : {BandRow{"Header", "header", &page.header},
                                  BandRow{"Footer", "footer", &page.footer},
                                  BandRow{"Even-page header", "evenHeader", &page.evenHeader},
                                  BandRow{"Even-page footer", "evenFooter", &page.evenFooter},
                                  BandRow{"First-page header", "firstHeader", &page.firstHeader},
                                  BandRow{"First-page footer", "firstFooter", &page.firstFooter}}) {
        auto *label = new QLabel(name);
        layout->addWidget(label, row, 0);
        auto &group = band == &page.firstHeader || band == &page.firstFooter ? firstRows
                                                                             : evenRows;
        const bool optional = band != &page.header && band != &page.footer;
        if (optional)
            group.push_back(label);
        const std::u16string *texts[] = {&band->left, &band->center, &band->right};
        for (int column = 0; column < 3; ++column) {
            auto *edit = new QLineEdit(QString::fromStdU16String(*texts[column]));
            edit->setObjectName(QString(id) + columns[column]);
            edit->setAccessibleName(QString(name) + " " + QString(columns[column]).toLower());
            if (optional)
                group.push_back(edit);
            edit->setMaxLength(500);
            edit->setMinimumWidth(150);
            layout->addWidget(edit, row, column + 1);
            edits.push_back(edit);
            QObject::connect(edit, &QLineEdit::cursorPositionChanged, &dialog,
                             [&focused, edit] { focused = edit; });
        }
        ++row;
    }
    // Field buttons insert at the last edited box (the footer centre by default).
    focused = edits[4];
    auto *fields = new QHBoxLayout;
    for (auto [label, token] : {std::pair{"Insert page number", "{PAGE}"},
                                std::pair{"Insert page count", "{NUMPAGES}"}}) {
        auto *button = new QPushButton(label);
        button->setObjectName(token == std::string("{PAGE}") ? "insertPageField"
                                                              : "insertPageCountField");
        button->setAutoDefault(false);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&focused, token] {
            focused->insert(token);
            focused->setFocus();
        });
        fields->addWidget(button);
    }
    fields->addStretch(1);
    layout->addLayout(fields, row++, 1, 1, 3);
    auto *hint = new QLabel("{PAGE} becomes the page number and {NUMPAGES} the page count, "
                            "for example \"Page {PAGE} of {NUMPAGES}\".");
    hint->setWordWrap(true);
    layout->addWidget(hint, row++, 1, 1, 3);
    auto *first = new QCheckBox("Different header and footer on the first page");
    first->setObjectName("differentFirstPage");
    first->setChecked(page.differentFirstPage);
    layout->addWidget(first, row++, 1, 1, 3);
    auto *oddEven = new QCheckBox("Different headers and footers on even pages");
    oddEven->setObjectName("differentOddEven");
    oddEven->setChecked(page.differentOddEven);
    layout->addWidget(oddEven, row++, 1, 1, 3);
    auto enabler = [](std::vector<QWidget *> rows) {
        return [rows](bool on) {
            for (auto *widget : rows)
                widget->setEnabled(on);
        };
    };
    enabler(evenRows)(page.differentOddEven);
    enabler(firstRows)(page.differentFirstPage);
    QObject::connect(oddEven, &QCheckBox::toggled, &dialog, enabler(evenRows));
    QObject::connect(first, &QCheckBox::toggled, &dialog, enabler(firstRows));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons, row, 0, 1, 4);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::u16string *targets[] = {&page.header.left,     &page.header.center,
                                 &page.header.right,    &page.footer.left,
                                 &page.footer.center,   &page.footer.right,
                                 &page.evenHeader.left, &page.evenHeader.center,
                                 &page.evenHeader.right, &page.evenFooter.left,
                                 &page.evenFooter.center, &page.evenFooter.right,
                                 &page.firstHeader.left, &page.firstHeader.center,
                                 &page.firstHeader.right, &page.firstFooter.left,
                                 &page.firstFooter.center, &page.firstFooter.right};
    for (std::size_t i = 0; i < std::size(targets); ++i) {
        auto text = edits[i]->text().toStdU16String();
        for (auto &character : text)
            if (character < 0x20 || character == 0x7f || character == 0xfffc)
                character = u' ';
        *targets[i] = text.find_first_not_of(u' ') == std::u16string::npos ? u"" : text;
    }
    page.differentFirstPage = first->isChecked();
    page.differentOddEven = oddEven->isChecked();
    if (page != view->sectionPage())
        view->setPage(page);
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
    auto *underline = create("underline", "Underline", "Format", [view] {
        const bool value = !view->typingStyle().underline;
        view->format([value](auto &s) {
            s.underline = value;
            s.underlineStyle = UnderlineStyle::Single;
        });
    }, "Ctrl+U");
    // Word's underline styles; each turns the underline on.
    for (auto [id, text, value] :
         {std::tuple{"underlineDouble", "Double", UnderlineStyle::Double},
          std::tuple{"underlineThick", "Thick", UnderlineStyle::Thick},
          std::tuple{"underlineDotted", "Dotted", UnderlineStyle::Dotted},
          std::tuple{"underlineDashed", "Dashed", UnderlineStyle::Dashed},
          std::tuple{"underlineWavy", "Wavy", UnderlineStyle::Wavy}})
        create(id, QString("Underline: ") + text, "Format", [view, value] {
            view->format([value](auto &s) {
                s.underline = true;
                s.underlineStyle = value;
            });
        })->setCheckable(true);
    auto *strike = create("strike", "Strikethrough", "Format", toggle(&CharacterStyle::strike));
    auto baseline = [view](TextBaseline target) {
        return [view, target] {
            const auto value = view->typingStyle().baseline == target ? TextBaseline::Normal : target;
            view->format([value](auto &s) { s.baseline = value; });
        };
    };
    create("superscript", "Superscript", "Format", baseline(TextBaseline::Superscript), "Ctrl+Shift+=");
    create("subscript", "Subscript", "Format", baseline(TextBaseline::Subscript), "Ctrl+=");
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
    get("superscript")->setIconText(QString::fromUtf8("x²"));
    get("subscript")->setIconText(QString::fromUtf8("x₂"));
    get("fontColor")->setIconText("A");
    get("clearFormatting")->setIconText("A×");

    // Paragraph.
    auto list = [view](ListKind kind) {
        return [view, kind] {
            const auto value = selectedList(view, kind) ? ListKind::None : kind;
            view->formatParagraph([value](auto &f) {
                f.list = value;
                f.listId = 0; // Wordforge's own list of that kind
            });
        };
    };
    create("bullets", "Bulleted list", "Paragraph", list(ListKind::Bullet));
    create("numbering", "Numbered list", "Paragraph", list(ListKind::Decimal));
    create("decreaseIndent", "Decrease indent", "Paragraph", [view] {
        view->formatParagraph([](auto &p) {
            // A list paragraph moves up a level too, as in Word.
            if (p.list != ListKind::None && p.indentLeft > 0 && p.listLevel > 0)
                --p.listLevel;
            p.indentLeft = std::max(0.0, p.indentLeft - 18);
            p.indentFirstLine = std::max(p.indentFirstLine, -p.indentLeft);
        });
    });
    create("increaseIndent", "Increase indent", "Paragraph", [view] {
        view->formatParagraph([](auto &p) {
            p.indentLeft += 18;
            if (p.list != ListKind::None && p.listLevel + 1 < listLevels)
                ++p.listLevel;
        });
    });
    const QStringList alignments{"Left", "Center", "Right", "Justify"};
    for (int index = 0; index < 4; ++index)
        create("align" + alignments[index],
               index == 1   ? QString("Center")
               : index == 3 ? QString("Justify")
                            : "Align " + alignments[index].toLower(),
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
    create("newStyle", "New style from selection…", "Styles", [this, view] {
        if (const auto name = askStyleName(window_, view, "New style", {}); !name.isEmpty())
            view->createStyleFromSelection(name);
    });
    create("newCharacterStyle", "New character style from selection…", "Styles", [this, view] {
        if (const auto name = askStyleName(window_, view, "New character style", {}); !name.isEmpty())
            view->createCharacterStyleFromSelection(name);
    });
    create("removeCharacterStyle", "Remove character style", "Styles",
           [view] { view->applyCharacterStyle({}); });
    create("renameStyle", "Rename style…", "Styles", [this, view] {
        const auto id = caretStyle(view);
        const auto current = styleName(view, id);
        if (const auto name = askStyleName(window_, view, "Rename style", current, id); !name.isEmpty())
            view->renameStyle(id, name);
    });
    create("deleteStyle", "Delete style", "Styles", [this, view] {
        const auto id = caretStyle(view);
        const auto name = styleName(view, id);
        const bool character = view->document().styles.findCharacter(id) != nullptr;
        if (QMessageBox::question(window_, "Delete style",
                                  "Delete the style \"" + name + "\"? " +
                                      (character ? "Text using it loses that formatting."
                                                 : "Paragraphs using it take the style it is "
                                                   "based on.")) ==
            QMessageBox::Yes)
            view->deleteStyle(id);
    });

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
    create("tableMerge", "Merge cells", "Table", [view] { view->mergeCells(); })
        ->setIconText("Merge");
    create("tableSplit", "Split merged cell", "Table", [view] { view->splitCell(); })
        ->setIconText("Split");
    create("tableColumnWidth", "Column width…", "Table",
           [this, view] { columnWidthDialog(view, window_); })
        ->setIconText("Width");
    create("tableBorders", "Borders…", "Table", [this, view] { tableBorderDialog(view, window_); })
        ->setIconText("Borders");
    create("tableShading", "Cell shading…", "Table",
           [this, view] { cellShadingDialog(view, window_); })
        ->setIconText("Shading");
    create("tableNoShading", "No cell shading", "Table", [view] { view->shadeCells(0); })
        ->setIconText("No shading");
    create("tableDistribute", "Distribute columns evenly", "Table",
           [view] { view->distributeColumns(); })
        ->setIconText("Even widths");

    create("footnote", "Footnote…", "Insert",
           [this, view] { footnoteDialog(view, window_, std::nullopt); });
    get("footnote")->setIconText("Footnote");
    get("footnote")->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F)); // as in Word
    create("endnote", "Endnote…", "Insert",
           [this, view] { footnoteDialog(view, window_, std::nullopt, true); });
    get("endnote")->setIconText("Endnote");
    get("endnote")->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D)); // as in Word
    create("editFootnote", "Edit footnote…", "Insert", [this, view] {
        if (const auto id = view->selectedFootnote())
            footnoteDialog(view, window_, id);
    })->setIconText("Edit note");
    create("tableOfContents", "Table of contents", "Insert",
           [view] { view->insertTableOfContents(); })
        ->setIconText("Contents");
    create("updateTableOfContents", "Update table of contents", "Insert",
           [view] { view->updateTablesOfContents(); })
        ->setIconText("Update contents");
    create("link", "Link…", "Insert", [this, view] { linkDialog(view, window_); });
    get("link")->setIconText("Link");
    get("link")->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K)); // Ctrl+K: palette
    create("removeLink", "Remove link", "Insert", [view] { view->setLink({}); })
        ->setIconText("Remove link");
    QObject::connect(view, &PageView::editLinkRequested, window_,
                     [this, view] { linkDialog(view, window_); });
    create("comment", "Comment…", "Insert",
           [this, view] { commentDialog(view, window_, std::nullopt); });
    get("comment")->setIconText("Comment");
    get("comment")->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M)); // as in Word
    create("editComment", "Edit comment…", "Insert", [this, view] {
        if (const auto id = view->commentAtCaret())
            commentDialog(view, window_, id);
    })->setIconText("Edit comment");
    create("deleteComment", "Delete comment", "Insert", [view] {
        if (const auto id = view->commentAtCaret())
            view->deleteComment(*id);
    })->setIconText("Delete comment");
    // Review: tracked changes.
    create("trackChanges", "Track changes", "Review",
           [this, view] { view->setTrackChanges(get("trackChanges")->isChecked()); })
        ->setIconText("Track");
    get("trackChanges")->setCheckable(true);
    get("trackChanges")->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)); // as in Word
    get("alignJustify")->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J)); // as in Word
    create("nextChange", "Next change", "Review", [view] { view->nextChange(); })
        ->setIconText("Next change");
    create("acceptChange", "Accept change", "Review", [view] {
        view->acceptChanges();
        view->nextChange();
    })->setIconText("Accept");
    create("rejectChange", "Reject change", "Review", [view] {
        view->rejectChanges();
        view->nextChange();
    })->setIconText("Reject");
    create("acceptAllChanges", "Accept all changes", "Review",
           [view] { view->acceptAllChanges(); })
        ->setIconText("Accept all");
    create("rejectAllChanges", "Reject all changes", "Review",
           [view] { view->rejectAllChanges(); })
        ->setIconText("Reject all");
    create("headerFooter", "Header and footer…", "Insert",
           [this, view] { headerFooterDialog(view, window_); })
        ->setIconText("Header & footer");

    // Page setup.
    auto page = [view](const std::function<void(PageStyle &)> &change) {
        return [view, change] {
            auto p = view->sectionPage();
            change(p);
            view->setPage(p);
        };
    };
    for (auto [id, text, value] : {std::tuple{"marginsDefault", "Margins: default (2 cm)", 56.7},
                                   std::tuple{"marginsNormal", "Margins: normal (1 in)", 72.0},
                                   std::tuple{"marginsModerate", "Margins: moderate (0.75 in)", 54.0},
                                   std::tuple{"marginsNarrow", "Margins: narrow (0.5 in)", 36.0}})
        create(id, text, "Page", page([value](auto &p) { p.setMargins(value); }));
    create("marginsCustom", "Margins: custom…", "Page", [this, view] { marginsDialog(view, window_); })
        ->setIconText("Custom margins");
    for (bool landscape : {false, true})
        create(landscape ? "landscape" : "portrait",
               landscape ? "Orientation: landscape" : "Orientation: portrait", "Page",
               page([landscape](auto &p) {
                   if ((p.width > p.height) != landscape)
                       std::swap(p.width, p.height);
               }));
    for (std::size_t count : {1, 2, 3})
        create(QString("columns%1").arg(count),
               count == 1 ? QString("Columns: one") : QString("Columns: %1").arg(count == 2 ? "two" : "three"),
               "Page", page([count](auto &p) {
                   p.columns = count;
                   p.columnGap = 36;
               }));
    create("sectionBreakNextPage", "Section break (next page)", "Insert",
           [view] { view->insertSectionBreak(false); })
        ->setIconText("Section break");
    create("sectionBreakContinuous", "Section break (continuous)", "Insert",
           [view] { view->insertSectionBreak(true); })
        ->setIconText("Continuous break");
    create("removeSectionBreak", "Remove section break", "Insert",
           [view] { view->removeSectionBreak(); })
        ->setIconText("Remove break");
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
        view->setZoom((view->viewport()->width() - 64) / (view->sectionPage().width * 96 / 72));
    });

    for (const auto &id : {"bold", "italic", "underline", "strike", "superscript", "subscript", "highlight", "bullets",
                           "numbering", "alignLeft", "alignCenter", "alignRight",
                           "alignJustify", "styleNormal",
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
    const QStringList alignments{"alignLeft", "alignCenter", "alignRight", "alignJustify"};
    for (int n = 0; n < 4; ++n)
        get(alignments[n])->setIcon(alignmentIcon(n, ink));
    get("picture")->setIcon(pictureIcon(ink));
}
void Commands::sync() {
    const auto &s = view_->typingStyle();
    const auto &p = view_->document().paragraphs[selectedParagraphs(view_->selection()).first];
    get("bold")->setChecked(s.bold);
    get("italic")->setChecked(s.italic);
    get("underline")->setChecked(s.underline);
    for (auto [id, value] : {std::pair{"underlineDouble", UnderlineStyle::Double},
                             std::pair{"underlineThick", UnderlineStyle::Thick},
                             std::pair{"underlineDotted", UnderlineStyle::Dotted},
                             std::pair{"underlineDashed", UnderlineStyle::Dashed},
                             std::pair{"underlineWavy", UnderlineStyle::Wavy}})
        get(id)->setChecked(s.underline && s.underlineStyle == value);
    get("strike")->setChecked(s.strike);
    get("superscript")->setChecked(s.baseline == TextBaseline::Superscript);
    get("subscript")->setChecked(s.baseline == TextBaseline::Subscript);
    get("highlight")->setChecked(s.highlight != 0);
    get("bullets")->setChecked(selectedList(view_, ListKind::Bullet));
    get("numbering")->setChecked(selectedList(view_, ListKind::Decimal));
    const QStringList alignments{"alignLeft", "alignCenter", "alignRight", "alignJustify"};
    for (int n = 0; n < 4; ++n)
        get(alignments[n])->setChecked(static_cast<int>(p.alignment) == n);
    const QStringList styles{"styleNormal", "styleTitle", "styleHeading1", "styleHeading2"};
    for (int n = 0; n < 4; ++n)
        get(styles[n])->setChecked(p.style == presetStyleId(static_cast<ParagraphPreset>(n)));
    // Rename/Delete name the style they act on.
    const auto target = caretStyle(view_);
    const bool custom = !isBuiltinStyle(target);
    get("renameStyle")->setEnabled(custom);
    get("deleteStyle")->setEnabled(custom);
    get("renameStyle")->setText(custom ? "Rename style “" + styleName(view_, target) + "”…"
                                       : QString("Rename style…"));
    get("deleteStyle")->setText(custom ? "Delete style “" + styleName(view_, target) + "”"
                                       : QString("Delete style"));
    const auto selection = view_->selection();
    get("newCharacterStyle")->setEnabled(selection.anchor != selection.focus);
    get("removeCharacterStyle")->setEnabled(
        !view_->document().runAt(selection.focus).characterStyle.empty() ||
        selection.anchor != selection.focus);
    for (double value : {1.0, 1.15, 1.5, 2.0})
        get("lineSpacing" + QString::number(value).remove('.'))
            ->setChecked(std::abs(p.lineSpacing - value) < 0.001);
    const auto page = view_->sectionPage();
    get("decreaseIndent")->setEnabled(p.indentLeft > 0);
    get("increaseIndent")
        ->setEnabled(page.contentWidth() - p.indentLeft -
                             std::max(0.0, p.indentFirstLine) - p.indentRight -
                             (p.list == ListKind::None ? 0 : 18) >=
                         54 &&
                     p.indentLeft < 360);
    get("pictureSize")->setEnabled(view_->selectedImage().has_value());
    get("editFootnote")->setEnabled(view_->selectedFootnote().has_value());
    get("updateTableOfContents")->setEnabled(view_->hasTableOfContents());
    {
        bool laterBreak = false;
        const auto &paragraphs = view_->document().paragraphs;
        for (auto i = view_->selection().focus.paragraph; i < paragraphs.size() && !laterBreak; ++i)
            laterBreak = paragraphs[i].sectionBreak.has_value();
        get("removeSectionBreak")->setEnabled(laterBreak);
    }
    get("removeLink")->setEnabled(!view_->linkAtCaret().empty() ||
                                  view_->selection().anchor != view_->selection().focus);
    const bool inComment = view_->commentAtCaret().has_value();
    get("editComment")->setEnabled(inComment);
    get("deleteComment")->setEnabled(inComment);
    get("trackChanges")->setChecked(view_->document().trackChanges);
    const bool anyChanges = !view_->document().revisions.empty();
    const bool hereChanges = anyChanges && view_->selectionHasChanges();
    get("acceptChange")->setEnabled(hereChanges);
    get("rejectChange")->setEnabled(hereChanges);
    for (const auto *id : {"nextChange", "acceptAllChanges", "rejectAllChanges"})
        get(id)->setEnabled(anyChanges);
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
    const auto &paragraphs = view_->document().paragraphs;
    const auto other = paragraphs[view_->selection().anchor.paragraph].cell;
    get("tableMerge")->setEnabled(table && other && other->tableId == cell->tableId &&
                                  *other != *cell);
    get("tableSplit")->setEnabled(table && table->mergeAt(cell->row, cell->column));
    get("tableColumnWidth")->setEnabled(table && table->columns > 1);
    get("tableDistribute")->setEnabled(table && table->columns > 1 && !table->widths.empty());
    get("tableBorders")->setEnabled(table != nullptr);
    get("tableShading")->setEnabled(table != nullptr);
    get("tableNoShading")->setEnabled(table && !table->shading.empty());
}
} // namespace wf
