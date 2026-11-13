#include "apps/desktop/page_view.hpp"
#include "apps/desktop/clipboard.hpp"
#include "apps/desktop/spelling.hpp"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainterPath>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QImageReader>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPageLayout>
#include <QPainter>
#include <QPrinter>
#include <QSaveFile>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <include/codec/SkJpegDecoder.h>
#include <include/core/SkData.h>
#include <include/core/SkPaint.h>
#include <include/core/SkStream.h>
#include <include/core/SkSurface.h>
#include <include/docs/SkPDFDocument.h>
#include <include/encode/SkJpegEncoder.h>
#include <stdexcept>
namespace wf {
PageView::PageView(QWidget *parent)
    : QAbstractScrollArea(parent), shaper_(systemFontFiles()), engine_(shaper_) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setAccessibleName("Document editing surface");
    blink_.setInterval(std::max(100, QApplication::cursorFlashTime() / 2));
    connect(&blink_, &QTimer::timeout, this, [this] {
        caretVisible_ = !caretVisible_;
        viewport()->update(caretRectangle());
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PageView::invalidateScene);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &PageView::invalidateScene);
    refresh(false);
}
double PageView::scale() const {
    return zoom_ * 96.0 / 72.0;
}
double PageView::pageLeft() const {
    return std::max(24.0, (viewport()->width() - layout_.page.width * scale()) / 2) -
           horizontalScrollBar()->value();
}
double PageView::pageTop(std::size_t index) const {
    return 24 + index * (layout_.page.height * scale() + 24) - verticalScrollBar()->value();
}
void PageView::updateScrollbars() {
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setRange(
        0, std::max(
               0, static_cast<int>(layout_.pageCount * (layout_.page.height * scale() + 24) + 24) -
                      viewport()->height()));
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(layout_.page.width * scale() + 48) - viewport()->width()));
}
void PageView::revealCaret() {
    const auto caret = layout_.caret(displayCaret_);
    const auto top = pageTop(caret.page) + caret.y * scale(), bottom = top + caret.height * scale();
    if (top < 12)
        verticalScrollBar()->setValue(verticalScrollBar()->value() + static_cast<int>(top - 12));
    else if (bottom > viewport()->height() - 12)
        verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                      static_cast<int>(bottom - viewport()->height() + 12));
    const auto x = pageLeft() + caret.x * scale();
    if (x < 12)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + static_cast<int>(x - 12));
    else if (x > viewport()->width() - 12)
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                        static_cast<int>(x - viewport()->width() + 12));
}
void PageView::refresh(bool content) {
    displayCaret_ = editor_.selection.focus;
    if (preedit_.isEmpty())
        layout_ = engine_.compose(editor_.document);
    else {
        Editor preview;
        preview.reset(editor_.document);
        preview.selection = editor_.selection;
        preview.typingStyle = editor_.typingStyle;
        preview.typingStyle.underline = true;
        preview.replace(preedit_.toStdU16String());
        LayoutEngine previewEngine(shaper_);
        layout_ = previewEngine.compose(preview.document);
        displayCaret_ = preview.selection.focus;
    }
    renderer_.synchronize(layout_);
    invalidateScene();
    present(content);
}
void PageView::present(bool content) {
    selectionDirty_ = true;
    updateScrollbars();
    caretVisible_ = true;
    revealCaret();
    viewport()->update();
    if (content)
        emit contentChanged();
    emit selectionChanged();
    if (auto *input = QApplication::inputMethod())
        input->update(Qt::ImQueryAll);
}
bool PageView::change(const std::function<void(Editor &)> &command) {
    try {
        const auto revision = editor_.document.revision;
        Editor candidate = editor_;
        command(candidate);
        if (candidate.document.revision == revision && preedit_.isEmpty()) {
            editor_ = std::move(candidate);
            displayCaret_ = editor_.selection.focus;
            present(false);
            return true;
        }
        auto nextLayout = engine_.compose(candidate.document);
        renderer_.synchronize(nextLayout);
        invalidateScene();
        editor_ = std::move(candidate);
        layout_ = std::move(nextLayout);
        preedit_.clear();
        displayCaret_ = editor_.selection.focus;
        present(revision != editor_.document.revision);
        return true;
    } catch (const std::exception &error) {
        engine_.clear();
        refresh(false);
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}
void PageView::setDocument(Document d) {
    Editor candidate = editor_;
    candidate.reset(std::move(d));
    try {
        auto nextLayout = engine_.compose(candidate.document);
        renderer_.synchronize(nextLayout);
        invalidateScene();
        editor_ = std::move(candidate);
        layout_ = std::move(nextLayout);
        preedit_.clear();
        displayCaret_ = editor_.selection.focus;
        present(true);
        verticalScrollBar()->setValue(0);
        horizontalScrollBar()->setValue(0);
    } catch (...) {
        engine_.clear();
        refresh(false);
        throw;
    }
}
QStringList PageView::fontFamilies() const {
    QStringList names;
    for (const auto &family : shaper_.families())
        names.push_back(QString::fromStdString(family));
    return names;
}
bool PageView::findNext(const QString &query) {
    const auto needle = query.toStdU16String();
    if (needle.empty() || needle.find(u'\n') != std::u16string::npos)
        return false;
    if (auto *input = QApplication::inputMethod())
        input->commit();
    const auto from = std::max(editor_.selection.anchor, editor_.selection.focus);
    for (int pass = 0; pass < 2; ++pass) {
        const auto first = pass ? 0 : from.paragraph;
        const auto last = pass ? from.paragraph + 1 : editor_.document.paragraphs.size();
        for (auto index = first; index < last; ++index) {
            const auto text = editor_.document.paragraphs[index].text();
            const auto begin = !pass && index == from.paragraph ? from.offset : 0;
            auto found = text.find(needle, begin);
            if (found == std::u16string::npos)
                continue;
            const auto boundaries = graphemeBoundaries(text);
            for (; found != std::u16string::npos &&
                   (!pass || index != from.paragraph || found < from.offset);
                 found = text.find(needle, found + 1)) {
                if (!std::binary_search(boundaries.begin(), boundaries.end(), found) ||
                    !std::binary_search(boundaries.begin(), boundaries.end(),
                                        found + needle.size()))
                    continue;
                moveCaret({index, found}, false);
                editor_.selection.focus = {index, found + needle.size()};
                displayCaret_ = editor_.selection.focus;
                present(false);
                return true;
            }
        }
    }
    return false;
}
bool PageView::replaceCurrent(const QString &query, const QString &replacement) {
    if (query.isEmpty() || selectedText() != query)
        return false;
    const auto revision = editor_.document.revision;
    insertText(replacement);
    return editor_.document.revision != revision;
}
void PageView::insertText(const QString &text) {
    change([&](Editor &candidate) { candidate.replace(text.toStdU16String()); });
}
bool PageView::insertPicture(const QString &path) {
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
            throw std::runtime_error(
                "Could not open picture, or file exceeds the 16 MiB preview limit");
        QImageReader::setAllocationLimit(128);
        QImageReader reader(&file);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 16384 || size.height() > 16384 ||
            static_cast<std::uint64_t>(size.width()) * size.height() > 25000000)
            throw std::runtime_error(
                "Picture exceeds the 25 megapixel preview limit or is invalid");
        const auto pixels = reader.read();
        if (pixels.isNull())
            throw std::runtime_error(reader.errorString().toStdString());
        QByteArray png;
        QBuffer output(&png);
        if (!output.open(QIODevice::WriteOnly) || !pixels.save(&output, "PNG"))
            throw std::runtime_error("Could not normalize picture pixels");
        auto resource = std::make_shared<const ImageResource>(
            ImageResource{static_cast<std::uint32_t>(pixels.width()),
                          static_cast<std::uint32_t>(pixels.height()), png.toStdString()});
        validateImageResource(*resource);
        const auto &page = document().page;
        const auto &paragraph = document().paragraphs[selection().focus.paragraph];
        const auto maxWidth = writingWidth();
        const auto maxHeight =
            (page.height - 2 * page.margin) / paragraph.lineSpacing - typingStyle().size;
        double width = pixels.width() * 0.75, height = pixels.height() * 0.75;
        const auto fit = std::min({1.0, 360.0 / width, maxWidth / width, maxHeight / height});
        width *= fit;
        height *= fit;
        return change([&](Editor &candidate) {
            candidate.insertImage(resource, width, height,
                                  QFileInfo(path).fileName().toStdString());
        });
    } catch (const std::exception &error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}
