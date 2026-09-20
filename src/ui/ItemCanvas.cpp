#include "ItemCanvas.h"
#include "ItemCard.h"
#include "../domain/Clock.h"
#include "../media/BlobStore.h"
#include "Tokens.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QLabel>
#include <QMimeData>
#include <QDesktopServices>
#include <QEvent>
#include <QUrl>
#include "../domain/Links.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QVBoxLayout>

namespace napkin {
namespace {
using namespace tokens;
}  // namespace

ItemCanvas::ItemCanvas(Thumbnailer& thumbs, BlobStore& blobs, QWidget* parent)
    : QScrollArea(parent), thumbs_(thumbs), blobs_(blobs)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    // The board is the desk; the cards are paper on it. Cards carry their own
    // Base fill and an edge, so the surface behind them has to differ or they
    // have nothing to sit against.
    setBackgroundRole(QPalette::Window);
    viewport()->setAutoFillBackground(true);
    viewport()->setBackgroundRole(QPalette::Window);
    setFocusPolicy(Qt::StrongFocus);
    // The board is a focusable surface you arrow around, so it has to say what
    // it is when focus lands on it. Without this, tabbing from the buffer list
    // announced nothing and there was no way to tell you had arrived.
    setAccessibleName(tr("Board"));
    setAccessibleDescription(tr("The items on the selected napkin. "
                                "Arrow keys move between them, Enter opens one."));
    // Horizontal scrolling only appears if the window is narrower than one
    // full-width card, which is the honest outcome of a real minimum width.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    body_ = new QWidget;
    body_->setAutoFillBackground(false);
    setWidget(body_);

    // Rebuild the visible band as the board scrolls.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { syncVisibleCards(); });

    placeholder_ = new QLabel;
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    showNothingSelected();
}

void ItemCanvas::clearItems()
{
    cards_.clear();
    textCards_.clear();
    live_.clear();
    selected_.clear();
    anchor_ = kNoItem;
    cursor_ = -1;
    items_.clear();
    // The whole napkin, not just what is showing: an empty napkin's board kept
    // the previous napkin's items here, where a later filter could find them.
    allItems_.clear();
    // Copy first: setParent(nullptr) removes the child from the very list being
    // iterated, so walking it live skips every other widget and leaves stale
    // cards parented and visible.
    const QObjectList children = body_->children();
    for (QObject* child : children) {
        auto* w = qobject_cast<QWidget*>(child);
        if (!w || w == placeholder_) continue;
        // Reparenting before deleteLater() takes the widget out of the tree
        // now; deferring alone leaves removed cards live and drawn until the
        // next event-loop turn.
        w->hide();
        w->setParent(nullptr);
        w->deleteLater();
    }
}

// The width every card is laid out against. Deliberately NOT viewport()->width():
// with an as-needed scrollbar, adding one item can make the bar appear, shrink
// the viewport by ~14px, change the column width and resize EVERY card in the
// buffer. Reserving the extent unconditionally makes a card's size depend only
// on its own content.
int ItemCanvas::stableWidth() const
{
    return std::max(kCardMinWidth, width() - verticalScrollBar()->sizeHint().width()
                                       - frameWidth() * 2);
}

void ItemCanvas::wireCard(ItemCard* card)
{
    connect(card, &ItemCard::selectRequested, this, &ItemCanvas::applySelection);
    connect(card, &ItemCard::activated, this, &ItemCanvas::imageActivated);
    connect(card, &ItemCard::copyRequested, this, [this](ItemId id) {
        // One mechanism at two scopes, held by an invariant: after any copy the
        // clipboard matches what is visibly selected.
        applySelection(id, Qt::NoModifier);
        copySelection();   // which is also what acknowledges it
    });
    connect(card, &ItemCard::escaped, this, [this, card] {
        applySelection(card->itemId(), Qt::NoModifier);
        setFocus(Qt::OtherFocusReason);
    });
    if (auto* asText = qobject_cast<TextItemCard*>(card)) {
        // Only one card edits at a time, so the board never has two carets or
        // an ambiguous Ctrl+C.
        connect(asText, &TextItemCard::editingStarted, this, [this](ItemId id) {
            for (auto* other : textCards_)
                if (other->itemId() != id) other->endEditing();
            clearSelection();
        });
        connect(asText, &TextItemCard::editingFinished, this, &ItemCanvas::editingFinished);
    }
}

