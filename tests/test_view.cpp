#include "GuiFixture.h"
#include "../src/ui/LinkChip.h"
#include "../src/ui/Tokens.h"

#include <QApplication>
#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QWheelEvent>
#include <QScreen>
#include <QtTest>

using namespace napkin;

// How the board is viewed: its zoom and its column cap. Both exist for large
// monitors, where "as many 360px columns as fit" put eight or nine columns of
// small cards on a 32-inch screen (user report, 2026-11-07).
class TestView : public QObject {
    Q_OBJECT

    static void forgetView()
    {
        QSettings s;
        s.remove(QStringLiteral("view"));
    }

    // A napkin of plain notes on a board wide enough for many columns.
    static BufferId seedNotes(GuiFixture& f, int count, const QString& text = {})
    {
        const auto id = f.buffers.create();
        for (int i = 0; i < count; ++i)
            f.service.appendTo(id, Item::makeText(text.isEmpty()
                                                      ? QStringLiteral("note %1 with a few words in it").arg(i)
                                                      : text));
        f.model()->reload();
        f.select(id);
        return id;
    }

    static void widen(GuiFixture& f, int width = 2600)
    {
        f.window.resize(width, 1100);
        QTRY_VERIFY2(f.canvas()->width() > width / 2,
                     qPrintable(QStringLiteral("window %1x%2, canvas %3, screen %4")
                                    .arg(f.window.width()).arg(f.window.height())
                                    .arg(f.canvas()->width())
                                    .arg(f.window.screen()->availableGeometry().width())));
        QCoreApplication::processEvents();
    }

    static TextItemCard* firstText(GuiFixture& f)
    {
        const auto cards = f.canvas()->findChildren<TextItemCard*>();
        return cards.isEmpty() ? nullptr : cards.first();
    }

private slots:
    void init() { forgetView(); }
    void cleanup() { forgetView(); }

    // The point of zoom: bigger cards, not just bigger letters in the same
    // columns. Text and column width scale by the same factor.
    void zoomScalesTheTextAndTheCardsTogether()
    {
        GuiFixture f;
        seedNotes(f, 12);   // first: with no napkins the window shows its welcome page
        widen(f);
        const auto& board = f.canvas()->boardForTest();
        const int width100 = board.columnWidth();
        const int columns100 = board.columnCount();
        const qreal points100 = firstText(f)->font().pointSizeF();

        f.window.setBoardZoom(1.5);
        QCOMPARE(f.canvas()->zoom(), 1.5);
        QVERIFY(firstText(f));
        QCOMPARE(firstText(f)->font().pointSizeF(), points100 * 1.5);
        // Not exactly 1.5 times as wide: the column count is a whole number, so
        // 5 columns become 3, not 3.33. What holds is that the width stays inside
        // the scaled bounds and the cards got wider.
        QVERIFY2(board.columnWidth() > width100,
                 qPrintable(QStringLiteral("%1 -> %2").arg(width100).arg(board.columnWidth())));
        QVERIFY(board.columnWidth() >= qRound(tokens::kCardMinWidth * 1.5));
        QVERIFY(board.columnWidth() <= qRound(tokens::kCardMaxWidth * 1.5));
        QVERIFY(board.columnCount() < columns100);
        QCOMPARE(board.gap(), qRound(tokens::kCardGap * 1.5));
        QCOMPARE(board.maxCardHeight(), qRound(tokens::kCardMaxHeight * 1.5));
    }

    // An unzoomed board sets no font of its own, so it behaves exactly as it
    // did before zoom existed — including following the application's font.
    void actualSizeLeavesTheFontInherited()
    {
        GuiFixture f;
        QVERIFY(!f.canvas()->testAttribute(Qt::WA_SetFont));
        f.window.setBoardZoom(1.3);
        QVERIFY(f.canvas()->testAttribute(Qt::WA_SetFont));
        f.window.setBoardZoom(1.0);
        QVERIFY(!f.canvas()->testAttribute(Qt::WA_SetFont));
    }

