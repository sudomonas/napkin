
#include <QPushButton>
#include <QStackedWidget>
#include "../src/ui/EmptyStateView.h"
#include "GuiFixture.h"
#include <QTimer>
#include <QMessageBox>
#include "../src/domain/Clock.h"
#include <QtTest>

using namespace napkin;

// Phase 3 behaviour, driven through the real widget tree. Pin, Keep and Trash
// are where a mistake costs the user data, so these are the cases that matter
// most in the whole suite.
class TestLifecycle : public QObject {
    Q_OBJECT
private slots:
    void pinMovesTheCardToTheTop()
    {
        GuiFixture f;
        f.seed("older");
        const auto target = f.seed("newer");
        const auto third = f.seed("newest");
        QCOMPARE(f.model()->idAt(0), third);

        f.window.togglePin(f.model()->rowForId(target));

        QCOMPARE(f.model()->idAt(0), target);
        QVERIFY(f.buffers.find(target)->pinned);
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::SectionNameRole).toString(),
                 QStringLiteral("PINNED"));
    }

    void keepDoesNotMoveTheCard()
    {
        GuiFixture f;
        const auto first = f.seed("first");
        const auto second = f.seed("second");
        QCOMPARE(f.model()->idAt(0), second);

        f.window.toggleKeep(f.model()->rowForId(first));

        // Keep is lifecycle, not placement. The list must not reshuffle.
        QCOMPARE(f.model()->idAt(0), second);
        QCOMPARE(f.model()->idAt(1), first);
        QVERIFY(f.buffers.find(first)->kept);
        QVERIFY(!f.buffers.find(first)->pinned);
    }

    void pinAndKeepStayIndependentThroughTheUi()
    {
        GuiFixture f;
        const auto id = f.seed("both");

        f.window.togglePin(f.model()->rowForId(id));
        f.window.toggleKeep(f.model()->rowForId(id));
        QVERIFY(f.buffers.find(id)->pinned);
        QVERIFY(f.buffers.find(id)->kept);

        f.window.togglePin(f.model()->rowForId(id));
        QVERIFY(!f.buffers.find(id)->pinned);
        QVERIFY(f.buffers.find(id)->kept);   // unpinning never releases the keep
    }

    void deletingAnOrdinaryBufferIsSoftAndOffersUndo()
    {
        GuiFixture f;
        const auto id = f.seed("throwaway");

        f.window.trashRow(f.model()->rowForId(id));

        QCOMPARE(f.buffers.countLive(), 0);
        QCOMPARE(f.buffers.countTrash(), 1);
        QVERIFY(f.buffers.find(id).has_value());        // nothing was destroyed
        QVERIFY(f.toast()->isVisible());
        QVERIFY(f.toast()->hasOffer());
    }

    void undoBringsItBack()
    {
        GuiFixture f;
        const auto id = f.seed("mistake");
        f.window.trashRow(f.model()->rowForId(id));

        auto* undo = f.toast()->findChild<QPushButton*>();
        QVERIFY(undo);
        undo->click();

        QCOMPARE(f.buffers.countLive(), 1);
        QCOMPARE(f.buffers.countTrash(), 0);
        QCOMPARE(f.model()->rowCount(), 1);
        QVERIFY(!f.toast()->isVisible());
        QCOMPARE(f.items.listForBuffer(id).front().text, QStringLiteral("mistake"));
    }

    void deletingAKeptBufferIsRefusedWithoutConfirmation()
    {
        GuiFixture f;
        const auto id = f.seed("important");
        f.window.toggleKeep(f.model()->rowForId(id));

        // trashRow would raise a modal here, so exercise the layer it delegates
        // to: the repository refuses outright, which is what makes the
        // confirmation impossible to skip.
        QCOMPARE(f.service.trash(id), false);
        QCOMPARE(f.buffers.countTrash(), 0);
        QVERIFY(f.buffers.find(id)->kept);
    }

    void confirmingReleasesTheKeepAndTrashes()
    {
        GuiFixture f;
        const auto id = f.seed("important");
        f.window.toggleKeep(f.model()->rowForId(id));

        f.service.trashConfirmed(id);
        f.model()->reload();

        QVERIFY(f.buffers.find(id)->inTrash());
        QVERIFY(!f.buffers.find(id)->kept);
        QCOMPARE(f.model()->rowCount(), 0);
    }

    void trashViewListsDeletedBuffersAndRestores()
    {
        GuiFixture f;
        const auto id = f.seed("deleted thing");
        f.window.trashRow(f.model()->rowForId(id));
        QCOMPARE(f.model()->rowCount(), 0);      // gone from the live stack

        f.window.showTrash(true);
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);
        QCOMPARE(f.model()->rowCount(), 1);
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::SectionNameRole).toString(),
                 QStringLiteral("TRASH"));

        // Delete in the trash now means delete, as it does in every file
        // manager; Restore is its own action, bound to R.
        f.window.restoreRow(0);
        QCOMPARE(f.model()->rowCount(), 0);
        f.window.showTrash(false);
        QCOMPARE(f.model()->rowCount(), 1);
    }

    void restoreIsItsOwnActionSeparateFromDelete()
    {
        GuiFixture f;
        const auto id = f.seed("recoverable");
        f.window.trashRow(f.model()->rowForId(id));
        f.window.showTrash(true);
        QCOMPARE(f.model()->rowCount(), 1);

        f.window.restoreRow(0);

        QCOMPARE(f.buffers.countTrash(), 0);
        QCOMPARE(f.buffers.countLive(), 1);
        QVERIFY(!f.buffers.find(id)->inTrash());
    }

    void pinAndKeepAreInertInTheTrashView()
    {
        GuiFixture f;
        const auto id = f.seed("deleted");
        f.window.trashRow(f.model()->rowForId(id));
        f.window.showTrash(true);

        f.window.togglePin(0);
        f.window.toggleKeep(0);

        QVERIFY(!f.buffers.find(id)->pinned);
        QVERIFY(!f.buffers.find(id)->kept);
    }

    void accessibleLabelCarriesStateNotJustStyling()
    {
        GuiFixture f;
        const auto id = f.seed("labelled");
        f.window.togglePin(f.model()->rowForId(id));
        f.window.toggleKeep(f.model()->rowForId(id));

        const QString label =
            f.model()->index(f.model()->rowForId(id), 0).data(Qt::AccessibleTextRole).toString();
        QVERIFY(label.contains(QStringLiteral("labelled")));
        QVERIFY(label.contains(QStringLiteral("Pinned")));
        QVERIFY(label.contains(QStringLiteral("Kept")));
    }

    void anotherDeleteReplacesTheStandingUndoOffer()
    {
        GuiFixture f;
        const auto first = f.seed("first");
        const auto second = f.seed("second");

        f.window.trashRow(f.model()->rowForId(first));
        QVERIFY(f.toast()->hasOffer());
        f.window.trashRow(f.model()->rowForId(second));

        // The most recent wins: undoing brings back the second, not the first.
        f.toast()->findChild<QPushButton*>()->click();
        QCOMPARE(f.buffers.countLive(), 1);
        QVERIFY(f.buffers.find(second)->deletedAt == std::nullopt);
        QVERIFY(f.buffers.find(first)->inTrash());
    }

    // --- the empty states ---------------------------------------------------
    // A trash with nothing in it and a search with no hits are dead ends. Each
    // has to say which dead end it is and offer the way out of it.

    void anEmptyTrashShowsTheBinAndAWayBack()
    {
        GuiFixture f;
        f.seed("kept");
        auto* empty = f.window.findChild<EmptyStateView*>();
        QVERIFY(empty);

        f.window.showTrash(true);
        QCOMPARE(f.model()->rowCount(), 0);
        auto* stack = f.window.findChild<QStackedWidget*>();
        QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(empty));

        // The illustration has to have decoded — a missing resource shows up as
        // a null pixmap and an invisible label, not as a build failure.
        QVERIFY(empty->hasArtwork());

        auto* back = empty->findChild<QPushButton*>(QStringLiteral("emptyStateAction"));
        QVERIFY(back && back->isVisible());
        back->click();

        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QCOMPARE(f.model()->rowCount(), 1);
        QCOMPARE(stack->currentIndex(), 0);
    }

    void aSearchWithNoHitsGetsNoIllustration()
    {
        GuiFixture f;
        f.seed("kept");
        auto* empty = f.window.findChild<EmptyStateView*>();
        f.model()->setQuery(QStringLiteral("zzzznothinghere"));
        QCOMPARE(f.model()->rowCount(), 0);

        auto* stack = f.window.findChild<QStackedWidget*>();
        QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(empty));
        // The bin belongs to the trash. Borrowing it here would say the wrong
        // thing about a search that simply missed.
        QVERIFY(!empty->hasArtwork());

        empty->findChild<QPushButton*>(QStringLiteral("emptyStateAction"))->click();
        QVERIFY(!f.model()->isSearching());
        QCOMPARE(f.model()->rowCount(), 1);
    }

    void theTrashIsStillAListWhenItHasSomethingInIt()
    {
        GuiFixture f;
        const auto id = f.seed("doomed");
        f.window.trashRow(f.model()->rowForId(id));
        f.window.showTrash(true);

        QCOMPARE(f.model()->rowCount(), 1);
        QCOMPARE(f.window.findChild<QStackedWidget*>()->currentIndex(), 0);
    }

    // A test user could not find the way out of the trash: they tried the
    // mouse's Back button, then hunted, before seeing the Trash button was a
    // toggle (2026-10-06). The way out is now the Home half of the switch, in
    // view the whole time; the sidebar bar that said so was removed at the
    // user's request.
    void theTrashSaysHowToLeaveAndLeavingWorks()
    {
        GuiFixture f;
        f.seed("still here");
        f.service.trash(f.seed("thrown away"));   // an empty trash shows its own page
        f.model()->reload();
        auto* toggle = f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"));
        auto* home = f.window.findChild<QPushButton*>(QStringLiteral("homeSegment"));
        QVERIFY(toggle && home);
        QVERIFY(!f.window.findChild<QPushButton*>(QStringLiteral("leaveTrashButton")));

        toggle->click();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);
        QVERIFY2(home->isVisibleTo(&f.window), "the trash does not show the way out");
        QVERIFY2(toggle->toolTip().contains(QStringLiteral("days")),
                 "how long the trash keeps things is said nowhere");
        home->click();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QVERIFY(!toggle->isChecked());
    }

    // "Select a napkin to see what is on it" is a screen with nothing to do,
    // and the user asked never to land there: whenever the list has napkins
    // and none is open, the latest opens.
    void theBoardNeverWaitsForASelection()
    {
        GuiFixture f;
        const auto older = f.seed("older");
        f.buffers.setModifiedAt(older, nowMs() - 3600 * 1000);
        const auto pinned = f.seed("pinned, so first in the list");
        f.buffers.setPinned(pinned, true);
        f.buffers.setModifiedAt(pinned, nowMs() - 7200 * 1000);
        const auto latest = f.seed("latest");
        f.model()->reload();

        // The latest by time, not merely the top row (a pinned napkin is on top).
        f.view()->selectionModel()->clearCurrentIndex();
        QTRY_VERIFY(f.canvas()->showingANapkin());
        QCOMPARE(f.model()->idAt(f.view()->currentIndex().row()), latest);

        // Deleting the open napkin opens the next latest, not a blank board.
        f.window.trashRow(f.view()->currentIndex().row());
        QTRY_VERIFY(f.canvas()->showingANapkin());
        QCOMPARE(f.model()->idAt(f.view()->currentIndex().row()), older);

        // The trash opens on what was deleted last.
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        QTRY_VERIFY(f.canvas()->showingANapkin());
        QCOMPARE(f.model()->idAt(f.view()->currentIndex().row()), latest);
    }

    // User report, 2026-10-06: deleting items from a napkin in the trash made
    // them vanish from the board while nothing changed — clicking the napkin
    // showed them all again, and the napkin stayed. Delete in the trash is
    // final, after asking; the last item takes the napkin with it.
    void deletingItemsInTheTrashIsFinalAndTheLastTakesTheNapkin()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (const char* t : {"one", "two", "three", "four"})
            f.service.appendTo(id, Item::makeText(QString::fromLatin1(t)));
        f.model()->reload();
        f.select(id);
        // Delete three of them on the live napkin: they go to the trash together.
        auto cards = f.canvas()->findChildren<TextItemCard*>();
        QCOMPARE(cards.size(), 4);
        f.canvas()->selectAll();
        QTest::mouseClick(cards.first(), Qt::LeftButton, Qt::ControlModifier);   // keep one back
        f.canvas()->deleteSelection();
        QCOMPARE(f.items.countForBuffer(id), 1);

        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        QTRY_VERIFY(f.canvas()->showingANapkin());          // the holder opens
        const BufferId holder = f.model()->idAt(f.view()->currentIndex().row());
        QVERIFY(holder != id);
        QCOMPARE(f.items.countForBuffer(holder), 3);

        // Confirm whatever asks, as a user pressing "Delete permanently" would.
        QStringList errors;
        QTimer confirm;
        QObject::connect(&confirm, &QTimer::timeout, [&errors] {
            // Only the confirmation is answered. An error dialog is closed and
            // recorded, so a failure fails the test instead of hanging it.
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                bool confirmed = false;
                for (auto* b : box->buttons())
                    if (box->buttonRole(b) == QMessageBox::DestructiveRole) { b->click(); confirmed = true; }
                if (!confirmed) { errors << box->text(); box->close(); }
            }
        });
        confirm.start(20);

        // One item: gone for good, the rest still there.
        QTest::mouseClick(f.canvas()->findChildren<TextItemCard*>().first(), Qt::LeftButton);
        f.canvas()->deleteSelection();
        QCOMPARE(f.items.countForBuffer(holder), 2);
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 2);

        // Reopening it shows what is really there, not the deleted item again.
        f.view()->selectionModel()->clearCurrentIndex();
        QTRY_VERIFY(f.canvas()->showingANapkin());
        QCOMPARE(f.canvas()->findChildren<TextItemCard*>().size(), 2);

        // The rest: the napkin goes with them.
        f.canvas()->selectAll();
        f.canvas()->deleteSelection();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('|'))));
        QVERIFY2(!f.buffers.find(holder), "an empty napkin was left in the trash");
        QCOMPARE(f.items.countForBuffer(holder), 0);
        QCOMPARE(f.items.countForBuffer(id), 1);         // the live napkin is untouched

        // And deleting for good refuses a napkin that is not in the trash: a
        // live napkin's items go to the trash, never straight to nothing.
        const auto kept = f.items.listForBuffer(id);
        QVERIFY_THROWS_EXCEPTION(napkin::DbError,
                                 f.service.deleteTrashedItems(id, {kept.front().id}));
        QCOMPARE(f.items.countForBuffer(id), 1);
    }

    void theMouseBackButtonLeavesTheTrash()
    {
        GuiFixture f;
        f.seed("still here");
        f.service.trash(f.seed("thrown away"));   // an empty trash shows its own page
        f.model()->reload();
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);

        // Pressed over the list, which takes presses itself.
        QTest::mouseClick(f.view()->viewport(), Qt::BackButton);
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QVERIFY(!f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->isChecked());

        // And it does nothing on the ordinary list.
        QTest::mouseClick(f.view()->viewport(), Qt::BackButton);
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
    }

    void altLeftLeavesTheTrashAndOnlyTheTrash()
    {
        GuiFixture f;
        f.seed("still here");
        f.service.trash(f.seed("thrown away"));   // an empty trash shows its own page
        f.model()->reload();
        auto* back = f.window.findChild<QAction*>(QStringLiteral("leaveTrashAction"));
        QVERIFY(back);
        QCOMPARE(back->shortcut(), QKeySequence(QKeySequence::Back));
        QVERIFY2(!back->isEnabled(), "Alt+Left is taken away from the ordinary list");
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        QVERIFY(back->isEnabled());
        back->trigger();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
        QVERIFY(!back->isEnabled());
    }
};

QTEST_MAIN(TestLifecycle)
#include "test_lifecycle.moc"