void ItemCanvas::showEmptyBuffer()
{
    clearItems();
    bufferShown_ = emptyBufferShown_ = true;
    // Napkin is temporary storage, not an editor. An empty buffer is waiting to
    // be pasted into, so it says that rather than offering a blank page.
    placeholder_->setText(tr("Nothing here yet.\n\nPaste with Ctrl+V, or press Ctrl+T "
                             "to write something."));
    QPalette pal = placeholder_->palette();
    pal.setColor(QPalette::WindowText, text(pal, kTextTertiary));
    placeholder_->setPalette(pal);
    placeholder_->setParent(body_);
    placeholder_->setGeometry(0, kPadTop, std::max(200, stableWidth()), 120);
    placeholder_->show();
    body_->setFixedSize(std::max(200, stableWidth()), viewport()->height());
}

void ItemCanvas::showNothingSelected()
{
    clearItems();
    bufferShown_ = emptyBufferShown_ = false;
    placeholder_->setText(tr("Select a napkin to see what is on it."));
    QPalette pal = placeholder_->palette();
    pal.setColor(QPalette::WindowText, text(pal, kTextTertiary));
    placeholder_->setPalette(pal);
    placeholder_->setParent(body_);
    placeholder_->setGeometry(0, kPadTop, std::max(200, stableWidth()), 120);
    placeholder_->show();
    body_->setFixedSize(std::max(200, stableWidth()), viewport()->height());
}

int ItemCanvas::indexOf(ItemId id) const
{
    return board_.indexOf(id);
}

QList<ItemId> ItemCanvas::itemOrder() const
{
    QList<ItemId> out;
    for (const auto& item : items_)
        if (item.id != kNoItem) out << item.id;   // the composer has no row yet
    return out;
}

void ItemCanvas::resizeEvent(QResizeEvent* e)
{
    QScrollArea::resizeEvent(e);
    relayout();
}

// Ctrl+T. One unwritten card at the top, focused, which becomes a real item the
// moment it has content and evaporates if it never does (invariant 5).
void ItemCanvas::addPendingTextCard()
{
    for (auto* existing : textCards_)
        if (existing->isComposer()) { existing->focusText(); return; }
    emptyBufferShown_ = false;

    Item blank;
    blank.type = ItemType::Text;
    blank.modifiedAt = nowMs();       // newest, so it lands at the top
    items_.insert(items_.begin(), blank);

    placeholder_->hide();
    relayout();

    // The composer has no row yet, so cardFor() cannot find it by id; build it
    // directly and let the next sync keep it alive because it is dirty.
    for (auto* card : cards_)
        if (auto* asText = qobject_cast<TextItemCard*>(card))
            if (asText->isComposer()) { asText->focusTextInteraction(); asText->focusText(); return; }
}

// Keeps the board's copy of an item in step with the widget the user is typing
// into. Both lists are updated: allItems_ is what a filter is re-derived from,
// and items_ is what the layout measures.
// A font change invalidates more than it looks like it does: the board's
// measurements were taken against the old face, and every card cached
// font-derived metrics when it was built. Rebuilding from the items we already
// hold is cheaper to reason about than trying to nudge each one.
void ItemCanvas::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::ApplicationFontChange || e->type() == QEvent::FontChange) {
        board_.setFont(font());   // also clears the measurement cache
        if (!allItems_.empty()) {
            const auto items = allItems_;
            setItems(items, -1);
        }
    }
    QScrollArea::changeEvent(e);
}

