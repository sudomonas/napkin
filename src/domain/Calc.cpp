#include "Calc.h"

#include <QList>

#include <cmath>
#include <numbers>

namespace napkin::calc {
namespace {

// "((((((…" is a stack overflow in a recursive-descent parser, and a pasted log
// can hold any line at all. Neither bound is reachable by a real calculation.
constexpr int kMaxDepth = 64;
constexpr int kMaxLine  = 1000;

// A percent is remembered as one, so that "200 + 10%" can mean what it means on
// every pocket calculator (220) rather than 200.1.
struct Value {
    double v = 0;
    bool percent = false;
};

bool isCurrency(QChar c)
{
    return c == u'$' || c == u'€' || c == u'£' || c == u'¥' || c == u'₹';
}

bool isOperator(QChar c)
{
    return c == u'+' || c == u'-' || c == u'−' || c == u'*' || c == u'/' || c == u'^'
        || c == u'×' || c == u'÷' || c == u'%';
}

// What evaluate() returns, plus whether the failure condemns the whole line.
// Some refusals must not be escaped by calculating a shorter tail: "£5 + $5"
// is refused, and so must be the "$5" at its end; 101 minus signs before
// "(1+2)" are too deep, and the innermost 64 of them are not the answer.
struct Parsed {
    Evaluation evaluation;
    bool conclusive = false;
};

class Parser {
public:
    explicit Parser(const QString& s) : s_(s) {}

    Parsed run()
    {
        Parsed out;
        skipSpace();
        if (atEnd()) return out;
        Value v = expr();
        skipSpace();
        if (tooDeep_ || mixedCurrency_) { out.conclusive = true; return out; }
        if (failed_ || !atEnd()) return out;

        Evaluation& ev = out.evaluation;
        ev.currency = currency_;
        ev.grouping = grouping_;
        ev.bareNumber = operations_ == 0;
        ev.hasOperator = binary_ > 0;
        if (error_ != Error::None) { ev.error = error_; return out; }
        if (!std::isfinite(v.v)) { ev.error = Error::Undefined; return out; }
        // sin(pi) is 1.2e-16 in binary floating point. That is noise from
        // the function, not an answer anyone wants written into a note.
        if (trig_ && std::abs(v.v) < 1e-12) v.v = 0;
        ev.value = v.v;
        ev.error = Error::None;
        return out;
    }

private:
    const QString& s_;
    int pos_ = 0;
    int depth_ = 0;
    bool failed_ = false;
    bool tooDeep_ = false;
    Error error_ = Error::None;
    QString currency_;
    bool mixedCurrency_ = false;
    QChar grouping_;
    int operations_ = 0;   // anything that makes it more than a number
    int binary_ = 0;       // of those, the binary operators
    bool trig_ = false;

    bool atEnd() const { return pos_ >= s_.size(); }
    QChar at(int i) const { return i < s_.size() ? s_[i] : QChar(); }
    void skipSpace() { while (!atEnd() && s_[pos_].isSpace()) ++pos_; }
    Value fail() { failed_ = true; return {}; }
    void semantic(Error e) { if (error_ == Error::None) error_ = e; }

    // The one depth limit. Every nesting — a parenthesis, a run of minus
    // signs — passes through unary(), and a function's argument through its
    // own call in primary(); both take one of these.
    struct Nest {
        Parser& p;
        bool ok;
        explicit Nest(Parser& parser) : p(parser), ok(++p.depth_ <= kMaxDepth)
        {
            if (!ok) { p.tooDeep_ = true; p.failed_ = true; }
        }
        ~Nest() { --p.depth_; }
    };

    bool eat(QChar c)
    {
        skipSpace();
        if (at(pos_) != c) return false;
        ++pos_;
        return true;
    }

    // A word operator must be a whole word: "model" does not contain "mod".
    bool eatWord(QLatin1String word)
    {
        skipSpace();
        if (s_.size() - pos_ < word.size()) return false;
        if (QStringView(s_).mid(pos_, word.size()).compare(word, Qt::CaseInsensitive) != 0)
            return false;
        if (at(pos_ + int(word.size())).isLetter()) return false;
        pos_ += int(word.size());
        return true;
    }