bool PageView::insertTable(std::size_t rows, std::size_t columns) {
    return change([&](Editor &candidate) { candidate.insertTable(rows, columns); });
}
bool PageView::tableRow(bool insert, bool before) {
    return change([&](Editor &candidate) { candidate.tableRow(insert, before); });
}
bool PageView::tableColumn(bool insert, bool before) {
    return change([&](Editor &candidate) { candidate.tableColumn(insert, before); });
}
bool PageView::removeTable() {
    return change([](Editor &candidate) { candidate.removeTable(); });
}
double PageView::writingWidth() const {
    const auto &page = document().page;
    const auto &paragraph = document().paragraphs[selection().focus.paragraph];
    double width = page.width - 2 * page.margin;
    if (paragraph.cell) {
        const auto table = std::find_if(document().tables.begin(), document().tables.end(),
                                        [&](const auto &t) { return t.id == paragraph.cell->tableId; });
        if (table != document().tables.end())
            width = width / static_cast<double>(table->columns) - 8; // 4 pt cell padding
    }
    return width - paragraph.indentLeft - (paragraph.list == ListKind::None ? 0 : 36);
}
bool PageView::resizePicture(double width, double height) {
    return change([&](Editor &candidate) { candidate.resizeImage(width, height); });
}
std::uint64_t PageView::addBookmark(const QString &name) {
    std::uint64_t id = 0;
    const bool committed =
        change([&](Editor &candidate) { id = candidate.addBookmark(name.toStdString()); });
    return committed ? id : 0;
}
void PageView::removeBookmark(std::uint64_t id) {
    change([id](Editor &candidate) { candidate.removeBookmark(id); });
}
bool PageView::goToBookmark(std::uint64_t id) {
    if (auto *input = QApplication::inputMethod())
        input->commit();
    if (!editor_.goToBookmark(id))
        return false;
    moveCaret(editor_.selection.focus, false);
    setFocus();
    return true;
}
void PageView::format(const std::function<void(CharacterStyle &)> &command) {
    change([&](Editor &candidate) { candidate.format(command); });
}
void PageView::align(Alignment alignment) {
    change([&](Editor &candidate) { candidate.align(alignment); });
}
void PageView::formatParagraph(const std::function<void(ParagraphFormat &)> &command) {
    change([&](Editor &candidate) { candidate.formatParagraph(command); });
}
void PageView::applyPreset(ParagraphPreset preset) {
    change([&](Editor &candidate) { candidate.applyPreset(preset); });
}
void PageView::updateStyleFromSelection() {
    change([](Editor &candidate) { candidate.updateStyleFromSelection(); });
}
void PageView::clearCharacterFormatting() {
    change([](Editor &candidate) { candidate.clearCharacterFormatting(); });
}
void PageView::insertPageBreak() {
    change([](Editor &candidate) { candidate.insertPageBreak(); });
}
void PageView::setPage(PageStyle page) {
    change([&](Editor &candidate) { candidate.setPage(page); });
}
void PageView::undo() {
    change([&](Editor &candidate) { candidate.undo(); });
}
void PageView::redo() {
    change([&](Editor &candidate) { candidate.redo(); });
}
QString PageView::selectedText() const {
    const auto start = std::min(editor_.selection.anchor, editor_.selection.focus),
               end = std::max(editor_.selection.anchor, editor_.selection.focus);
    std::u16string result;
    for (auto i = start.paragraph; i <= end.paragraph; ++i) {
        if (i > start.paragraph)
            result += u'\n';
        auto text = editor_.document.paragraphs[i].text();
        const auto a = i == start.paragraph ? start.offset : 0,
                   b = i == end.paragraph ? end.offset : text.size();
        result += text.substr(a, b - a);
    }
    return QString::fromStdU16String(result);
}
void PageView::copy() {
    if (editor_.selection.anchor == editor_.selection.focus)
        return;
    // Plain text for any application, HTML so formatting survives a paste.
    auto *data = new QMimeData;
    data->setText(selectedText());
    data->setHtml(fragmentToHtml(editor_.copySelection()));
    QApplication::clipboard()->setMimeData(data);
}
void PageView::cut() {
    copy();
    insertText("");
}
void PageView::paste() {
    const auto *data = QApplication::clipboard()->mimeData();
    if (!data)
        return;
    if (data->hasHtml()) {
        try {
            const auto fragment = fragmentFromHtml(data->html(), editor_.typingStyle);
            if (!fragment.empty() &&
                change([&](Editor &candidate) { candidate.insertFragment(fragment); }))
                return;
        } catch (const std::exception &error) {
            emit errorOccurred(QString::fromUtf8(error.what()));
        }
    }
    // Plain text, or HTML that could not be pasted with its formatting.
    insertText(data->text());
}
void PageView::selectAll() {
    editor_.selection = {
        {0, 0},
        {editor_.document.paragraphs.size() - 1, editor_.document.paragraphs.back().text().size()}};
    const bool composing = !preedit_.isEmpty();
    preedit_.clear();
    displayCaret_ = editor_.selection.focus;
    if (composing)
        refresh(false);
    else
        present(false);
}
void PageView::setZoom(double zoom) {
    if (!std::isfinite(zoom))
        return;
    zoom_ = std::clamp(zoom, 0.25, 3.0);
    updateScrollbars();
    invalidateScene();
    emit zoomChanged(zoom_);
}
void PageView::invalidateScene() {
    sceneDirty_ = true;
    selectionDirty_ = true;
    viewport()->update();
}
QRect PageView::caretRectangle() const {
    const auto caret = layout_.caret(displayCaret_);
    return QRectF(pageLeft() + caret.x * scale(), pageTop(caret.page) + caret.y * scale(), 1.5,
                  caret.height * scale())
        .toAlignedRect()
        .adjusted(-1, -1, 1, 1);
}
void PageView::paintEvent(QPaintEvent *) {
    const auto dpr = viewport()->devicePixelRatioF();
    const QSize dimensions(static_cast<int>(std::ceil(viewport()->width() * dpr)),
                           static_cast<int>(std::ceil(viewport()->height() * dpr)));
    if (dimensions.isEmpty())
        return;
    if (buffer_.size() != dimensions || buffer_.devicePixelRatio() != dpr) {
        buffer_ = QImage(dimensions, QImage::Format_ARGB32_Premultiplied);
        sceneDirty_ = true;
        selectionDirty_ = true;
    }
    const bool selected = editor_.selection.anchor != editor_.selection.focus && preedit_.isEmpty();
    const bool paintScene = sceneDirty_, paintSelection = selected && selectionDirty_;
    if (paintScene || paintSelection) {
        if (paintSelection && selectionBuffer_.size() != dimensions)
            selectionBuffer_ = QImage(dimensions, QImage::Format_ARGB32_Premultiplied);
        auto wrap = [](QImage &image) {
            return SkSurfaces::WrapPixels(SkImageInfo::Make(image.width(), image.height(),
                                                            kBGRA_8888_SkColorType,
                                                            kPremul_SkAlphaType),
                                          image.bits(), image.bytesPerLine());
        };
        auto surface = paintScene ? wrap(buffer_) : nullptr;
        auto overlay = paintSelection ? wrap(selectionBuffer_) : nullptr;
        if ((paintScene && !surface) || (paintSelection && !overlay)) {
            emit errorOccurred("Cannot allocate the Skia viewport surface");
            return;
        }
        if (surface) {
            surface->getCanvas()->clear(0xffe9e2d8);
            surface->getCanvas()->scale(dpr, dpr);
        }
        if (overlay) {
            overlay->getCanvas()->clear(SK_ColorTRANSPARENT);
            overlay->getCanvas()->scale(dpr, dpr);
        }
        const double stride = layout_.page.height * scale() + 24;
        const auto first = static_cast<std::size_t>(
            std::max(0.0, std::floor((verticalScrollBar()->value() - 24) / stride)));
        const auto last =
            std::min(layout_.pageCount,
                     static_cast<std::size_t>(std::ceil(
                         (verticalScrollBar()->value() + viewport()->height()) / stride)) +
                         1);
        for (auto page = first; page < last; ++page) {
            const auto top = pageTop(page);
            auto transform = [&](SkCanvas *canvas) {
                canvas->save();
                canvas->translate(pageLeft(), top);
                canvas->scale(scale(), scale());
                canvas->clipRect(SkRect::MakeWH(layout_.page.width, layout_.page.height));
            };
            if (surface) {
                auto *canvas = surface->getCanvas();
                SkPaint shadow;
                shadow.setColor(0x18000000);
                canvas->drawRect(SkRect::MakeXYWH(pageLeft() + 2, top + 3,
                                                  layout_.page.width * scale(),
                                                  layout_.page.height * scale()),
                                 shadow);
                transform(canvas);
                renderer_.drawPage(*canvas, page);
                canvas->restore();
            }
            if (overlay) {
                auto *canvas = overlay->getCanvas();
                transform(canvas);
                renderer_.drawSelection(*canvas, layout_, editor_.selection, page);
                canvas->restore();
            }
        }
        buffer_.setDevicePixelRatio(dpr);
        selectionBuffer_.setDevicePixelRatio(dpr);
        sceneDirty_ = false;
        selectionDirty_ = false;
        if (paintScene)
            emit viewportRasterized();
    }
    QPainter painter(viewport());
    painter.drawImage(QPoint(0, 0), buffer_);
    if (selected)
        painter.drawImage(QPoint(0, 0), selectionBuffer_);
    paintSpelling(painter);
    if (hasFocus() && caretVisible_ && !selected) {
        const auto caret = layout_.caret(displayCaret_);
        painter.fillRect(QRectF(pageLeft() + caret.x * scale(),
                                pageTop(caret.page) + caret.y * scale(), 1.2,
                                caret.height * scale()),
                         palette().color(QPalette::Highlight));
    }
}
void PageView::setSpellCheck(bool enabled) {
    spellCheck_ = enabled;
    viewport()->update();
}
const std::vector<std::pair<std::size_t, std::size_t>> &
PageView::misspellings(std::size_t index) const {
    static const std::vector<std::pair<std::size_t, std::size_t>> none;
    auto &checker = SpellChecker::shared();
    if (!spellCheck_ || !checker.available() || index >= editor_.document.paragraphs.size())
        return none;
    const auto &paragraph = editor_.document.paragraphs[index];
    auto &cached = spelling_[paragraph.id];
    if (!cached.generation || cached.generation != checker.generation() ||
        cached.revision != paragraph.revision) {
        cached.ranges = checker.misspelled(paragraph.text());
        cached.revision = paragraph.revision;
        cached.generation = checker.generation();
    }
    return cached.ranges;
}
void PageView::paintSpelling(QPainter &painter) {
    if (!spellCheck_ || !preedit_.isEmpty() || !SpellChecker::shared().available())
        return;
    const auto caret = editor_.selection.focus;
    const bool collapsed = editor_.selection.anchor == editor_.selection.focus;
    QPen pen(QColor(0xd0, 0x30, 0x30));
    pen.setWidthF(std::max(1.0, scale() * 0.8));
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(pen);
    const auto height = viewport()->height();
    for (const auto &line : layout_.lines) {
        const auto y = pageTop(line.page) + (line.baseline + 2) * scale();
        if (y < -4 || y > height + 4)
            continue;
        for (const auto &[start, end] : misspellings(line.paragraph)) {
            if (end <= line.start || start >= line.end)
                continue;
            // Leave the word being typed alone.
            if (collapsed && caret.paragraph == line.paragraph && start <= caret.offset &&
                caret.offset <= end)
                continue;
            const auto a = std::max(start, line.start), b = std::min(end, line.end);
            double x1 = -1, x2 = -1;
            for (const auto &[offset, x] : line.caretStops) {
                if (offset == a)
                    x1 = x;
                if (offset == b)
                    x2 = x;
            }
            if (x1 < 0 || x2 < 0)
                continue;
            if (x1 > x2)
                std::swap(x1, x2);
            const double left = pageLeft() + (line.x + x1) * scale(),
                         right = pageLeft() + (line.x + x2) * scale();
            const double step = std::max(2.0, 2.0 * scale());
            QPainterPath wave(QPointF(left, y));
            bool up = true;
            for (double x = left + step; x < right + step / 2; x += step, up = !up)
                wave.lineTo(std::min(x, right), y + (up ? -step / 2 : 0));
            painter.drawPath(wave);
        }
    }
    painter.restore();
}
bool PageView::replaceRange(std::size_t paragraph, std::size_t start, std::size_t end,
                            const QString &text) {
    return change([&](Editor &candidate) {
        candidate.selection = {{paragraph, start}, {paragraph, end}};
        candidate.typingStyle = candidate.document.styleAt({paragraph, start + (start < end)});
        candidate.replace(text.toStdU16String());
    });
}
void PageView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.setObjectName("documentContextMenu");
    const auto position = positionAt(event->pos());
    const auto &ranges = misspellings(position.paragraph);
    const auto hit = std::find_if(ranges.begin(), ranges.end(), [&](const auto &range) {
        return range.first <= position.offset && position.offset <= range.second;
    });
    if (hit != ranges.end()) {
        const auto [start, end] = *hit;
        const auto paragraph = position.paragraph;
        const auto word = QString::fromStdU16String(
            editor_.document.paragraphs[paragraph].text().substr(start, end - start));
        const auto suggestions = SpellChecker::shared().suggestions(word);
        for (const auto &suggestion : suggestions) {
            auto *action = menu.addAction(suggestion);
            auto font = action->font();
            font.setBold(true);
            action->setFont(font);
            connect(action, &QAction::triggered, this, [this, paragraph, start, end, suggestion] {
                replaceRange(paragraph, start, end, suggestion);
            });
        }
        if (suggestions.isEmpty())
            menu.addAction("No suggestions")->setEnabled(false);
        menu.addSeparator();
        connect(menu.addAction("Add to dictionary"), &QAction::triggered, this, [this, word] {
            SpellChecker::shared().addToDictionary(word);
            viewport()->update();
        });
        connect(menu.addAction("Ignore"), &QAction::triggered, this, [this, word] {
            SpellChecker::shared().ignore(word);
            viewport()->update();
        });
        menu.addSeparator();
    }
    const bool selected = editor_.selection.anchor != editor_.selection.focus;
    auto *cutAction = menu.addAction("Cut", this, &PageView::cut);
    auto *copyAction = menu.addAction("Copy", this, &PageView::copy);
    menu.addAction("Paste", this, &PageView::paste);
    cutAction->setEnabled(selected);
    copyAction->setEnabled(selected);
    menu.exec(event->globalPos());
}
void PageView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollbars();
    invalidateScene();
}
void PageView::moveCaret(Position position, bool extend) {
    if (auto *input = QApplication::inputMethod())
        input->commit();
    editor_.selection.focus = position;
    if (!extend)
        editor_.selection.anchor = position;
    editor_.typingStyle = editor_.document.styleAt(position);
    const bool composing = !preedit_.isEmpty();
    preedit_.clear();
    displayCaret_ = editor_.selection.focus;
    if (composing)
        refresh(false);
    else
        present(false);
}
void PageView::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        cut();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        paste();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }
    if (event->matches(QKeySequence::Bold)) {
        const bool value = !editor_.typingStyle.bold;
        format([value](auto &s) { s.bold = value; });
        return;
    }
    if (event->matches(QKeySequence::Italic)) {
        const bool value = !editor_.typingStyle.italic;
        format([value](auto &s) { s.italic = value; });
        return;
    }
    if (event->matches(QKeySequence::Underline)) {
        const bool value = !editor_.typingStyle.underline;
        format([value](auto &s) { s.underline = value; });
        return;
    }
    const bool extend = event->modifiers().testFlag(Qt::ShiftModifier);
    auto position = editor_.selection.focus;
    const auto text = editor_.document.paragraphs[position.paragraph].text();
    switch (event->key()) {
    case Qt::Key_Backspace:
        change([&](Editor &candidate) { candidate.eraseBackward(); });
        return;
    case Qt::Key_Delete:
        change([&](Editor &candidate) { candidate.eraseForward(); });
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (event->modifiers().testFlag(Qt::ControlModifier))
            insertPageBreak();
        else
            change([](Editor &candidate) { candidate.insertParagraphBreak(); });
        return;
    case Qt::Key_Tab:
        // In a table, Tab moves to the next cell (adding a row after the last one).
        if (editor_.selectedCell())
            change([](Editor &candidate) { candidate.nextCell(); });
        else
            insertText("\t");
        return;
    case Qt::Key_Backtab:
        if (editor_.selectedCell())
            change([](Editor &candidate) { candidate.nextCell(true); });
        return;
    case Qt::Key_Left:
        if (position.offset)
            position.offset =
                previousGrapheme(editor_.document.paragraphs[position.paragraph], position.offset);
        else if (position.paragraph) {
            --position.paragraph;
            position.offset = editor_.document.paragraphs[position.paragraph].text().size();
        }
        moveCaret(position, extend);
        return;
    case Qt::Key_Right:
        if (position.offset < text.size())
            position.offset =
                nextGrapheme(editor_.document.paragraphs[position.paragraph], position.offset);
        else if (position.paragraph + 1 < editor_.document.paragraphs.size()) {
            ++position.paragraph;
            position.offset = 0;
        }
        moveCaret(position, extend);
        return;
    case Qt::Key_Home:
    case Qt::Key_End:
        if (event->modifiers().testFlag(Qt::ControlModifier))
            position = event->key() == Qt::Key_Home
                           ? Position{}
                           : Position{editor_.document.paragraphs.size() - 1,
                                      editor_.document.paragraphs.back().text().size()};
        else
            for (const auto &line : layout_.lines)
                if (line.paragraph == position.paragraph && position.offset >= line.start &&
                    position.offset <= line.end) {
                    position.offset = event->key() == Qt::Key_Home ? line.start : line.end;
                    break;
                }
        moveCaret(position, extend);
        return;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const auto caret = layout_.caret(position);
        const int direction = event->key() == Qt::Key_Up ? -1 : 1;
        auto page = caret.page;
        double y = caret.y + direction * caret.height + caret.height / 2;
        if (y < layout_.page.margin && page) {
            --page;
            y = layout_.page.height - layout_.page.margin - 1;
        } else if (y > layout_.page.height - layout_.page.margin && page + 1 < layout_.pageCount) {
            ++page;
            y = layout_.page.margin;
        }
        moveCaret(layout_.hitTest(caret.x, y, page), extend);
        return;
    }
    default:
        break;
    }
    if (!event->text().isEmpty() && !event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::MetaModifier)) {
        insertText(event->text());
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}
Position PageView::positionAt(QPointF point) const {
    const auto fullY = point.y() + verticalScrollBar()->value() - 24;
    const auto stride = layout_.page.height * scale() + 24;
    const auto page = static_cast<std::size_t>(
        std::clamp(std::floor(fullY / stride), 0.0, static_cast<double>(layout_.pageCount - 1)));
    return layout_.hitTest((point.x() - pageLeft()) / scale(),
                           (point.y() - pageTop(page)) / scale(), page);
}
void PageView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        setFocus();
        moveCaret(positionAt(event->position()), event->modifiers().testFlag(Qt::ShiftModifier));
    } else
        QAbstractScrollArea::mousePressEvent(event);
}
void PageView::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons().testFlag(Qt::LeftButton))
        moveCaret(positionAt(event->position()), true);
}
void PageView::inputMethodEvent(QInputMethodEvent *event) {
    if (event->replacementLength() || event->replacementStart()) {
        emit errorOccurred("Input-method replacement ranges are not supported yet");
        event->ignore();
        return;
    }
    if (!event->commitString().isEmpty())
        insertText(event->commitString());
    preedit_ = event->preeditString();
    refresh(false);
    event->accept();
}
QVariant PageView::inputMethodQuery(Qt::InputMethodQuery query) const {
    const auto caret = layout_.caret(displayCaret_);
    switch (query) {
    case Qt::ImCursorRectangle:
        return QRectF(pageLeft() + caret.x * scale(), pageTop(caret.page) + caret.y * scale(), 1,
                      caret.height * scale());
    case Qt::ImSurroundingText:
        return QString::fromStdU16String(
            editor_.document.paragraphs[editor_.selection.focus.paragraph].text());
    case Qt::ImCursorPosition:
        return static_cast<int>(editor_.selection.focus.offset);
    case Qt::ImAnchorPosition:
        return static_cast<int>(editor_.selection.anchor.offset);
    case Qt::ImCurrentSelection:
        return selectedText();
    default:
        return QAbstractScrollArea::inputMethodQuery(query);
    }
}
void PageView::focusInEvent(QFocusEvent *event) {
    QAbstractScrollArea::focusInEvent(event);
    caretVisible_ = true;
    blink_.start();
    viewport()->update();
}
void PageView::focusOutEvent(QFocusEvent *event) {
    QAbstractScrollArea::focusOutEvent(event);
    blink_.stop();
    caretVisible_ = false;
    viewport()->update();
}
void PageView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        setZoom(zoom_ * (event->angleDelta().y() > 0 ? 1.1 : 1 / 1.1));
        event->accept();
    } else
        QAbstractScrollArea::wheelEvent(event);
}
void PageView::configurePrinter(QPrinter &printer) const {
    const auto &page = editor_.document.page;
    const bool landscape = page.width > page.height;
    const QPageSize size(QSizeF(std::min(page.width, page.height), std::max(page.width, page.height)),
                         QPageSize::Point, {}, QPageSize::FuzzyMatch);
    printer.setFullPage(true);
    printer.setPageLayout(QPageLayout(size,
                                      landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                      QMarginsF(0, 0, 0, 0), QPageLayout::Point));
}
void PageView::print(QPrinter &printer) {
    const auto committed = engine_.compose(editor_.document);
    PageRenderer renderer;
    renderer.synchronize(committed);
    const auto count = static_cast<int>(committed.pageCount);
    int first = 1, last = count;
    if (printer.printRange() == QPrinter::PageRange && printer.fromPage() > 0) {
        first = std::max(1, printer.fromPage());
        last = std::min(count, printer.toPage() > 0 ? printer.toPage() : count);
    } else if (printer.printRange() == QPrinter::CurrentPage) {
        first = last = static_cast<int>(layout_.caret(editor_.selection.focus).page) + 1;
    }
    if (first > last)
        return;
    QPainter painter;
    if (!painter.begin(&printer))
        throw std::runtime_error("The printer could not be started");
    // Rasterize one page at a time; 300 dpi is ample for text and bounds memory use.
    const double dpi = std::min(300, std::max(72, printer.resolution()));
    const QRectF target(QPointF(0, 0), QSizeF(printer.width(), printer.height()));
    for (int page = first; page <= last; ++page) {
        if (page != first && !printer.newPage())
            throw std::runtime_error("The printer rejected a new page");
        QImage image(static_cast<int>(std::ceil(committed.page.width * dpi / 72)),
                     static_cast<int>(std::ceil(committed.page.height * dpi / 72)),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        auto surface = SkSurfaces::WrapPixels(
            SkImageInfo::Make(image.width(), image.height(), kBGRA_8888_SkColorType,
                              kPremul_SkAlphaType),
            image.bits(), static_cast<size_t>(image.bytesPerLine()));
        if (!surface)
            throw std::runtime_error("Could not prepare the page for printing");
        surface->getCanvas()->scale(static_cast<float>(dpi / 72), static_cast<float>(dpi / 72));
        renderer.drawPage(*surface->getCanvas(), static_cast<std::size_t>(page - 1));
        painter.drawImage(target, image);
    }
    if (!painter.end())
        throw std::runtime_error("Printing did not finish");
}
void PageView::exportPdf(const QString &path) {
    // Export the committed scene; preedit text never enters persisted files.
    const auto committed = engine_.compose(editor_.document);
    PageRenderer renderer;
    renderer.synchronize(committed);
    SkDynamicMemoryWStream stream;
    SkPDF::Metadata metadata;
    metadata.fCreator = SkString("Wordforge");
    metadata.jpegDecoder = [](sk_sp<SkData> data) {
        return SkJpegDecoder::Decode(std::move(data), nullptr);
    };
    metadata.jpegEncoder = [](SkWStream *destination, const SkPixmap &pixels, int quality) {
        SkJpegEncoder::Options options;
        options.fQuality = quality;
        return SkJpegEncoder::Encode(destination, pixels, options);
    };
    auto pdf = SkPDF::MakeDocument(&stream, metadata);
    if (!pdf)
        throw std::runtime_error("Skia PDF backend is unavailable");
    for (std::size_t page = 0; page < committed.pageCount; ++page) {
        auto *canvas = pdf->beginPage(static_cast<float>(committed.page.width),
                                      static_cast<float>(committed.page.height));
        renderer.drawPage(*canvas, page);
        pdf->endPage();
    }
    pdf->close();
    auto bytes = stream.detachAsData();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(static_cast<const char *>(bytes->data()), static_cast<qint64>(bytes->size())) !=
            static_cast<qint64>(bytes->size()) ||
        !file.commit())
        throw std::runtime_error("Could not atomically save the PDF");
}
} // namespace wf
