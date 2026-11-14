#include "apps/desktop/shell.hpp"
#include "apps/desktop/commands.hpp"
#include "apps/desktop/page_view.hpp"
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QGridLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace wf {
namespace {
const QList<double> spacings{1.0, 1.15, 1.5, 2.0};
QString spacingId(double value) {
    return "lineSpacing" + QString::number(value).remove('.');
}
} // namespace

CompactToolbar::CompactToolbar(PageView *view, Commands &commands, QMenu *insert, QWidget *parent)
    : QToolBar("Toolbar", parent) {
    setObjectName("compactToolbar");
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(18, 18));
    toggleViewAction()->setEnabled(false);
    auto add = [&](const QString &id) {
        auto *action = commands.get(id);
        addAction(action);
        if (auto *button = widgetForAction(action)) {
            button->setFocusPolicy(Qt::NoFocus);
            button->setFont(action->font());
            button->setObjectName(id + "ToolButton");
        }
    };
    commands.get("undo")->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    commands.get("redo")->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    add("undo");
    add("redo");
    addSeparator();

    auto *styles = new QComboBox;
    styles->setObjectName("styleCombo");
    styles->setAccessibleName("Paragraph style");
    styles->setFocusPolicy(Qt::ClickFocus);
    styles->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addWidget(styles);
    // Items are paragraph styles, then character styles with "c:" before their identifier.
    connect(styles, &QComboBox::activated, this, [view, styles](int index) {
        const auto data = styles->itemData(index).toString();
        if (data.startsWith("c:"))
            view->applyCharacterStyle(data.mid(2).toStdString());
        else if (!data.isEmpty())
            view->applyStyle(data.toStdString());
        view->setFocus();
    });
    auto *family = new QComboBox;
    family->setObjectName("fontFamilyCombo");
    family->setAccessibleName("Font family");
    family->setFocusPolicy(Qt::ClickFocus);
    family->addItems(view->fontFamilies());
    family->setMinimumWidth(120);
    family->setMaximumWidth(150);
    family->setMaxVisibleItems(20);
    addWidget(family);
    connect(family, &QComboBox::textActivated, this, [view](const QString &value) {
        view->format([&](auto &s) { s.family = value.toStdString(); });
        view->setFocus();
    });
    auto *size = new QSpinBox;
    size->setObjectName("fontSizeSpin");
    size->setAccessibleName("Font size");
    size->setRange(1, 200);
    size->setMaximumWidth(58);
    addWidget(size);
    connect(size, &QSpinBox::valueChanged, this,
            [view](int value) { view->format([&](auto &s) { s.size = value; }); });
    addSeparator();
    for (const auto &id : {"bold", "italic", "underline", "highlight", "fontColor"})
        add(id);
    addSeparator();
    for (const auto &id : {"bullets", "numbering", "decreaseIndent", "increaseIndent"})
        add(id);
    addSeparator();
    for (const auto &id : {"alignLeft", "alignCenter", "alignRight"})
        add(id);
    addSeparator();
    auto *insertButton = new QToolButton;
    insertButton->setObjectName("insertMenuButton");
    insertButton->setText("Insert");
    insertButton->setToolTip("Insert page break, picture, date, symbol or bookmark");
    std::function<QMenu *(QMenu *, QWidget *)> clone = [&clone](QMenu *source, QWidget *owner) {
        auto *copy = new QMenu(source->title(), owner);
        for (auto *action : source->actions())
            if (action->menu())
                copy->addMenu(clone(action->menu(), copy));
            else
                copy->addAction(action);
        return copy;
    };
    insertButton->setMenu(clone(insert, insertButton));
    insertButton->setPopupMode(QToolButton::InstantPopup);
    insertButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    insertButton->setFocusPolicy(Qt::NoFocus);
    addWidget(insertButton);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spacer);
    for (const auto &id : {"find", "formatPanel", "commandPalette"})
        add(id);

    auto sync = [=] {
        const auto &s = view->typingStyle();
        const auto &p = view->document().paragraphs[selectedParagraphs(view->selection()).first];
        QSignalBlocker f(family), z(size), st(styles);
        const auto name = QString::fromStdString(s.family);
        const auto index = family->findText(name, Qt::MatchFixedString);
        family->setCurrentIndex(index);
        family->setPlaceholderText(name);
        family->setToolTip(index < 0 ? name + " is not installed; a similar font is used"
                                     : QString("Font family"));
        size->setValue(static_cast<int>(s.size));
        // Built-in styles first in their fixed order, then the document's own by name.
        std::vector<const ParagraphStyle *> list;
        for (const auto *id : {"Normal", "Title", "Heading1", "Heading2"})
            list.push_back(view->document().styles.find(id));
        std::vector<const ParagraphStyle *> custom;
        for (const auto &style : view->document().styles.paragraphStyles)
            if (!isBuiltinStyle(style.id))
                custom.push_back(&style);
        std::sort(custom.begin(), custom.end(), [](const auto *a, const auto *b) {
            return QString::fromStdString(a->name).localeAwareCompare(QString::fromStdString(b->name)) < 0;
        });
        list.insert(list.end(), custom.begin(), custom.end());
        std::vector<std::pair<QString, QString>> items; // text, data; empty data is a separator
        for (const auto *style : list)
            items.emplace_back(QString::fromStdString(style->name), QString::fromStdString(style->id));
        std::vector<const NamedCharacterStyle *> characters;
        for (const auto &style : view->document().styles.characterStyles)
            characters.push_back(&style);
        std::sort(characters.begin(), characters.end(), [](const auto *a, const auto *b) {
            return QString::fromStdString(a->name).localeAwareCompare(QString::fromStdString(b->name)) < 0;
        });
        if (!characters.empty())
            items.emplace_back(QString(), QString());
        for (const auto *style : characters)
            items.emplace_back(QString::fromStdString(style->name) + " (character)",
                               "c:" + QString::fromStdString(style->id));
        bool same = styles->count() == static_cast<int>(items.size());
        for (int i = 0; same && i < styles->count(); ++i)
            same = styles->itemData(i).toString() == items[i].second &&
                   (items[i].second.isEmpty() || styles->itemText(i) == items[i].first);
        if (!same) {
            styles->clear();
            for (const auto &[text, data] : items)
                if (data.isEmpty())
                    styles->insertSeparator(styles->count());
                else
                    styles->addItem(text, data);
        }
        styles->setCurrentIndex(styles->findData(QString::fromStdString(p.style)));
    };
    connect(view, &PageView::selectionChanged, this, sync);
    connect(view, &PageView::contentChanged, this, sync);
    sync();
}

