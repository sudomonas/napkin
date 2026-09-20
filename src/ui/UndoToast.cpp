#include "UndoToast.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <utility>

namespace napkin {

UndoToast::UndoToast(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 10, 10);
    layout->setSpacing(14);

    message_ = new QLabel;
    auto* undo = new QPushButton(tr("Undo"));
    undoButton_ = undo;
    undo->setFlat(true);
    undo->setCursor(Qt::PointingHandCursor);
    undo->setAccessibleName(tr("Undo the last deletion"));
    undo->setToolTip(tr("Undo (Ctrl+Z)"));

    layout->addWidget(message_);
    layout->addWidget(undo);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(kVisibleMs);
    connect(timer_, &QTimer::timeout, this, &UndoToast::dismiss);

    connect(undo, &QPushButton::clicked, this, [this] { undoNow(); });

    hide();
}

bool UndoToast::undoNow()
{
    auto action = undo_;
    if (!action) return false;
    dismiss();
    action();
    emit undone();
    return true;
}

// An informational message reports something that destroyed nothing, so it must
// not cancel an offer the user may still be reaching for. Spelled
// `offer(message, nullptr)`, it did exactly that: a copy — or a paste that
// found nothing on the clipboard — silently threw away the Undo for a delete
// made two seconds earlier, and Ctrl+Z then did nothing. The offer is held,
// shown over for a moment, and put back with what is left of its countdown.
void UndoToast::inform(const QString& message)
{
    if (!undo_) { offer(message, nullptr); return; }

    if (heldMessage_.isEmpty()) {          // not already showing over an offer
        heldMessage_ = message_->text();
        heldMs_      = std::max(1, timer_->remainingTime());
    }
    timer_->stop();
    message_->setText(message);
    undoButton_->setVisible(false);
    adjustSize();
    reposition();
    show();
    raise();

    if (!informTimer_) {
        informTimer_ = new QTimer(this);
        informTimer_->setSingleShot(true);
        connect(informTimer_, &QTimer::timeout, this, [this] { restoreHeldOffer(); });
    }
    informTimer_->start(kInformMs);
}

void UndoToast::restoreHeldOffer()
{
    if (heldMessage_.isEmpty()) return;
    const QString message = std::exchange(heldMessage_, QString());
    const int remaining   = heldMs_;
    if (!undo_) { dismiss(); return; }     // it was taken up or dropped meanwhile
    message_->setText(message);
    undoButton_->setVisible(true);
    adjustSize();
    reposition();
    timer_->start(remaining);
}

void UndoToast::enterEvent(QEnterEvent* e)
{
    timer_->stop();
    if (informTimer_) informTimer_->stop();
    QWidget::enterEvent(e);
}

void UndoToast::leaveEvent(QEvent* e)
{
    // While an acknowledgement is standing in front of an offer, it is the
    // acknowledgement's short timer that has to resume — starting the offer's
    // would leave the wrong message on screen for eight seconds.
    if (!isVisible()) { QWidget::leaveEvent(e); return; }
    if (!heldMessage_.isEmpty() && informTimer_) informTimer_->start(kInformMs);
    else                                         timer_->start();
    QWidget::leaveEvent(e);
}

void UndoToast::offer(const QString& message, std::function<void()> undo)
{
    // A real offer supersedes anything being shown over the previous one; there
    // is nothing left to put back.
    heldMessage_.clear();
    if (informTimer_) informTimer_->stop();

    undo_ = std::move(undo);
    undoButton_->setVisible(bool(undo_));
    message_->setText(message);
    adjustSize();
    reposition();
    show();
    raise();
    timer_->start();
}

void UndoToast::dismiss()
{
    timer_->stop();
    heldMessage_.clear();
    if (informTimer_) informTimer_->stop();
    const bool had = bool(undo_);
    undo_ = nullptr;
    hide();
    // Expiring releases whatever the offer was holding, exactly as taking it up
    // would. Without this the blobs an abandoned offer protected are pinned
    // until the next delete.
    if (had) emit expired();
}

void UndoToast::reposition()
{
    if (!parentWidget()) return;
    const QSize s = sizeHint();
    move((parentWidget()->width() - s.width()) / 2, parentWidget()->height() - s.height() - 20);
    resize(s);
}

void UndoToast::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

    // Sits above the stack, so it needs its own surface rather than the card
    // fill — but still no shadow and no accent colour (SPEC.md §7).
    p.fillPath(path, palette().color(QPalette::Base));
    QColor border = palette().color(QPalette::Text);
    border.setAlpha(80);
    p.setPen(QPen(border, 1));
    p.drawPath(path);
}

}  // namespace napkin
