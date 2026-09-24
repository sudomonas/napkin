#include <QApplication>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextBlock>
#include "GuiFixture.h"
#include <QtTest>

using namespace napkin;

// SPEC.md §7 "Calculating a line", through the real editor. The parser's rules
// are pinned in test_calc; this is about the key, the menu, undo and saving.
class TestCalculate : public QObject {
    Q_OBJECT

    // A composer to type into, in an active window, so key events go through
    // the same shortcut dispatch they do in the app. Without activateWindow()
    // a QAction bound to Ctrl+Tab would never get the chance to steal it.
    static QPlainTextEdit* typeInto(GuiFixture& f, const char* text)
    {
        f.window.activateWindow();
        auto* edit = f.newTextCard();
        if (!edit) return nullptr;
        // keyClicks cannot type a newline; Enter is how a person makes one.
        const QStringList lines = QString::fromUtf8(text).split(u'\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (i > 0) QTest::keyClick(edit, Qt::Key_Return);
            QTest::keyClicks(edit, lines[i]);
        }
        return edit;
    }

    static TextItemCard* cardOf(QPlainTextEdit* edit)
    {
        QObject* o = edit;
        while (o && !qobject_cast<TextItemCard*>(o)) o = o->parent();
        return qobject_cast<TextItemCard*>(o);
    }