FormatPanel::FormatPanel(PageView *view, Commands &commands, QWidget *parent)
    : QDockWidget("Format", parent) {
    setObjectName("formatPanel");
    setFeatures(QDockWidget::DockWidgetClosable);
    setAllowedAreas(Qt::RightDockWidgetArea);
    auto *area = new QScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    content->setObjectName("formatPanelContent");
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(12, 8, 12, 12);
    column->setSpacing(6);
    area->setWidget(content);
    setWidget(area);
    setMinimumWidth(240);
    auto section = [&](const QString &title) {
        auto *heading = new QLabel(title);
        heading->setObjectName("panelSection");
        column->addSpacing(column->count() ? 10 : 0);
        column->addWidget(heading);
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignLeft);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        column->addLayout(form);
        return form;
    };
    auto actionButton = [&](const QString &id) {
        auto *button = new QToolButton;
        button->setDefaultAction(commands.get(id));
        button->setFont(commands.get(id)->font());
        button->setFocusPolicy(Qt::NoFocus);
        button->setObjectName(QStringLiteral("panel") + id.at(0).toUpper() + id.mid(1));
        return button;
    };

    auto *text = section("Text");
    auto *textRow = new QHBoxLayout;
    for (const auto &id : {"strike", "clearFormatting"})
        textRow->addWidget(actionButton(id));
    textRow->addStretch();
    text->addRow(textRow);

    auto *paragraph = section("Paragraph");
    auto *spacing = new QComboBox;
    spacing->setObjectName("panelLineSpacing");
    spacing->setAccessibleName("Line spacing");
    for (double value : spacings)
        spacing->addItem(QString::number(value) + " lines", value);
    paragraph->addRow("Line spacing", spacing);
    connect(spacing, &QComboBox::activated, this,
            [&commands](int index) { commands.get(spacingId(spacings[index]))->trigger(); });
    auto spin = [&](const QString &name, const QString &label, double maximum, double step,
                    const std::function<void(ParagraphFormat &, double)> &apply) {
        auto *box = new QDoubleSpinBox;
        box->setObjectName(name);
        box->setAccessibleName(label);
        box->setRange(0, maximum);
        box->setSingleStep(step);
        box->setDecimals(0);
        box->setSuffix(" pt");
        paragraph->addRow(label, box);
        connect(box, &QDoubleSpinBox::valueChanged, this, [view, apply](double value) {
            view->formatParagraph([&](auto &p) { apply(p, value); });
        });
        return box;
    };
    auto *indent = spin("panelIndent", "Left indent", 360, 18,
                        [](auto &p, double value) { p.indentLeft = value; });
    auto *before = spin("panelSpaceBefore", "Space before", 200, 6,
                        [](auto &p, double value) { p.spaceBefore = value; });
    auto *after = spin("panelSpaceAfter", "Space after", 200, 6,
                       [](auto &p, double value) { p.spaceAfter = value; });

    auto *picture = section("Picture");
    auto *pictureSize = new QPushButton("Picture size…");
    pictureSize->setObjectName("panelPictureSize");
    pictureSize->setFocusPolicy(Qt::NoFocus);
    picture->addRow(pictureSize);
    auto *pictureAction = commands.get("pictureSize");
    connect(pictureSize, &QPushButton::clicked, pictureAction, &QAction::trigger);
    auto syncPicture = [pictureSize, pictureAction] {
        pictureSize->setEnabled(pictureAction->isEnabled());
    };
    connect(pictureAction, &QAction::changed, this, syncPicture);
    syncPicture();

    auto *table = section("Table");
    auto *tableGrid = new QGridLayout;
    tableGrid->setSpacing(4);
    int cellIndex = 0;
    for (const auto &id : {"table", "tableDelete", "tableRowAbove", "tableRowBelow",
                           "tableColumnLeft", "tableColumnRight", "tableDeleteRow",
                           "tableDeleteColumn"}) {
        auto *button = actionButton(id);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        tableGrid->addWidget(button, cellIndex / 2, cellIndex % 2);
        ++cellIndex;
    }
    table->addRow(tableGrid);

    auto *pageForm = section("Page");
    auto combo = [&](const QString &name, const QString &label,
                     const QList<std::pair<QString, QString>> &items) {
        auto *box = new QComboBox;
        box->setObjectName(name);
        box->setAccessibleName(label);
        box->setPlaceholderText("Custom");
        for (const auto &[text, id] : items)
            box->addItem(text, id);
        pageForm->addRow(label, box);
        connect(box, &QComboBox::activated, this, [&commands, box](int index) {
            commands.get(box->itemData(index).toString())->trigger();
        });
        return box;
    };
    auto *margins = combo("panelMargins", "Margins",
                          {{"Default · 2 cm", "marginsDefault"},
                           {"Normal · 1 in", "marginsNormal"},
                           {"Moderate · 0.75 in", "marginsModerate"},
                           {"Narrow · 0.5 in", "marginsNarrow"}});
    auto *orientation = combo("panelOrientation", "Orientation",
                              {{"Portrait", "portrait"}, {"Landscape", "landscape"}});
    auto *paper = combo("panelPaper", "Paper size",
                        {{"A4 · 210 × 297 mm", "paperA4"}, {"Letter · 8.5 × 11 in", "paperLetter"}});
    column->addStretch(1);

    auto sync = [=] {
        const auto &p = view->document().paragraphs[selectedParagraphs(view->selection()).first];
        const auto &page = view->document().page;
        QSignalBlocker s(spacing), i(indent), b(before), a(after), m(margins), o(orientation),
            pa(paper);
        spacing->setCurrentIndex(spacing->findData(p.lineSpacing));
        indent->setValue(p.indentLeft);
        before->setValue(p.spaceBefore);
        after->setValue(p.spaceAfter);
        const QList<double> marginValues{56.7, 72, 54, 36};
        int marginIndex = -1;
        for (int n = 0; n < marginValues.size(); ++n)
            if (std::abs(page.margin - marginValues[n]) < 0.05)
                marginIndex = n;
        margins->setCurrentIndex(marginIndex);
        orientation->setCurrentIndex(page.width > page.height ? 1 : 0);
        const auto shortSide = std::min(page.width, page.height);
        const auto longSide = std::max(page.width, page.height);
        paper->setCurrentIndex(std::abs(shortSide - 595.28) < 0.5 && std::abs(longSide - 841.89) < 0.5
                                   ? 0
                               : std::abs(shortSide - 612) < 0.5 && std::abs(longSide - 792) < 0.5
                                   ? 1
                                   : -1);
    };
    connect(view, &PageView::selectionChanged, this, sync);
    connect(view, &PageView::contentChanged, this, sync);
    sync();
}

