#pragma once
#include "core/rendering/page_renderer.hpp"
#include <QAbstractScrollArea>
#include <QImage>
#include <QTimer>
#include <unordered_map>
class QPrinter;
namespace wf {
class PageView : public QAbstractScrollArea {
    Q_OBJECT
  public:
    explicit PageView(QWidget *parent = nullptr);
    const Document &document() const {
        return editor_.document;
    }
    Selection selection() const {
        return editor_.selection;
    }
    const CharacterStyle &typingStyle() const {
        return editor_.typingStyle;
    }
    const Layout &layout() const {
        return layout_;
    }
    void setDocument(Document);
    void insertText(const QString &);
    void format(const std::function<void(CharacterStyle &)> &);
    void align(Alignment);
    void formatParagraph(const std::function<void(ParagraphFormat &)> &);
    void applyPreset(ParagraphPreset);
    void updateStyleFromSelection();
    void applyStyle(const std::string &id);
    // Style management; each reports a refused change through errorOccurred and returns false.
    bool createStyleFromSelection(const QString &name);
    bool createCharacterStyleFromSelection(const QString &name);
    // Empty `id` removes the character style.
    void applyCharacterStyle(const std::string &id);
    bool renameStyle(const std::string &id, const QString &name);
    bool deleteStyle(const std::string &id);
    void clearCharacterFormatting();
    void insertPageBreak();
    void setPage(PageStyle);
    void undo();
    void redo();
    void copy();
    void cut();
    void paste();
    void selectAll();
    void setZoom(double);
    double zoom() const {
        return zoom_;
    }
    QString selectedText() const;
    QStringList fontFamilies() const;
    bool findNext(const QString &);
    bool replaceCurrent(const QString &query, const QString &replacement);
    void exportPdf(const QString &);
    // Sets the printer's paper to the document page; call before showing print dialogs.
    void configurePrinter(QPrinter &) const;
    // Prints the committed document (never preedit text), honouring the page range.
    void print(QPrinter &);
    std::uint64_t addBookmark(const QString &);
    void removeBookmark(std::uint64_t);
    bool goToBookmark(std::uint64_t);
    bool insertPicture(const QString &);
    bool resizePicture(double width, double height);
    std::optional<std::uint64_t> selectedFootnote() const {
        return editor_.selectedFootnote();
    }
    bool insertFootnote(const std::u16string &text, bool endnote = false);
    bool selectedNoteIsEndnote() const;
    // The caret's section's page settings (size, margins, columns, header and footer).
    PageStyle sectionPage() const {
        return editor_.sectionPage();
    }
    bool insertSectionBreak(bool continuous);
    bool removeSectionBreak();
    // A table of contents at the caret, with page numbers from the current layout.
    bool insertTableOfContents();
    bool updateTablesOfContents();
    bool hasTableOfContents() const;
    // Links: `target` on the selection (or `text` inserted at the caret); empty removes.
    bool setLink(const std::string &target, const std::u16string &text = {});
    std::string linkAtCaret() const {
        return editor_.linkAt();
    }
    std::optional<Selection> linkRange() const {
        return editor_.linkRange();
    }
    // The link under a document position, or empty.
    std::string linkAt(Position) const;
    // Opens a web or mail link in the system's handler, or goes to a "#bookmark".
    bool openLink(const std::string &target);
    bool addComment(const std::u16string &text);
    // Tracked changes, recorded under the user's name.
    bool setTrackChanges(bool on);
    bool acceptChanges();
    bool rejectChanges();
    bool acceptAllChanges();
    bool rejectAllChanges();
    bool nextChange();
    bool selectionHasChanges() const {
        return editor_.selectionHasChanges();
    }
    bool setCommentText(std::uint64_t id, const std::u16string &text);
    bool deleteComment(std::uint64_t id);
    // The comment whose range holds the caret (the innermost, latest one), if any.
    std::optional<std::uint64_t> commentAtCaret() const;
    void selectRange(Selection);
    bool setFootnoteText(std::uint64_t id, const std::u16string &text);
    std::optional<InlineImage> selectedImage() const {
        return editor_.selectedImage();
    }
    // Spell checking marks misspelled words with a red wavy underline; right-click
    // offers suggestions. The word being typed is not marked until the caret leaves it.
    void setSpellCheck(bool);
    bool spellCheck() const {
        return spellCheck_;
    }
    const std::vector<std::pair<std::size_t, std::size_t>> &misspellings(std::size_t paragraph) const;
    // Replaces [start, end) in a paragraph as one undoable edit (spelling suggestions).
    bool replaceRange(std::size_t paragraph, std::size_t start, std::size_t end, const QString &);
    std::optional<TableCell> selectedCell() const {
        return editor_.selectedCell();
    }
    bool insertTable(std::size_t rows, std::size_t columns);
    bool tableRow(bool insert, bool before = false);
    bool tableColumn(bool insert, bool before = false);
    bool removeTable();
    bool mergeCells();
    bool splitCell();
    bool setColumnWidth(double points);
    bool distributeColumns();
    bool setTableBorder(TableBorder);
    bool shadeCells(std::uint32_t color); // 0 removes the fill
    // Width available for content at the caret: the page body or the table cell,
    // less the paragraph's indent and list marker.
    double writingWidth() const;
  signals:
    void contentChanged();
    void selectionChanged();
    void zoomChanged(double);
    void errorOccurred(const QString &);
    void editLinkRequested(); // the context menu's "Edit link…"
    void viewportRasterized();

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    bool focusNextPrevChild(bool) override {
        return false;
    }

  private:
    Editor editor_;
    Shaper shaper_;
    LayoutEngine engine_;
    PageRenderer renderer_;
    Layout layout_;
    QTimer blink_;
    bool caretVisible_ = true;
    double zoom_ = 1;
    QString preedit_;
    Position displayCaret_;
    QImage buffer_;
    QImage selectionBuffer_;
    bool spellCheck_ = true;
    struct SpellingCache {
        std::uint64_t revision = 0, generation = 0;
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
    };
    mutable std::unordered_map<std::uint64_t, SpellingCache> spelling_;
    void paintSpelling(QPainter &);
    bool sceneDirty_ = true, selectionDirty_ = true;
    QRect caretRectangle() const;
    void invalidateScene();
    bool change(const std::function<void(Editor &)> &);
    void refresh(bool content = true);
    void present(bool content);
    void updateScrollbars();
    void revealCaret();
    void moveCaret(Position, bool extend);
    Position positionAt(QPointF) const;
    double scale() const;
    // Screen position of a page's top-left corner. Pages differ in size between sections;
    // each is centred on the widest.
    double pageLeft(std::size_t page) const;
    double pageTop(std::size_t) const;
    std::size_t pageAtY(double viewportY) const; // the page under (or nearest) a height
    double pageOffset(std::size_t page) const;   // sum of the heights of earlier pages, in pt
    double widestPage() const;
};
} // namespace wf
