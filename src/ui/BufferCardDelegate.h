#pragma once
#include <QPixmap>
#include "Tokens.h"
#include <QStyledItemDelegate>

namespace napkin {

class Thumbnailer;

// Paints the buffer stack. The visual contract is SPEC.md §7: whitespace,
// subtle separators, restrained borders, clear typography — no shadows, no
// gradients, no large radii. Every colour comes from QPalette so light and dark
// both work without a second code path.
class BufferCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit BufferCardDelegate(QObject* parent = nullptr);

    // Optional: without one, image buffers simply render as text.
    void setThumbnailer(Thumbnailer* thumbnailer) { thumbnailer_ = thumbnailer; }

    // Only one card animates at a time — the one under the pointer. Animating
    // every visible GIF would break §12's idle-CPU budget for decoration.
    void setAnimationFrame(int row, const QPixmap& frame);
    void clearAnimationFrame();

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // The card box inside the item rect, below the section label when the row
    // starts a section. The view needs this to place the inline editor.
    QRect cardRect(const QRect& itemRect, const QModelIndex& index) const;
    QRect contentRect(const QRect& itemRect, const QModelIndex& index) const;

    int collapsedHeight() const;
    void drawSnippet(QPainter* p, const QRect& box, const QString& snippet,
                     const QPalette& pal) const;
    int sectionHeight(const QModelIndex& index) const;

    static constexpr int kMarginX  = tokens::kRowMarginX;
    static constexpr int kMarginY  = tokens::kRowMarginY;
    static constexpr int kPadding  = tokens::kRowPad;
    // The list's rows are cards too, so they use the card radius. They were 6
    // against the board's 10, which is why the two panes read as two different
    // levels of finish even when everything else matched.
    static constexpr int kRadius   = tokens::kCardRadius;
    static constexpr int kSectionH = tokens::kRowSectionH;
    static constexpr int kMaxCardWidth = tokens::kRowMaxWidth;
    static constexpr int kThumbSize = tokens::kRowThumb;
    static constexpr int kThumbSizeMulti = tokens::kRowThumbMulti;

private:
    QFont timestampFont(const QFont& base) const;

    Thumbnailer* thumbnailer_ = nullptr;
    int     animatedRow_ = -1;
    QPixmap animatedFrame_;
};

}  // namespace napkin