CommandPalette::CommandPalette(Commands &commands, QWidget *parent)
    : QDialog(parent, Qt::Popup), commands_(commands), query_(new QLineEdit),
      results_(new QListWidget) {
    setObjectName("commandPalette");
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(8, 8, 8, 8);
    query_->setObjectName("paletteQuery");
    query_->setPlaceholderText("Type a command…");
    query_->setAccessibleName("Search commands");
    query_->setClearButtonEnabled(true);
    results_->setObjectName("paletteResults");
    results_->setFocusPolicy(Qt::NoFocus);
    results_->setUniformItemSizes(true);
    column->addWidget(query_);
    column->addWidget(results_);
    resize(520, 380);
    query_->installEventFilter(this);
    connect(query_, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(results_, &QListWidget::itemActivated, this, &CommandPalette::run);
}
void CommandPalette::showPalette() {
    query_->clear();
    filter({});
    if (auto *owner = parentWidget()) {
        const auto top = owner->mapToGlobal(QPoint(0, 0));
        move(top.x() + (owner->width() - width()) / 2, top.y() + 60);
    }
    show();
    activateWindow();
    query_->setFocus();
}
void CommandPalette::filter(const QString &text) {
    results_->clear();
    const auto words = text.split(' ', Qt::SkipEmptyParts);
    for (std::size_t index = 0; index < commands_.all().size(); ++index) {
        auto *action = commands_.all()[index];
        if (action->objectName() == "commandPaletteAction")
            continue;
        const auto label = Commands::label(action);
        const auto category = Commands::category(action);
        const auto haystack = category + ' ' + label;
        if (!std::all_of(words.begin(), words.end(), [&](const auto &word) {
                return haystack.contains(word, Qt::CaseInsensitive);
            }))
            continue;
        const auto hint = Commands::hint(action);
        auto *item = new QListWidgetItem(label + "   ·   " + category +
                                         (hint.isEmpty() ? QString() : "   ·   " + hint));
        item->setData(Qt::UserRole, static_cast<qulonglong>(index));
        if (!action->isEnabled())
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        results_->addItem(item);
    }
    for (int row = 0; row < results_->count(); ++row)
        if (results_->item(row)->flags() & Qt::ItemIsEnabled) {
            results_->setCurrentRow(row);
            break;
        }
}
void CommandPalette::run() {
    auto *item = results_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    auto *action = commands_.all()[item->data(Qt::UserRole).toULongLong()];
    hide();
    action->trigger();
}
bool CommandPalette::eventFilter(QObject *watched, QEvent *event) {
    if (watched == query_ && event->type() == QEvent::KeyPress) {
        const auto key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            const int step = key == Qt::Key_Down ? 1 : -1;
            for (int row = results_->currentRow() + step; row >= 0 && row < results_->count();
                 row += step)
                if (results_->item(row)->flags() & Qt::ItemIsEnabled) {
                    results_->setCurrentRow(row);
                    break;
                }
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            run();
            return true;
        }
        if (key == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
} // namespace wf
