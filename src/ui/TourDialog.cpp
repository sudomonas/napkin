#include "TourDialog.h"
#include "Icons.h"
#include "NapkinStyle.h"
#include "SettingsDialog.h"
#include "Tokens.h"
#include "../app/GlobalShortcut.h"
#include "../domain/BufferService.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace napkin {

using namespace tokens;

namespace {
constexpr auto kSeenKey = "onboarding/tourSeen";

QPainterPath roundedPath(const QRectF& r, qreal radius)
{
    QPainterPath p;
    p.addRoundedRect(r, radius, radius);
    return p;
}

}  // namespace

// --- the pictures -------------------------------------------------------------
// Drawn, not screenshots: a miniature of the real thing in the theme's own
// colours, so it is right in Light and in Dark and never goes stale.

class Illustration : public QWidget {
public:
    explicit Illustration(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(320, 260);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setPicture(TourDialog::Picture p) { picture_ = p; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPalette& pal = palette();
        // A stage a step darker than the dialog, so the miniature desk and its
        // surfaces read as a picture rather than as more of the dialog.
        // In Dark a darkened text colour lands on the cards' own shade, so the
        // stage goes toward black there instead.
        p.fillPath(roundedPath(QRectF(rect()), kCardRadius),
                   isLightTheme(pal) ? text(pal, 14) : QColor(0, 0, 0, 80));
        switch (picture_) {
        case TourDialog::Picture::Board:     board(p, pal); break;
        case TourDialog::Picture::Paste:     paste(p, pal); break;
        case TourDialog::Picture::PinKeep:   pinKeep(p, pal); break;
        case TourDialog::Picture::Calculate: calculate(p, pal); break;
        case TourDialog::Picture::Search:    search(p, pal); break;
        case TourDialog::Picture::Trash:     trash(p, pal); break;
        case TourDialog::Picture::Anywhere:  anywhere(p, pal); break;
        }
    }

private:
    QFont scaled(qreal ratio, int weight = QFont::Normal) const
    {
        return scaledBy(font(), ratio, weight);
    }

    void card(QPainter& p, const QPalette& pal, const QRectF& r) const
    {
        p.fillPath(roundedPath(r, 8), pal.color(QPalette::Base));
    }

    // Grey bars standing for lines of text: the shape of a note, not words to read.
    void lines(QPainter& p, const QPalette& pal, QRectF r, int count) const
    {
        const qreal h = 6, gap = 9;
        for (int i = 0; i < count && r.top() + h <= r.bottom(); ++i) {
            const qreal w = r.width() * (i == count - 1 ? 0.55 : (i % 2 ? 0.85 : 1.0));
            p.fillPath(roundedPath(QRectF(r.left(), r.top(), w, h), 3), text(pal, 40));
            r.setTop(r.top() + h + gap);
        }
    }

    void keyChip(QPainter& p, const QPalette& pal, const QPointF& centre, const QString& keys) const
    {
        const QFont f = scaled(kTypeBody, QFont::DemiBold);
        p.setFont(f);
        const QFontMetricsF fm(f);
        const QRectF r(centre.x() - fm.horizontalAdvance(keys) / 2 - 12, centre.y() - fm.height() / 2 - 6,
                       fm.horizontalAdvance(keys) + 24, fm.height() + 12);
        p.fillPath(roundedPath(r, 7), pal.color(QPalette::Base));
        p.setPen(QPen(text(pal, kControlEdgeAlpha), 1.0));
        p.drawPath(roundedPath(r.adjusted(0.5, 0.5, -0.5, -0.5), 7));
        p.setPen(pal.color(QPalette::Text));
        p.drawText(r, Qt::AlignCenter, keys);
    }

    void words(QPainter& p, const QPalette& pal, const QRectF& r, const QString& s,
               int alpha = kTextPrimary, qreal ratio = kTypeBody, int weight = QFont::Normal,
               Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) const
    {
        p.setFont(scaled(ratio, weight));
        p.setPen(text(pal, alpha));
        p.drawText(r, align, s);
    }

    void board(QPainter& p, const QPalette& pal) const
    {
        const QRectF a(24, 28, 128, 96), b(168, 28, 128, 150), c(24, 140, 128, 92);
        card(p, pal, a);
        lines(p, pal, a.adjusted(14, 16, -14, -14), 4);
        card(p, pal, b);
        QLinearGradient g(b.topLeft(), b.bottomRight());
        g.setColorAt(0, QColor(86, 124, 200));
        g.setColorAt(1, QColor(226, 142, 92));
        p.fillPath(roundedPath(b.adjusted(5, 5, -5, -40), 5), g);
        lines(p, pal, QRectF(b.left() + 12, b.bottom() - 26, b.width() - 24, 12), 1);
        card(p, pal, c);
        lines(p, pal, c.adjusted(14, 16, -14, -14), 3);
    }

    void paste(QPainter& p, const QPalette& pal) const
    {
        keyChip(p, pal, QPointF(160, 62), QStringLiteral("Ctrl+V"));
        p.setPen(QPen(text(pal, 110), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(160, 92), QPointF(160, 120));
        p.drawLine(QPointF(153, 113), QPointF(160, 120));
        p.drawLine(QPointF(167, 113), QPointF(160, 120));
        const QRectF r(70, 134, 180, 96);
        card(p, pal, r);
        lines(p, pal, r.adjusted(16, 18, -16, -36), 3);
        words(p, pal, QRectF(r.left() + 16, r.bottom() - 28, 120, 20), tr("Copy text"),
              kTextTertiary, kTypeCaption);
    }

    void row(QPainter& p, const QPalette& pal, const QRectF& r, const QString& title,
             icons::GlyphPainter glyph, bool chosen) const
    {
        if (chosen) p.fillPath(roundedPath(r, 8), pal.color(QPalette::Window));
        words(p, pal, r.adjusted(14, 0, -34, 0), title, kTextPrimary, kTypeBody,
              chosen ? QFont::DemiBold : QFont::Medium);
        if (glyph) glyph(&p, QRect(int(r.right()) - 28, int(r.center().y()) - 7, 14, 14),
                         text(pal, 215));
    }

    void pinKeep(QPainter& p, const QPalette& pal) const
    {
        const QRectF panel(40, 24, 240, 212);
        p.fillPath(roundedPath(panel, kCardRadius), pal.color(QPalette::Base));
        words(p, pal, QRectF(panel.left() + 16, panel.top() + 12, 200, 18), tr("PINNED"),
              kTextTertiary, kTypeCaption, QFont::Bold);
        row(p, pal, QRectF(panel.left() + 8, panel.top() + 36, panel.width() - 16, 40),
            tr("Today's plan"), &icons::drawPin, true);
        words(p, pal, QRectF(panel.left() + 16, panel.top() + 88, 200, 18), tr("RECENT"),
              kTextTertiary, kTypeCaption, QFont::Bold);
        row(p, pal, QRectF(panel.left() + 8, panel.top() + 112, panel.width() - 16, 40),
            tr("Tax receipts"), &icons::drawKeep, false);
        row(p, pal, QRectF(panel.left() + 8, panel.top() + 156, panel.width() - 16, 40),
            tr("Shopping list"), nullptr, false);
    }

    void calculate(QPainter& p, const QPalette& pal) const
    {
        const QRectF before(30, 30, 260, 56), after(30, 168, 260, 56);
        card(p, pal, before);
        words(p, pal, before.adjusted(16, 0, -16, 0), QStringLiteral("rent 1200*12 ="));
        keyChip(p, pal, QPointF(160, 127), QStringLiteral("Ctrl+Tab"));
        card(p, pal, after);
        p.setFont(scaled(kTypeBody));
        const QString asked = QStringLiteral("rent 1200*12 = ");
        words(p, pal, after.adjusted(16, 0, -16, 0), asked);
        const qreal x = after.left() + 16 + QFontMetricsF(scaled(kTypeBody)).horizontalAdvance(asked);
        p.setPen(readableAccent(pal, 1.0));
        p.setFont(scaled(kTypeBody, QFont::DemiBold));
        p.drawText(QRectF(x, after.top(), 100, after.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("14400"));
    }

    void search(QPainter& p, const QPalette& pal) const
    {
        const QRectF field(30, 30, 260, 38);
        p.fillPath(roundedPath(field, 19), pal.color(QPalette::Base));
        icons::drawSearch(&p, QRect(int(field.left()) + 14, int(field.center().y()) - 8, 16, 16),
                          text(pal, kTextSecondary));
        words(p, pal, field.adjusted(40, 0, -10, 0), QStringLiteral("receipt"));
        const QRectF r(30, 92, 260, 140);
        card(p, pal, r);
        words(p, pal, QRectF(r.left() + 16, r.top() + 14, 220, 22), tr("Tax"), kTextPrimary,
              kTypeBody, QFont::DemiBold);
        const QString lead = tr("Lunch ");
        p.setFont(scaled(kTypeBody));
        const QFontMetricsF fm(p.font());
        const QRectF match(r.left() + 16 + fm.horizontalAdvance(lead), r.top() + 46,
                           fm.horizontalAdvance(QStringLiteral("receipt")) + 4, fm.height() + 2);
        p.fillPath(roundedPath(match, 3), highlight(pal, 70));
        words(p, pal, QRectF(r.left() + 16, r.top() + 46, 240, fm.height() + 2),
              lead + QStringLiteral("receipt") + tr(", 12 Sep"));
        lines(p, pal, QRectF(r.left() + 16, r.top() + 84, r.width() - 32, 40), 2);
    }

    void trash(QPainter& p, const QPalette& pal) const
    {
        const QPixmap bin(QStringLiteral(":/resources/icons/trash-empty-128.png"));
        if (!bin.isNull())
            p.drawPixmap(QRectF(120, 26, 80, 80), bin, QRectF(bin.rect()));
        // The toast that offers the way back.
        const QRectF toast(40, 150, 240, 46);
        p.fillPath(roundedPath(toast, kMenuRadius), pal.color(QPalette::Base));
        words(p, pal, toast.adjusted(16, 0, -90, 0), tr("Moved to the trash"), kTextPrimary,
              kTypeCaption);
        const QRectF undo(toast.right() - 78, toast.top() + 8, 70, 30);
        p.fillPath(roundedPath(undo, 15), pal.color(QPalette::Window));
        words(p, pal, undo, tr("Undo"), kTextPrimary, kTypeCaption, QFont::DemiBold, Qt::AlignCenter);
        words(p, pal, QRectF(0, 206, width(), 24), tr("or Ctrl+Z"), kTextTertiary, kTypeCaption,
              QFont::Normal, Qt::AlignCenter);
    }

    void anywhere(QPainter& p, const QPalette& pal) const
    {
        // Another app's window, and the chord that reaches past it.
        const QRectF other(26, 26, 170, 110);
        p.fillPath(roundedPath(other, 8), text(pal, 30));
        lines(p, pal, other.adjusted(16, 20, -16, -16), 4);
        keyChip(p, pal, QPointF(200, 158), QStringLiteral("Ctrl+Alt+V"));
        const QRectF r(150, 190, 144, 50);
        card(p, pal, r);
        lines(p, pal, r.adjusted(14, 14, -14, -10), 2);
    }

    TourDialog::Picture picture_ = TourDialog::Picture::Board;
};

// The step markers: one dot per step, the current one wider and in the text colour.
class Dots : public QWidget {
public:
    explicit Dots(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(12); }
    void set(int count, int current)
    {
        count_ = count;
        current_ = current;
        setFixedWidth(count * 14 + 12);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        qreal x = 0;
        for (int i = 0; i < count_; ++i) {
            const qreal w = i == current_ ? 20 : 8;
            p.fillPath(roundedPath(QRectF(x, 2, w, 8), 4),
                       text(palette(), i == current_ ? kTextPrimary : 60));
            x += w + 6;
        }
    }

private:
    int count_ = 0;
    int current_ = 0;
};

// --- the dialog ---------------------------------------------------------------

TourDialog::TourDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Welcome to Napkin"));
    setObjectName(QStringLiteral("tourDialog"));

    const int days = BufferService::trashRetentionDays();
    steps_ = {
        {Picture::Board, tr("Welcome to Napkin"),
         tr("A place to put things for now — text, links and screenshots. Nothing needs a "
            "name, a folder or a tag, and everything stays on this computer.<br><br>"
            "This tour takes a minute. You can open it again from the menu.")},
        {Picture::Paste, tr("Paste anything"),
         tr("Press <b>Ctrl+V</b> and what you copied lands on the napkin you have open, as a "
            "card. <b>Ctrl+N</b> starts a new napkin; <b>Ctrl+T</b> starts a note to type in."
            "<br><br>Every card has <b>Copy</b> at its foot, to take it back out in one click.")},
        {Picture::PinKeep, tr("Pin and Keep"),
         tr("<b>Pin</b> (<b>Ctrl+P</b>) holds a napkin at the top of the list.<br><br>"
            "<b>Keep</b> (<b>Ctrl+D</b>) protects it: Clean up never moves a kept napkin to "
            "the trash.<br><br>Both are on a napkin's right-click menu too.")},
        {Picture::Calculate, tr("Calculate as you write"),
         tr("In a note, end a line with <b>=</b> and press <b>Ctrl+Tab</b>: the answer is "
            "written after it. Right-click ▸ <b>Calculate</b> does the same.<br><br>"
            "Nothing is worked out until you ask, and one <b>Ctrl+Z</b> takes it back.")},
        {Picture::Search, tr("Find it again"),
         tr("<b>Ctrl+F</b> searches the text of every napkin and the names of pasted files. "
            "The board then shows only the cards that match, with the words marked.")},
        {Picture::Trash, tr("Nothing is lost by accident"),
         tr("Deleting moves a napkin to the <b>Trash</b>, where it stays for %1 days; "
            "<b>Ctrl+Z</b> brings back the last delete.<br><br>Napkins you have not touched "
            "in a while move under <b>Older</b>. <b>Clean up</b> suggests what to throw away — "
            "Napkin never deletes anything on its own.").arg(days)},
    };
    // Only where the desktop can do it: a step about a feature that cannot be
    // turned on here would be a promise the app cannot keep.
    if (GlobalShortcut::isSupported())
        steps_.push_back({Picture::Anywhere, tr("Catch things from any app"),
            tr("Turn on the shortcut in <b>Settings ▸ In the background</b>. Then copy "
               "something anywhere and press <b>Ctrl+Alt+V</b>: it lands on the napkin you "
               "have open, without leaving what you are doing.")});

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kHeaderGap, kHeaderGap, kHeaderGap, kWindowMargin);
    outer->setSpacing(kHeaderGap);
    outer->setSizeConstraint(QLayout::SetFixedSize);

    auto* panes = new QHBoxLayout;
    panes->setSpacing(kHeaderGap);
    picture_ = new Illustration;
    panes->addWidget(picture_, 0, Qt::AlignTop);

    auto* words = new QVBoxLayout;
    words->setSpacing(10);
    counter_ = new QLabel;
    counter_->setFont(scaledBy(font(), kTypeCaption, QFont::DemiBold));
    title_ = new QLabel;
    title_->setFont(scaledBy(font(), kTypeTitle, QFont::DemiBold));
    title_->setWordWrap(true);
    body_ = new QLabel;
    body_->setWordWrap(true);
    body_->setTextFormat(Qt::RichText);
    body_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // A fixed measure, so every step is the same size and the buttons do not
    // move under the pointer between steps.
    const int measure = QFontMetrics(font()).averageCharWidth() * 44;
    for (QLabel* l : {counter_, title_, body_}) l->setFixedWidth(measure);
    body_->setMinimumHeight(QFontMetrics(font()).lineSpacing() * 9);
    words->addWidget(counter_);
    words->addWidget(title_);
    words->addWidget(body_, 1);
    panes->addLayout(words);
    outer->addLayout(panes);

    auto* foot = new QHBoxLayout;
    skip_ = new QPushButton(tr("Skip tour"));
    skip_->setFlat(true);
    skip_->setProperty(NapkinStyle::kShapeProperty, QStringLiteral("pill"));
    skip_->setAutoDefault(false);
    dots_ = new Dots;
    back_ = new QPushButton(tr("Back"));
    back_->setAutoDefault(false);
    next_ = new QPushButton;
    next_->setDefault(true);
    foot->addWidget(skip_);
    foot->addStretch();
    foot->addWidget(dots_, 0, Qt::AlignVCenter);
    foot->addStretch();
    foot->addWidget(back_);
    foot->addWidget(next_);
    outer->addLayout(foot);

    connect(skip_, &QPushButton::clicked, this, &QDialog::reject);
    connect(back_, &QPushButton::clicked, this, [this] { showStep(step_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this] {
        if (step_ + 1 < stepCount()) showStep(step_ + 1);
        else accept();
    });

    // Quieter text for the step counter and the body than for the title.
    QPalette quiet = counter_->palette();
    quiet.setColor(counter_->foregroundRole(), text(quiet, kTextTertiary));
    counter_->setPalette(quiet);
    showStep(0);
}

void TourDialog::showStep(int index)
{
    step_ = std::clamp(index, 0, stepCount() - 1);
    const Step& s = steps_[size_t(step_)];
    picture_->setPicture(s.picture);
    counter_->setText(tr("%1 of %2").arg(step_ + 1).arg(stepCount()));
    title_->setText(s.title);
    body_->setText(s.body);
    dots_->set(stepCount(), step_);
    back_->setVisible(step_ > 0);
    const bool last = step_ + 1 == stepCount();
    next_->setText(last ? tr("Start using Napkin") : tr("Next"));
    skip_->setVisible(!last);
    setAccessibleDescription(s.title);
}

void TourDialog::keyPressEvent(QKeyEvent* e)
{
    // The arrow keys step through, as in any slideshow.
    if (e->key() == Qt::Key_Right) { if (step_ + 1 < stepCount()) showStep(step_ + 1); return; }
    if (e->key() == Qt::Key_Left)  { showStep(step_ - 1); return; }
    QDialog::keyPressEvent(e);
}

void TourDialog::done(int result)
{
    markSeen();   // finished, skipped or closed: it does not come back unasked
    QDialog::done(result);
}

bool TourDialog::seen() { return QSettings().value(kSeenKey, false).toBool(); }
void TourDialog::markSeen() { QSettings().setValue(kSeenKey, true); }

}  // namespace napkin
