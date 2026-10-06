#include "Database.h"
#include "Migrations.h"
#include "Statement.h"

namespace napkin {

Database::~Database() { close(); }

void Database::fail(const QString& context) const
{
    const QString msg = db_ ? QString::fromUtf8(sqlite3_errmsg(db_)) : QStringLiteral("no connection");
    throw DbError(context + ": " + msg);
}

void Database::open(const QString& path, int schema)
{
    close();
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        const QString msg = db_ ? QString::fromUtf8(sqlite3_errmsg(db_))
                                : QStringLiteral("out of memory");
        sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("cannot open database: " + msg);
    }

    // SPEC.md §8. WAL + synchronous=NORMAL loses data only on OS or power loss,
    // never on application crash or kill -9 — verified in Phase 0.
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=NORMAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA busy_timeout=3000;");
    // Without this, INSERT OR REPLACE silently deletes the replaced row WITHOUT
    // firing guard_kept_delete — a hole straight through invariant 1.
    exec("PRAGMA recursive_triggers=ON;");

    if (schema < 0) migrate(*this);
    else            migrate(*this, schema);
}

void Database::close()
{
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::exec(const char* sql)
{
    char* msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        const QString m = QString::fromUtf8(msg ? msg : "unknown error");
        sqlite3_free(msg);
        throw DbError(QString("exec failed: %1 [%2]").arg(m, QString::fromUtf8(sql).left(120)));
    }
}

qint64 Database::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }
int Database::changes() const { return sqlite3_changes(db_); }

int Database::userVersion()
{
    Statement s(*this, "PRAGMA user_version;");
    return s.step() ? s.columnInt(0) : 0;
}

void Database::setUserVersion(int v)
{
    // PRAGMA will not accept a bound parameter.
    exec(QString("PRAGMA user_version=%1;").arg(v).toUtf8().constData());
}

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN IMMEDIATE;"); }

Transaction::~Transaction()
{
    if (active_) {
        try { db_.exec("ROLLBACK;"); } catch (...) { /* destructor must not throw */ }
    }
}

void Transaction::commit()
{
    db_.exec("COMMIT;");
    active_ = false;
}

}  // namespace napkin
