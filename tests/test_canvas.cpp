#include "../src/ui/BoardLayout.h"
#include "GuiFixture.h"
#include "../src/domain/Clock.h"
#include "../src/media/BlobGc.h"
#include "../src/ui/Tokens.h"
#include "../src/ui/TourDialog.h"
#include "../src/ui/CardFooter.h"

#include <QApplication>
#include <QScrollBar>
#include <QBuffer>
#include <QClipboard>
#include <QMimeData>
#include <QPushButton>
#include <QAbstractButton>
#include <QTimer>
#include <QMessageBox>
#include "../src/ui/MessageDialog.h"
#include <QLineEdit>
#include <QPushButton>
#include "../src/ui/WelcomeView.h"

#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QSettings>
#include <QSplitter>
#include <QtTest>

using namespace napkin;

namespace {
QByteArray png(int w, int h, QColor c = Qt::red)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(c);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

QString toastText(GuiFixture& f)
{
    QString said;
    for (auto* l : f.toast()->findChildren<QLabel*>())
        if (!l->text().isEmpty()) said = l->text();
    return said;
}
}  // namespace

// The two-pane layout: a buffer list on the left, its items on the right, where
// items are selectable objects you can copy, cut and delete.
class TestCanvas : public QObject {
    Q_OBJECT
private:
    BufferId seedMixed(GuiFixture& f)
    {
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("Investigate this bug")));
        for (int i = 0; i < 2; ++i) {
            const auto stored = f.blobs.store(png(80 + i, 60, QColor::fromHsv(i * 90, 200, 220)));
            f.service.appendTo(id, Item::makeImage(stored.hash, stored.size.width(),
                                                   stored.size.height(), stored.byteSize,
                                                   QStringLiteral("shot%1.png").arg(i),
                                                   stored.mime));
        }
        f.service.appendTo(id, Item::makeText(QStringLiteral("and a closing note")));
        f.model()->reload();
        return id;
    }

    BufferId seedMixed(GuiFixture& f, int images)
    {
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("Investigate this bug")));
        for (int i = 0; i < images; ++i) {
            const auto stored = f.blobs.store(png(60 + i, 40 + i, QColor::fromHsv(i * 40, 200, 220)));
            Q_ASSERT(stored.ok);
            f.service.appendTo(id, Item::makeImage(stored.hash, stored.size.width(),
                                                   stored.size.height(), stored.byteSize,
                                                   QStringLiteral("shot%1.png").arg(i),
                                                   stored.mime));
        }
        f.model()->reload();
        return id;
    }

