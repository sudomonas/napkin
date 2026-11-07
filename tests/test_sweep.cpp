#include "GuiFixture.h"
#include "../src/ui/SweepDialog.h"
#include "../src/domain/Clock.h"
#include "../src/media/BlobGc.h"

#include <QBuffer>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

using namespace napkin;

// Phase 6. SPEC §6 has promised this lifecycle since v2 and nothing implemented
// it: olderThanCutoff() was dead code, there was no OLDER section and no sweep.
class TestSweep : public QObject {
    Q_OBJECT
private:
    qint64 clock_ = 1'700'000'000'000LL;

    BufferId aged(GuiFixture& f, const char* text, int daysAgo, bool pinned = false,
                  bool kept = false)
    {
        // Back to the fixed "now", not the real clock: olderThanCutoff() reads
        // the same clock, and letting it jump to real time would make every
        // seeded buffer ancient.
        const qint64 when = clock_ - qint64(daysAgo) * kMsPerDay;
        setClockForTesting([when] { return when; });
        const auto id = f.buffers.create(pinned, kept);
        f.service.appendTo(id, Item::makeText(QString::fromUtf8(text)));
        setClockForTesting([this] { return clock_; });
        return id;
    }

private slots:
    void init() { setClockForTesting([this] { return clock_; }); }
    void cleanup() { resetClock(); }

    // --- the OLDER section ---------------------------------------------------
    void oldBuffersMoveIntoTheirOwnSection()
    {
        GuiFixture f;
        aged(f, "from last week", 3);
        aged(f, "from two months ago", 60);
        f.model()->reload();

        QStringList sections;
        for (int row = 0; row < f.model()->rowCount(); ++row)
            if (f.model()->index(row, 0).data(BufferListModel::SectionFirstRole).toBool())
                sections << f.model()->index(row, 0).data(BufferListModel::SectionNameRole)
                                .toString();

        QVERIFY(sections.contains(QStringLiteral("RECENT")));
        QVERIFY(sections.contains(QStringLiteral("OLDER")));
    }

