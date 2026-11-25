#include "apps/desktop/bookmarks_dialog.hpp"
#include "apps/desktop/page_view.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
namespace wf {
void showBookmarksDialog(PageView *view, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Bookmarks");
    dialog.setObjectName("bookmarksDialog");
    dialog.resize(430, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel("Add a name at the cursor, or select a bookmark to navigate.");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *name = new QLineEdit;
    name->setObjectName("bookmarkName");
    name->setAccessibleName("Bookmark name");
    name->setPlaceholderText("Bookmark name");
    layout->addWidget(name);
    auto *list = new QListWidget;
    list->setObjectName("bookmarksList");
    list->setAccessibleName("Document bookmarks");
    layout->addWidget(list);
    // Word's own bookmarks (for tables of contents, cross-references and the last edit)
    // have names starting with an underscore; Word hides them unless asked, as here.
    auto *hidden = new QCheckBox("Hidden bookmarks");
    hidden->setObjectName("hiddenBookmarks");
    layout->addWidget(hidden);
    auto *row = new QHBoxLayout;
    layout->addLayout(row);
    auto *add = new QPushButton("Add");
    add->setObjectName("addBookmarkButton");
    auto *go = new QPushButton("Go to");
    go->setObjectName("goToBookmarkButton");
    auto *remove = new QPushButton("Delete");
    remove->setObjectName("deleteBookmarkButton");
    row->addWidget(add);
    row->addStretch();
    row->addWidget(go);
    row->addWidget(remove);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto refresh = [&](std::uint64_t selected = 0) {
        list->clear();
        for (const auto &bookmark : view->document().bookmarks) {
            if (bookmark.name.starts_with('_') && !hidden->isChecked() && bookmark.id != selected)
                continue;
            auto *item = new QListWidgetItem(QString::fromStdString(bookmark.name), list);
            item->setData(Qt::UserRole, QVariant::fromValue(static_cast<qulonglong>(bookmark.id)));
        }
        list->sortItems();
        for (int index = 0; index < list->count(); ++index)
            if (list->item(index)->data(Qt::UserRole).toULongLong() == selected)
                list->setCurrentRow(index);
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        go->setEnabled(list->currentItem());
        remove->setEnabled(list->currentItem());
    };
    QObject::connect(list, &QListWidget::itemSelectionChanged, &dialog, [&] {
        go->setEnabled(list->currentItem());
        remove->setEnabled(list->currentItem());
    });
    QObject::connect(name, &QLineEdit::textChanged, &dialog,
                     [add](const QString &value) { add->setEnabled(!value.trimmed().isEmpty()); });
    add->setEnabled(false);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        if (const auto id = view->addBookmark(name->text().trimmed())) {
            name->clear();
            refresh(id);
        }
    });
    auto navigate = [&] {
        if (auto *item = list->currentItem())
            if (view->goToBookmark(item->data(Qt::UserRole).toULongLong()))
                dialog.accept();
    };
    QObject::connect(go, &QPushButton::clicked, &dialog, navigate);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, navigate);
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (auto *item = list->currentItem()) {
            view->removeBookmark(item->data(Qt::UserRole).toULongLong());
            refresh();
        }
    });
    QObject::connect(hidden, &QCheckBox::toggled, &dialog, [&] {
        const auto *item = list->currentItem();
        refresh(item ? item->data(Qt::UserRole).toULongLong() : 0);
    });
    refresh();
    dialog.exec();
    view->setFocus();
}
} // namespace wf
