#pragma once
#include "../domain/Item.h"
#include <QStringList>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QTextDocument;

namespace napkin {

class BlobStore;
class CardFooter;
class LinkChip;
class Thumbnailer;

// One item on the board, as a card.
//
// Selection is uniform — every item answers a click the same way, so "click it,
// then delete it" works on a paragraph exactly as it works on a picture:
//
//   single click       -> select
//   double click       -> edit it (text) or open the lightbox (image)
//   Enter on selected  -> edit it
//   Ctrl/Shift+click   -> extend the selection
//   Esc while editing  -> stop editing, keep the card selected
//   click empty board  -> clear the selection
class ItemCard : public QWidget {
    Q_OBJECT
public:
    // Relative times go stale; the window's slow tick calls this.
    void refreshTimestamp();
    explicit ItemCard(const Item& item, QWidget* parent = nullptr);

    ItemId itemId() const { return item_.id; }
    const Item& item() const { return item_; }
    bool isComposer() const { return item_.id == kNoItem; }

    bool isSelected() const { return selected_; }
    void setSelected(bool selected);

    // The keyboard cursor. Distinct from selection: you can move the cursor
    // across cards with the arrow keys and the card under it draws a focus ring
    // whether or not it is part of the selection.
    bool isCurrent() const { return current_; }
    void setCurrent(bool current);

    virtual QString asPlainText() const { return {}; }

    // Natural height at this column width, clamped between the minimum a card
    // is allowed to be and the maximum it may grow to. A card's size depends
    // only on its own content — never on what its neighbours are doing.
    int heightForColumn(int width) const;
    bool isClipped() const { return clipped_; }
    // Set by the board, which is what computes heights now.
    void setClipped(bool clipped);
    void acknowledge(const QString& message);
    void noteSaved(Timestamp when);
    void setFooterAction(const QString& label);

    virtual bool hasEditFocus() const { return false; }

signals:
    void selectRequested(ItemId id, Qt::KeyboardModifiers modifiers);
    void activated(ItemId id);
    void escaped();
    void copyRequested(ItemId id);

protected:
    // Re-reads every colour this card derived from the palette. A colour cached
    // on a child widget goes stale when the desktop theme changes — card text
    // stayed the old colour until the buffer was reopened — so anything that
    // caches one overrides this.
    virtual void applyPalette() {}
    void changeEvent(QEvent* e) override;

    // Subclasses call this once, with the widget that fills the content area.
    void setContent(QWidget* content, const QString& copyLabel);
    virtual int contentHeightForWidth(int innerWidth) const = 0;
    int chromeHeight() const;

    void mousePressEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void leaveEvent(QEvent* e) override;

    Item item_;
    mutable bool clipped_ = false;

private:
    CardFooter* footer_ = nullptr;
    bool selected_ = false;
    bool current_ = false;
    bool hovered_ = false;
};

class TextItemCard : public ItemCard {
    Q_OBJECT
public:
    explicit TextItemCard(const Item& item, QWidget* parent = nullptr);

    QString text() const;
    QString asPlainText() const override { return text(); }
    bool isDirty() const { return dirty_; }
    void markClean() { dirty_ = false; }
    void setItemId(ItemId id) { item_.id = id; }
    void focusText();
    void beginEditing(bool moveToEnd = true);
    void selectAllText();
    void insertText(const QString& text);
    void focusTextInteraction();
    void endEditing();
    bool textHasFocus() const;
    bool hasEditFocus() const override;
    void updateAccessibleName();
    void setSearchTerms(const QStringList& terms);
    // SPEC.md §7: answer the calculation on the caret's line, the selected
    // expression, or every line of a multi-line selection that ends in "=".
    // Ctrl+Tab. False, with a tooltip saying why, when there was nothing to do.
    bool calculate();

    // SPEC.md §3: a text item whose whole content is one URL is *rendered* as a
    // chip. Derived presentation — the item is still text and still editable,
    // so the chip steps aside the moment editing starts.
    QString linkUrl() const;
    bool showingChip() const;
    // Ctrl+click follows a URL inside prose; a plain click still selects the
    // card, because that is what a click means on every other card.
    QString urlAt(const QPoint& viewportPos) const;

signals:
    void edited();
    void openUrlRequested(const QString& url);
    void imagePasted(const QByteArray& bytes, const QString& mime);
    void heightChanged();
    void editingStarted(ItemId id);
    void editingFinished(ItemId id, bool leftEmpty);

protected:
    int contentHeightForWidth(int innerWidth) const override;
    void applyPalette() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    // Takes the editing intent rather than reading it back off the widget.
    // Inferring it from hasEditFocus() also works today — beginEditing() sets
    // the interaction flags before it gets here, so the inference happens to be
    // right — but only because of that call order, and the cost of getting it
    // wrong is a chip left covering the editor the caret just moved into.
    // Checked: the inferring version passes these tests too, so this is a
    // hazard removed, not a bug fixed.
    void refreshChip(bool editing);

    QPlainTextEdit* edit_ = nullptr;
    LinkChip*       chip_ = nullptr;
    // Not chip_->isVisible(): a widget reports itself invisible until its whole
    // ancestor chain is shown, and the board measures cards before the window
    // exists. Measuring through isVisible() therefore took the text path for
    // every chip and produced a card too short to draw one in.
    bool            chipShown_ = false;
    class MatchHighlighter* highlighter_ = nullptr;
    bool dirty_ = false;
    QString lastText_;   // to tell a real edit from a reformat

    // Measuring happens against our own document, not the editor's. See the
    // note on contentHeightForWidth.
    mutable QTextDocument* measure_ = nullptr;
    mutable QString measured_;
};

class ImageItemCard : public ItemCard {
    Q_OBJECT
public:
    ImageItemCard(const Item& item, Thumbnailer& thumbs, BlobStore& blobs,
                  QWidget* parent = nullptr);

    QString asPlainText() const override;

protected:
    int contentHeightForWidth(int innerWidth) const override;
    void applyPalette() override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void rescale();

    QPixmap source_;
    QLabel* view_ = nullptr;
    QLabel* caption_ = nullptr;
};

}  // namespace napkin
