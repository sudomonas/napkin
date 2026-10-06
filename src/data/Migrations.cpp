#include "Migrations.h"
#include "Database.h"
#include "DbError.h"
#include <QString>
#include <array>

namespace napkin {
namespace {

// --- v1: buffers, items, the kept-buffer guard -------------------------------
constexpr const char* kV1 = R"SQL(
CREATE TABLE buffers (
  id           INTEGER PRIMARY KEY,
  created_at   INTEGER NOT NULL,
  modified_at  INTEGER NOT NULL,
  pinned       INTEGER NOT NULL DEFAULT 0 CHECK (pinned IN (0,1)),
  kept         INTEGER NOT NULL DEFAULT 0 CHECK (kept   IN (0,1)),
  deleted_at   INTEGER
);

CREATE TABLE items (
  id           INTEGER PRIMARY KEY,
  buffer_id    INTEGER NOT NULL REFERENCES buffers(id) ON DELETE CASCADE,
  position     INTEGER NOT NULL,
  type         TEXT    NOT NULL CHECK (type IN ('text','image')),
  created_at   INTEGER NOT NULL,
  text         TEXT,
  blob_hash    TEXT,
  source_name  TEXT,
  width        INTEGER NOT NULL DEFAULT 0,
  height       INTEGER NOT NULL DEFAULT 0,
  byte_size    INTEGER NOT NULL DEFAULT 0,

  -- A row must actually be the type it claims to be.
  CHECK ((type = 'text'  AND text IS NOT NULL AND blob_hash IS NULL)
      OR (type = 'image' AND blob_hash IS NOT NULL AND text IS NULL))
);

CREATE INDEX idx_items_buffer   ON items(buffer_id, position);
CREATE INDEX idx_items_blob     ON items(blob_hash) WHERE blob_hash IS NOT NULL;
CREATE INDEX idx_buffers_recent ON buffers(deleted_at, pinned, modified_at DESC);
CREATE INDEX idx_buffers_trash  ON buffers(deleted_at) WHERE deleted_at IS NOT NULL;

CREATE TABLE napkin_meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- Invariant 1, enforced below the application layer (SPEC.md §5).
--
-- No legitimate code path hard-deletes a kept buffer: confirming the deletion
-- of a kept buffer *releases the keep* as part of moving it to trash, so by the
-- time a row can be purged it always has kept = 0. This trigger therefore
-- functions as a standing assertion — if it ever fires, a service-layer bug has
-- tried to destroy data the user asked Napkin to hold on to.
--
-- The escape hatch exists so a future "delete permanently, I mean it" path has
-- somewhere to go. It is transaction-scoped: Transaction rolls back on any
-- throw, which unsets the flag along with everything else.
CREATE TRIGGER guard_kept_delete BEFORE DELETE ON buffers
WHEN OLD.kept = 1
 AND COALESCE((SELECT value FROM napkin_meta WHERE key = 'allow_kept_delete'), '0') <> '1'
BEGIN
  SELECT RAISE(ABORT, 'refusing to delete a kept buffer');
END;
)SQL";

// --- v2: remember an image's media type --------------------------------------
// Images are stored byte-for-byte as they arrived rather than transcoded to
// PNG: re-encoding a JPEG photo would inflate it several times over and add
// generation loss for nothing. That means the blob's type has to be recorded.
constexpr const char* kV2 = R"SQL(
ALTER TABLE items ADD COLUMN mime TEXT;
UPDATE items SET mime = 'image/png' WHERE type = 'image' AND mime IS NULL;
)SQL";

// --- v3: remember whether an image is animated -------------------------------
// Checked once at import rather than by reopening the file every time a card
// repaints, which at 12 visible cards would mean 12 file opens per frame.
constexpr const char* kV3 = R"SQL(
ALTER TABLE items ADD COLUMN animated INTEGER NOT NULL DEFAULT 0;
)SQL";