    Value expr()
    {
        Value left = term();
        while (!failed_) {
            int sign = 0;
            if (eat(u'+')) sign = 1;
            else if (eat(u'-') || eat(u'−')) sign = -1;
            else break;
            const Value right = term();
            if (failed_) break;
            ++operations_;
            ++binary_;
            if (right.percent && !left.percent) left.v *= 1 + sign * right.v;
            else left.v += sign * right.v;
            left.percent = left.percent && right.percent;
        }
        return left;
    }

    Value term()
    {
        Value left = unary();
        while (!failed_) {
            skipSpace();
            enum { Mul, Div, Mod, Of } op;
            // "**" is a power, and power() has already taken any that follow a
            // factor — but only if it saw them first, so check before eating "*".
            if (at(pos_) == u'*' && at(pos_ + 1) != u'*') { ++pos_; op = Mul; }
            else if (eat(u'×')) op = Mul;
            else if ((at(pos_) == u'x' || at(pos_) == u'X') && !at(pos_ + 1).isLetter()) { ++pos_; op = Mul; }
            else if (eat(u'/') || eat(u'÷')) op = Div;
            else if (eatWord(QLatin1String("mod"))) op = Mod;
            else if (left.percent && eatWord(QLatin1String("of"))) op = Of;
            else break;

            const Value right = unary();
            if (failed_) break;
            ++operations_;
            ++binary_;
            switch (op) {
            case Mul:
            case Of:
                left.v *= right.v;
                break;
            case Div:
                if (right.v == 0) semantic(Error::DivideByZero);
                left.v /= right.v;
                break;
            case Mod:
                if (right.v == 0) semantic(Error::DivideByZero);
                left.v = std::fmod(left.v, right.v);
                break;
            }
            left.percent = false;
        }
        return left;
    }

    Value unary()
    {
        const Nest nest(*this);
        if (!nest.ok) return {};
        if (eat(u'-') || eat(u'−')) {
            Value v = unary();
            v.v = -v.v;
            return v;
        }
        if (eat(u'+')) return unary();
        return power();
    }

    Value power()
    {
        Value base = postfix();
        if (failed_) return base;
        skipSpace();
        bool isPower = false;
        if (at(pos_) == u'^') { ++pos_; isPower = true; }
        else if (at(pos_) == u'*' && at(pos_ + 1) == u'*') { pos_ += 2; isPower = true; }
        if (!isPower) return base;
        // Right-associative, and the exponent may be negative: 2^-1 is 0.5.
        const Value exponent = unary();
        ++operations_;
        ++binary_;
        return {std::pow(base.v, exponent.v), false};
    }

    Value postfix()
    {
        Value v = primary();
        while (!failed_) {
            if (eat(u'%')) {
                v.v /= 100;
                v.percent = true;
            } else if (eat(u'°')) {
                v.v *= std::numbers::pi / 180;   // sin(30°): the angle a person means
                v.percent = false;
            } else {
                break;
            }
            ++operations_;
        }
        return v;
    }

    Value primary()
    {
        skipSpace();
        if (atEnd()) return fail();
        const QChar c = s_[pos_];

        if (c == u'(') {
            ++pos_;
            Value v = expr();
            if (!eat(u')')) return fail();
            return v;
        }

        if (isCurrency(c)) {
            ++pos_;
            const QString symbol(c);
            if (currency_.isEmpty()) currency_ = symbol;
            else if (currency_ != symbol) mixedCurrency_ = true;
            skipSpace();
            if (!(at(pos_).isDigit() || at(pos_) == u'.')) return fail();
            return number();
        }

        if (c.isDigit() || c == u'.') return number();

        if (c.isLetter()) {
            const int start = pos_;
            while (!atEnd() && s_[pos_].isLetter()) ++pos_;
            const QString word = s_.mid(start, pos_ - start).toLower();

            if (word == QLatin1String("pi") || word == QStringLiteral("π")) {
                ++operations_;
                return {std::numbers::pi, false};
            }
            if (word == QLatin1String("e")) {
                ++operations_;
                return {std::numbers::e, false};
            }

            using Fn = double (*)(double);
            struct Named { const char* name; Fn fn; bool trig; };
            static const Named kFunctions[] = {
                {"sqrt",  [](double x) { return std::sqrt(x); }, false},
                {"abs",   [](double x) { return std::abs(x); }, false},
                {"round", [](double x) { return std::round(x); }, false},
                {"floor", [](double x) { return std::floor(x); }, false},
                {"ceil",  [](double x) { return std::ceil(x); }, false},
                {"ln",    [](double x) { return std::log(x); }, false},
                {"log",   [](double x) { return std::log10(x); }, false},
                {"sin",   [](double x) { return std::sin(x); }, true},
                {"cos",   [](double x) { return std::cos(x); }, true},
                {"tan",   [](double x) { return std::tan(x); }, true},
            };
            for (const auto& f : kFunctions) {
                if (word != QLatin1String(f.name)) continue;
                // The argument is one primary — "(1000)", "81", "pi" — so a
                // power or percent after the call applies to its result:
                // log(1000)^2 is 9, not log(1000^2). Without brackets the
                // argument keeps its own ° or %: sin 30° is sin(30°).
                const Nest nest(*this);
                if (!nest.ok) return {};
                skipSpace();
                const Value arg = at(pos_) == u'(' ? primary() : postfix();
                if (failed_) return {};
                ++operations_;
                trig_ = trig_ || f.trig;
                // log(0) is -inf and sqrt(-1) is NaN; run() reports either as
                // Undefined rather than writing "nan" into somebody's note.
                return {f.fn(arg.v), false};
            }
            return fail();
        }

        return fail();
    }

