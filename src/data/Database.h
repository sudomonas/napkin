#pragma once
#include "DbError.h"
#include <sqlite3.h>
#include <QString>

namespace napkin {

// Owns the single sqlite3 connection. Napkin is single-instance (see
// app/SingleInstance) so one connection is the whole story; there is no pool
// and no threading story to get wrong.
class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Opens (creating if needed), applies pragmas, and runs migrations.
    // Pass ":memory:" for tests.
    // Migrates to the current schema; `schema` stops earlier, for tests only.
    void open(const QString& path, int schema = -1);
    void close();

    bool isOpen() const { return db_ != nullptr; }
    sqlite3* handle() const { return db_; }

    void exec(const char* sql);
    qint64 lastInsertId() const;
    int changes() const;

    int userVersion();
    void setUserVersion(int v);

    [[noreturn]] void fail(const QString& context) const;

private:
    sqlite3* db_ = nullptr;
};

// Scoped transaction. Rolls back unless commit() is called, which is what makes
// the allow_kept_delete escape hatch in Migrations safe: if anything throws
// between raising and clearing the flag, the rollback unsets it too.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database& db_;
    bool active_ = true;
};

}  // namespace napkin
