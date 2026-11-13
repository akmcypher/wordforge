#pragma once
#include <QColor>
#include <QIcon>
namespace wf {
// kind: 0 left, 1 centre, 2 right.
QIcon alignmentIcon(int kind, QColor);
// kind: 0 bullets, 1 numbering, 2 decrease indent, 3 increase indent.
QIcon paragraphIcon(int kind, QColor);
QIcon clipboardIcon(QColor);
QIcon pictureIcon(QColor);
} // namespace wf