void ItemCanvas::openUrl(const QString& url)
{
    // Checked again here, at the boundary. The chip only appears for an http or
    // https URL, but this is the call that starts a browser — the cost of the
    // second check is nothing and the cost of trusting a caller is a scratch
    // surface that will execute whatever was on someone's clipboard.
    if (!links::isOpenable(url)) return;
    QDesktopServices::openUrl(QUrl(url, QUrl::StrictMode));
}

void ItemCanvas::syncCardText(TextItemCard* card)
{
    if (!card) return;
    const QString live = card->text();
    const ItemId id = card->itemId();

    auto update = [&](std::vector<Item>& list) {
        for (auto& item : list) {
            if (item.type != ItemType::Text) continue;
            // An unwritten composer has no id, and there is only ever one.
            if (item.id == id && (id != kNoItem || item.id == kNoItem)) {
                item.text = live;
                return;
            }
        }
    };
    update(items_);
    update(allItems_);
    board_.invalidate(id);
}

ItemCard* ItemCanvas::cardFor(const Item& item)
{
    if (auto* existing = live_.value(item.id, nullptr)) return existing;

    ItemCard* card = nullptr;
    if (item.type == ItemType::Text) {
        auto* text = new TextItemCard(item, body_);
        connect(text, &TextItemCard::edited, this, &ItemCanvas::edited);
        connect(text, &TextItemCard::imagePasted, this, &ItemCanvas::imagePasted);
        connect(text, &TextItemCard::openUrlRequested, this, &ItemCanvas::openUrl);
        connect(text, &TextItemCard::heightChanged, this, [this, text] {
            // The board measures heights from the ITEM data, which goes stale
            // the moment you type — so a card being edited stayed at its
            // minimum height no matter how much you pasted into it. Push the
            // live text back before measuring.
            syncCardText(text);
            relayout();
        });
        textCards_.push_back(text);
        card = text;
    } else {
        card = new ImageItemCard(item, thumbs_, blobs_, body_);
    }
    wireCard(card);
    live_.insert(item.id, card);
    return card;
}

void ItemCanvas::syncVisibleCards()
{
    if (items_.empty()) return;

    const QRect visible(0, verticalScrollBar()->value(),
                        viewport()->width(), viewport()->height());
    // An overscan band either side keeps scrolling smooth without holding the
    // whole board in memory.
    const auto wanted = board_.indicesIn(visible, viewport()->height());

    QSet<ItemId> keep;
    for (int i : wanted) keep.insert(board_.placements()[size_t(i)].id);

    // Never destroy the card being typed in, or the caret goes with it.
    for (auto* text : textCards_)
        if (text->hasEditFocus() || text->isDirty()) keep.insert(text->itemId());

    for (auto it = live_.begin(); it != live_.end();) {
        if (keep.contains(it.key())) { ++it; continue; }
        QWidget* w = it.value();
        cards_.erase(std::remove(cards_.begin(), cards_.end(), w), cards_.end());
        textCards_.erase(std::remove_if(textCards_.begin(), textCards_.end(),
                                        [w](TextItemCard* t) { return t == w; }),
                         textCards_.end());
        w->hide();
        w->setParent(nullptr);
        w->deleteLater();
        it = live_.erase(it);
    }

    cards_.clear();
    for (int i : wanted) {
        const auto& slot = board_.placements()[size_t(i)];
        const Item& item = items_[size_t(i)];
        ItemCard* card = cardFor(item);
        if (auto* asText = qobject_cast<TextItemCard*>(card))
            asText->setSearchTerms(query_.simplified().split(QLatin1Char(' '),
                                                            Qt::SkipEmptyParts));
        card->setGeometry(slot.rect);
        card->setClipped(slot.clipped);
        card->setSelected(selected_.contains(item.id));
        card->setCurrent(cursor_ == i);
        card->show();
        cards_.push_back(card);
    }
}

