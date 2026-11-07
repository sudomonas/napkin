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
