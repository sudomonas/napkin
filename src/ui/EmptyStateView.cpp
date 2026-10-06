#include "EmptyStateView.h"
#include "Tokens.h"

#include <QEvent>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace napkin {
namespace {
using namespace tokens;
constexpr int kArtHeight = 128;

// A measure, not a pixel count: roughly 46 characters, which is a readable
// line at any font size. Fixed at 380px it was a comfortable column at the
// default font and a two-word-per-line ribbon at 200%.
int textColumn(const QFont& font)
{
    return QFontMetrics(font).averageCharWidth() * 46;
}
}  // namespace

EmptyStateView::EmptyStateView(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignHCenter);
    outer->setSpacing(0);
    // A little above the middle of the page, so the block sits at the middle of
    // the WINDOW: the page starts below the header, and centred in the page
    // alone it read as sagging low (user report, 2026-10-06).
    outer->addStretch(2);

    art_ = new QLabel;
    art_->setAlignment(Qt::AlignCenter);
    outer->addWidget(art_, 0, Qt::AlignHCenter);
    outer->addSpacing(20);

    title_ = new QLabel;
    title_->setAlignment(Qt::AlignCenter);
    title_->setWordWrap(true);
    title_->setFixedWidth(textColumn(title_->font()));
    title_->setFont(scaledBy(font(), kTypeTitle, QFont::Medium));
    outer->addWidget(title_);
    outer->addSpacing(6);

    detail_ = new QLabel;
    detail_->setAlignment(Qt::AlignCenter);
    detail_->setWordWrap(true);
    // Fixed, not merely bounded: a word-wrapped label asks for the narrowest
    // width it can survive at, so left to itself it wrapped a six-word sentence
    // onto two lines in the middle of an empty window.
    detail_->setFixedWidth(textColumn(detail_->font()));
    outer->addWidget(detail_);
    outer->addSpacing(22);

    action_ = new QPushButton;
    action_->setCursor(Qt::PointingHandCursor);
    action_->setObjectName(QStringLiteral("emptyStateAction"));
    // Named before it has any text. The button lives on a stack page that is
    // not current, so it is not "hidden" — it is simply unpopulated until the
    // state that needs it arrives, and until then it announced as nothing.
    action_->setAccessibleName(tr("Leave this view"));
    connect(action_, &QPushButton::clicked, this, &EmptyStateView::actionTriggered);
    outer->addWidget(action_, 0, Qt::AlignHCenter);
    outer->addStretch(3);

    applyPalette();
}

void EmptyStateView::setContent(const QString& artwork, const QString& title,
                                const QString& detail, const QString& actionLabel)
{
    artwork_ = artwork;
    drawArtwork();

    title_->setText(title);
    detail_->setText(detail);
    // The whole screen as one announcement, so a screen reader says why this
    // view is empty rather than reading three disconnected labels.
    setAccessibleName(title);
    setAccessibleDescription(detail);
    action_->setText(actionLabel);
    if (!actionLabel.isEmpty()) action_->setAccessibleName(actionLabel);
    action_->setVisible(!actionLabel.isEmpty());
}

void EmptyStateView::drawArtwork()
{
    if (artwork_.isEmpty()) {
        art_->clear();
        art_->hide();
        return;
    }
    // Loaded at twice the drawn size so it stays sharp on a HiDPI screen.
    QPixmap art(artwork_);
    if (!art.isNull()) art = art.scaledToHeight(kArtHeight, Qt::SmoothTransformation);
    art_->setPixmap(art);
    art_->setVisible(!art.isNull());
}

bool EmptyStateView::hasArtwork() const
{
    const QPixmap art = art_->pixmap();
    return !art.isNull();
}

void EmptyStateView::applyPalette()
{
    const QPalette pal = QGuiApplication::palette();
    auto tint = [&](QLabel* label, int alpha) {
        QPalette p = label->palette();
        // The role the label paints with, which is not always WindowText —
        // see the note in WelcomeView::applyPalette.
        p.setColor(label->foregroundRole(), text(pal, alpha));
        label->setPalette(p);
    };
    tint(title_, kTextPrimary);
    tint(detail_, kTextTertiary);
}

void EmptyStateView::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        applyPalette();
    if (e->type() == QEvent::FontChange || e->type() == QEvent::ApplicationFontChange) {
        title_->setFixedWidth(textColumn(title_->font()));
        detail_->setFixedWidth(textColumn(detail_->font()));
    }
    QWidget::changeEvent(e);
}

}  // namespace napkin
