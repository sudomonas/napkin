#include "TestDb.h"
#include <QSettings>
#include <QtTest>

class TestBuffers : public QObject {
    Q_OBJECT
private slots:
    // Its own settings, so a retention left in a shared store cannot change
    // what the purge tests below expect.
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("napkin-test-buffers"));
        QCoreApplication::setApplicationName(QStringLiteral("napkin-test-buffers"));
        QSettings().clear();
    }
    void cleanupTestCase() { QSettings().clear(); }

    void createAndFind()
    {
        TestDb t;
        const auto id = t.buffers.create();
        const auto b = t.buffers.find(id);
        QVERIFY(b.has_value());
        QCOMPARE(b->createdAt, t.clock);
        QCOMPARE(b->modifiedAt, t.clock);
        QVERIFY(!b->pinned);
        QVERIFY(!b->kept);
        QVERIFY(!b->inTrash());
    }

    void pinAndKeepAreIndependent()
    {
        TestDb t;
        const auto id = t.buffers.create();

        t.service.setPinned(id, true);
        QVERIFY(t.buffers.find(id)->pinned);
        QVERIFY(!t.buffers.find(id)->kept);   // pinning does not protect

        t.service.setKept(id, true);
        t.service.setPinned(id, false);
        QVERIFY(!t.buffers.find(id)->pinned);
        QVERIFY(t.buffers.find(id)->kept);    // unpinning does not release
    }

    void pinDoesNotBumpModifiedAt()
    {
        TestDb t;
        const auto id = t.buffers.create();
        const auto before = t.buffers.find(id)->modifiedAt;

        t.advanceDays(1);
        t.service.setPinned(id, true);
        t.service.setKept(id, true);

        // Flipping metadata must not reshuffle the list (SPEC.md §7).
        QCOMPARE(t.buffers.find(id)->modifiedAt, before);
    }

    void pinnedSortAboveEverythingElse()
    {
        TestDb t;
        const auto old = t.buffers.create();
        t.advanceDays(1);
        const auto recent = t.buffers.create();
        t.service.setPinned(old, true);

        const auto list = t.buffers.listLive(10);
        QCOMPARE(list.size(), size_t(2));
        QCOMPARE(list[0].id, old);      // pinned, despite being older
        QCOMPARE(list[1].id, recent);
    }

    void listIsWindowed()
    {
        TestDb t;
        for (int i = 0; i < 25; ++i) { t.buffers.create(); t.advanceDays(1); }
        QCOMPARE(t.buffers.listLive(10).size(), size_t(10));
        QCOMPARE(t.buffers.listLive(10, 20).size(), size_t(5));
    }

    void trashHidesFromLiveListAndRestoreBringsItBack()
    {
        TestDb t;
        const auto id = t.buffers.create();

        QVERIFY(t.service.trash(id));
        QCOMPARE(t.buffers.countLive(), 0);
        QCOMPARE(t.buffers.countTrash(), 1);
        QCOMPARE(t.buffers.listTrash().size(), size_t(1));

        t.service.restore(id);
        QCOMPARE(t.buffers.countLive(), 1);
        QCOMPARE(t.buffers.countTrash(), 0);
    }

    void trashIsSoftSoUndoCanAlwaysWork()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.service.appendTo(id, napkin::Item::makeText(QStringLiteral("important")));

        t.service.trash(id);
        // Items must survive the soft delete or undo would be a lie.
        QCOMPARE(t.items.countForBuffer(id), 1);
        QCOMPARE(t.items.listForBuffer(id)[0].text, QStringLiteral("important"));
    }

    void purgeOnlyTakesExpiredTrash()
    {
        TestDb t;
        const auto liveOne = t.buffers.create();
        const auto trashed = t.buffers.create();
        t.service.trash(trashed);

        t.advanceDays(napkin::kTrashRetentionDays - 1);
        QCOMPARE(t.service.purgeExpiredTrash(), 0);  // not yet

        t.advanceDays(2);
        QCOMPARE(t.service.purgeExpiredTrash(), 1);
        QVERIFY(t.buffers.find(liveOne).has_value());  // a live buffer never expires
        QVERIFY(!t.buffers.find(trashed).has_value());
    }

    // A retention the dialog could not have written is not believed. Read as a
    // bare toInt(), each of these was zero or less and the next start deleted
    // the entire trash for good (audit, 2026-10-09).
    void anUnbelievableRetentionFallsBackToTheDefault()
    {
        const QString key = QStringLiteral("lifecycle/trashRetentionDays");
        for (const QVariant& bad : {QVariant(QStringLiteral("abc")), QVariant(QStringLiteral("30 days")),
                                    QVariant(0), QVariant(-5), QVariant(99999)}) {
            QSettings().setValue(key, bad);
            QCOMPARE(napkin::BufferService::trashRetentionDays(), napkin::kTrashRetentionDays);

            TestDb t;
            const auto trashed = t.buffers.create();
            t.service.trash(trashed);
            t.advanceDays(1);
            QCOMPARE(t.service.purgeExpiredTrash(), 0);
            QVERIFY(t.buffers.find(trashed).has_value());
        }
        QSettings().setValue(key, 7);   // a real choice is still honoured
        QCOMPARE(napkin::BufferService::trashRetentionDays(), 7);
        QSettings().remove(key);
    }

    void purgeCascadesToItems()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.service.appendTo(id, napkin::Item::makeText(QStringLiteral("gone")));
        t.service.trash(id);
        t.advanceDays(napkin::kTrashRetentionDays + 1);
        t.service.purgeExpiredTrash();

        QCOMPARE(t.items.countForBuffer(id), 0);
    }

    void liveBuffersNeverExpireNoMatterHowOld()
    {
        TestDb t;
        const auto id = t.buffers.create();
        t.advanceDays(3650);  // ten years
        t.service.purgeExpiredTrash();

        // SPEC.md §6: age changes visibility, never existence.
        QVERIFY(t.buffers.find(id).has_value());
        QVERIFY(t.buffers.find(id)->modifiedAt < napkin::BufferService::olderThanCutoff());
    }
};

QTEST_APPLESS_MAIN(TestBuffers)
#include "test_buffers.moc"
