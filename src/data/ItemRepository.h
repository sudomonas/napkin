#pragma once
#include "../domain/Item.h"
#include "../domain/Preview.h"
#include <optional>
#include <vector>

namespace napkin {

class Database;

class ItemRepository {
public:
    explicit ItemRepository(Database& db) : db_(db) {}

    // Appends at the end of the buffer. Assigns position and created_at.
    ItemId append(BufferId bufferId, Item item);

    // Puts a removed item back at the position it held, so undo restores the
    // buffer as it was rather than moving everything to the end.
    ItemId restoreAt(const Item& item);

    std::optional<Item> find(ItemId id);
    std::vector<Item> listForBuffer(BufferId bufferId);

    // Only the first few items, for deriving a card preview. A list of 5000
    // buffers must never read every item to draw itself (SPEC.md §12).
    std::vector<Item> previewHead(BufferId bufferId, int limit = kPreviewHeadSize);
    // The most recently added or edited items — the top of the board — text
    // cut to what a list row can show.
    std::vector<Item> latestItems(BufferId bufferId, int limit);
    int countForBuffer(BufferId bufferId);

    struct Counts { int total = 0; int images = 0; };
    // Both counts in one statement: a card needs the item count for its detail
    // line and the image count for its thumbnail row.
    Counts countsForBuffer(BufferId bufferId);

    void updateText(ItemId id, const QString& text);
    void remove(ItemId id);
    // Re-homes an item without touching its content or timestamps: deleting
    // items moves them into the trash this way, and undo moves them back.
    void moveTo(ItemId id, BufferId buffer, int position);

    // SPEC.md §5: a blob is unlinked only when no item references it any more.
    // A query, not a refcount — so no drift is possible.
    bool blobIsReferenced(const QString& hash);
    std::vector<QString> allBlobHashes();
    std::vector<Item> allImageItems();

private:
    int nextPosition(BufferId bufferId);

    Database& db_;
};

}  // namespace napkin
