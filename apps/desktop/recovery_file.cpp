#include "apps/desktop/recovery_file.hpp"
#include "core/formats/native.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <stdexcept>
namespace wf {
RecoveryFile::RecoveryFile(QString path) : path_(std::move(path)), lock_(path_ + ".lock") {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    // A live process keeps ownership regardless of how long the window is idle.
    lock_.setStaleLockTime(0);
    lock_.tryLock(0);
}
QString RecoveryFile::directory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
}
QString RecoveryFile::newPath() {
    return directory() + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wfg";
}
void RecoveryFile::write(const Document &document) {
    if (!locked())
        throw std::runtime_error("Recovery storage is locked or unavailable");
    const auto bytes = serializeNative(document);
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Recovery document exceeds the current 16 MiB file limit");
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes.data(), static_cast<qint64>(bytes.size())) !=
            static_cast<qint64>(bytes.size()) ||
        !file.commit())
        throw std::runtime_error("Could not write recovery snapshot");
}
Document RecoveryFile::read() const {
    if (!locked())
        throw std::runtime_error("Recovery draft belongs to another running window");
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Could not read recovery snapshot (16 MiB limit)");
    return parseNative(file.readAll().toStdString());
}
void RecoveryFile::clear() {
    if (!locked() || (QFile::exists(path_) && !QFile::remove(path_)))
        throw std::runtime_error("Could not remove recovery snapshot");
}
} // namespace wf
