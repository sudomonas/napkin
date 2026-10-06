
#include <QPushButton>
#include <QStackedWidget>
#include "../src/ui/EmptyStateView.h"
#include "GuiFixture.h"
#include <QLabel>
#include <QAction>
#include <QLineEdit>
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
        // The heading carries the rule, so it is said while there is something to keep.
        QCOMPARE(f.model()->index(0, 0).data(BufferListModel::SectionNameRole).toString(),
                 QStringLiteral("TRASH · KEPT %1 DAYS").arg(napkin::BufferService::trashRetentionDays()));

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

    // --- the trash, after the 2026-10-06 usability test --------------------

    // Deleting a napkin's last card left focus in the search box, and Ctrl+Z
    // there undid the search text: the napkin stayed in the trash while the
    // toast vanished as if it had worked.
    void ctrlZInTheSearchBoxTakesBackTheDeleteWhileItIsOffered()
    {
        GuiFixture f;
        const auto only = f.seed("Groceries\n- oats");
        f.seed("something else");
        f.select(only);
        auto* search = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));
        search->setText(QStringLiteral("dal"));               // text the box could undo
        search->clear();
        QTRY_VERIFY(!f.model()->isSearching());
        f.select(only);
        f.canvas()->selectAll();
        f.canvas()->deleteSelection();                        // the last card: the napkin goes
        QVERIFY(f.buffers.find(only)->inTrash());
        QVERIFY(f.toast()->hasOffer());

        QTest::keyClick(search, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY2(!f.buffers.find(only)->inTrash(), "Ctrl+Z in the search box did not undo the delete");
        QVERIFY2(search->text().isEmpty(), "Ctrl+Z also undid the search box's text");
    }

    // Case B of the usability test: right after restoring something, delete a
    // card and press Ctrl+Z — the card must come back.
    void undoAfterARestoreStillTakesBackTheNextDelete()
    {
        GuiFixture f;
        const auto restored = f.seed("restore me");
        const auto work = f.buffers.create();
        for (const char* t : {"keep", "delete then undo"})
            f.service.appendTo(work, Item::makeText(QString::fromLatin1(t)));
        f.service.trash(restored);
        f.model()->reload();

        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        QTRY_VERIFY(f.canvas()->showingANapkin());
        f.window.restoreRow(f.model()->rowForId(restored));
        QVERIFY(!f.buffers.find(restored)->inTrash());

        f.window.findChild<QPushButton*>(QStringLiteral("homeSegment"))->click();
        f.select(work);
        QCOMPARE(f.items.countForBuffer(work), 2);
        QTest::mouseClick(f.canvas()->findChildren<TextItemCard*>().first(), Qt::LeftButton);
        f.canvas()->deleteSelection();
        QCOMPARE(f.items.countForBuffer(work), 1);
        f.window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
        QCOMPARE(f.items.countForBuffer(work), 2);
        QVERIFY2(!f.buffers.find(restored)->inTrash(), "Ctrl+Z took back the restore instead");
    }

    // The Trash tab searched the live napkins, so an old note in the trash was
    // "not found" from anywhere. Now the trash searches the trash, and a live
    // search that finds nothing says the trash has it and goes there.
    void searchFindsWhatIsInTheTrashFromBothSides()
    {
        GuiFixture f;
        const auto old = f.seed("Old todo: renew passport");
        f.seed("Recipe: dal tadka");
        f.service.trash(old);
        f.model()->reload();
        auto* search = f.window.findChild<QLineEdit*>(QStringLiteral("searchField"));

        search->setText(QStringLiteral("passport"));
        QTRY_VERIFY(f.model()->isSearching());
        QCOMPARE(f.model()->rowCount(), 0);
        auto* empty = f.window.findChild<EmptyStateView*>();
        auto* action = empty->findChild<QPushButton*>(QStringLiteral("emptyStateAction"));
        QTRY_VERIFY(action->isVisibleTo(&f.window));
        QCOMPARE(action->text(), QStringLiteral("Look in the trash"));
        action->click();
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);
        QTRY_COMPARE(f.model()->rowCount(), 1);
        QCOMPARE(f.model()->idAt(0), old);

        // And in the trash, only the trash: the live recipe is not a result.
        search->setText(QStringLiteral("dal"));
        QTRY_COMPARE(f.model()->query(), QStringLiteral("dal"));
        QCOMPARE(f.model()->rowCount(), 0);
        // Empty trash… stays offered: the trash has something, whatever the search shows.
        QVERIFY(f.window.findChild<QPushButton*>(QStringLiteral("emptyTrashButton"))->isVisibleTo(&f.window));
    }

    // Two cards deleted from "Q4 planning" showed up in the trash titled by one
    // of themselves, with nothing saying whose they were. A trash row now
    // carries where its items came from and when it was deleted.
    void aTrashRowSaysWhereItCameFromAndWhenItWasDeleted()
    {
        GuiFixture f;
        const auto q4 = f.buffers.create();
        for (const char* t : {"Q4 planning notes", "whiteboard", "Action items"})
            f.service.appendTo(q4, Item::makeText(QString::fromLatin1(t)));
        const auto whole = f.seed("Old todo: renew passport");
        std::vector<ItemId> two;
        for (const auto& it : f.items.listForBuffer(q4))
            if (it.text != QStringLiteral("Q4 planning notes")) two.push_back(it.id);
        const auto holder = f.service.trashItems(q4, two).holder;
        f.service.trash(whole);
        f.model()->reload();
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();

        const QModelIndex cards = f.model()->index(f.model()->rowForId(holder), 0);
        const QModelIndex napkin = f.model()->index(f.model()->rowForId(whole), 0);
        QCOMPARE(cards.data(BufferListModel::OriginRole).toString(), QStringLiteral("Q4 planning notes"));
        QVERIFY2(napkin.data(BufferListModel::OriginRole).toString().isEmpty(),
                 "a whole napkin came from nowhere else");
        for (const QModelIndex& i : {cards, napkin}) {
            const qint64 at = i.data(BufferListModel::DeletedAtRole).toLongLong();
            QVERIFY2(at > 0 && nowMs() - at < 60000, "no deletion time on a trash row");
        }
    }

    // --- restoring single cards -----------------------------------------------
    static BufferId napkinOf(GuiFixture& f, std::initializer_list<const char*> texts)
    {
        const auto id = f.buffers.create();
        for (const char* t : texts) f.service.appendTo(id, Item::makeText(QString::fromLatin1(t)));
        return id;
    }
    static ItemId itemCalled(GuiFixture& f, BufferId in, const char* text)
    {
        for (const auto& it : f.items.listForBuffer(in))
            if (it.text == QString::fromLatin1(text)) return it.id;
        return kNoItem;
    }

    void oneCardOfADeletedSetGoesBackIntoItsNapkin()
    {
        GuiFixture f;
        const auto q4 = napkinOf(f, {"notes", "photo", "action items"});
        const auto holder = f.service.trashItems(q4, {itemCalled(f, q4, "photo"),
                                                      itemCalled(f, q4, "action items")}).holder;
        const auto target = f.service.restoreItems(holder, {itemCalled(f, holder, "photo")});
        QCOMPARE(target, q4);
        QVERIFY(itemCalled(f, q4, "photo") != kNoItem);
        QCOMPARE(f.items.countForBuffer(q4), 2);
        QCOMPARE(f.items.countForBuffer(holder), 1);           // the other stays in the trash
        QVERIFY(f.buffers.find(holder)->inTrash());
        QCOMPARE(*f.buffers.find(holder)->restoresTo, q4);      // and still knows where it goes
    }

    // "Read later" was split in two for good: its card restored alone became a
    // napkin of its own because the napkin was in the trash too.
    void restoringCardsWhoseNapkinIsInTheTrashBringsTheNapkinBack()
    {
        GuiFixture f;
        const auto later = napkinOf(f, {"read later", "doc.qt.io link"});
        const auto holder = f.service.trashItems(later, {itemCalled(f, later, "doc.qt.io link")}).holder;
        f.service.trash(later);
        const auto target = f.service.restore(holder);
        QCOMPARE(target, later);
        QVERIFY(!f.buffers.find(later)->inTrash());
        QCOMPARE(f.items.countForBuffer(later), 2);
        QVERIFY2(!f.buffers.find(holder), "the card came back as a napkin of its own");
    }

    void partOfAWholeNapkinInTheTrashComesBackAndTheRestRejoinsItLater()
    {
        GuiFixture f;
        const auto recipe = napkinOf(f, {"dal tadka", "ingredients", "photo"});
        f.buffers.setPinned(recipe, true);
        f.service.trash(recipe);
        QCOMPARE(f.service.restoreItems(recipe, {itemCalled(f, recipe, "ingredients")}), recipe);
        QVERIFY(!f.buffers.find(recipe)->inTrash());
        QVERIFY2(f.buffers.find(recipe)->pinned, "the napkin came back as a different one");
        QCOMPARE(f.items.countForBuffer(recipe), 1);

        // The rest is in the trash as cards deleted from it.
        BufferId rest = kNoBuffer;
        for (const auto& b : f.buffers.listTrash()) if (b.restoresTo == recipe) rest = b.id;
        QVERIFY(rest != kNoBuffer);
        QCOMPARE(f.items.countForBuffer(rest), 2);
        QCOMPARE(f.service.restore(rest), recipe);
        QCOMPARE(f.items.countForBuffer(recipe), 3);
        QStringList order;
        for (const auto& it : f.items.listForBuffer(recipe)) order << it.text;
        QCOMPARE(order.size(), 3);                               // all back, one napkin
    }

    // Through the window: the card menu in the trash restores just that card,
    // says where it went, and Undo puts it back in the trash.
    void restoringACardFromTheBoardSaysWhereAndCanBeUndone()
    {
        GuiFixture f;
        const auto q4 = napkinOf(f, {"notes", "photo", "action items"});
        const auto holder = f.service.trashItems(q4, {itemCalled(f, q4, "photo"),
                                                      itemCalled(f, q4, "action items")}).holder;
        f.model()->reload();
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        f.select(holder);
        QVERIFY(f.canvas()->inTrash());
        emit f.canvas()->restoreRequested({itemCalled(f, holder, "photo")});
        QCOMPARE(f.items.countForBuffer(q4), 2);
        QCOMPARE(f.items.countForBuffer(holder), 1);
        QVERIFY(f.toast()->hasOffer());
        f.toast()->undoNow();
        QCOMPARE(f.items.countForBuffer(q4), 1);                 // back in the trash
        QCOMPARE(itemCalled(f, q4, "photo"), kNoItem);
    }

    // --- the retest, 2026-10-06 ------------------------------------------------
    static QString toastWords(GuiFixture& f)
    {
        QString said;
        for (auto* l : f.toast()->findChildren<QLabel*>()) if (!l->text().isEmpty()) said = l->text();
        return said;
    }

    // Restore and undo put things back as they were, place in the list
    // included: a three-day-old napkin restored came back "just now", at the top.
    void restoreAndUndoLeaveANapkinWhereItWas()
    {
        GuiFixture f;
        const auto recipe = napkinOf(f, {"dal tadka", "ingredients"});
        const qint64 threeDays = nowMs() - 3 * kMsPerDay;
        f.buffers.setModifiedAt(recipe, threeDays);
        f.service.trash(recipe);
        f.service.restore(recipe);
        QCOMPARE(f.buffers.find(recipe)->modifiedAt, threeDays);

        // A card deleted and undone: the napkin's age is what it was.
        f.model()->reload();
        f.select(recipe);
        QTest::mouseClick(f.canvas()->findChildren<TextItemCard*>().first(), Qt::LeftButton);
        f.canvas()->deleteSelection();
        QVERIFY(f.buffers.find(recipe)->modifiedAt > threeDays);   // the delete is a change…
        f.toast()->undoNow();
        QCOMPARE(f.buffers.find(recipe)->modifiedAt, threeDays);   // …and undoing it is not
        QCOMPARE(f.items.countForBuffer(recipe), 2);
    }

    // Ctrl+Z after the toast has gone did nothing at all; it now says where things are.
    void undoWithNothingToUndoSaysWhereThingsWent()
    {
        GuiFixture f;
        f.seed("something");
        QVERIFY(!f.toast()->hasOffer());
        f.window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
        QVERIFY(f.toast()->isVisible());
        QVERIFY2(toastWords(f).contains(QStringLiteral("trash")), qPrintable(toastWords(f)));
    }

    // Cards deleted together are titled by where they came from, not by the first of them.
    void deletedCardsAreTitledByTheirNapkin()
    {
        GuiFixture f;
        const auto q4 = napkinOf(f, {"Q4 planning notes", "photo", "Action items"});
        const auto holder = f.service.trashItems(q4, {itemCalled(f, q4, "photo"),
                                                      itemCalled(f, q4, "Action items")}).holder;
        f.model()->reload();
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        const QModelIndex row = f.model()->index(f.model()->rowForId(holder), 0);
        QCOMPARE(row.data(BufferListModel::PrimaryRole).toString(),
                 QStringLiteral("2 cards from “Q4 planning notes”"));
        QVERIFY(!row.data(BufferListModel::HeadRole).toString().isEmpty());   // what they are, still shown
    }

    // A card restored whose napkin was in the trash too brings the napkin back;
    // the toast says so, and Undo sends the napkin back to the trash.
    void reviveIsSaidAndUndone()
    {
        GuiFixture f;
        const auto later = napkinOf(f, {"Read later", "doc.qt.io link"});
        const auto holder = f.service.trashItems(later, {itemCalled(f, later, "doc.qt.io link")}).holder;
        f.service.trash(later);
        f.model()->reload();
        f.window.findChild<QPushButton*>(QStringLiteral("trashToggle"))->click();
        f.window.restoreRow(f.model()->rowForId(holder));
        QVERIFY(!f.buffers.find(later)->inTrash());
        QVERIFY2(toastWords(f).contains(QStringLiteral("back from the trash")), qPrintable(toastWords(f)));
        f.toast()->undoNow();
        QVERIFY(f.buffers.find(later)->inTrash());
        QCOMPARE(f.items.countForBuffer(later), 2);   // with its card, in one piece
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
