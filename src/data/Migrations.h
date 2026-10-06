#pragma once

namespace napkin {

class Database;

// Forward-only, tracked by PRAGMA user_version. Never edit a migration that has
// shipped; add another one. Each runs inside its own transaction.
inline constexpr int kSchemaVersion = 7;

void migrate(Database& db);

}  // namespace napkin
