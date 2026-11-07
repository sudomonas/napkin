#include "LinkChip.h"
#include "../domain/Links.h"
#include "Icons.h"
#include "Tokens.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <QPushButton>
#include <QResizeEvent>
#include <QUrl>
#include <QVBoxLayout>

namespace napkin {
namespace {
using namespace tokens;
constexpr int kPad = 10;
constexpr int kGlyph = 18;
}  // namespace

LinkChip::LinkChip(QWidget* parent) : QWidget(parent)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(kPad + kGlyph + 10, kPad, kPad, kPad);
    row->setSpacing(10);

    auto* column = new QVBoxLayout;
    column->setSpacing(1);
    host_ = new QLabel;
    host_->setFont(scaledBy(font(), kTypeLead, QFont::DemiBold));
    // Elided like the path below, so it must be allowed narrower than its text.
    host_->setMinimumWidth(1);
    host_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rest_ = new QLabel;
    rest_->setFont(scaledBy(font(), kTypeCaption));
    column->addWidget(host_);
    column->addWidget(rest_);
    row->addLayout(column, 1);

    open_ = new QPushButton(tr("Open"));
    open_->setCursor(Qt::PointingHandCursor);
    open_->setObjectName(QStringLiteral("linkChipOpen"));
    open_->setFocusPolicy(Qt::TabFocus);
    connect(open_, &QPushButton::clicked, this, [this] { emit openRequested(url_); });
    row->addWidget(open_, 0, Qt::AlignVCenter);

    applyPalette();
}

void LinkChip::setUrl(const QString& url)
{
    url_ = url;
    const QString host = links::hostOf(url);
    // "www." says nothing about whose site it is and costs the room that does;
    // the tooltip and the accessible name keep the address whole.
    hostText_ = host.startsWith(QLatin1String("www.")) ? host.mid(4) : host;

    // The path, not the whole URL again: the host is already above it, and
    // repeating it wastes the only line that distinguishes two links to the
    // same site. Elision happens against the width the label actually gets,
    // not against a character count guessed here.
    const QUrl parsed(url, QUrl::StrictMode);
    path_ = parsed.path();
    if (parsed.hasQuery()) path_ += u'?' + parsed.query();
    if (path_ == QLatin1String("/")) path_.clear();
    rest_->setVisible(!path_.isEmpty());
    elidePath();

    // Screen readers get the address in full; the visual chip is a summary and
    // a summary is not enough to decide whether to follow a link.
    setAccessibleName(tr("Link to %1").arg(host));
    setAccessibleDescription(url);
    open_->setAccessibleName(tr("Open %1 in your browser").arg(host));
    setToolTip(url);
}

void LinkChip::elidePath()
{
    const int available = std::max(40, rest_->width());
    rest_->setText(QFontMetrics(rest_->font()).elidedText(path_, Qt::ElideMiddle, available));
    // The host was set whole and clipped by the label: "www.example.c", cut
    // mid-letter beside a neatly elided path (usability test, 2026-09-19).
    // Elided from the left, so the part that says whose site it is survives.
    const int hostRoom = std::max(40, host_->width());
    const QFontMetrics hostMetrics(host_->font());
    QString shown = hostText_;
    if (hostMetrics.horizontalAdvance(shown) > hostRoom) {
        // Drop whole labels from the left — "…example.com", never "…mple.com" —
        // and cut inside a label only if even the last two do not fit.
        const QStringList labels = hostText_.split(QLatin1Char('.'));
        shown.clear();
        for (int keep = int(labels.size()) - 1; keep >= 2 && shown.isEmpty(); --keep) {
            const QString candidate = QChar(0x2026) + labels.mid(labels.size() - keep).join(QLatin1Char('.'));
            if (hostMetrics.horizontalAdvance(candidate) <= hostRoom) shown = candidate;
        }
        if (shown.isEmpty()) shown = hostMetrics.elidedText(hostText_, Qt::ElideLeft, hostRoom);
    }
    host_->setText(shown);
}

void LinkChip::resizeEvent(QResizeEvent* e)
{
    elidePath();
    QWidget::resizeEvent(e);
}

// Derived from the font, not a constant. §14 asks for sensible text scaling,
// and a chip with a fixed 60px height clipped its own second line the moment
// the desktop font went up — the board measures cards from this number, so a
// stale one is a card too short to draw the chip it was sized for.
int LinkChip::preferredHeight(const QFont& base)
{
    // size(TextSingleLine), not height(): it is what a QLabel's size hint is
    // made of, and at fractional point sizes height() comes out a pixel short —
    // two labels, two pixels, and the host lost the top of its letters at 150%
    // board zoom. Measured across 7-24pt: size() matched the hint every time.
    const auto line = [](const QFont& f) {
        return QFontMetrics(f).size(Qt::TextSingleLine, QStringLiteral("Ag")).height();
    };
    const int host = line(scaledBy(base, kTypeLead, QFont::DemiBold));
    const int path = line(scaledBy(base, kTypeCaption));
    // Tall enough for a push button too: at small fonts the button, not the
    // text, is what sets the floor.
    const int button = QFontMetrics(base).height() + 14;
    return kPad * 2 + std::max(host + 1 + path, button);
}

void LinkChip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(text(pal, kHairline), 1.0));
    p.setBrush(text(pal, isLightTheme(pal) ? 10 : 16));
    p.drawRoundedRect(box, kInsetRadius, kInsetRadius);

    const QRect glyph(kPad, (height() - kGlyph) / 2, kGlyph, kGlyph);
    icons::drawLink(&p, glyph, text(pal, kTextSecondary));
}

void LinkChip::applyPalette()
{
    const QPalette pal = palette();
    auto tint = [&](QLabel* label, int alpha) {
        QPalette p = label->palette();
        p.setColor(label->foregroundRole(), text(pal, alpha));
        label->setPalette(p);
    };
    tint(host_, kTextPrimary);
    tint(rest_, kTextTertiary);
}

void LinkChip::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        applyPalette();
    if (e->type() == QEvent::FontChange || e->type() == QEvent::ApplicationFontChange) {
        host_->setFont(scaledBy(font(), kTypeLead, QFont::DemiBold));
        rest_->setFont(scaledBy(font(), kTypeCaption));
        elidePath();
    }
    QWidget::changeEvent(e);
}

}  // namespace napkin
