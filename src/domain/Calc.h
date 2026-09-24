#pragma once
#include <QString>
#include <optional>

namespace napkin::calc {

// SPEC.md §7 "Calculating a line". Arithmetic, not inference: the same text
// always gives the same answer, and nothing here guesses what a line meant.
// The editor asks for a calculation (Ctrl+Tab); nothing is ever evaluated or
// rewritten on its own.

enum class Error {
    None,
    NotAnExpression,   // nothing on the line that parses as a calculation
    DivideByZero,
    Undefined,         // sqrt(-1), ln(0), an overflow: no real, finite answer
};

struct Evaluation {
    std::optional<double> value;
    Error   error = Error::NotAnExpression;
    QString currency;       // "$", "₹"… when the operands carried one, and all the same one
    QChar   grouping;       // 0, or u'w' (1,234,567) / u'i' (12,34,567) as the input wrote it
    bool    bareNumber = false;   // the whole expression was one number: nothing to calculate
    bool    hasOperator = false;  // at least one binary operator: + - * / ^ x mod of
};

// The whole of `expression` must parse, or the answer is NotAnExpression.
// Operators: + - * / ^ ** x × ÷ − mod, parentheses, a postfix %, "X% of Y".
// A percent after + or - is a calculator percent: 200 + 10% is 220.
// Functions: sqrt abs round floor ceil ln log sin cos tan — radians, or 30°.
// Constants: pi π e. Numbers: 1234, 1.5, .5, 1e3, 1,234 and 1,23,456 — but
// not 007 or 0x1F, which are identifiers rather than quantities.
Evaluation evaluate(const QString& expression);

// The answer as it is written back into the note: at most twelve significant
// digits, so 0.1+0.2 reads 0.3, grouped and prefixed the way the input was.
QString format(const Evaluation& evaluation);

// A change to one line: replace [start, start+length) with `text`.
struct LineEdit {
    int     start  = 0;
    int     length = 0;
    QString text;
};

struct LineResult {
    std::optional<LineEdit> edit;
    Error error = Error::NotAnExpression;
};

enum class Scope {
    // The line the caret is on, asked for explicitly. A line ending in "=" gets
    // its answer; a line already showing one gets it recalculated; any other
    // line ending in a calculation with an operator gets " = answer" appended.
    CaretLine,
    // One line of a multi-line selection. Only lines that ask for an answer —
    // ending in "=" or already showing one — are touched, so selecting a whole
    // note and recalculating it never appends to "Call 555-1234".
    MarkedOnly,
};

// The calculation is the run at the end of the line (or of the part before
// its last "=") that parses, and that starts where a calculation plausibly
// can: the start of the line, after a list bullet, or after prose — never
// straight after a digit, a bracket, an operator or a currency sign. So
// "rent 1200*12 =" calculates 1200*12, while "1 500 + 250 =" is refused
// rather than answered as 500 + 250.
LineResult calculateLine(const QString& line, Scope scope);

// The selection [from, to) of `line`, exactly. When the rest of the line is
// "=" or "= <old answer>", that is where the answer goes; otherwise
// " = answer" follows the selection.
LineResult calculateSelection(const QString& line, int from, int to);

}  // namespace napkin::calc
