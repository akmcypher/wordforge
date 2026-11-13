#include "apps/desktop/search_bar.hpp"
#include "apps/desktop/page_view.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
namespace wf {
SearchBar::SearchBar(PageView *view, QWidget *parent) : QToolBar("Find and replace", parent) {
    setObjectName("searchBar");
    setMovable(false);
    addWidget(new QLabel("Find"));
    query_ = new QLineEdit;
    query_->setObjectName("findQuery");
    query_->setAccessibleName("Find exact text");
    query_->setPlaceholderText("Exact text · match case");
    query_->setMaximumWidth(260);
    addWidget(query_);
    auto *next = addAction("Next");
    replacement_ = new QLineEdit;
    replacement_->setObjectName("replacementText");
    replacement_->setPlaceholderText("Replace with");
    replacement_->setAccessibleName("Replacement text");
    replacement_->setMaximumWidth(260);
    addWidget(replacement_);
    auto *replace = addAction("Replace");
    replace->setObjectName("replaceMatchAction");
    auto *result = new QLabel;
    result->setTextFormat(Qt::PlainText);
    addWidget(result);
    auto advance = [=, this] {
        result->setText(view->findNext(query_->text()) ? "Match selected" : "No match");
    };
    connect(next, &QAction::triggered, this, advance);
    connect(query_, &QLineEdit::returnPressed, this, advance);
    connect(replace, &QAction::triggered, this, [=, this] {
        if (view->replaceCurrent(query_->text(), replacement_->text())) {
            result->setText("Replaced");
            view->findNext(query_->text());
        } else
            advance();
    });
    auto *stretch = new QWidget;
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(stretch);
    auto *close = addAction(style()->standardIcon(QStyle::SP_TitleBarCloseButton), "Close find");
    close->setShortcut(QKeySequence(Qt::Key_Escape));
    close->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(close, &QAction::triggered, this, [this, view] {
        hide();
        view->setFocus();
    });
    next->setEnabled(false);
    replace->setEnabled(false);
    connect(query_, &QLineEdit::textChanged, this, [=](const QString &text) {
        next->setEnabled(!text.isEmpty());
        replace->setEnabled(!text.isEmpty());
        result->clear();
    });
    hide();
}
void SearchBar::focusSearch(bool replacement) {
    show();
    auto *field = replacement && !query_->text().isEmpty() ? replacement_ : query_;
    field->setFocus();
    field->selectAll();
}
} // namespace wf