    // Zoom is on top of Settings ▸ Text size, not instead of it: changing the
    // application's font under a zoomed board re-derives the board's font.
    void aZoomedBoardFollowsTheApplicationTextSize()
    {
        GuiFixture f;
        seedNotes(f, 3);
        f.window.setBoardZoom(1.5);
        const qreal zoomedBefore = f.canvas()->font().pointSizeF();
        struct Restore {
            QFont font = QApplication::font();
            ~Restore() { QApplication::setFont(font); }
        } restore;
        QFont bigger = restore.font;
        bigger.setPointSizeF(restore.font.pointSizeF() * 2);
        QApplication::setFont(bigger);

        QTRY_COMPARE(f.window.font().pointSizeF(), bigger.pointSizeF());
        QTRY_COMPARE(f.canvas()->font().pointSizeF(), zoomedBefore * 2);
        // And the cards were rebuilt at it.
        QVERIFY(firstText(f));
        QCOMPARE(firstText(f)->font().pointSizeF(), zoomedBefore * 2);
    }

    // Ctrl+wheel anywhere on the board zooms the board. Over a card that is not
    // being edited it used to zoom that card's editor alone — QPlainTextEdit
    // does that when read-only — leaving its text out of step with the height
    // the board measured for it.
    void ctrlWheelOverACardZoomsTheBoardNotTheCard()
    {
        GuiFixture f;
        seedNotes(f, 3);
        auto* card = firstText(f);
        QVERIFY(card);
        auto* editor = card->findChild<QPlainTextEdit*>();
        QVERIFY(editor);
        const qreal editorPoints = editor->font().pointSizeF();

        const QPointF at(editor->viewport()->width() / 2.0, editor->viewport()->height() / 2.0);
        QWheelEvent wheel(at, editor->viewport()->mapToGlobal(at), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(editor->viewport(), &wheel);

        QCOMPARE(f.canvas()->zoom(), 1.1);
        // Sideways is not a zoom, and is left for whoever wants it.
        QWheelEvent sideways(at, editor->viewport()->mapToGlobal(at), QPoint(), QPoint(120, 0),
                             Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(firstText(f)->findChild<QPlainTextEdit*>()->viewport(), &sideways);
        QCOMPARE(f.canvas()->zoom(), 1.1);
        // The card was rebuilt at the board's new size, not zoomed on its own.
        auto* rebuilt = firstText(f);
        QVERIFY(rebuilt);
        QCOMPARE(rebuilt->findChild<QPlainTextEdit*>()->font().pointSizeF(),
                 f.canvas()->font().pointSizeF());
        QVERIFY(rebuilt->findChild<QPlainTextEdit*>()->font().pointSizeF() > editorPoints);
    }

    // Saved across launches, and read back defensively: a corrupt value must
    // not leave a board nobody can read.
    void zoomIsRememberedAndClamped()
    {
        {
            GuiFixture f;
            f.window.setBoardZoom(5.0);
            QCOMPARE(f.canvas()->zoom(), ItemCanvas::kMaxZoom);
            QCOMPARE(QSettings().value(QStringLiteral("view/boardZoomPercent")).toInt(), 200);
            f.window.stepBoardZoom(-3);
            QCOMPARE(f.canvas()->zoom(), 1.7);
        }
        {
            GuiFixture again;
            QCOMPARE(again.canvas()->zoom(), 1.7);
        }
        QSettings().setValue(QStringLiteral("view/boardZoomPercent"), QStringLiteral("enormous"));
        QSettings().setValue(QStringLiteral("view/boardColumns"), 99);
        {
            GuiFixture corrupt;
            QCOMPARE(corrupt.canvas()->zoom(), ItemCanvas::kMinZoom);   // "enormous" reads as 0
            QCOMPARE(corrupt.canvas()->columnCap(), 0);
        }
    }

    // The keys work without opening the menu, and the menu lists them.
    void theZoomKeysAreBoundAndListed()
    {
        GuiFixture f;
        auto* in = f.window.findChild<QAction*>(QStringLiteral("zoomInAction"));
        auto* out = f.window.findChild<QAction*>(QStringLiteral("zoomOutAction"));
        auto* reset = f.window.findChild<QAction*>(QStringLiteral("zoomResetAction"));
        QVERIFY(in && out && reset);
        QVERIFY(in->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+="))));
        QCOMPARE(reset->shortcut(), QKeySequence(QStringLiteral("Ctrl+0")));
        for (auto* a : {in, out, reset}) QVERIFY(f.window.actions().contains(a));
        in->trigger();
        in->trigger();
        QCOMPARE(f.canvas()->zoom(), 1.2);
        out->trigger();
        QCOMPARE(f.canvas()->zoom(), 1.1);
        reset->trigger();
        QCOMPARE(f.canvas()->zoom(), 1.0);
    }

    // A capped board gives the columns it did not draw to the cards that are
    // left, up to a readable measure, and centres the result.
    void aColumnCapWidensAndCentresTheCards()
    {
        GuiFixture f;
        seedNotes(f, 12);
        widen(f, 3200);
        const auto& board = f.canvas()->boardForTest();
        QVERIFY2(board.columnCount() > 3, qPrintable(QString::number(board.columnCount())));

        f.window.setBoardColumns(3);
        QCOMPARE(board.columnCount(), 3);
        QVERIFY(board.columnWidth() > tokens::kCardMaxWidth);
        QVERIFY(board.columnWidth() <= tokens::kCardCappedMaxWidth);
        const int used = 3 * board.columnWidth() + 2 * board.gap();
        const int usable = f.canvas()->viewport()->width() + f.canvas()->verticalScrollBar()->sizeHint().width()
                           - f.canvas()->verticalScrollBar()->sizeHint().width();
        // Centred: the space left of the first column matches the space right
        // of the last, within the scrollbar reservation and a pixel of rounding.
        const int leftSpace = board.left();
        const int rightSpace = usable - board.left() - used;
        QVERIFY2(std::abs(leftSpace - rightSpace) <= f.canvas()->verticalScrollBar()->sizeHint().width() + 2,
                 qPrintable(QStringLiteral("left %1 right %2").arg(leftSpace).arg(rightSpace)));
        // The cards are where the board says.
        for (auto* card : f.canvas()->findChildren<ItemCard*>())
            QVERIFY(card->x() >= board.left());

        QCOMPARE(QSettings().value(QStringLiteral("view/boardColumns")).toInt(), 3);
        f.window.setBoardColumns(0);
        QVERIFY(board.columnCount() > 3);
        QCOMPARE(board.left(), tokens::kPadX);   // uncapped keeps to the left, as before
    }

    // A link card at 150%: the chip is measured at the card's font, not the
    // application's. Measured at 100% it was drawn into a box too short for
    // its own label, which lost its top (render, 2026-11-07).
    void aLinkChipFitsAtAZoomedSize()
    {
        GuiFixture f;
        seedNotes(f, 1, QStringLiteral("https://github.com/sudomonas/napkin/releases"));
        f.window.setBoardZoom(1.5);
        auto* card = firstText(f);
        QVERIFY(card);
        auto* chip = card->findChild<LinkChip*>();
        QVERIFY(chip);
        QVERIFY(!chip->isHidden());
        QVERIFY(LinkChip::preferredHeight(card->font()) > LinkChip::preferredHeight(QApplication::font()));
        QVERIFY2(chip->height() >= LinkChip::preferredHeight(card->font()),
                 qPrintable(QStringLiteral("chip %1px, needs %2px").arg(chip->height())
                                .arg(LinkChip::preferredHeight(card->font()))));
        // Every label in the chip has the height its text asks for.
        for (auto* label : chip->findChildren<QLabel*>())
            if (!label->text().isEmpty())
                QVERIFY2(label->height() >= label->sizeHint().height(),
                         qPrintable(QStringLiteral("\"%1\" %2px of %3px").arg(label->text())
                                        .arg(label->height()).arg(label->sizeHint().height())));
    }

    // --- Spotlight ------------------------------------------------------------
    static int middleOfView(GuiFixture& f)
    {
        return f.canvas()->verticalScrollBar()->value() + f.canvas()->viewport()->height() / 2;
    }
    static QWidget* rail(GuiFixture& f)
    {
        for (auto* w : f.canvas()->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            if (w->accessibleName() == QStringLiteral("Cards on this napkin")) return w;
        return nullptr;
    }

    // One column, centred, at a step up in type; the shade and the rail up.
    void spotlightIsOneCentredColumnAtReadingSize()
    {
        GuiFixture f;
        seedNotes(f, 12);
        widen(f);
        const qreal boardPoints = firstText(f)->font().pointSizeF();
        f.window.setSpotlight(true);
        const auto& board = f.canvas()->boardForTest();
        QCOMPARE(board.columnCount(), 1);
        QVERIFY(board.spotlight());
        QCOMPARE(firstText(f)->font().pointSizeF(), boardPoints * tokens::kSpotlightType);
        // Centred within the width the board has.
        const int usable = f.canvas()->viewport()->width() - 2 * tokens::kPadX;
        const int leftSpace = board.left() - tokens::kPadX;
        const int rightSpace = usable - leftSpace - board.columnWidth();
        QVERIFY2(std::abs(leftSpace - rightSpace) <= f.canvas()->verticalScrollBar()->sizeHint().width() + 2,
                 qPrintable(QStringLiteral("left %1 right %2").arg(leftSpace).arg(rightSpace)));
        // A comfortable line, not the whole screen.
        QVERIFY(board.columnWidth() < f.canvas()->viewport()->width() / 2);
        QVERIFY(rail(f) && !rail(f)->isHidden());
        QVERIFY(f.canvas()->spotlitIndex() >= 0);
        // The shade lies over the cards: it is the topmost child of the body.
        auto* body = f.canvas()->widget();
        QWidget* top = nullptr;
        for (QObject* o : body->children())
            if (auto* w = qobject_cast<QWidget*>(o); w && !w->isHidden()) top = w;
        QVERIFY(top && !qobject_cast<ItemCard*>(top));
        QVERIFY(top->testAttribute(Qt::WA_TransparentForMouseEvents));

        f.window.setSpotlight(false);
        QVERIFY(board.columnCount() > 1);
        QCOMPARE(f.canvas()->spotlitIndex(), -1);
        QVERIFY(rail(f)->isHidden());
        QCOMPARE(firstText(f)->font().pointSizeF(), boardPoints);
    }

    // The arrows glide the next card to the middle of the view.
    void theArrowsBringTheNextCardToTheMiddle()
    {
        GuiFixture f;
        seedNotes(f, 12);
        f.window.setSpotlight(true);
        f.canvas()->setFocus(Qt::OtherFocusReason);
        for (int i = 0; i < 3; ++i) QTest::keyClick(f.canvas(), Qt::Key_Down);
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 3);
        const QRect r = f.canvas()->boardForTest().placements()[3].rect;
        QTRY_VERIFY(std::abs(r.center().y() - middleOfView(f)) <= 2);
        // Left and Right step through the one column as well.
        QTest::keyClick(f.canvas(), Qt::Key_Left);
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 2);
        QTest::keyClick(f.canvas(), Qt::Key_Right);
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 3);
    }

    // A dimmed neighbour can be clicked through the shade, and comes to the middle.
    void clickingADimmedCardBringsItToTheMiddle()
    {
        GuiFixture f;
        seedNotes(f, 6);
        f.window.setSpotlight(true);
        QCOMPARE(f.canvas()->spotlitIndex(), 0);
        ItemCard* second = nullptr;
        for (auto* c : f.canvas()->findChildren<ItemCard*>())
            if (f.canvas()->indexOf(c->itemId()) == 1) second = c;
        QVERIFY(second);
        QTest::mouseClick(second, Qt::LeftButton, Qt::NoModifier, QPoint(second->width() - 30, 8));
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 1);
        QCOMPARE(f.canvas()->selection(), QList<ItemId>{second->itemId()});
    }