// --- v4: items carry their own modified time ---------------------------------
// The canvas shows the newest thing first, and "newest" has to mean edited as
// well as added — otherwise amending an old note leaves it buried. Backfilled
// from created_at so existing rows keep a sensible order.
constexpr const char* kV4 = R"SQL(
ALTER TABLE items ADD COLUMN modified_at INTEGER NOT NULL DEFAULT 0;
UPDATE items SET modified_at = created_at WHERE modified_at = 0;
CREATE INDEX idx_items_recent ON items(buffer_id, modified_at DESC);
)SQL";

// --- v5: full-text search ----------------------------------------------------
// An external-content table: FTS5 stores only the index and reads the columns
// back from `items`, so a pasted log is not held twice. The triggers are the
// price of that — an external-content index does not maintain itself.
//
// `source_name` is indexed alongside the text because searching for a filename
// is the same act as searching for a word, and a user does not care which
// column their memory of it lives in.
constexpr const char* kV5 = R"SQL(
CREATE VIRTUAL TABLE items_fts USING fts5(
  text,
  source_name,
  content = 'items',
  content_rowid = 'id',
  tokenize = "unicode61 remove_diacritics 2"
);

INSERT INTO items_fts(rowid, text, source_name)
  SELECT id, COALESCE(text, ''), COALESCE(source_name, '') FROM items;

CREATE TRIGGER items_fts_insert AFTER INSERT ON items BEGIN
  INSERT INTO items_fts(rowid, text, source_name)
    VALUES (new.id, COALESCE(new.text, ''), COALESCE(new.source_name, ''));
END;

CREATE TRIGGER items_fts_delete AFTER DELETE ON items BEGIN
  INSERT INTO items_fts(items_fts, rowid, text, source_name)
    VALUES ('delete', old.id, COALESCE(old.text, ''), COALESCE(old.source_name, ''));
END;

CREATE TRIGGER items_fts_update AFTER UPDATE ON items BEGIN
  INSERT INTO items_fts(items_fts, rowid, text, source_name)
    VALUES ('delete', old.id, COALESCE(old.text, ''), COALESCE(old.source_name, ''));
  INSERT INTO items_fts(rowid, text, source_name)
    VALUES (new.id, COALESCE(new.text, ''), COALESCE(new.source_name, ''));
END;
)SQL";

// --- v6: trashed items remember their napkin ----------------------------------
// Deleting items moves them into a napkin of their own in the trash. Restoring
// that napkin used to leave the items there, as a separate napkin, while the
// one they came from still said "2 items" (second usability test). The holder
// now records where its items belong, and restoring puts them back if that
// napkin is still around. No foreign key: the original may be purged first,
// and then the holder simply restores as a napkin of its own.
constexpr const char* kV6 = R"SQL(
ALTER TABLE buffers ADD COLUMN restores_to INTEGER;
)SQL";

// --- v7: an optional name ------------------------------------------------------
// A napkin is titled by what was first put on it, and still is. A test user
// asked to be able to name one (2026-10-06); NULL means "as before".
constexpr const char* kV7 = R"SQL(
ALTER TABLE buffers ADD COLUMN name TEXT;
)SQL";

struct Migration {
    int version;
    const char* sql;
};

constexpr std::array kMigrations{
    Migration{1, kV1},
    Migration{2, kV2},
    Migration{3, kV3},
    Migration{4, kV4},
    Migration{5, kV5},
    Migration{6, kV6},
    Migration{7, kV7},
};

}  // namespace

void migrate(Database& db, int upTo)
{
    const int from = db.userVersion();
    if (from > kSchemaVersion)
        throw DbError(QString("database schema is version %1, but this build only understands %2. "
                              "Napkin will not downgrade it.")
                          .arg(from).arg(kSchemaVersion));

    for (const auto& m : kMigrations) {
        if (m.version <= from || m.version > upTo) continue;
        Transaction tx(db);
        db.exec(m.sql);
        db.setUserVersion(m.version);
        tx.commit();
    }
}

}  // namespace napkin