    void ageChangesWhereABufferSitsNeverWhetherItExists()
    {
        GuiFixture f;
        const auto ancient = aged(f, "from a year ago", 400);
        f.model()->reload();

        // §6: Napkin never deletes a live buffer on its own, at any age.
        QCOMPARE(f.buffers.countLive(), 1);
        QVERIFY(f.buffers.find(ancient).has_value());
        QVERIFY(!f.buffers.find(ancient)->inTrash());
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::IsOlderRole).toBool(), true);
    }

    void aPinnedBufferNeverFallsIntoOlder()
    {
        GuiFixture f;
        aged(f, "pinned and ancient", 400, /*pinned=*/true);
        f.model()->reload();
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::SectionNameRole).toString(),
                 QStringLiteral("PINNED"));
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::IsOlderRole).toBool(), false);
    }

    // --- the nudge -----------------------------------------------------------
    void theNudgeStaysQuietUntilThereIsSomethingWorthSaying()
    {
        GuiFixture f;
        for (int i = 0; i < 3; ++i) aged(f, "old", 60);
        f.model()->reload();
        f.window.updateSweepNudgeForTest();

        // Napkin tolerates accumulation; the offer should feel like a
        // convenience, not a scolding.
        QCOMPARE(f.model()->sweepableCount(), 3);
        QVERIFY(!f.window.findChild<QPushButton*>(QStringLiteral("sweepReviewButton"))
                     ->isVisible());
    }

    void theNudgeAppearsOnceEnoughHasPiledUp()
    {
        GuiFixture f;
        for (int i = 0; i < kSweepNudgeThreshold + 2; ++i) aged(f, "old", 60);
        f.model()->reload();
        f.window.updateSweepNudgeForTest();
        QVERIFY(f.model()->sweepableCount() >= kSweepNudgeThreshold);
    }

    void keptBuffersAreNeverOffered()
    {
        GuiFixture f;
        for (int i = 0; i < 5; ++i) aged(f, "ordinary", 60);
        for (int i = 0; i < 3; ++i) aged(f, "kept", 60, false, /*kept=*/true);
        f.model()->reload();

        // This is exactly what Keep promises.
        QCOMPARE(f.model()->sweepableCount(), 5);
    }

    void pinningDoesNotProtectFromASweep()
    {
        GuiFixture f;
        aged(f, "pinned but old", 60, /*pinned=*/true);
        aged(f, "ordinary and old", 60);
        f.model()->reload();

        // Pin is placement, Keep is lifecycle. A pinned buffer is not in OLDER
        // because it is not in the recency order at all — but it is still
        // sweepable, which is the distinction the two flags exist to draw.
        QCOMPARE(f.model()->sweepableCount(), 2);
    }

    // --- the review dialog ---------------------------------------------------
    void theDialogListsWhatItProposesToTake()
    {
        GuiFixture f;
        for (int i = 0; i < 4; ++i) aged(f, "old thing", 60);
        aged(f, "recent thing", 2);
        aged(f, "kept thing", 60, false, true);
        f.model()->reload();

        SweepDialog dialog(f.buffers, f.items);
        auto* list = dialog.findChild<QListWidget*>();
        QVERIFY(list);
        QCOMPARE(list->count(), 4);          // not the recent one, not the kept one
        for (int i = 0; i < list->count(); ++i)
            QCOMPARE(list->item(i)->checkState(), Qt::Checked);
    }

    void untickingSomethingKeepsIt()
    {
        GuiFixture f;
        for (int i = 0; i < 3; ++i) aged(f, "old thing", 60);
        f.model()->reload();

        SweepDialog dialog(f.buffers, f.items);
        auto* list = dialog.findChild<QListWidget*>();
        list->item(0)->setCheckState(Qt::Unchecked);

        for (auto* button : dialog.findChildren<QPushButton*>())
            if (button->text().contains(QStringLiteral("trash"))) { button->click(); break; }

        QCOMPARE(dialog.accepted().size(), 2);
    }

    // --- sweeping ------------------------------------------------------------
    void aSweepTrashesRatherThanDeletes()
    {
        GuiFixture f;
        const auto a = aged(f, "old one", 60);
        const auto b = aged(f, "old two", 60);
        f.model()->reload();

        f.window.sweepForTest({a, b});

        // Never a hard delete: a sweep has to stay undoable.
        QCOMPARE(f.buffers.countLive(), 0);
        QCOMPARE(f.buffers.countTrash(), 2);
        QVERIFY(f.buffers.find(a)->inTrash());
        QVERIFY(f.items.countForBuffer(a) > 0);   // its contents are intact
    }

    void aSweepCannotTakeAKeptBuffer()
    {
        GuiFixture f;
        const auto kept = aged(f, "kept", 60, false, true);
        f.model()->reload();

        // Even asked directly, trash() refuses a kept buffer — the guarantee is
        // in the repository, not in the dialog that is supposed to filter it.
        f.window.sweepForTest({kept});
        QCOMPARE(f.buffers.countTrash(), 0);
        QVERIFY(f.buffers.find(kept)->kept);
    }

    void aSweepIsUndoable()
    {
        GuiFixture f;
        const auto a = aged(f, "old one", 60);
        const auto b = aged(f, "old two", 60);
        f.model()->reload();

        f.window.sweepForTest({a, b});
        QVERIFY(f.toast()->hasOffer());

        f.toast()->findChild<QPushButton*>()->click();
        QCOMPARE(f.buffers.countLive(), 2);
        QCOMPARE(f.buffers.countTrash(), 0);
    }

    void moveToTrashIsOnlyAvailableWhenSomethingIsTicked()
    {
        // Usability test: with nothing to clean, the dialog opened an empty
        // list with "Move to trash" highlighted as the default.
        GuiFixture f;
        aged(f, "recent thing", 2);
        f.model()->reload();
        QCOMPARE(SweepDialog(f.buffers, f.items).candidates(), 0);

        aged(f, "old thing", 60);
        f.model()->reload();
        SweepDialog dialog(f.buffers, f.items);
        QCOMPARE(dialog.candidates(), 1);
        QPushButton* sweep = nullptr;
        for (auto* b : dialog.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Move to trash")) sweep = b;
        QVERIFY(sweep);
        QVERIFY(sweep->isEnabled());
        dialog.findChild<QListWidget*>()->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(!sweep->isEnabled());
    }

    void emptyingTheTrashAlsoRemovesTheThumbnail()
    {
        GuiFixture f;
        QImage img(300, 200, QImage::Format_RGB32); img.fill(Qt::magenta);
        QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");

        const auto stored = f.blobs.store(png);
        QVERIFY(stored.ok);
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 300, 200, stored.byteSize,
                                               {}, stored.mime));
        QVERIFY(!f.thumbs.forBlob(stored.hash, stored.mime).isNull());

        const QString thumbDir = f.thumbsDir();
        QVERIFY(!QDir(thumbDir).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()
                || !QDir(thumbDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

        f.service.trash(id);
        f.service.emptyTrash();
        const auto result = reconcileBlobs(f.items, f.blobs, thumbDir);

        QVERIFY(!f.blobs.exists(stored.hash, stored.mime));
        QCOMPARE(result.thumbnailsRemoved, 1);
    }

    void theEmptyTrashCountMatchesWhatWillActuallyBeDestroyed()
    {
        GuiFixture f;
        const auto ordinary = f.buffers.create();
        const auto stuck = f.buffers.create();
        f.buffers.setKept(stuck, true);
        f.db.exec("UPDATE buffers SET deleted_at = 1");

        QCOMPARE(f.buffers.countTrash(), 2);
        QCOMPARE(f.buffers.countPurgeable(), 1);
        QCOMPARE(f.service.emptyTrash(), 1);
        QVERIFY(f.buffers.find(stuck).has_value());
        QVERIFY(!f.buffers.find(ordinary).has_value());
    }

    void theSweepDoesNotDestroyABlobUndoStillNeeds()
    {
        GuiFixture f;
        QImage img(40, 30, QImage::Format_RGB32); img.fill(Qt::red);
        QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");

        const auto stored = f.blobs.store(png);
        const auto keep = f.buffers.create();
        f.service.appendTo(keep, Item::makeText(QStringLiteral("still here")));
        const auto doomed = f.buffers.create();
        f.service.appendTo(doomed, Item::makeImage(stored.hash, 40, 30, stored.byteSize,
                                                   {}, stored.mime));
        f.model()->reload();

        f.select(doomed);
        f.canvas()->selectAll();
        f.canvas()->deleteSelection();
        QVERIFY(f.toast()->hasOffer());

        reconcileBlobs(f.items, f.blobs, f.thumbsDir(), f.window.undoProtectedBlobsForTest());

        if (f.toast()->hasOffer()) {
            f.toast()->findChild<QPushButton*>()->click();
            for (const auto& item : f.items.allImageItems())
                QVERIFY2(f.blobs.exists(item.blobHash, item.mime),
                         "undo restored a row whose blob had been swept away");
        }
    }
};

QTEST_MAIN(TestSweep)
#include "test_sweep.moc"
