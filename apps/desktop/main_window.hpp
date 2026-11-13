#pragma once
#include "apps/desktop/page_view.hpp"
#include "apps/desktop/recovery_file.hpp"
#include <QMainWindow>
class QLabel;
namespace wf {
Document sampleDocument();
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    PageView *pageView() const {
        return view_;
    }
    void openFile(const QString &);
    void saveFile(const QString &);
    void recoverDrafts();

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    PageView *view_;
    QLabel *statistics_ = nullptr;
    QLabel *documentState_ = nullptr;
    QString path_;
    bool dirty_ = false;
    bool panelBeforeFocus_ = true;
    std::uint64_t statisticsRevision_ = static_cast<std::uint64_t>(-1);
    std::size_t words_ = 0;
    RecoveryFile recovery_{RecoveryFile::newPath()};
    void checkpointRecovery();
    void clearRecovery();
    bool confirmDiscard();
    void buildUi();
    void updateStatus();
    void showWordCount();
    void save(bool saveAs = false);
    void showError(const QString &);
};
} // namespace wf
