#pragma once
#include <optional>
#include <string>
class QWidget;
namespace wf {
class PageView;
void insertPictureDialog(PageView *, QWidget *);
void pictureSizeDialog(PageView *, QWidget *);
// Decodes picture bytes in any format Qt reads (JPEG, GIF, BMP...) within the preview
// limits and re-encodes them as PNG; nothing when that fails. Used for DOCX import.
std::optional<std::string> pictureToPng(const std::string &bytes);
} // namespace wf