    static void ctrlTab(QPlainTextEdit* edit) { QTest::keyClick(edit, Qt::Key_Tab, Qt::ControlModifier); }

private slots:
    void ctrlTabAnswersTheLineAndSavesIt()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "rent 1200*12 =");
        QVERIFY(edit);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("rent 1200*12 = 14400"));
        // The caret is after the answer, so the next line can just be typed.
        QCOMPARE(edit->textCursor().position(), edit->toPlainText().size());

        // The answer is ordinary text: it is what autosave writes.
        QTRY_COMPARE_WITH_TIMEOUT(f.buffers.countLive(), 1, 2000);
        const auto id = f.buffers.listLive(10).front().id;
        QTRY_COMPARE_WITH_TIMEOUT(f.items.listForBuffer(id).front().text,
                                  QStringLiteral("rent 1200*12 = 14400"), 2000);
    }

    void onlyTheCaretLineIsTouched()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "1+1 =\n2+2 =");
        QVERIFY(edit);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("1+1 =\n2+2 = 4"));
    }

    void plainTabStillMovesOn()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "12*3 =");
        QVERIFY(edit);
        QTest::keyClick(edit, Qt::Key_Tab);
        QCOMPARE(edit->toPlainText(), QStringLiteral("12*3 ="));
    }

    // Typing and then calculating must be two undo steps, not one: undo
    // taking the answer should leave the sum that was typed.
    void oneUndoTakesBackOnlyTheAnswer()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "12*3 =");
        QVERIFY(edit);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("12*3 = 36"));
        QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(edit->toPlainText(), QStringLiteral("12*3 ="));
    }

    // And typing after it must not merge into it either: fixing a typo in
    // what followed should not take the answer away (independent review).
    void undoAfterTypingKeepsTheAnswer()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "12*3 =");
        QVERIFY(edit);
        ctrlTab(edit);
        QTest::keyClicks(edit, " ok");
        QCOMPARE(edit->toPlainText(), QStringLiteral("12*3 = 36 ok"));
        QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(edit->toPlainText(), QStringLiteral("12*3 = 36"));
        QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(edit->toPlainText(), QStringLiteral("12*3 ="));
    }

    void recalculatingReplacesTheOldAnswer()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "13*3 = 36");
        QVERIFY(edit);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("13*3 = 39"));

        // Asking again when nothing changed changes nothing, not even undo.
        const int steps = edit->document()->availableUndoSteps();
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("13*3 = 39"));
        QCOMPARE(edit->document()->availableUndoSteps(), steps);
    }

    void proseIsNotRewritten()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "Chapter 3");
        QVERIFY(edit);
        const int steps = edit->document()->availableUndoSteps();
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("Chapter 3"));
        QCOMPARE(edit->document()->availableUndoSteps(), steps);
        QVERIFY(!cardOf(edit)->calculate());
    }

    void aSelectionInsideALineIsCalculatedExactly()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "boxes of 2*3 each");
        QVERIFY(edit);
        QTextCursor c = edit->textCursor();
        c.setPosition(9);
        c.setPosition(12, QTextCursor::KeepAnchor);
        edit->setTextCursor(c);
        QCOMPARE(c.selectedText(), QStringLiteral("2*3"));
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("boxes of 2*3 = 6 each"));
    }

    // Select the whole note and recalculate it: lines that asked for an answer
    // get one, the rest are left exactly as they were, and one undo reverts all.
    void aMultiLineSelectionRecalculatesWhatAsked()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "1+1 =\nCall 555-1234\n2*3 = 5\n12*3");
        QVERIFY(edit);
        edit->selectAll();
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("1+1 = 2\nCall 555-1234\n2*3 = 6\n12*3"));
        QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(edit->toPlainText(), QStringLiteral("1+1 =\nCall 555-1234\n2*3 = 5\n12*3"));
    }

    // Shift+Down twice from the start selects up to the start of line 3. That
    // is two lines, and line 3 is not one of them (independent review).
    void aSelectionEndingAtALineStartStopsBeforeIt()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "1+1 =\n2+2 =\n3+3 =");
        QVERIFY(edit);
        edit->moveCursor(QTextCursor::Start);
        QTest::keyClick(edit, Qt::Key_Down, Qt::ShiftModifier);
        QTest::keyClick(edit, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(edit->textCursor().selectionEnd(), 12);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("1+1 = 2\n2+2 = 4\n3+3 ="));
    }

    // A whole line selected, newline included, is that line.
    void aWholeLineSelectedIsThatLine()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "1+1 =\n2+2 =");
        QVERIFY(edit);
        edit->moveCursor(QTextCursor::Start);
        QTest::keyClick(edit, Qt::Key_Down, Qt::ShiftModifier);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("1+1 = 2\n2+2 ="));
    }

    void selectingASumBeforeItsAnswerReplacesTheAnswer()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "2+4 = 5");
        QVERIFY(edit);
        QTextCursor c = edit->textCursor();
        c.setPosition(0);
        c.setPosition(3, QTextCursor::KeepAnchor);
        edit->setTextCursor(c);
        ctrlTab(edit);
        QCOMPARE(edit->toPlainText(), QStringLiteral("2+4 = 6"));
    }

    // Right-clicking a line and choosing Calculate calculates that line, not
    // the one the caret was left on (independent review).
    void theMenuActsOnTheLineThatWasClicked()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "1+1 =\n\n\n\n2+2 =");
        QVERIFY(edit);
        edit->moveCursor(QTextCursor::Start);
        QTextCursor line5(edit->document()->findBlockByNumber(4));
        const QPoint at = edit->cursorRect(line5).center();
        QContextMenuEvent e(QContextMenuEvent::Mouse, at, edit->viewport()->mapToGlobal(at));
        QApplication::sendEvent(edit->viewport(), &e);

        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible()) menu = m;
        QVERIFY(menu);
        auto* calculate = menu->findChild<QAction*>(QStringLiteral("calculateAction"));
        QVERIFY(calculate);
        menu->close();
        calculate->trigger();
        QCOMPARE(edit->toPlainText(), QStringLiteral("1+1 =\n\n\n\n2+2 = 4"));
    }

    void theMenuOffersCalculate()
    {
        GuiFixture f;
        auto* edit = typeInto(f, "7*6 =");
        QVERIFY(edit);
        QContextMenuEvent e(QContextMenuEvent::Mouse, QPoint(5, 5),
                            edit->viewport()->mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(edit->viewport(), &e);

        QMenu* menu = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible()) menu = m;
        QVERIFY(menu);
        auto* calculate = menu->findChild<QAction*>(QStringLiteral("calculateAction"));
        QVERIFY(calculate);
        QCOMPARE(calculate->text(), QStringLiteral("Calculate\tCtrl+Tab"));
        // The standard entries are still there beside it.
        bool hasCopy = false;
        for (QAction* a : menu->actions()) hasCopy = hasCopy || a->text().contains(QStringLiteral("Copy"));
        QVERIFY(hasCopy);

        menu->close();
        calculate->trigger();
        QCOMPARE(edit->toPlainText(), QStringLiteral("7*6 = 42"));
    }

    // A card nobody is editing is read-only; calculating it would be an edit
    // the user never started.
    void aCardNotBeingEditedIsLeftAlone()
    {
        GuiFixture f;
        f.select(f.seed("2+2 ="));
        TextItemCard* card = nullptr;
        for (auto* c : f.window.findChildren<TextItemCard*>())
            if (!c->isComposer()) card = c;
        QVERIFY(card);
        QVERIFY(!card->hasEditFocus());
        QVERIFY(!card->calculate());
        QCOMPARE(card->text(), QStringLiteral("2+2 ="));
    }
};

QTEST_MAIN(TestCalculate)
#include "test_calculate.moc"
