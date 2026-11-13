#pragma once
#include <QToolBar>
class QLineEdit;
namespace wf {
class PageView;
class SearchBar : public QToolBar {
  public:
    explicit SearchBar(PageView *, QWidget *parent = nullptr);
    void focusSearch(bool replacement = false);

  private:
    QLineEdit *query_;
    QLineEdit *replacement_;
};
} // namespace wf