    // 1234, 1.5, .5, 1e-3, and grouped thousands as either 1,234,567 or the
    // Indian 12,34,567. A comma that fits neither pattern is not a number, so
    // "1,2" fails rather than silently becoming twelve.
    // Any script's digits — ५, ٣, １ — as their ASCII value, so that what
    // toDouble() refuses below can only be range, never an unreadable digit.
    void takeDigit(QString& digits)
    {
        digits += QChar(u'0' + s_[pos_++].digitValue());
    }

    Value number()
    {
        QString digits;
        const int intStart = pos_;
        while (!atEnd() && s_[pos_].isDigit()) takeDigit(digits);
        const int firstGroup = pos_ - intStart;

        // A leading zero is an identifier, not a quantity: the 01 of a date,
        // the 05 of 10:05, a zero-padded ID, and the 0 of 0x1F. Reading them
        // as numbers answered "2024-01-15" with 2008.
        if (firstGroup > 1 && digits[0] == u'0') return fail();
        if (digits == QLatin1String("0") && (at(pos_) == u'x' || at(pos_) == u'X')) return fail();

        if (firstGroup > 0 && at(pos_) == u',' && at(pos_ + 1).isDigit()) {
            QList<int> groups;
            while (at(pos_) == u',' && at(pos_ + 1).isDigit()) {
                ++pos_;
                int len = 0;
                while (!atEnd() && s_[pos_].isDigit()) { takeDigit(digits); ++len; }
                groups.append(len);
            }
            bool western = firstGroup <= 3;
            for (int g : groups) western = western && g == 3;
            bool indian = firstGroup <= 2 && groups.size() >= 2 && groups.last() == 3;
            for (int i = 0; i + 1 < groups.size(); ++i) indian = indian && groups[i] == 2;
            if (!western && !indian) return fail();
            if (grouping_.isNull()) grouping_ = western ? u'w' : u'i';
        }

        if (at(pos_) == u'.' && at(pos_ + 1).isDigit()) {
            digits += s_[pos_++];
            while (!atEnd() && s_[pos_].isDigit()) takeDigit(digits);
        }
        if (digits.isEmpty()) return fail();

        // An exponent only when a digit follows, so "2e" is not a number and
        // the constant e is never swallowed by the literal before it.
        const QChar e = at(pos_);
        if ((e == u'e' || e == u'E')
            && (at(pos_ + 1).isDigit()
                || ((at(pos_ + 1) == u'+' || at(pos_ + 1) == u'-') && at(pos_ + 2).isDigit()))) {
            digits += s_[pos_++];
            if (!at(pos_).isDigit()) digits += s_[pos_++];   // the exponent's sign
            while (!atEnd() && s_[pos_].isDigit()) takeDigit(digits);
        }

        // A letter straight after a number ("12px", "3rd") makes this a word,
        // not a quantity.
        if (at(pos_).isLetter() && at(pos_) != u'x' && at(pos_) != u'X') return fail();

        bool ok = false;
        const double v = digits.toDouble(&ok);   // QString::toDouble is always the C locale
        // The digits are well-formed by construction, so a failure here is
        // range: 1e400 is a number too large to hold, not a non-number.
        if (!ok) { semantic(Error::Undefined); return {HUGE_VAL, false}; }
        return {v, false};
    }
};

Parsed parse(const QString& expression)
{
    if (expression.size() > kMaxLine) return {};
    return Parser(expression).run();
}

QString group(const QString& integer, QChar style)
{
    if (style.isNull() || integer.size() <= 3) return integer;
    QString out = integer.right(3);
    int i = int(integer.size()) - 3;
    const int step = style == u'i' ? 2 : 3;
    while (i > 0) {
        const int take = std::min(step, i);
        out.prepend(integer.mid(i - take, take) + u',');
        i -= take;
    }
    return out;
}

// Where the content of a line begins, past any list marker: "- ", "* ", "+ ",
// "• ", "1. ", "2) ". A markdown bullet read as a minus sign turned
// "- 5 + 3 =" into -2.
int contentStart(const QString& line)
{
    int i = 0;
    while (i < line.size() && line[i].isSpace()) ++i;
    const int indent = i;
    if (i < line.size() && (line[i] == u'-' || line[i] == u'*' || line[i] == u'+' || line[i] == u'•'))
        ++i;
    else {
        while (i < line.size() && line[i].isDigit()) ++i;
        if (i == indent || i >= line.size() || (line[i] != u'.' && line[i] != u')')) return indent;
        ++i;
    }
    if (i >= line.size() || !line[i].isSpace()) return indent;
    while (i < line.size() && line[i].isSpace()) ++i;
    return i;
}

// Where a calculation can start. At the start of the content, or after
// whitespace that follows prose. Never straight after a character: "10:30",
// "0x1F", "(4+5". Never after a space that follows a digit, a bracket, an
// operator or a currency sign either: those mean the calculation began
// earlier, and if the whole of it did not parse, a tail of it is not the
// answer. That is what turned "1 500 + 250 =" into 750 and
// "(2+3)*(4+5 =" into 9.
bool canStartAt(const QString& line, int i, int content)
{
    if (i < content || line[i].isSpace()) return false;
    if (i == content) return true;
    if (!line[i - 1].isSpace()) return false;
    int p = i - 1;
    while (p >= content && line[p].isSpace()) --p;
    if (p < content) return true;
    const QChar prev = line[p];
    return !(prev.isDigit() || prev == u'.' || prev == u',' || prev == u'(' || prev == u')'
             || isOperator(prev) || isCurrency(prev));
}

struct Found {
    int start = -1;
    Evaluation evaluation;
};

// The longest run ending at `end` that parses and starts where a calculation
// can. Leftmost start wins, which is the longest.
Found trailingCalculation(const QString& line, int end)
{
    const int content = contentStart(line);
    for (int i = content; i < end; ++i) {
        if (!canStartAt(line, i, content)) continue;
        Parsed p = parse(line.mid(i, end - i));
        if (p.conclusive) return {};
        if (p.evaluation.error == Error::NotAnExpression) continue;
        if (p.evaluation.bareNumber) {
            p.evaluation.value.reset();
            p.evaluation.error = Error::NotAnExpression;
        }
        return {i, p.evaluation};
    }
    return {};
}

int trimmedEnd(const QString& s, int end)
{
    while (end > 0 && s[end - 1].isSpace()) --end;
    return end;
}

// What follows an "=": nothing (asking for an answer), or an answer already
// given. Anything else — "x = 5+3", "a = b" — is not an answer slot.
bool isAnswerSlot(const QString& afterEquals)
{
    const QString rhs = afterEquals.trimmed();
    if (rhs.isEmpty()) return true;
    const Evaluation shown = evaluate(rhs);
    return shown.value.has_value() && shown.bareNumber;
}

}  // namespace

Evaluation evaluate(const QString& expression)
{
    return parse(expression).evaluation;
}

QString format(const Evaluation& evaluation)
{
    if (!evaluation.value) return {};
    double v = *evaluation.value;
    if (v == 0) v = 0;   // no "-0"

    QString body;
    const bool negative = v < 0;
    const double magnitude = std::abs(v);
    if (magnitude != 0 && (magnitude >= 1e15 || magnitude < 1e-9)) {
        // Past what a double holds exactly, or smaller than any note needs:
        // scientific is the honest form.
        body = QString::number(magnitude, 'g', 12);
    } else if (magnitude == std::floor(magnitude)) {
        body = group(QString::number(qint64(magnitude)), evaluation.grouping);
    } else {
        // Twelve significant digits, or as many as the whole part needs up to
        // sixteen (what a double holds), so a large amount keeps its cents rather than switching to
        // 1.23456789012e+12.
        const int wholeDigits = magnitude < 1 ? 0 : int(std::floor(std::log10(magnitude))) + 1;
        const int significant = std::max(12, std::min(16, wholeDigits + 2));
        body = QString::number(magnitude, 'f', std::max(0, significant - wholeDigits));
        if (body.contains(u'.')) {
            while (body.endsWith(u'0')) body.chop(1);
            if (body.endsWith(u'.')) body.chop(1);
        }
        const int dot = int(body.indexOf(u'.'));
        const QString whole = dot < 0 ? body : body.left(dot);
        body = group(whole, evaluation.grouping) + (dot < 0 ? QString() : body.mid(dot));
    }
    // A negative answer that rounds to nothing is not "-0" — but $5 - $5 is
    // still "$0".
    const bool minus = negative && body != QLatin1String("0");
    return (minus ? QStringLiteral("-") : QString()) + evaluation.currency + body;
}

LineResult calculateLine(const QString& line, Scope scope)
{
    LineResult out;
    if (line.size() > kMaxLine) return out;

    // Replace [start, end of line) with prefix + the answer.
    const auto answer = [&line](const Found& f, int start, const QString& prefix) {
        LineResult r;
        r.error = Error::None;
        r.edit = LineEdit{start, int(line.size()) - start, prefix + format(f.evaluation)};
        return r;
    };

    // The line asks for an answer: it ends in "=", or what follows its last
    // "=" is a number — an answer from last time, to be recalculated.
    const int eq = int(line.lastIndexOf(u'='));
    if (eq >= 0 && isAnswerSlot(line.mid(eq + 1))) {
        const Found f = trailingCalculation(line, trimmedEnd(line, eq));
        if (f.evaluation.value) {
            const bool spaced = eq > 0 && line[eq - 1].isSpace();
            return answer(f, eq + 1, spaced ? QStringLiteral(" ") : QString());
        }
        if (f.start >= 0 && f.evaluation.error != Error::NotAnExpression) {
            out.error = f.evaluation.error;
            return out;
        }
    }
    if (scope == Scope::MarkedOnly) return out;

    // Anything else: a calculation at the end of the line gets " = answer" —
    // but only one with an operator in it. Without an "=" the line did not
    // ask, so "I like pi" and "check the log 100" are prose, not requests.
    const int end = trimmedEnd(line, int(line.size()));
    const Found f = trailingCalculation(line, end);
    if (f.evaluation.value && f.evaluation.hasOperator)
        return answer(f, end, QStringLiteral(" = "));
    if (f.start >= 0 && f.evaluation.error != Error::None) out.error = f.evaluation.error;
    return out;
}

LineResult calculateSelection(const QString& line, int from, int to)
{
    LineResult out;
    if (line.size() > kMaxLine || from < 0 || to > line.size() || from >= to) return out;
    const Evaluation ev = evaluate(line.mid(from, to - from));
    if (!ev.value) { out.error = ev.error; return out; }
    // Selecting the 3 of "Chapter 3" is not a calculation either.
    if (ev.bareNumber) return out;

    out.error = Error::None;
    const QString rest = line.mid(to);
    const int eqInRest = int(rest.indexOf(u'='));
    if (eqInRest >= 0 && rest.left(eqInRest).trimmed().isEmpty()
        && isAnswerSlot(rest.mid(eqInRest + 1))) {
        // "2+3 = 5" with 2+3 selected: the answer goes where the old one was.
        const int eq = to + eqInRest;
        const bool spaced = eq > 0 && line[eq - 1].isSpace();
        out.edit = LineEdit{eq + 1, int(line.size()) - (eq + 1),
                            (spaced ? QStringLiteral(" ") : QString()) + format(ev)};
    } else {
        out.edit = LineEdit{to, 0, QStringLiteral(" = ") + format(ev)};
    }
    return out;
}

}  // namespace napkin::calc