    // Scrolled with the wheel, the keys act on the card in the middle — not on
    // wherever the keyboard last left the cursor.
    void afterScrollingTheKeysActOnTheMiddleCard()
    {
        GuiFixture f;
        const auto id = seedNotes(f, 12);
        f.window.setSpotlight(true);
        const auto& placed = f.canvas()->boardForTest().placements();
        QVERIFY(placed.size() == 12);
        // As the wheel would: straight to the scroll bar.
        f.canvas()->verticalScrollBar()->setValue(placed[6].rect.center().y()
                                                  - f.canvas()->viewport()->height() / 2);
        QCOMPARE(f.canvas()->spotlitIndex(), 6);
        const ItemId middle = placed[6].id;
        f.canvas()->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(f.canvas(), Qt::Key_Delete);
        QTRY_COMPARE(f.items.countForBuffer(id), 11);
        QVERIFY(!f.items.find(middle) || f.items.find(middle)->bufferId != id);
    }

    // Independent review: a card selected by a click and then scrolled out of
    // sight stayed selected, and Delete trashed it instead of the card in the
    // middle.
    void aSelectionScrolledAwayDoesNotTakeTheKeys()
    {
        GuiFixture f;
        const auto id = seedNotes(f, 12);
        f.window.setSpotlight(true);
        ItemCard* second = nullptr;
        for (auto* c : f.canvas()->findChildren<ItemCard*>())
            if (f.canvas()->indexOf(c->itemId()) == 1) second = c;
        QVERIFY(second);
        const ItemId clicked = second->itemId();
        QTest::mouseClick(second, Qt::LeftButton, Qt::NoModifier, QPoint(second->width() - 30, 8));
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 1);
        QTRY_VERIFY(f.canvas()->verticalScrollBar()->value() > 0 || f.canvas()->spotlitIndex() == 1);
        QTest::qWait(250);   // let the glide finish
        const auto& placed = f.canvas()->boardForTest().placements();
        f.canvas()->verticalScrollBar()->setValue(placed[6].rect.center().y() - f.canvas()->viewport()->height() / 2);
        QCOMPARE(f.canvas()->spotlitIndex(), 6);
        const ItemId middle = placed[6].id;
        f.canvas()->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(f.canvas(), Qt::Key_Delete);
        QTRY_COMPARE(f.items.countForBuffer(id), 11);
        QVERIFY2(f.items.find(clicked) && f.items.find(clicked)->bufferId == id, "the hidden card was deleted");
        QVERIFY(!f.items.find(middle) || f.items.find(middle)->bufferId != id);
    }

    // Independent review: a resize in Spotlight made cards taller without
    // rebuilding them, so a long note's card held the old, shorter preview and
    // was drawn half empty under the clip fade.
    void aResizeInSpotlightExtendsALongNotesPreview()
    {
        GuiFixture f;
        QString paragraph;
        while (paragraph.size() < 14000) paragraph += QStringLiteral("one long paragraph of pasted words ");
        seedNotes(f, 1, paragraph);
        f.window.resize(1400, 600);
        QCoreApplication::processEvents();
        f.window.setSpotlight(true);
        f.window.resize(1400, 1400);
        QTRY_VERIFY(f.canvas()->viewport()->height() > 1000);
        QCoreApplication::processEvents();
        const int limit = f.canvas()->boardForTest().measureLimit();
        auto* editor = firstText(f)->findChild<QPlainTextEdit*>();
        QVERIFY2(editor->document()->characterCount() - 1 >= std::min<qsizetype>(limit, paragraph.size()) - 1,
                 qPrintable(QStringLiteral("loaded %1, limit %2").arg(editor->document()->characterCount()).arg(limit)));
        QCOMPARE(firstText(f)->text(), paragraph);   // and the whole note is still the note
    }

    // Independent review: a glide still running when another napkin opened
    // scrolled the new napkin to the old one's target.
    void aGlideDoesNotFollowYouToAnotherNapkin()
    {
        GuiFixture f;
        const auto first = seedNotes(f, 40);
        const auto second = f.buffers.create();
        for (int i = 0; i < 40; ++i) f.service.appendTo(second, Item::makeText(QStringLiteral("other %1").arg(i)));
        f.model()->reload();
        f.select(first);
        f.window.setSpotlight(true);
        f.canvas()->setCursorTo(30, Qt::NoModifier);   // starts a glide
        f.select(second);                               // before it can finish
        QTest::qWait(300);
        QVERIFY2(f.canvas()->spotlitIndex() <= 1,
                 qPrintable(QStringLiteral("spotlit %1 on the new napkin").arg(f.canvas()->spotlitIndex())));
    }

    // Independent review: with a thousand cards the ticks ran far past the
    // bottom of the rail, so most cards could not be reached from it.
    void theRailReachesEveryCardOnABigNapkin()
    {
        GuiFixture f;
        seedNotes(f, 400);
        f.window.setSpotlight(true);
        auto* strip = rail(f);
        QVERIFY(strip);
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, QPoint(strip->width() / 2, strip->height() - 9));
        QTRY_VERIFY2(f.canvas()->spotlitIndex() >= 395,
                     qPrintable(QStringLiteral("spotlit %1").arg(f.canvas()->spotlitIndex())));
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, QPoint(strip->width() / 2, strip->height() / 2));
        QTRY_VERIFY2(std::abs(f.canvas()->spotlitIndex() - 200) < 20,
                     qPrintable(QStringLiteral("middle of the rail -> %1").arg(f.canvas()->spotlitIndex())));
    }

    // The rail jumps to the tick you click.
    void theRailJumpsToACard()
    {
        GuiFixture f;
        seedNotes(f, 12);
        f.window.setSpotlight(true);
        auto* strip = rail(f);
        QVERIFY(strip);
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, QPoint(strip->width() / 2, strip->height() - 2));
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 11);   // the nearest tick to the bottom is the last
    }

    // A long note shows more of itself in Spotlight's taller card than the
    // board's preview holds — the card loads what the layout says it can show.
    void aLongNoteShowsMoreInSpotlight()
    {
        GuiFixture f;
        QString longText;
        for (int i = 0; i < 300; ++i) longText += QStringLiteral("line %1 of a long pasted log\n").arg(i);
        QVERIFY(longText.size() > tokens::kMeasureLimit * 2);
        seedNotes(f, 1, longText);
        widen(f);
        auto loaded = [&] { return firstText(f)->findChild<QPlainTextEdit*>()->document()->characterCount(); };
        const int boardLoaded = loaded();
        QVERIFY(boardLoaded <= tokens::kMeasureLimit + 1);
        const int boardHeight = firstText(f)->height();

        f.window.setSpotlight(true);
        const auto& board = f.canvas()->boardForTest();
        QVERIFY(board.measureLimit() > tokens::kMeasureLimit);
        QVERIFY2(loaded() > boardLoaded, qPrintable(QStringLiteral("%1 vs %2").arg(loaded()).arg(boardLoaded)));
        QVERIFY(firstText(f)->height() > boardHeight);
        QVERIFY(firstText(f)->height() <= board.maxCardHeight());
        // Still the whole note when edited, and nothing was lost on the way.
        QCOMPARE(firstText(f)->text(), longText);
    }

    // The card that was current stays current across the switch, both ways.
    void switchingKeepsYourPlace()
    {
        GuiFixture f;
        seedNotes(f, 12);
        widen(f);
        f.canvas()->setCursorTo(7, Qt::NoModifier);
        const ItemId seventh = f.canvas()->itemOrder().value(7);
        f.window.setSpotlight(true);
        QCOMPARE(f.canvas()->spotlitIndex(), 7);
        f.canvas()->bringToCentre(9, false);
        f.window.setSpotlight(false);
        QCOMPARE(f.canvas()->cursorIndex(), 9);
        const QRect r = f.canvas()->boardForTest().placements()[9].rect;
        const QRect view(0, f.canvas()->verticalScrollBar()->value(),
                         f.canvas()->viewport()->width(), f.canvas()->viewport()->height());
        QVERIFY(view.intersects(r));
        QVERIFY(seventh != kNoItem);
    }

    // Saved across launches; bound to Ctrl+. and checked in the menu.
    void spotlightIsRememberedAndInTheMenu()
    {
        {
            GuiFixture f;
            auto* action = f.window.findChild<QAction*>(QStringLiteral("spotlightAction"));
            QVERIFY(action && action->isCheckable());
            QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+.")));
            QVERIFY(f.window.actions().contains(action));
            action->trigger();
            QVERIFY(f.canvas()->spotlight());
            QVERIFY(action->isChecked());
        }
        {
            GuiFixture again;
            QVERIFY(again.canvas()->spotlight());
            again.window.setSpotlight(false);
        }
        GuiFixture third;
        QVERIFY(!third.canvas()->spotlight());
    }

    // Enter edits the card in the middle, and what is typed is saved.
    void enterEditsTheCentredCard()
    {
        GuiFixture f;
        const auto id = seedNotes(f, 5);
        f.window.setSpotlight(true);
        f.canvas()->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(f.canvas(), Qt::Key_Down);
        QTest::keyClick(f.canvas(), Qt::Key_Down);
        QTRY_COMPARE(f.canvas()->spotlitIndex(), 2);
        const ItemId target = f.canvas()->itemOrder().value(2);
        QTest::keyClick(f.canvas(), Qt::Key_Return);
        TextItemCard* editing = nullptr;
        for (auto* c : f.canvas()->findChildren<TextItemCard*>())
            if (c->hasEditFocus()) editing = c;
        QVERIFY(editing);
        QCOMPARE(editing->itemId(), target);
        editing->insertText(QStringLiteral(" EDITED"));
        QVERIFY(f.window.flushForTest(false));
        QVERIFY(f.items.find(target)->text.endsWith(QStringLiteral(" EDITED")));
        QCOMPARE(f.items.countForBuffer(id), 5);
    }

    // A long note clips at the scaled height, and on a whole line: the board
    // and the card agree on where the cut is at every zoom.
    void aLongNoteClipsAtTheScaledHeight()
    {
        GuiFixture f;
        QString longText;
        for (int i = 0; i < 80; ++i) longText += QStringLiteral("line %1 of a long note\n").arg(i);
        seedNotes(f, 1, longText);
        f.window.setBoardZoom(1.4);
        const auto& board = f.canvas()->boardForTest();
        QVERIFY(!board.placements().empty());
        const auto& slot = board.placements().front();
        QVERIFY(slot.clipped);
        QVERIFY(slot.rect.height() <= board.maxCardHeight());
        QVERIFY(slot.rect.height() > board.maxCardHeight() - QFontMetrics(f.canvas()->font()).lineSpacing() - 1);
        auto* card = firstText(f);
        QVERIFY(card);
        QCOMPARE(card->height(), slot.rect.height());
    }
};

QTEST_MAIN(TestView)
#include "test_view.moc"
