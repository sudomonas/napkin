#pragma once
#include "Buffer.h"
#include "Item.h"
#include <vector>

namespace napkin {

class Database;
class BufferRepository;
class ItemRepository;

// Lifecycle constants from SPEC.md §6. Napkin never auto-deletes a live buffer;
// age only moves it into the collapsed OLDER section.
inline constexpr int kOlderThresholdDays  = 30;
inline constexpr int kTrashRetentionDays  = 30;

// Below this the nudge would be noise. Napkin tolerates accumulation — the
// offer to clean up should feel like a convenience, not a scolding.
inline constexpr int kSweepNudgeThreshold = 12;
inline constexpr qint64 kMsPerDay = 24LL * 60 * 60 * 1000;

// An unpersisted buffer. Ctrl+N produces one of these and nothing else:
// invariant 5 says no row exists until there is content, which is what makes
// "abandoned empty buffers" a non-problem rather than a cleanup rule.
class Draft {
public:
    void add(Item item) { items_.push_back(std::move(item)); }
    void setText(const QString& text);

    bool isEmpty() const;
    const std::vector<Item>& items() const { return items_; }
    void clear() { items_.clear(); }

private:
    std::vector<Item> items_;
};

class BufferService {
public:
    BufferService(Database& db, BufferRepository& buffers, ItemRepository& items)
        : db_(db), buffers_(buffers), items_(items) {}

    // Returns kNoBuffer and writes nothing when the draft has no content.
    BufferId commitDraft(const Draft& draft);

    ItemId appendTo(BufferId id, Item item);
    void updateTextItem(BufferId bufferId, ItemId itemId, const QString& text);
    void removeItem(BufferId bufferId, ItemId itemId);

    // Deleting items puts them in the trash, like deleting a napkin does, so a
    // missed undo toast no longer means they are gone. Deleting every item of
    // a napkin trashes that napkin; deleting some moves them into a new napkin
    // that goes straight to the trash, where it can be restored or emptied like
    // any other. The trash stays a list of napkins, so there is one place to
    // look and one retention rule.
    struct TrashedItems {
        BufferId holder = kNoBuffer;   // the napkin now in the trash
        bool wholeNapkin = false;      // holder is the original napkin
    };
    TrashedItems trashItems(BufferId from, const std::vector<ItemId>& ids);
    // Deleting items from a napkin that is already in the trash is final, as
    // deleting the napkin there is. When that leaves it empty the napkin goes
    // too: an empty napkin in the trash would restore as nothing. Returns
    // whether the napkin was deleted with them. Throws if `from` is not in the
    // trash — a live napkin's items go to the trash, never straight to nothing.
    bool deleteTrashedItems(BufferId from, const std::vector<ItemId>& ids);
    // Undo of trashItems: every item goes back to `from` where it was.
    void untrashItems(BufferId from, const TrashedItems& trashed,
                      const std::vector<Item>& originals);

    void setPinned(BufferId id, bool pinned);
    void setKept(BufferId id, bool kept);

    // false => the buffer is kept and the caller must confirm first.
    bool trash(BufferId id);
    void trashConfirmed(BufferId id);
    // Returns where the content now is: `id` itself, or — for a napkin that
    // holds items deleted from another one — that original napkin, if it is
    // still live, with the items put back into it.
    BufferId restore(BufferId id);
    // Some of the cards of something in the trash, back where they belong:
    // into the napkin they were deleted from (brought back too if it is itself
    // in the trash), or — for a whole napkin in the trash — that napkin comes
    // back with just these, and the rest stay in the trash as cards deleted
    // from it, so restoring them later joins the same napkin rather than a new
    // one. All of them is the same as restore(). Returns where they went.
    BufferId restoreItems(BufferId from, const std::vector<ItemId>& ids);

    // The only automatic hard delete in Napkin. Touches nothing the user has
    // not already deleted.
    int purgeExpiredTrash();

    // Empties the trash now, at the user's explicit request.
    int emptyTrash();

    // Buffers older than this belong in the collapsed OLDER section.
    static Timestamp olderThanCutoff();
    static int olderThanDays();
    static int trashRetentionDays();

private:
    Database&         db_;
    BufferRepository& buffers_;
    ItemRepository&   items_;
};

}  // namespace napkin
