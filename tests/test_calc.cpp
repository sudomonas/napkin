#include "../src/domain/Calc.h"
#include <QtTest>

using namespace napkin;
using namespace napkin::calc;

// SPEC.md §7 "Calculating a line". The parser is the whole feature below the
// editor, so the rules — including what it must refuse to touch — are pinned
// here rather than through the widget tree.
class TestCalc : public QObject {
    Q_OBJECT

    static QString answer(const char* expression)
    {
        return format(evaluate(QString::fromUtf8(expression)));
    }

    // The line after Ctrl+Tab, or the line unchanged when there was nothing to do.
    static QString line(const char* text, Scope scope = Scope::CaretLine)
    {
        QString s = QString::fromUtf8(text);
        const LineResult r = calculateLine(s, scope);
        if (r.edit) s.replace(r.edit->start, r.edit->length, r.edit->text);
        return s;
    }

private slots:
    void arithmetic_data()
    {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("expected");
        const auto row = [](const char* e, const char* x) {
            QTest::newRow(e) << QString::fromUtf8(e) << QString::fromUtf8(x);
        };
        row("2+2", "4");
        row("2 + 3 * 4", "14");
        row("(2 + 3) * 4", "20");
        row("10 - 4 - 3", "3");           // left-associative
        row("100 / 8", "12.5");
        row("2^10", "1024");
        row("2**10", "1024");
        row("2^3^2", "512");              // right-associative
        row("2^-1", "0.5");
        row("-2^2", "-4");                // unary minus binds looser than power
        row("-(3+4)", "-7");
        row("--5+1", "6");
        row("7 mod 3", "1");
        row("3 x 4", "12");
        row("3x4", "12");
        row("3 × 4 ÷ 2 − 1", "5");        // what a phone keyboard types
        row(".5 + .25", "0.75");
        row("1e3 + 1", "1001");
        row("2.5e-1", "0.25");
    }
    void arithmetic()
    {
        QFETCH(QString, expression);
        QFETCH(QString, expected);
        QCOMPARE(format(evaluate(expression)), expected);
    }

    // Binary floating point is not what a note should show.
    void answersAreRoundedToWhatAPersonWrote()
    {
        QCOMPARE(answer("0.1 + 0.2"), QStringLiteral("0.3"));
        QCOMPARE(answer("1/3"), QStringLiteral("0.333333333333"));
        QCOMPARE(answer("sin(pi)"), QStringLiteral("0"));
        QCOMPARE(answer("0 * -1"), QStringLiteral("0"));   // never "-0"
        // A 13-digit total is still written in full.
        QCOMPARE(answer("1234567890123 + 1"), QStringLiteral("1234567890124"));
        QCOMPARE(answer("1e20 * 10"), QStringLiteral("1e+21"));
    }

    void percentMeansWhatItMeansOnACalculator()
    {
        QCOMPARE(answer("200 + 10%"), QStringLiteral("220"));
        QCOMPARE(answer("200 - 10%"), QStringLiteral("180"));
        QCOMPARE(answer("200 * 10%"), QStringLiteral("20"));
        QCOMPARE(answer("15% of 80"), QStringLiteral("12"));
        QCOMPARE(answer("50%"), QStringLiteral("0.5"));
        // "of" is only an operator after a percent: "3 of 4" is not arithmetic.
        QVERIFY(!evaluate(QStringLiteral("3 of 4")).value);
    }

    void functionsAndConstants()
    {
        QCOMPARE(answer("sqrt(16) + abs(-2)"), QStringLiteral("6"));
        QCOMPARE(answer("sqrt 81"), QStringLiteral("9"));
        QCOMPARE(answer("round(2.5) + floor(1.9) + ceil(1.1)"), QStringLiteral("6"));
        QCOMPARE(answer("log(1000)"), QStringLiteral("3"));
        QCOMPARE(answer("ln(e)"), QStringLiteral("1"));
        QCOMPARE(answer("2 * pi"), QStringLiteral("6.28318530718"));
        QCOMPARE(answer("π"), QStringLiteral("3.14159265359"));
        QCOMPARE(answer("SQRT(4)"), QStringLiteral("2"));
    }

    void groupedNumbersKeepTheirGrouping()
    {
        QCOMPARE(answer("1,200 * 12"), QStringLiteral("14,400"));
        QCOMPARE(answer("1,000,000 / 3"), QStringLiteral("333,333.333333"));
        // Lakh and crore grouping, as it is written in India.
        QCOMPARE(answer("1,20,000 * 12"), QStringLiteral("14,40,000"));
        QCOMPARE(answer("12000 * 12"), QStringLiteral("144000"));   // ungrouped in, ungrouped out
        // A comma that is neither grouping is not silently dropped.
        QVERIFY(!evaluate(QStringLiteral("1,2 + 3")).value);
        QVERIFY(!evaluate(QStringLiteral("1,2345")).value);
    }

