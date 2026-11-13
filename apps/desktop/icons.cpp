#include "apps/desktop/icons.hpp"
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
namespace wf {
namespace {
QPixmap canvas(int size) {
    QPixmap pixels(size, size);
    pixels.setDevicePixelRatio(2);
    pixels.fill(Qt::transparent);
    return pixels;
}
} // namespace
QIcon alignmentIcon(int kind, QColor color) {
    auto pixels = canvas(36);
    QPainter painter(&pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.5));
    for (int i = 0; i < 4; ++i) {
        const double width = (i % 2) ? 10 : 16;
        const double x = kind == 0 ? 2 : kind == 1 ? (20 - width) / 2 : 18 - width;
        painter.drawLine(QPointF(x, 4 + i * 4), QPointF(x + width, 4 + i * 4));
    }
    return QIcon(pixels);
}
QIcon clipboardIcon(QColor color) {
    auto pixels = canvas(48);
    QPainter painter(&pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.5));
    painter.drawRoundedRect(QRectF(4, 5, 16, 17), 2, 2);
    painter.drawRoundedRect(QRectF(8, 2, 8, 5), 1, 1);
    for (int y : {11, 15, 19})
        painter.drawLine(8, y, 16, y);
    return QIcon(pixels);
}
QIcon pictureIcon(QColor color) {
    auto pixels = canvas(48);
    QPainter painter(&pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.5));
    painter.drawRoundedRect(QRectF(2, 4, 20, 16), 2, 2);
    painter.drawEllipse(QPointF(17, 9), 1.5, 1.5);
    painter.drawPolyline(QPolygonF{QPointF(3, 18), QPointF(9, 11), QPointF(14, 16), QPointF(18, 13),
                                   QPointF(21, 17)});
    return QIcon(pixels);
}
QIcon paragraphIcon(int kind, QColor color) {
    auto pixels = canvas(36);
    QPainter painter(&pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.5));
    for (int row = 0; row < 3; ++row) {
        const auto y = 4 + row * 5;
        painter.drawLine(7, y, 16, y);
        if (kind == 0) {
            painter.setBrush(color);
            painter.drawEllipse(QPointF(2.5, y), .8, .8);
        }
        if (kind == 1) {
            auto font = painter.font();
            font.setPixelSize(6);
            painter.setFont(font);
            painter.drawText(QRectF(0, y - 3, 5, 6), Qt::AlignCenter, QString::number(row + 1));
        }
    }
    if (kind >= 2) {
        const double left = kind == 2 ? 1 : 5, right = kind == 2 ? 5 : 1;
        painter.drawLine(QPointF(left, 9), QPointF(right, 9));
        painter.drawPolyline(
            QPolygonF{QPointF(right, 6.5), QPointF(left, 9), QPointF(right, 11.5)});
    }
    return QIcon(pixels);
}
} // namespace wf
