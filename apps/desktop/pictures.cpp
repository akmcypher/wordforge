#include "apps/desktop/pictures.hpp"
#include "apps/desktop/page_view.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QBuffer>
#include <QByteArray>
#include <QImageReader>
#include <QSignalBlocker>
#include <algorithm>
namespace wf {
std::optional<std::string> pictureToPng(const std::string &bytes) {
    if (bytes.size() > 16 * 1024 * 1024)
        return std::nullopt;
    QByteArray input(bytes.data(), static_cast<qsizetype>(bytes.size()));
    QBuffer source(&input);
    if (!source.open(QIODevice::ReadOnly))
        return std::nullopt;
    QImageReader::setAllocationLimit(128);
    QImageReader reader(&source);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 16384 || size.height() > 16384 ||
        static_cast<std::uint64_t>(size.width()) * size.height() > 25000000)
        return std::nullopt;
    const auto pixels = reader.read();
    if (pixels.isNull())
        return std::nullopt;
    QByteArray png;
    QBuffer output(&png);
    if (!output.open(QIODevice::WriteOnly) || !pixels.save(&output, "PNG"))
        return std::nullopt;
    return png.toStdString();
}
void insertPictureDialog(PageView *view, QWidget *parent) {
    const auto path =
        QFileDialog::getOpenFileName(parent, "Insert picture", {}, "Pictures (*.png *.jpg *.jpeg)");
    if (!path.isEmpty())
        view->insertPicture(path);
    view->setFocus();
}
void pictureSizeDialog(PageView *view, QWidget *parent) {
    const auto selected = view->selectedImage();
    if (!selected)
        return;
    QDialog dialog(parent);
    dialog.setWindowTitle("Picture size");
    dialog.setObjectName("pictureSizeDialog");
    auto *form = new QFormLayout(&dialog);
    auto *width = new QDoubleSpinBox;
    auto *height = new QDoubleSpinBox;
    width->setObjectName("pictureWidth");
    height->setObjectName("pictureHeight");
    const auto page = view->sectionPage();
    for (auto *spin : {width, height}) {
        spin->setRange(1, 2880);
        spin->setDecimals(1);
        spin->setSuffix(" pt");
    }
    width->setMaximum(view->writingWidth());
    height->setMaximum(page.contentHeight());
    width->setValue(selected->width);
    height->setValue(selected->height);
    form->addRow("Width", width);
    form->addRow("Height", height);
    auto *lock = new QCheckBox("Lock aspect ratio");
    lock->setObjectName("pictureAspectLock");
    lock->setChecked(true);
    form->addRow(lock);
    double ratio = selected->height / selected->width;
    const double maxWidth = width->maximum(), maxHeight = height->maximum();
    auto constrain = [&] {
        const QSignalBlocker widthBlock(width), heightBlock(height);
        if (lock->isChecked()) {
            width->setRange(std::max(1.0, 1.0 / ratio), std::min(maxWidth, maxHeight / ratio));
            height->setRange(std::max(1.0, ratio), std::min(maxHeight, maxWidth * ratio));
            height->setValue(width->value() * ratio);
        } else {
            width->setRange(1, maxWidth);
            height->setRange(1, maxHeight);
        }
    };
    QObject::connect(lock, &QCheckBox::toggled, &dialog, [&](bool checked) {
        if (checked)
            ratio = height->value() / width->value();
        constrain();
    });
    constrain();
    QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        if (lock->isChecked()) {
            const QSignalBlocker blocker(height);
            height->setValue(value * ratio);
        }
    });
    QObject::connect(height, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        if (lock->isChecked()) {
            const QSignalBlocker blocker(width);
            width->setValue(value / ratio);
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (view->resizePicture(width->value(),
                                lock->isChecked() ? width->value() * ratio : height->value()))
            dialog.accept();
    });
    dialog.exec();
    view->setFocus();
}
} // namespace wf
