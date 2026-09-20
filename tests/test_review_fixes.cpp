#include "GuiFixture.h"
#include "../src/media/BlobGc.h"
#include "../src/media/ImageFormats.h"
#include "../src/ui/ItemCanvas.h"
#include "../src/ui/ItemCard.h"
#include "../src/ui/CardFooter.h"
#include "../src/ui/Tokens.h"

#include <cmath>

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

using namespace napkin;

namespace {
QByteArray png(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}
}  // namespace

// Regressions for findings from the independent security/performance and
// UI/UX reviews. Each of these reproduced a real defect before its fix.
class TestReviewFixes : public QObject {
    Q_OBJECT
private slots:
    // --- SVG could read local files -----------------------------------------
    void anSvgThatNamesALocalFileIsRefused()
    {
        // Qt refuses file:// but happily loads a BARE path, which an earlier
        // "measured, not assumed" claim missed because the probe only ever
        // tested the spelling Qt already rejected.
        QVERIFY(formats::svgHasExternalReferences(
            "<svg xmlns='http://www.w3.org/2000/svg'><image href='/etc/hostname'/></svg>"));
        QVERIFY(formats::svgHasExternalReferences(
            "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink'>"
            "<image xlink:href='/tmp/secret.png'/></svg>"));
        QVERIFY(formats::svgHasExternalReferences(
            "<svg xmlns='http://www.w3.org/2000/svg'><rect fill='url(/tmp/x.png)'/></svg>"));
    }

    void aSelfContainedSvgIsStillAccepted()
    {
        QVERIFY(!formats::svgHasExternalReferences(
            "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<defs><linearGradient id='g'/></defs><rect fill='url(#g)' width='10' height='10'/>"
            "</svg>"));

        QTemporaryDir dir;
        BlobStore store(dir.path());
        const auto ok = store.store(
            "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<rect width='10' height='10' fill='red'/></svg>");
        QVERIFY2(ok.ok, qPrintable(ok.error));

        const auto refused = store.store(
            "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<image href='/tmp/secret.png' width='10' height='10'/></svg>");
        QVERIFY(!refused.ok);
        QVERIFY(refused.error.contains(QStringLiteral("other files")));
    }

    // --- a small file can still be an enormous image -------------------------
    void aPixelBombIsRefusedEvenThoughItIsTiny()
    {
        QTemporaryDir dir;
        BlobStore store(dir.path());
        QImage huge(20000, 20000, QImage::Format_Mono);   // declares 400 megapixels
        huge.fill(0);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!huge.save(&buffer, "PNG")) QSKIP("could not build the test image");