    void currencyIsCarriedThrough()
    {
        QCOMPARE(answer("$12.50 * 4"), QStringLiteral("$50"));
        QCOMPARE(answer("₹1,200 * 12"), QStringLiteral("₹14,400"));
        QCOMPARE(answer("€5 - €8"), QStringLiteral("-€3"));
        QCOMPARE(answer("£ 3 + 4"), QStringLiteral("£7"));
        // Adding dollars to euros has no single answer.
        QVERIFY(!evaluate(QStringLiteral("$5 + €3")).value);
    }

    void failuresSayWhy()
    {
        QCOMPARE(evaluate(QStringLiteral("5 / 0")).error, Error::DivideByZero);
        QCOMPARE(evaluate(QStringLiteral("5 mod 0")).error, Error::DivideByZero);
        QCOMPARE(evaluate(QStringLiteral("sqrt(-1)")).error, Error::Undefined);
        QCOMPARE(evaluate(QStringLiteral("ln(0)")).error, Error::Undefined);
        QCOMPARE(evaluate(QStringLiteral("10^400")).error, Error::Undefined);
        for (const char* bad : {"", "   ", "2 +", "(2 + 3", "2 3", "12px + 3", "foo(2)",
                                "2 ** ** 3", "hello", "3rd + 1"}) {
            const Evaluation ev = evaluate(QString::fromUtf8(bad));
            QVERIFY2(!ev.value, bad);
            QCOMPARE(ev.error, Error::NotAnExpression);
        }
    }

    // Found by hand, not by construction: a recursive-descent parser overflows
    // its stack on deep nesting, and a note can hold any pasted text at all.
    void hostileInputIsRefusedNotCrashedOn()
    {
        const QString deep = QString(100000, u'(') + u'1' + QString(100000, u')');
        QVERIFY(!evaluate(deep).value);
        QVERIFY(!evaluate(QString(100000, u'-') + u'1').value);
        // Those two are refused by the length cap before the parser sees them.
        // Deep nesting inside the cap is what the depth limit exists for — 400
        // levels is several hundred stack frames, and Windows gives the main
        // thread 1 MB.
        QVERIFY(!evaluate(QString(400, u'(') + u'1' + QString(400, u')')).value);
        QVERIFY(!evaluate(QString(900, u'-') + u'1').value);
        const QString longLine = QStringLiteral("1+").repeated(5000) + u'1';
        QVERIFY(!calculateLine(longLine, Scope::CaretLine).edit);
        // Nesting a person would actually write still works.
        QCOMPARE(answer("((((((1+1))))))*2"), QStringLiteral("4"));
    }

    void aLineEndingInEqualsGetsItsAnswer()
    {
        QCOMPARE(line("12*3 ="), QStringLiteral("12*3 = 36"));
        QCOMPARE(line("12*3="), QStringLiteral("12*3=36"));      // spacing follows the writer
        QCOMPARE(line("12*3 =   "), QStringLiteral("12*3 = 36"));
        QCOMPARE(line("rent 1200*12 ="), QStringLiteral("rent 1200*12 = 14400"));
        QCOMPARE(line("Balance -5+3 ="), QStringLiteral("Balance -5+3 = -2"));
        QCOMPARE(line("groceries: ₹450 + ₹1,200 ="), QStringLiteral("groceries: ₹450 + ₹1,200 = ₹1,650"));
    }

    void aShownAnswerIsRecalculated()
    {
        // Change 12 to 13 and ask again: the old answer is replaced, not kept.
        QCOMPARE(line("13*3 = 36"), QStringLiteral("13*3 = 39"));
        QCOMPARE(line("tip 15% of 80 = 0"), QStringLiteral("tip 15% of 80 = 12"));
        QCOMPARE(line("total ₹1,200 * 12 = ₹14,000"), QStringLiteral("total ₹1,200 * 12 = ₹14,400"));
    }

    void aBareCalculationGetsAnswerAppended()
    {
        QCOMPARE(line("12*3"), QStringLiteral("12*3 = 36"));
        QCOMPARE(line("rent 1200 * 12  "), QStringLiteral("rent 1200 * 12 = 14400"));
        // What follows "=" is an expression, not an answer, so the whole
        // right-hand side is what gets calculated.
        QCOMPARE(line("x = 5+3"), QStringLiteral("x = 5+3 = 8"));
    }

