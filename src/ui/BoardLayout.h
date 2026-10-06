#pragma once
#include "../domain/Item.h"
#include <QHash>
#include <QRect>
#include <QSize>
#include <vector>

class QFont;

namespace napkin {

// Geometry for the whole board, computed from the ITEMS rather than from
// widgets.
//
// This is what makes virtualization possible. A card's height depends only on
// its own content, so it can be measured without ever constructing the card:
// text against a shared QTextDocument, images from the width and height already
// stored in the row. The canvas then builds widgets for the visible slice only.
class BoardLayout {
public:
    struct Slot {
        ItemId id = kNoItem;
        QRect  rect;
        bool   clipped = false;   // content taller than a card may be
    };

    void setViewport(int width);
    void setFont(const QFont& body);
    // The whole board, now.
    void rebuild(const std::vector<Item>& items);

    // The same layout, a piece at a time. Cards are dealt newest first into
    // the shortest column, so a card's place depends only on the cards before
    // it — the top of the board is final as soon as the top is measured, and
    // the rest can wait. Opening a 1000-item napkin used to measure every card
    // before the first one could be drawn, twice: once at the width the window
    // was built at and again at the width it was shown at.
    //
    // reset() settles the columns and places nothing. placeThrough() places
    // until no later card can start above `y`; placeFor() places for about
    // `budgetMs`. Both answer whether anything new was placed. The `items`
    // passed must be the same list each time, until the next reset().
    void reset(const std::vector<Item>& items);
    bool placeThrough(const std::vector<Item>& items, int y);
    bool placeFor(const std::vector<Item>& items, int budgetMs);
    void placeAll(const std::vector<Item>& items);
    bool complete() const { return placements_.size() >= itemCount_; }

    int columnWidth() const { return columnWidth_; }
    // Exact once complete(); until then an estimate from the cards measured so
    // far, so the scrollbar is roughly the right size from the start.
    int totalHeight() const { return totalHeight_; }
    int columnCount() const { return columns_; }

    // Which column a slot was dealt into. Exact, not inferred: rebuild() places
    // every card at kPadX + col * (columnWidth + gap).
    int columnOf(int index) const;

    enum class Step { Up, Down, Left, Right };
    // Where an arrow key lands, or -1 when the board ends that way.
    //
    // Masonry deals cards into explicit columns, so "the card below this one"
    // is a fact about the layout rather than a nearest-neighbour guess. Up and
    // Down stay in the column; Left and Right cross to the neighbouring column
    // and land on whichever card best lines up with this one.
    int neighbour(int index, Step step) const;

    // Only the cards placed so far; see placeThrough().
    const std::vector<Slot>& placements() const { return placements_; }
    std::vector<int> indicesIn(const QRect& visible, int overscan) const;
    int indexOf(ItemId id) const;

    // Drops a cached measurement. The cache is keyed on the item's last SAVED
    // time, which does not move while you are typing — so a card being edited
    // kept returning the height it had before you started.
    void invalidate(ItemId id);

private:
    int heightFor(const Item& item, int columnWidth, bool* clipped) const;

    // Measuring a thousand documents to open a buffer is a real cost, and the
    // same buffer gets reopened constantly. Keyed on what can actually change
    // the answer: the item, its last edit, and the column width.
    struct MeasureKey {
        ItemId id;
        Timestamp modifiedAt;
        int width;
        bool operator==(const MeasureKey&) const = default;
    };
    struct Measurement { int height; bool clipped; };
    mutable QHash<ItemId, std::pair<MeasureKey, Measurement>> cache_;

    void placeNext(const Item& item);
    void updateTotalHeight();

    std::vector<Slot> placements_;
    std::vector<int> bottoms_;    // the next free y in each column
    size_t itemCount_ = 0;
    qint64 placedHeight_ = 0;     // sum of placed card heights, for the estimate
    int viewportWidth_ = 0;
    int columns_ = 1;
    int columnWidth_ = 0;
    int totalHeight_ = 0;
    QFont* body_ = nullptr;
};

}  // namespace napkin
