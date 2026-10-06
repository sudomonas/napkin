
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include "GuiFixture.h"
#include "../src/ui/BackgroundSaver.h"
#include "../src/domain/Clock.h"
#include <QtTest>
#include <QTimer>
#include <chrono>
#include <memory>
#include <thread>

using namespace napkin;

// Drives the real widget tree offscreen. These are the Phase 2 behaviours that
// only exist once the UI is wired up, so unit-testing the layers below would
// not have caught any of them.
namespace {
using namespace napkin;

// A third connection holding the write lock keeps a background save in
// flight for as long as a test needs; Napkin has no other writer to race.
struct WriteLock {
    napkin::Database db;
    explicit WriteLock(napkin::Database& of)
    {
        db.open(QString::fromUtf8(sqlite3_db_filename(of.handle(), "main")));
        db.exec("BEGIN IMMEDIATE;");
    }
    void release() { db.exec("COMMIT;"); }
};

// Opens a large note for editing, caret at the end.
TextItemCard* openLargeNote(GuiFixture& f, napkin::BufferId* id)
{
    *id = f.buffers.create();
    QString note;
    while (note.size() < 600 * 1024) note += QStringLiteral("a line of a long pasted log\n");
    f.service.appendTo(*id, Item::makeText(note));
    f.model()->reload();
    f.select(*id);
    auto* card = f.canvas()->findChildren<TextItemCard*>().first();
    card->beginEditing();
    auto* editor = card->findChild<QPlainTextEdit*>();
    editor->moveCursor(QTextCursor::End);
    return card;
}

// Typing that the debounce then hands to the worker, which `lock` holds up.
void typeAtEnd(TextItemCard* card, const QString& typed)
{
    auto* editor = card->findChild<QPlainTextEdit*>();
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(typed);
}


}  // namespace

class TestEditing : public QObject {
    Q_OBJECT
private slots:
    void ctrlNOpensAnEditorButWritesNothing()
    {
        GuiFixture f;
        f.trigger("newBufferAction");

        QVERIFY(f.newTextCard());
        QCOMPARE(f.model()->rowCount(), 1);   // a card is visible...
        QCOMPARE(f.buffers.countLive(), 0);   // ...but invariant 5 holds
    }

    void typingThenFlushingWritesExactlyOneBuffer()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        QTest::keyClicks(f.newTextCard(), "systemctl restart nginx");