    // The failure mode that would make the feature untrustworthy: rewriting a
    // line that was never arithmetic.
    void proseIsLeftAlone()
    {
        for (const char* prose : {"Chapter 3", "I have 3 cats", "x = 5", "see you at 10",
                                  "v0.1.6", "https://example.org/a-b", "a = b", "=",
                                  "12px", "Room 12 =", "just words ="}) {
            const LineResult r = calculateLine(QString::fromUtf8(prose), Scope::CaretLine);
            QVERIFY2(!r.edit, prose);
        }
    }

    // --- Independent review, 2026-09-24. Each of these was a wrong answer,
    // not a refusal: the tail that parsed was calculated and the rest of what
    // the user wrote was silently dropped. ---

    void aListBulletIsNotAMinusSign()
    {
        QCOMPARE(line("- 5 + 3 ="), QStringLiteral("- 5 + 3 = 8"));
        QCOMPARE(line("  - 100 + 50 ="), QStringLiteral("  - 100 + 50 = 150"));
        QCOMPARE(line("* 12*3 ="), QStringLiteral("* 12*3 = 36"));
        QCOMPARE(line("1. 5+3 ="), QStringLiteral("1. 5+3 = 8"));
        QCOMPARE(line("2) 4*4 ="), QStringLiteral("2) 4*4 = 16"));
        QCOMPARE(line("- 5 + 3 = 8", Scope::MarkedOnly), QStringLiteral("- 5 + 3 = 8"));
        // A minus sign that is a minus sign still is one.
        QCOMPARE(line("-5 + 3 ="), QStringLiteral("-5 + 3 = -2"));
    }

    void partOfACalculationIsNotAnswered()
    {
        for (const char* partial : {"1 000 000 * 2 =", "1 500 + 250 =", "(2+3)*(4+5 =",
                                    "2 (3+4) =", "3 sqrt(4) =", "2 pi", "10:30 + 45 =",
                                    "Meeting 2024-01-15", "£5 + $5 =", "$10 * 3 - €2 =",
                                    "0x10", "error at 0x00401000", "call 555 0123 + 4 ="}) {
            const LineResult r = calculateLine(QString::fromUtf8(partial), Scope::CaretLine);
            QVERIFY2(!r.edit, qPrintable(QString::fromUtf8(partial) + " -> "
                                         + (r.edit ? r.edit->text : QString())));
        }
    }

    // The depth limit held for the whole line, and the tail after the
    // outermost 64 levels was then answered instead.
    void tooDeepIsRefusedForTheWholeLine()
    {
        const QString minuses = QString(101, u'-') + QStringLiteral("(1+2) =");
        QVERIFY(!calculateLine(minuses, Scope::CaretLine).edit);
        const QString roots = QStringLiteral("sqrt ").repeated(80) + QStringLiteral("65536 =");
        QVERIFY(!calculateLine(roots, Scope::CaretLine).edit);
        QVERIFY(!calculateLine(QStringLiteral("compute ") + roots, Scope::CaretLine).edit);
    }

    void aFunctionBindsTighterThanPowerAndPercent()
    {
        QCOMPARE(answer("log(1000)^2"), QStringLiteral("9"));
        QCOMPARE(answer("round(2.5)^2"), QStringLiteral("9"));
        QCOMPARE(answer("ln(e)^2"), QStringLiteral("1"));
        QCOMPARE(answer("sqrt(16)%"), QStringLiteral("0.04"));
        QCOMPARE(answer("sqrt 81 + 1"), QStringLiteral("10"));
        QCOMPARE(answer("sin 30°"), QStringLiteral("0.5"));
        QCOMPARE(answer("sin(30°) * 2"), QStringLiteral("1"));
        QCOMPARE(answer("cos(60°)"), QStringLiteral("0.5"));
    }

