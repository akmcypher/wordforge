#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <vector>
class QAction;
class QWidget;
namespace wf {
class PageView;
// One registry of user commands shared by menus, toolbars, panels and the
// command palette, so every surface triggers the same QAction and state.
class Commands : public QObject {
  public:
    Commands(PageView *, QWidget *window);
    // Registers an existing action under a palette category.
    QAction *add(QAction *, const QString &category);
    // Creates an action named "<id>Action" that runs command.
    QAction *create(const QString &id, const QString &text, const QString &category,
                    const std::function<void()> &command, const QString &hint = {});
    QAction *get(const QString &id) const;
    const std::vector<QAction *> &all() const {
        return actions_;
    }
    // Text shown to people: menu accelerators and tab-separated hints removed.
    static QString label(const QAction *);
    static QString hint(const QAction *);
    static QString category(const QAction *);
    // Redraws the drawn icons in the window's current text colour (after a theme change).
    void refreshIcons();

  private:
    PageView *view_;
    QWidget *window_;
    std::vector<QAction *> actions_;
    void sync();
};
} // namespace wf
