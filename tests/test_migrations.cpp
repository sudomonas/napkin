#include "TestDb.h"
#include <QtTest>

class TestMigrations : public QObject {
    Q_OBJECT
private slots:
    void appliesSchemaVersion()
    {
        TestDb t;
        QCOMPARE(t.db.userVersion(), napkin::kSchemaVersion);
    }

    void isIdempotent()
    {
        TestDb t;
        napkin::migrate(t.db);  // running again must be a no-op
        QCOMPARE(t.db.userVersion(), napkin::kSchemaVersion);
        QCOMPARE(t.buffers.countLive(), 0);
    }

    // v7: a database from before names keeps every napkin, unnamed.
    void aV6DatabaseGainsNamesWithEveryNapkinUnnamed()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.db.exec("ALTER TABLE buffers DROP COLUMN name;");
        t.db.setUserVersion(6);
        napkin::migrate(t.db);
        QCOMPARE(t.db.userVersion(), napkin::kSchemaVersion);
        const auto found = t.buffers.find(id);
        QVERIFY(found.has_value());
        QVERIFY(found->name.isEmpty());
        t.buffers.setName(id, QStringLiteral("now named"));
        QCOMPARE(t.buffers.find(id)->name, QStringLiteral("now named"));
        t.buffers.setName(id, QStringLiteral("   "));   // blank is "no name", stored as NULL
        QVERIFY(t.buffers.find(id)->name.isEmpty());
    }

    void refusesToDowngrade()
    {
        TestDb t;
        t.db.setUserVersion(napkin::kSchemaVersion + 5);
        QVERIFY_THROWS_EXCEPTION(napkin::DbError, napkin::migrate(t.db));
    }

    void enforcesForeignKeys()
    {
        TestDb t;
        QVERIFY_THROWS_EXCEPTION(napkin::DbError,
            t.items.append(9999, napkin::Item::makeText(QStringLiteral("orphan"))));
    }

    void rejectsMistypedItemRows()
    {
        TestDb t;
        const auto id = t.buffers.create();
        // A row claiming to be text while carrying a blob violates the CHECK.
        QVERIFY_THROWS_EXCEPTION(napkin::DbError,
            t.db.exec(QString("INSERT INTO items(buffer_id,position,type,created_at,text,blob_hash)"
                              " VALUES(%1,0,'text',1,'hi','deadbeef')").arg(id).toUtf8().constData()));
    }
};

QTEST_APPLESS_MAIN(TestMigrations)
#include "test_migrations.moc"
