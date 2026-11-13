#pragma once
#include "core/document/document.hpp"
#include <QString>
namespace wf {
// HTML for other applications (and for pasting back into Wordforge).
QString fragmentToHtml(const std::vector<Paragraph> &);
// Reads HTML from browsers, Word or LibreOffice into formatted paragraphs. Character
// properties the HTML does not set come from `base`. Values outside Wordforge's limits
// are clamped; pictures and unsupported objects are dropped.
std::vector<Paragraph> fragmentFromHtml(const QString &, const CharacterStyle &base);
} // namespace wf