void ItemCanvas::setItems(const std::vector<Item>& items, int selectIndex)
{
    clearItems();
    if (items.empty()) { showEmptyBuffer(); return; }
    bufferShown_ = true;
    emptyBufferShown_ = false;
    placeholder_->hide();

    allItems_ = items;
    applyFilter();

    if (selectIndex >= 0 && !items_.empty()) {
        const int target = std::min(selectIndex, int(items_.size()) - 1);
        selected_ = {items_[size_t(target)].id};
        anchor_ = *selected_.begin();
        cursor_ = target;
        syncVisibleCards();
        // setItems destroyed whatever had focus, including the card the user
        // was deleting. Take it back, or the *second* Delete goes nowhere.
        setFocus(Qt::OtherFocusReason);
        emit selectionChanged();
    }
}

void ItemCanvas::setSearch(const QString& query, const std::vector<ItemId>& matching)
{
    query_ = query.trimmed();
    matching_.clear();
    for (ItemId id : matching) matching_.insert(id);
    showAll_ = false;
    applyFilter();
}

void ItemCanvas::setShowAll(bool showAll)
{
    if (showAll_ == showAll) return;
    showAll_ = showAll;
    applyFilter();
}

void ItemCanvas::applyFilter()
{
    const QStringList terms = query_.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);

    items_.clear();
    for (const auto& item : allItems_) {
        // An unwritten composer is never filtered away — it is where you are
        // typing, not a search result.
        if (isFiltered() && item.id != kNoItem && !matching_.contains(item.id)) continue;
        items_.push_back(item);
    }

    // Rebuild rather than reuse: the filter changes which ids exist on the
    // board, and a stale card would be bound to a row that is no longer shown.
    live_.clear();
    cards_.clear();
    textCards_.clear();
    const QObjectList children = body_->children();
    for (QObject* child : children) {
        auto* w = qobject_cast<QWidget*>(child);
        if (!w || w == placeholder_) continue;
        w->hide();
        w->setParent(nullptr);
        w->deleteLater();
    }
    cursor_ = -1;
    selected_.clear();

    relayout();
    for (auto* card : textCards_) card->setSearchTerms(terms);
    emit filterChanged();
}

void ItemCanvas::relayout()
{
    board_.setViewport(stableWidth());
    if (!textCards_.empty()) board_.setFont(textCards_.front()->font());
    board_.rebuild(items_);
    body_->setFixedSize(stableWidth(), std::max(board_.totalHeight(), viewport()->height()));
    syncVisibleCards();
}

void ItemCanvas::applySelection(ItemId id, Qt::KeyboardModifiers modifiers)
{
    if (id == kNoItem) return;   // the unwritten composer is not a selectable object

    if (modifiers & Qt::ControlModifier) {
        if (selected_.contains(id)) selected_.remove(id);
        else { selected_.insert(id); anchor_ = id; }
    } else if ((modifiers & Qt::ShiftModifier) && anchor_ != kNoItem) {
        // Over the board, not the band: a range whose anchor had scrolled out
        // of existence used to find no `from` and silently extend nothing.
        const int from = indexOf(anchor_);
        const int to   = indexOf(id);
        if (from >= 0 && to >= 0) {
            selected_.clear();
            for (int i = std::min(from, to); i <= std::max(from, to); ++i)
                if (items_[size_t(i)].id != kNoItem) selected_.insert(items_[size_t(i)].id);
        }
    } else {
        // Uniform: a single click selects, whatever the item is.
        selected_ = {id};
        anchor_ = id;
    }

    // Keep the keyboard cursor where the mouse just acted, so the two never
    // disagree about "the current card". A BOARD index: syncVisibleCards()
    // always read it as one (`cursor_ == i` over placements) while this wrote a
    // band index, so the ring was drawn on the wrong card whenever the band did
    // not start at the top.
    cursor_ = indexOf(id);
    for (auto* card : cards_) card->setCurrent(card->itemId() == id);

    // Selecting another card is also leaving the one being edited.
    for (auto* card : textCards_)
        if (card->itemId() != id) card->endEditing();

    for (auto* card : cards_) card->setSelected(selected_.contains(card->itemId()));
    // Selecting in the canvas moves the keyboard here. Without this, clicking a
    // card left focus on the buffer list, so Delete was delivered to the list —
    // which trashes a whole buffer — rather than to the selected item.
    if (!keyboardIsHere()) setFocus(Qt::MouseFocusReason);
    emit selectionChanged();
}

