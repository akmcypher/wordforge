#include "apps/desktop/clipboard.hpp"
#include <QColor>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <algorithm>
#include <cmath>
namespace wf {
namespace {
QString cssColor(std::uint32_t argb) {
    return QColor::fromRgba(argb).name(QColor::HexRgb);
}
QString runCss(const CharacterStyle &s) {
    QString css = QString("font-family:'%1';font-size:%2pt;color:%3;")
                      .arg(QString::fromStdString(s.family).toHtmlEscaped())
                      .arg(s.size)
                      .arg(cssColor(s.color));
    if (s.bold)
        css += "font-weight:bold;";
    if (s.italic)
        css += "font-style:italic;";
    if (s.underline || s.strike)
        css += QString("text-decoration:%1%2;")
                   .arg(s.underline ? "underline " : "")
                   .arg(s.strike ? "line-through" : "");
    if (s.highlight)
        css += "background-color:" + cssColor(s.highlight) + ";";
    return css;
}
double clamp(double value, double low, double high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : low;
}
// Very light text is usually a dark web theme; on a white page it would be invisible.
bool tooLight(const QColor &color) {
    return color.lightnessF() > 0.92;
}
} // namespace

QString fragmentToHtml(const std::vector<Paragraph> &fragment) {
    QString html = "<html><body>";
    QString openList;
    for (const auto &p : fragment) {
        const QString list = p.list == ListKind::Bullet    ? "ul"
                             : p.list == ListKind::Decimal ? "ol"
                                                           : "";
        if (list != openList) {
            if (!openList.isEmpty())
                html += "</" + openList + ">";
            if (!list.isEmpty())
                html += "<" + list + ">";
            openList = list;
        }
        const QString tag = !list.isEmpty()                             ? "li"
                            : p.style == "Heading1"                       ? "h1"
                            : p.style == "Heading2"                       ? "h2"
                                                                     : "p";
        const char *align[] = {"left", "center", "right"};
        html += QString("<%1 style=\"text-align:%2;margin-top:%3pt;margin-bottom:%4pt;"
                        "margin-left:%5pt;line-height:%6%;\">")
                    .arg(tag)
                    .arg(align[static_cast<int>(p.alignment)])
                    .arg(p.spaceBefore)
                    .arg(p.spaceAfter)
                    .arg(p.indentLeft)
                    .arg(std::lround(p.lineSpacing * 100));
        for (const auto &run : p.runs)
            if (!run.image() && !run.text().empty())
                html += "<span style=\"" + runCss(run.style) + "\">" +
                        QString::fromStdU16String(run.text()).toHtmlEscaped().replace(
                            '\t', "&#9;") +
                        "</span>";
        html += "</" + tag + ">";
    }
    if (!openList.isEmpty())
        html += "</" + openList + ">";
    return html + "</body></html>";
}

std::vector<Paragraph> fragmentFromHtml(const QString &html, const CharacterStyle &base) {
    QTextDocument document;
    document.setHtml(html);
    std::vector<Paragraph> fragment;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        Paragraph p;
        p.runs.clear();
        const auto format = block.blockFormat();
        const auto horizontal = format.alignment() & Qt::AlignHorizontal_Mask;
        p.alignment = horizontal & Qt::AlignHCenter ? Alignment::Center
                      : horizontal & Qt::AlignRight ? Alignment::Right
                                                    : Alignment::Left;
        if (auto *list = block.textList()) {
            const auto style = list->format().style();
            p.list = style == QTextListFormat::ListDecimal ||
                             style == QTextListFormat::ListLowerAlpha ||
                             style == QTextListFormat::ListUpperAlpha ||
                             style == QTextListFormat::ListLowerRoman ||
                             style == QTextListFormat::ListUpperRoman
                         ? ListKind::Decimal
                         : ListKind::Bullet;
        }
        if (format.headingLevel() == 1)
            p.style = "Heading1";
        else if (format.headingLevel() >= 2)
            p.style = "Heading2";
        // Qt reports CSS margins in pixels at 96 dpi; Wordforge uses points.
        p.spaceBefore = clamp(format.topMargin() * 0.75, 0, 200);
        p.spaceAfter = clamp(format.bottomMargin() * 0.75, 0, 200);
        p.indentLeft = clamp(format.leftMargin() * 0.75, 0, 144);
        if (format.lineHeightType() == QTextBlockFormat::ProportionalHeight)
            p.lineSpacing = clamp(format.lineHeight() / 100.0, 1, 3);
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto piece = it.fragment();
            if (!piece.isValid())
                continue;
            std::u16string text;
            for (const auto ch : piece.text()) {
                if (ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator)
                    text += u'\n';
                else if (ch == u'\t' || (ch.unicode() >= 32 && ch != QChar::ObjectReplacementCharacter &&
                                          ch.unicode() != 0x7f))
                    text += ch.unicode();
            }
            if (text.empty())
                continue;
            const auto f = piece.charFormat();
            CharacterStyle style = base;
            const auto families = f.fontFamilies().toStringList();
            if (!families.isEmpty() && !families.front().trimmed().isEmpty())
                style.family = families.front().trimmed().left(256).toStdString();
            if (f.fontPointSize() > 0)
                style.size = clamp(f.fontPointSize(), 1, 200);
            else if (f.hasProperty(QTextFormat::FontPixelSize))
                style.size = clamp(f.intProperty(QTextFormat::FontPixelSize) * 0.75, 1, 200);
            style.size = std::round(style.size * 2) / 2;
            style.bold = f.fontWeight() >= QFont::DemiBold;
            style.italic = f.fontItalic();
            style.underline = f.fontUnderline();
            style.strike = f.fontStrikeOut();
            if (f.foreground().style() != Qt::NoBrush) {
                const auto color = f.foreground().color();
                if (color.alpha() > 0 && !tooLight(color))
                    style.color = color.rgba();
            }
            if (f.background().style() != Qt::NoBrush) {
                const auto color = f.background().color();
                style.highlight = color.alpha() > 0 && !tooLight(color) ? color.rgba() : 0;
            }
            p.runs.push_back({std::move(text), style});
        }
        if (p.runs.empty())
            p.runs.push_back({u"", base});
        fragment.push_back(std::move(p));
    }
    // Qt adds an empty final block after closing tags; do not paste a stray paragraph.
    while (fragment.size() > 1 && fragment.back().text().empty())
        fragment.pop_back();
    return fragment;
}
} // namespace wf
