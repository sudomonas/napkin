#include "BoardLayout.h"
#include "../domain/Links.h"
#include "LinkChip.h"
#include "Tokens.h"

#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QElapsedTimer>
#include <QTextDocument>
#include <algorithm>
#include <cmath>
#include <limits>

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

void BoardLayout::setScale(qreal scale)
{
    if (qFuzzyCompare(scale_, scale)) return;
    scale_ = scale;
    // The key holds the column width but not the height cap, which scales too.
    cache_.clear();
}

void BoardLayout::setColumnCap(int columns) { columnCap_ = std::max(0, columns); }

int BoardLayout::scaled(int px) const { return int(std::lround(px * scale_)); }
int BoardLayout::gap() const { return scaled(kCardGap); }
int BoardLayout::maxCardHeight() const { return scaled(kCardMaxHeight); }

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

    const bool image = item.type == ItemType::Image;
    const int inner = std::max(40, columnWidth - cardInset(image) * 2);
    const int chrome = cardChromeHeight(body_ ? *body_ : QFont(), image);

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
        content = LinkChip::preferredHeight(body_ ? *body_ : QFont());
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
        if (item.text.size() > kMeasureLimit) content = maxCardHeight();   // certainly over
    } else {
        // From the stored dimensions: no file is opened and no image decoded
        // just to find out how tall a card is.
        if (item.width > 0 && item.height > 0)
            content = item.height * std::min(inner, item.width) / std::max(1, item.width);
        else
            content = 96;
    }
    const int natural = content + chrome;
    const int tallest = maxCardHeight();
    int height = std::clamp(natural, std::min(scaled(kCardMinHeight), tallest), tallest);
    const bool overflows = natural > tallest;

    // A clipped text card is cut wherever the height cap happens to fall, which
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

void BoardLayout::reset(const std::vector<Item>& items)
{
    placements_.clear();
    const int minWidth = scaled(kCardMinWidth);
    const int maxWidth = scaled(kCardMaxWidth);
    const int target   = scaled(kCardTargetWidth);
    const int g        = gap();
    const int usable = std::max(minWidth, viewportWidth_ - kPadX * 2);
    int columns = std::max(1, (usable + g) / (target + g));
    if (columnCap_ > 0 && columns > columnCap_) {
        // Fewer columns than fit: the width they would have had goes to the
        // cards — up to a measure that still reads as a card rather than a page.
        columns = columnCap_;
        columnWidth_ = std::clamp((usable - (columns - 1) * g) / columns, minWidth,
                                  std::max(maxWidth, scaled(kCardCappedMaxWidth)));
    } else {
        columnWidth_ = std::clamp((usable - (columns - 1) * g) / columns, minWidth, maxWidth);
        // Recompute the count against the settled width so the two agree; an
        // earlier version derived them independently and the column count
        // flipped on 20px of unrelated chrome.
        columns = std::max(1, (usable + g) / (columnWidth_ + g));
        columnWidth_ = std::clamp((usable - (columns - 1) * g) / columns, minWidth, maxWidth);
    }
    // A capped board is centred in what its columns leave over. An uncapped one
    // keeps to the left, as it always has: its leftover is under one column.
    const int spare = usable - (columns * columnWidth_ + (columns - 1) * g);
    left_ = kPadX + (columnCap_ > 0 ? std::max(0, spare / 2) : 0);

    columns_ = columns;
    bottoms_.assign(size_t(columns), kPadTop);
    itemCount_ = items.size();
    placedHeight_ = 0;
    placements_.reserve(items.size());
    updateTotalHeight();
}

void BoardLayout::placeNext(const Item& item)
{
    size_t shortest = 0;
    for (size_t i = 1; i < bottoms_.size(); ++i)
        if (bottoms_[i] < bottoms_[shortest]) shortest = i;

    bool clipped = false;
    const int h = heightFor(item, columnWidth_, &clipped);
    const int x = left_ + int(shortest) * (columnWidth_ + gap());
    placements_.push_back({item.id, QRect(x, bottoms_[shortest], columnWidth_, h), clipped});
    bottoms_[shortest] += h + gap();
    placedHeight_ += h + gap();
}

void BoardLayout::updateTotalHeight()
{
    int deepest = kPadTop;
    for (int b : bottoms_) deepest = std::max(deepest, b);
    if (!complete() && !placements_.empty()) {
        // The rest, at the average height of what has been measured, shared
        // across the columns.
        const qint64 remaining = qint64(itemCount_ - placements_.size());
        const qint64 average = placedHeight_ / qint64(placements_.size());
        int shallowest = deepest;
        for (int b : bottoms_) shallowest = std::min(shallowest, b);
        deepest = std::max<qint64>(deepest,
                                   shallowest + remaining * average / std::max(1, columns_));
    }
    totalHeight_ = deepest + kPadTop - gap();
}

bool BoardLayout::placeThrough(const std::vector<Item>& items, int y)
{
    const size_t before = placements_.size();
    // The next card goes into the shortest column, so once every column
    // reaches past y nothing still to come can start above it.
    auto shallowest = [this] { return *std::min_element(bottoms_.begin(), bottoms_.end()); };
    while (placements_.size() < itemCount_ && placements_.size() < items.size()
           && shallowest() <= y)
        placeNext(items[placements_.size()]);
    if (placements_.size() == before) return false;
    updateTotalHeight();
    return true;
}

bool BoardLayout::placeFor(const std::vector<Item>& items, int budgetMs)
{
    const size_t before = placements_.size();
    QElapsedTimer clock;
    clock.start();
    while (placements_.size() < itemCount_ && placements_.size() < items.size()) {
        placeNext(items[placements_.size()]);
        if (clock.elapsed() >= budgetMs) break;
    }
    if (placements_.size() == before) return false;
    updateTotalHeight();
    return true;
}

void BoardLayout::placeAll(const std::vector<Item>& items)
{
    placeThrough(items, std::numeric_limits<int>::max());
}

void BoardLayout::rebuild(const std::vector<Item>& items)
{
    reset(items);
    placeAll(items);
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
    const int pitch = columnWidth_ + gap();
    if (pitch <= 0) return 0;
    return (placements_[size_t(index)].rect.x() - left_) / pitch;
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