    void largeAndSmallAnswersKeepTheirDigits()
    {
        QCOMPARE(answer("1e14 + 0.5"), QStringLiteral("100000000000000.5"));
        // Large amounts keep their cents, and are rounded to them.
        QCOMPARE(answer("1000000000000 - 0.01"), QStringLiteral("999999999999.99"));
        // More digits than twelve for ordinary numbers is where float noise
        // shows: 4.35*100 is 434.99999999999994 in binary.
        QCOMPARE(answer("4.35 * 100"), QStringLiteral("435"));
        QCOMPARE(answer("$1,234,567,890,123.50 + 1"), QStringLiteral("$1,234,567,890,124.5"));
        QCOMPARE(answer("0.00001 * 1"), QStringLiteral("0.00001"));
        QCOMPARE(answer("-0.0000001 * 1"), QStringLiteral("-0.0000001"));
        QCOMPARE(answer("$5 - $5"), QStringLiteral("$0"));
        // Past what a double holds exactly, scientific is the honest form.
        QCOMPARE(answer("999999999999999 + 1"), QStringLiteral("1e+15"));
        QCOMPARE(answer("1e-12 * 1"), QStringLiteral("1e-12"));
    }

    // isDigit() accepts every script's digits and toDouble() only ASCII, so
    // these were refused as "That has no answer" (found re-running the
    // review's inputs).
    void digitsInAnyScriptAreNumbers()
    {
        QCOMPARE(answer("५*२"), QStringLiteral("10"));
        QCOMPARE(answer("٣+٤"), QStringLiteral("7"));
        QCOMPARE(answer("１２+３"), QStringLiteral("15"));
    }

    void aNumberTooLargeToHoldHasNoAnswer()
    {
        QCOMPARE(evaluate(QStringLiteral("1e400 * 1")).error, Error::Undefined);
        QCOMPARE(calculateLine(QStringLiteral("1e400*1 ="), Scope::CaretLine).error, Error::Undefined);
    }

    // Without an "=" the line did not ask, so a trailing constant or function
    // in prose is not a request.
    void proseEndingInAConstantIsNotARequest()
    {
        for (const char* prose : {"I like pi", "option e", "check the log 100", "sqrt(16)"}) {
            const LineResult r = calculateLine(QString::fromUtf8(prose), Scope::CaretLine);
            QVERIFY2(!r.edit, prose);
        }
        // With an "=", it asked.
        QCOMPARE(line("pi ="), QStringLiteral("pi = 3.14159265359"));
        QCOMPARE(line("sqrt(16) ="), QStringLiteral("sqrt(16) = 4"));
    }

    void aSelectionIsCalculatedExactly()
    {
        const auto select = [](const char* text, const char* part) {
            QString s = QString::fromUtf8(text);
            const int from = int(s.indexOf(QString::fromUtf8(part)));
            const LineResult r = calculateSelection(s, from, from + int(strlen(part)));
            if (r.edit) s.replace(r.edit->start, r.edit->length, r.edit->text);
            return s;
        };
        QCOMPARE(select("boxes of 2*3 each", "2*3"), QStringLiteral("boxes of 2*3 = 6 each"));
        // Followed by an answer slot: the answer goes there, not beside it.
        QCOMPARE(select("2+3 = 5", "2+3"), QStringLiteral("2+3 = 5"));
        QCOMPARE(select("2+4 = 5", "2+4"), QStringLiteral("2+4 = 6"));
        QCOMPARE(select("2+4 =", "2+4"), QStringLiteral("2+4 = 6"));
        // A lone number is no more a calculation selected than unselected.
        QCOMPARE(select("Chapter 3", "3"), QStringLiteral("Chapter 3"));
    }

    // A multi-line selection recalculates only lines that ask for an answer.
    void markedOnlyIgnoresLinesThatDidNotAsk()
    {
        QCOMPARE(line("2+2 =", Scope::MarkedOnly), QStringLiteral("2+2 = 4"));
        QCOMPARE(line("2+3 = 4", Scope::MarkedOnly), QStringLiteral("2+3 = 5"));
        QCOMPARE(line("Call 555-1234", Scope::MarkedOnly), QStringLiteral("Call 555-1234"));
        QCOMPARE(line("12*3", Scope::MarkedOnly), QStringLiteral("12*3"));
        QCOMPARE(line("x = 5+3", Scope::MarkedOnly), QStringLiteral("x = 5+3"));
    }

    void aLineThatCannotBeCalculatedSaysWhy()
    {
        QCOMPARE(calculateLine(QStringLiteral("share 100 / 0 ="), Scope::CaretLine).error,
                 Error::DivideByZero);
        QCOMPARE(calculateLine(QStringLiteral("sqrt(-4)"), Scope::CaretLine).error, Error::Undefined);
        QCOMPARE(calculateLine(QStringLiteral("nothing here"), Scope::CaretLine).error,
                 Error::NotAnExpression);
        QVERIFY(!calculateLine(QStringLiteral("share 100 / 0 ="), Scope::CaretLine).edit);
    }
};

QTEST_APPLESS_MAIN(TestCalc)
#include "test_calc.moc"