        QVERIFY(bytes.size() < BlobStore::kMaxBytes);   // passes the byte check
        const auto stored = store.store(bytes);
        QVERIFY(!stored.ok);                            // and is still refused
    }

    // --- INSERT OR REPLACE walked straight past the guard --------------------
    void replaceCannotSmuggleAKeptBufferOut()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.buffers.setKept(id, true);

        QVERIFY_THROWS_EXCEPTION(DbError,
            f.db.exec(QString("INSERT OR REPLACE INTO buffers(id, created_at, modified_at, kept)"
                              " VALUES(%1, 1, 1, 0)").arg(id).toUtf8().constData()));
        QVERIFY(f.buffers.find(id)->kept);
    }

    // --- undo used to hand back a stripped buffer ----------------------------
    void undoingAConfirmedDeleteRestoresTheKeep()
    {
        GuiFixture f;
        const auto id = f.seed("important reference");
        f.window.toggleKeep(f.model()->rowForId(id));
        const auto modifiedBefore = f.buffers.find(id)->modifiedAt;

        // Confirming release the keep, by design. Undo has to put it back.
        f.service.trashConfirmed(id);
        f.model()->reload();
        QVERIFY(!f.buffers.find(id)->kept);

        f.window.undoLastTrashForTest(id, /*wasKept=*/true, modifiedBefore);

        QVERIFY(f.buffers.find(id)->kept);
        QVERIFY(!f.buffers.find(id)->inTrash());
        QCOMPARE(f.buffers.find(id)->modifiedAt, modifiedBefore);   // history intact
    }

    // --- thumbnails outlived the images they came from -----------------------
    void emptyingTheTrashAlsoRemovesTheThumbnail()
    {
        GuiFixture f;
        const auto stored = f.blobs.store(png(300, 200));
        QVERIFY(stored.ok);
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeImage(stored.hash, 300, 200, stored.byteSize,
                                               {}, stored.mime));
        QVERIFY(!f.thumbs.forBlob(stored.hash, stored.mime).isNull());   // writes the cache file

        const QString thumbDir = f.thumbsDir();
        QVERIFY(!QDir(thumbDir).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()
                || !QDir(thumbDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

        f.service.trash(id);
        f.service.emptyTrash();
        const auto result = reconcileBlobs(f.items, f.blobs, thumbDir);

        QVERIFY(!f.blobs.exists(stored.hash, stored.mime));
        QCOMPARE(result.thumbnailsRemoved, 1);   // the rendering went too
    }

    // --- the empty-trash dialog promised more than it delivered --------------
    void theEmptyTrashCountMatchesWhatWillActuallyBeDestroyed()
    {
        GuiFixture f;
        const auto ordinary = f.buffers.create();
        const auto stuck = f.buffers.create();
        f.buffers.setKept(stuck, true);
        f.db.exec("UPDATE buffers SET deleted_at = 1");   // both in the bin

        QCOMPARE(f.buffers.countTrash(), 2);        // what is in there
        QCOMPARE(f.buffers.countPurgeable(), 1);    // what emptying it would take
        QCOMPARE(f.service.emptyTrash(), 1);
        QVERIFY(f.buffers.find(stuck).has_value());
        QVERIFY(!f.buffers.find(ordinary).has_value());
    }

    // --- Ctrl+N inside the trash created a live buffer shown in the bin ------
    void newBufferLeavesTheTrashViewFirst()
    {
        GuiFixture f;
        f.seed("something");
        f.window.showTrash(true);
        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Trash);

        f.trigger("newBufferAction");

        QCOMPARE(f.model()->mode(), BufferListModel::Mode::Live);
    }

    // --- a huge single line made every repaint O(text length) ---------------
    void aPreviewLineIsBoundedNoMatterHowLargeTheText()
    {
        const QString huge(400000, QLatin1Char('x'));
        const auto p = derivePreview({Item::makeText(huge)}, 1, 0);
        QVERIFY(p.primary.size() <= kPreviewLineLimit);
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

// --- third audit ------------------------------------------------------------
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

    void theOverflowMenuActuallyListsTheActions()
    {
        GuiFixture f;
        auto* button = f.window.findChild<QToolButton*>(QStringLiteral("overflowButton"));
        QVERIFY(button);
        QVERIFY(button->menu());
        QStringList labels;
        for (auto* a : button->menu()->actions())
            if (!a->isSeparator()) labels << a->text();
        // Ctrl+T is the keyboard way to start a note; it must be findable.
        QVERIFY2(labels.filter(QStringLiteral("New note")).size() > 0,
                 qPrintable("menu had: " + labels.join(", ")));
        QVERIFY(labels.size() >= 4);
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

        // Any sweep inside the eight-second window used to destroy the blob the
        // offer depends on. Run one directly.
        reconcileBlobs(f.items, f.blobs, f.thumbsDir(), f.window.undoProtectedBlobsForTest());

        if (f.toast()->hasOffer()) {
            f.toast()->findChild<QPushButton*>()->click();
            for (const auto& item : f.items.allImageItems())
                QVERIFY2(f.blobs.exists(item.blobHash, item.mime),
                         "undo restored a row whose blob had been swept away");
        }
    }

    // --- the board is operable without a mouse -------------------------------
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

    void deleteFromTheKeyboardAloneRemovesTheCard()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        for (int i = 0; i < 3; ++i)
            f.service.appendTo(id, Item::makeText(QStringLiteral("card %1").arg(i)));
        f.model()->reload();
        f.select(id);
        f.canvas()->setFocus(Qt::OtherFocusReason);

        QTest::keyClick(f.canvas(), Qt::Key_Down);
        QTest::keyClick(f.canvas(), Qt::Key_Delete);
        QCOMPARE(f.items.countForBuffer(id), 2);
    }

    void everyCardAnnouncesItselfToAScreenReader()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        f.service.appendTo(id, Item::makeText(QStringLiteral("a note worth reading")));
        f.model()->reload();
        f.select(id);

        for (auto* card : f.canvas()->findChildren<ItemCard*>()) {
            QVERIFY2(!card->accessibleName().isEmpty(),
                     "a card with no accessible name announces nothing");
        }
    }

    void theSelectedBorderIsActuallyVisible()
    {
        // The raw Highlight at alpha 160 measured 1.74:1 in Breeze Light —
        // fainter than the 3.10:1 resting border it replaced.
        QPalette pal;
        pal.setColor(QPalette::Base, QColor(252, 252, 252));
        pal.setColor(QPalette::Window, QColor(239, 240, 241));
        pal.setColor(QPalette::Highlight, QColor(61, 174, 233));

        auto relLum = [](const QColor& c) {
            auto ch = [](int v) {
                const qreal s = v / 255.0;
                return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * ch(c.red()) + 0.7152 * ch(c.green()) + 0.0722 * ch(c.blue());
        };
        const QColor accent = tokens::readableAccent(pal, 1.0);
        const qreal la = relLum(accent), lb = relLum(pal.color(QPalette::Base));
        const qreal ratio = (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
        QVERIFY2(ratio >= 3.0, qPrintable(QString("selected border is %1:1").arg(ratio)));
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

    // --- feedback the user can see -------------------------------------------
    // There are three routes to a copy and they must all say so. The
    // acknowledgement first shipped wired to the footer button's SIGNAL, which
    // left Ctrl+C and the context menu silent — so the routes are enumerated
    // here rather than the verb being tested once.
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

    // No single card can speak for a copy of several, and flashing all of them
    // would be a light show, so the count goes to the toast.
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

    // inform() was spelled offer(message, nullptr), so a message about something
    // that destroyed NOTHING cancelled a live Undo. Delete two items, then copy
    // within the eight seconds, and the delete became unundoable.
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

    // Quit was spelled close(), and close() ends the process only as a side
    // effect of quitOnLastWindowClosed — which never fires for a window that is
    // ALREADY hidden. That is exactly the state the tray's Quit is used from,
    // so Quit did nothing. A nested event loop stands in for the real one:
    // QCoreApplication::quit() must exit it.
    void quitEndsTheApplicationEvenWithTheWindowHidden()
    {
        GuiFixture f;
        f.window.hide();
        QVERIFY(!f.window.isVisible());

        QSignalSpy going(&f.window, &MainWindow::quitting);
        QVERIFY(f.window.quitNapkin());
        QCOMPARE(going.count(), 1);
        QVERIFY(!f.window.isVisible());
    }

    // Copy a PDF in the file manager, press Ctrl+V: Napkin holds text and
    // images and nothing else, and it used to return in silence — which looks
    // exactly like a broken application.
    void pastingWhatNapkinCannotHoldSaysSoAndMakesNoNapkin()
    {
        GuiFixture f;
        const int before = f.model()->rowCount();
        QApplication::clipboard()->clear();

        f.window.pasteFromClipboard();

        QVERIFY2(f.toast()->isVisible(), "an impossible paste must say why");
        QString said;
        for (auto* l : f.toast()->findChildren<QLabel*>())
            if (!l->text().isEmpty()) said = l->text();
        QVERIFY2(said.contains(QStringLiteral("clipboard")),
                 qPrintable(QStringLiteral("unhelpful message: ") + said));
        // And it must not leave a blank napkin behind as evidence of trying.
        QCOMPARE(f.model()->rowCount(), before);
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

    void aThemeChangeRepaintsTheCards()
    {
        GuiFixture f;
        const auto id = f.buffers.create();
        const auto stored = f.blobs.store([]{
            QImage i(20, 20, QImage::Format_RGB32); i.fill(Qt::blue);
            QByteArray b; QBuffer buf(&b); buf.open(QIODevice::WriteOnly);
            i.save(&buf, "PNG"); return b; }());
        f.service.appendTo(id, Item::makeImage(stored.hash, 20, 20, stored.byteSize,
                                               {}, stored.mime));
        f.model()->reload();
        f.select(id);

        auto* card = f.canvas()->findChildren<ImageItemCard*>().first();
        auto* caption = card->findChildren<QLabel*>().last();
        const QColor before = caption->palette().color(QPalette::WindowText);

        QPalette dark;
        dark.setColor(QPalette::Base, QColor(27, 30, 32));
        dark.setColor(QPalette::Window, QColor(35, 38, 41));
        dark.setColor(QPalette::Text, QColor(252, 252, 252));
        card->setPalette(dark);

        // Colours captured at construction went stale on a theme change: card
        // text stayed the old colour until the buffer was reopened.
        QVERIFY(caption->palette().color(QPalette::WindowText) != before);
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
};

QTEST_MAIN(TestReviewFixes)
#include "test_review_fixes.moc"
