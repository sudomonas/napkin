#include "../src/ui/BoardLayout.h"
#include "GuiFixture.h"
#include "../src/domain/Clock.h"
#include "../src/media/BlobGc.h"
#include "../src/ui/Tokens.h"
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
#include <QLineEdit>
#include <QPushButton>
#include "../src/ui/WelcomeView.h"

#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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

private slots:
    void bothPanesExist()
    {
        GuiFixture f;
        auto* splitter = f.window.findChild<QSplitter*>();
        QVERIFY(splitter);
        QCOMPARE(splitter->count(), 2);
        QVERIFY(f.canvas());
    }

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

        QVERIFY(QFile::remove(f.blobs.pathFor(stored.hash, stored.mime)));
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
        QVERIFY(QFile::remove(f.blobs.pathFor(stored.hash, stored.mime)));

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

    // --- the menu bar --------------------------------------------------------
    void everyActionIsReachableFromTheMenuBar()
    {
        GuiFixture f;
        QStringList menus;
        for (auto* action : f.window.menuBar()->actions()) menus << action->text();
        QCOMPARE(menus.size(), 4);
        QVERIFY(menus.join(QLatin1Char('|')).contains(QStringLiteral("File")));
        QVERIFY(menus.join(QLatin1Char('|')).contains(QStringLiteral("Napkins")));
        QVERIFY(menus.join(QLatin1Char('|')).contains(QStringLiteral("Trash")));
        QVERIFY(menus.join(QLatin1Char('|')).contains(QStringLiteral("Help")));

        // The menu is where a user finds out what the app can do, so every
        // shortcut must be listed rather than only bound.
        int listed = 0;
        for (auto* menuAction : f.window.menuBar()->actions())
            if (auto* menu = menuAction->menu())
                for (auto* a : menu->actions())
                    if (!a->isSeparator() && !a->shortcut().isEmpty()) ++listed;
        QVERIFY2(listed >= 6, qPrintable(QString("only %1 shortcuts listed").arg(listed)));
    }

    void theTrashMenuAndTheHeaderToggleStayInStep()
    {
        GuiFixture f;
        f.seed("something");
        auto* toggle = f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"));
        QVERIFY(toggle);

        QAction* showTrash = nullptr;
        for (auto* menuAction : f.window.menuBar()->actions())
            if (auto* menu = menuAction->menu())
                for (auto* a : menu->actions())
                    if (a->text().contains(QStringLiteral("Show trash"))) showTrash = a;
        QVERIFY(showTrash);

        showTrash->setChecked(true);
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);
        QVERIFY(toggle->isChecked());

        toggle->setChecked(false);
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QVERIFY(!showTrash->isChecked());
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
        for (auto* menuAction : f.window.menuBar()->actions())
            if (auto* menu = menuAction->menu())
                for (auto* a : menu->actions())
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
                if (auto* box = qobject_cast<QMessageBox*>(w))
                    for (auto* b : box->buttons())
                        if (b->text() == QStringLiteral("Delete permanently")) b->click();
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
        QCOMPARE(said, QStringLiteral("Napkin restored"));
    }

};

QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"
