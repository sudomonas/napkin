#include "TestDb.h"
#include <QtTest>

// Invariant 1 (SPEC.md §2): a kept buffer is never deleted by any automatic
// process, and the guarantee lives below the application layer.
class TestInvariants : public QObject {
    Q_OBJECT
private slots:
    void rawSqlDeleteOfKeptBufferAborts()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.buffers.setKept(id, true);

        // Acceptance criterion 17: this is what a service-layer bug would do.
        QVERIFY_THROWS_EXCEPTION(napkin::DbError,
            t.db.exec("DELETE FROM buffers WHERE kept = 1"));

        QVERIFY(t.buffers.find(id).has_value());
    }

    void trashRefusesKeptBufferWithoutConfirmation()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.buffers.setKept(id, true);

        QCOMPARE(t.service.trash(id), false);
        QVERIFY(!t.buffers.find(id)->inTrash());
    }

    void confirmedTrashReleasesTheKeep()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.buffers.setKept(id, true);

        t.service.trashConfirmed(id);
        const auto b = t.buffers.find(id);
        QVERIFY(b->inTrash());
        QCOMPARE(b->kept, false);  // confirming withdraws the request to hold it
    }

    void purgeNeverTakesAKeptBuffer()
    {
        TestDb t;
        const auto kept = t.buffers.create();
        const auto ordinary = t.buffers.create();
        t.buffers.setKept(kept, true);

        // Force both into trash, kept flag intact, simulating a service bug.
        t.db.exec(QString("UPDATE buffers SET deleted_at = %1").arg(t.clock).toUtf8().constData());
        t.advanceDays(napkin::kTrashRetentionDays + 1);

        QCOMPARE(t.service.purgeExpiredTrash(), 1);
        QVERIFY(t.buffers.find(kept).has_value());       // survived
        QVERIFY(!t.buffers.find(ordinary).has_value());  // purged
    }

    void escapeHatchIsTransactionScoped()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.buffers.setKept(id, true);

        t.buffers.hardDeleteEvenIfKept(id);
        QVERIFY(!t.buffers.find(id).has_value());

        // The flag must not survive the transaction that raised it.
        const auto second = t.buffers.create();
        t.buffers.setKept(second, true);
        QVERIFY_THROWS_EXCEPTION(napkin::DbError, t.db.exec("DELETE FROM buffers WHERE kept = 1"));
        QVERIFY(t.buffers.find(second).has_value());
    }

    void replaceCannotSmuggleAKeptBufferOut()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.buffers.setKept(id, true);

        QVERIFY_THROWS_EXCEPTION(napkin::DbError,
            t.db.exec(QString("INSERT OR REPLACE INTO buffers(id, created_at, modified_at, kept)"
                              " VALUES(%1, 1, 1, 0)").arg(id).toUtf8().constData()));
        QVERIFY(t.buffers.find(id)->kept);
    }
};

QTEST_APPLESS_MAIN(TestInvariants)
#include "test_invariants.moc"
