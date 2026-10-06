#include "BufferRepository.h"
#include "Database.h"
#include "Statement.h"
#include "../domain/Clock.h"

namespace napkin {
namespace {

constexpr const char* kCols =
    "id, created_at, modified_at, pinned, kept, deleted_at";

Buffer readBuffer(const Statement& s)
{
    Buffer b;
    b.id         = s.columnInt64(0);
    b.createdAt  = s.columnInt64(1);
    b.modifiedAt = s.columnInt64(2);
    b.pinned     = s.columnBool(3);
    b.kept       = s.columnBool(4);
    b.deletedAt  = s.columnOptInt64(5);
    b.name       = s.columnText(6);
    b.restoresTo = s.columnOptInt64(7);
    return b;
}

}  // namespace

BufferId BufferRepository::create(bool pinned, bool kept)
{
    const Timestamp now = nowMs();
    Statement s(db_, "INSERT INTO buffers(created_at, modified_at, pinned, kept) VALUES(?,?,?,?)");
    s.bind(1, now).bind(2, now).bind(3, pinned).bind(4, kept);
    s.exec();
    return db_.lastInsertId();
}

std::optional<Buffer> BufferRepository::find(BufferId id)
{
    Statement s(db_, "SELECT id, created_at, modified_at, pinned, kept, deleted_at, name, restores_to"
                     " FROM buffers WHERE id = ?");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return readBuffer(s);
}

void BufferRepository::touch(BufferId id)
{
    Statement s(db_, "UPDATE buffers SET modified_at = ? WHERE id = ?");
    s.bind(1, nowMs()).bind(2, id);
    s.exec();
}

void BufferRepository::setPinned(BufferId id, bool pinned)
{
    Statement s(db_, "UPDATE buffers SET pinned = ? WHERE id = ?");
    s.bind(1, pinned).bind(2, id);
    s.exec();
}

void BufferRepository::setName(BufferId id, const QString& name)
{
    // Not a change to the napkin's contents, so modified_at stays: naming one
    // must not move it in the list.
    const QString cleaned = name.simplified().left(kMaxNameLength);
    Statement s(db_, "UPDATE buffers SET name = ? WHERE id = ?");
    if (cleaned.isEmpty()) s.bindNull(1); else s.bind(1, cleaned);
    s.bind(2, id);
    s.exec();
}

void BufferRepository::setKept(BufferId id, bool kept)
{
    Statement s(db_, "UPDATE buffers SET kept = ? WHERE id = ?");
    s.bind(1, kept).bind(2, id);
    s.exec();
}

bool BufferRepository::moveToTrash(BufferId id)
{
    const auto b = find(id);
    if (!b) return false;
    if (b->kept) return false;  // caller must confirm; see moveToTrashConfirmed

    Statement s(db_, "UPDATE buffers SET deleted_at = ? WHERE id = ? AND deleted_at IS NULL");
    s.bind(1, nowMs()).bind(2, id);
    s.exec();
    return true;
}

void BufferRepository::moveToTrashConfirmed(BufferId id)
{
    Statement s(db_, "UPDATE buffers SET deleted_at = ?, kept = 0"
                     " WHERE id = ? AND deleted_at IS NULL");
    s.bind(1, nowMs()).bind(2, id);
    s.exec();
}

void BufferRepository::restore(BufferId id)
{
    // Back where it was: its last change is when it was last changed, not
    // when it left the trash. Stamping it "just now" moved a three-day-old
    // napkin to the top of the list, and every restore reshuffled it
    // (usability test, 2026-10-06).
    Statement s(db_, "UPDATE buffers SET deleted_at = NULL WHERE id = ?");
    s.bind(1, id);
    s.exec();
}

void BufferRepository::setModifiedAt(BufferId id, Timestamp when)
{
    Statement s(db_, "UPDATE buffers SET modified_at = ? WHERE id = ?");
    s.bind(1, when).bind(2, id);
    s.exec();
}

std::vector<Buffer> BufferRepository::listLive(int limit, int offset)
{
    // SPEC.md §7: pinned above everything, then newest first. Windowed, never
    // SELECT * over the whole table (§12).
    Statement s(db_, "SELECT id, created_at, modified_at, pinned, kept, deleted_at, name, restores_to"
                     " FROM buffers WHERE deleted_at IS NULL"
                     " ORDER BY pinned DESC, modified_at DESC, id DESC"
                     " LIMIT ? OFFSET ?");
    s.bind(1, limit).bind(2, offset);

    std::vector<Buffer> out;
    while (s.step()) out.push_back(readBuffer(s));
    return out;
}

std::vector<Buffer> BufferRepository::listTrash()
{
    Statement s(db_, "SELECT id, created_at, modified_at, pinned, kept, deleted_at, name, restores_to"
                     " FROM buffers WHERE deleted_at IS NOT NULL"
                     " ORDER BY deleted_at DESC");
    std::vector<Buffer> out;
    while (s.step()) out.push_back(readBuffer(s));
    return out;
}

int BufferRepository::countLive()
{
    Statement s(db_, "SELECT COUNT(*) FROM buffers WHERE deleted_at IS NULL");
    return s.step() ? s.columnInt(0) : 0;
}

int BufferRepository::countTrash()
{
    Statement s(db_, "SELECT COUNT(*) FROM buffers WHERE deleted_at IS NOT NULL");
    return s.step() ? s.columnInt(0) : 0;
}

int BufferRepository::countPurgeable()
{
    Statement s(db_, "SELECT COUNT(*) FROM buffers WHERE deleted_at IS NOT NULL AND kept = 0");
    return s.step() ? s.columnInt(0) : 0;
}

int BufferRepository::countKept()
{
    Statement s(db_, "SELECT COUNT(*) FROM buffers WHERE kept = 1 AND deleted_at IS NULL");
    return s.step() ? s.columnInt(0) : 0;
}

int BufferRepository::purgeTrashOlderThan(Timestamp cutoff)
{
    Statement s(db_, "DELETE FROM buffers"
                     " WHERE deleted_at IS NOT NULL AND deleted_at < ? AND kept = 0");
    s.bind(1, cutoff);
    s.exec();
    return db_.changes();
}

int BufferRepository::purgeAllTrash()
{
    Statement s(db_, "DELETE FROM buffers WHERE deleted_at IS NOT NULL AND kept = 0");
    s.exec();
    return db_.changes();
}

void BufferRepository::setRestoresTo(BufferId holder, BufferId origin)
{
    Statement s(db_, "UPDATE buffers SET restores_to = ? WHERE id = ?");
    s.bind(1, origin).bind(2, holder);
    s.exec();
}

std::optional<BufferId> BufferRepository::restoresTo(BufferId holder)
{
    Statement s(db_, "SELECT restores_to FROM buffers WHERE id = ?");
    s.bind(1, holder);
    if (!s.step()) return std::nullopt;
    const auto v = s.columnOptInt64(0);
    return v ? std::optional<BufferId>(*v) : std::nullopt;
}

void BufferRepository::clearRestoresTo(BufferId holder)
{
    Statement s(db_, "UPDATE buffers SET restores_to = NULL WHERE id = ?");
    s.bind(1, holder);
    s.exec();
}

bool BufferRepository::removeIfEmpty(BufferId id)
{
    Statement s(db_, "DELETE FROM buffers WHERE id = ? AND kept = 0"
                     " AND NOT EXISTS (SELECT 1 FROM items WHERE buffer_id = ?)");
    s.bind(1, id).bind(2, id);
    s.exec();
    return db_.changes() > 0;
}

void BufferRepository::hardDeleteEvenIfKept(BufferId id)
{
    Transaction tx(db_);
    db_.exec("INSERT OR REPLACE INTO napkin_meta(key, value) VALUES('allow_kept_delete','1')");
    {
        Statement s(db_, "DELETE FROM buffers WHERE id = ?");
        s.bind(1, id);
        s.exec();
    }
    db_.exec("DELETE FROM napkin_meta WHERE key = 'allow_kept_delete'");
    tx.commit();
}

}  // namespace napkin