// Tabbing onto the board showed nothing: no card was current, so there was no
// ring to draw. Arriving by keyboard now marks the first card current — only
// current, not selected, so it changes nothing the user did not ask for.
void ItemCanvas::focusInEvent(QFocusEvent* e)
{
    QScrollArea::focusInEvent(e);
    const bool byKeyboard = e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason;
    if (byKeyboard && cursor_ < 0 && !cards_.empty()) {
        cursor_ = 0;
        cards_.front()->setCurrent(true);
    }
}

bool ItemCanvas::isTyping(const QKeyEvent* e)
{
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    const QString t = e->text();
    return !t.isEmpty() && t.at(0).isPrint();
}

void ItemCanvas::refreshTimestamps()
{
    for (auto* card : cards_) card->refreshTimestamp();
}

bool ItemCanvas::startsNoteOnTyping() const
{
    if (emptyBufferShown_) return true;
    for (auto* card : textCards_)
        if (card->isComposer()) return true;
    return false;
}

void ItemCanvas::discardComposer()
{
    TextItemCard* composer = nullptr;
    for (auto* card : textCards_)
        if (card->isComposer()) composer = card;
    if (!composer || !composer->text().trimmed().isEmpty()) return;

    std::erase_if(items_, [](const Item& i) { return i.id == kNoItem; });
    std::erase(cards_, static_cast<ItemCard*>(composer));
    std::erase(textCards_, composer);
    live_.remove(kNoItem);
    composer->hide();
    composer->deleteLater();

    if (items_.empty() && allItems_.empty()) { showEmptyBuffer(); return; }
    relayout();
}

void ItemCanvas::startNote(const QString& firstText)
{
    addPendingTextCard();
    for (auto* card : textCards_)
        if (card->isComposer()) { card->insertText(firstText); return; }
}

// Double-clicking empty board space writes a note. It is what people try on an
// empty napkin (the usability test: click, type, double-click, then give up),
// and on a full one it is the mouse's Ctrl+T.
ItemCard* ItemCanvas::cardAt(const QPoint& viewportPos) const
{
    const QPoint inBody = body_->mapFrom(viewport(), viewportPos);
    for (QWidget* w = body_->childAt(inBody); w && w != body_; w = w->parentWidget())
        if (auto* card = qobject_cast<ItemCard*>(w)) return card;
    return nullptr;
}

// Right-clicking an item offered nothing: napkins in the list had a menu, the
// things on them did not, and Cut/Copy/Delete were only in the shortcut sheet
// (usability test, 2026-09-19). A card being edited keeps its editor's own
// menu, which reaches here only if the editor declines it.
void ItemCanvas::contextMenuEvent(QContextMenuEvent* e)
{
    ItemCard* card = cardAt(viewport()->mapFromGlobal(e->globalPos()));
    if (!card) { QScrollArea::contextMenuEvent(e); return; }
    if (!selected_.contains(card->itemId())) applySelection(card->itemId(), Qt::NoModifier);

    QMenu menu(this);
    const bool single = selected_.size() == 1;
    if (single) {
        if (auto* text = qobject_cast<TextItemCard*>(card))
            menu.addAction(tr("Edit\tEnter"), this, [text] { text->beginEditing(); });
        else
            menu.addAction(tr("Open\tEnter"), this, [this, id = card->itemId()] { emit imageActivated(id); });
        menu.addSeparator();
    }
    menu.addAction(tr("Copy\tCtrl+C"), this, [this] { copySelection(); });
    menu.addAction(tr("Cut\tCtrl+X"), this, [this] { cutSelection(); });
    menu.addSeparator();
    menu.addAction(tr("Delete\tDel"), this, [this] { deleteSelection(); });
    menu.exec(e->globalPos());
}

void ItemCanvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (cardAt(e->position().toPoint())) { QScrollArea::mouseDoubleClickEvent(e); return; }
    if (bufferShown_ && e->button() == Qt::LeftButton) { addPendingTextCard(); return; }
    QScrollArea::mouseDoubleClickEvent(e);
}

void ItemCanvas::mousePressEvent(QMouseEvent* e)
{
    // Clicking away from a card is a commit. Anything else makes the user
    // wonder whether their typing was kept.
    commitEditing();
    clearSelection();
    QScrollArea::mousePressEvent(e);
}

void ItemCanvas::clearSelection()
{
    if (selected_.isEmpty()) return;
    selected_.clear();
    for (auto* card : cards_) card->setSelected(false);
    emit selectionChanged();
}

// THE BOARD IS NOT ITS CARDS. `cards_` holds only the virtualized visible band;
// `items_` is the whole napkin. Every selection verb used to be written against
// `cards_`, so "all" meant "all of the dozen currently on screen": Ctrl+A on a
// 60-item napkin selected 13, Ctrl+C copied those 13, and Ctrl+A then Delete
// left 47 items behind. Selection is a property of the napkin, so it is
// computed over `items_` and merely *drawn* on whichever cards exist.
void ItemCanvas::selectAll()
{
    selected_.clear();
    for (const auto& item : items_)
        if (item.id != kNoItem) selected_.insert(item.id);   // not the composer
    for (auto* card : cards_) card->setSelected(selected_.contains(card->itemId()));
    emit selectionChanged();
}

QList<ItemId> ItemCanvas::selection() const
{
    // In document order, so a copied multi-selection reads the way it looked.
    QList<ItemId> out;
    for (const auto& item : items_)
        if (item.id != kNoItem && selected_.contains(item.id)) out << item.id;
    return out;
}

// The card for a board index, or null when that item is outside the band and
// so has no widget. Callers must cope with null rather than assume a card.
ItemCard* ItemCanvas::liveCardAt(int index) const
{
    if (index < 0 || index >= int(items_.size())) return nullptr;
    return live_.value(items_[size_t(index)].id, nullptr);
}

// Copying is otherwise completely silent: nothing on screen changes, so there
// is no way to know it worked. The acknowledgement belongs to the verb, not to
// any one route into it. Wired to the footer button's signal, as it first
// shipped, Ctrl+C and the context menu stayed silent.
void ItemCanvas::acknowledgeCopy(const QList<ItemId>& ids)
{
    // A single card says so on itself, on the button that was pressed. Several
    // at once have no card to speak for them — and flashing all of them would
    // be a light show — so the count goes to the window's toast.
    if (ids.size() == 1) {
        if (auto* card = live_.value(ids.first(), nullptr)) {
            card->acknowledge(tr("Copied"));
            return;
        }
        // Reachable: selection() spans the whole board, so a selected item
        // whose card is outside the visible band has no footer to speak on.
        emit announced(tr("Copied"));
        return;
    }
    emit announced(tr("%n items copied", nullptr, int(ids.size())));
}

// What a card would put on the clipboard for this item. The live card wins when
// there is one: a note being edited holds text the database has not seen yet.
// Without the fallback, copying an item outside the band copied nothing.
QString ItemCanvas::plainTextFor(const Item& item) const
{
    if (auto* card = live_.value(item.id, nullptr)) return card->asPlainText();
    if (item.type == ItemType::Image)
        return item.sourceName.isEmpty() ? tr("[image]") : item.sourceName;
    return item.text;
}

