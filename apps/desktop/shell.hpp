#pragma once
#include <QDialog>
#include <QDockWidget>
#include <QToolBar>
class QLineEdit;
class QListWidget;
class QMenu;
namespace wf {
class Commands;
class PageView;
// Single compact row of the most-used commands.
class CompactToolbar : public QToolBar {
  public:
    CompactToolbar(PageView *, Commands &, QMenu *insert, QWidget *parent = nullptr);
};
// Detailed paragraph, picture and page settings for the current selection.
class FormatPanel : public QDockWidget {
  public:
    FormatPanel(PageView *, Commands &, QWidget *parent = nullptr);
};
// Ctrl+K search over every registered command.
class CommandPalette : public QDialog {
  public:
    CommandPalette(Commands &, QWidget *parent = nullptr);
    void showPalette();

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    Commands &commands_;
    QLineEdit *query_;
    QListWidget *results_;
    void filter(const QString &);
    void run();
};
} // namespace wf
