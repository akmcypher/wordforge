#pragma once
#include "core/document/document.hpp"
#include <QLockFile>
#include <QString>
namespace wf {
// The lock lives as long as the window/restore operation; destruction preserves the draft.
class RecoveryFile {
  public:
    explicit RecoveryFile(QString path);
    bool locked() const {
        return lock_.isLocked();
    }
    const QString &path() const {
        return path_;
    }
    void write(const Document &);
    Document read() const;
    void clear();
    static QString directory();
    static QString newPath();

  private:
    QString path_;
    QLockFile lock_;
};
} // namespace wf