private slots:
    // Its own settings, by name. Without an organization QSettings fell back
    // to a shared "Unknown Organization" on Linux, and on Windows the registry
    // write did not stick at all — the tour's "seen" was lost (CI, 2026-10-06).
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("napkin-test-canvas"));
        QCoreApplication::setApplicationName(QStringLiteral("napkin-test-canvas"));
        QSettings().clear();
    }
    void cleanupTestCase() { QSettings().clear(); }

    void selectingABufferFillsTheCanvasWithEveryItem()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        QCOMPARE(f.canvas()->findChildren<ImageItemCard*>().size(), 2);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 2);  // no composer
    }

    void selectingNothingShowsAPlaceholder()
    {
        GuiFixture f;
        f.seed("something");
        f.select(f.buffers.listLive(10).front().id);
        QVERIFY(!f.canvas()->findChildren<TextItemCard*>().isEmpty());

        f.view()->setCurrentIndex({});
        f.window.selectBuffer(-1);
        QVERIFY(f.canvas()->findChildren<TextItemCard*>().isEmpty());
    }

    // --- selection -----------------------------------------------------------
    void clickingAnImageSelectsItAsAnObject()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);

        QCOMPARE(f.canvas()->selection().size(), 1);
        QCOMPARE(f.canvas()->selection().first(), image->itemId());
        QVERIFY(image->isSelected());
    }

    void ctrlClickAddsToTheSelection()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        const auto images = f.canvas()->findChildren<ImageItemCard*>();
        QTest::mouseClick(images[0], Qt::LeftButton);
        QTest::mouseClick(images[1], Qt::LeftButton, Qt::ControlModifier);

        QCOMPARE(f.canvas()->selection().size(), 2);
    }

    void selectionIsReturnedInDocumentOrder()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        const auto images = f.canvas()->findChildren<ImageItemCard*>();
        QTest::mouseClick(images[1], Qt::LeftButton);                      // later first
        QTest::mouseClick(images[0], Qt::LeftButton, Qt::ControlModifier);

        const auto order = f.canvas()->selection();
        QCOMPARE(order.size(), 2);
        QCOMPARE(order[0], images[0]->itemId());   // still in document order
    }

    void selectAllSkipsTheUnwrittenComposer()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        f.canvas()->selectAll();

        // 4 real items; the composer has no row and is not an object.
        QCOMPARE(f.canvas()->selection().size(), 4);
    }

    void escapeClearsTheSelection()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        f.canvas()->selectAll();
        QVERIFY(f.canvas()->hasSelection());

        QTest::keyClick(f.canvas(), Qt::Key_Escape);
        QVERIFY(!f.canvas()->hasSelection());
    }

    // --- copy / cut / delete -------------------------------------------------
    void copyingASingleImagePutsAnImageOnTheClipboard()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        QApplication::clipboard()->clear();

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);
        f.canvas()->copySelection();

        const auto* mime = QApplication::clipboard()->mimeData();
        QVERIFY(mime);               // null after a clear() on some platforms
        QVERIFY(mime->hasImage());   // pasteable into anything, not just Napkin
    }

    void copyingAMixedSelectionPutsTextOnTheClipboard()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        f.canvas()->selectAll();
        f.canvas()->copySelection();

        const QString text = QApplication::clipboard()->text();
        QVERIFY(text.contains(QStringLiteral("Investigate this bug")));
        QVERIFY(text.contains(QStringLiteral("shot0.png")));
        QVERIFY(text.contains(QStringLiteral("and a closing note")));
    }

    // --- the one copy that can genuinely fail --------------------------------
    // A lone image whose blob has gone from disk. The copy used to fall through
    // to the text path, put the literal "[image]" on the clipboard, and say
    // "Copied" — a failure reported as success.
    void copyingAnImageWhoseBlobIsGoneSaysSoInsteadOfClaimingSuccess()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        const auto stored = f.blobs.store(png(80, 60, Qt::red));
        QVERIFY(stored.ok);
        f.service.appendTo(id, Item::makeImage(stored.hash, 80, 60, stored.byteSize,
                                               QStringLiteral("gone.png"), stored.mime));
        f.model()->reload();
        f.select(id);

        // Retried: on Windows a file another thread has open cannot be
        // deleted, and the thumbnail worker reads this blob as the napkin opens.
        // Bounded, so a blob held open for good would still fail here. The
        // success is remembered: QTRY evaluates its condition once more after
        // it holds, and a second remove of a removed file is false.
        {
            bool removed = false;
            const QString blob = f.blobs.pathFor(stored.hash, stored.mime);
            QTRY_VERIFY_WITH_TIMEOUT(removed || (removed = QFile::remove(blob)), 5000);
        }
        QApplication::clipboard()->setText(QStringLiteral("untouched"));

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);
        f.canvas()->copySelection();

        // Not the filename, and above all not "[image]" passed off as the picture.
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("untouched"));
        QString said;
        for (auto* l : f.toast()->findChildren<QLabel*>())
            if (!l->text().isEmpty()) said = l->text();
        QVERIFY2(said.contains(QStringLiteral("missing")),
                 qPrintable(QStringLiteral("said: ") + said));
    }

    // Worse for cut: it copied the placeholder text and then deleted the item
    // anyway, so the move lost the thing being moved.
    void cuttingAnImageWhoseBlobIsGoneRemovesNothing()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        const auto stored = f.blobs.store(png(80, 60, Qt::blue));
        f.service.appendTo(id, Item::makeImage(stored.hash, 80, 60, stored.byteSize,
                                               QStringLiteral("gone.png"), stored.mime));
        f.model()->reload();
        f.select(id);
        // Retried: on Windows a file another thread has open cannot be
        // deleted, and the thumbnail worker reads this blob as the napkin opens.
        // Bounded, so a blob held open for good would still fail here. The
        // success is remembered: QTRY evaluates its condition once more after
        // it holds, and a second remove of a removed file is false.
        {
            bool removed = false;
            const QString blob = f.blobs.pathFor(stored.hash, stored.mime);
            QTRY_VERIFY_WITH_TIMEOUT(removed || (removed = QFile::remove(blob)), 5000);
        }

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);
        f.canvas()->cutSelection();

        QCOMPARE(f.items.countForBuffer(id), 1);   // still there
        QVERIFY(!f.buffers.find(id)->inTrash());
    }

    // A cut answers with ONE message, the window's, which carries Undo. It used
    // to announce "Copied" first, on a card that was about to disappear.
    void cuttingDoesNotAlsoAnnounceACopy()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("first")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("second")));
        f.model()->reload();
        f.select(id);

        f.canvas()->selectAll();
        // Watched at the signal, not at the toast: the cut's own message
        // replaces the copy's in the same call stack, so the final toast looks
        // right either way and only the signal shows the second announcement.
        QSignalSpy announcements(f.canvas(), &ItemCanvas::announced);
        f.canvas()->cutSelection();

        for (const auto& args : announcements) {
            const QString said = args.value(0).toString();
            QVERIFY2(!said.contains(QStringLiteral("copied"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("a cut reported a copy: ") + said));
        }
        QVERIFY2(f.toast()->hasOffer(), "a cut must still offer Undo");
    }

    // --- arrows walk the board as it looks -----------------------------------
    // Down used to mean "+1 in document order", which on a masonry board is the
    // card to the RIGHT: with three columns it travelled along the top row,
    // once per column, before it ever went down, and the card directly beneath
    // the cursor could not be reached by any key at all.
    void arrowsMoveByGeometryNotByDocumentOrder()
    {
        GuiFixture f;
        f.window.resize(1800, 900);            // wide enough for three columns
        const auto id = f.buffers.create();
        const char* bodies[] = {"ONE", "TWO\na\nb", "THREE", "FOUR\na\nb\nc", "FIVE",
                                "SIX\na\nb", "SEVEN", "EIGHT", "NINE\na\nb\nc", "TEN"};
        for (const char* body : bodies)
            f.service.appendTo(id, Item::makeText(QString::fromUtf8(body)));
        f.model()->reload();
        f.select(id);

        auto* canvas = f.canvas();
        const auto cards = canvas->findChildren<ItemCard*>();
        QSet<int> columns;
        for (auto* c : cards) columns.insert(c->geometry().x());
        if (columns.size() < 2) QSKIP("the board laid out in one column; nothing to cross");

        auto rectOfSelected = [&]() -> QRect {
            const auto ids = canvas->selection();
            if (ids.isEmpty()) return {};
            for (auto* c : canvas->findChildren<ItemCard*>())
                if (c->itemId() == ids.first()) return c->geometry();
            return {};
        };

        canvas->setCursorTo(0, Qt::NoModifier);
        const QRect start = rectOfSelected();
        QVERIFY(start.isValid());

        QTest::keyClick(canvas, Qt::Key_Down);
        const QRect below = rectOfSelected();
        QVERIFY2(below.isValid(), "Down selected nothing");
        QCOMPARE(below.x(), start.x());                      // same column
        QVERIFY2(below.y() > start.y(), "Down did not move down the column");

        QTest::keyClick(canvas, Qt::Key_Right);
        const QRect across = rectOfSelected();
        QVERIFY2(across.x() > below.x(), "Right did not cross to the next column");

        QTest::keyClick(canvas, Qt::Key_Left);
        QCOMPARE(rectOfSelected().x(), below.x());           // and back again

        QTest::keyClick(canvas, Qt::Key_Up);
        const QRect up = rectOfSelected();
        QCOMPARE(up.x(), start.x());
        QCOMPARE(up.y(), start.y());                         // exactly where we began
    }

    // Deliberately no wrap: falling off the bottom of a column into the top of
    // the next one is the jump across the whole board that makes a spatial walk
    // feel random. Left/Right is how you change column.
    void downAtTheBottomOfAColumnStaysPut()
    {
        GuiFixture f;
        f.window.resize(1800, 900);
        const auto id = f.buffers.create();
        for (int i = 0; i < 9; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1").arg(i)));
        f.model()->reload();
        f.select(id);

        auto* canvas = f.canvas();
        canvas->setCursorTo(0, Qt::NoModifier);
        for (int i = 0; i < 20; ++i) QTest::keyClick(canvas, Qt::Key_Down);
        const int bottom = canvas->cursorIndex();

        // The foot of the FIRST column, not the last card on the board — which
        // is where stepping document order would have ended up.
        QVERIFY2(bottom < 8, qPrintable(QStringLiteral(
                     "Down ran out of its column and into another (index %1)").arg(bottom)));

        QTest::keyClick(canvas, Qt::Key_Down);
        QCOMPARE(canvas->cursorIndex(), bottom);
        QCOMPARE(canvas->selection().size(), 1);
    }

    // --- the board is laid out a piece at a time -------------------------------
    // A 1000-item napkin measured every card before the first one was drawn,
    // twice. Now only the top is measured up front and the rest while idle —
    // which is only acceptable if the finished board is exactly the board a
    // whole-board layout would have made.
    void aLongNapkinOpensWithOnlyItsTopMeasuredAndFinishesTheSame()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 400; ++i)
            f.service.appendTo(id, Item::makeText(
                QStringLiteral("note %1\n%2").arg(i).arg(QString(i % 7 * 30, QLatin1Char('x')))));
        f.model()->reload();
        f.select(id);

        const BoardLayout& board = f.canvas()->boardForTest();
        QVERIFY2(!board.complete(), "the whole board was measured before it was shown");
        QVERIFY(board.placements().size() < 100);
        QVERIFY(!f.canvas()->findChildren<TextItemCard*>().isEmpty());   // the top is there

        QTRY_VERIFY_WITH_TIMEOUT(board.complete(), 5000);

        BoardLayout whole;
        whole.setViewport(f.canvas()->widget()->width());
        whole.setFont(f.canvas()->findChildren<TextItemCard*>().first()->font());
        const auto items = f.items.listForBuffer(id);
        whole.rebuild(items);
        QCOMPARE(board.placements().size(), whole.placements().size());
        for (size_t i = 0; i < whole.placements().size(); ++i)
            QCOMPARE(board.placements()[i].rect, whole.placements()[i].rect);
        QCOMPARE(board.totalHeight(), whole.totalHeight());
        QCOMPARE(f.canvas()->widget()->height(), whole.totalHeight());
    }

    void endReachesTheLastItemBeforeTheBoardHasFinishedMeasuring()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 400; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("line %1").arg(i)));
        f.model()->reload();
        f.select(id);
        auto* canvas = f.canvas();
        QVERIFY(!canvas->boardForTest().complete());

        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_End);
        QCOMPARE(canvas->cursorIndex(), 399);
        // The oldest item, and its card exists because the board scrolled to it.
        const auto items = f.items.listForBuffer(id);
        bool built = false;
        for (auto* card : canvas->findChildren<TextItemCard*>())
            if (card->itemId() == items.back().id && !card->isHidden()) built = true;
        QVERIFY2(built, "End moved the cursor to a card that was never built");
    }

    void scrollingAheadOfTheMeasuringStillShowsCards()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 400; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("row %1").arg(i)));
        f.model()->reload();
        f.select(id);
        auto* canvas = f.canvas();
        QVERIFY(!canvas->boardForTest().complete());

        // Dragging the scrollbar to the far end of an estimated board.
        auto* bar = canvas->verticalScrollBar();
        bar->setValue(bar->maximum());
        const QRect visible(0, bar->value(), canvas->viewport()->width(),
                            canvas->viewport()->height());
        int shown = 0;
        for (auto* card : canvas->findChildren<ItemCard*>())
            if (!card->isHidden() && card->geometry().intersects(visible)) ++shown;
        QVERIFY2(shown > 0, "the bottom of the board was empty while it was being measured");
    }

    // The board is virtualized: only the visible band has card widgets. Every
    // selection verb used to be written against `cards_`, so "select all" meant
    // "select what happens to be on screen" — and Ctrl+A then Ctrl+C copied a
    // fraction of a long napkin while reporting a confident count.
    void selectAllCoversTheWholeNapkinNotOnlyWhatIsOnScreen()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 60; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1 here").arg(i)));
        f.model()->reload();
        f.select(id);

        // If the board is not actually virtualizing, this test proves nothing.
        QVERIFY2(f.canvas()->findChildren<TextItemCard*>().size() < 60,
                 "the board held every card, so the band cannot be the bug");

        f.canvas()->selectAll();
        QCOMPARE(f.canvas()->selection().size(), 60);

        f.canvas()->copySelection();
        const QString copied = QApplication::clipboard()->text();
        for (int i = 0; i < 60; ++i)
            QVERIFY2(copied.contains(QStringLiteral("item %1 here").arg(i)),
                     qPrintable(QStringLiteral("item %1 was selected but not copied").arg(i)));
    }

    // Delete after Ctrl+A had the same root: it removed only the band, so 47 of
    // 60 items quietly survived a "delete everything". Deleting every item is
    // documented to trash the napkin whole (§6), which is the tell: before the
    // fix the napkin stayed live because 47 items were still on it.
    void deleteAfterSelectAllTakesTheWholeNapkin()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 60; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1").arg(i)));
        f.model()->reload();
        f.select(id);
        QVERIFY(!f.buffers.find(id)->inTrash());

        f.canvas()->selectAll();
        f.canvas()->deleteSelection();

        QVERIFY2(f.buffers.find(id)->inTrash(),
                 "items survived a select-all delete, so the napkin stayed live");
    }

    void deletingSelectedItemsRemovesThemFromTheBuffer()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        QCOMPARE(f.items.countForBuffer(id), 4);

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);
        f.canvas()->deleteSelection();

        QCOMPARE(f.items.countForBuffer(id), 3);
        QCOMPARE(f.canvas()->findChildren<ImageItemCard*>().size(), 1);
    }

    void cutCopiesThenRemoves()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        QApplication::clipboard()->clear();

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);
        f.canvas()->cutSelection();

        const auto* mime = QApplication::clipboard()->mimeData();
        QVERIFY(mime && mime->hasImage());
        QCOMPARE(f.items.countForBuffer(id), 3);
    }

    void deletingEveryItemTrashesTheBufferRatherThanLeavingAHusk()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        f.canvas()->selectAll();
        f.canvas()->deleteSelection();

        QCOMPARE(f.buffers.countLive(), 0);
        QCOMPARE(f.buffers.countTrash(), 1);
        QVERIFY(f.toast()->isVisible());     // and it is undoable
    }

    void removingAnImageKeepsItsBlobUntilTheSweep()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        const auto image = f.items.listForBuffer(id)[1];
        QVERIFY(f.blobs.exists(image.blobHash, image.mime));

        f.window.removeItems({image.id});

        // Deliberately still there: a blob whose last reference just went is
        // exactly the one the 8-second undo is about to need. Unlinking here is
        // what made undo restore rows pointing at deleted files.
        QVERIFY(f.blobs.exists(image.blobHash, image.mime));

        // Nor does the startup sweep take it: the item is in the trash now, and
        // the trash is still a promise. Only emptying it lets the file go.
        reconcileBlobs(f.items, f.blobs, f.thumbsDir());
        QVERIFY(f.blobs.exists(image.blobHash, image.mime));
        f.service.emptyTrash();
        reconcileBlobs(f.items, f.blobs, f.thumbsDir());
        QVERIFY(!f.blobs.exists(image.blobHash, image.mime));
    }

    // Removing an item hard-deletes its row and used to unlink its blob at once.
    // If that emptied the buffer, the buffer was trashed with an undo offer —
    // and undo handed back a buffer whose items no longer existed.
    void undoAfterRemovingEveryItemGivesTheContentBackNotAnEmptyShell()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        const auto blobBefore = f.items.listForBuffer(id)[1];
        f.select(id);

        f.canvas()->selectAll();
        f.canvas()->deleteSelection();
        QCOMPARE(f.buffers.countTrash(), 1);

        auto* undo = f.toast()->findChild<QPushButton*>();
        QVERIFY(undo);
        undo->click();

        QCOMPARE(f.buffers.countLive(), 1);
        QCOMPARE(f.items.countForBuffer(id), 4);          // the items came back
        QVERIFY(f.blobs.exists(blobBefore.blobHash, blobBefore.mime));   // and their blobs
    }

    void undoAfterRemovingOneItemPutsItBack()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        const auto removed = *f.items.find(image->itemId());
        QTest::mouseClick(image, Qt::LeftButton);
        f.canvas()->deleteSelection();
        QCOMPARE(f.items.countForBuffer(id), 3);

        auto* undo = f.toast()->findChild<QPushButton*>();
        QVERIFY(undo);
        undo->click();

        QCOMPARE(f.items.countForBuffer(id), 4);
        QVERIFY(f.blobs.exists(removed.blobHash, removed.mime));
    }

    // --- the four inconsistencies the user reported --------------------------
    void pastingTextGoesIntoTheSelectedBufferJustLikeAnImage()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        QApplication::clipboard()->setText(QStringLiteral("pasted note"));

        f.trigger("pasteAction");

        // Text used to always make a NEW buffer while images appended to the
        // selected one: the same gesture doing two different things.
        QCOMPARE(f.buffers.countLive(), 1);
        QCOMPARE(f.items.countForBuffer(id), 5);
        // Newest first: the thing you just pasted is at the top of the board.
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("pasted note"));
    }

    void pastingTextWithNothingSelectedMakesOneBuffer()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("a stray thought"));

        f.trigger("pasteAction");

        QCOMPARE(f.buffers.countLive(), 1);
        QCOMPARE(f.items.countForBuffer(f.buffers.listLive(10).front().id), 1);
    }

    void deletingAnItemLeavesTheNextOneSelected()
    {
        GuiFixture f;
        const auto id = seedMixed(f);   // text, image, image, text
        f.select(id);

        const auto items = f.items.listForBuffer(id);
        auto* second = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(second, Qt::LeftButton);
        f.canvas()->deleteSelection();

        // Selection lands on whatever now occupies that slot, so a run of
        // deletes does not require re-aiming the mouse each time.
        QCOMPARE(f.canvas()->selection().size(), 1);
        QCOMPARE(f.canvas()->selection().first(), items[2].id);
    }

    void deletingTheLastItemSelectsTheNewLast()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        // Delete the card at the end of the board.
        const auto ordered = f.items.listForBuffer(id);
        f.window.removeItems({ordered.back().id});

        QCOMPARE(f.canvas()->selection().size(), 1);
        QCOMPARE(f.canvas()->selection().first(), ordered[ordered.size() - 2].id);
    }

    void aSingleClickSelectsATextBlockRatherThanEditingIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* text = f.canvas()->findChildren<TextItemCard*>().first();
        QTest::mouseClick(text->findChild<QPlainTextEdit*>(), Qt::LeftButton);

        QCOMPARE(f.canvas()->selection().size(), 1);
        QCOMPARE(f.canvas()->selection().first(), text->itemId());
        QVERIFY(!text->hasEditFocus());
    }

    void aSelectedTextBlockCanBeDeletedWithTheMouseAlone()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        const int before = f.items.countForBuffer(id);

        auto* text = f.canvas()->findChildren<TextItemCard*>().first();
        QTest::mouseClick(text->findChild<QPlainTextEdit*>(), Qt::LeftButton);
        f.canvas()->deleteSelection();

        QCOMPARE(f.items.countForBuffer(id), before - 1);
    }

    void doubleClickingATextBlockStartsEditingIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* text = f.canvas()->findChildren<TextItemCard*>().first();
        QTest::mouseDClick(text->findChild<QPlainTextEdit*>(), Qt::LeftButton);

        QVERIFY(text->hasEditFocus());
        QVERIFY(!f.canvas()->hasSelection());   // editing and selecting are exclusive
    }

    void ctrlTAddsATextBlockToTheSelectedBuffer()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        f.trigger("addTextAction");
        QTest::keyClicks(f.editor(), "written after Ctrl+T");
        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(id) == 5, 2000);
        QCOMPARE(f.items.listForBuffer(id).front().text,
                 QStringLiteral("written after Ctrl+T"));
    }

    // --- crashes reported from real use --------------------------------------
    void pastingAfterTheSelectedBufferIsPurgedDoesNotCrash()
    {
        // editingBuffer_ kept naming a row that trashing, then Empty trash, had
        // removed. Appending to it violated the foreign key, and the DbError
        // unwound into Qt's event loop, which calls std::terminate.
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("first"));
        f.trigger("pasteAction");
        const auto id = f.buffers.listLive(10).front().id;

        f.window.trashRow(f.model()->rowForId(id));
        f.window.emptyTrashForTest();
        QVERIFY(!f.buffers.find(id).has_value());

        QApplication::clipboard()->setText(QStringLiteral("second"));
        f.trigger("pasteAction");

        // A fresh buffer holding the new text, rather than a crash. (SQLite
        // reuses rowids after a delete, so the id may well be the same one.)
        QCOMPARE(f.buffers.countLive(), 1);
        const auto fresh = f.buffers.listLive(10).front().id;
        QCOMPARE(f.items.countForBuffer(fresh), 1);
        QCOMPARE(f.items.listForBuffer(fresh).front().text, QStringLiteral("second"));
    }

    void pastingAfterTheSelectedBufferIsTrashedStartsAFreshOne()
    {
        // Same stale reference, milder symptom: the paste landed inside the
        // trashed buffer, so the text vanished from view while quietly
        // accumulating somewhere the user could not see.
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("first"));
        f.trigger("pasteAction");
        const auto id = f.buffers.listLive(10).front().id;
        const int itemsBefore = f.items.countForBuffer(id);

        f.window.trashRow(f.model()->rowForId(id));
        QApplication::clipboard()->setText(QStringLiteral("second"));
        f.trigger("pasteAction");

        QCOMPARE(f.items.countForBuffer(id), itemsBefore);   // the dead one is untouched
        QCOMPARE(f.buffers.countLive(), 1);
    }

    void aLiveBufferNeverHasZeroItems()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("only item"));
        f.trigger("pasteAction");
        const auto id = f.buffers.listLive(10).front().id;

        f.canvas()->selectAll();
        f.canvas()->deleteSelection();

        // The card used to stay in the list with no items, still showing the
        // text it no longer contained.
        QCOMPARE(f.model()->rowCount(), 0);
        for (const auto& b : f.buffers.listLive(100))
            QVERIFY(f.items.countForBuffer(b.id) > 0);
        QVERIFY(f.buffers.find(id)->inTrash());
    }

    void deleteAllThenUndoThenDeleteAgainSurvives()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("resilient"));
        f.trigger("pasteAction");
        const auto id = f.buffers.listLive(10).front().id;

        f.canvas()->selectAll();
        f.canvas()->deleteSelection();
        f.toast()->findChild<QPushButton*>()->click();
        QCOMPARE(f.items.countForBuffer(id), 1);

        f.select(id);
        f.canvas()->selectAll();
        f.canvas()->deleteSelection();
        QCOMPARE(f.buffers.countLive(), 0);
    }

    // --- search --------------------------------------------------------------
    void typingInTheSearchFieldFiltersTheList()
    {
        GuiFixture f;
        const auto wanted = f.buffers.create();
        f.service.appendTo(wanted, Item::makeText(QStringLiteral("systemctl restart nginx")));
        const auto other = f.buffers.create();
        f.service.appendTo(other, Item::makeText(QStringLiteral("sudo pacman -Syu")));
        f.model()->reload();
        QCOMPARE(f.model()->rowCount(), 2);

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        QVERIFY(field);
        field->setText(QStringLiteral("nginx"));

        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);
        QCOMPARE(f.model()->rowCount(), 1);
        QCOMPARE(f.model()->idAt(0), wanted);
        QVERIFY(f.model()->isSearching());
    }

    void clearingTheSearchRestoresTheWholeList()
    {
        GuiFixture f;
        f.seed("first"); f.seed("second");
        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("first"));
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);
        QCOMPARE(f.model()->rowCount(), 1);

        field->clear();
        QTRY_VERIFY_WITH_TIMEOUT(!f.model()->isSearching(), 2000);
        QCOMPARE(f.model()->rowCount(), 2);
        QVERIFY(!f.model()->isSearching());
    }

    void aResultCarriesTheSnippetThatExplainsIt()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(
            QStringLiteral("the quick brown fox jumps over the lazy dog")));
        f.seed("something else");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("brown"));
        // Wait for the SEARCH, not for a row count that may already be right.
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);
        QCOMPARE(f.model()->rowCount(), 1);

        // A result list that all reads "4 items · 2 days ago" says nothing
        // about which one you wanted.
        const QString snippet =
            f.model()->index(0, 0).data(BufferListModel::SnippetRole).toString();
        QVERIFY(snippet.contains(QStringLiteral("brown")));
        QVERIFY2(snippet.contains(QChar(2)), "the matched term must be marked for the delegate");
    }

    void searchingSelectsTheBestResultSoItIsAlreadyOpen()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("findable thing")));
        f.seed("unrelated");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("findable"));
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);
        QCOMPARE(f.model()->rowCount(), 1);

        // The board shows the top hit without a second gesture.
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 1);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().first()->text(),
                 QStringLiteral("findable thing"));
    }

    void searchFindsSomethingYouJustPasted()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("a brand new thought"));
        f.trigger("pasteAction");

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("brand"));
        // The index is maintained by triggers, so there is no moment where a
        // just-written item is invisible to search.
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);
        QCOMPARE(f.model()->rowCount(), 1);
    }

    void searchNarrowsTheBoardToTheItemsThatMatched()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("restart nginx now")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("unrelated thought")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("nginx config path")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("another unrelated one")));
        f.seed("a different buffer entirely");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("nginx"));
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);

        // Finding which buffer matched and then having to re-find the item
        // inside it is half an answer.
        QVERIFY(f.canvas()->isFiltered());
        QCOMPARE(f.canvas()->matchCount(), 2);
        QCOMPARE(f.canvas()->totalCount(), 4);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 2);
        for (auto* card : f.canvas()->findChildren<TextItemCard*>())
            QVERIFY(card->text().contains(QStringLiteral("nginx")));
    }

    void showAllRestoresTheRestOfTheBuffer()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("nginx here")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("context that matters")));
        f.seed("other");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("nginx"));
        QTRY_VERIFY_WITH_TIMEOUT(f.canvas()->isFiltered(), 2000);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 1);

        auto* showAll = f.window.findChild<QPushButton*>(QStringLiteral("showAllButton"));
        QVERIFY(showAll);
        showAll->click();

        // The surrounding items are often the context you actually wanted.
        QVERIFY(!f.canvas()->isFiltered());
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 2);
    }

    void clearingTheSearchRestoresEveryItem()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("nginx here")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("and something else")));
        f.seed("other");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("nginx"));
        QTRY_VERIFY_WITH_TIMEOUT(f.canvas()->isFiltered(), 2000);

        field->clear();
        QTRY_VERIFY_WITH_TIMEOUT(!f.model()->isSearching(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(f.canvas()->findChildren<TextItemCard*>().size(), 2, 2000);
    }

    void animageMatchesOnItsFilename()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("some notes")));
        f.service.appendTo(id, Item::makeImage(QStringLiteral("hash"), 10, 10, 1,
                                               QStringLiteral("wayland-clipboard.png")));
        f.seed("other");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("wayland"));
        QTRY_VERIFY_WITH_TIMEOUT(f.canvas()->isFiltered(), 2000);

        QCOMPARE(f.canvas()->matchCount(), 1);
        QCOMPARE(f.canvas()->findChildren<ImageItemCard*>().size(), 1);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 0);
    }

    // --- keyboard and editing state ------------------------------------------
    void deleteCanBePressedRepeatedly()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 5; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1").arg(i)));
        f.model()->reload();
        f.select(id);

        auto* first = f.canvas()->findChildren<TextItemCard*>().first();
        QTest::mouseClick(first, Qt::LeftButton);

        // Clicking a card must move the keyboard to the canvas: otherwise
        // Delete is delivered to the buffer list, which trashes a whole buffer.
        QVERIFY(f.canvas()->keyboardIsHere());

        for (int expected = 4; expected >= 1; --expected) {
            QTest::keyClick(f.canvas(), Qt::Key_Delete);
            QCOMPARE(f.items.countForBuffer(id), expected);
            // And after each one the canvas still owns the keyboard, which is
            // what makes the NEXT press work.
            QVERIFY2(f.canvas()->keyboardIsHere(),
                     qPrintable(QString("lost focus with %1 items left").arg(expected)));
            QCOMPARE(f.canvas()->selection().size(), 1);
        }
    }

    // User, 2026-10-09: with the focus ring on a card, Delete did nothing — it
    // only worked on a card selected with the mouse. And Tab put a ring on one
    // card while the arrows moved the selection to another.
    void deleteActsOnTheCardTheKeyboardIsOn()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 4; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1").arg(i)));
        f.model()->reload();
        f.select(id);
        auto stillThere = [&](const QString& text) {
            for (const auto& item : f.items.listForBuffer(id)) if (item.text == text) return true;
            return false;
        };
        auto currentText = [&]() -> QString {
            for (auto* c : f.canvas()->findChildren<TextItemCard*>())
                if (c->isCurrent()) return f.items.find(c->itemId())->text;
            return {};
        };
        // Offscreen cannot activate the window; Tab's focus is delivered directly.
        QFocusEvent tabIn(QEvent::FocusIn, Qt::TabFocusReason);

        // 1. Tab onto the board: the current card is marked, and Delete takes it.
        QApplication::sendEvent(f.canvas(), &tabIn);
        const QString first = currentText();
        QVERIFY(!first.isEmpty());
        QTest::keyClick(f.canvas(), Qt::Key_Delete);
        QCOMPARE(f.items.countForBuffer(id), 3);
        QVERIFY(!stillThere(first));

        // 2. Click one card, arrow to the next: the selection goes with the
        //    arrow, so Delete takes the card the arrow reached.
        auto cards = f.canvas()->findChildren<TextItemCard*>();
        QTest::mouseClick(cards[0], Qt::LeftButton);
        const QString clicked = f.items.find(cards[0]->itemId())->text;
        QTest::keyClick(f.canvas(), Qt::Key_Down);
        const QString reached = currentText();
        QVERIFY(reached != clicked);
        QCOMPARE(f.canvas()->selection().size(), 1);
        QTest::keyClick(f.canvas(), Qt::Key_Delete);
        QVERIFY(stillThere(clicked));
        QVERIFY(!stillThere(reached));

        // 3. Nothing selected, the ring moved with Ctrl+arrow: Delete takes the
        //    ringed card.
        f.canvas()->clearSelection();
        QTest::keyClick(f.canvas(), Qt::Key_Home, Qt::ControlModifier);
        QVERIFY(f.canvas()->selection().isEmpty());   // Ctrl moves the ring only
        const int before = f.items.countForBuffer(id);
        QTest::keyClick(f.canvas(), Qt::Key_Delete);
        QCOMPARE(f.items.countForBuffer(id), before - 1);
    }

    // One "you are here" at a time: the board is a single Tab stop, the arrows
    // move one ring, and the ring goes when the keyboard leaves the board.
    void theBoardHasOneTabStopAndOneRing()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 4; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1").arg(i)));
        f.model()->reload();
        f.select(id);
        auto cards = f.canvas()->findChildren<ItemCard*>();
        for (auto* c : cards) QCOMPARE(c->focusPolicy(), Qt::NoFocus);   // never their own stop

        auto rings = [&] {
            int n = 0;
            for (auto* c : f.canvas()->findChildren<ItemCard*>()) n += c->showsFocusRing();
            return n;
        };
        QFocusEvent tabIn(QEvent::FocusIn, Qt::TabFocusReason);
        QApplication::sendEvent(f.canvas(), &tabIn);
        QCOMPARE(rings(), 1);
        for (auto key : {Qt::Key_Down, Qt::Key_Right, Qt::Key_Up}) {
            QTest::keyClick(f.canvas(), key, Qt::ControlModifier);   // ring only
            QCOMPARE(rings(), 1);
        }
        QTest::keyClick(f.canvas(), Qt::Key_Down);   // selected: the selection edge says it
        QCOMPARE(rings(), 0);
        QCOMPARE(f.canvas()->selection().size(), 1);

        f.canvas()->clearSelection();
        QCOMPARE(rings(), 1);
        QFocusEvent tabOut(QEvent::FocusOut, Qt::TabFocusReason);
        QApplication::sendEvent(f.canvas(), &tabOut);
        QCOMPARE(rings(), 0);                          // the keyboard is elsewhere
    }

    // User, 2026-10-09: double-click to edit selects the word; after editing
    // ended, the word stayed highlighted. And clicking outside the board did
    // not end editing at all.
    void leavingEditingLeavesNoTextSelected()
    {
        GuiFixture f;
        const auto id = f.seed("call the dentist on tuesday");
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        auto* edit = card->findChild<QPlainTextEdit*>();
        QTest::mouseDClick(edit->viewport(), Qt::LeftButton, {}, QPoint(20, 8));
        QVERIFY(card->hasEditFocus());
        QVERIFY(edit->textCursor().hasSelection());   // the word, as an editor does

        card->endEditing();
        QVERIFY(!card->hasEditFocus());
        QVERIFY(!edit->textCursor().hasSelection());

        // The keyboard leaving for elsewhere — the list, the search box — ends it.
        QTest::mouseDClick(edit->viewport(), Qt::LeftButton, {}, QPoint(20, 8));
        QVERIFY(card->hasEditFocus());
        QFocusEvent away(QEvent::FocusOut, Qt::MouseFocusReason);
        QApplication::sendEvent(edit, &away);
        QTRY_VERIFY(!card->hasEditFocus());
        QVERIFY(!edit->textCursor().hasSelection());

        // But not its own context menu.
        QTest::mouseDClick(edit->viewport(), Qt::LeftButton, {}, QPoint(20, 8));
        QFocusEvent menu(QEvent::FocusOut, Qt::PopupFocusReason);
        QApplication::sendEvent(edit, &menu);
        QTest::qWait(20);
        QVERIFY(card->hasEditFocus());
        card->endEditing();
    }

    // User, 2026-10-09: a delete button on the card under the pointer or the
    // keyboard, on its top-left corner, half outside — and no "are you sure?":
    // it goes to the trash, with Undo.
    void aCardsDeleteButtonTrashesItWithoutAsking()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 3; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("item %1").arg(i)));
        f.model()->reload();
        f.select(id);

        auto cards = f.canvas()->findChildren<TextItemCard*>();
        QCOMPARE(cards.size(), 3);
        for (auto* c : cards) QVERIFY(!c->removeButtonShown());   // not on every card at once

        QTest::mouseClick(cards[0], Qt::LeftButton);              // selected: the button shows
        QVERIFY(cards[0]->removeButtonShown());
        QTest::mouseClick(cards[1], Qt::LeftButton);              // and moves with the selection
        QVERIFY(cards[1]->removeButtonShown());
        QVERIFY(!cards[0]->removeButtonShown());
        QVERIFY(!cards[2]->removeButtonShown());

        QAbstractButton* button = nullptr;
        for (auto* b : f.canvas()->findChildren<QAbstractButton*>(QStringLiteral("cardRemoveButton")))
            if (b->isVisible()) button = b;
        QVERIFY(button);
        // On the card's top-left corner, half outside it, and still on the board.
        const QRect card = cards[1]->geometry();
        QVERIFY(button->geometry().contains(card.topLeft()));
        QVERIFY(button->geometry().left() < card.left());
        QVERIFY(button->geometry().top() < card.top());
        QVERIFY(button->geometry().top() >= 0);
        QVERIFY(button->geometry().left() >= 0);

        // Not over the text while the note is being typed into.
        cards[1]->beginEditing();
        QVERIFY(!cards[1]->removeButtonShown());
        cards[1]->endEditing();
        QTest::mouseClick(cards[1], Qt::LeftButton);
        QVERIFY(cards[1]->removeButtonShown());

        // Any dialog that opens is a failure; close it so the test cannot hang.
        bool asked = false;
        QTimer watch;
        QObject::connect(&watch, &QTimer::timeout, [&asked] {
            if (auto* w = QApplication::activeModalWidget()) { asked = true; w->close(); }
        });
        watch.start(20);

        const ItemId doomed = cards[1]->itemId();
        QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(!asked);
        QCOMPARE(f.items.countForBuffer(id), 2);
        QVERIFY(f.buffers.find(f.items.find(doomed)->bufferId)->inTrash());   // trashed, not destroyed
        QVERIFY(f.toast()->hasOffer());                                       // and Undo is offered

        // In the trash a delete is permanent, so there is no one-click button.
        f.canvas()->setInTrash(true);
        auto* left = f.canvas()->findChildren<TextItemCard*>().first();
        QTest::mouseClick(left, Qt::LeftButton);
        QVERIFY(!left->removeButtonShown());
        f.canvas()->setInTrash(false);
        QVERIFY(left->removeButtonShown());
    }

    void anEditingCardIsNotWashedOverWithTheSelectionTint()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QTest::mouseClick(card, Qt::LeftButton);
        QVERIFY(card->isSelected());

        card->beginEditing();

        // Editing clears the selection, so the tint that made the whole card
        // blue while typing has nothing to draw from — only the border remains.
        QVERIFY(card->hasEditFocus());
        QVERIFY(!card->isSelected());
        QVERIFY(!f.canvas()->hasSelection());
    }

    // --- the start page ------------------------------------------------------
    void anEmptyNapkinExplainsItself()
    {
        GuiFixture f;
        auto* welcome = f.window.findChild<WelcomeView*>();
        QVERIFY(welcome);
        QVERIFY(welcome->isVisible());

        // Napkin has no menus to explore on first run and no document to open;
        // this is the one screen that has to say what it is for.
        QStringList shown;
        for (auto* label : welcome->findChildren<QLabel*>()) shown << label->text();
        const QString all = shown.join(QLatin1Char('|'));
        QVERIFY(all.contains(QStringLiteral("Napkin")));
        QVERIFY(all.contains(QStringLiteral("scratch surface")));
        QVERIFY(all.contains(QStringLiteral("Ctrl+N")));
        QVERIFY(all.contains(QStringLiteral("Ctrl+V")));

        // And the logo actually loaded, rather than leaving an empty label.
        bool hasMark = false;
        for (auto* label : welcome->findChildren<QLabel*>())
            if (!label->pixmap().isNull()) hasMark = true;
        QVERIFY2(hasMark, "the start page has no logo");
    }

    void aShortcutRowDoesTheThingItDescribes()
    {
        GuiFixture f;
        auto* welcome = f.window.findChild<WelcomeView*>();
        QVERIFY(welcome);

        // Reading what a key does and pressing it should be the same gesture on
        // the screen that exists to teach you the keys.
        QPushButton* newBuffer = nullptr;
        for (auto* row : welcome->findChildren<QPushButton*>())
            if (row->accessibleName().contains(QStringLiteral("New napkin"))) newBuffer = row;
        QVERIFY(newBuffer);

        newBuffer->click();
        QCOMPARE(f.model()->rowCount(), 1);      // a draft card is showing
        QCOMPARE(f.buffers.countLive(), 0);      // and invariant 5 still holds
    }

    void theStartPageGivesWayAsSoonAsThereIsContent()
    {
        GuiFixture f;
        QVERIFY(f.window.findChild<WelcomeView*>()->isVisible());

        QApplication::clipboard()->setText(QStringLiteral("first thing"));
        f.trigger("pasteAction");

        QVERIFY(!f.window.findChild<WelcomeView*>()->isVisible());
    }

    void anEmptyTrashIsNotGreetedLikeAFirstRun()
    {
        GuiFixture f;
        f.seed("something");
        f.window.showTrash(true);

        // An empty trash and a search with no hits are not "you have nothing
        // yet", and should not be met with the whole start page.
        QVERIFY(!f.window.findChild<WelcomeView*>()->isVisible());
    }

    // --- structural changes must not be deferred -----------------------------
    void deletingEveryItemAfterTypingStillRemovesTheBufferFromTheList()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("something")));
        f.model()->reload();
        f.select(id);

        // Typing freezes the list order so it cannot re-sort under you.
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        QTest::keyClicks(card->findChild<QPlainTextEdit*>(), " more");
        QTest::qWait(600);
        QVERIFY(f.model()->orderFrozen());

        f.canvas()->selectAll();
        f.canvas()->deleteSelection();

        // The freeze must not also defer a change in MEMBERSHIP: the buffer was
        // left visible in the list while the toast beneath it said it had been
        // moved to the trash.
        QTRY_COMPARE_WITH_TIMEOUT(f.model()->rowCount(), 0, 2000);
        QCOMPARE(f.buffers.countLive(), 0);
        QCOMPARE(f.buffers.countTrash(), 1);
    }

    // --- the menu -------------------------------------------------------------
    // The menu bar became one menu behind the header's menu button (2026-10-06
    // mockup). It must still hold everything the four menus held.
    static QMenu* appMenu(GuiFixture& f)
    {
        auto* button = f.window.findChild<QToolButton*>(QStringLiteral("overflowButton"));
        return button ? button->menu() : nullptr;
    }

    void everyActionIsReachableFromTheMenu()
    {
        GuiFixture f;
        QVERIFY2(!f.window.menuWidget(), "a menu bar is back beside the menu button");
        QMenu* menu = appMenu(f);
        QVERIFY(menu);

        QStringList sections, items;
        int listed = 0;
        for (auto* a : menu->actions()) {
            if (a->isSeparator()) { if (!a->text().isEmpty()) sections << a->text(); continue; }
            items << a->text();
            if (!a->shortcut().isEmpty()) ++listed;
        }
        QCOMPARE(sections, (QStringList{QStringLiteral("Napkins"), QStringLiteral("Trash")}));
        for (const char* needed : {"New napkin", "New note", "Paste", "Undo", "Pin or unpin",
                                   "Rename…", "Search", "Clean up…", "Show trash", "Empty trash…",
                                   "Export everything…", "Settings…", "Keyboard shortcuts…",
                                   "About Napkin", "Quit"})
            QVERIFY2(items.contains(QString::fromUtf8(needed)), needed);
        // The menu is where a user finds out what the app can do, so every
        // shortcut must be listed rather than only bound.
        QVERIFY2(listed >= 10, qPrintable(QString("only %1 shortcuts listed").arg(listed)));
    }

    // Pin, Keep, Calculate and the Trash's undo are a chord or a right-click
    // away and otherwise invisible, so a tour shows them (user request,
    // 2026-10-06). It steps forward and back, and once seen it stays seen.
    // F11 and the menu's Full screen: one action, F11 bound exactly once (a
    // chord listed twice on an action is ambiguous and fires nothing), and the
    // check mark follows the window.
    void fullScreenIsF11AndInTheMenu()
    {
        GuiFixture f;
        auto* action = f.window.findChild<QAction*>(QStringLiteral("fullScreenAction"));
        QVERIFY(action && action->isCheckable());
        QCOMPARE(action->shortcuts().count(QKeySequence(Qt::Key_F11)), 1);
        QVERIFY(f.window.actions().contains(action));        // works without opening the menu
        QVERIFY(appMenu(f)->actions().contains(action));     // and is listed in it

        f.window.showMaximized();
        action->trigger();
        QTRY_VERIFY(f.window.isFullScreen());
        QVERIFY(action->isChecked());
        action->trigger();
        QTRY_VERIFY(!f.window.isFullScreen());
        QVERIFY(!action->isChecked());
        QVERIFY2(f.window.isMaximized(), "leaving full screen did not go back to maximized");
    }

    void theTourWalksThroughAndIsSeenOnce()
    {
        GuiFixture f;
        QSettings().remove(QStringLiteral("onboarding/tourSeen"));
        QVERIFY(!TourDialog::seen());

        TourDialog tour(&f.window);
        QVERIFY(tour.stepCount() >= 6);
        QCOMPARE(tour.currentStep(), 0);
        QTest::keyClick(&tour, Qt::Key_Left);            // nothing before the first
        QCOMPARE(tour.currentStep(), 0);
        QTest::keyClick(&tour, Qt::Key_Right);
        QCOMPARE(tour.currentStep(), 1);
        tour.showStep(tour.stepCount() + 5);             // clamped, not out of range
        QCOMPARE(tour.currentStep(), tour.stepCount() - 1);

        QStringList said;
        for (auto* l : tour.findChildren<QLabel*>()) said << l->text();
        for (int i = 0; i < tour.stepCount(); ++i) {
            tour.showStep(i);
            for (auto* l : tour.findChildren<QLabel*>()) said << l->text();
        }
        const QString all = said.join(QLatin1Char(' '));
        for (const char* feature : {"Pin", "Keep", "Ctrl+Tab", "Ctrl+F", "Ctrl+Z", "Trash"})
            QVERIFY2(all.contains(QString::fromLatin1(feature)), feature);

        tour.reject();                                    // skipping counts as seen
        QVERIFY(TourDialog::seen());

        // And it can always be opened again.
        QVERIFY(f.window.findChild<QAction*>(QStringLiteral("tourAction")));
        QVERIFY(f.window.findChild<QPushButton*>(QStringLiteral("tourButton")));
    }

    void theTrashMenuAndTheHeaderToggleStayInStep()
    {
        GuiFixture f;
        f.seed("something");
        auto* toggle = f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"));
        QVERIFY(toggle);

        auto* home = f.window.findChild<QPushButton*>(QStringLiteral("homeSegment"));
        QVERIFY(home && home->isChecked());

        QAction* showTrash = nullptr;
        for (auto* a : appMenu(f)->actions())
            if (a->text().contains(QStringLiteral("Show trash"))) showTrash = a;
        QVERIFY(showTrash);

        showTrash->trigger();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);
        QVERIFY(toggle->isChecked());
        QVERIFY(!home->isChecked());   // the switch shows one place at a time

        // Pressing the place you are in keeps you there — a second click on
        // Trash used to un-press it and leave (independent review).
        toggle->click();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);
        QVERIFY(toggle->isChecked());

        // Home is the way back, and pressing it again stays home.
        home->click();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QVERIFY(!toggle->isChecked());
        home->click();
        QVERIFY(home->isChecked());
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
    }

    void homeReturnsFromBothTrashAndSearch()
    {
        GuiFixture f;
        f.seed("findable"); f.seed("other");
        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("findable"));
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching(), 2000);
        f.window.showTrash(true);

        QAction* home = nullptr;
        for (auto* a : appMenu(f)->actions())
            if (a->text().contains(QStringLiteral("All napkins"))) home = a;
        QVERIFY(home);
        home->trigger();

        // One gesture back to the ordinary view from wherever you are.
        QVERIFY(!f.model()->isSearching());
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QCOMPARE(f.model()->rowCount(), 2);
    }

    // --- a card grows with what you put in it --------------------------------
    void aCardMadeWithCtrlTGrowsAsYouPasteIntoIt()
    {
        GuiFixture f;
        const auto id = f.seed("existing");
        f.select(id);
        auto* edit = f.newTextCard();

        // findChildren is tree order, so .first() is the card that already
        // existed — not the one Ctrl+T just made.
        TextItemCard* card = nullptr;
        for (auto* c : f.canvas()->findChildren<TextItemCard*>())
            if (c->isComposer()) card = c;
        QVERIFY(card);
        const int empty = card->height();

        edit->setPlainText(QStringLiteral(
            "This is a long pasted paragraph that should wrap onto several lines "
            "and therefore make its card considerably taller than a single line."));
        QTRY_VERIFY_WITH_TIMEOUT(card->height() > empty, 2000);
    }

    void editingAnExistingCardGrowsItToo()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("short")));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        const int before = card->height();
        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral(
            "Now considerably longer text that wraps across several lines in a "
            "narrow column and should make the card grow to fit what it holds."));

        // The measurement cache is keyed on the item's last SAVED time, which
        // does not move while you type — so this card kept the height it had
        // before the edit began.
        QTRY_VERIFY_WITH_TIMEOUT(card->height() > before, 2000);
    }

    void aCardShrinksBackWhenYouCutTextOut()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral(
            "A reasonably long note that wraps across several lines so that the "
            "card it lives in is meaningfully taller than the minimum height.")));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        const int tall = card->height();
        QVERIFY(tall > tokens::kCardMinHeight);

        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral("brief"));
        QTRY_VERIFY_WITH_TIMEOUT(card->height() < tall, 2000);
    }

    // --- card chrome and sizing ----------------------------------------------
    void aPastedParagraphGetsACardTallEnoughToReadIt()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral(
            "This is some of the multilined text that got accumulated into lines and can "
            "be showcased, and this has to be a very cool napkin where you can store "
            "things temporarily without having to name them."));
        f.trigger("pasteAction");

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        auto* edit = card->findChild<QPlainTextEdit*>();

        // The card must be tall enough that the text does not need scrolling:
        // a paragraph that wraps to five lines used to arrive as one visible
        // line, because the height was measured against a viewport that did not
        // have a width yet.
        const int lines = 5;
        QVERIFY2(card->height() >= edit->fontMetrics().lineSpacing() * lines,
                 qPrintable(QString("card is only %1px tall").arg(card->height())));
        QVERIFY(!card->isClipped());

        // And nothing is scrolled out of view horizontally or vertically: the
        // card shows the text from its very first character.
        QCOMPARE(edit->horizontalScrollBar()->value(), 0);
        QCOMPARE(edit->verticalScrollBar()->value(), 0);
    }

    void pastingTextDoesNotAlsoOpenABlankCard()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("just this"));
        f.trigger("pasteAction");

        const auto cards = f.canvas()->findChildren<TextItemCard*>();
        QCOMPARE(cards.size(), 1);          // the pasted card, and nothing else
        QVERIFY(!cards.first()->isComposer());
        QCOMPARE(f.items.countForBuffer(f.buffers.listLive(10).front().id), 1);
    }

    void doubleClickingABufferDoesNotAddATextBlock()
    {
        GuiFixture f;
        const auto id = f.seed("just looking");
        f.select(id);
        const int before = f.canvas()->findChildren<TextItemCard*>().size();
        QCOMPARE(before, 1);   // the one real item, and no blank block

        const QRect rect = f.view()->visualRect(f.model()->index(f.model()->rowForId(id), 0));
        QTest::mouseDClick(f.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());

        // Opening a buffer is not a request to write in it.
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), before);
        QCOMPARE(f.items.countForBuffer(id), 1);
    }

    void everyCardHasACopyActionAndAnAge()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        const auto footers = f.canvas()->findChildren<CardFooter*>();
        QCOMPARE(footers.size(), f.items.countForBuffer(id));
    }

    void copyingFromACardAlsoSelectsIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        QApplication::clipboard()->clear();

        // Select something else entirely, then use a different card's button.
        auto* image = f.canvas()->findChildren<ImageItemCard*>().first();
        QTest::mouseClick(image, Qt::LeftButton);

        auto* textCard = f.canvas()->findChildren<TextItemCard*>().first();
        emit textCard->copyRequested(textCard->itemId());

        // The invariant: after any copy, the clipboard matches what is VISIBLY
        // selected. Restoring the old selection afterwards would leave one card
        // highlighted while a different one sat on the clipboard.
        QCOMPARE(QApplication::clipboard()->text(), textCard->text());
        QCOMPARE(f.canvas()->selection().size(), 1);
        QCOMPARE(f.canvas()->selection().first(), textCard->itemId());
    }

    void aTinyCardStillHasAMinimumSize()
    {
        GuiFixture f;
        f.window.resize(1200, 800);
        QTest::qWait(30);
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("ok")));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY2(card->height() >= tokens::kCardMinHeight,
                 qPrintable(QString("a two-letter card is %1px tall").arg(card->height())));
        QVERIFY(card->width() >= tokens::kCardMinWidth);
    }

    void addingAnItemDoesNotResizeTheCardsAlreadyThere()
    {
        // With an as-needed scrollbar, the bar appearing shrinks the viewport by
        // ~14px, which changes the column width and resizes every card in the
        // buffer. The layout width now reserves the extent unconditionally.
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("first")));
        f.model()->reload();
        f.select(id);
        const int widthBefore = f.canvas()->findChildren<TextItemCard*>().first()->width();

        for (int i = 0; i < 12; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("filler %1").arg(i)));
        f.window.selectBuffer(f.model()->rowForId(id));

        for (auto* card : f.canvas()->findChildren<TextItemCard*>())
            QCOMPARE(card->width(), widthBefore);
    }

    void aCardsHeightDependsOnlyOnItsOwnContent()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("short")));
        f.model()->reload();
        f.select(id);
        const int aloneHeight = f.canvas()->findChildren<TextItemCard*>().first()->height();

        // Add a very tall neighbour; the short card must not change size.
        QString huge;
        for (int i = 0; i < 60; ++i) huge += QStringLiteral("line %1\n").arg(i);
        f.service.appendTo(id, Item::makeText(huge));
        f.select(id);
        f.window.selectBuffer(f.model()->rowForId(id));

        for (auto* card : f.canvas()->findChildren<TextItemCard*>())
            if (card->text() == QStringLiteral("short"))
                QCOMPARE(card->height(), aloneHeight);
    }

    // --- the board model -----------------------------------------------------
    void clearingACardWhileEditingItKeepsIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        const int before = f.items.countForBuffer(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* edit = card->findChild<QPlainTextEdit*>();
        edit->selectAll();
        QTest::keyClick(edit, Qt::Key_Delete);
        QTest::qWait(700);   // well past the autosave debounce

        // Clearing a card in order to rewrite it must not delete it out from
        // under you mid-sentence.
        QCOMPARE(f.items.countForBuffer(id), before);
        QVERIFY(card->hasEditFocus());

        // And typing the replacement keeps the same item, rather than making a
        // new one beside a corpse.
        QTest::keyClicks(edit, "rewritten from scratch");
        QTRY_VERIFY_WITH_TIMEOUT(
            f.items.find(card->itemId())->text == QStringLiteral("rewritten from scratch"), 3000);
        QCOMPARE(f.items.countForBuffer(id), before);
    }

    void clearingACardAndThenLeavingItRemovesIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);
        const int before = f.items.countForBuffer(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* edit = card->findChild<QPlainTextEdit*>();
        edit->selectAll();
        QTest::keyClick(edit, Qt::Key_Delete);

        // Leaving is the moment an empty card is judged.
        card->endEditing();
        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(id) == before - 1, 3000);
    }

    void ctrlEnterFinishesEditing()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        QVERIFY(card->hasEditFocus());

        QTest::keyClick(card->findChild<QPlainTextEdit*>(), Qt::Key_Return,
                        Qt::ControlModifier);
        QVERIFY(!card->hasEditFocus());
    }

    void plainEnterStaysInTheText()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* edit = card->findChild<QPlainTextEdit*>();
        const int lines = edit->toPlainText().count(QLatin1Char('\n'));
        QTest::keyClick(edit, Qt::Key_Return);

        // A note is several lines more often than it is one.
        QVERIFY(card->hasEditFocus());
        QCOMPARE(edit->toPlainText().count(QLatin1Char('\n')), lines + 1);
    }

    void clickingAwayFromACardCommitsIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        QTest::keyClicks(card->findChild<QPlainTextEdit*>(), " plus more");

        // Clicking the empty board is a commit; anything else leaves the user
        // wondering whether their typing was kept.
        QTest::mouseClick(f.canvas()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(5, 5));
        QVERIFY(!card->hasEditFocus());
        QTRY_VERIFY_WITH_TIMEOUT(
            f.items.find(card->itemId())->text.endsWith(QStringLiteral(" plus more")), 3000);
    }

    void theCaretIsVisibleTheMomentYouDoubleClick()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        auto* edit = card->findChild<QPlainTextEdit*>();

        // While read-only the editor must NOT hold focus: the caret blink only
        // starts on a focus-in event, so an editor that already had focus gained
        // a caret that never appeared until an arrow key forced a repaint.
        QCOMPARE(edit->focusPolicy(), Qt::NoFocus);
        QVERIFY(!edit->hasFocus());

        card->beginEditing();
        QCOMPARE(edit->focusPolicy(), Qt::StrongFocus);
        QVERIFY(!edit->isReadOnly());
        QCOMPARE(edit->cursorWidth(), 2);
    }

    void clearingTheOnlyCardAndLeavingItRemovesTheBuffer()
    {
        GuiFixture f;
        QApplication::clipboard()->setText(QStringLiteral("only thing"));
        f.trigger("pasteAction");
        const auto id = f.buffers.listLive(10).front().id;

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* edit = card->findChild<QPlainTextEdit*>();
        edit->selectAll();
        QTest::keyClick(edit, Qt::Key_Delete);
        card->endEditing();

        QTRY_VERIFY_WITH_TIMEOUT(f.buffers.countLive() == 0, 3000);
        QVERIFY(f.buffers.find(id)->inTrash());
    }

    void ctrlNGivesAnEmptyBoardRatherThanABlankPage()
    {
        GuiFixture f;
        f.trigger("newBufferAction");

        // Napkin is temporary storage, not an editor: a new buffer waits to be
        // pasted into instead of offering somewhere to write.
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 0);
        QCOMPARE(f.buffers.countLive(), 0);
    }

    void theNewestItemIsFirst()
    {
        GuiFixture f;
        const auto id = f.seed("oldest");
        f.select(id);
        QTest::keyClicks(f.newTextCard(), "newest");
        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(id) == 2, 2000);

        const auto ordered = f.items.listForBuffer(id);
        QCOMPARE(ordered.front().text, QStringLiteral("newest"));
        // And the board agrees: first on screen is the newest, not the first
        // widget that happened to be constructed.
        QCOMPARE(f.canvas()->itemOrder().first(), ordered.front().id);
    }

    void aCardIsNoWiderThanTheBoardAllows()
    {
        GuiFixture f;
        const auto id = f.seed("short");
        f.select(id);
        f.window.resize(1400, 800);
        QTest::qWait(50);

        for (auto* card : f.canvas()->findChildren<ItemCard*>())
            QVERIFY2(card->width() <= tokens::kCardMaxWidth,
                     qPrintable(QString("card is %1px wide").arg(card->width())));
    }

    void theWindowOpensWideEnoughForTwoColumns()
    {
        // Napkin opening one column wide reads as a list of notes, which §1
        // says it is not. The opening width is therefore a claim about the
        // board, and this is the claim being checked — not the number.
        std::vector<Item> items;
        for (int i = 0; i < 6; ++i) {
            Item item = Item::makeText(QStringLiteral("x"));
            item.id = ItemId(i + 1);
            items.push_back(item);
        }
        BoardLayout wide;
        wide.setViewport(tokens::boardWidthForColumns(2));
        wide.rebuild(items);
        QCOMPARE(wide.columnCount(), 2);

        // One pixel less is one column: the width is a threshold, so a helper
        // that returned a merely generous number would pass the line above and
        // still let the window open on a single column.
        BoardLayout narrow;
        narrow.setViewport(tokens::boardWidthForColumns(2) - 1);
        narrow.rebuild(items);
        QCOMPARE(narrow.columnCount(), 1);

        // And it survives the real widgets — splitter, scrollbar and frame —
        // rather than only the arithmetic. Qt's offscreen platform reports an
        // 800px screen and MainWindow clamps its opening size to the screen,
        // so the width it wanted has to be asked for again here.
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("left")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("right")));
        f.model()->reload();
        f.select(id);

        auto* splitter = f.window.findChild<QSplitter*>();
        QVERIFY(splitter);
        f.window.resize(tokens::kWindowMargin + splitter->sizes().value(0)
                            + f.canvas()->widthForColumns(2) + splitter->handleWidth(),
                        760);
        QTest::qWait(50);

        const auto cards = f.canvas()->findChildren<TextItemCard*>();
        QCOMPARE(cards.size(), 2);
        QVERIFY2(cards.at(0)->x() != cards.at(1)->x(),
                 qPrintable(QString("both cards are at x=%1; the board is one column wide")
                                .arg(cards.at(0)->x())));
    }

    void aWideBoardWidensItsColumnsRatherThanAddingMore()
    {
        // Columns used to be packed at kCardMinWidth, so the reading measure
        // got WORSE the bigger the window was: 1920px gave five 280px columns
        // of about 34 characters while 1280px gave 417px columns. A board is
        // for reading, and a wide screen should not be punished for it.
        std::vector<Item> items;
        for (int i = 0; i < 12; ++i) {
            Item item = Item::makeText(QStringLiteral("x"));
            item.id = ItemId(i + 1);
            items.push_back(item);
        }

        int previous = 0;
        for (int board : {600, 940, 1260, 1580, 2220}) {
            BoardLayout layout;
            layout.setViewport(board);
            layout.rebuild(items);
            const int width = layout.columnWidth();

            QVERIFY2(width >= tokens::kCardMinWidth && width <= tokens::kCardMaxWidth,
                     qPrintable(QStringLiteral("board %1 -> column %2").arg(board).arg(width)));
            // Never pinned to the floor once there is room to be wider.
            if (board >= 940)
                QVERIFY2(width > tokens::kCardMinWidth + 40,
                         qPrintable(QStringLiteral("board %1 -> column %2, at the minimum")
                                        .arg(board).arg(width)));
            previous = width;
        }
        Q_UNUSED(previous);
    }

    void aHugePasteIsNotFullyMeasuredJustToFindItsHeight()
    {
        // A card caps at kCardMaxHeight, so laying out the rest of a 140 KB
        // paste to find its height is work whose result is already known.
        //
        // This used to time the whole of opening the napkin against a fixed
        // 250 ms and failed intermittently on a busy machine (254 and 303 ms
        // with every core loaded — why it only showed up straight after
        // builds). Timing opening as a ratio then showed it does grow with the
        // text: not because of the measuring, which is capped, but because the
        // card's editor holds the whole note. That is recorded in SPEC §12. The
        // measuring is what this test is about, so it times the board alone,
        // as a ratio, which load cannot fake.
        auto measure = [](int lines) {
            QString text;
            for (int i = 0; i < lines; ++i) text += QStringLiteral("line %1\n").arg(i);
            Item item = Item::makeText(text);
            item.id = lines;
            BoardLayout board;
            board.setViewport(320);
            QElapsedTimer t;
            t.start();
            for (int k = 0; k < 5; ++k) { board.invalidate(item.id); board.rebuild({item}); }
            return t.nsecsElapsed();
        };
        measure(2000);                                   // first-use costs land here
        const qint64 small = measure(2000);
        const qint64 huge = measure(20000);
        QVERIFY2(huge <= small * 3 + 20'000'000,
                 qPrintable(QString("measuring 10x the text took %1 ms against %2 ms")
                                .arg(huge / 1e6).arg(small / 1e6)));

        // And the card itself is capped.
        GuiFixture f;
        const auto id = f.buffers.create();
        QString text;
        for (int i = 0; i < 20000; ++i) text += QStringLiteral("line %1\n").arg(i);
        f.service.appendTo(id, Item::makeText(text));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY(card->isClipped());
        // At the cap, give or take one line: a clipped card is trimmed to a
        // whole number of lines so the cut lands in the leading.
        QVERIFY2(card->height() <= tokens::kCardMaxHeight
                     && card->height() > tokens::kCardMaxHeight - card->fontMetrics().lineSpacing(),
                 qPrintable(QString("card is %1px, cap is %2px")
                                .arg(card->height()).arg(tokens::kCardMaxHeight)));
    }

    void aVeryLongTextCardIsCappedRatherThanOwningTheBoard()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        QString huge;
        for (int i = 0; i < 400; ++i) huge += QStringLiteral("line %1\n").arg(i);
        f.service.appendTo(id, Item::makeText(huge));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY(card->height() <= tokens::kCardMaxHeight);
        QVERIFY(card->isClipped());   // and it says so, rather than hiding it
    }

    // --- editing -------------------------------------------------------------
    void ctrlTThenTypingAddsANewTextItem()
    {
        GuiFixture f;
        const auto id = seedMixed(f);
        f.select(id);

        QTest::keyClicks(f.newTextCard(), "one more thought");
        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(id) == 5, 2000);
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("one more thought"));
    }

    void switchingBufferSavesTheOneYouAreLeaving()
    {
        GuiFixture f;
        const auto first = f.seed("first buffer");
        const auto second = f.seed("second buffer");

        f.select(first);
        QTest::keyClicks(f.newTextCard(), "an unsaved addition");
        f.select(second);   // inside the debounce window

        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(first) == 2, 2000);
        QCOMPARE(f.items.listForBuffer(first).front().text,
                 QStringLiteral("an unsaved addition"));
    }

    void theListDoesNotResortWhileYouTypeInTheCanvas()
    {
        GuiFixture f;
        const auto older = f.seed("older");
        const auto newer = f.seed("newer");
        QCOMPARE(f.model()->idAt(0), newer);

        f.select(older);
        QTest::keyClicks(f.newTextCard(), "edited");
        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(older) == 2, 2000);

        QCOMPARE(f.model()->idAt(0), newer);   // held while typing
        QVERIFY(f.model()->orderFrozen());
    }

    // Usability test, 2026-09-19: after "Clear search" a napkin was highlighted
    // in the list while the board said "Select a napkin", and clicking it did
    // nothing — the board only followed *changes* of the current row.
    void clearingASearchReopensTheHighlightedNapkin()
    {
        GuiFixture f;
        const auto pinned = f.seed("Dr. Rao 555-0142");
        f.seed("something else");
        f.service.setPinned(pinned, true);
        f.model()->reload();
        f.select(pinned);

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("zzz"));
        QTRY_VERIFY_WITH_TIMEOUT(f.model()->isSearching() && f.model()->rowCount() == 0, 2000);

        QPushButton* clear = nullptr;
        for (auto* b : f.window.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Clear search") && b->isVisible()) clear = b;
        QVERIFY(clear);
        clear->click();

        // Whatever is highlighted is what the board shows.
        QVERIFY(f.view()->currentIndex().isValid());
        const BufferId highlighted = f.model()->idAt(f.view()->currentIndex().row());
        QCOMPARE(f.canvas()->itemOrder(), [&] {
            QList<ItemId> ids;
            for (const auto& i : f.items.listForBuffer(highlighted)) ids << i.id;
            return ids;
        }());
    }

    void leavingTheTrashLeavesNothingHighlightedThatIsNotShown()
    {
        GuiFixture f;
        const auto id = f.seed("a note");
        f.select(id);
        f.window.showTrash(true);
        f.window.showTrash(false);
        // Either nothing is current, or the current row is on the board.
        if (f.view()->currentIndex().isValid())
            QVERIFY(!f.canvas()->itemOrder().isEmpty());
    }

    void clickingTheCurrentRowOpensItWhenTheBoardIsBlank()
    {
        GuiFixture f;
        const auto id = f.seed("a note");
        f.select(id);
        f.canvas()->showNothingSelected();          // however it came to be blank
        const QRect r = f.view()->visualRect(f.model()->index(f.model()->rowForId(id), 0));
        QTest::mouseClick(f.view()->viewport(), Qt::LeftButton, {}, r.center());
        QVERIFY(!f.canvas()->itemOrder().isEmpty());
    }


    void emptyingTheTrashFromTheMenuLeavesNoTrashButtonOnTheList()
    {
        GuiFixture f;
        f.seed("keep me");
        const auto doomed = f.seed("throw me away");
        f.service.trash(doomed);
        f.model()->reload();
        auto* button = f.window.findChild<QPushButton*>(QStringLiteral("emptyTrashButton"));
        QVERIFY(button);

        QTimer::singleShot(50, [] {
            for (QWidget* w : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<MessageDialog*>(w))
                    if (box->actionButton()->text() == QStringLiteral("Delete permanently"))
                        box->actionButton()->click();
        });
        f.window.emptyTrash();               // from the menu, on the napkin list
        QCOMPARE(f.buffers.countTrash(), 0);
        QVERIFY(button->isHidden());

        f.window.showTrash(true);            // and in the trash, with nothing in it
        QVERIFY(button->isHidden());
    }


    void editingANoteRefreshesItsTimeInTheList()
    {
        // Usability test: after editing, the list still said "23 minutes ago" —
        // the row kept the time from the last reload while its order was frozen.
        GuiFixture f;
        const auto id = f.seed("Call the dentist");
        const Timestamp dayAgo = nowMs() - kMsPerDay;
        f.buffers.setModifiedAt(id, dayAgo);
        f.model()->reload();
        f.select(id);
        const int row = f.model()->rowForId(id);
        QCOMPARE(f.model()->index(row, 0).data(BufferListModel::ModifiedAtRole).toLongLong(), dayAgo);

        auto* card = f.window.findChildren<TextItemCard*>().first();
        card->beginEditing();
        QTest::keyClicks(card->findChild<QPlainTextEdit*>(), " today");
        QTRY_VERIFY_WITH_TIMEOUT(
            f.model()->index(f.model()->rowForId(id), 0).data(BufferListModel::ModifiedAtRole)
                .toLongLong() > dayAgo, 4000);
    }


    void tabbingOntoTheBoardShowsWhereTheKeyboardIs()
    {
        // Usability test: after Tab reached the board nothing marked where
        // the keyboard was, because no card was current.
        GuiFixture f;
        const auto id = f.seed("one");
        f.service.appendTo(id, Item::makeText(QStringLiteral("two")));
        f.model()->reload();
        f.select(id);
        QFocusEvent in(QEvent::FocusIn, Qt::TabFocusReason);
        QApplication::sendEvent(f.canvas(), &in);
        int current = 0;
        for (auto* card : f.canvas()->findChildren<ItemCard*>()) current += card->isCurrent();
        QCOMPARE(current, 1);
        QVERIFY(f.canvas()->selection().isEmpty());   // marked, not selected
    }


    void escapeOnAnEmptyNewNoteLeavesNoBlankCard()
    {
        GuiFixture f;
        const auto id = f.seed("existing note");
        f.select(id);
        f.canvas()->addPendingTextCard();
        QCOMPARE(f.window.findChildren<TextItemCard*>().size(), 2);
        QTest::keyClick(f.editor(), Qt::Key_Escape);
        QTRY_COMPARE_WITH_TIMEOUT(f.canvas()->findChildren<TextItemCard*>().size(), 1, 2000);
        QCOMPARE(int(f.items.listForBuffer(id).size()), 1);

        // And on a brand-new napkin the board goes back to "Nothing here yet".
        f.trigger("newBufferAction");
        f.canvas()->addPendingTextCard();
        QTest::keyClick(f.editor(), Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(f.canvas()->findChildren<TextItemCard*>().isEmpty(), 2000);
        QVERIFY(f.canvas()->startsNoteOnTyping());
    }


    void aNapkinKeepsTheTitleOfWhatWasPutOnItFirst()
    {
        // Usability test: the list title followed every addition and edit, so a
        // napkin could not be recognised from one look to the next.
        GuiFixture f;
        const auto id = f.seed("Call the dentist");
        f.service.appendTo(id, Item::makeText(QStringLiteral("Dr. Rao 555-0142")));
        f.model()->invalidatePreview(id);
        const auto title = [&] {
            return f.model()->index(f.model()->rowForId(id), 0).data(BufferListModel::PrimaryRole).toString();
        };
        QCOMPARE(title(), QStringLiteral("Call the dentist"));
        const ItemId later = f.items.listForBuffer(id).front().id;   // newest first on the board
        f.service.updateTextItem(id, later, QStringLiteral("Dr. Rao 555-0199"));
        f.model()->invalidatePreview(id);
        QCOMPARE(title(), QStringLiteral("Call the dentist"));
    }


    void restoringANapkinTakesItOffTheTrashBoardAndSaysSo()
    {
        // Second usability test: after Restore the board kept showing the
        // napkin under TRASH, beside a list that no longer held it.
        GuiFixture f;
        const auto id = f.seed("Packing list for the trip");
        f.service.trash(id);
        f.window.showTrash(true);
        f.select(id);
        QVERIFY(!f.canvas()->itemOrder().isEmpty());
        f.window.restoreRow(f.model()->rowForId(id));
        QVERIFY(f.canvas()->itemOrder().isEmpty());
        QVERIFY(!f.buffers.find(id)->inTrash());
        QString said;
        for (auto* l : f.toast()->findChildren<QLabel*>()) if (!l->text().isEmpty()) said = l->text();
        QCOMPARE(said, QStringLiteral("“Packing list for the trip” restored"));
    }


    // --- Consolidated multi-item and search tests ---
    void editingAnExistingTextItemDoesNotCreateADuplicate()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 0);
        f.select(id);

        // A block is read-only until you ask to edit it, so that a click can
        // select it. Double-click is the gesture; this is its keyboard twin.
        auto* card = f.canvas()->findChildren<TextItemCard*>()[0];
        card->beginEditing();
        QTest::keyClicks(card->findChild<QPlainTextEdit*>(), " - amended");
        QTest::qWait(600);

        QCOMPARE(f.items.countForBuffer(id), 1);   // updated in place
        QVERIFY(f.items.listForBuffer(id)[0].text.endsWith(QStringLiteral(" - amended")));
    }

    void continuedTypingAfterADraftCommitsLandsAtTheCaretNotTheStart()
    {
        // Regression: rebuilding the editor widgets after the first write reset
        // the caret to position 0, so the next keystrokes were prepended.
        GuiFixture f;
        f.trigger("newBufferAction");
        auto* edit = f.newTextCard();
        QTest::keyClicks(edit, "first");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 2000);

        QTest::keyClicks(edit, " and second");
        QTest::qWait(600);

        const auto id = f.buffers.listLive(10).front().id;
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("first and second"));
        QCOMPARE(f.items.countForBuffer(id), 1);
    }

    void typingWithTheListFocusedOnAnEmptyNapkinIsNotAListCommand()
    {
        // "P" and "K" are list commands; on an empty napkin they are letters.
        GuiFixture f;
        f.trigger("newBufferAction");
        QTest::keyClicks(f.view(), "Pack keys");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 3000);
        const auto napkin = f.buffers.listLive(5).front();
        QVERIFY(!napkin.pinned);
        QVERIFY(!napkin.kept);
        QCOMPARE(f.items.listForBuffer(napkin.id).front().text, QStringLiteral("Pack keys"));
    }

    void typingOnAFullNapkinInTheListWritesOnItAndPinsNothing()
    {
        // Second usability test: typing "pack" into the list pinned AND kept
        // the napkin, silently. Letters are text now, everywhere.
        GuiFixture f;
        const auto id = seedMixed(f, 0);
        f.select(id);
        QTest::keyClick(f.view(), 'p');
        auto* note = f.editor();
        QVERIFY2(note, "typing did not start a note");
        QTest::keyClicks(note, "ack the charger");
        QTRY_COMPARE_WITH_TIMEOUT(int(f.items.listForBuffer(id).size()), 2, 3000);
        QVERIFY(!f.buffers.find(id)->pinned);
        QVERIFY(!f.buffers.find(id)->kept);
        bool found = false;
        for (const auto& i : f.items.listForBuffer(id)) found |= i.text == QStringLiteral("pack the charger");
        QVERIFY(found);
    }

    void pinAndKeepHaveShortcutsAToastAndUndo()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 0);
        f.select(id);
        f.trigger("pinAction");
        QVERIFY(f.buffers.find(id)->pinned);
        QCOMPARE(toastText(f), QStringLiteral("Pinned “Investigate this bug” — it stays at the top"));
        f.trigger("undoAction");                     // Ctrl+Z
        QVERIFY(!f.buffers.find(id)->pinned);

        f.trigger("keepAction");
        QVERIFY(f.buffers.find(id)->kept);
        QVERIFY(toastText(f).startsWith(QStringLiteral("Kept")));
        auto* pinAction = f.window.findChild<QAction*>(QStringLiteral("pinAction"));
        auto* keepAction = f.window.findChild<QAction*>(QStringLiteral("keepAction"));
        QCOMPARE(pinAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+P")));
        QCOMPARE(keepAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+D")));
    }

    void ctrlZUndoesADeleteAfterTheToastWasMissed()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);
        const ItemId gone = f.items.listForBuffer(id)[1].id;
        f.window.removeItems({gone});
        f.trigger("undoAction");
        QCOMPARE(f.items.find(gone)->bufferId, id);
        QCOMPARE(f.buffers.countTrash(), 0);
    }

    void doubleClickingAnEmptyBoardStartsANote()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        auto* viewport = f.canvas()->viewport();
        QTest::mouseDClick(viewport, Qt::LeftButton, {}, QPoint(viewport->width() / 2, viewport->height() - 20));
        auto* edit = f.editor();
        QVERIFY(edit);
        QTest::keyClicks(edit, "hello");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 3000);
    }

    void typingOnTheWelcomeScreenStartsANote()
    {
        // The start page says "dump text here". Its first row is a focused
        // button, so letters vanished until a Space "clicked" it.
        GuiFixture f;
        auto* welcome = f.window.findChild<WelcomeView*>();
        QVERIFY(welcome);
        auto* row = welcome->findChild<QPushButton*>();
        QVERIFY(row);
        // The first letter is what reaches the start page; after it the
        // keyboard is in the new note, which is where the rest must land.
        QTest::keyClick(row, 'C', Qt::ShiftModifier);
        auto* note = f.editor();
        QVERIFY2(note, "the first letter did not start a note");
        QTest::keyClicks(note, "all the dentist");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 3000);
        const auto items = f.items.listForBuffer(f.buffers.listLive(5).front().id);
        QCOMPARE(int(items.size()), 1);
        QCOMPARE(items.front().text, QStringLiteral("Call the dentist"));
    }

    void aPastedCutLeavesNothingInTheTrash()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);
        const Item note = f.items.listForBuffer(id).back();      // the text, oldest
        QCOMPARE(note.type, ItemType::Text);
        QApplication::clipboard()->setText(note.text);           // what the cut copied
        f.window.removeItems({note.id}, /*cut=*/true);
        QCOMPARE(f.buffers.countTrash(), 1);

        f.trigger("newBufferAction");
        f.trigger("pasteAction");
        QCOMPARE(f.buffers.countTrash(), 0);                     // the original is gone
        QCOMPARE(f.buffers.countLive(), 2);                      // and the copy is here
    }

    void aMixedCutKeepsItsOriginalsBecauseThePasteIsOnlyText()
    {
        // A selection of text AND an image copies as text alone. Discarding the
        // originals after that paste would lose the image.
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.service.appendTo(id, Item::makeText(QStringLiteral("second note")));
        f.model()->reload();
        f.select(id);
        QList<ItemId> cut;
        for (const auto& i : f.items.listForBuffer(id))
            if (i.type == ItemType::Image || i.text == QStringLiteral("second note")) cut << i.id;
        QApplication::clipboard()->setText(QStringLiteral("second note"));
        f.window.removeItems(cut, /*cut=*/true);

        f.trigger("newBufferAction");
        f.trigger("pasteAction");
        QCOMPARE(f.buffers.countTrash(), 1);                     // the image is still recoverable
    }

    void aCutIsKeptIfTheClipboardHasMovedOn()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);
        const Item note = f.items.listForBuffer(id).back();
        QApplication::clipboard()->setText(note.text);
        f.window.removeItems({note.id}, /*cut=*/true);
        QApplication::clipboard()->setText(QStringLiteral("copied elsewhere since"));
        f.trigger("newBufferAction");
        f.trigger("pasteAction");
        QCOMPARE(f.buffers.countTrash(), 1);                     // not what was cut: keep it
    }

    void pastingTextIntoAnEmptySearchBoxPutsItOnTheNapkin()
    {
        GuiFixture f;
        auto* search = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        QApplication::clipboard()->setText(QStringLiteral("Remember the milk"));
        QTest::keyClick(search, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(search->text().isEmpty());                        // not a search for it
        QCOMPARE(f.buffers.countLive(), 1);
        const auto id = f.buffers.listLive(5).front().id;
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("Remember the milk"));
    }

    void pastingAnImageWithTheSearchBoxFocusedPutsItOnTheNapkin()
    {
        GuiFixture f;
        auto* search = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        auto* mime = new QMimeData;
        mime->setData(QStringLiteral("image/png"), png(40, 30, Qt::red));
        QApplication::clipboard()->setMimeData(mime);
        QTest::keyClick(search, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(f.buffers.countLive(), 1);                       // it used to vanish
        QCOMPARE(f.items.listForBuffer(f.buffers.listLive(5).front().id).front().type, ItemType::Image);
    }

    void pastingIntoAQueryBeingEditedStillEditsTheQuery()
    {
        GuiFixture f;
        f.seed("something");
        auto* search = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        search->setText(QStringLiteral("some"));
        QApplication::clipboard()->setText(QStringLiteral("thing"));
        QTest::keyClick(search, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(search->text(), QStringLiteral("something"));
        QCOMPARE(f.buffers.countLive(), 1);                       // nothing new was made
    }

    void ctrlShiftVPastesIntoTheSearchBox()
    {
        GuiFixture f;
        f.seed("buy more milk");
        auto* search = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        QApplication::clipboard()->setText(QStringLiteral("more\nmilk"));
        QTest::keyClick(search, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(search->text(), QStringLiteral("more milk"));    // one line
        QCOMPARE(f.buffers.countLive(), 1);                       // nothing was pasted as an item
    }

    void pastingACutWithdrawsOnlyTheCutsOffer()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (const char* t : {"keep me", "delete me", "cut me"})
            f.service.appendTo(id, Item::makeText(QString::fromLatin1(t)));
        f.model()->reload();
        f.select(id);

        auto pick = [&](const QString& text) {
            for (const auto& item : f.items.listForBuffer(id))
                if (item.text == text) {
                    f.canvas()->setCursorTo(f.canvas()->indexOf(item.id), Qt::NoModifier);
                    return;
                }
        };
        pick(QStringLiteral("delete me"));
        f.canvas()->deleteSelection();
        pick(QStringLiteral("cut me"));
        f.canvas()->cutSelection();
        QCOMPARE(f.toast()->offerCount(), 2);

        f.trigger("pasteAction");
        QCOMPARE(f.toast()->offerCount(), 1);
        QVERIFY(toastText(f).contains(QStringLiteral("moved to trash")));
        QVERIFY(f.toast()->undoNow());
        bool back = false;
        for (const auto& item : f.items.listForBuffer(id))
            if (item.text == QStringLiteral("delete me")) back = true;
        QVERIFY2(back, "the earlier delete was not the offer left standing");
    }

    void cutThenDeleteThenPasteThenUndoAllLeavesOneCopy()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (const char* t : {"keep me", "delete me", "cut me"})
            f.service.appendTo(id, Item::makeText(QString::fromLatin1(t)));
        f.model()->reload();
        f.select(id);
        auto pick = [&](const QString& text) {
            for (const auto& item : f.items.listForBuffer(id))
                if (item.text == text)
                    f.canvas()->setCursorTo(f.canvas()->indexOf(item.id), Qt::NoModifier);
        };
        pick(QStringLiteral("cut me"));
        f.canvas()->cutSelection();
        pick(QStringLiteral("delete me"));
        f.canvas()->deleteSelection();
        f.trigger("pasteAction");
        QCOMPARE(f.toast()->offerCount(), 1);   // the cut is finished; the delete stays

        while (f.toast()->undoNow()) {}
        int copies = 0;
        for (const auto& b : f.buffers.listLive(100))
            for (const auto& item : f.items.listForBuffer(b.id))
                if (item.text == QStringLiteral("cut me")) ++copies;
        QCOMPARE(copies, 1);
    }

    void theListRowShowsWhatWasAddedLast()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("Groceries")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("eggs")));
        f.model()->reload();
        const auto row = [&] { return f.model()->index(f.model()->rowForId(id), 0); };
        QCOMPARE(row().data(BufferListModel::PrimaryRole).toString(), QStringLiteral("Groceries"));
        QCOMPARE(row().data(BufferListModel::LatestRole).toString(), QStringLiteral("eggs"));

        QTest::qWait(5);   // a later modified_at
        f.service.appendTo(id, Item::makeText(QStringLiteral("milk")));
        f.model()->invalidatePreview(id);
        QCOMPARE(row().data(BufferListModel::LatestRole).toString(), QStringLiteral("milk"));
        QCOMPARE(row().data(BufferListModel::PrimaryRole).toString(), QStringLiteral("Groceries"));
    }

    void aNameIsTheTitleAndClearingItGoesBackToTheContents()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("Groceries")));
        QTest::qWait(5);
        f.service.appendTo(id, Item::makeText(QStringLiteral("eggs")));
        f.model()->reload();
        const auto row = [&] { return f.model()->index(f.model()->rowForId(id), 0); };
        const Timestamp before = f.buffers.find(id)->modifiedAt;

        f.window.renameNapkin(id, QStringLiteral("  Weekend   shop "));
        QCOMPARE(f.buffers.find(id)->name, QStringLiteral("Weekend shop"));   // tidied
        QCOMPARE(row().data(BufferListModel::PrimaryRole).toString(), QStringLiteral("Weekend shop"));
        QCOMPARE(f.buffers.find(id)->modifiedAt, before);   // naming is not editing
        QVERIFY(toastText(f).contains(QStringLiteral("Weekend shop")));

        f.window.renameNapkin(id, QString());
        QVERIFY(f.buffers.find(id)->name.isEmpty());
        QCOMPARE(row().data(BufferListModel::PrimaryRole).toString(), QStringLiteral("Groceries"));

        QVERIFY(f.toast()->undoNow());   // back to the name
        QCOMPARE(row().data(BufferListModel::PrimaryRole).toString(), QStringLiteral("Weekend shop"));
        QVERIFY(f.toast()->undoNow());   // and to no name
        QVERIFY(f.buffers.find(id)->name.isEmpty());
    }

    void aNamedNapkinShowsItsFirstNoteAsTheLatestToo()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("eggs")));
        QTest::qWait(5);
        f.service.appendTo(id, Item::makeText(QStringLiteral("Groceries")));
        f.model()->reload();
        // Unnamed, the newest ("Groceries") is not the title ("eggs"): shown.
        const auto row = [&] { return f.model()->index(f.model()->rowForId(id), 0); };
        QCOMPARE(row().data(BufferListModel::LatestRole).toString(), QStringLiteral("Groceries"));
        // Named, nothing on it is the title any more, so the newest always shows.
        QTest::qWait(5);
        f.service.updateTextItem(id, f.items.listForBuffer(id).back().id, QStringLiteral("eggs!"));
        f.window.renameNapkin(id, QStringLiteral("Shop"));
        QCOMPARE(row().data(BufferListModel::LatestRole).toString(), QStringLiteral("eggs!"));
    }

    void searchFindsANapkinByItsNameAndShowsAllOfIt()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("eggs")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("milk")));
        f.seed("unrelated");
        f.model()->reload();
        f.window.renameNapkin(id, QStringLiteral("Weekend shop"));

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("weekend"));
        QTRY_COMPARE_WITH_TIMEOUT(f.model()->rowCount(), 1, 2000);
        QCOMPARE(f.model()->idAt(0), id);
        f.select(id);
        QVERIFY2(!f.canvas()->isFiltered(), "a napkin found by name was filtered to nothing");
        QCOMPARE(f.canvas()->totalCount(), 2);
    }

    void f2AsksForANameWithTheAutomaticTitleAsTheHint()
    {
        GuiFixture f;
        const auto id = f.seed("Groceries");
        f.model()->reload();
        f.select(id);
        QString hint;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (auto* field = dialog->findChild<QLineEdit*>()) hint = field->placeholderText();
            dialog->setTextValue(QStringLiteral("Shop"));
            dialog->accept();
        });
        f.trigger("renameAction");
        QCOMPARE(hint, QStringLiteral("Groceries"));
        QCOMPARE(f.buffers.find(id)->name, QStringLiteral("Shop"));
    }

    // --- Consolidated review regression tests ---
    void editingAnOlderCardThenAppendingMustNotRebindTheOthers()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (const char* t : {"AAA", "BBB", "CCC"})
            f.service.appendTo(id, Item::makeText(QString::fromUtf8(t)));
        f.model()->reload();
        f.select(id);

        // Edit the OLDEST card. That bumps its modified_at, so the database
        // order changes while the widget order deliberately does not.
        TextItemCard* oldest = nullptr;
        for (auto* c : f.canvas()->findChildren<TextItemCard*>())
            if (c->text() == QStringLiteral("AAA")) oldest = c;
        QVERIFY(oldest);
        oldest->beginEditing();
        QTest::keyClicks(oldest->findChild<QPlainTextEdit*>(), "-edited");
        QTest::qWait(600);

        // Now append a new card, which is what triggers the rebind.
        QTest::keyClicks(f.newTextCard(), "NEW");
        QTest::qWait(600);

        // Every widget must still be bound to the row whose text it shows.
        for (auto* c : f.canvas()->findChildren<TextItemCard*>()) {
            if (c->isComposer()) continue;
            const auto row = f.items.find(c->itemId());
            QVERIFY(row.has_value());
            QCOMPARE(c->text(), row->text);
        }
    }

    void aFailingReadDuringARowClickDoesNotTerminate()
    {
        GuiFixture f;
        f.seed("one"); f.seed("two");
        f.db.exec("DROP TABLE items");

        bool threw = false;
        try { f.window.selectBuffer(0); } catch (...) { threw = true; }
        QVERIFY2(threw, "selectBuffer still throws — the boundary must be at notify()");
    }

    void shiftArrowExtendsAndSpaceToggles()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 4; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("card %1").arg(i)));
        f.model()->reload();
        f.select(id);
        f.canvas()->setFocus(Qt::OtherFocusReason);

        QTest::keyClick(f.canvas(), Qt::Key_Down);
        QTest::keyClick(f.canvas(), Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(f.canvas()->selection().size(), 2);

        QTest::keyClick(f.canvas(), Qt::Key_Space);
        QCOMPARE(f.canvas()->selection().size(), 1);
    }

    void aClippedCardSaysSo()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        QString huge;
        for (int i = 0; i < 200; ++i) huge += QStringLiteral("line %1\n").arg(i);
        f.service.appendTo(id, Item::makeText(huge));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY(card->isClipped());
        auto* footer = card->findChild<CardFooter*>();
        QVERIFY(footer);
        QVERIFY2(!footer->toolTip().isEmpty(),
                 "a clipped card must announce that there is more in it");
    }

    void copyingAcknowledgesItself_data()
    {
        QTest::addColumn<QString>("route");
        QTest::newRow("footer button")   << QStringLiteral("button");
        QTest::newRow("Ctrl+C")          << QStringLiteral("key");
        QTest::newRow("Ctrl+C by focus") << QStringLiteral("focus");
        QTest::newRow("context menu")    << QStringLiteral("verb");
    }

    void copyingAcknowledgesItself()
    {
        QFETCH(QString, route);

        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("copy me")));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        auto* footer = card->findChild<CardFooter*>();
        QVERIFY(footer);

        if (route == QStringLiteral("button")) {
            emit card->copyRequested(card->itemId());
        } else if (route == QStringLiteral("focus")) {
            // The others hand the key straight to the canvas, which proves the
            // handler works but NOT that a keypress ever reaches it. Here the
            // card is clicked and the key goes to whatever actually holds
            // focus, so the walk up the parent chain is exercised too.
            // window()->focusWidget(), not QApplication::focusWidget(), which
            // is null for a window no headless platform can activate.
            QTest::mouseClick(card, Qt::LeftButton);
            QWidget* focus = f.window.focusWidget();
            QVERIFY2(focus, "clicking a card left the keyboard nowhere");
            QTest::keyClick(focus, Qt::Key_C, Qt::ControlModifier);
        } else {
            // Both other routes act on the selection, so there has to be one.
            f.canvas()->setCursorTo(0, Qt::NoModifier);
            QCOMPARE(f.canvas()->selection().size(), 1);
            if (route == QStringLiteral("key"))
                QTest::keyClick(f.canvas(), Qt::Key_C, Qt::ControlModifier);
            else
                f.canvas()->copySelection();   // the body of the menu's action
        }

        // Copying changes nothing on screen otherwise, so there is no way to
        // know it worked. It says so on the button that was pressed, not in
        // place of the age at the far end of the footer.
        QCOMPARE(footer->shownActionLabel(), QStringLiteral("Copied"));
        QVERIFY(!footer->isFlashing());
        QTRY_COMPARE_WITH_TIMEOUT(footer->shownActionLabel(), QStringLiteral("Copy text"), 3000);
    }

    void copyingSeveralItemsSaysSoInTheToast()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("first")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("second")));
        f.model()->reload();
        f.select(id);

        f.canvas()->selectAll();
        QCOMPARE(f.canvas()->selection().size(), 2);
        f.canvas()->copySelection();

        QVERIFY(f.toast()->isVisible());
        QString said;
        for (auto* l : f.toast()->findChildren<QLabel*>())
            if (!l->text().isEmpty()) said = l->text();
        QCOMPARE(said, QStringLiteral("2 items copied"));
        // Nothing was destroyed, so the toast must not offer to put it back.
        QVERIFY(!f.toast()->hasOffer());
    }

    void anAcknowledgementDoesNotCancelALiveUndo()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("delete me")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("keep me")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("and me")));
        f.model()->reload();
        f.select(id);

        const auto doomed = f.canvas()->itemOrder().first();
        f.window.removeItems({doomed});
        QVERIFY2(f.toast()->hasOffer(), "a delete must offer Undo");

        // A copy happens while that offer is still standing. Two items must
        // survive the delete: a single-item copy speaks on its own card and
        // never reaches the toast, so it could not show this either way.
        f.canvas()->selectAll();
        QCOMPARE(f.canvas()->selection().size(), 2);
        f.canvas()->copySelection();

        QVERIFY2(f.toast()->hasOffer(),
                 "copying destroyed nothing, so it must not destroy the Undo");
        QVERIFY(f.toast()->undoNow());   // and Ctrl+Z still works
    }

    void pastingTextIntoAnOlderNapkinMovesItToTheTop()
    {
        GuiFixture f;
        const auto old = f.seed("from last year");
        f.buffers.setModifiedAt(old, nowMs() - 365 * kMsPerDay);
        // The others an hour old, so a paste in the same millisecond as a seed
        // cannot tie with it — a tie sorts by id, and this test flaked on it.
        f.buffers.setModifiedAt(f.seed("yesterday's list"), nowMs() - 3600 * 1000);
        f.buffers.setModifiedAt(f.seed("this morning"), nowMs() - 3600 * 1000);
        f.model()->reload();
        QVERIFY(f.model()->rowForId(old) > 0);
        QCOMPARE(f.model()->index(f.model()->rowForId(old), 0)
                     .data(BufferListModel::SectionNameRole).toString(), QStringLiteral("OLDER"));
        f.select(old);

        QApplication::clipboard()->setText(QStringLiteral("pasted today"));
        f.window.pasteFromClipboard();

        QCOMPARE(f.model()->rowForId(old), 0);
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::SectionNameRole).toString(),
                 QStringLiteral("RECENT"));
        // Moving it must not lose it: still selected, still on the board.
        QCOMPARE(f.model()->idAt(f.view()->currentIndex().row()), old);
        QCOMPARE(f.items.countForBuffer(old), 2);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 2);
    }

    void pastingAnImageIntoAnOlderNapkinMovesItToTheTop()
    {
        GuiFixture f;
        const auto old = f.seed("needs a picture");
        f.buffers.setModifiedAt(old, nowMs() - kMsPerDay);
        f.buffers.setModifiedAt(f.seed("newer"), nowMs() - 3600 * 1000);
        f.model()->reload();
        QCOMPARE(f.model()->rowForId(old), 1);
        f.select(old);

        QApplication::clipboard()->setImage(QImage::fromData(png(8, 8)));
        f.window.pasteFromClipboard();

        QCOMPARE(f.items.countForBuffer(old), 2);
        QCOMPARE(f.model()->rowForId(old), 0);
        QCOMPARE(f.model()->idAt(f.view()->currentIndex().row()), old);
    }

    void savingAnEditAcknowledgesItselfAndResetsTheAge()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("before")));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        QTest::keyClicks(card->findChild<QPlainTextEdit*>(), " and after");
        QTRY_VERIFY_WITH_TIMEOUT(card->findChild<CardFooter*>()->isFlashing(), 3000);
    }

    void aComposerLooksLikeEveryOtherCard()
    {
        GuiFixture f;
        const auto id = f.seed("something");
        f.select(id);
        f.canvas()->addPendingTextCard();

        for (auto* card : f.canvas()->findChildren<TextItemCard*>())
            QVERIFY2(card->findChild<CardFooter*>(),
                     "a card made with Ctrl+T had no copy action and no age, so it "
                     "was visibly a different kind of object from every other card");
    }

    void highlightingSearchTermsDoesNotMarkCardsAsEdited()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("restart nginx now")));
        f.service.appendTo(id, Item::makeText(QStringLiteral("nginx config path")));
        f.seed("unrelated");
        f.model()->reload();

        auto* field = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        field->setText(QStringLiteral("nginx"));
        QTRY_VERIFY_WITH_TIMEOUT(f.canvas()->isFiltered(), 2000);
        QTest::qWait(400);

        // The highlighter reformats the whole document, which emits
        // textChanged — and marked every visible card dirty, autosaved it, and
        // flashed "Saved" on cards nobody had touched.
        for (auto* card : f.canvas()->findChildren<TextItemCard*>())
            QVERIFY2(!card->isDirty(), "a highlighted card was marked as edited");
        QVERIFY(f.canvas()->dirtyText().empty());
    }

    void thePreviewCacheIsBounded()
    {
        GuiFixture f;
        for (int i = 0; i < 60; ++i) f.seed("buffer");
        f.model()->reload();
        for (int row = 0; row < f.model()->rowCount(); ++row)
            f.model()->index(row, 0).data(BufferListModel::PrimaryRole);
        QVERIFY(f.model()->previewCacheSize() <= 400);
    }

    void aFailedSearchLeavesTheListAsItWas()
    {
        // On SQLite 3.35–3.38 every search with a hit threw from inside
        // beginResetModel()/endResetModel(). The reset was never finished, the
        // rows were already cleared, and the model claimed to be searching for
        // a query it had no results for. A missing index stands in for any
        // failure the query can meet.
        GuiFixture f;
        f.seed("systemctl restart nginx");
        f.seed("sudo pacman -Syu");
        QCOMPARE(f.model()->rowCount(), 2);

        QSignalSpy aboutToReset(f.model(), &QAbstractItemModel::modelAboutToBeReset);
        QSignalSpy reset(f.model(), &QAbstractItemModel::modelReset);
        f.db.exec("DROP TABLE items_fts");

        QVERIFY_THROWS_EXCEPTION(std::exception, f.model()->setQuery(QStringLiteral("nginx")));
        QCOMPARE(aboutToReset.count(), reset.count());
        QCOMPARE(f.model()->rowCount(), 2);
        QVERIFY(f.model()->query().isEmpty());
    }

    void metadataRolesDoNotTouchTheDatabase()
    {
        // sizeHint() reads IsExpandedRole for every row, and QListView asks
        // every row. Computing a preview there turned startup into thousands of
        // queries and materialised every buffer's text.
        GuiFixture f;
        for (int i = 0; i < 50; ++i) f.seed("buffer");

        f.model()->reload();          // drops the preview cache
        const int before = f.statementCount();
        for (int row = 0; row < f.model()->rowCount(); ++row) {
            f.model()->index(row, 0).data(BufferListModel::IsDraftRole);
            f.model()->index(row, 0).data(BufferListModel::PinnedRole);
            f.model()->index(row, 0).data(BufferListModel::SectionFirstRole);
        }
        QCOMPARE(f.statementCount(), before);   // not one query
    }

    // ── Restored from test_multiitem / test_review_fixes ───────────────────
    // The 2026-10 consolidation dropped these without a replacement; nothing
    // else pins the toast wording, undo order, or restore-into-origin.

    void aDeletedItemIsInTheTrashAfterTheToastIsGone()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 2);
        f.select(id);
        const auto before = f.items.listForBuffer(id);
        const ItemId gone = before[1].id;

        f.window.removeItems({gone});
        QCOMPARE(int(f.items.listForBuffer(id).size()), 2);
        QCOMPARE(toastText(f), QStringLiteral("Item moved to trash from “Investigate this bug”"));

        // The toast expiring must not matter: the item is in a trashed napkin.
        const auto trash = f.buffers.listTrash();
        QCOMPARE(int(trash.size()), 1);
        const auto held = f.items.listForBuffer(trash.front().id);
        QCOMPARE(int(held.size()), 1);
        QCOMPARE(held.front().id, gone);

        // And restoring it puts the item back where it came from — not into a
        // napkin of its own, which the second usability test found confusing.
        const Item before1 = before[1];
        QCOMPARE(f.service.restore(trash.front().id), id);
        QCOMPARE(f.buffers.countTrash(), 0);
        QCOMPARE(f.buffers.countLive(), 1);                       // no stray holder
        QCOMPARE(f.items.find(gone)->bufferId, id);
        QCOMPARE(f.items.find(gone)->position, before1.position);
        QCOMPARE(int(f.items.listForBuffer(id).size()), 3);
    }

    void aDeletedItemWhoseNapkinIsGoneRestoresOnItsOwn()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 2);
        f.select(id);
        const ItemId gone = f.items.listForBuffer(id)[1].id;
        f.window.removeItems({gone});
        // The original deleted for good. (Only trashed, it now comes back with
        // its card — restoringCardsWhoseNapkinIsInTheTrashBringsTheNapkinBack —
        // rather than the card becoming a napkin of its own, which split
        // napkins in two in the 2026-10-06 usability test.)
        f.service.trash(id);
        f.buffers.hardDeleteEvenIfKept(id);
        BufferId holder = kNoBuffer;
        for (const auto& b : f.buffers.listTrash()) if (b.id != id) holder = b.id;
        QVERIFY(holder != kNoBuffer);

        QCOMPARE(f.service.restore(holder), holder);              // nowhere to go back to
        QCOMPARE(f.items.find(gone)->bufferId, holder);
        QVERIFY(!f.buffers.restoresTo(holder));                   // and it is its own napkin now
    }

    void anImageCanBeRemovedFromInsideTheBuffer()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 2);
        f.select(id);

        const auto before = f.items.listForBuffer(id);
        QCOMPARE(int(before.size()), 3);
        const ItemId imageId = before[1].id;

        f.window.removeItems({imageId});

        const auto after = f.items.listForBuffer(id);
        QCOMPARE(int(after.size()), 2);
        for (const auto& item : after) QVERIFY(item.id != imageId);
        // And the editor redrew without it.
        QCOMPARE(f.canvas()->findChildren<ImageItemCard*>().size(), 1);
    }

    void arrowKeysMoveTheSelectionAcrossCards()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 5; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("card %1").arg(i)));
        f.model()->reload();
        f.select(id);
        f.canvas()->setFocus(Qt::OtherFocusReason);

        // Every canvas verb acts on the selection, and until this there was no
        // keyboard gesture anywhere that wrote to it — a keyboard user could
        // select all or nothing.
        QCOMPARE(f.canvas()->selection().size(), 0);
        QTest::keyClick(f.canvas(), Qt::Key_Down);
        QCOMPARE(f.canvas()->selection().size(), 1);
        QCOMPARE(f.canvas()->cursorIndex(), 0);

        QTest::keyClick(f.canvas(), Qt::Key_Down);
        QCOMPARE(f.canvas()->cursorIndex(), 1);
        QTest::keyClick(f.canvas(), Qt::Key_Up);
        QCOMPARE(f.canvas()->cursorIndex(), 0);

        QTest::keyClick(f.canvas(), Qt::Key_End);
        QCOMPARE(f.canvas()->cursorIndex(), 4);
        QTest::keyClick(f.canvas(), Qt::Key_Home);
        QCOMPARE(f.canvas()->cursorIndex(), 0);
    }

    void aSelectedCardCanBeActedOnWithTheMouseAlone()
    {
        GuiFixture f;
        const auto id = f.seed("act on me");
        const int row = f.model()->rowForId(id);

        const QRect rect = f.view()->visualRect(f.model()->index(row, 0));
        QTest::mouseClick(f.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());

        // This is the workflow that was impossible: click, then pin/keep/delete.
        f.window.togglePin(f.view()->currentIndex().row());
        QVERIFY(f.buffers.find(id)->pinned);
        f.window.toggleKeep(f.model()->rowForId(id));
        QVERIFY(f.buffers.find(id)->kept);
    }

    void clickingSelectsTheRow()
    {
        GuiFixture f;
        const auto id = f.seed("selectable");
        const int row = f.model()->rowForId(id);

        const QRect rect = f.view()->visualRect(f.model()->index(row, 0));
        QTest::mouseClick(f.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());

        QCOMPARE(f.view()->currentIndex().row(), row);
    }

    void ctrlTAddsATextItem()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);

        QTest::keyClicks(f.newTextCard(), "a second thought");
        QTRY_VERIFY_WITH_TIMEOUT(f.items.countForBuffer(id) == 3, 2000);

        const auto items = f.items.listForBuffer(id);
        QCOMPARE(items.front().type, ItemType::Text);
        QCOMPARE(items.front().text, QStringLiteral("a second thought"));
    }

    void cutDoesNotAnnounceItselfAsADeletion()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);
        f.window.removeItems({f.items.listForBuffer(id)[1].id}, /*cut=*/true);
        QCOMPARE(toastText(f), QStringLiteral("Item cut from “Investigate this bug”"));
    }

    void deletingEveryItemTrashesTheNapkinWithItsContent()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);
        QList<ItemId> all;
        for (const auto& i : f.items.listForBuffer(id)) all << i.id;

        f.window.removeItems(all);
        QCOMPARE(toastText(f), QStringLiteral("Those were all its cards — “Investigate this bug” moved to trash"));
        QCOMPARE(f.buffers.countTrash(), 1);
        QCOMPARE(f.buffers.listTrash().front().id, id);          // the napkin itself
        QCOMPARE(int(f.items.listForBuffer(id).size()), 2);      // not an empty shell
    }

    void enterPutsTheCaretInTheCanvas()
    {
        GuiFixture f;
        const auto id = f.seed("keyboard");
        f.view()->setCurrentIndex(f.model()->index(f.model()->rowForId(id), 0));
        QTest::keyClick(f.view(), Qt::Key_Return);
        QVERIFY(f.window.focusWidget());
        QVERIFY(f.canvas()->isAncestorOf(f.window.focusWidget()));
    }

    void selectingABufferShowsItInTheCanvas()
    {
        GuiFixture f;
        const auto id = f.seed("show me");
        f.select(id);

        const auto texts = f.canvas()->findChildren<TextItemCard*>();
        QCOMPARE(texts.size(), 1);
        QCOMPARE(texts[0]->text(), QStringLiteral("show me"));
    }

    void theExpandedEditorRendersEveryItem()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 3);   // 1 text + 3 images
        f.select(id);

        auto* editor = f.canvas();
        QVERIFY(editor);
        const auto images = editor->findChildren<ImageItemCard*>();
        const auto texts = editor->findChildren<TextItemCard*>();

        QCOMPARE(images.size(), 3);          // all three, not just the first
        QCOMPARE(texts.size(), 1);
        QCOMPARE(texts[0]->text(), QStringLiteral("Investigate this bug"));
    }

    void theSweepReclaimsABlobOnceNothingReferencesIt()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 1);
        f.select(id);

        Item item;
        for (const auto& i : f.items.listForBuffer(id))
            if (i.type == ItemType::Image) item = i;
        QVERIFY(f.blobs.exists(item.blobHash, item.mime));

        f.window.removeItems({item.id});
        QVERIFY(f.blobs.exists(item.blobHash, item.mime));   // undo may still need it

        // A deleted item is in the trash, so a sweep must leave its image alone.
        reconcileBlobs(f.items, f.blobs, f.thumbsDir());
        QVERIFY(f.blobs.exists(item.blobHash, item.mime));

        // Emptying the trash is what finally lets it go.
        f.service.emptyTrash();
        reconcileBlobs(f.items, f.blobs, f.thumbsDir());
        QVERIFY(!f.blobs.exists(item.blobHash, item.mime));
    }

    void typingAfterCtrlNStartsANote()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        QTest::keyClicks(f.canvas(), "Call the dentist");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 3000);
        const auto items = f.items.listForBuffer(f.buffers.listLive(5).front().id);
        QCOMPARE(int(items.size()), 1);
        QCOMPARE(items.front().text, QStringLiteral("Call the dentist"));
    }

    void undoPutsTheItemBackWhereItWasAndLeavesNothingInTheTrash()
    {
        GuiFixture f;
        const auto id = seedMixed(f, 2);
        f.select(id);
        const auto before = f.items.listForBuffer(id);

        f.window.removeItems({before[1].id});
        f.toast()->findChild<QPushButton*>()->click();

        const auto after = f.items.listForBuffer(id);
        QCOMPARE(int(after.size()), 3);
        for (size_t i = 0; i < before.size(); ++i) {
            QCOMPARE(after[i].id, before[i].id);             // same order
            QCOMPARE(after[i].position, before[i].position);
        }
        QCOMPARE(f.buffers.countTrash(), 0);                  // no stray holder
        QCOMPARE(f.buffers.countLive(), 1);
    }
};

QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"