void ItemCanvas::copySelection()
{
    const auto ids = selection();
    if (ids.isEmpty()) return;

    // A single image goes to the clipboard as an image, so it can be pasted
    // into anything. Anything else goes as text, joined in document order.
    if (ids.size() == 1) {
        for (const auto& item : items_) {
            if (item.id != ids.first() || item.type != ItemType::Image) continue;
            const QString path = blobs_.pathFor(item.blobHash, item.mime);
            QImage image(path);
            if (!image.isNull()) {
                auto* mime = new QMimeData;
                mime->setImageData(image);
                mime->setUrls({QUrl::fromLocalFile(path)});
                QApplication::clipboard()->setMimeData(mime);
                acknowledgeCopy(ids);
                return;
            }
            break;
        }
    }

    QStringList parts;
    for (const auto& item : items_)
        if (item.id != kNoItem && selected_.contains(item.id)) parts << plainTextFor(item);
    QApplication::clipboard()->setText(parts.join(QStringLiteral("\n\n")));
    acknowledgeCopy(ids);
}

void ItemCanvas::cutSelection()
{
    if (selected_.isEmpty()) return;
    copySelection();
    const auto ids = selection();
    if (!ids.isEmpty()) emit cutRequested(ids);
}

void ItemCanvas::deleteSelection()
{
    const auto ids = selection();
    if (ids.isEmpty()) return;
    emit removeRequested(ids);
}

void ItemCanvas::setCursorTo(int index, Qt::KeyboardModifiers modifiers)
{
    if (items_.empty()) return;
    // Over the whole board: clamping to the band meant End stopped at the last
    // card that happened to exist rather than the last item on the napkin.
    const int target = std::clamp(index, 0, int(items_.size()) - 1);
    if (auto* was = liveCardAt(cursor_)) was->setCurrent(false);
    cursor_ = target;

    const ItemId id = items_[size_t(cursor_)].id;
    if (modifiers & Qt::ShiftModifier) applySelection(id, Qt::ShiftModifier);
    else if (!(modifiers & Qt::ControlModifier)) applySelection(id, Qt::NoModifier);

    // The card the cursor just moved to may not exist yet. Scroll by the slot's
    // geometry, which the board knows for every item; that build the card.
    const auto& placed = board_.placements();   // not `slots`: Qt owns that word
    if (cursor_ < int(placed.size())) {
        const QRect r = placed[size_t(cursor_)].rect;
        ensureVisible(r.center().x(), r.center().y(),
                      r.width() / 2, r.height() / 2 + kCardGap);
    }
    if (auto* now = liveCardAt(cursor_)) now->setCurrent(true);
}

// Arrows walk the board as it LOOKS, not as it was written.
//
// They used to step document order — Down and Right both meaning "+1" — on the
// grounds that masonry makes a spatial walk unpredictable. It does not: the
// board deals every card into an explicit column, so "below" is exact. What the
// old rule actually produced was Down travelling sideways along the top row,
// once per column, before it ever moved down; with three columns the card
// directly beneath the cursor was unreachable by any key at all.
//
// On a one-column board this is identical to stepping document order, so narrow
// windows behave exactly as before.
void ItemCanvas::moveCursorSpatially(BoardLayout::Step step, Qt::KeyboardModifiers modifiers)
{
    if (items_.empty()) return;
    // The first arrow press has nowhere to move from, so it lands rather than
    // doing nothing — whichever direction it was.
    if (cursor_ < 0) { setCursorTo(0, modifiers); return; }

    const int target = board_.neighbour(cursor_, step);
    if (target < 0) return;   // the board ends this way; stay put
    setCursorTo(target, modifiers);
}

