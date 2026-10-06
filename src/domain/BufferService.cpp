#include "BufferService.h"
#include "Clock.h"

#include <QSettings>
#include "../data/BufferRepository.h"
#include "../data/Database.h"
#include "../data/ItemRepository.h"

namespace napkin {

void Draft::setText(const QString& text)
{
    for (auto& i : items_) {
        if (i.type == ItemType::Text) { i.text = text; return; }
    }
    if (!text.trimmed().isEmpty()) items_.insert(items_.begin(), Item::makeText(text));
}

bool Draft::isEmpty() const
{
    for (const auto& i : items_)
        if (!i.isEmpty()) return false;
    return true;
}

BufferId BufferService::commitDraft(const Draft& draft)
{
    if (draft.isEmpty()) return kNoBuffer;  // invariant 5

    Transaction tx(db_);
    const BufferId id = buffers_.create();
    for (const auto& item : draft.items()) {
        if (item.isEmpty()) continue;
        items_.append(id, item);
    }
    tx.commit();
    return id;
}

ItemId BufferService::appendTo(BufferId id, Item item)
{
    Transaction tx(db_);
    const ItemId itemId = items_.append(id, std::move(item));
    buffers_.touch(id);
    tx.commit();
    return itemId;
}

void BufferService::updateTextItem(BufferId bufferId, ItemId itemId, const QString& text)
{
    Transaction tx(db_);
    items_.updateText(itemId, text);
    buffers_.touch(bufferId);
    tx.commit();
}

bool BufferService::deleteTrashedItems(BufferId from, const std::vector<ItemId>& ids)
{
    const auto buffer = buffers_.find(from);
    if (!buffer || !buffer->inTrash())
        throw DbError(QStringLiteral("refusing to delete items for good from a napkin that is not in the trash"));
    // Only items that are on this napkin; anything else is not ours to delete.
    std::vector<ItemId> mine;
    for (ItemId id : ids)
        if (const auto item = items_.find(id); item && item->bufferId == from) mine.push_back(id);

    // All of what is left: delete the napkin, and its items go with it (ON
    // DELETE CASCADE). One statement's transaction — hardDeleteEvenIfKept opens
    // its own, and SQLite does not nest them, which is what failed when this
    // deleted the items first inside an outer transaction.
    if (int(mine.size()) >= items_.countForBuffer(from)) {
        buffers_.hardDeleteEvenIfKept(from);
        return true;
    }
    Transaction tx(db_);
    for (ItemId id : mine) items_.remove(id);
    tx.commit();
    return false;
}

void BufferService::removeItem(BufferId bufferId, ItemId itemId)
{
    Transaction tx(db_);
    items_.remove(itemId);
    buffers_.touch(bufferId);
    tx.commit();
}

BufferService::TrashedItems BufferService::trashItems(BufferId from,
                                                     const std::vector<ItemId>& ids)
{
    TrashedItems out;
    if (ids.empty()) return out;
    Transaction tx(db_);
    if (int(ids.size()) >= items_.countForBuffer(from)) {
        // Everything is going: that is deleting the napkin, and a kept napkin
        // asked for this item by item, so the keep is released rather than
        // refused (the undo path puts it back).
        if (!buffers_.moveToTrash(from)) buffers_.moveToTrashConfirmed(from);
        out = {from, true};
    } else {
        const BufferId holder = buffers_.create();
        for (ItemId id : ids)
            if (const auto item = items_.find(id)) items_.moveTo(id, holder, item->position);
        buffers_.setRestoresTo(holder, from);
        buffers_.moveToTrash(holder);
        buffers_.touch(from);
        out = {holder, false};
    }
    tx.commit();
    return out;
}

void BufferService::untrashItems(BufferId from, const TrashedItems& trashed,
                                 const std::vector<Item>& originals)
{
    Transaction tx(db_);
    if (trashed.wholeNapkin) {
        buffers_.restore(from);
    } else {
        for (const Item& item : originals) items_.moveTo(item.id, from, item.position);
        buffers_.removeIfEmpty(trashed.holder);
    }
    tx.commit();
}

// Pin and Keep are metadata about the buffer, not edits to it, so neither
// bumps modified_at — flipping a pin must not reshuffle the list (SPEC.md §7).
void BufferService::setPinned(BufferId id, bool pinned) { buffers_.setPinned(id, pinned); }
void BufferService::setKept(BufferId id, bool kept)     { buffers_.setKept(id, kept); }

bool BufferService::trash(BufferId id)          { return buffers_.moveToTrash(id); }
void BufferService::trashConfirmed(BufferId id) { buffers_.moveToTrashConfirmed(id); }
BufferId BufferService::restore(BufferId id)
{
    Transaction tx(db_);
    BufferId target = id;
    const auto origin = buffers_.restoresTo(id);
    const auto home = origin ? buffers_.find(*origin) : std::nullopt;
    if (home && !home->inTrash()) {
        for (const Item& item : items_.listForBuffer(id)) items_.moveTo(item.id, *origin, item.position);
        buffers_.removeIfEmpty(id);
        buffers_.touch(*origin);
        target = *origin;
    } else {
        // Its napkin is gone or in the trash itself: it comes back as a napkin
        // of its own, and from then on it is one.
        buffers_.clearRestoresTo(id);
        buffers_.restore(id);
    }
    tx.commit();
    return target;
}

int BufferService::purgeExpiredTrash()
{
    return buffers_.purgeTrashOlderThan(nowMs() - trashRetentionDays() * kMsPerDay);
}

int BufferService::emptyTrash()
{
    return buffers_.purgeAllTrash();
}

// Both read from settings, with the constants as defaults. The domain does not
// depend on the UI for this: a plain QSettings read keeps napkin_core free of
// any dialog.
int BufferService::olderThanDays()
{
    return QSettings().value(QStringLiteral("lifecycle/olderThanDays"),
                             kOlderThresholdDays).toInt();
}

int BufferService::trashRetentionDays()
{
    return QSettings().value(QStringLiteral("lifecycle/trashRetentionDays"),
                             kTrashRetentionDays).toInt();
}

Timestamp BufferService::olderThanCutoff()
{
    return nowMs() - qint64(olderThanDays()) * kMsPerDay;
}

}  // namespace napkin