        // Wait past the debounce; the autosave must fire on its own.
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 2000);
        QCOMPARE(f.model()->rowCount(), 1);

        const auto id = f.buffers.listLive(10).front().id;
        QCOMPARE(f.items.countForBuffer(id), 1);
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("systemctl restart nginx"));
    }

    void continuedTypingUpdatesTheSameBufferRatherThanMakingMore()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        auto* edit = f.newTextCard();
        QTest::keyClicks(edit, "first");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 2000);

        QTest::keyClicks(edit, " and second");   // the same card, not a new one
        QTest::qWait(600);

        QCOMPARE(f.buffers.countLive(), 1);  // still one
        const auto id = f.buffers.listLive(10).front().id;
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("first and second"));
    }

    void anAbandonedEmptyDraftEvaporates()
    {
        GuiFixture f;
        const auto existing = f.seed("something else");
        f.trigger("newBufferAction");
        QCOMPARE(f.model()->rowCount(), 2);   // the draft card is showing

        // Selecting away from an empty draft discards it: it never had a row.
        f.select(existing);

        QCOMPARE(f.model()->rowCount(), 1);
        QCOMPARE(f.buffers.countLive(), 1);
    }

    void whitespaceOnlyIsNotContent()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        QTest::keyClicks(f.newTextCard(), "   \t  ");
        QTest::qWait(600);

        QCOMPARE(f.buffers.countLive(), 0);  // invariant 5
    }

    void leavingABufferFlushesItFirst()
    {
        GuiFixture f;
        const auto other = f.seed("other");
        f.trigger("newBufferAction");
        QTest::keyClicks(f.newTextCard(), "quick note");
        f.select(other);   // immediately, inside the debounce window

        // Moving on must not cost the user the last keystrokes (SPEC.md §8).
        QCOMPARE(f.buffers.countLive(), 2);
        bool found = false;
        for (const auto& b : f.buffers.listLive(10))
            for (const auto& item : f.items.listForBuffer(b.id))
                if (item.text == QStringLiteral("quick note")) found = true;
        QVERIFY(found);
    }

    void theListDoesNotResortWhileYouAreTyping()
    {
        qint64 clock = 1'700'000'000'000LL;
        setClockForTesting([&clock] { return clock; });

        GuiFixture f;
        const auto older = f.buffers.create();
        f.service.appendTo(older, Item::makeText(QStringLiteral("older buffer")));
        clock += 60'000;
        const auto newer = f.buffers.create();
        f.service.appendTo(newer, Item::makeText(QStringLiteral("newer buffer")));

        f.model()->reload();
        QCOMPARE(f.model()->idAt(0), newer);
        QCOMPARE(f.model()->idAt(1), older);

        // Open the older card and edit it. Autosave bumps modified_at past
        // newer's, so a naive reload would yank the card you are typing into
        // to the top of the list (SPEC.md §7).
        f.view()->setCurrentIndex(f.model()->index(1, 0));
        f.model()->freezeOrder(true);
        clock += 60'000;
        const auto itemId = f.items.listForBuffer(older).front().id;
        f.service.updateTextItem(older, itemId, QStringLiteral("older buffer, edited"));
        f.model()->reload();

        QCOMPARE(f.model()->idAt(0), newer);   // order held while expanded
        QCOMPARE(f.model()->idAt(1), older);

        f.model()->freezeOrder(false);         // releasing applies the reload
        QCOMPARE(f.model()->idAt(0), older);   // and only now does it move
        QCOMPARE(f.model()->idAt(1), newer);

        resetClock();
    }

    void closingTheWindowFlushesPendingText()
    {
        GuiFixture f;
        f.trigger("newBufferAction");
        QTest::keyClicks(f.newTextCard(), "unsaved when closing");
        f.window.close();  // inside the debounce window

        QCOMPARE(f.buffers.countLive(), 1);
    }

    void emptyStateAppearsOnlyWhenThereIsNothing()
    {
        GuiFixture f;
        auto* stack = f.window.findChild<QStackedWidget*>();
        QVERIFY(stack);
        QCOMPARE(stack->currentIndex(), 1);  // empty state

        f.trigger("newBufferAction");
        QTest::keyClicks(f.newTextCard(), "content");
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 2000);
        QCOMPARE(stack->currentIndex(), 0);  // the stack
    }

    // --- a long note costs what a short one does until it is opened ----------
    // setPlainText is linear — 135 ms for 1.2 MB — and every card on screen
    // paid it on opening the napkin, for text a card clips at a few hundred
    // pixels anyway.
    void aLongNoteLoadsOnlyItsTopUntilItIsEdited()
    {
        GuiFixture f;
        QString log;
        for (int i = 0; log.size() < 400'000; ++i)
            log += QStringLiteral("line %1 of a pasted log that goes on\n").arg(i);
        log += QStringLiteral("the very end");
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(log));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        auto* editor = card->findChild<QPlainTextEdit*>();
        QVERIFY2(editor->document()->characterCount() < 5000,
                 "the whole note was loaded into a card nobody is editing");
        QVERIFY(!card->holdsWholeTextForTest());
        // Everything that reads the note still gets all of it.
        QCOMPARE(card->text(), log);
        QVERIFY(!card->isDirty());
        f.canvas()->selectAll();
        f.canvas()->copySelection();
        QVERIFY(QApplication::clipboard()->text().endsWith(QStringLiteral("the very end")));

        // Opening it for editing brings the rest, and that is not an edit.
        card->beginEditing();
        QVERIFY(card->holdsWholeTextForTest());
        QCOMPARE(editor->toPlainText(), log);
        QVERIFY2(!card->isDirty(), "loading the rest of the note was taken for typing");

        // And typing at the end lands after the real end, not after the cut.
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("!"));
        card->endEditing();   // leaving a card commits it
        QTRY_COMPARE_WITH_TIMEOUT(f.items.listForBuffer(id).front().text,
                                  log + QStringLiteral("!"), 2000);
    }

    // --- a large note saves in the background while you type -----------------
    // Saving a 10 MB note took ~140 ms on the UI thread, every two seconds of
    // continuous typing. Only the timed autosave of an existing large note goes
    // to the worker; everything else still waits for the text to land.
    static QString bigNote()
    {
        QString text;
        while (text.size() < 600 * 1024) text += QStringLiteral("a line of a long pasted log\n");
        return text;
    }

    void aTimedSaveOfALargeNoteRunsInTheBackgroundAndLands()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        QVERIFY(f.window.saverForTest()->isAvailable());
        const QString log = bigNote();
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(log));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* editor = card->findChild<QPlainTextEdit*>();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("typed"));

        QVERIFY(f.window.flushForTest(/*timed=*/true));
        QVERIFY2(f.window.saverForTest()->isBusy(), "the timed save was written on the UI thread");
        QVERIFY(card->isDirty());   // not until it has actually landed

        f.window.saverForTest()->waitForIdle();
        QVERIFY(!card->isDirty());
        QCOMPARE(f.items.listForBuffer(id).front().text, log + QStringLiteral("typed"));
    }

    void typingDuringABackgroundSaveIsNotMarkedSaved()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        const QString log = bigNote();
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(log));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* editor = card->findChild<QPlainTextEdit*>();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("one"));
        QVERIFY(f.window.flushForTest(true));
        editor->insertPlainText(QStringLiteral(" two"));   // while that is in flight

        f.window.saverForTest()->waitForIdle();
        QVERIFY2(card->isDirty(), "a save of the older text marked the newer text as saved");

        // And any flush that is not timed waits for queued work, then writes.
        QVERIFY(f.window.flushForTest(false));
        QVERIFY(!f.window.saverForTest()->isBusy());
        QCOMPARE(f.items.listForBuffer(id).front().text, log + QStringLiteral("one two"));
    }

    void anUntimedFlushNeverLeavesABackgroundSaveBehindIt()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(bigNote()));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->insertPlainText(QStringLiteral("x"));
        QVERIFY(f.window.flushForTest(true));
        QVERIFY(f.window.saverForTest()->isBusy());

        // Leaving the card, switching napkins, losing focus and quitting all
        // come through here: the text must have landed when it returns.
        QVERIFY(f.window.flushForTest(false));
        QVERIFY2(!f.window.saverForTest()->isBusy(), "returned with a save still in flight");
        QVERIFY(!card->isDirty());
    }

    void aSmallNoteStillSavesOnTheSpot()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("short")));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->insertPlainText(QStringLiteral("!"));
        QVERIFY(f.window.flushForTest(true));
        QVERIFY(!f.window.saverForTest()->isBusy());
        QVERIFY(!card->isDirty());
    }

    // A long note is not re-measured while it stays long — it is drawn at the
    // maximum height regardless — but cutting it down must still shrink it.
    void cuttingALongNoteDownShrinksItsCardWhileEditing()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(bigNote()));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        const int tall = card->height();
        card->beginEditing();
        card->selectAllText();
        card->findChild<QPlainTextEdit*>()->insertPlainText(QStringLiteral("short now"));
        QApplication::processEvents();
        QVERIFY2(card->height() < tall, qPrintable(QStringLiteral("%1 -> %2").arg(tall).arg(card->height())));
    }

    // --- the background save, driven through the real autosave timers ---------
    void overtypingWhileASaveIsInFlightIsSaved()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        napkin::BufferId id;
        auto* card = openLargeNote(f, &id);
        WriteLock lock(f.db);
        typeAtEnd(card, QStringLiteral("one"));
        QTRY_VERIFY_WITH_TIMEOUT(f.window.saverForTest()->isBusy(), 2000);

        // Select the last letter and type over it: the same length.
        auto* editor = card->findChild<QPlainTextEdit*>();
        QTextCursor c = editor->textCursor();
        c.movePosition(QTextCursor::End);
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
        editor->setTextCursor(c);
        QTest::keyClicks(editor, QStringLiteral("E"));   // keyClick(Key_E) types a lower-case e

        lock.release();
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.saverForTest()->isBusy(), 5000);
        QVERIFY(card->text().endsWith(QStringLiteral("onE")));
        QTRY_VERIFY_WITH_TIMEOUT(f.items.listForBuffer(id).front().text.endsWith(QStringLiteral("onE")),
                                 5000);
    }

    // flushNow() is what pasting an image, losing focus and closing rely on.
    void losingFocusWaitsForASaveAlreadyInFlight()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        napkin::BufferId id;
        auto* card = openLargeNote(f, &id);
        WriteLock lock(f.db);
        typeAtEnd(card, QStringLiteral("landed"));
        QTRY_VERIFY_WITH_TIMEOUT(f.window.saverForTest()->isBusy(), 2000);

        std::thread later([&lock] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            lock.release();
        });
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&f.window, &deactivate);
        later.join();
        QVERIFY2(!f.window.saverForTest()->isBusy(), "losing focus returned before the text landed");
        QVERIFY(f.items.listForBuffer(id).front().text.endsWith(QStringLiteral("landed")));
    }

    void aBoardRebuiltMidSaveKeepsTheTypingAndItsUnsavedState()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        napkin::BufferId id;
        typeAtEnd(openLargeNote(f, &id), QStringLiteral(""));
        WriteLock lock(f.db);
        typeAtEnd(f.canvas()->findChildren<TextItemCard*>().first(), QStringLiteral("SAVED"));
        QTRY_VERIFY_WITH_TIMEOUT(f.window.saverForTest()->isBusy(), 2000);

        // Rebuilt from the database, which does not have "SAVED" yet.
        f.canvas()->setItems(f.items.listForBuffer(id));
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY2(card->text().endsWith(QStringLiteral("SAVED")), "the rebuild showed older text");
        QVERIFY2(card->isDirty(), "the rebuild forgot the text was not saved yet");

        card->beginEditing();
        auto* editor = card->findChild<QPlainTextEdit*>();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("Q"));
        lock.release();
        QTRY_VERIFY_WITH_TIMEOUT(!f.window.saverForTest()->isBusy(), 5000);
        QVERIFY2(card->isDirty(), "the older save marked the newer typing as saved");

        QVERIFY(f.window.flushForTest(false));
        QVERIFY(f.items.listForBuffer(id).front().text.endsWith(QStringLiteral("SAVEDQ")));
    }

    void aBackgroundSaveThatFailsAfterARebuildLosesNothing()
    {
        GuiFixture f{GuiFixture::OnDisk{}};
        // The failure says so in a dialog; answer it.
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, [] {
            if (auto* w = QApplication::activeModalWidget()) w->close();
        });
        closer.start(50);

        napkin::BufferId id;
        auto* opened = openLargeNote(f, &id);
        auto lock = std::make_unique<WriteLock>(f.db);
        typeAtEnd(opened, QStringLiteral("KEEP"));
        QTRY_VERIFY_WITH_TIMEOUT(f.window.saverForTest()->isBusy(), 2000);
        f.canvas()->setItems(f.items.listForBuffer(id));

        // Held past busy_timeout, the worker gives up. Waiting on the failure
        // itself, not on "idle": autosave retries at once, into the same lock.
        QSignalSpy failed(f.window.saverForTest(), &BackgroundSaver::failed);
        QTRY_VERIFY_WITH_TIMEOUT(failed.size() >= 1, 8000);
        lock->release();
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY(card->text().endsWith(QStringLiteral("KEEP")));
        QVERIFY(card->isDirty());
        QVERIFY(f.window.flushForTest(false));
        QVERIFY(f.items.listForBuffer(id).front().text.endsWith(QStringLiteral("KEEP")));
    }

    void changingTheFontWhileEditingALongNoteKeepsTheTyping()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(bigNote()));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        auto* editor = card->findChild<QPlainTextEdit*>();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("mid-edit"));

        QFont bigger = f.canvas()->font();
        bigger.setPointSizeF(bigger.pointSizeF() + 3);
        f.canvas()->setFont(bigger);   // rebuilds every card

        auto* rebuilt = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY(rebuilt->text().endsWith(QStringLiteral("mid-edit")));
        QVERIFY(rebuilt->isDirty());
        QVERIFY(f.window.flushForTest(false));
        QVERIFY(f.items.listForBuffer(id).front().text.endsWith(QStringLiteral("mid-edit")));
    }

    // A search filter rebuilds every card too, and can be applied without the
    // window committing the edit first.
    void filteringTheBoardMidEditKeepsTheUnsavedTyping()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("alpha")));
        f.model()->reload();
        f.select(id);
        auto* card = f.canvas()->findChildren<TextItemCard*>().first();
        card->beginEditing();
        card->findChild<QPlainTextEdit*>()->insertPlainText(QStringLiteral(" beta"));
        const auto item = f.items.listForBuffer(id).front().id;

        f.canvas()->setSearch(QStringLiteral("alpha"), {item});   // rebuilds
        auto* rebuilt = f.canvas()->findChildren<TextItemCard*>().first();
        QVERIFY(rebuilt->text().contains(QStringLiteral("beta")));
        QVERIFY(rebuilt->isDirty());
        QVERIFY(f.window.flushForTest(false));
        QVERIFY(f.items.listForBuffer(id).front().text.contains(QStringLiteral("beta")));
    }
};

QTEST_MAIN(TestEditing)
#include "test_editing.moc"