void ItemCanvas::keyPressEvent(QKeyEvent* e)
{
    // An empty napkin used to swallow typing without a trace: the board had
    // focus, nothing on it could take the text, and "Ctrl+T" was the only way
    // in. Typing on an empty napkin now starts a note with what was typed.
    if (isTyping(e) && startsNoteOnTyping()) { startNote(e->text()); return; }

    // Arrow keys walk the board in document order — down/right forward,
    // up/left back. Deliberately not spatial: masonry puts item 2 top-middle,
    // so a spatial walk would be unpredictable, while document order is the
    // order the cards were made.
    switch (e->key()) {
    case Qt::Key_Down:  moveCursorSpatially(BoardLayout::Step::Down, e->modifiers()); return;
    case Qt::Key_Up:    moveCursorSpatially(BoardLayout::Step::Up, e->modifiers()); return;
    case Qt::Key_Right: moveCursorSpatially(BoardLayout::Step::Right, e->modifiers()); return;
    case Qt::Key_Left:  moveCursorSpatially(BoardLayout::Step::Left, e->modifiers()); return;
    case Qt::Key_Home:  setCursorTo(0, e->modifiers()); return;
    case Qt::Key_End:   setCursorTo(int(items_.size()) - 1, e->modifiers()); return;
    case Qt::Key_Space:
        if (cursor_ >= 0 && cursor_ < int(items_.size())) {
            applySelection(items_[size_t(cursor_)].id, Qt::ControlModifier);
            return;
        }
        break;
    default: break;
    }

    // These only fire when the canvas itself has focus, not while a caret is in
    // a text block, so they can never eat a keystroke meant for the text.
    if (e->matches(QKeySequence::Copy))      { copySelection(); return; }
    if (e->matches(QKeySequence::Cut))       { cutSelection(); return; }
    if (e->matches(QKeySequence::SelectAll)) { selectAll(); return; }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelection();
        return;
    }
    if (e->key() == Qt::Key_Escape) { clearSelection(); return; }
    // Enter edits the card under the cursor — the keyboard equivalent of the
    // double-click — or opens it, if it is an image.
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        // The cursor is always on an item; the card exists because setCursorTo()
        // scrolled to it, so there is nothing sensible to do if it does not.
        if (auto* card = liveCardAt(cursor_)) {
            if (auto* asText = qobject_cast<TextItemCard*>(card)) { asText->beginEditing(); return; }
            emit imageActivated(card->itemId());
            return;
        }
    }
    QScrollArea::keyPressEvent(e);
}

std::vector<ItemCanvas::DirtyText> ItemCanvas::dirtyText() const
{
    std::vector<DirtyText> out;
    for (auto* card : textCards_)
        if (card->isDirty()) out.push_back({card->itemId(), card->text()});
    return out;
}

void ItemCanvas::acknowledgeSaved(const QList<ItemId>& ids, Timestamp when)
{
    for (ItemId id : ids)
        if (auto* card = live_.value(id, nullptr)) card->noteSaved(when);
}

void ItemCanvas::markClean()
{
    for (auto* card : textCards_) card->markClean();
}

bool ItemCanvas::bindComposer(ItemId newId)
{
    if (newId == kNoItem) return false;

    bool bound = false;
    for (auto* card : textCards_) {
        if (!card->isComposer()) continue;
        card->setItemId(newId);
        bound = true;
        break;
    }
    if (!bound) return false;

    // The board's own list has to learn the id too, or the composer stays a
    // row-less placeholder in the layout while its widget claims to be an item.
    for (auto& item : items_)
        if (item.id == kNoItem) { item.id = newId; break; }
    live_.remove(kNoItem);
    for (auto* card : cards_)
        if (card->itemId() == newId) live_.insert(newId, card);
    return true;
}



bool ItemCanvas::keyboardIsHere() const
{
    // window()->focusWidget(), not QApplication::focusWidget(): the latter is
    // only populated while the window is ACTIVE, so it answers null for a
    // background window and always null under a headless platform.
    const QWidget* top = window();
    QWidget* focus = top ? top->focusWidget() : nullptr;
    return focus && (focus == this || isAncestorOf(focus));
}

void ItemCanvas::commitEditing()
{
    for (auto* card : textCards_) card->endEditing();
}

bool ItemCanvas::isEditing() const
{
    for (auto* card : textCards_) if (card->hasEditFocus()) return true;
    return false;
}

bool ItemCanvas::textHasFocus() const
{
    for (auto* card : textCards_) if (card->textHasFocus()) return true;
    return false;
}

}  // namespace napkin
