#pragma once
#include "../domain/Buffer.h"
#include <optional>
#include <vector>

namespace napkin {

class Database;

class BufferRepository {
public:
    explicit BufferRepository(Database& db) : db_(db) {}

    BufferId create(bool pinned = false, bool kept = false);
    std::optional<Buffer> find(BufferId id);
    void touch(BufferId id);  // bump modified_at

    void setPinned(BufferId id, bool pinned);
    void setKept(BufferId id, bool kept);
    // Empty clears it, and the napkin is titled from its contents again.
    static constexpr int kMaxNameLength = 120;
    void setName(BufferId id, const QString& name);

    // Ordinary trash. Refuses a kept buffer and returns false — the caller must
    // obtain confirmation and call moveToTrashConfirmed instead. Making the
    // refusal structural means no UI path can forget to ask (SPEC.md §6).
    bool moveToTrash(BufferId id);

    // Confirmed deletion of a kept buffer. Releases the keep as it goes: having
    // confirmed, the user has withdrawn the request to hold on to it.
    void moveToTrashConfirmed(BufferId id);

    void restore(BufferId id);
    // Undo has to be able to put a timestamp back exactly as it was.
    void setModifiedAt(BufferId id, Timestamp when);

    std::vector<Buffer> listLive(int limit, int offset = 0);
    std::vector<Buffer> listTrash();

    int countLive();
    int countTrash();
    // What an "empty trash" would actually destroy: kept rows are skipped.
    int countPurgeable();
    int countKept();

    // The only automatic hard delete in Napkin, and it only ever touches rows
    // the user already deleted (SPEC.md §6). kept = 0 is belt and braces; the
    // confirmed-trash path has already cleared it.
    int purgeTrashOlderThan(Timestamp cutoff);

    // User-initiated "empty trash". Irreversible, so the caller confirms first.
    // kept = 0 is belt and braces: a kept buffer cannot be in the trash, and if
    // one somehow is, skipping it is the safe failure.
    int purgeAllTrash();

    // Escape hatch for a future "delete permanently" path. Raises the
    // transaction-scoped guard flag so the trigger permits the delete.
    void hardDeleteEvenIfKept(BufferId id);
    // Deletes the buffer only if it holds no items and is not kept. Used to
    // take back the napkin an item delete put in the trash, once undo has
    // moved its items home. Returns whether it deleted.
    bool removeIfEmpty(BufferId id);

    // Where a napkin's items belong when it is restored (see Migrations v6).
    void setRestoresTo(BufferId holder, BufferId origin);
    std::optional<BufferId> restoresTo(BufferId holder);
    void clearRestoresTo(BufferId holder);

private:
    Database& db_;
};

}  // namespace napkin
