#include "BoardLayout.h"
#include "../domain/Links.h"
#include "LinkChip.h"
#include "Tokens.h"

#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QTextDocument>

namespace napkin {
namespace {
using namespace tokens;

// One document reused for every measurement. Constructing a QTextDocument per
// card per layout pass was a measurable slice of the 44 ms sweep at 1000 items.
QTextDocument& scratch()
{
    static QTextDocument doc;
    static bool ready = false;
    if (!ready) { doc.setDocumentMargin(1); ready = true; }
    return doc;
}

}  // namespace

void BoardLayout::setViewport(int width) { viewportWidth_ = width; }

void BoardLayout::setFont(const QFont& body)
{
    if (body_ && *body_ == body) return;
    delete body_;
    body_ = new QFont(body);
    // Every cached height was measured against the old font, and the key does
    // not mention the font — so without this a text-size change left every card
    // at the height its old face happened to need.
    cache_.clear();
}

int BoardLayout::heightFor(const Item& item, int columnWidth, bool* clipped) const
{
    const MeasureKey key{item.id, item.modifiedAt, columnWidth};
    if (item.id != kNoItem) {
        const auto it = cache_.constFind(item.id);
        if (it != cache_.constEnd() && it->first == key) {
            if (clipped) *clipped = it->second.clipped;
            return it->second.height;
        }
    }

    const int inner = std::max(40, columnWidth - kCardPad * 2);
    const int chrome = cardChromeHeight(body_ ? *body_ : QFont());

    int content = 0;
    if (item.type == ItemType::Text && links::soleUrl(item.text)) {
        // A chip is a fixed object. The rule for what becomes one lives in
        // links::soleUrl and is consulted here as well as in the card, because
        // the board measures from item data and never constructs a card — so a
        // chip the board did not know about got a card too short to draw it in.
        //
        // This height also applies while the card is being edited, when the
        // editor is showing instead. That is deliberate: the alternative is a
        // card that changes size the moment you click into it.
        content = LinkChip::preferredHeight();
    } else if (item.type == ItemType::Text) {
        QTextDocument& doc = scratch();
        if (body_) doc.setDefaultFont(*body_);
        // Only as much text as can still change the answer. A card caps at
        // kCardMaxHeight, and this many characters overflows that at any column
        // width we allow — so laying out the rest of a pasted log would be work
        // whose result is already known.
        doc.setPlainText(item.text.left(kMeasureLimit));
        doc.setTextWidth(inner);
        content = int(std::ceil(doc.size().height())) + 2;
        if (item.text.size() > kMeasureLimit) content = kCardMaxHeight;   // certainly over
    } else {
        // From the stored dimensions: no file is opened and no image decoded
        // just to find out how tall a card is.
        const QFontMetrics fm(body_ ? *body_ : QFont());
        const int captionH = fm.height() + 6;
        if (item.width > 0 && item.height > 0) {
            const int drawn = item.height * std::min(inner, item.width)
                              / std::max(1, item.width);
            content = drawn + captionH;
        } else {
            content = 96 + captionH;
        }
    }
    const int natural = content + chrome;
    int height = std::clamp(natural, kCardMinHeight, kCardMaxHeight);
    const bool overflows = natural > kCardMaxHeight;

    // A clipped text card is cut wherever kCardMaxHeight happens to fall, which
    // is usually through the middle of a line — leaving a row of severed
    // ascenders above the footer that reads as breakage. Land the cut in the
    // leading instead, by giving the card a whole number of lines.
    if (overflows && item.type == ItemType::Text) {
        const qreal line = QFontMetricsF(body_ ? *body_ : QFont()).lineSpacing();
        if (line > 1.0) {
            const int lines = int((height - chrome) / line);
            if (lines > 0) height = chrome + int(lines * line);
        }
    }

    const Measurement measured{height, overflows};
    if (item.id != kNoItem) {
        // Bounded: a buffer nobody is looking at should not pin its measurements
        // for the life of the process.
        if (cache_.size() > 4000) cache_.clear();
        cache_.insert(item.id, {key, measured});
    }
    if (clipped) *clipped = measured.clipped;
    return measured.height;
}

void BoardLayout::rebuild(const std::vector<Item>& items)
{
    placements_.clear();
    const int usable = std::max(kCardMinWidth, viewportWidth_ - kPadX * 2);
    int columns = std::max(1, (usable + kCardGap) / (kCardTargetWidth + kCardGap));
    columnWidth_ = std::clamp((usable - (columns - 1) * kCardGap) / columns,
                              kCardMinWidth, kCardMaxWidth);
    // Recompute the count against the settled width so the two agree; an
    // earlier version derived them independently and the column count flipped
    // on 20px of unrelated chrome.
    columns = std::max(1, (usable + kCardGap) / (columnWidth_ + kCardGap));
    columnWidth_ = std::clamp((usable - (columns - 1) * kCardGap) / columns,
                              kCardMinWidth, kCardMaxWidth);

    columns_ = columns;
    std::vector<int> bottoms(size_t(columns), kPadTop);
    placements_.reserve(items.size());
    for (const auto& item : items) {
        size_t shortest = 0;
        for (size_t i = 1; i < bottoms.size(); ++i)
            if (bottoms[i] < bottoms[shortest]) shortest = i;

        bool clipped = false;
        const int h = heightFor(item, columnWidth_, &clipped);
        const int x = kPadX + int(shortest) * (columnWidth_ + kCardGap);
        placements_.push_back({item.id, QRect(x, bottoms[shortest], columnWidth_, h), clipped});
        bottoms[shortest] += h + kCardGap;
    }

    totalHeight_ = kPadTop;
    for (int b : bottoms) totalHeight_ = std::max(totalHeight_, b);
    totalHeight_ += kPadTop - kCardGap;
}

std::vector<int> BoardLayout::indicesIn(const QRect& visible, int overscan) const
{
    const QRect band = visible.adjusted(0, -overscan, 0, overscan);
    std::vector<int> out;
    for (size_t i = 0; i < placements_.size(); ++i)
        if (placements_[i].rect.intersects(band)) out.push_back(int(i));
    return out;
}

void BoardLayout::invalidate(ItemId id)
{
    cache_.remove(id);
}

int BoardLayout::columnOf(int index) const
{
    if (index < 0 || index >= int(placements_.size())) return -1;
    const int pitch = columnWidth_ + kCardGap;
    if (pitch <= 0) return 0;
    return (placements_[size_t(index)].rect.x() - kPadX) / pitch;
}

int BoardLayout::neighbour(int index, Step step) const
{
    if (index < 0 || index >= int(placements_.size())) return -1;
    const int col = columnOf(index);
    const QRect cur = placements_[size_t(index)].rect;

    // Within a column the cards are a plain top-to-bottom stack, so up and down
    // are the adjacent slot in that same column — and the bottom of a column is
    // the end. Deliberately no wrap to the next column's top: that jump across
    // the whole board is the unpredictable move, and Left/Right is how you
    // change column.
    if (step == Step::Up || step == Step::Down) {
        int best = -1;
        for (size_t i = 0; i < placements_.size(); ++i) {
            if (columnOf(int(i)) != col) continue;
            const int y = placements_[i].rect.y();
            if (step == Step::Down ? y <= cur.y() : y >= cur.y()) continue;
            if (best < 0
                || (step == Step::Down ? y < placements_[size_t(best)].rect.y()
                                       : y > placements_[size_t(best)].rect.y()))
                best = int(i);
        }
        return best;
    }

    const int target = col + (step == Step::Right ? 1 : -1);
    if (target < 0 || target >= columns_) return -1;

    // The card that best lines up with this one: most vertical overlap, and
    // where nothing overlaps at all, the nearest centre. Ragged columns mean
    // the answer is rarely the same row index.
    int best = -1;
    int bestOverlap = 0;
    int bestGap = 0;
    for (size_t i = 0; i < placements_.size(); ++i) {
        if (columnOf(int(i)) != target) continue;
        const QRect r = placements_[i].rect;
        const int overlap = std::min(cur.bottom(), r.bottom()) - std::max(cur.top(), r.top());
        const int gap = std::abs(r.center().y() - cur.center().y());
        if (best < 0 || overlap > bestOverlap || (overlap == bestOverlap && gap < bestGap)) {
            best = int(i);
            bestOverlap = overlap;
            bestGap = gap;
        }
    }
    return best;
}

int BoardLayout::indexOf(ItemId id) const
{
    for (size_t i = 0; i < placements_.size(); ++i)
        if (placements_[i].id == id) return int(i);
    return -1;
}

}  // namespace napkin
