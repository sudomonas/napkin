#pragma once

namespace napkin {

class Database;

// Forward-only, tracked by PRAGMA user_version. Never edit a migration that has
// shipped; add another one. Each runs inside its own transaction.
inline constexpr int kSchemaVersion = 7;

// Up to `upTo`, which is the current schema except in tests that need a database
// as an older build left it: faking one by dropping a column needs SQLite 3.35,
// and CI tests against 3.31.
void migrate(Database& db, int upTo = kSchemaVersion);

}  // namespace napkin
